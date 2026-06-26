#!/usr/bin/env bash
# Full B-2 verification: build, flash via s32dbg, start edge ECU peer,
# run DDS round-trip on the X-S32Z27X-DC kit.
set -euo pipefail

: "${S32_RTD_PATH:?Set S32_RTD_PATH}"
: "${FREERTOS_PATH:?Set FREERTOS_PATH}"
: "${LWIP_PATH:?Set LWIP_PATH}"
: "${S32CT_GENERATED_DIR:?Set S32CT_GENERATED_DIR to your S32 Config Tools project root}"
: "${S32DS_PATH:=/usr/local/NXP/S32DS.3.6.2}"
: "${ZEPHYR_VENV:=$HOME/zephyr-env}"
: "${UART_DEV:=/dev/ttyUSB0}"
: "${DISPLAY:=:99}"
: "${XVFB_ARGS:=-screen 0 1024x768x24}"
export DISPLAY

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

# s32dbg needs an X display; start a headless Xvfb if one isn't already up.
if ! pgrep -x Xvfb >/dev/null 2>&1; then
    nohup Xvfb "${DISPLAY}" ${XVFB_ARGS} > /tmp/xvfb.log 2>&1 &
    sleep 1
fi

# shellcheck source=/dev/null
source "${ZEPHYR_VENV}/bin/activate"

# Rebuild target firmware.
BUILD_DIR="${REPO_ROOT}/build/freertos-s32z2"
rm -rf "${BUILD_DIR}"
./build.sh --platform freertos-s32z2 -d "${BUILD_DIR}"

# Build the host-side edge ECU peer (dds_pub / dds_sub).
./actuation_module/freertos_s32z2/scripts/build-edge-ecu-peer.sh

CYCLONEDDS_XML=demo/cyclonedds.xml
[ -f demo/cyclonedds-s32z2.xml ] && CYCLONEDDS_XML=demo/cyclonedds-s32z2.xml
export CYCLONEDDS_URI=file://$(pwd)/${CYCLONEDDS_XML}

EDGE_PUB_LOG=/tmp/freertos-s32z2-b2-edge-pub.log
EDGE_SUB_LOG=/tmp/freertos-s32z2-b2-edge-sub.log
UART_LOG=/tmp/freertos-s32z2-b2-uart.log
rm -f "${EDGE_PUB_LOG}" "${EDGE_SUB_LOG}" "${UART_LOG}"

# Start UART capture and edge ECU pub before flashing so we don't miss the
# first markers after the kit boots.
( timeout 30s cat "${UART_DEV}" >"${UART_LOG}" 2>&1 ) &
UART_PID=$!
./build-edge-ecu-peer/edge_ecu_pub > "${EDGE_PUB_LOG}" 2>&1 &
PUB_PID=$!
trap 'kill ${PUB_PID} ${UART_PID} 2>/dev/null || true' EXIT

# nxp_s32dbg supports only `debug`; --batch makes the GDB session
# non-interactive. Retries with the demo-doc cleanup recipe between attempts.
attempt=0
until west debug --s32ds-path="${S32DS_PATH}" -d "${BUILD_DIR}" --tool-opt='--batch'; do
    attempt=$((attempt+1))
    if [ "${attempt}" -ge 3 ]; then
        echo "west debug failed 3 times; physical layer (power / J6 JTAG / J14 / J17-J18 / S2)?"
        exit 2
    fi
    pkill -9 -f 'gta|s32dbg|arm-none-eabi-gdb' 2>/dev/null || true
    rm -rf /tmp/*nxp_s32dbg* /tmp/tmp*nxp_s32dbg* 2>/dev/null || true
    sleep 5
done

timeout 30s ./build-edge-ecu-peer/edge_ecu_sub > "${EDGE_SUB_LOG}" 2>&1 || true
wait "${UART_PID}" 2>/dev/null || true

errors=0
grep -F "Controller Node Started"         "${UART_LOG}" || errors=$((errors+1))
grep -F "Actuation Safety Island is Live"  "${UART_LOG}" || errors=$((errors+1))
count=$(grep -cF "STEERING REPORT" "${EDGE_SUB_LOG}")
if [ "${count}" -lt 2 ]; then
    echo "STEERING REPORT count is ${count} (need >= 2)"
    errors=$((errors+1))
fi
# CONTROL CMD is the actual round-trip evidence: it is published by the board
# after it ran MPC/PID, whereas STEERING REPORT above is only the host
# publisher's own loopback. At the -O2 control cycle (~3.5 s) at least one
# CONTROL CMD should land inside the 30 s edge_ecu_sub window.
cmd_count=$(grep -cF "CONTROL CMD" "${EDGE_SUB_LOG}")
if [ "${cmd_count}" -lt 1 ]; then
    echo "CONTROL CMD count is ${cmd_count} (need >= 1; no board round-trip observed)"
    errors=$((errors+1))
fi
if grep -qF "actuation_main returned" "${UART_LOG}"; then
    echo "actuation_main returned — controller loop died during verification"
    errors=$((errors+1))
fi

if [ "${errors}" -eq 0 ]; then
    echo "B-2 verification OK"
    exit 0
else
    echo "B-2 verification FAILED (${errors} check failures)"
    exit 1
fi
