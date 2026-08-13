# Stage 0 Audit: R-Car FreeRTOS BSP (`rcar_bsp` submodule)

Submodule: `actuation_module/freertos_x5h/rcar_bsp`
Source: `https://github.com/renesas-rcar/FreeRTOS.git`
Pinned tag: `rcar-v2.5.0` (commit `f129675e3`)
Nested submodule: `FreeRTOS/Source` -> `https://github.com/FreeRTOS/FreeRTOS-Kernel.git` @ `dbf70559b27d39c1fdb68dfb9a32140b6a6777a0` (tag `V11.1.0`)

All content below was read directly from the checked-out tree; no vendor SDK, manual, or board measurement was consulted.

## Divergence from the brief's assumed layout

The brief assumes the demo tree lives at `Demo/R-Car_Gen5_CR52/` (repo root). The public tree actually
nests it one level deeper, under the FreeRTOS kernel submodule's parent directory:

```
actuation_module/freertos_x5h/rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/
```

All paths below are relative to this directory unless stated otherwise. This is purely a path
difference, not a missing-content problem — every file the brief asked about exists, just one
directory level down from where it guessed.

The submodule repo root (`rcar_bsp/README.md`) is the unmodified upstream `FreeRTOS/FreeRTOS`
README (cloning instructions, generic "Repository structure" section) — it was not
re-authored for the R-Car fork. The actual R-Car BSP documentation lives at
`FreeRTOS/Demo/R-Car_Gen5_CR52/Readme.md` (see Section 6).

---

## 1. Does `Demo/R-Car_Gen5_CR52/` contain the board config, linker scripts, CR52 startup, and `rpmsg_sample` sources?

Yes — all present, at `FreeRTOS/Demo/R-Car_Gen5_CR52/`. Note `x5h_ironhide` is **not** a separate
subdirectory tree; it is a CMake `BOARD` value (`BOARD_ID=3`) that is threaded through the single
shared demo tree via preprocessor defines and a couple of conditional source lists.

**Board config** (selected by `BOARD=x5h_ironhide`):
- `include/board.h` — `#define X5H_IRONHIDE 3` (matches `BOARD_ID=3` set in `CMakeLists.txt:36`)
- `common/memory_map/memory_map_x5h_ironhide.h` — board-specific peripheral/OSAL/CMA/shared-memory
  address map, dispatched from `common/memory_map/memory_map.h:15`
  (`#elif (BOARD == X5H_IRONHIDE || BOARD == X5H_RFS2)`)
- `CMakeLists.txt:32-43` — `BOARD_ID` mapping (`x5h_vdk`=1, `ai_acc`=2, `x5h_ironhide`=3, `x5h_rfs2`=4)
- `CMakeLists.txt:92-96` — `TCM_ENABLE=1` only when `BOARD STREQUAL "x5h_ironhide"`
- `CMakeLists.txt:75-81` — `UCIE_VER=RCAR_UCIE_V100` for `x5h_ironhide`/`x5h_rfs2`
- `CMakeLists.txt:132-135` — `x5h_ironhide` builds `drivers/ucie/uciedrv.c` (distinct from the
  `ucie_rfs`/`ucie_vdk` variants used by other boards)

**Linker scripts** (board-agnostic, `RAM_REGION`-selected — see Section 5): `common/linker/*.ld`
(`lscript_common.ld`, `lscript_vram1/2/3.ld`, `lscript_dram.ld`, `lscript_dram2.ld`,
`lscript_rsc_table_vram1/2/3.ld`, `lscript_rsc_table_virtio.ld`).

**CR52 startup**: `common/boot.S`, `common/asm_vectors.S`, `common/ARM_CR52/port.c`,
`common/ARM_CR52/CMakeLists.txt`, `common/system_rcar_gen5.c`, `common/interrupts.c`.

**`sample_apps/rpmsg_sample/` sources** — all four files the brief named are present, exact names:
- `sample_apps/rpmsg_sample/rpmsg-echo.c`
- `sample_apps/rpmsg_sample/rsc_table.c`
- `sample_apps/rpmsg_sample/platform_rcar.c`
- `sample_apps/rpmsg_sample/remoteproc_rcar.c`

Also present in the same directory (not asked for, noted for completeness):
`rpmsg-perf.c`, `rsc_table.h`, `platform_info.h`, `platform_info_common.h`, `CMakeLists.txt`, `Readme.md`.

**Caveat — a second, different set of files with the same names exists** at
`drivers/virtio/{platform_rcar.c,remoteproc_rcar.c,rsc_table.c}`. This is a *different*
implementation (used by `virtio_iommu_frontend_sample`/`virtio_iommu_backend_sample`, wired
into `bsp_source` in `CMakeLists.txt:195-205`). Confirmed by `diff` that the two `platform_rcar.c`
files are not the same content. `sample_apps/rpmsg_sample/CMakeLists.txt:10-15` proves
`rpmsg_sample`'s executables compile their **own local** copies
(`platform_rcar.c`, `remoteproc_rcar.c`, `rsc_table.c` resolved relative to the target's own
source dir), not the `drivers/virtio/` versions. Later tasks should read from
`sample_apps/rpmsg_sample/`, not `drivers/virtio/`.

## 2. Which OpenAMP copy does it vendor, and what is the platform init call sequence?

**Not vendored as source.** Both OpenAMP and libmetal are fetched at CMake configure/build time
via `ExternalProject_Add`, from public upstream GitHub repos, pinned to the same tag:

- `libraries/openamp/CMakeLists.txt:27-29` — `GIT_REPOSITORY https://github.com/OpenAMP/open-amp.git`, `GIT_TAG v2024.10.0`
- `libraries/libmetal/CMakeLists.txt:16-18` — `GIT_REPOSITORY https://github.com/OpenAMP/libmetal.git`, `GIT_TAG v2024.10.0`

Consequence for later build-script tasks: building `rpmsg_sample` requires network access during
the CMake configure/build step to clone these two repos. Both are public; this is a build-process
fact, not an NDA gap.

The only in-tree OpenAMP-adjacent content is a 6-line CPU-yield shim:
`common/libmetal/metal/cpu.c` (`metal_cpu_yield()` -> `taskYIELD()`) and its header `cpu.h`.

**Platform init call sequence** (from `sample_apps/rpmsg_sample/platform_rcar.c` and
`remoteproc_rcar.c`, driven from `rpmsg-echo.c:110,115`):

1. `rpmsg-echo.c:110` — `platform_init(MFIS_CHAN, &platform)`
2. `platform_rcar.c:92` `platform_init()` -> `platform_rcar.c:42` `platform_create_proc(mfis_ch, rsc_id=0)`
3. `platform_create_proc()`: `init_resource_table()` -> `get_resource_table()` ->
   `remoteproc_init(&rproc_inst, &x5h_r_a_proc_ops, &mfis_inst)` (`platform_rcar.c:57`) — this
   invokes `x5h_r_a_proc_ops.init = x5h_proc_init` (`remoteproc_rcar.c:44-61`), which calls
   `mfis_init(mfis)`
4. `remoteproc_mmap()` x2 (`platform_rcar.c:62,70`) — invokes `x5h_r_a_proc_ops.mmap = x5h_proc_mmap`
   (`remoteproc_rcar.c:77-122`)
5. `remoteproc_set_rsc_table(&rproc_inst, ...)` (`platform_rcar.c:76`)
6. `rpmsg-echo.c:115` — `platform_create_rpmsg_vdev(platform, 0, ...)` (function defined at
   `platform_rcar.c:121`, return type on the preceding line `:120`) ->
   `remoteproc_get_io_with_pa()` (`platform_rcar.c:137`) -> `remoteproc_create_virtio()`
   (`platform_rcar.c:148`) -> `rpmsg_init_vdev()` (`platform_rcar.c:161`) ->
   `rpmsg_virtio_get_rpmsg_device()` (`platform_rcar.c:169`)
7. Runtime: `platform_poll()` (`platform_rcar.c:181-195`) checks `mfis->int_source` and calls
   `remoteproc_get_notification()`; outbound notify is `x5h_proc_notify` ->
   `mfis_trigger_interrupt()` (`remoteproc_rcar.c:127-130`)

`vdev_index=0` is passed at `rpmsg-echo.c:115`, consistent with the single vdev entry in the
resource table (Section 5).

**Important caveat — the sample is documented as non-functional.**
`sample_apps/rpmsg_sample/Readme.md:9-11`:
> "This application demonstrates how to use the OpenAMP library on FreeRTOS. However, the
> functions are not fully implemented, so the application will not run. Please refer to the
> flow for guidance only."

Consistent with that note, `platform_release_rpmsg_vdev()` and `platform_cleanup()` in
`platform_rcar.c:201-213` are empty stubs. Any later task building the real RPMsg transport off
this sample must treat it as a reference/skeleton, not a working starting point.

## 3. Does the BSP bundle an lwIP tree or a `sys_arch` port?

**No, not within the CR52/X5H tree.** Scoped correctly to
`FreeRTOS/Demo/R-Car_Gen5_CR52/` — the directory that actually matters for `x5h_ironhide` — running
`find . -iname 'lwip*' -o -iname 'sys_arch*'` from inside that directory returns zero matches
(exit code 0, no output).

**Correction of an earlier version of this claim**: an earlier draft of this document said the
same command, run from the `rcar_bsp` submodule root, also returned zero matches. That was wrong.
Run from the submodule root, the same command returns dozens of matches, e.g.
`FreeRTOS/Demo/lwIP_AVR32_UC3`, `FreeRTOS/Demo/Common/ethernet/lwip-1.4.0`,
`FreeRTOS/Demo/lwIP_MCF5235_GCC`, `FreeRTOS/Demo/lwIP_Demo_Rowley_ARM7/lwip-1.1.0`,
`FreeRTOS/Demo/ARM9_STR91X_IAR/lwip`, `FreeRTOS/Demo/CORTEX_A9_Zynq_ZC702/.../lwIP_Demo`,
`FreeRTOS/Demo/MicroBlaze_Kintex7_EthernetLite/.../lwIP_Demo`, and their respective `sys_arch.c`/
`sys_arch.h` ports. These are generic upstream FreeRTOS-mainline demo trees for unrelated MCUs
(AVR32, ColdFire MCF52xx, MicroBlaze, Zynq, AT91SAM7X, STR91X) that happen to be bundled in the
same `renesas-rcar/FreeRTOS` submodule (it is a fork of the full `FreeRTOS/FreeRTOS` monorepo).
None of them are wired into `FreeRTOS/Demo/R-Car_Gen5_CR52/CMakeLists.txt`, none target CR52/X5H,
and none are applicable to `x5h_ironhide`.

**Go-forward conclusion (unchanged)**: there is no lwIP tree or `sys_arch` port anywhere inside
`FreeRTOS/Demo/R-Car_Gen5_CR52/` — the CR52/X5H demo tree the `x5h_ironhide` build actually
compiles. A later task (Task 4) must add lwIP as its own dependency; the BSP does not provide one
usable for this target.

## 4. Is anything required for the `x5h_ironhide` build absent from the public tree (NDA-gated)?

**No genuinely NDA-gated content was found missing.** Every source/header/linker-script the
`CMakeLists.txt` references for `BOARD=x5h_ironhide` resolves to a file present in the public
`renesas-rcar/FreeRTOS` repo at `rcar-v2.5.0` (board header, board memory map, UCIE driver
variant, CR52 startup, `rpmsg_sample` sources, linker scripts — all cited above with paths).
OpenAMP/libmetal are fetched from public upstream GitHub at build time (a network dependency,
not a vendor-blob dependency).

**Decision: the spec's conditional resolves "public-only".** `autoware-safety-island-x5h-config`
is NOT created.

Two non-blocking observations worth recording, since neither is an NDA gate but both matter to
later tasks:

- `FreeRTOS/Demo/R-Car_Gen5_CR52/Readme.md:5` has a section heading
  `## Initialize submodules (RENESAS internal only)`. Despite the heading, the actual submodule
  (`FreeRTOS/Source` -> the public `FreeRTOS/FreeRTOS-Kernel` repo) cloned with no authentication
  and no gating in this session (see Step 4 verification below) — the heading appears to be a
  stale/generic doc note, not an enforced restriction. No evidence any content is actually gated.
- The `rpmsg_sample` non-functional-stub caveat from Question 2 above.

## 5. Which linker script places `.text` at `0x11600000` and `.resource_table` at `0x96650000`?

`common/linker/lscript_common.ld:5,11` defines the named memory regions:
```
vram2_base_addr        : ORIGIN = 0x11600000, LENGTH = 0xA00000   (10 MiB)
remote_proc_rsc_table_1 : ORIGIN = 0x96650000, LENGTH = 0x1000
```

- `common/linker/lscript_vram2.ld` — `INCLUDE lscript_common.ld`; its `SECTIONS` block places
  `.text` (and all other program sections) `> vram2_base_addr`, i.e. at `0x11600000`, matching
  the frozen 10 MiB boot-slot window (`0xA00000` = `0x00A00000`).
- `common/linker/lscript_rsc_table_vram2.ld` — places `.resource_table` `> remote_proc_rsc_table_1`,
  i.e. at `0x96650000`. The `0x1000`-byte carveout window (`lscript_common.ld:11`) is the reserved
  memory **region**; the `.resource_table` **section** itself is forced to exactly `0x100` bytes,
  not by the linker script but by the struct definition:
  `sample_apps/rpmsg_sample/rsc_table.h:29-38` defines `struct remote_resource_table` with
  `__attribute__((packed, aligned(0x100)))` on its closing brace (line 38). GCC's `aligned(N)`
  attribute forces `sizeof(struct)` to be rounded up to the next multiple of `N`; with `packed`
  removing any interior padding, the struct's natural (unrounded) size is the sum of its fields —
  `version`+`num`+`reserved[2]`+`offset[8]` = `4+4+8+32` = `48` bytes (verified directly from
  `rsc_table.h:30-33`), plus one `struct fw_rsc_vdev` and two `struct fw_rsc_vdev_vring` entries
  (`rsc_table.h:35-37`). Those three types are defined in `<openamp/open_amp.h>`, which — like
  `VIRTIO_RPMSG_F_NS` below — is not vendored in this tree (OpenAMP is fetched at build time, see
  Section 2), so their exact byte sizes are not independently resolvable from local BSP content.
  What *is* certain from the attribute alone: as long as the unrounded total (48 bytes plus those
  three entries) does not exceed `0x100`, `aligned(0x100)` rounds it up to exactly `0x100` — one
  multiple, not two or more — which is consistent with, and is the actual mechanism producing, the
  frozen contract's `.resource_table` section size of `0x100`. This is the in-tree proof Task 2's
  contract check (assert section size `== 0x100`) can rely on: the size is attribute-forced, not
  incidental to current field values.

These two scripts are the pair selected for `RAM_REGION=2`. In `sample_apps/rpmsg_sample/CMakeLists.txt`,
`RAM_REGION=2` corresponds to `CORE=1` (`CMakeLists.txt:85-86`: `if(RAM_REGION EQUAL 2) set(CORE 1)`),
and the default (no `-DMFIS_CHAN` override) build loop pairs `CORE=1` with `MFIS_CHAN=1`
(`CMakeLists.txt:52-54`). This produces the target `rpmsg_mfis1_cluster0_core1` — MFIS channel 1,
`.text`@`0x11600000`, `.resource_table`@`0x96650000` — exactly the frozen contract's parameters.

**Resource table content** confirms the rest of the frozen contract, from
`sample_apps/rpmsg_sample/rsc_table.c`:
- `VIRTIO_ID_RPMSG_` = `7` (line 29) — vdev id 7
- `NUM_VRINGS` = `0x02` (line 31) — 2 vrings
- `VRING_ALIGN` = `0x1000` (line 32) — align `0x1000`
- `RING_TX`/`RING_RX` default to `FW_RSC_U32_ADDR_ANY` (lines 33-38) — `da=0xFFFFFFFF`.
  `FW_RSC_U32_ADDR_ANY` itself is defined in the externally-fetched OpenAMP header
  (`<openamp/open_amp.h>`, not vendored in this tree — see Section 2), so its numeric value is
  not independently resolvable from local BSP content, same caveat as `VIRTIO_RPMSG_F_NS` below.
  It is conventionally `0xFFFFFFFF` in upstream OpenAMP, which is what the frozen contract expects.
- `VRING_SIZE` = `256` = `0x100` (line 39) — vring `num=0x100`
- `RPMSG_VDEV_DFEATURES` = `(1 << VIRTIO_RPMSG_F_NS)` (line 26) — `VIRTIO_RPMSG_F_NS` is defined
  in the externally-fetched OpenAMP header (`open-amp` is not vendored in this tree, see
  Section 2), so its value is not resolvable from files in this local BSP tree. Upstream
  `OpenAMP/open-amp` defines `VIRTIO_RPMSG_F_NS` as `0`, which would give `dfeatures=1`, matching
  the frozen contract — flagged here as not independently verifiable from this repo's own
  content, since the header lives outside this submodule.

All of the above matches the task's frozen Linux-facing contract for MFIS channel 1.

---

## 6. CMake configure invocation for the Gen 5 CR52 demo (RPMsg sample) — for Task 2

Source: `FreeRTOS/Demo/R-Car_Gen5_CR52/Readme.md` (the BSP's own build documentation).

**Toolchain file** (`Readme.md:24`, file present at
`FreeRTOS/Demo/R-Car_Gen5_CR52/toolchain_arm_none_eabi.cmake`):
```
-DCMAKE_TOOLCHAIN_FILE=toolchain_arm_none_eabi.cmake
```
(path is relative to `FreeRTOS/Demo/R-Car_Gen5_CR52/`, the CMake source dir for this project;
confirmed the file exists there and sets `arm-none-eabi-gcc`/`g++`/`objcopy` via `find_program`).

**Full documented configure invocation** (`Readme.md:20-30`):
```bash
export PATH=$PATH:<your_tool_chain_path>/bin/
cd FreeRTOS/Demo/R-Car_Gen5_CR52/
mkdir build && cd build
cmake -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=toolchain_arm_none_eabi.cmake \
  -DCMAKE_INSTALL_PREFIX=<path/to/install/dir> \
  -DBOARD=<TARGET_PLATFORM> \
  -DENABLE_OPENAMP=1 \
  -DUART_ID=1 \
  -DCACHE=1 \
  -DRAM_REGION=1 \
  ..
make
```

**Variable names that select the board and RAM region** (`Readme.md:33-37`, corroborated by
`CMakeLists.txt:23-63,92-96`):
- `-DBOARD=x5h_ironhide` — selects the Ironhide board (`BOARD_ID=3`, `TCM_ENABLE=1`, `UCIE_VER=RCAR_UCIE_V100`).
  Other values: `x5h_vdk`, `x5h_rfs2`, `ai_acc`.
- `-DENABLE_OPENAMP=1` — required to build OpenAMP/libmetal and the `rpmsg_sample`/virtio-iommu
  sample subdirectories at all (`CMakeLists.txt:349-386`); also stated directly in
  `sample_apps/rpmsg_sample/Readme.md:7`: "Make sure `-DENABLE_OPENAMP=1` is in your CMake configuration command."
- `-DRAM_REGION=2` — selects the `vram2`/`0x11600000` window and, inside `rpmsg_sample`'s own
  `CMakeLists.txt`, drives `CORE=1`/`MFIS_CHAN=1` pairing (see Section 5) to hit the frozen
  contract's addresses.
- There is **no separate "MFIS channel" top-level CMake variable** for the whole BSP; MFIS
  channel selection for the RPMsg sample specifically is internal to
  `sample_apps/rpmsg_sample/CMakeLists.txt` (its own `MFIS_CHAN`/`CORE` logic, lines 46-104), and
  can be pinned explicitly by also passing `-DMFIS_CHAN=1` at the top-level configure (that
  CMakeLists reads a top-level-scoped `MFIS_CHAN` if defined, `CMakeLists.txt:46,77`).
- `-DUART_ID=1`, `-DCACHE=1` — optional, documented but not required for the RPMsg sample
  specifically (`CMakeLists.txt:68-73,89-90`).

**Sample/target name that produces the RPMsg sample ELF**: when `MFIS_CHAN` is left undefined
(the documented default path), `sample_apps/rpmsg_sample/CMakeLists.txt:46-76` builds both
CPU-core pairings; the one matching the frozen contract (MFIS channel 1, `RAM_REGION=2`,
`0x11600000`/`0x96650000`) is the target:
```
rpmsg_mfis1_cluster0_core1
```
built from `rpmsg-echo.c` (there is also a `rpmsg_perf_mfis1_cluster0_core1`, built from
`rpmsg-perf.c`, sharing the same `platform_rcar.c`/`remoteproc_rcar.c`/`rsc_table.c` and linker
scripts). Output path: `build/sample_apps/rpmsg_sample/rpmsg_mfis1_cluster0_core1.elf` (plus
`.map`; `.srec` via the `convert_bin_to_srec` custom target defined in the top-level
`CMakeLists.txt:322-345`).

This is the brief's build-skeleton note "adjust per the BSP's own README" resolved: use
`toolchain_arm_none_eabi.cmake`, `-DBOARD=x5h_ironhide`, `-DENABLE_OPENAMP=1`,
`-DRAM_REGION=2` (optionally `-DMFIS_CHAN=1` to pin explicitly), target
`rpmsg_mfis1_cluster0_core1`.
