#!/usr/bin/env bash
# Downloads and unpacks the pinned Arm GNU 13.2.Rel1 arm-none-eabi toolchain
# into build/toolchain/ (idempotent: a re-run with a valid extracted toolchain
# already in place skips the download). Prints the bin/ path on stdout so
# callers can do `PATH="$(fetch-toolchain.sh):$PATH"`.
#
# Network note: this hits a public Arm developer.arm.com URL that 302-redirects
# to an Azure blob (~179 MB). Network access is required to run this script.
#
# Safety: this artifact becomes board-flashed firmware's build toolchain, so
# the downloaded tarball is verified against a pinned sha256 before it is
# ever extracted (TARBALL_SHA256 below, computed once from a verified
# download of TARBALL_URL and frozen here). The download and extraction
# happen in a private temp directory that is only `mv`'d into its final
# location (BIN's parent) after both the checksum and the extracted
# toolchain's --version output have been verified -- so a run that is
# interrupted midway (killed, network drop, disk full) can never leave a
# partial toolchain tree sitting at the final path that would then
# incorrectly satisfy the `[ -x "$BIN/arm-none-eabi-gcc" ]` idempotency guard
# on the next invocation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEST="$ROOT/build/toolchain"
TARBALL_URL="https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz"
TARBALL_SHA256="6cd1bbc1d9ae57312bcd169ae283153a9572bd6a8e4eeae2fedfbc33b115fdbb"
TOOLCHAIN_DIRNAME="arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi"
BIN="$DEST/$TOOLCHAIN_DIRNAME/bin"

if [ ! -x "$BIN/arm-none-eabi-gcc" ]; then
  mkdir -p "$DEST"
  TMPDIR="$(mktemp -d "$DEST/.fetch-XXXXXX")"
  trap 'rm -rf "$TMPDIR"' EXIT
  echo "fetch-toolchain: downloading arm-gnu-toolchain-13.2.rel1 (~179 MB)..." >&2
  curl -fL -o "$TMPDIR/toolchain.tar.xz" "$TARBALL_URL"
  echo "$TARBALL_SHA256  $TMPDIR/toolchain.tar.xz" | sha256sum -c - || {
    echo "ERROR: downloaded toolchain tarball does not match the pinned sha256 $TARBALL_SHA256." >&2
    exit 1
  }
  mkdir -p "$TMPDIR/extracted"
  tar -xJ -C "$TMPDIR/extracted" -f "$TMPDIR/toolchain.tar.xz"
  [ -x "$TMPDIR/extracted/$TOOLCHAIN_DIRNAME/bin/arm-none-eabi-gcc" ] || {
    echo "ERROR: extracted tarball does not contain $TOOLCHAIN_DIRNAME/bin/arm-none-eabi-gcc." >&2
    exit 1
  }
  # Atomic publish: rename the fully-verified, fully-extracted directory
  # into place. mv within the same filesystem (TMPDIR is a subdirectory of
  # DEST) is a single rename syscall, so there is no window where a partial
  # tree is visible at the final path.
  mv "$TMPDIR/extracted/$TOOLCHAIN_DIRNAME" "$DEST/$TOOLCHAIN_DIRNAME"
fi

VERSION_OUT="$("$BIN/arm-none-eabi-gcc" --version)"
echo "$VERSION_OUT" | grep -q '13\.2\.1 20231009' || {
  echo "ERROR: toolchain at $BIN is not 13.2.Rel1 (arm-none-eabi-gcc 13.2.1 20231009)." >&2
  echo "Got: $(echo "$VERSION_OUT" | head -1)" >&2
  exit 1
}

echo "$BIN"
