cmake_minimum_required(VERSION 3.20)
get_filename_component(KORAOS_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_TOOLCHAIN_FILE "${KORAOS_SOURCE_ROOT}/cmake/aarch64-toolchain.cmake"
    CACHE PATH "AArch64 toolchain file")
project(KoraOSUserFS C ASM)
include("${KORAOS_SOURCE_ROOT}/cmake/userfs.cmake")
koraos_add_userfs("${KORAOS_SOURCE_ROOT}" "${CMAKE_BINARY_DIR}/koraos.img")
