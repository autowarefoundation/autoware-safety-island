# FreeRTOS on Renesas R-Car X5H — Build and Verification

This directory contains the FreeRTOS build for the Renesas R-Car Gen5 X5H
"Ironhide" board (Cortex-R52, Core1), running as a remoteproc-managed
firmware image alongside Linux on the same SoC.

## Status (Task 3 scaffold)

`actuation_x5h.elf` boots the R-Car BSP, brings up the console, starts the
FreeRTOS scheduler, and prints `X5H_SCAFFOLD_ALIVE` once per second. It does
**not** start the actuation module, bring up lwIP, or open an RPMsg
transport endpoint yet:

- The actuation module (CycloneDDS, controller) lands in Task 4.
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

- The R-Car FreeRTOS BSP, vendored as the `rcar_bsp` git submodule:

  ```bash
  git submodule update --init actuation_module/freertos_x5h/rcar_bsp
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
