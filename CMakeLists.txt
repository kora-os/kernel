cmake_minimum_required(VERSION 3.20)

# Set the toolchain file before project()
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/aarch64-toolchain.cmake" CACHE PATH "Toolchain file")

project(KoraOS C ASM)

# Enable compile_commands.json generation for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build configuration
set(RPI_VERSION "4" CACHE STRING "Raspberry Pi version (3 or 4)")

option(BUILD_QEMU_VARIANT "Build kernel image configured for QEMU" ON)
option(BUILD_HW_VARIANT "Build kernel image configured for physical hardware" ON)

set(BOOTMNT "/workspace/build/boot" CACHE PATH "Boot mount directory")

# Directories
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/lib")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LINKER_SCRIPT "${SRC_DIR}/linker.ld")

# Gather sources from src tree (including subdirectories) and src/lib
file(GLOB_RECURSE KERNEL_C_SOURCES "${SRC_DIR}/*.c")
file(GLOB_RECURSE KERNEL_ASM_SOURCES "${SRC_DIR}/*.S" "${SRC_DIR}/*.s")
file(GLOB LIB_C_SOURCES "${LIB_DIR}/*.c")
file(GLOB LIB_ASM_SOURCES "${LIB_DIR}/*.S" "${LIB_DIR}/*.s")

set(KERNEL_SOURCES ${KERNEL_C_SOURCES} ${KERNEL_ASM_SOURCES} ${LIB_C_SOURCES} ${LIB_ASM_SOURCES})

# ---------------------------------------------------------------------------
# User programs: standalone position-independent ELF executables built with the
# same cross toolchain but their own flags and linker settings, then embedded
# into the kernel image as read-only blobs (looked up via src/user/embedded.c).
# ---------------------------------------------------------------------------
set(USER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/user")
set(USER_BUILD_DIR "${CMAKE_BINARY_DIR}/user")
file(MAKE_DIRECTORY "${USER_BUILD_DIR}")

# Runtime linked into every user program: entry stub + syscall wrappers.
set(USER_RUNTIME_SOURCES
    "${USER_DIR}/crt0.S"
    "${USER_DIR}/libk/syscall.S"
)

set(USER_COMPILE_FLAGS
    --target=aarch64-none-elf
    -mcpu=cortex-a72
    -ffreestanding
    -nostdlib
    -fno-builtin
    -fno-stack-protector
    -mgeneral-regs-only
    -fPIE
    -O2
    -Wall
    "-I${USER_DIR}"
)

# -pie produces an ET_DYN image; --pack-dyn-relocs=none forces classic DT_RELA
# (only R_AARCH64_RELATIVE entries) so the Step 3 loader needs a single
# relocation path -- no packed DT_RELR.
set(USER_LINK_FLAGS
    -nostdlib
    -fuse-ld=lld
    -Wl,-pie
    -Wl,-e,_start
    -Wl,--pack-dyn-relocs=none
    -Wl,-z,norelro
    -Wl,-z,noexecstack
    # Pack segments on 4 KB instead of the default 64 KB boundaries, so the
    # loaded image spans a handful of pages rather than tens of them.
    -Wl,-z,max-page-size=4096
)

set(USER_PROGRAM_NAMES "")
set(USER_PROGRAM_ELVES "")

# add_user_program(<name> <source>...) builds user/<...> into
# ${USER_BUILD_DIR}/<name>.elf and records it for embedding.
function(add_user_program NAME)
    set(elf "${USER_BUILD_DIR}/${NAME}.elf")
    add_custom_command(
        OUTPUT "${elf}"
        COMMAND ${CMAKE_C_COMPILER} ${USER_COMPILE_FLAGS} ${USER_LINK_FLAGS}
                ${USER_RUNTIME_SOURCES} ${ARGN} -o "${elf}"
        DEPENDS ${USER_RUNTIME_SOURCES} ${ARGN}
        COMMENT "Building user program ${NAME}.elf (PIE)"
        VERBATIM
    )
    set(_names ${USER_PROGRAM_NAMES})
    set(_elves ${USER_PROGRAM_ELVES})
    list(APPEND _names "${NAME}")
    list(APPEND _elves "${elf}")
    set(USER_PROGRAM_NAMES "${_names}" PARENT_SCOPE)
    set(USER_PROGRAM_ELVES "${_elves}" PARENT_SCOPE)
endfunction()

add_user_program(hello "${USER_DIR}/hello.c")
add_user_program(init "${USER_DIR}/init.c")
add_user_program(shell "${USER_DIR}/shell.c")
add_user_program(echo "${USER_DIR}/echo.c")
add_user_program(gfxdemo "${USER_DIR}/gfxdemo.c")
add_user_program(ls "${USER_DIR}/ls.c")
add_user_program(cat "${USER_DIR}/cat.c")

# ---------------------------------------------------------------------------
# Filesystem ramdisk: a FAT32 image built on the host by create-fs-image.sh
# (mtools) and embedded into the kernel as a read-only blob, served by
# src/fs/blkdev.c. The userland programs above are installed into /bin on the
# image and loaded from there at runtime (src/proc/task.c) -- there is no
# separate embedded-blob registry.
# ---------------------------------------------------------------------------
set(FS_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fsroot")
set(FS_IMAGE "${CMAKE_BINARY_DIR}/fs/koraos.img")
set(FS_IMAGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/create-fs-image.sh")

# Rebuild the image whenever any shipped file, any user program, or the builder
# changes.
file(GLOB_RECURSE FS_ROOT_FILES "${FS_ROOT_DIR}/*")

add_custom_command(
    OUTPUT "${FS_IMAGE}"
    COMMAND "${FS_IMAGE_SCRIPT}" --fsroot "${FS_ROOT_DIR}" --output "${FS_IMAGE}"
            --bindir "${USER_BUILD_DIR}"
    DEPENDS ${FS_ROOT_FILES} ${USER_PROGRAM_ELVES} "${FS_IMAGE_SCRIPT}"
    COMMENT "Building FAT32 ramdisk image koraos.img"
    VERBATIM
)

set(FS_BLOB_ASM "${CMAKE_BINARY_DIR}/fs/fs_blob.S")
file(GENERATE OUTPUT "${FS_BLOB_ASM}" CONTENT
"// Generated by CMake. Do not edit. Embeds the FAT32 ramdisk image.\n\n.section .rodata.koraosfs,\"a\"\n.balign 8\n.global _koraos_fs_start\n_koraos_fs_start:\n    .incbin \"${FS_IMAGE}\"\n.global _koraos_fs_end\n_koraos_fs_end:\n")

# Reassemble the blob (and so relink) whenever the image changes, and make sure
# the image is produced before the blob is assembled.
set_source_files_properties("${FS_BLOB_ASM}" PROPERTIES
    GENERATED TRUE
    OBJECT_DEPENDS "${FS_IMAGE}"
)

list(APPEND KERNEL_SOURCES "${FS_BLOB_ASM}")

# Common compiler flags
set(COMMON_FLAGS
    --target=aarch64-none-elf
    -mcpu=cortex-a72
    -ffreestanding
    -nostdlib
    -fno-builtin
    -fno-stack-protector
)

# C-specific flags
set(C_FLAGS
    ${COMMON_FLAGS}
    -Wall
    -mgeneral-regs-only
    -DRPI_VERSION=${RPI_VERSION}
    -DPRINTF_LONG_SUPPORT
)

# Assembly flags
set(ASM_FLAGS
    ${COMMON_FLAGS}
)

include(CMakeParseArguments)

if(NOT OBJCOPY)
    find_program(OBJCOPY llvm-objcopy)
endif()

if(NOT OBJCOPY)
    message(FATAL_ERROR "llvm-objcopy not found. Install the LLVM toolchain or provide OBJCOPY explicitly.")
endif()

function(add_kernel_variant VARIANT)
    set(options QEMU)
    set(oneValueArgs OUTPUT_NAME IMAGE_NAME BOOT_IMAGE_NAME MAP_NAME)
    cmake_parse_arguments(KVAR "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT KVAR_OUTPUT_NAME)
        set(KVAR_OUTPUT_NAME ${VARIANT})
    endif()

    if(NOT KVAR_IMAGE_NAME)
        set(KVAR_IMAGE_NAME "${KVAR_OUTPUT_NAME}.img")
    endif()

    if(NOT KVAR_BOOT_IMAGE_NAME)
        set(KVAR_BOOT_IMAGE_NAME ${KVAR_IMAGE_NAME})
    endif()

    if(NOT KVAR_MAP_NAME)
        set(KVAR_MAP_NAME "${KVAR_OUTPUT_NAME}.map")
    endif()

    set(target "kernel_${VARIANT}")
    add_executable(${target}
        ${KERNEL_SOURCES}
    )

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${KVAR_OUTPUT_NAME}
        SUFFIX ".elf"
        LINK_DEPENDS ${LINKER_SCRIPT}
    )

    target_include_directories(${target} PRIVATE ${INCLUDE_DIR})

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${C_FLAGS}>
        $<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
    )

    target_compile_definitions(${target} PRIVATE RPI_VERSION=${RPI_VERSION})
    if(KVAR_QEMU)
        target_compile_definitions(${target} PRIVATE QEMU_TESTING)
    endif()

    target_link_options(${target} PRIVATE
        -T${LINKER_SCRIPT}
        -Map=${CMAKE_BINARY_DIR}/${KVAR_MAP_NAME}
    )

    set(image_path "${CMAKE_BINARY_DIR}/${KVAR_IMAGE_NAME}")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${OBJCOPY} -O binary $<TARGET_FILE:${target}> ${image_path}
        COMMENT "Creating ${KVAR_IMAGE_NAME} binary"
        BYPRODUCTS ${image_path}
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/boot
        COMMAND ${CMAKE_COMMAND} -E copy ${image_path} ${CMAKE_BINARY_DIR}/boot/${KVAR_BOOT_IMAGE_NAME}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/config.txt ${CMAKE_BINARY_DIR}/boot/config.txt
        COMMENT "Copying ${KVAR_IMAGE_NAME} to local boot directory (${KVAR_BOOT_IMAGE_NAME})"
    )

    add_custom_target(install_${VARIANT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOTMNT}
        COMMAND ${CMAKE_COMMAND} -E copy ${image_path} ${BOOTMNT}/${KVAR_BOOT_IMAGE_NAME}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/config.txt ${BOOTMNT}/config.txt
        DEPENDS ${target}
        COMMENT "Copying ${KVAR_IMAGE_NAME} to boot mount: ${BOOTMNT}"
    )

    set_property(GLOBAL APPEND PROPERTY KERNEL_VARIANT_TARGETS ${target})
    set_property(GLOBAL APPEND PROPERTY KERNEL_INSTALL_TARGETS install_${VARIANT})
endfunction()

if(BUILD_QEMU_VARIANT)
    add_kernel_variant(qemu
        QEMU
        OUTPUT_NAME kernel8
        IMAGE_NAME kernel8.img
        BOOT_IMAGE_NAME kernel8-qemu.img
        MAP_NAME kernel8.map
    )
endif()

if(BUILD_HW_VARIANT)
    add_kernel_variant(hw
        OUTPUT_NAME kernel8-hw
        IMAGE_NAME kernel8-hw.img
        BOOT_IMAGE_NAME kernel8-rpi${RPI_VERSION}.img
        MAP_NAME kernel8-hw.map
    )
endif()

get_property(_kernel_targets GLOBAL PROPERTY KERNEL_VARIANT_TARGETS)
if(NOT _kernel_targets)
    message(FATAL_ERROR "No kernel variants are enabled. Enable at least one of BUILD_QEMU_VARIANT or BUILD_HW_VARIANT.")
endif()

get_property(_kernel_install_targets GLOBAL PROPERTY KERNEL_INSTALL_TARGETS)
if(_kernel_install_targets)
    add_custom_target(install_kernel DEPENDS ${_kernel_install_targets})
endif()

set(enabled_variants)
if(BUILD_QEMU_VARIANT)
    list(APPEND enabled_variants "QEMU")
endif()
if(BUILD_HW_VARIANT)
    list(APPEND enabled_variants "hardware")
endif()
list(JOIN enabled_variants ", " enabled_variants_str)

# Create boot directory ahead of time for user convenience
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/boot)

# Print configuration
message(STATUS "Building for Raspberry Pi ${RPI_VERSION}")
message(STATUS "Boot mount directory: ${BOOTMNT}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Linker script: ${LINKER_SCRIPT}")
message(STATUS "Kernel variants enabled: ${enabled_variants_str}")
