# FreeRTOS on Renesas R-Car X5H — Build and Verification

This directory contains the FreeRTOS build for the Renesas R-Car Gen5 X5H
"Ironhide" board (Cortex-R52, Core1), running as a remoteproc-managed
firmware image alongside Linux on the same SoC.

## Status

`actuation_x5h.elf` boots the R-Car BSP, brings up the console, starts the
FreeRTOS scheduler, and runs the full actuation module: CycloneDDS, the
controller, real lwIP-over-RPMsg network bring-up
(`lwip_bring_up_blocking()`, `freertos_x5h/lwip_bringup.c`), and a real
RPMsg transport endpoint (`freertos_x5h/rpmsg_transport.{h,c}`) carrying
Ethernet frames to/from the Linux edge ECU peer over the CR52's `rpmsg-eth`
channel.

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

## The netif-only board artifact (`netif_only_x5h.elf`)

`./build.sh --platform freertos-x5h` builds this second ELF automatically,
alongside `actuation_x5h.elf`, in the same CMake configure (it is
`EXCLUDE_FROM_ALL` in `CMakeLists.txt`, so a bare `cmake --build` of that
directory does not build it as a side effect — `build.sh` builds it
explicitly by name, `--target netif_only_x5h`, right after
`actuation_x5h`). It links the same board/RPMsg-transport/lwIP-netif stack
as `actuation_x5h.elf` but leaves out the actuation module, CycloneDDS,
Eigen, and `autoware_msgs` entirely: lwIP answers ICMP natively with
nothing built on top of it, so a bare `ping` against the CR52's
172.16.52.2 address proves the RPMsg netif itself is alive before ever
trusting the full actuation/DDS link on top of it. Both
`check-elf-contract.sh` and `check-image-budget.sh` (below) run against
`netif_only_x5h.elf` as well as `actuation_x5h.elf` on every `build.sh` run,
so it cannot silently bit-rot or drift out of budget between uses. Build it
on its own with `cmake --build build/freertos-x5h --target netif_only_x5h`
when narrowing down whether a failure is in the netif or in the actuation
module above it.

## Verify the ELF contract

```bash
./actuation_module/freertos_x5h/scripts/check-elf-contract.sh build/freertos-x5h/actuation_x5h.elf
```

Expected: `CONTRACT_PASS build/freertos-x5h/actuation_x5h.elf`. This script
checks the ELF's LOAD segments, `.text` base address, and the
`.resource_table` section's address, size, and byte-level vdev/vring
contents — the facts a hardware flash decision depends on. It must not be
modified; a failure here means the build produced a different memory layout,
not that the script is wrong. `build.sh` runs it against both
`actuation_x5h.elf` and `netif_only_x5h.elf` on every build.

## Check the image budget

```bash
./actuation_module/freertos_x5h/scripts/check-image-budget.sh build/freertos-x5h/actuation_x5h.elf
```

Expected: `BUDGET_PASS ...` (or a `BUDGET_WARN` above 90% full, still a
zero exit). This is the memory-risk gate for the frozen 10 MiB Core1 boot
slot window: the full lwIP + CycloneDDS + actuation module link must fit
inside it alongside the resource table and everything else remoteproc
carves out of that same window. `build.sh` runs this against both
`actuation_x5h.elf` and `netif_only_x5h.elf` on every build, so
`netif_only_x5h.elf` — much smaller today, but still linked from the same
board/RPMsg/lwIP sources — cannot silently grow past budget unnoticed
between the times someone happens to check it by hand.

## Verify the DDS wire config

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

## Diagnostics: reading the console

Both ELFs carry an always-on diagnostic surface (`x5h_diag.h`,
`x5h_diag.c`, `x5h_diag_vectors.S`), not behind any build flag.

**This section is the operator's run-book only.** The rationale — the three
wedge candidates, why each is silent by construction, why nothing in there
may allocate or take the scheduler lock, and what each field means — lives
in exactly one place, the header comment of **`x5h_diag.h`**. Read that
before interpreting anything below; do not restate it here.

What to look for, in the order it appears:

- **First line of all, before `FreeRTOS X5H … starting…`:**
  `x5h-diag: exception vectors installed, VBAR 0x… -> 0x…`. If this line is
  missing, `main()` was never reached and nothing below applies.
- **Every 5 s, forever:** `x5h-diag: beacon #N uptime_ms=… launcher=… stack_hwm_words=… heap_brk=… heap_used=… sbrk_free=…`.
  This task is never deleted, so **silence now means dead** — before this
  image, a quiet console was ambiguous between "wedged" and "nothing was
  logging". Watch `stack_hwm_words`: words of headroom left on the task
  running the DDS chain, falling towards 0 means an imminent overflow.
- **Immediately before the DDS domain call:**
  `x5h-diag: mark pre-dds_create_domain_with_rawconfig …` with the same
  fields. Compare it against the last beacon line before the console went
  quiet.
- **On a CPU fault:** `*** X5H EXCEPTION: <name> ***` followed by the
  offset-corrected faulting `PC`, `SPSR`, `DFSR`/`DFAR`/`IFSR`/`IFAR` and
  the running task's name, then `*** halted ***`. All four fault registers
  are printed on every exception; only the pair that matches the exception
  is meaningful (`DFSR`/`DFAR` for a Data Abort, `IFSR`/`IFAR` for a
  Prefetch Abort), the others hold whatever the last fault of their kind
  left there.
- **On an allocation-failure death:** `*** X5H: abort() called -- halting ***`
  or `*** X5H: _exit(status=N) -- halting ***`. As shipped, `-lnosys`'s
  `_exit` is a bare `while(1)` with no output, which is what made
  `ddsrt_malloc()`'s failure path silent.

Beacons stopping with **no** exception block and **no** abort line means the
scheduler itself stopped while the fault vectors were installed and working
— a `vTaskSuspendAll()` that is never resumed (`heap_useNewlib.c`'s
`__malloc_lock` is exactly that). No task of any priority runs in that
state, this one included, so no in-image diagnostic can narrate it; it is
reported by the stopping itself. Move to a debugger from there.

There is deliberately **no heap-free-list figure** in the beacon. Getting
one means `mallinfo()` walking newlib's arena under `__malloc_lock`, which
a stack overflow can send into an infinite loop with the scheduler
suspended — turning a candidate-3 death into something that reads exactly
like the paragraph above. `sbrk_free` bounds the heap without walking it.
See `diag_put_resources()` in `x5h_diag.c` for the full argument.

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
  `X5H_BSP_RPMSG_SOURCES` CMake variable. `rpmsg_transport.c` — the real
  RPMsg transport endpoint, announcing the `rpmsg-eth` service and feeding
  inbound frames to `rpmsg_netif_rx()` — is a separate, actively-maintained
  local file added alongside it via `target_sources()`, together with
  `rpmsg_netif_core.c`, `rpmsg_netif.c`, and `lwip_bringup.c` (see the
  "RPMsg-backed lwIP netif + bring-up" section of `CMakeLists.txt`).
- The BSP's `drivers/virtio/` tree contains a different-content trio with
  the same file names and exported symbol names, compiled into
  `freertos_bsp` whenever `ENABLE_OPENAMP=1`. This does not collide at link
  time: our copies are linked as ordinary (non-archive) object files, so
  they resolve those symbols before the linker has any reason to pull the
  shadowed copies out of `libfreertos_bsp.a`.
