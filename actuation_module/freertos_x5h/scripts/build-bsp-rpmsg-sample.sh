#!/usr/bin/env bash
# Configures and builds the R-Car FreeRTOS BSP's own rpmsg_sample application
# (rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/sample_apps/rpmsg_sample) from public
# source, using the pinned arm-none-eabi toolchain fetched by
# fetch-toolchain.sh. The resulting ELF is the Stage 1 board artifact: it gets
# flashed to hardware later to prove that "built from public source with our
# toolchain" reproduces the vendor prebuilt's Linux-facing contract, before
# any new code ships.
#
# Configure invocation follows Demo/R-Car_Gen5_CR52/Readme.md (see
# actuation_module/freertos_x5h/AUDIT.md Section 6), with two deliberate
# departures from the README's literal example:
#   - RAM_REGION=2, not the README's example RAM_REGION=1. RAM_REGION=2 is
#     what selects CORE=1 inside sample_apps/rpmsg_sample/CMakeLists.txt,
#     which is what produces the rpmsg_mfis1_cluster0_core1 target whose
#     addresses (.text@0x11600000, .resource_table@0x96650000) match the
#     frozen contract. Dropping this flag silently picks CORE=0 instead.
#   - MFIS_CHAN=1 is pinned explicitly (the README's example configure line
#     omits it) so the build produces exactly the one target this script
#     wants, instead of both mfis0_core0/mfis1_core1 pairs.
#
# Network note: ENABLE_OPENAMP=1 makes this configure/build step fetch OpenAMP
# and libmetal from public GitHub (tag v2024.10.0) via ExternalProject_Add --
# see AUDIT.md Section 2. Network access is required to run this script.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="$("$ROOT/actuation_module/freertos_x5h/scripts/fetch-toolchain.sh")"
export PATH="$BIN:$PATH"

SRC="$ROOT/actuation_module/freertos_x5h/rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52"
BUILD="$ROOT/build/freertos-x5h-bsp-sample"
TARGET="rpmsg_mfis1_cluster0_core1"

cmake -S "$SRC" -B "$BUILD" -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$SRC/toolchain_arm_none_eabi.cmake" \
  -DCMAKE_INSTALL_PREFIX="$BUILD/install" \
  -DBOARD=x5h_ironhide \
  -DENABLE_OPENAMP=1 \
  -DRAM_REGION=2 \
  -DMFIS_CHAN=1 \
  -DUART_ID=1 \
  -DCACHE=1

cmake --build "$BUILD" --target "$TARGET" -j"$(nproc)"

SRC_ELF="$BUILD/sample_apps/rpmsg_sample/$TARGET.elf"
[ -f "$SRC_ELF" ] || {
  echo "build-bsp-rpmsg-sample: expected ELF not found at $SRC_ELF" >&2
  exit 1
}
cp -f "$SRC_ELF" "$BUILD/$TARGET.elf"

echo "$BUILD/$TARGET.elf"
