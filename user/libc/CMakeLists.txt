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
    libc_host_test(ctype SOURCES src/ctype/ctype.c)
    libc_host_test(stdlib_convert SOURCES src/stdlib/convert.c src/ctype/ctype.c)
    libc_host_test(binary128)
    libc_host_test(stdlib_exit SOURCES src/stdlib/exit.c ${LIBC_FAKE_KERNEL})
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
