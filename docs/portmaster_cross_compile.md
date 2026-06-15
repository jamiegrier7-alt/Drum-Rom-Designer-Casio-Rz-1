# PortMaster Cross-Compile and Staging

This project now includes scripts to build and stage the handheld binary for PortMaster.

Reference: https://portmaster.games/build-environments.html

This repository includes dedicated CMake toolchains:
- toolchains/aarch64-linux-gnu.cmake
- toolchains/armhf-linux-gnueabihf.cmake

## 1) Build in an ARM environment

Use one of the PortMaster-supported build environments:
- Anchor chroot VM
- WSL2 chroot + qemu-aarch64-static
- Docker multi-arch environment

Run this inside that ARM environment:

```bash
cd /path/to/drumrom
./scripts/build_portmaster.sh
```

Choose 32-bit ARM (armhf) when needed:

```bash
cd /path/to/drumrom
ARCH=armhf ./scripts/build_portmaster.sh
```

Expected result from file command:
- aarch64 target: ELF 64-bit LSB executable, ARM aarch64
- armhf target: ELF 32-bit LSB executable, ARM

If you still see x86-64, you are not building in the correct environment.

## Cross-compile dependencies

If you are cross-compiling from x86 Linux instead of building directly inside an ARM chroot,
install cross compilers and target SDL2 dev packages first.

Example (Ubuntu):

```bash
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
				 gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

In that setup, pkg-config must resolve target-arch SDL2, not host SDL2.
Using the PortMaster chroot environments is usually simpler and more reliable.

## 2) Stage a PortMaster folder tree

```bash
cd /path/to/drumrom
./scripts/stage_portmaster.sh
```

Output folder:
- dist/portmaster/

Contains:
- DrumRomDesigner.sh
- drumrom/launch.sh
- drumrom/drumrom_handheld
- drumrom/configs
- drumrom/kits
- drumrom/presets
- drumrom/samples (if present)
- drumrom/roms (writable output)
- drumrom/settings (writable output)

## 3) Copy to device

Copy dist/portmaster contents onto the device ports location.
Typical layout:
- /roms/ports/DrumRomDesigner.sh
- /roms/ports/drumrom/...

## 4) Notes

- launch.sh forces HOME/XDG paths into the game folder for portable writes.
- If target firmware requires bundled shared libs, add a libs folder and set LD_LIBRARY_PATH in launch.sh.
- Keep configs, kits, and presets together with the binary, because handheld runtime expects relative paths.
