#!/bin/bash
# Build script for KoraOS using CMake and the LLVM toolchain

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Options:
  --variant, -v <qemu|virt|hw|all>   Select kernel variant(s) to build (default: all)
  --qemu                        Build only the QEMU testing variant
  --virt                        Build only the QEMU virt variant
  --hw, --hardware              Build only the hardware variant
  --release                     Configure with CMAKE_BUILD_TYPE=Release
  --debug                       Configure with CMAKE_BUILD_TYPE=Debug (default)
  --build-dir <dir>             Override the build directory (default: build)
  --clean                       Remove the build directory before configuring
  clean                         Remove the build directory and exit
  --help, -h                    Show this help message
EOF
}

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
RPI_VERSION="${RPI_VERSION:-4}"
BOOTMNT="${BOOTMNT:-/workspace/build/boot}"
VARIANT="all"
CLEAN_FIRST=0
CLEAN_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -v|--variant)
            [[ $# -ge 2 ]] || { echo "Error: --variant requires an argument"; usage; exit 1; }
            VARIANT="$2"
            shift 2
            ;;
        --qemu)
            VARIANT="qemu"
            shift
            ;;
        --virt)
            VARIANT="virt"
            shift
            ;;
        --hw|--hardware)
            VARIANT="hw"
            shift
            ;;
        --all|--both)
            VARIANT="all"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "Error: --build-dir requires a path"; usage; exit 1; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --clean)
            CLEAN_FIRST=1
            shift
            ;;
        clean)
            CLEAN_ONLY=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if [[ ${CLEAN_ONLY} -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
    echo "Removed ${BUILD_DIR}"
    exit 0
fi

if [[ ${CLEAN_FIRST} -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
fi

variant_normalized=$(echo "${VARIANT}" | tr '[:upper:]' '[:lower:]')

BUILD_VIRT_VARIANT=OFF
case "${variant_normalized}" in
    qemu)
        BUILD_QEMU_VARIANT=ON
        BUILD_HW_VARIANT=OFF
        ;;
    virt)
        BUILD_QEMU_VARIANT=OFF
        BUILD_HW_VARIANT=OFF
        BUILD_VIRT_VARIANT=ON
        ;;
    hw|hardware)
        BUILD_QEMU_VARIANT=OFF
        BUILD_HW_VARIANT=ON
        ;;
    all|both)
        BUILD_QEMU_VARIANT=ON
        BUILD_HW_VARIANT=ON
        ;;
    *)
        echo "Unknown variant: ${VARIANT}"
        usage
        exit 1
        ;;
esac

mkdir -p "${BUILD_DIR}"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DRPI_VERSION="${RPI_VERSION}" \
    -DBOOTMNT="${BOOTMNT}" \
    -DBUILD_QEMU_VARIANT="${BUILD_QEMU_VARIANT}" \
    -DBUILD_HW_VARIANT="${BUILD_HW_VARIANT}" \
    -DBUILD_VIRT_VARIANT="${BUILD_VIRT_VARIANT}" \
    -G "Unix Makefiles"

cmake --build "${BUILD_DIR}" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"


echo ""
echo "Build complete!"
if [[ ${BUILD_QEMU_VARIANT} == ON ]]; then
    echo "  QEMU kernel image: ${BUILD_DIR}/kernel8.img"
fi
if [[ ${BUILD_VIRT_VARIANT} == ON ]]; then
    echo "  QEMU virt image: ${BUILD_DIR}/kernel-virt.img"
fi
if [[ ${BUILD_HW_VARIANT} == ON ]]; then
    echo "  Hardware kernel image: ${BUILD_DIR}/kernel8-hw.img"
fi
echo "  compile_commands.json generated for clangd"
echo ""

