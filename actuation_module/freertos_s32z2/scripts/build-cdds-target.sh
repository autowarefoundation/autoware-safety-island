#!/usr/bin/env bash
# Cross-build CycloneDDS as a static library for the S32Z2 (Cortex-R52).
# Run on the development host.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

# The host idlc from the FreeRTOS POSIX simulator pipeline is reused.
if [ ! -x build-freertos/cdds_host_out/bin/idlc ]; then
    echo "Host idlc not found at build-freertos/cdds_host_out/bin/idlc."
    echo "Build it first via the FreeRTOS POSIX simulator's Phase 1 cmake (see"
    echo "actuation_module/freertos/CMakeLists.txt header comments)."
    exit 1
fi

# FREERTOS_PATH and LWIP_PATH point CycloneDDS's WITH_FREERTOS/WITH_LWIP
# backends at the NXP-supplied headers. S32_RTD_PATH provides the AUTOSAR
# base headers (Compiler.h, Std_Types.h) that lwIP's NXP arch/cc.h pulls in
# transitively through Devassert.h. S32CT_GENERATED_DIR provides the
# project-specific Platform_Types.h / Soc_Ips.h emitted by S32 Config Tools.
: "${FREERTOS_PATH:?Set FREERTOS_PATH}"
: "${LWIP_PATH:?Set LWIP_PATH}"
: "${S32_RTD_PATH:?Set S32_RTD_PATH}"
: "${S32CT_GENERATED_DIR:?Set S32CT_GENERATED_DIR to your S32 Config Tools project root}"

rm -rf build-s32z2/cdds_target
cmake -S cyclonedds -B build-s32z2/cdds_target \
    -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/actuation_module/freertos_s32z2/cmake/arm-cortex-r52.cmake" \
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
    -DCMAKE_C_FLAGS="-mcpu=cortex-r52 -mfpu=neon-fp-armv8 -mfloat-abi=hard -ffunction-sections -fdata-sections -fno-common -D__int64_t_defined=1 -DUSING_RTD=1 -DS32Z27 -DLWIP_TIMEVAL_PRIVATE=0 -include inttypes.h -include sys/select.h -I${REPO_ROOT}/actuation_module/include/platform/freertos/s32z2 -I${FREERTOS_PATH}/Source/include -I${FREERTOS_PATH}/Source/portable/GCC/ARM_CR52_GIC -I${LWIP_PATH}/lwip/src/include -I${LWIP_PATH}/code/ports/platform/generic/gcc/setting -I${S32_RTD_PATH}/RTD/BaseNXP_TS_T31D53M20I1R0/include -I${S32_RTD_PATH}/RTD/BaseNXP_TS_T31D53M20I1R0/header -I${S32CT_GENERATED_DIR}/generate/include" \
    -DCMAKE_INSTALL_PREFIX="${REPO_ROOT}/build-s32z2/cdds_target_out" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build-s32z2/cdds_target --target install -j
test -f build-s32z2/cdds_target_out/lib/libddsc.a
echo "CycloneDDS target library built: build-s32z2/cdds_target_out/lib/libddsc.a"
