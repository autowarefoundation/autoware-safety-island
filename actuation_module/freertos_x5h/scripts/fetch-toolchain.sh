#!/usr/bin/env bash
# Downloads and unpacks the pinned Arm GNU 13.2.Rel1 arm-none-eabi toolchain
# into build/toolchain/ (idempotent: a re-run with a valid extracted toolchain
# already in place skips the download). Prints the bin/ path on stdout so
# callers can do `PATH="$(fetch-toolchain.sh):$PATH"`.
#
# Network note: this hits a public Arm developer.arm.com URL that 302-redirects
# to an Azure blob (~179 MB). Network access is required to run this script.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEST="$ROOT/build/toolchain"
TARBALL_URL="https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz"
BIN="$DEST/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin"

if [ ! -x "$BIN/arm-none-eabi-gcc" ]; then
  mkdir -p "$DEST"
  echo "fetch-toolchain: downloading arm-gnu-toolchain-13.2.rel1 (~179 MB)..." >&2
  curl -fL "$TARBALL_URL" | tar -xJ -C "$DEST"
fi

VERSION_OUT="$("$BIN/arm-none-eabi-gcc" --version)"
echo "$VERSION_OUT" | grep -q '13\.2\.1 20231009' || {
  echo "ERROR: toolchain at $BIN is not 13.2.Rel1 (arm-none-eabi-gcc 13.2.1 20231009)." >&2
  echo "Got: $(echo "$VERSION_OUT" | head -1)" >&2
  exit 1
}

echo "$BIN"
