#!/usr/bin/env bash
# Cross-build CycloneDDS as a static library for the R-Car X5H (Cortex-R52).
# Run on the development host. Ported from
# actuation_module/freertos_s32z2/scripts/build-cdds-target.sh, with two
# deliberate differences:
#   - No NXP RTD/S32 Config Tools env vars: every input here (FreeRTOS
#     kernel, CR52 port, lwIP, our lwip_port) is either a public git
#     submodule under actuation_module/freertos_x5h/ or a file we wrote
#     ourselves, so there is nothing to point at via environment variables.
#   - -mfloat-abi=softfp, not S32Z2's -mfloat-abi=hard -- matches the R-Car
#     BSP's own inherited ABI (see cmake/arm-cortex-r52-x5h.cmake's header
#     comment).
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

X5H_DIR="${REPO_ROOT}/actuation_module/freertos_x5h"
RCAR_BSP_DEMO_DIR="${X5H_DIR}/rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52"
LWIP_DIR="${X5H_DIR}/lwip"
LWIP_PORT_DIR="${X5H_DIR}/lwip_port"

BUILD_ROOT=${FREERTOS_X5H_BUILD_ROOT:-"${REPO_ROOT}/build/freertos-x5h"}
CDDS_HOST_PREFIX=${FREERTOS_X5H_CDDS_HOST_PREFIX:-"${REPO_ROOT}/build/cyclonedds_host/out"}
CDDS_TARGET_BUILD_DIR=${FREERTOS_X5H_CDDS_TARGET_BUILD_DIR:-"${BUILD_ROOT}/cdds_target"}
CDDS_TARGET_PREFIX=${FREERTOS_X5H_CDDS_TARGET_PREFIX:-"${BUILD_ROOT}/cdds_target_out"}

# The host idlc is built for the development host and reused while
# configuring the X5H target library (CycloneDDS's own target build runs
# idlc on its own internal .idl files; the just-cross-compiled target
# libddsc.a cannot run on the host, so a separately-built host idlc is
# required even though this script never generates any x5h-side IDL code
# itself -- that is autoware_msgs's job, wired up in CMakeLists.txt).
if [ ! -x "${CDDS_HOST_PREFIX}/bin/idlc" ]; then
    echo "Host idlc not found at ${CDDS_HOST_PREFIX}/bin/idlc."
    echo "Build it first via ./build.sh --platform freertos-x5h, or set"
    echo "FREERTOS_X5H_CDDS_HOST_PREFIX to an existing CycloneDDS host install."
    exit 1
fi

toolchain_bin=$("${X5H_DIR}/scripts/fetch-toolchain.sh")
export PATH="${toolchain_bin}:${PATH}"

mkdir -p "${BUILD_ROOT}"
if [ -z "${CDDS_TARGET_BUILD_DIR}" ] || [ "${CDDS_TARGET_BUILD_DIR}" = "/" ]; then
    echo "Refusing to clean unsafe CDDS target build directory: ${CDDS_TARGET_BUILD_DIR}"
    exit 1
fi

# Incremental build: skip the rm -rf when FREERTOS_X5H_CDDS_NO_CLEAN is set.
if [ -z "${FREERTOS_X5H_CDDS_NO_CLEAN:-}" ]; then
    rm -rf "${CDDS_TARGET_BUILD_DIR}"
fi

# -mfloat-abi=softfp, LWIP_TIMEVAL_PRIVATE=0 (same newlib struct timeval
# collision as S32Z2's identical fix), and the four include paths CycloneDDS's
# WITH_FREERTOS/WITH_LWIP backends need: our lwip_port (arch/cc.h,
# arch/sys_arch.h, lwipopts.h), lwip's own public headers, the FreeRTOS
# kernel, and the R-Car CR52 demo's include/ (FreeRTOSConfig.h + portmacro.h
# -- see ddsrt/src/threads/freertos/threads.c's #include <FreeRTOS.h> /
# #include <task.h>, which resolve through these four paths).
#
# -D__int64_t_defined=1 -include inttypes.h -include sys/select.h:
# identical fix to S32Z2's own build-cdds-target.sh (same newlib toolchain
# family, same problems -- confirmed this is a plain newlib quirk, not
# something specific to S32Z2's NXP RTD headers: this vanilla Arm GNU
# 13.2.Rel1 toolchain's own <inttypes.h> gates PRId64/PRIu64/etc. behind
# `#if __int64_t_defined`, and under -ffreestanding that macro is not set by
# the time <inttypes.h> is preprocessed even after <stdint.h> has already
# defined int64_t -- confirmed with a standalone repro before adding this
# flag here). inttypes.h: ddsrt/src/core/cdr/src/dds_cdrstream.c and
# ddsi/src/ddsi_portmapping.c use PRIu64 without including it themselves
# (relies on some other header pulling it in transitively on a hosted libc
# -- not guaranteed on newlib). sys/select.h:
# forces newlib's own fd_set/FD_SET to be defined before lwip/sockets.h is
# ever reached, so lwip/sockets.h's own `#ifndef FD_SET` guard skips its
# conflicting typedef instead of redefining `struct fd_set` a second time.
#
# -include lwip_port/cdds_multicast_compat.h: see that file's own header
# comment -- ddsi_udp.c references IP_MULTICAST_*/IP_*_MEMBERSHIP/
# struct ip_mreq unconditionally, which our LWIP_IGMP=0 config does not
# provide.
#
# -include lwip_port/cdds_freertos_compat.h: see that file's own header
# comment -- ddsrt/src/threads/freertos/threads.c calls xTaskCreateFpu(),
# an NXP-RTD-specific FreeRTOS API this R-Car BSP port does not have (and
# does not need -- its FPU context is saved/restored unconditionally on
# every context switch, unlike NXP RTD's lazy-restore scheme).
cmake -S cyclonedds -B "${CDDS_TARGET_BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${X5H_DIR}/cmake/arm-cortex-r52-x5h.cmake" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_IDLC=OFF \
    -DBUILD_DDSPERF=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF \
    -DENABLE_SECURITY=OFF \
    -DENABLE_SSL=OFF \
    -DENABLE_SHM=OFF \
    -DENABLE_IPV6=OFF \
    -DENABLE_SOURCE_SPECIFIC_MULTICAST=OFF \
    -DENABLE_NETWORK_PARTITIONS=OFF \
    -DENABLE_LTO=OFF \
    -DWITH_FREERTOS=ON \
    -DWITH_LWIP=ON \
    -DCMAKE_C_FLAGS="-mcpu=cortex-r52 -mtune=cortex-r52 -mfpu=neon-fp-armv8 -mfloat-abi=softfp -ffreestanding -ffunction-sections -fdata-sections -fno-common -DLWIP_TIMEVAL_PRIVATE=0 -D__int64_t_defined=1 -include inttypes.h -include sys/select.h -include ${LWIP_PORT_DIR}/cdds_multicast_compat.h -include ${LWIP_PORT_DIR}/cdds_freertos_compat.h -I${LWIP_PORT_DIR} -I${LWIP_DIR}/src/include -I${RCAR_BSP_DEMO_DIR}/../../Source/include -I${RCAR_BSP_DEMO_DIR}/include" \
    -DCMAKE_INSTALL_PREFIX="${CDDS_TARGET_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "${CDDS_TARGET_BUILD_DIR}" --target install -j"$(nproc)"
test -f "${CDDS_TARGET_PREFIX}/lib/libddsc.a"

# Make the -include cdds_freertos_compat.h wiring's absence loud (review
# round 1 fix), instead of leaving it a silent, badly-attributed failure
# deferred to a much later build step: libddsc.a is a static archive, never
# linked by this script (WITH_FREERTOS's threads.c is only compiled here,
# not resolved against a real xTaskCreate/xTaskCreateFpu symbol) -- so if
# the -include flag above were ever dropped (a typo'd edit to the
# CMAKE_C_FLAGS string, a refactor that reorders/drops one -include),
# ddsrt_thread_create() would still compile with only an implicit-
# declaration warning (arm-none-eabi-gcc does not error on this by
# default), and this whole script would report success -- the missing
# xTaskCreateFpu symbol would only surface as an "undefined reference" at
# the final actuation_x5h link, a separate build step and separate command,
# confusingly far from its actual cause. Check for it here instead, right
# where the shim is wired: if the alias applied, threads.c's call compiles
# down to a reference to plain xTaskCreate (resolved later against
# freertos_bsp); if it did not, the object still references the
# nonexistent xTaskCreateFpu symbol directly, which nm can see in this
# archive today without waiting for the final link.
nm_bin="$(command -v arm-none-eabi-nm || true)"
if [ -z "${nm_bin}" ]; then
    echo "ERROR: arm-none-eabi-nm not found on PATH; cannot verify the cdds_freertos_compat.h shim applied." >&2
    exit 1
fi
if "${nm_bin}" -u "${CDDS_TARGET_PREFIX}/lib/libddsc.a" 2>/dev/null | grep -q 'xTaskCreateFpu'; then
    echo "ERROR: libddsc.a still references xTaskCreateFpu directly." >&2
    echo "The -include ${LWIP_PORT_DIR}/cdds_freertos_compat.h flag (see this script's" >&2
    echo "own -DCMAKE_C_FLAGS line and that header's comment) did not take effect --" >&2
    echo "check it is still present, in order, ahead of any conflicting -include." >&2
    exit 1
fi

echo "CycloneDDS target library built: ${CDDS_TARGET_PREFIX}/lib/libddsc.a"
