# Minimal libc. For now it is configured with the kernel (add_subdirectory from
# the top-level CMakeLists.txt); it is meant to be split into its own project.
#
#   koraos_libc  the AArch64 static library (not part of the default build)
#   libc_tests   builds and runs the unit tests on the host under ASan/UBSan
#
#   cmake --build build/<dir> --target libc_tests

cmake_minimum_required(VERSION 3.20)

file(GLOB LIBC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.c")
set(LIBC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/includes")

add_library(koraos_libc STATIC EXCLUDE_FROM_ALL ${LIBC_SOURCES})
target_include_directories(koraos_libc PUBLIC "${LIBC_INCLUDE_DIR}")
target_compile_options(koraos_libc PRIVATE
    --target=aarch64-none-elf -mcpu=cortex-a72
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector
    -mgeneral-regs-only -Wall -Wextra)

# Unit tests run on the host, so they need the host compiler rather than the
# cross compiler selected by the toolchain file. The libc headers come first on
# the include path and shadow the host's; host_rename.h renames the libc symbols
# so they cannot clash with the host libc or the sanitizer's interceptors.
find_program(KORAOS_HOST_CC NAMES clang cc
    PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)

if(KORAOS_HOST_CC)
    set(LIBC_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/tests")
    set(LIBC_TEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/tests")
    set(LIBC_HOST_FLAGS
        -std=c11 -g -O1 -Wall -Wextra -Werror
        -fno-builtin -fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -I "${LIBC_INCLUDE_DIR}" -I "${LIBC_TEST_DIR}"
        -include "${LIBC_TEST_DIR}/host_rename.h")

    file(MAKE_DIRECTORY "${LIBC_TEST_OUT}")
    add_custom_target(libc_tests
        COMMAND "${KORAOS_HOST_CC}" ${LIBC_HOST_FLAGS}
                -o "${LIBC_TEST_OUT}/string_test"
                "${LIBC_TEST_DIR}/string_test.c" "${CMAKE_CURRENT_LIST_DIR}/string.c"
        COMMAND "${LIBC_TEST_OUT}/string_test"
        DEPENDS "${LIBC_TEST_DIR}/string_test.c" "${LIBC_TEST_DIR}/test.h"
                "${LIBC_TEST_DIR}/host_rename.h"
                "${CMAKE_CURRENT_LIST_DIR}/string.c" "${LIBC_INCLUDE_DIR}/string.h"
        COMMENT "Building and running the libc unit tests on the host"
        VERBATIM)
else()
    message(STATUS "No host C compiler found; the libc_tests target is unavailable")
endif()
