#!/usr/bin/env bash
# Build the arm64 edge_ecu_peer bundle for AutoSD deployment (Task 8, Step 3).
#
# This is the ONLY script that produces the AutoSD edge_ecu_peer bundle.
# freertos_s32z2/scripts/build-edge-ecu-peer.sh builds the S32Z2 bench's own
# x86_64 peer and must not be repurposed for X5H/AutoSD (off-limits file;
# also wrong target arch and wrong wire constants). Any AutoSD deployment
# consumer must invoke *this* script.
#
# Reuses freertos_s32z2/edge_ecu_peer/CMakeLists.txt AS-IS (the S32Z2 target
# is off-limits to modify) inside an arm64v8/ubuntu:22.04 container via
# podman --arch arm64 (qemu/binfmt-emulated on an x86_64 host, native on an
# arm64 host -- see the "Running arm64 build in..." echo below, which
# reports which one this run actually is), and overrides its CONFIG_DDS_*
# CACHE vars at cmake configure time to the frozen X5H wire constants:
#   - CONFIG_DDS_DOMAIN_ID=2
#   - CONFIG_DDS_NETWORK_INTERFACE=tap0   (Linux's own RPMsg-netif device,
#     brought up out-of-band on the AutoSD host, e.g.
#     `ip tuntap add dev tap0 mode tap && ip addr add
#     172.16.52.1/24 dev tap0`; this script does not bring it up itself)
#   - CONFIG_DDS_PEER=172.16.52.2         (CR52 unicast SPDP peer)
# plus CONFIG_DDS_DISABLE_MULTICAST=1, CONFIG_DDS_MAX_MSG_SIZE=434,
# CONFIG_DDS_MAX_REXMIT_MSG_SIZE=434 and CONFIG_DDS_FRAGMENT_SIZE=348 via
# CMAKE_C_FLAGS/CMAKE_CXX_FLAGS (the reused CMakeLists.txt has no CACHE var
# for any of these four -- see common/dds/config.hpp's Task 8 comment for why
# full multicast disable, not just a unicast peer, is required on this
# point-to-point link, and its Task 21 comment for the full derivation of
# the three DDS-sizing constants; both ends of the link must set all three
# sizing knobs or the end left at CycloneDDS's defaults still IP-fragments).
# check-dds-config.sh asserts these four literal -D/compile-definition
# strings appear, uncommented, in THIS file -- see that script's Important-1
# fix comment for why the Linux-side source of truth is this script, not
# edge_ecu_peer/cyclonedds-x5h.xml (which is never read at runtime; see that
# XML's own header comment).
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
# walk back out to the real /src tree) alongside a container-local build/
# directory, so REPO_ROOT resolves to /build-arm64 and CDDS_HOST_PREFIX to
# /build-arm64/build/cyclonedds_host/out: an arm64-only CycloneDDS install
# this script builds fresh, never touching the real build/cyclonedds_host/out.
#
# Cache/scratch split (review round 2, Minor #1): that CDDS_HOST_PREFIX
# install is expensive (a full CycloneDDS host build under qemu emulation)
# and was previously reused only by *skipping* the rebuild
# (`if [ ! -x ".../idlc" ]`) -- but the top-level `rm -rf "${WORK}"` below
# wiped that same directory at the start of every run, making the reuse
# branch permanently unreachable in practice. CACHE_DIR now lives outside
# WORK and is never rm -rf'd by this script; only WORK (peer-build scratch,
# the assembled bundle, and the output tarball) is wiped fresh every run.
# CACHE_DIR is bind-mounted at the *nested* container path
# /build-arm64/build/cyclonedds_host (a sub-path of the separate
# /build-arm64/build mount), which both podman and plain Linux bind mounts
# support: the more specific mount simply shadows that subdirectory of the
# broader one. One consequence: host-side references to the installed
# CycloneDDS libs (the RPATH/patchelf section and the bundling section
# below) must read from CACHE_DIR directly, not from
# "${WORK}/build/cyclonedds_host" -- that path only ever exists inside the
# container's merged view, never on the host filesystem.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

# Minor #4: pin by manifest digest, not the mutable `22.04` tag, so a future
# run of this script (or CI) builds against the exact same base image bytes
# this was developed and reviewed against, rather than whatever `22.04` has
# drifted to mean by then. Digest captured via:
#   podman inspect docker.io/arm64v8/ubuntu:22.04 --format '{{.Digest}}'
IMAGE="docker.io/arm64v8/ubuntu@sha256:70490a6c9a3e6632c5baa4d8674d179da8928f37f4484e9ececc75dc9bce6299"
# Native (non-emulated) x86_64 image used only for the host-side patchelf
# RPATH fix-up below (Important #3) -- no qemu/binfmt involved, since
# patchelf never executes the arm64 binaries it edits, only rewrites their
# ELF program-header/dynamic-section bytes. Also digest-pinned for the same
# reproducibility reason as IMAGE above.
PATCHELF_IMAGE="docker.io/library/ubuntu@sha256:561618e2c15bf2397621dd04f96926663a3b5616c189cf7e38db7e82f5c538ea"

CACHE_DIR="${REPO_ROOT}/build/edge-ecu-peer-arm64-cache"
WORK="${REPO_ROOT}/build/edge-ecu-peer-arm64"
OUT_TAR="${REPO_ROOT}/build/edge-ecu-peer-arm64.tar.gz"
CYCLONEDDS_X5H_XML="${REPO_ROOT}/actuation_module/freertos_x5h/edge_ecu_peer/cyclonedds-x5h.xml"

mkdir -p "${CACHE_DIR}"
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
# -DCMAKE_BUILD_TYPE=Release (Minor #5): the reused S32Z2 CMakeLists.txt / its
# own build-edge-ecu-peer.sh set no CMAKE_BUILD_TYPE at all (confirmed via
# grep -- neither file mentions it), so this is a deliberate divergence for
# the AutoSD arm64 bundle, not an oversight: this bundle ships to a board and
# benefits from optimized, non-debug binaries, whereas the S32Z2 peer is a
# bench-only dev tool where that trade-off was apparently never made.
#
# -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON / -DCMAKE_INSTALL_RPATH='$ORIGIN/lib'
# (Important #3, part 1): without CMAKE_BUILD_WITH_INSTALL_RPATH, CMake
# stamps binaries in the *build* tree with an automatically-computed RPATH
# pointing at the actual build-time absolute path of their shared-lib
# dependencies (here, /build-arm64/build/cyclonedds_host/out/lib -- a
# container-internal path that will not exist on the board). This project's
# CMakeLists.txt performs no `install()` step, so that build-tree RPATH is
# also the one that ships. Forcing CMAKE_INSTALL_RPATH to apply immediately
# at build time, with the $ORIGIN token (resolved by the dynamic loader at
# *load* time to "the directory containing this binary"), makes the shipped
# bundle's lib/ subdirectory discoverable no matter where the tarball is
# extracted on the board. $ORIGIN is single-quoted here so neither this
# heredoc's outer shell nor this inner script's shell expand it -- cmake
# must receive the literal two characters "$ORIGIN", for the loader to
# expand at runtime.
#
# This is belt-and-suspenders with the host-side patchelf step further down
# (Important #3, part 2): that step is the one actually verified via
# readelf -d before/after, further down in this same script, and remains
# authoritative even if some future CMake/toolchain change quietly stops
# honoring these two flags.
# Task 21: CONFIG_DDS_MAX_MSG_SIZE/CONFIG_DDS_MAX_REXMIT_MSG_SIZE/
# CONFIG_DDS_FRAGMENT_SIZE=434/434/348 -- same three DDS-sizing knobs, same
# values, as freertos_x5h/CMakeLists.txt sets for the CR52 firmware side of
# this identical 462-byte link (see that file's own comment, and common/dds/
# config.hpp's fuller derivation). Bundled into the same -DCMAKE_C_FLAGS/
# -DCMAKE_CXX_FLAGS string as CONFIG_DDS_DISABLE_MULTICAST=1, for the same
# reason: this reused CMakeLists.txt has no CACHE var for any of the four.
cmake -S /build-arm64/actuation_module/freertos_s32z2/edge_ecu_peer -B /build-arm64/build/peer-build \
    -DCONFIG_DDS_DOMAIN_ID=2 \
    -DCONFIG_DDS_NETWORK_INTERFACE=tap0 \
    -DCONFIG_DDS_PEER=172.16.52.2 \
    -DCMAKE_C_FLAGS="-DCONFIG_DDS_DISABLE_MULTICAST=1 -DCONFIG_DDS_MAX_MSG_SIZE=434 -DCONFIG_DDS_MAX_REXMIT_MSG_SIZE=434 -DCONFIG_DDS_FRAGMENT_SIZE=348" \
    -DCMAKE_CXX_FLAGS="-DCONFIG_DDS_DISABLE_MULTICAST=1 -DCONFIG_DDS_MAX_MSG_SIZE=434 -DCONFIG_DDS_MAX_REXMIT_MSG_SIZE=434 -DCONFIG_DDS_FRAGMENT_SIZE=348" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/lib'
cmake --build /build-arm64/build/peer-build -j"$(nproc)"

test -x /build-arm64/build/peer-build/edge_ecu_pub
test -x /build-arm64/build/peer-build/edge_ecu_sub
echo "--- file(1) on built binaries ---"
file /build-arm64/build/peer-build/edge_ecu_pub
file /build-arm64/build/peer-build/edge_ecu_sub
INNER
chmod +x "${WORK}/in-container-build.sh"

# Review finding (Minor): `podman run --arch arm64` only invokes qemu/binfmt
# emulation when the host itself is not already arm64 -- on a native arm64
# host (e.g. this repo's arm64 CI runner), --arch arm64 is a plain native
# run with no emulation at all, and a message that unconditionally says
# "emulated" is simply untrue there.
if [ "$(uname -m)" = "aarch64" ]; then
    echo "Running arm64 build in a native arm64v8/ubuntu:22.04 container (host is already aarch64, no emulation)..."
else
    echo "Running arm64 build in an emulated arm64v8/ubuntu:22.04 container (podman --arch arm64, host is $(uname -m))..."
fi
podman run --rm --arch arm64 \
    -v "${REPO_ROOT}/actuation_module:/build-arm64/actuation_module:ro,Z" \
    -v "${REPO_ROOT}/cyclonedds:/build-arm64/cyclonedds:ro,Z" \
    -v "${WORK}/build:/build-arm64/build:Z" \
    -v "${CACHE_DIR}:/build-arm64/build/cyclonedds_host:Z" \
    -v "${WORK}/in-container-build.sh:/build-arm64/in-container-build.sh:ro,Z" \
    "${IMAGE}" \
    bash /build-arm64/in-container-build.sh

# ---- Verify the binaries really are arm64 (from the host, no execution needed) ----
file "${WORK}/build/peer-build/edge_ecu_pub" | tee "${WORK}/file-edge_ecu_pub.txt"
file "${WORK}/build/peer-build/edge_ecu_sub" | tee "${WORK}/file-edge_ecu_sub.txt"
grep -q "ARM aarch64" "${WORK}/file-edge_ecu_pub.txt"
grep -q "ARM aarch64" "${WORK}/file-edge_ecu_sub.txt"

# ---- Important #3, part 2: authoritative RPATH fix-up via patchelf ----
# Runs in a NATIVE (host-arch) container -- no --arch flag, so no
# qemu/binfmt emulation at all, since patchelf only rewrites ELF metadata
# bytes and never executes the aarch64 target. This is the step actually
# proven correct via the readelf -d before/after check right below; the
# cmake flags above are a secondary, best-effort defense, not the assertion
# this script relies on.
echo "--- RUNPATH before patchelf fix-up ---"
readelf -d "${WORK}/build/peer-build/edge_ecu_pub" | grep -i runpath || echo "(none)"
podman run --rm \
    -v "${WORK}/build/peer-build:/work:Z" \
    "${PATCHELF_IMAGE}" \
    bash -c 'set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends patchelf >/dev/null
        patchelf --set-rpath "\$ORIGIN/lib" /work/edge_ecu_pub
        patchelf --set-rpath "\$ORIGIN/lib" /work/edge_ecu_sub'
echo "--- RUNPATH after patchelf fix-up ---"
readelf -d "${WORK}/build/peer-build/edge_ecu_pub" | tee "${WORK}/runpath-edge_ecu_pub-after.txt" | grep -i runpath
readelf -d "${WORK}/build/peer-build/edge_ecu_sub" | tee "${WORK}/runpath-edge_ecu_sub-after.txt" | grep -i runpath
grep -q '\$ORIGIN/lib' "${WORK}/runpath-edge_ecu_pub-after.txt"
grep -q '\$ORIGIN/lib' "${WORK}/runpath-edge_ecu_sub-after.txt"

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
# execute the aarch64 loader itself. Read from CACHE_DIR (see the cache/
# scratch split comment at the top of this file) -- not
# "${WORK}/build/cyclonedds_host", which no longer exists on the host side.
needed=$(readelf -d "${WORK}/build/peer-build/edge_ecu_pub" | awk -F'[][]' '/NEEDED/{print $2}')
for lib in ${needed}; do
    case "${lib}" in
        libddsc*)
            found="${CACHE_DIR}/out/lib/${lib}"
            if [ -e "${found}" ]; then
                cp -L "${found}" "${BUNDLE}/lib/${lib}"
            else
                # Review finding (Important 6): without this branch, a
                # missing source lib was a silent no-op -- the loop would
                # exit 0 either way, and the bundle would ship with a NEEDED
                # entry it cannot actually satisfy, failing only later, at
                # runtime on the board, with a bare "cannot open shared
                # object file" from the dynamic loader instead of failing
                # here, at build time, with the missing path spelled out.
                echo "ERROR: NEEDED entry '${lib}' not found at ${found}" >&2
                exit 1
            fi
            ;;
    esac
done
# Also grab the unversioned/major-versioned symlink targets so the bundle's
# lib/ dir is self-sufficient regardless of which SONAME the loader asks for.
cp -L "${CACHE_DIR}/out/lib"/libddsc.so* "${BUNDLE}/lib/" 2>/dev/null || true

# Review finding (Important 6): assert every libddsc* NEEDED entry actually
# landed in the bundle, as a second, independent check after both copy
# steps above -- not just "the per-entry copy above didn't fail", but "the
# bundle this tarball ships is actually complete". A future readelf/awk
# parsing change, or a CACHE_DIR layout change, could otherwise slip a
# bundle out the door that looks fine (`cp` succeeded, `tar` succeeded) but
# is missing a library the binaries need at runtime.
for lib in ${needed}; do
    case "${lib}" in
        libddsc*)
            if [ ! -e "${BUNDLE}/lib/${lib}" ]; then
                echo "ERROR: required NEEDED entry '${lib}' is missing from ${BUNDLE}/lib/" >&2
                exit 1
            fi
            ;;
    esac
done

rm -rf "${BUNDLE}"/lib/*.a 2>/dev/null || true

tar -C "${BUNDLE}" -czf "${OUT_TAR}" .

echo ""
echo "Wrote ${OUT_TAR}"
echo "--- tarball contents ---"
tar -tzf "${OUT_TAR}"
