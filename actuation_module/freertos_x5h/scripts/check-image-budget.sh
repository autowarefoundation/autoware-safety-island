#!/usr/bin/env bash
# Asserts the firmware fits the frozen Core1 boot-slot memory budget: every
# LOAD segment's MemSiz must fall entirely inside either the 10 MiB Core1
# slot window (0x11600000-0x12000000) or the firmware-loadable subregion of
# the resource-table carveout (0x96650000-0x96651000 -- see
# check-elf-contract.sh's "Carveout" comment for why that subregion, not the
# full carveout, is the firmware's to use). Reports the slot window's used
# bytes and percentage on success.
#
# This is the spec's memory-risk gate for Task 4 (lwIP + CycloneDDS +
# actuation module full link): if this ever prints BUDGET_FAIL, the fix is
# to shrink lwIP/CycloneDDS static pools, NOT to move the resource table,
# grow the slot window, or edit this script.
#
# The task brief's own draft of this script uses awk's strtonum() to parse
# the hex VirtAddr/MemSiz fields from `readelf -lW`. strtonum() is a gawk
# extension; the default `awk` on this system is mawk, where it is
# undefined -- confirmed by running `awk 'BEGIN{print strtonum("10")}'`,
# which errors with "calling undefined function strtonum". This script
# follows check-elf-contract.sh's already-established fix for the same
# problem: do all hex arithmetic in bash's own $((16#...)) instead of in awk.
#
# Pipe-and-early-exit hazard: same as check-elf-contract.sh. Every readelf
# invocation here is either captured into a variable before being matched,
# or (the LOAD-segment loop below) piped only into an awk whose action never
# exits early -- so there is no live readelf writer left for a downstream
# early-quitting consumer to SIGPIPE under `set -o pipefail`.
#
# Usage: check-image-budget.sh <elf>
# Exit 0 iff every LOAD segment fits its region (prints BUDGET_PASS <elf>).
# Exit 1 with "BUDGET_FAIL: <reason>" on stderr otherwise.
set -euo pipefail

ELF="${1:-}"

fail() {
  echo "BUDGET_FAIL: $1" >&2
  exit 1
}

is_hex() { [[ "$1" =~ ^[0-9a-fA-F]+$ ]]; }

hex_to_dec() { # hex_digits (no 0x prefix) -> decimal on stdout
  is_hex "$1" || fail "expected a hex value, got '$1'"
  echo $((16#$1))
}

[ -n "$ELF" ] || fail "usage: check-image-budget.sh <elf>"
[ -f "$ELF" ] || fail "no such file: $ELF"

hdr_dump="$(readelf -h "$ELF" 2>/dev/null || true)"
[ -n "$hdr_dump" ] || fail "readelf could not parse '$ELF' as an ELF file"

# Core1 boot-slot window: 10 MiB (0xA00000) starting at 0x11600000.
SLOT_LO=$(hex_to_dec 11600000)
SLOT_SIZE=$(hex_to_dec A00000)
SLOT_HI=$((SLOT_LO + SLOT_SIZE))
# Resource-table carveout's firmware-loadable subregion (the rest of the
# carveout is where Linux/remoteproc allocates the vrings after boot -- see
# check-elf-contract.sh). Firmware LOAD content must stay out of that part.
RSC_LO=$(hex_to_dec 96650000)
RSC_HI=$(hex_to_dec 96651000)

# readelf -lW program-header columns (same layout check-elf-contract.sh
# already verified for this ELF): Type Offset VirtAddr PhysAddr FileSiz
# MemSiz Flg Align -- $1 Type, $3 VirtAddr, $6 MemSiz. The awk step below
# never exits early (no `exit` in its action), so it always lets readelf
# finish writing on its own.
slot_used=0
load_count=0
while read -r addr_hex size_hex; do
  [ -n "$addr_hex" ] || continue
  load_count=$((load_count + 1))
  is_hex "${addr_hex#0x}" || fail "readelf -lW produced a non-hex LOAD VirtAddr: '$addr_hex'"
  is_hex "${size_hex#0x}" || fail "readelf -lW produced a non-hex LOAD MemSiz: '$size_hex'"
  addr=$((16#${addr_hex#0x}))
  size=$((16#${size_hex#0x}))
  end=$((addr + size))
  if [ "$addr" -ge "$SLOT_LO" ] && [ "$end" -le "$SLOT_HI" ]; then
    slot_used=$((slot_used + size))
  elif [ "$addr" -ge "$RSC_LO" ] && [ "$end" -le "$RSC_HI" ]; then
    continue
  else
    fail "segment $addr_hex size $size_hex (end 0x$(printf '%x' "$end")) outside both the slot window (0x11600000-0x$(printf '%x' "$SLOT_HI")) and the resource-table subregion (0x96650000-0x96651000)"
  fi
done < <(readelf -lW "$ELF" | awk '$1 == "LOAD" {print $3, $6}')

[ "$load_count" -gt 0 ] || fail "no LOAD segments found in '$ELF'"

# One-decimal percentage, not integer division (review round 1 fix): plain
# `$((slot_used * 100 / SLOT_SIZE))` truncates, so e.g. 99.9% used would
# have printed "99%" -- indistinguishable at a glance from genuinely
# comfortable headroom. Scaling by 1000 before the integer divide keeps
# this in bash arithmetic (no external bc/awk float dependency, matching
# this script's own header comment about avoiding gawk-only features).
pct_x10=$((slot_used * 1000 / SLOT_SIZE))
pct_int=$((pct_x10 / 10))
pct_frac=$((pct_x10 % 10))
printf 'BUDGET_PASS: slot window used 0x%x of 0x%x (%d.%d%%)\n' "$slot_used" "$SLOT_SIZE" "$pct_int" "$pct_frac"
if [ "$pct_x10" -ge 900 ]; then
  echo "BUDGET_WARN: slot window is above 90% full (${pct_int}.${pct_frac}%); headroom for Task 6's netif bring-up is getting tight." >&2
fi
