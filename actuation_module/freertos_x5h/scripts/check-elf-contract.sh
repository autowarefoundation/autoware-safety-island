#!/usr/bin/env bash
# Asserts a CR52 firmware ELF satisfies the frozen Linux-facing contract shared
# by every actuation_module/freertos_x5h firmware target (this script is run
# on every later firmware, not just the BSP rpmsg sample built by
# build-bsp-rpmsg-sample.sh):
#   - it is an ARM ELF
#   - the executable LOAD segment (.text) starts at exactly 0x11600000, inside
#     the Core1 boot-slot window 0x11600000 .. 0x12000000 (10 MiB)
#   - every LOAD segment starts inside either the slot window or the
#     resource-table region 0x96650000 .. 0x96651000 (see "Carveout" below)
#   - the .resource_table section sits at exactly 0x96650000 and is exactly
#     0x100 bytes (the struct's `packed, aligned(0x100)` forces this size;
#     see actuation_module/freertos_x5h/AUDIT.md Section 5)
#   - the resource table's vdev entry is RSC_VDEV(type=3)/VIRTIO_ID_RPMSG(id=7)
#     with notifyid=31, dfeatures=1, num_of_vrings=2, and both vring
#     descriptors have da=0xFFFFFFFF, align=0x1000, num=0x100
#
# Known non-coverage: which MFIS channel the firmware talks to at runtime is
# baked in by which build target was built, not by anything that shows up in
# the static ELF/resource-table content. It is NOT checked by this script and
# must be verified by construction (which target was built) or on the board.
#
# Carveout: the frozen contract's Linux-side reserved region "cr52_ram1" spans
# 0x96650000-0x9669FFFF. Firmware may legitimately place LOAD content in only
# the first 0x1000 bytes of that region -- the .resource_table itself, per the
# BSP's own linker region `remote_proc_rsc_table_1` (see AUDIT.md Section 5).
# The remainder of cr52_ram1 (0x96651000-0x9669FFFF) is where Linux/remoteproc
# dynamically allocates the two vrings after boot (each vring's resource-table
# entry has da=0xFFFFFFFF, i.e. "address chosen by Linux, not the firmware" --
# see the vdev/vring checks below); firmware must not place LOAD content
# there, since that would collide with Linux's allocation. Hence RSC_HI below
# is 0x96651000 (the firmware-loadable subregion), not the full carveout end
# 0x966A0000.
#
# Pipe-and-early-exit hazard: every readelf invocation below is captured into
# a shell variable via plain command substitution BEFORE any pattern matching
# happens. That much was already true in an earlier revision of this script
# and is NOT sufficient by itself: capturing readelf's output only guarantees
# readelf itself has exited before this script inspects the result, it says
# nothing about whatever a LATER stage does with that captured string. A
# revision of this script still piped the captured variable into a
# quits-on-first-match consumer (`printf '%s\n' "$var" | grep -q ...`, or an
# awk action containing `exit`), which recreates the exact same hazard one
# step downstream: `grep -q`/`awk {exit}` can stop reading as soon as it finds
# a match, closing its end of the pipe while `printf` still has unwritten
# bytes queued for a full pipe buffer (64 KiB on Linux) -- `printf` then takes
# SIGPIPE and exits 141, and under `set -o pipefail` (below) bash reports the
# whole pipeline's status as the last non-zero exit among its stages scanning
# right-to-left, i.e. 141, even though the consumer (rightmost, grep/awk)
# itself exited 0 (found its match). That 141 is indistinguishable from
# "pattern not found" to a `|| fail` guard, so it fires for the wrong reason
# even though the ELF is fully conforming. This is size-dependent, not
# flaky-timing-dependent: it reproduced 5/5 on this ELF's real 341,776-byte
# `readelf -p .rodata` dump (comfortably over the 64 KiB pipe-buffer
# threshold) and 0/5 on the header dump used for the ARM-machine check
# earlier in this script, which is only a few hundred bytes. The fix actually
# applied below is not "capture first" (already tried, insufficient) but
# eliminating the live pipe at the matching step entirely: either a pure
# in-process bash match (`[[ ... =~ ... ]]`, `case ... in *pattern*)`, no fork
# at all) where only presence/absence is being tested, or feeding the already
# -captured variable to awk via a here-string (`<<< "$var"`) where field
# extraction is needed -- a here-string is backed by a temp file bash writes
# and closes before the reader ever starts, so there is no live writer for a
# quits-early reader to SIGPIPE, regardless of consumer behavior or data
# size. Verified empirically for both shapes (grep -q and an early-exiting
# awk action) against synthetic worst-case inputs (the match at the very
# front of hundreds of KB of trailing data): the herestring/pure-match form
# was 5/5 clean where the old piped form was 5/5 failing.
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

is_hex() { [[ "$1" =~ ^[0-9a-fA-F]+$ ]]; }

hex_to_dec() { # hex_digits (no 0x prefix) -> decimal on stdout
  is_hex "$1" || fail "expected a hex value, got '$1'"
  echo $((16#$1))
}

[ -n "$ELF" ] || fail "usage: check-elf-contract.sh <elf> [expected-service-name]"
[ -f "$ELF" ] || fail "no such file: $ELF"

hdr_dump="$(readelf -h "$ELF" 2>/dev/null || true)"
[ -n "$hdr_dump" ] || fail "readelf could not parse '$ELF' as an ELF file"
# Pure in-process bash regex match (no fork, no pipe) -- see the
# "Pipe-and-early-exit hazard" comment above for why this is not a
# `printf ... | grep -q ...` pipe.
[[ "$hdr_dump" =~ Machine:[[:space:]]*ARM ]] || fail "not an ARM ELF: $ELF"

SLOT_LO=$(hex_to_dec 11600000)
SLOT_HI=$(hex_to_dec 12000000)
RSC_LO=$(hex_to_dec 96650000)
RSC_HI=$(hex_to_dec 96651000) # firmware-loadable subregion only -- see "Carveout" above

in_range() { # addr lo hi
  [ "$1" -ge "$2" ] && [ "$1" -lt "$3" ]
}

# readelf -lW program-header columns (verified against actual output on this
# machine): Type Offset VirtAddr PhysAddr FileSiz MemSiz Flg Align -- so $1 is
# the segment Type, $3 is VirtAddr, $7 is Flg. The awk step below never exits
# early (no `exit` in its action, and it is only ever asked to match "LOAD"
# rows -- there is no early quit once a first match is found), so it always
# lets readelf finish writing on its own; there is no early-exit hazard here.
load_count=0
found_slot_load=0
exec_load_addr=""
while read -r addr_hex flags_field; do
  [ -n "$addr_hex" ] || continue
  load_count=$((load_count + 1))
  is_hex "${addr_hex#0x}" || fail "readelf -lW produced a non-hex LOAD VirtAddr: '$addr_hex'"
  addr=$((16#${addr_hex#0x}))
  if in_range "$addr" "$SLOT_LO" "$SLOT_HI"; then
    found_slot_load=1
    if [[ "$flags_field" == *E* ]]; then
      exec_load_addr="$addr"
    fi
  elif ! in_range "$addr" "$RSC_LO" "$RSC_HI"; then
    fail "LOAD segment at $addr_hex is outside both the slot window (0x11600000-0x12000000) and the resource-table region (0x96650000-0x96651000)"
  fi
done < <(readelf -lW "$ELF" | awk '$1 == "LOAD" {print $3, $7}')

[ "$load_count" -gt 0 ] || fail "no LOAD segments found in '$ELF'"
[ "$found_slot_load" -eq 1 ] || fail "no LOAD segment starts inside the Core1 slot window 0x11600000-0x12000000"
[ -n "$exec_load_addr" ] || fail "no executable (Flg=E) LOAD segment found -- expected the .text-bearing segment"
[ "$exec_load_addr" -eq "$SLOT_LO" ] || fail ".text-bearing LOAD segment starts at 0x$(printf '%x' "$exec_load_addr"), expected exactly 0x11600000"

# readelf -SW section-header columns. readelf pads the section index with
# %2u, so index 4 prints as "[ 4]" and naive whitespace field-splitting
# treats "[" and "4]" as two separate fields, shifting every column after it
# by one for any section indexed below 10 (empirically confirmed: this ELF's
# own .rodata is section "[ 4]", and the old field numbers ($2/$3/$4/$6)
# matched nothing for it even though they matched .resource_table's
# two-digit "[14]" -- i.e. the bug was real and present in this exact ELF,
# not merely hypothetical). Strip the bracketed index (and surrounding
# spaces) from $0 before field-splitting so the field numbers below are
# stable regardless of index width: after stripping, $1=Name, $2=Type,
# $3=Addr, $4=Off, $5=Size. Captured into a variable first, then fed to awk
# via a here-string (`<<<`, not a pipe) since this awk action does contain
# `exit` -- the exact early-exit shape flagged in the "Pipe-and-early-exit
# hazard" comment above. A here-string is backed by a temp file bash writes
# and closes before awk starts reading, so there is no live writer for awk's
# early exit to signal, regardless of how large `sw_dump` grows or where in
# it `.resource_table` sits (currently near the end of the section list, so
# this is not exploitable today with a live pipe either, but the fix should
# not depend on that happening to stay true).
sw_dump="$(readelf -SW "$ELF" 2>/dev/null || true)"
[ -n "$sw_dump" ] || fail "readelf -SW produced no output for '$ELF'"
rsc_line="$(awk '{ sub(/^[ \t]*\[[ 0-9]+\][ \t]*/, "") } $1 == ".resource_table" && $2 == "PROGBITS" { print; exit }' <<< "$sw_dump")"
[ -n "$rsc_line" ] || fail ".resource_table section not found in '$ELF'"

rsc_addr_hex="$(printf '%s' "$rsc_line" | awk '{print $3}')"
rsc_size_hex="$(printf '%s' "$rsc_line" | awk '{print $5}')"
is_hex "$rsc_addr_hex" || fail "readelf -SW produced a non-hex .resource_table address: '$rsc_addr_hex'"
is_hex "$rsc_size_hex" || fail "readelf -SW produced a non-hex .resource_table size: '$rsc_size_hex'"
rsc_addr=$((16#$rsc_addr_hex))
rsc_size=$((16#$rsc_size_hex))

[ "$rsc_addr" -eq "$RSC_LO" ] || fail ".resource_table is at 0x$(printf '%x' "$rsc_addr"), expected 0x96650000"
[ "$rsc_size" -eq $((16#100)) ] || fail ".resource_table size is 0x$(printf '%x' "$rsc_size"), expected 0x100"

# Decode the vdev entry (struct fw_rsc_vdev) and both vring descriptors
# (struct fw_rsc_vdev_vring) against the frozen contract's fixed values.
# These are the public, widely-published remoteproc resource-table structs
# (Linux kernel / OpenAMP upstream headers, not vendor/NDA content). The
# byte-level derivation is spelled out here rather than deferred elsewhere,
# because this script is the only place the frozen contract is enforced.
# The vdev entry sits at a fixed byte offset (0x30) from the table base: struct
# remote_resource_table's header is version(4)+num(4)+reserved[2](8)+
# offset[8](32) = 0x30 bytes, always, before its one resource entry (see
# AUDIT.md Section 5 / rsc_table.h); the two vring descriptors immediately
# follow the vdev entry, each 20 bytes (5 u32 fields).
#
# Built by flattening readelf -x's hex dump into one continuous byte string
# (in file order) and slicing it by absolute byte offset, rather than by
# matching text that happens to be adjacent within readelf's line-wrapped
# dump -- so a match cannot depend on two fields staying on the same printed
# line, and each field below is anchored to its own offset independently of
# every other field.
rsc_hex="$(readelf -x .resource_table "$ELF" 2>/dev/null | awk '/^[[:space:]]*0x[0-9a-fA-F]+[[:space:]]/ {printf "%s%s%s%s", $2, $3, $4, $5}')"
[ "${#rsc_hex}" -eq $((0x100 * 2)) ] || fail "readelf -x .resource_table yielded ${#rsc_hex} hex chars, expected 512 (0x100 bytes)"

field() { # byte_offset byte_length -> lowercase hex substring on stdout
  echo "${rsc_hex:$(($1 * 2)):$(($2 * 2))}"
}

[ "$(field 0x30 4)" = "03000000" ] || fail "vdev.type is not RSC_VDEV (3): got 0x$(field 0x30 4)"
[ "$(field 0x34 4)" = "07000000" ] || fail "vdev.id is not VIRTIO_ID_RPMSG (7): got 0x$(field 0x34 4)"
[ "$(field 0x38 4)" = "1f000000" ] || fail "vdev.notifyid is not 31 (0x1f): got 0x$(field 0x38 4)"
[ "$(field 0x3c 4)" = "01000000" ] || fail "vdev.dfeatures is not 1: got 0x$(field 0x3c 4)"
[ "$(field 0x49 1)" = "02" ] || fail "vdev.num_of_vrings is not 2: got 0x$(field 0x49 1)"

[ "$(field 0x4c 4)" = "ffffffff" ] || fail "vring[0].da is not 0xFFFFFFFF: got 0x$(field 0x4c 4)"
[ "$(field 0x50 4)" = "00100000" ] || fail "vring[0].align is not 0x1000: got 0x$(field 0x50 4)"
[ "$(field 0x54 4)" = "00010000" ] || fail "vring[0].num is not 0x100: got 0x$(field 0x54 4)"

[ "$(field 0x60 4)" = "ffffffff" ] || fail "vring[1].da is not 0xFFFFFFFF: got 0x$(field 0x60 4)"
[ "$(field 0x64 4)" = "00100000" ] || fail "vring[1].align is not 0x1000: got 0x$(field 0x64 4)"
[ "$(field 0x68 4)" = "00010000" ] || fail "vring[1].num is not 0x100: got 0x$(field 0x68 4)"

if [ -n "$SVC" ]; then
  svc_dump="$(readelf -p .rodata "$ELF" 2>/dev/null || true)"
  # Pure in-process bash substring match (a `case` glob test, no fork, no
  # pipe at all) -- this is the exact site the "Pipe-and-early-exit hazard"
  # comment above describes: this ELF's real .rodata dump is 341,776 bytes,
  # `grep -qF` finds "$SVC" and exits long before `printf` finishes writing
  # that many bytes into a 64 KiB pipe, and the resulting SIGPIPE-under-
  # pipefail reads as "service string not found" even when it is present.
  # `case` never spawns a reader that can race the (nonexistent, here)
  # writer, so there is nothing to signal regardless of dump size.
  case "$svc_dump" in
    *"$SVC"*) ;;
    *) fail "service string '$SVC' not found in .rodata" ;;
  esac
fi

echo "CONTRACT_PASS $ELF"
