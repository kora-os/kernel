# Toolchain file for AArch64 bare-metal cross-compilation
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Target triple
set(TARGET_TRIPLE aarch64-none-elf)

# Prefer LLVM archive tools for cross-target ELF libraries. Apple's default
# ar/ranlib can produce empty archives from AArch64 ELF objects; select tools
# before project() chooses platform defaults. Preserve explicit alternatives.
if(NOT CMAKE_AR OR (CMAKE_HOST_APPLE AND CMAKE_AR STREQUAL "/usr/bin/ar"))
    find_program(KORAOS_LLVM_AR llvm-ar
        PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /usr/bin)
    if(KORAOS_LLVM_AR)
        set(CMAKE_AR "${KORAOS_LLVM_AR}" CACHE FILEPATH "ELF archive tool" FORCE)
    elseif(CMAKE_HOST_APPLE)
        message(FATAL_ERROR "llvm-ar is required for bare-metal ELF libraries on macOS.")
    endif()
endif()
if(NOT CMAKE_RANLIB OR (CMAKE_HOST_APPLE AND CMAKE_RANLIB STREQUAL "/usr/bin/ranlib"))
    find_program(KORAOS_LLVM_RANLIB llvm-ranlib
        PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /usr/bin)
    if(KORAOS_LLVM_RANLIB)
        set(CMAKE_RANLIB "${KORAOS_LLVM_RANLIB}" CACHE FILEPATH "ELF archive index tool" FORCE)
    elseif(CMAKE_HOST_APPLE)
        message(FATAL_ERROR "llvm-ranlib is required for bare-metal ELF libraries on macOS.")
    endif()
endif()

# Specify the cross compilers
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

# Linker selection: prefer ld.lld if available, otherwise use clang with lld
find_program(LLD_LINKER ld.lld)
if(LLD_LINKER)
    set(CMAKE_LINKER ${LLD_LINKER})
    # Link with ld.lld directly so bare linker options (-T, -Map=) in
    # target_link_options are understood. Both C and C++ kernel objects link the
    # same way: the kernel is -nostdlib and provides its own C++ runtime, so no
    # compiler-driver library injection is wanted.
    set(CMAKE_C_LINK_EXECUTABLE "${LLD_LINKER} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    set(CMAKE_CXX_LINK_EXECUTABLE "${LLD_LINKER} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
else()
    # Fall back to using clang with lld
    set(CMAKE_C_COMPILER_WORKS 1)
    set(CMAKE_CXX_COMPILER_WORKS 1)
    set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_C_COMPILER} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_COMPILER} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
endif()

# The link rules above invoke ld.lld directly, so compiler-driver link flags do
# not apply. CMake >= 3.27 would otherwise add "-Xlinker --dependency-file=..."
# for LLD/GNU-style linkers (not on macOS, whose system linker lacks it), which
# ld.lld rejects.
set(CMAKE_LINK_DEPENDS_USE_LINKER FALSE)

# Find llvm-objcopy
find_program(OBJCOPY llvm-objcopy
    PATHS 
        /opt/homebrew/opt/llvm/bin
        /usr/local/opt/llvm/bin
        /usr/bin
    NO_DEFAULT_PATH
)
if(NOT OBJCOPY)
    find_program(OBJCOPY llvm-objcopy)
endif()

if(NOT OBJCOPY)
    message(WARNING "llvm-objcopy not found. Install LLVM toolchain.")
    set(OBJCOPY llvm-objcopy)
endif()

# Search for programs only in the host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers only in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Don't run the linker test (it will fail for bare-metal targets)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)

# Make sure CMake doesn't add any standard libraries
set(CMAKE_C_STANDARD_LIBRARIES "")
set(CMAKE_CXX_STANDARD_LIBRARIES "")
set(CMAKE_ASM_STANDARD_LIBRARIES "")

