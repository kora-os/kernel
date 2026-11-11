# Minimal libc. Configured either standalone (what tests/run-libc-tests.sh and
# CI do) or with the kernel (add_subdirectory from the top-level CMakeLists.txt).
#
#   koraos_libc       the AArch64 static library
#   libc_link_check   links the whole library into a user-style PIE, so an
#                     undefined symbol (such as a compiler-rt helper the
#                     compiler called behind our back) fails the build
#   libc_tests        builds and runs the unit tests on the host under ASan/UBSan
#
#   cmake -S user/libc -B build/libc
#   cmake --build build/libc --target koraos_libc libc_link_check libc_tests

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../../cmake/aarch64-toolchain.cmake"
        CACHE PATH "AArch64 toolchain file")
    project(KoraOSLibc C ASM)
    set(LIBC_STANDALONE ON)
endif()

# The libc's flags are fixed below (like the user programs' in
# cmake/userfs.cmake), so the archive, and every program linking it, is the
# same in every build type: drop the per-configuration defaults here.
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_C_FLAGS_${config} "")
    set(CMAKE_ASM_FLAGS_${config} "")
endforeach()

set(LIBC_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(LIBC_SRC_DIR "${LIBC_DIR}/src")
set(LIBC_INCLUDE_DIR "${LIBC_DIR}/includes")
set(LIBC_INTERNAL_DIR "${LIBC_SRC_DIR}/internal")
file(GLOB_RECURSE LIBC_SOURCES CONFIGURE_DEPENDS "${LIBC_SRC_DIR}/*.c" "${LIBC_SRC_DIR}/*.S")

# Standalone, the library is part of `all`; inside the kernel build it is only
# built on request.
if(LIBC_STANDALONE)
    add_library(koraos_libc STATIC ${LIBC_SOURCES})
else()
    add_library(koraos_libc STATIC EXCLUDE_FROM_ALL ${LIBC_SOURCES})
endif()
target_include_directories(koraos_libc PUBLIC "${LIBC_INCLUDE_DIR}")
target_include_directories(koraos_libc PRIVATE "${LIBC_INTERNAL_DIR}")
# Position independent (it links into PIE user programs) and allowed to use
# FP/SIMD: the kernel switches those registers lazily per task. There is no
# compiler-rt, so libc code must not do arithmetic in long double (binary128);
# libc_link_check catches it if it does.
set(LIBC_TARGET_FLAGS
    --target=aarch64-none-elf -mcpu=cortex-a72
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fPIE
    -std=c11 -O2 -Wall -Wextra -Werror)
target_compile_options(koraos_libc PRIVATE ${LIBC_TARGET_FLAGS})

# The user program link flags (cmake/userfs.cmake), with the whole archive
# pulled in: lld rejects any symbol the library uses but does not define.
set(LIBC_LINK_CHECK_ELF "${CMAKE_CURRENT_BINARY_DIR}/libc_link_check.elf")
add_custom_command(
    OUTPUT "${LIBC_LINK_CHECK_ELF}"
    COMMAND ${CMAKE_C_COMPILER} ${LIBC_TARGET_FLAGS}
            -fuse-ld=lld -Wl,-pie -Wl,-e,_start -Wl,--pack-dyn-relocs=none
            -Wl,-z,norelro -Wl,-z,noexecstack -Wl,-z,max-page-size=4096
            "${LIBC_DIR}/tests/link_check.c"
            -Wl,--whole-archive "$<TARGET_FILE:koraos_libc>" -Wl,--no-whole-archive
            -o "${LIBC_LINK_CHECK_ELF}"
    DEPENDS koraos_libc "${LIBC_DIR}/tests/link_check.c"
    COMMENT "Linking the whole libc into a PIE (undefined symbol check)"
    VERBATIM)
if(LIBC_STANDALONE)
    add_custom_target(libc_link_check ALL DEPENDS "${LIBC_LINK_CHECK_ELF}")
else()
    add_custom_target(libc_link_check DEPENDS "${LIBC_LINK_CHECK_ELF}")
endif()

# Every public header must compile on its own (and twice, for the include
# guards) for AArch64, in C99, C11 and C17, with pedantic errors: MicroPython
# and the eventual on-OS compiler build C99. The headers' static assertions
# (tests/headers_static_checks.h) are also compiled for the target here.
file(GLOB LIBC_PUBLIC_HEADERS CONFIGURE_DEPENDS "${LIBC_INCLUDE_DIR}/*.h")
set(LIBC_HEADER_CHECK_DIR "${CMAKE_CURRENT_BINARY_DIR}/header-check")
set(LIBC_HEADER_STAMPS)
foreach(header IN LISTS LIBC_PUBLIC_HEADERS)
    get_filename_component(name "${header}" NAME_WE)
    set(src "${LIBC_HEADER_CHECK_DIR}/${name}.c")
    file(WRITE "${src}.in" "#include <${name}.h>\n#include <${name}.h>\nint libc_header_check_${name};\n")
    configure_file("${src}.in" "${src}" COPYONLY)
    foreach(std c99 c11 c17)
        set(stamp "${LIBC_HEADER_CHECK_DIR}/${name}.${std}.stamp")
        add_custom_command(
            OUTPUT "${stamp}"
            COMMAND ${CMAKE_C_COMPILER} --target=aarch64-none-elf -ffreestanding -nostdinc
                    -std=${std} -pedantic-errors -Wall -Wextra -Werror
                    -isystem "${LIBC_INCLUDE_DIR}" -fsyntax-only "${src}"
            COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
            DEPENDS "${src}" ${LIBC_PUBLIC_HEADERS}
            COMMENT "Header check: <${name}.h> in ${std}"
            VERBATIM)
        list(APPEND LIBC_HEADER_STAMPS "${stamp}")
    endforeach()
endforeach()
set(LIBC_STATIC_CHECKS_SRC "${LIBC_HEADER_CHECK_DIR}/static_checks.c")
file(WRITE "${LIBC_STATIC_CHECKS_SRC}.in"
    "#include <float.h>\n#include <limits.h>\n#include <stdalign.h>\n#include <stdbool.h>\n"
    "#include <stddef.h>\n#include <stdint.h>\n#include \"headers_static_checks.h\"\n")
configure_file("${LIBC_STATIC_CHECKS_SRC}.in" "${LIBC_STATIC_CHECKS_SRC}" COPYONLY)
set(stamp "${LIBC_HEADER_CHECK_DIR}/static_checks.stamp")
add_custom_command(
    OUTPUT "${stamp}"
    COMMAND ${CMAKE_C_COMPILER} --target=aarch64-none-elf -ffreestanding -nostdinc -std=c11
            -Wall -Wextra -Werror -isystem "${LIBC_INCLUDE_DIR}" -I "${LIBC_DIR}/tests"
            -fsyntax-only "${LIBC_STATIC_CHECKS_SRC}"
    COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
    DEPENDS "${LIBC_STATIC_CHECKS_SRC}" "${LIBC_DIR}/tests/headers_static_checks.h"
            ${LIBC_PUBLIC_HEADERS}
    COMMENT "Header check: static assertions on AArch64"
    VERBATIM)
list(APPEND LIBC_HEADER_STAMPS "${stamp}")
if(LIBC_STANDALONE)
    add_custom_target(libc_header_check ALL DEPENDS ${LIBC_HEADER_STAMPS})
else()
    add_custom_target(libc_header_check DEPENDS ${LIBC_HEADER_STAMPS})
endif()

# Unit tests run on the host, so they need the host compiler rather than the
# cross compiler selected by the toolchain file. The libc headers come first on
# the include path and shadow the host's; host_rename.h renames the libc symbols
# so they cannot clash with the host libc or the sanitizer's interceptors.
# Code that makes syscalls links tests/fake_syscall.c, a fake kernel, in place
# of src/sys/syscall.S.
find_program(KORAOS_HOST_CC NAMES clang cc
    PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)

if(KORAOS_HOST_CC)
    set(LIBC_TEST_DIR "${LIBC_DIR}/tests")
    set(LIBC_TEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/tests")
    set(LIBC_HOST_FLAGS
        -std=c11 -g -O1 -Wall -Wextra -Werror
        -fno-builtin -fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -I "${LIBC_INCLUDE_DIR}" -I "${LIBC_TEST_DIR}" -I "${LIBC_INTERNAL_DIR}"
        -include "${LIBC_TEST_DIR}/host_rename.h")

    set(LIBC_TEST_BUILD_COMMANDS)
    set(LIBC_TEST_RUN_COMMANDS)

    # libc_host_test(<name> SOURCES <files relative to user/libc>... [FLAGS <flags>...])
    # builds tests/<name>_test.c with the given sources and runs it.
    function(libc_host_test name)
        cmake_parse_arguments(T "" "" "SOURCES;FLAGS" ${ARGN})
        set(sources)
        foreach(src IN LISTS T_SOURCES)
            list(APPEND sources "${LIBC_DIR}/${src}")
        endforeach()
        set(exe "${LIBC_TEST_OUT}/${name}_test")
        set(LIBC_TEST_BUILD_COMMANDS ${LIBC_TEST_BUILD_COMMANDS}
            COMMAND "${KORAOS_HOST_CC}" ${LIBC_HOST_FLAGS} ${T_FLAGS}
                    -o "${exe}" "${LIBC_TEST_DIR}/${name}_test.c" ${sources}
            PARENT_SCOPE)
        set(LIBC_TEST_RUN_COMMANDS ${LIBC_TEST_RUN_COMMANDS} COMMAND "${exe}" PARENT_SCOPE)
    endfunction()

    set(LIBC_FAKE_KERNEL tests/fake_syscall.c)
    set(LIBC_MALLOC_SOURCES src/stdlib/malloc.c src/string/string.c ${LIBC_FAKE_KERNEL})

    libc_host_test(string SOURCES src/string/string.c)
    libc_host_test(string_search SOURCES src/string/search.c src/string/string.c)
    libc_host_test(ctype SOURCES src/ctype/ctype.c)
    libc_host_test(stdlib_convert SOURCES src/stdlib/convert.c src/stdlib/strtod.c src/ctype/ctype.c
                   src/string/string.c src/errno/errno.c)
    libc_host_test(binary128)
    libc_host_test(headers)
    libc_host_test(errno SOURCES src/errno/errno.c src/string/strerror.c src/string/string.c)
    libc_host_test(stdlib_exit SOURCES src/stdlib/exit.c ${LIBC_FAKE_KERNEL})
    libc_host_test(stdlib_sort SOURCES src/stdlib/sort.c src/string/string.c)
    libc_host_test(stdlib_misc SOURCES src/stdlib/integer.c src/stdlib/rand.c src/stdlib/getenv.c)
    libc_host_test(stdio_snprintf SOURCES src/stdio/snprintf.c src/stdio/format.c
                   src/stdio/format_float.c src/string/string.c src/errno/errno.c FLAGS -Wno-format)
    set(LIBC_STDIO_SOURCES src/stdio/file.c src/stdio/fwrite.c src/stdio/printf.c
        src/stdio/fopen.c src/stdio/fread.c src/stdio/fseek.c src/stdio/remove.c
        src/stdio/format.c src/stdio/format_float.c src/stdio/snprintf.c src/string/string.c
        src/string/search.c src/string/strerror.c src/errno/errno.c src/stdlib/exit.c
        src/stdlib/malloc.c ${LIBC_FAKE_KERNEL})
    libc_host_test(stdio_write SOURCES ${LIBC_STDIO_SOURCES} FLAGS -pthread -Wno-format)
    libc_host_test(stdio_file SOURCES ${LIBC_STDIO_SOURCES})
    libc_host_test(stdlib_strtod SOURCES src/stdlib/strtod.c src/ctype/ctype.c src/string/string.c
                   src/errno/errno.c)
    libc_host_test(malloc SOURCES ${LIBC_MALLOC_SOURCES})
    libc_host_test(malloc_threads SOURCES ${LIBC_MALLOC_SOURCES} FLAGS -pthread)

    file(GLOB_RECURSE LIBC_TEST_DEPENDS CONFIGURE_DEPENDS
        "${LIBC_TEST_DIR}/*.c" "${LIBC_TEST_DIR}/*.h"
        "${LIBC_SRC_DIR}/*.c" "${LIBC_SRC_DIR}/*.h" "${LIBC_INCLUDE_DIR}/*.h")

    file(MAKE_DIRECTORY "${LIBC_TEST_OUT}")
    add_custom_target(libc_tests
        ${LIBC_TEST_BUILD_COMMANDS}
        ${LIBC_TEST_RUN_COMMANDS}
        DEPENDS ${LIBC_TEST_DEPENDS}
        COMMENT "Building and running the libc unit tests on the host"
        VERBATIM)
else()
    message(STATUS "No host C compiler found; the libc_tests target is unavailable")
endif()
