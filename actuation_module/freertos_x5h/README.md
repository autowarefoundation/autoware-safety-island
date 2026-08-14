# FreeRTOS on Renesas R-Car X5H — Build and Verification

This directory contains the FreeRTOS build for the Renesas R-Car Gen5 X5H
"Ironhide" board (Cortex-R52, Core1), running as a remoteproc-managed
firmware image alongside Linux on the same SoC.

## Status

`actuation_x5h.elf` **links** the full actuation module — CycloneDDS, the
controller, Eigen, and lwIP — but does not yet **run** it. Distinguishing the
two matters here (review finding: an earlier revision of this section said the
actuation module "lands in Task 4" as if it were still future work, when the
full link landed in this PR):

- Linked, in this PR: the actuation module, CycloneDDS cross-built as a
  static library, lwIP, and the frozen resource-table/RPMsg glue. Everything
  the ELF contract and the image budget are asserted against is therefore
  real, not a placeholder.
- Not yet running: `main()` (`freertos_main.cpp`) still starts only the
  scaffold task, which boots the R-Car BSP, brings up the console, starts the
  FreeRTOS scheduler and prints `X5H_SCAFFOLD_ALIVE` once per second. It does
  not call `configure_network()` or the controller.
- Real lwIP-over-RPMsg bring-up lands in Task 6 (`lwip_bring_up_blocking()`
  is currently a weak stub returning 0).
- The RPMsg transport endpoint lands in Task 7.

The ELF's memory layout is already frozen and verified by
`scripts/check-elf-contract.sh`: `.text` at `0x11600000` (the Core1 `vram2`
slot window) and `.resource_table` at `0x96650000` (the fixed remoteproc
resource-table carveout Linux's remoteproc driver reads), both produced by
the BSP's own unmodified linker scripts. This is deliberate: later tasks
extend the same target rather than re-deriving the layout.

## Prerequisites

Everything here is public — no vendor portal downloads, no NDA-gated SDK,
no environment variables to export.

- Three git submodules: the R-Car FreeRTOS BSP (`rcar_bsp`), lwIP, and
  CycloneDDS. `--recursive` is required, not optional: `rcar_bsp` carries the
  FreeRTOS kernel as its own nested submodule at `FreeRTOS/Source`, which
  every compile here includes via `-I.../FreeRTOS/Source/include`, so a
  non-recursive init leaves that directory empty and the build fails on a
  missing `FreeRTOS.h` rather than on a missing submodule.

  ```bash
  git submodule update --init --recursive \
    actuation_module/freertos_x5h/rcar_bsp \
    actuation_module/freertos_x5h/lwip \
    cyclonedds
  ```

- The pinned Arm GNU Toolchain 13.2.Rel1 `arm-none-eabi` toolchain, fetched
  automatically by `scripts/fetch-toolchain.sh` (no manual install step).
- Network access at build time: with `ENABLE_OPENAMP=1` (always on for this
  target), CMake fetches OpenAMP and libmetal from public GitHub
  (`https://github.com/OpenAMP/{open-amp,libmetal}.git`, tag `v2024.10.0`)
  via `ExternalProject_Add`.

## Build

```bash
./build.sh --platform freertos-x5h -d build/freertos-x5h
```

Output: `build/freertos-x5h/actuation_x5h.elf`.

## Verify the ELF contract

```bash
./actuation_module/freertos_x5h/scripts/check-elf-contract.sh build/freertos-x5h/actuation_x5h.elf
```

Expected: `CONTRACT_PASS build/freertos-x5h/actuation_x5h.elf`. This script
checks the ELF's LOAD segments, `.text` base address, and the
`.resource_table` section's address, size, and byte-level vdev/vring
contents — the facts a hardware flash decision depends on. It must not be
modified; a failure here means the build produced a different memory layout,
not that the script is wrong.

## Verify the DDS wire config (Task 8)

```bash
./actuation_module/freertos_x5h/scripts/check-dds-config.sh
```

Expected: `PASS: check-dds-config.sh`. Needs no build artifacts (it reads
source files only), so `build.sh --platform freertos-x5h` also runs it
unconditionally, before compiling anything. It asserts the frozen wire
constants — DDS domain 2, CR52 172.16.52.2, Linux edge ECU 172.16.52.1,
multicast disabled on both sides (a point-to-point RPMsg link has no
multicast to fall back on) — are present, uncommented, on both sides of the
link:

- FreeRTOS/CR52 side: the `CONFIG_DDS_*` `CACHE` vars and compile
  definitions in this directory's own `CMakeLists.txt`.
- Linux/AutoSD side: the `-DCONFIG_DDS_*` flags in
  `scripts/build-edge-ecu-peer-arm64.sh`'s `cmake` invocation — the actual
  mechanism that reaches the Linux-side `edge_ecu_pub`/`edge_ecu_sub`
  binaries, since this codebase's DDS wrapper never parses
  `CYCLONEDDS_URI`/XML at runtime.
  `edge_ecu_peer/cyclonedds-x5h.xml` is checked too (via `xmllint --xpath`,
  so a commented-out node can't produce a false pass), but only as a
  secondary, documentation-only cross-check of the same constants — see
  that XML file's own header comment.

## Design notes

- `build.sh` builds this target by pointing `cmake -S` directly at the
  vendor's own `rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52` directory and
  pulling `CMakeLists.txt` back in via
  `-DCMAKE_PROJECT_INCLUDE=cmake/inject_actuation_x5h.cmake`, deferred with
  `cmake_language(DEFER CALL include ...)` — a different shape from
  `freertos_s32z2`, which is itself the `-S` argument and
  `add_subdirectory()`s its dependencies normally. **The full rationale —
  why this indirection is forced, why it must be a plain `include()` rather
  than `add_subdirectory()`, and the exact ordering invariant the `DEFER`
  protects — lives in one place: the header comment of
  `cmake/inject_actuation_x5h.cmake`.** Read that file first; this note and
  `CMakeLists.txt`'s own header comment intentionally do not repeat it, to
  avoid the rationale drifting out of sync across copies.
- One consequence worth calling out here because it is easy to trip over
  when adding a new target-scoped flag: because `CMakeLists.txt` runs
  *inside* the vendor's own directory scope (not a sibling scope of its
  own), it automatically inherits that scope's directory-wide
  `add_compile_options()` / `add_link_options()` / `link_libraries(dummy)`
  — the Cortex-R52 ABI flags, `-lnosys --specs=nosys.specs`, the `vram2`
  linker script, and the `dummy` weak-stub library all reach
  `actuation_x5h` for free. Do not repeat any of them: passing the same
  linker script to `ld` twice is a hard error (`ld: error: linker script
  file '...lscript_vram2.ld' appears multiple times`), which is how this
  was discovered while developing this target. `BOARD` / `RAM_REGION` /
  `MFIS_CHAN` / `UART_ID` / `CACHE` / `ENABLE_OPENAMP` arrive as `-D` cache
  entries on the command line instead of `set()` calls in `CMakeLists.txt`,
  for the same underlying reason (see the header comment).
- The RPMsg resource-table and platform glue sources
  (`platform_rcar.c`, `remoteproc_rcar.c`, `rsc_table.c`) are the BSP
  sample's own copies (`sample_apps/rpmsg_sample/`), referenced through one
  `X5H_BSP_RPMSG_SOURCES` CMake variable. Task 7 (the real RPMsg transport
  endpoint) swaps that one variable to local, actively-maintained copies —
  no other change to this file should be required.
- The BSP's `drivers/virtio/` tree contains a different-content trio with
  the same file names and exported symbol names, compiled into
  `freertos_bsp` whenever `ENABLE_OPENAMP=1`. This does not collide at link
  time: our copies are linked as ordinary (non-archive) object files, so
  they resolve those symbols before the linker has any reason to pull the
  shadowed copies out of `libfreertos_bsp.a`.
