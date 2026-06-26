#!/usr/bin/env bash
# Cross-build CycloneDDS as a static library for the S32Z2 (Cortex-R52).
# Run on the development host.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

BUILD_ROOT=${FREERTOS_S32Z2_BUILD_ROOT:-"${REPO_ROOT}/build/freertos-s32z2"}
CDDS_HOST_PREFIX=${FREERTOS_S32Z2_CDDS_HOST_PREFIX:-"${REPO_ROOT}/build/cyclonedds_host/out"}
CDDS_TARGET_BUILD_DIR=${FREERTOS_S32Z2_CDDS_TARGET_BUILD_DIR:-"${BUILD_ROOT}/cdds_target"}
CDDS_TARGET_PREFIX=${FREERTOS_S32Z2_CDDS_TARGET_PREFIX:-"${BUILD_ROOT}/cdds_target_out"}
S32CT_GENERATED_DIR=${S32CT_GENERATED_DIR:-"${REPO_ROOT}/actuation_module/freertos_s32z2/s32ct_config"}
export S32CT_GENERATED_DIR

# The host idlc is built for the development host and reused while configuring
# the S32Z2 target library.
if [ ! -x "${CDDS_HOST_PREFIX}/bin/idlc" ]; then
    echo "Host idlc not found at ${CDDS_HOST_PREFIX}/bin/idlc."
    echo "Build it first via ./build.sh --platform freertos-s32z2, or set"
    echo "FREERTOS_S32Z2_CDDS_HOST_PREFIX to an existing CycloneDDS host install."
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
if [ ! -d "${S32CT_GENERATED_DIR}/generate/include" ]; then
    echo "S32CT_GENERATED_DIR=${S32CT_GENERATED_DIR} does not contain generate/include."
    echo "Initialise actuation_module/freertos_s32z2/s32ct_config or set"
    echo "S32CT_GENERATED_DIR to your S32 Config Tools project root."
    exit 1
fi

mkdir -p "${BUILD_ROOT}"
if [ -z "${CDDS_TARGET_BUILD_DIR}" ] || [ "${CDDS_TARGET_BUILD_DIR}" = "/" ]; then
    echo "Refusing to clean unsafe CDDS target build directory: ${CDDS_TARGET_BUILD_DIR}"
    exit 1
fi

# Incremental build: skip the rm -rf when FREERTOS_S32Z2_CDDS_NO_CLEAN is set.
if [ -z "${FREERTOS_S32Z2_CDDS_NO_CLEAN:-}" ]; then
    rm -rf "${CDDS_TARGET_BUILD_DIR}"
fi

cmake -S cyclonedds -B "${CDDS_TARGET_BUILD_DIR}" \
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
    -DCMAKE_INSTALL_PREFIX="${CDDS_TARGET_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "${CDDS_TARGET_BUILD_DIR}" --target install -j"$(nproc)"
test -f "${CDDS_TARGET_PREFIX}/lib/libddsc.a"
echo "CycloneDDS target library built: ${CDDS_TARGET_PREFIX}/lib/libddsc.a"
