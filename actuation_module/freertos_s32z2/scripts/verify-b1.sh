#!/usr/bin/env bash
# Run on the development host.
# Build, flash, and validate the B-1 bring-up on the X-S32Z27X-DC kit.
set -euo pipefail

: "${S32_RTD_PATH:?Set S32_RTD_PATH to the extracted NXP RTD root}"
: "${FREERTOS_PATH:?Set FREERTOS_PATH to the extracted NXP FreeRTOS module root}"
: "${LWIP_PATH:?Set LWIP_PATH to the extracted NXP TCP/IP Stack root}"
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
# Without this the s32dbg session aborts at SoC connect with
# `CCS: connection to server refused`.
if ! pgrep -x Xvfb >/dev/null 2>&1; then
    nohup Xvfb "${DISPLAY}" ${XVFB_ARGS} > /tmp/xvfb.log 2>&1 &
    sleep 1
fi

# shellcheck source=/dev/null
source "${ZEPHYR_VENV}/bin/activate"

BUILD_DIR="${REPO_ROOT}/build/freertos-s32z2"
rm -rf "${BUILD_DIR}"
./build.sh --platform freertos-s32z2 -d "${BUILD_DIR}"

LOG=/tmp/freertos-s32z2-b1-uart.log
rm -f "${LOG}"

# Capture UART9 in the background while west loads the ELF non-interactively.
( timeout 30s cat "${UART_DEV}" >"${LOG}" 2>&1 ) &
UART_PID=$!
trap 'kill ${UART_PID} 2>/dev/null || true' EXIT

# nxp_s32dbg only supports `debug`; --batch makes the GDB session non-interactive.
# Retries up to 3x with the cleanup recipe from ces2026-demo.md §6.4 between
# attempts.
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

# Let UART capture finish (the timeout above bounds it).
wait "${UART_PID}" 2>/dev/null || true

# Boot success markers actually emitted by this firmware: freertos_main.cpp
# prints the startup banner, and the controller reaches the "Live" marker once
# the scheduler and board bring-up succeed. (There is no separate heartbeat
# build; the S32Z2 target boots straight into the full controller.)
if ! grep -Fq "FreeRTOS S32Z2 actuation starting..." "${LOG}"; then
    echo "B-1 verification FAILED: startup banner not seen on ${UART_DEV}"
    exit 1
fi
if ! grep -Fq "Actuation Safety Island is Live" "${LOG}"; then
    echo "B-1 verification FAILED: booted but never reached the controller Live marker"
    exit 1
fi
echo "B-1 verification OK (startup banner + Live marker seen)"
