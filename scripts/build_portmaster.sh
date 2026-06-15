#!/usr/bin/env bash
set -euo pipefail

# Build drumrom_handheld for PortMaster from within an ARM build environment
# (Anchor chroot, WSL2 chroot, or Docker container).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-portmaster}"
JOBS="${JOBS:-$(nproc)}"
ARCH="${ARCH:-aarch64}"

case "${ARCH}" in
	aarch64)
		TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/aarch64-linux-gnu.cmake"
		;;
	armhf)
		TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/armhf-linux-gnueabihf.cmake"
		;;
	*)
		echo "Unsupported ARCH='${ARCH}'. Use ARCH=aarch64 or ARCH=armhf."
		exit 1
		;;
esac

cd "${ROOT_DIR}"

echo "[1/3] Configuring CMake in ${BUILD_DIR}"
cmake -S . -B "${BUILD_DIR}" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
	-DDRUMROM_BUILD_GUI=OFF \
	-DDRUMROM_BUILD_HANDHELD=ON \
	-DDRUMROM_BUILD_BUILD_BIN=OFF \
	-DDRUMROM_BUILD_SYNTH_DEMO=OFF \
	-DDRUMROM_BUILD_PLUGIN=OFF

echo "[2/3] Building handheld target"
cmake --build "${BUILD_DIR}" -j"${JOBS}" --target drumrom_handheld

echo "[3/3] Verifying output binary"
file "${BUILD_DIR}/drumrom_handheld"

echo "Done. Binary: ${BUILD_DIR}/drumrom_handheld"
