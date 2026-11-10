# Minimal libc. For now it is configured with the kernel (add_subdirectory from
# the top-level CMakeLists.txt); it is meant to be split into its own project.
#
#   koraos_libc  the AArch64 static library (not part of the default build)
#   libc_tests   builds and runs the unit tests on the host under ASan/UBSan
#
#   cmake --build build/<dir> --target libc_tests

cmake_minimum_required(VERSION 3.20)

set(LIBC_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(LIBC_SRC_DIR "${LIBC_DIR}/src")
set(LIBC_INCLUDE_DIR "${LIBC_DIR}/includes")
set(LIBC_INTERNAL_DIR "${LIBC_SRC_DIR}/internal")
file(GLOB_RECURSE LIBC_SOURCES CONFIGURE_DEPENDS "${LIBC_SRC_DIR}/*.c" "${LIBC_SRC_DIR}/*.S")

add_library(koraos_libc STATIC EXCLUDE_FROM_ALL ${LIBC_SOURCES})
target_include_directories(koraos_libc PUBLIC "${LIBC_INCLUDE_DIR}")
target_include_directories(koraos_libc PRIVATE "${LIBC_INTERNAL_DIR}")
target_compile_options(koraos_libc PRIVATE
    --target=aarch64-none-elf -mcpu=cortex-a72
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector
    -mgeneral-regs-only -Wall -Wextra)

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
