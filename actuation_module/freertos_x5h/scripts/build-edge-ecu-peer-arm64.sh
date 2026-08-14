#!/usr/bin/env bash
# Build the arm64 edge_ecu_peer bundle for AutoSD deployment (Task 8, Step 3).
#
# Reuses freertos_s32z2/edge_ecu_peer/CMakeLists.txt AS-IS (the S32Z2 target
# is off-limits to modify) inside an emulated arm64v8/ubuntu:22.04 container
# via podman --arch arm64, and overrides its CONFIG_DDS_* CACHE vars at cmake
# configure time to the frozen X5H wire constants:
#   - CONFIG_DDS_DOMAIN_ID=2
#   - CONFIG_DDS_NETWORK_INTERFACE=tap0   (Linux's own RPMsg-netif device,
#     see task-9/task-10: `ip tuntap add dev tap0 ... && ip addr add
#     172.16.52.1/24 dev tap0`)
#   - CONFIG_DDS_PEER=172.16.52.2         (CR52 unicast SPDP peer)
# plus CONFIG_DDS_DISABLE_MULTICAST=1 via CMAKE_C_FLAGS/CMAKE_CXX_FLAGS (the
# reused CMakeLists.txt has no CACHE var for this -- see common/dds/
# config.hpp's Task 8 comment for why full multicast disable, not just a
# unicast peer, is required on this point-to-point link).
#
# CDDS_HOST_PREFIX inside that reused CMakeLists.txt is a plain (non-CACHE)
# `set(CDDS_HOST_PREFIX ${REPO_ROOT}/build/cyclonedds_host/out)`, where
# REPO_ROOT is derived from CMAKE_CURRENT_SOURCE_DIR via "../..": it cannot
# be overridden with -D, and the real repo's build/cyclonedds_host/out
# already holds an x86_64 host build from earlier tasks. Re-deriving that
# same path against a different filesystem root fixes this without touching
# the S32Z2 file: this script assembles a virtual root at /build-arm64
# inside the container by bind-mounting actuation_module/ and cyclonedds/
# directly under it (not via symlink -- symlink ".." resolution would just
# walk back out to the real /src tree) alongside a private, container-local
# build/ directory, so REPO_ROOT resolves to /build-arm64 and
# CDDS_HOST_PREFIX to /build-arm64/build/cyclonedds_host/out: an arm64-only
# CycloneDDS install this script builds fresh, never touching the real
# build/cyclonedds_host/out.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

IMAGE="docker.io/arm64v8/ubuntu:22.04"
WORK="${REPO_ROOT}/build/edge-ecu-peer-arm64"
OUT_TAR="${REPO_ROOT}/build/edge-ecu-peer-arm64.tar.gz"
CYCLONEDDS_X5H_XML="${REPO_ROOT}/actuation_module/freertos_x5h/edge_ecu_peer/cyclonedds-x5h.xml"

rm -rf "${WORK}"
mkdir -p "${WORK}/build"

cat > "${WORK}/in-container-build.sh" <<'INNER'
#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake ca-certificates file binutils >/dev/null

CDDS_HOST_PREFIX=/build-arm64/build/cyclonedds_host/out
if [ ! -x "${CDDS_HOST_PREFIX}/bin/idlc" ]; then
    echo "Building arm64 CycloneDDS host tools (idlc + shared libddsc)..."
    cmake -S /build-arm64/cyclonedds -B /build-arm64/build/cyclonedds_host_build \
        -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="${CDDS_HOST_PREFIX}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_SECURITY=OFF -DENABLE_SSL=OFF -DENABLE_SHM=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DDSPERF=OFF
    cmake --build /build-arm64/build/cyclonedds_host_build --target install -j"$(nproc)"
fi
file "${CDDS_HOST_PREFIX}/bin/idlc"
file "${CDDS_HOST_PREFIX}/lib/libddsc.so.0.11.0" 2>/dev/null || true

echo "Building edge_ecu_pub / edge_ecu_sub (reusing freertos_s32z2/edge_ecu_peer/CMakeLists.txt as-is)..."
rm -rf /build-arm64/build/peer-build
# The reused CMakeLists.txt only puts CDDS_HOST_PREFIX/bin on PATH for its own
# CMake-configure-time process (`set(ENV{PATH} ...)`), which does not carry
# over to the separate `cmake --build` invocation below. autoware_msgs/
# CMakeLists.txt resolves idlc from a build-time add_custom_command (`find
# ... -exec idlc {} \;`), which inherits *this* shell's PATH -- so idlc must
# be put on PATH here too, or the custom command fails at build time even
# though configure-time idlc checks (which ran inside the same CMake process
# where set(ENV{PATH}) took effect) succeed.
export PATH="${CDDS_HOST_PREFIX}/bin:${PATH}"
cmake -S /build-arm64/actuation_module/freertos_s32z2/edge_ecu_peer -B /build-arm64/build/peer-build \
    -DCONFIG_DDS_DOMAIN_ID=2 \
    -DCONFIG_DDS_NETWORK_INTERFACE=tap0 \
    -DCONFIG_DDS_PEER=172.16.52.2 \
    -DCMAKE_C_FLAGS=-DCONFIG_DDS_DISABLE_MULTICAST=1 \
    -DCMAKE_CXX_FLAGS=-DCONFIG_DDS_DISABLE_MULTICAST=1
cmake --build /build-arm64/build/peer-build -j"$(nproc)"

test -x /build-arm64/build/peer-build/edge_ecu_pub
test -x /build-arm64/build/peer-build/edge_ecu_sub
echo "--- file(1) on built binaries ---"
file /build-arm64/build/peer-build/edge_ecu_pub
file /build-arm64/build/peer-build/edge_ecu_sub
INNER
chmod +x "${WORK}/in-container-build.sh"

echo "Running arm64 build in an emulated arm64v8/ubuntu:22.04 container (podman --arch arm64)..."
podman run --rm --arch arm64 \
    -v "${REPO_ROOT}/actuation_module:/build-arm64/actuation_module:ro,Z" \
    -v "${REPO_ROOT}/cyclonedds:/build-arm64/cyclonedds:ro,Z" \
    -v "${WORK}/build:/build-arm64/build:Z" \
    -v "${WORK}/in-container-build.sh:/build-arm64/in-container-build.sh:ro,Z" \
    "${IMAGE}" \
    bash /build-arm64/in-container-build.sh

# ---- Verify the binaries really are arm64 (from the host, no execution needed) ----
file "${WORK}/build/peer-build/edge_ecu_pub" | tee "${WORK}/file-edge_ecu_pub.txt"
file "${WORK}/build/peer-build/edge_ecu_sub" | tee "${WORK}/file-edge_ecu_sub.txt"
grep -q "ARM aarch64" "${WORK}/file-edge_ecu_pub.txt"
grep -q "ARM aarch64" "${WORK}/file-edge_ecu_sub.txt"

# ---- Assemble the deployable bundle ----
BUNDLE="${WORK}/bundle"
rm -rf "${BUNDLE}"
mkdir -p "${BUNDLE}/lib"
cp "${WORK}/build/peer-build/edge_ecu_pub" "${BUNDLE}/"
cp "${WORK}/build/peer-build/edge_ecu_sub" "${BUNDLE}/"
cp "${CYCLONEDDS_X5H_XML}" "${BUNDLE}/"

# CycloneDDS shared libs the binaries link (the reused CMakeLists.txt builds
# with BUILD_SHARED_LIBS=ON, exactly like build.sh's own build_cyclonedds_host
# does for the host build): libddsc.so + its versioned target, resolved via
# readelf -d (NEEDED entries) rather than ldd, since ldd would need to
# execute the aarch64 loader itself.
needed=$(readelf -d "${WORK}/build/peer-build/edge_ecu_pub" | awk -F'[][]' '/NEEDED/{print $2}')
for lib in ${needed}; do
    case "${lib}" in
        libddsc*)
            found="${WORK}/build/cyclonedds_host/out/lib/${lib}"
            if [ -e "${found}" ]; then
                cp -L "${found}" "${BUNDLE}/lib/${lib}"
            fi
            ;;
    esac
done
# Also grab the unversioned/major-versioned symlink targets so the bundle's
# lib/ dir is self-sufficient regardless of which SONAME the loader asks for.
cp -L "${WORK}/build/cyclonedds_host/out/lib"/libddsc.so* "${BUNDLE}/lib/" 2>/dev/null || true

rm -rf "${BUNDLE}"/lib/*.a 2>/dev/null || true

tar -C "${BUNDLE}" -czf "${OUT_TAR}" .

echo ""
echo "Wrote ${OUT_TAR}"
echo "--- tarball contents ---"
tar -tzf "${OUT_TAR}"
