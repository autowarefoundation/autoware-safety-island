#!/usr/bin/env bash
# Asserts a CR52 firmware ELF satisfies the frozen Linux-facing contract shared
# by every actuation_module/freertos_x5h firmware target (this script is run
# on every later firmware, not just the BSP rpmsg sample built by
# build-bsp-rpmsg-sample.sh):
#   - it is an ARM ELF
#   - at least one LOAD segment starts inside the Core1 boot-slot window
#     0x11600000 .. 0x12000000 (10 MiB) -- that is where .text lives
#   - every LOAD segment starts inside either the slot window or the
#     resource-table carveout 0x96650000 .. 0x966A0000 (0x1000, per the linker
#     region) -- nothing may load outside the addresses Linux/remoteproc knows
#     about
#   - the .resource_table section sits at exactly 0x96650000 and is exactly
#     0x100 bytes (the struct's `packed, aligned(0x100)` forces this size;
#     see actuation_module/freertos_x5h/AUDIT.md Section 5)
#   - the resource table bytes encode an RSC_VDEV entry (type=3) with
#     VIRTIO_ID_RPMSG (id=7)
#
# All address/size arithmetic is done in bash ($((16#...))) rather than with
# awk's strtonum(): strtonum() is a gawk extension, and this repo's CI/dev
# images default to mawk, which does not implement it. Piping mawk through
# `awk '...strtonum...' || fail "<message>"` makes awk itself exit non-zero on
# the parse error, so the script would print the *wrong* CONTRACT_FAIL reason
# (an awk failure, not the actual contract check). Keeping the arithmetic in
# bash removes the gawk dependency entirely and keeps failure messages honest.
#
# Usage: check-elf-contract.sh <elf> [expected-service-name]
# Exit 0 iff the ELF satisfies the contract (prints CONTRACT_PASS <elf>).
# Exit 1 with "CONTRACT_FAIL: <reason>" on stderr otherwise.
set -euo pipefail

ELF="${1:-}"
SVC="${2:-}"

fail() {
  echo "CONTRACT_FAIL: $1" >&2
  exit 1
}

[ -n "$ELF" ] || fail "usage: check-elf-contract.sh <elf> [expected-service-name]"
[ -f "$ELF" ] || fail "no such file: $ELF"

readelf -h "$ELF" >/dev/null 2>&1 || fail "readelf could not parse '$ELF' as an ELF file"
readelf -h "$ELF" | grep -qE 'Machine:\s*ARM' || fail "not an ARM ELF: $ELF"

SLOT_LO=$((16#11600000))
SLOT_HI=$((16#12000000))
RSC_LO=$((16#96650000))
RSC_HI=$((16#966A0000))

in_range() { # addr lo hi
  [ "$1" -ge "$2" ] && [ "$1" -lt "$3" ]
}

# readelf -lW program-header columns (verified against actual output on this
# machine, both 32-bit and 64-bit ELF): Type Offset VirtAddr PhysAddr FileSiz
# MemSiz Flg Align -- so $1 is the segment Type and $3 is VirtAddr.
load_count=0
found_slot_load=0
while read -r addr_hex; do
  [ -n "$addr_hex" ] || continue
  load_count=$((load_count + 1))
  addr=$((16#${addr_hex#0x}))
  if in_range "$addr" "$SLOT_LO" "$SLOT_HI"; then
    found_slot_load=1
  elif ! in_range "$addr" "$RSC_LO" "$RSC_HI"; then
    fail "LOAD segment at $addr_hex is outside both the slot window (0x11600000-0x12000000) and the resource-table carveout (0x96650000-0x966A0000)"
  fi
done < <(readelf -lW "$ELF" | awk '$1 == "LOAD" {print $3}')

[ "$load_count" -gt 0 ] || fail "no LOAD segments found in '$ELF'"
[ "$found_slot_load" -eq 1 ] || fail "no LOAD segment starts inside the Core1 slot window 0x11600000-0x12000000"

# readelf -SW section-header columns (verified against actual output):
# [Nr] Name Type Addr Off Size ES Flg Lk Inf Al -- so $1 is "[Nr]", $2 is
# Name, $3 is Type, $4 is Addr, $5 is Off, $6 is Size.
rsc_line="$(readelf -SW "$ELF" | awk '$2 == ".resource_table" && $3 == "PROGBITS" {print; exit}')"
[ -n "$rsc_line" ] || fail ".resource_table section not found in '$ELF'"

rsc_addr_hex="$(echo "$rsc_line" | awk '{print $4}')"
rsc_size_hex="$(echo "$rsc_line" | awk '{print $6}')"
rsc_addr=$((16#$rsc_addr_hex))
rsc_size=$((16#$rsc_size_hex))

[ "$rsc_addr" -eq "$RSC_LO" ] || fail ".resource_table is at 0x$(printf '%x' "$rsc_addr"), expected 0x96650000"
[ "$rsc_size" -eq $((16#100)) ] || fail ".resource_table size is 0x$(printf '%x' "$rsc_size"), expected 0x100"

# RSC_VDEV (type=3) with VIRTIO_ID_RPMSG (id=7): consecutive little-endian
# u32 fields "03000000 07000000" in the section's byte dump.
readelf -x .resource_table "$ELF" | grep -q '03000000 07000000' \
  || fail "resource table lacks the RSC_VDEV(type=3)/VIRTIO_ID_RPMSG(id=7) byte pair"

if [ -n "$SVC" ]; then
  readelf -p .rodata "$ELF" 2>/dev/null | grep -q "$SVC" \
    || fail "service string '$SVC' not found in .rodata"
fi

echo "CONTRACT_PASS $ELF"
