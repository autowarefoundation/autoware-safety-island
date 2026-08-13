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
resource-table carveage Linux's remoteproc driver reads), both produced by
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

- Unlike `freertos_s32z2`, `build.sh` does not run `cmake
  actuation_module/freertos_x5h -B ...`. It runs `cmake -S
  rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52 -B ...` — the BSP's own directory —
  passing `-DBOARD=x5h_ironhide -DRAM_REGION=2 -DMFIS_CHAN=1 -DUART_ID=1
  -DCACHE=1 -DENABLE_OPENAMP=1` (matching Task 2's
  `scripts/build-bsp-rpmsg-sample.sh`) plus
  `-DCMAKE_PROJECT_INCLUDE=cmake/inject_actuation_x5h.cmake`. This is
  forced by a genuine constraint in the vendor's own (unmodified)
  `CMakeLists.txt`: it derives `BSP_DIR`/`FREERTOS_DIR` from
  `CMAKE_SOURCE_DIR`, which is fixed for the whole `cmake` invocation to
  whatever directory `-S` names and cannot be overridden from a child
  `add_subdirectory()` scope (confirmed empirically — a local `set()`
  shadow has no effect on it). So this directory cannot be the `-S`
  argument and `add_subdirectory()` the vendor tree underneath itself the
  way `freertos_s32z2/CMakeLists.txt` does; the vendor tree has to be `-S`
  instead, with this directory pulled in the other direction. The injected
  file uses `cmake_language(DEFER CALL include ...)` to delay processing
  `CMakeLists.txt` until after `freertos_bsp` / `openamp` / `libmetal` are
  already defined later in the vendor's own file (a plain, non-deferred
  `CMAKE_PROJECT_INCLUDE` runs immediately after `project()`, too early for
  those targets to exist yet). The deferred call is a plain `include()`,
  not `add_subdirectory()`: CMake refuses to create a new source/binary
  directory during deferred execution ("Subdirectories may not be created
  during deferred execution"), so `CMakeLists.txt` runs directly in the
  vendor's own directory scope rather than a child scope of its own — it
  does not call `project()` itself (the injected file calls
  `enable_language(CXX)` up front instead) and uses
  `CMAKE_CURRENT_LIST_DIR`, not `CMAKE_CURRENT_SOURCE_DIR`, for its own
  paths. See `cmake/inject_actuation_x5h.cmake` and this directory's own
  `CMakeLists.txt` header comment for the full rationale.
- Because `CMakeLists.txt` runs in the vendor's own directory scope (not a
  sibling scope of its own, unlike a plain `add_subdirectory()`), it
  automatically *inherits* the compile/link flags the vendor tree's own
  `add_compile_options()` / `add_link_options()` / `link_libraries(dummy)`
  calls apply directory-wide — the Cortex-R52 ABI flags, `-lnosys
  --specs=nosys.specs`, the `vram2` linker script, and the `dummy` weak-stub
  library all reach `actuation_x5h` for free, exactly as they would any
  other target defined later in that same file. `CMakeLists.txt` must NOT
  repeat any of them: passing the same linker script to `ld` twice is a hard
  error (`ld: error: linker script file '...lscript_vram2.ld' appears
  multiple times`), which is how this was discovered while developing this
  target. `BOARD` / `RAM_REGION` / `MFIS_CHAN` / `UART_ID` / `CACHE` /
  `ENABLE_OPENAMP` are not set inside `CMakeLists.txt` itself for a
  different reason — by the time this file runs (deferred), the vendor's own
  file has already consumed them at the top of its own processing — so they
  must arrive as `-D` cache entries on the command line instead.
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
