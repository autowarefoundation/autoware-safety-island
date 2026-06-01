# FreeRTOS on NXP S32Z2 — Build and Verification

This directory contains the FreeRTOS build for the ARM Automotive Kit
(X-S32Z27X-DC carrier, NXP S32Z2 SoC, Cortex-R52, RTU0 lock-step).

## Status (Phase 5)

The firmware `actuation_freertos_s32z2.elf` builds, links, and runs on real
hardware: the FreeRTOS scheduler and 1 kHz tick come up, lwIP brings up NETC
Ethernet 0 (RX and TX both flow), CycloneDDS creates its domain-2 participant,
and the controller reaches `Controller Node Started` / `Actuation Safety Island
is Live`, running the MPC/PID control loop at the 150 ms cadence. The
end-to-end control round-trip (host → board → `control_cmd` → host) is verified
on the POSIX simulator and being brought up on hardware.

The NXP RTD MCAL drivers (`Mcu`, `Clock_Ip`, `Siul2_Port_Ip`, `Uart`, `Gpt`,
`Eth_43_NETC`) require post-build configuration structures (`*_PBcfg.c`)
generated from a `.mex` configuration via S32 Design Studio's S32 Config Tools.
That generated project is bundled as the private `s32ct_config` submodule (see
[Generating PB configurations](#generating-pb-configurations) below); the build
falls back to it automatically and an `S32CT_GENERATED_DIR` env var overrides
it.

## Prerequisites (manual)

The NXP-licensed pieces are not in this repository.

Three separate downloads from the NXP Flexnet portal
(`freescaleesd.flexnetoperations.com`) are required:

1. **S32ZE Real-Time Drivers (RTD) Version 2.0.1** (`SW32ZE_RTD_R21-11_2.0.1_D2505_DesignStudio_updatesite.zip`)
2. **SW32ZE FreeRTOS 11.1.0 version 4.0.0** (`SW32ZE_FreeRTOS_11.1.0_4.0.0_D2505_DesignStudio_updatesite.zip`)
3. **SW32ZE_TCPIP_STACK_3.0.0_D2505** (`SW32ZE_TCPIP_STACK_3.0.0_D2505_DesignStudio_updatesite.zip`)

Each download is gated by an NXP S32 PLATFORM SOFTWARE LICENSE AGREEMENT
v1.10 click-through. Every package is an Eclipse p2 update site (outer
`.zip` wrapping an inner JAR with a misleading `.win32.win32.x86_64`
suffix — the contents are platform-neutral C source). **Both layers must
be unzipped** to expose the source tree.

Also required:

- **S32 Design Studio 3.6.x** for S32 Platform — for the S32 Config Tools
  GUI used to generate `*_PBcfg.c` files, and for the bundled `s32dbg`
  debugger.
- **`arm-none-eabi-gcc` 12.x or newer** (Ubuntu 24.04 standard package
  works: `sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
  libnewlib-arm-none-eabi`).

After extracting all three packages, export three environment variables
pointing at the unpacked roots:

```bash
export S32_RTD_PATH=/path/to/RTD_S32ZE_2.0.1/_jar_family/S32DS/software/PlatformSDK_S32ZE
export FREERTOS_PATH=/path/to/FreeRTOS_S32ZE_4.0.0/_jar/S32DS/software/PlatformSDK_S32ZE/FreeRTOS
export LWIP_PATH=/path/to/TCPIP_S32ZE_3.0.0/_jar/S32DS/software/PlatformSDK_S32ZE/stacks/tcpip
```

Pin them in `/etc/profile.d/nxp.sh` or `~/.bashrc` so subsequent shells
inherit them.

## Hardware wiring

The kit's J11 USB-C dual port exposes two distinct USB devices on the host:

| USB device | Role | Note |
|---|---|---|
| `/dev/ttyUSB0` | FT232RQ UART **console** at 115200 8N1 | The plan's earlier draft incorrectly labelled this `/dev/ttyACM0`. |
| `/dev/ttyACM0` | OpenSDA / debug probe | Not the console. |

Required physical setup before flashing:

- DC power 8–36 V on the carrier's J2 power jack (center positive 2.1 mm).
- NXP S32 Debug Probe (Rev B) on the carrier's **J6 JTAG** (20-pin IDC +
  CWH-CTP-ARM-YE adapter); probe Mini-USB to the host.
- **J14 JTAG select** jumper set to the right-hand position (OpenSDA path)
  *or* left-hand position (external Aurora/JTAG via the S32 Debug Probe),
  matching the debug probe used.
- **Boot mode** jumpers J17/J18 set per the boot media:
  - `J17 = unconnected (3.3 V)`, `J18 = connected (GND)` → boot from
    serial RCON / SD card (default).
  - `J17 = connected (GND)`, `J18 = unconnected (3.3 V)` → serial
    bootloader (UART or CAN).
- **Front-panel reset** switch **S2** released (not asserted).
- Optional for B-2: kit's J7 Ethernet 0 on the same LAN segment as the
  host.

If `west debug` reports `Target connection failed ... [CCS: connection to
server refused]`, the SoC isn't responding to JTAG — that is a hardware
state problem (power off, JTAG cable unseated, boot-mode jumpers wrong,
S2 asserted). The west / s32dbg toolchain itself is fine.

## Generating PB configurations

This is currently the gating step before any firmware can boot. The NXP
RTD MCAL drivers expect a post-build configuration structure per driver
(`Mcu_PBcfg.c`, `Clock_Ip_PBcfg.c`, `Siul2_Port_Ip_PBcfg.c`,
`Uart_PBcfg.c`, `Pit_PBcfg.c`, etc.). The drivers ship those structures
as EB Tresos templates under `$S32_RTD_PATH/RTD/<driver>_TS_*/generate_PB/src/`;
each contains `[!...!]` template markers that must be expanded by S32
Config Tools.

Workflow:

1. Open **S32 Design Studio 3.6.x**.
2. **File → New → S32DS Application Project** (or Import) — start from the
   `lwip_S32Z27X_FreeRTOS_R52` example shipped at
   `$LWIP_PATH/examples/S32Z270/lwip_S32Z27X_FreeRTOS_R52/`. It already
   targets the same SoC family + R52 core.
3. Open the project's `.mex` file in the S32 Configuration Tools editor
   (Peripherals view, Clock view, Pins view).
4. Adjust:
   - **Clock**: confirm the Cortex-R52 core clock at ≥ 240 MHz.
   - **UART9**: enable LinFlexD instance 9 at 115200 8N1.
   - **PIT**: enable PIT channel 0 as the FreeRTOS tick source.
   - **NETC**: leave Ethernet 0 enabled (Ethernet 1 is currently
     unsupported by the Zephyr port, but the FreeRTOS path can revisit).
5. **Update Code** — S32 Config Tools writes the expanded `*_PBcfg.c` /
   `*_Cfg.h` / `*_Cfg.c` files into the project's `generate/src/`,
   `generate/include/`, and `board/` directories. With the Pins /
   Peripherals / Clocks / DCD tools all enabled, the lwip example
   produces ~38 C sources and ~80 headers covering Mcu, Clock_Ip,
   Port, Pit_Ip, Gpt, Linflexd_Uart_Ip, NETC, Platform, Power_Ip,
   Ram_Ip, Mpu, Mru, OsIf, IntCtrl, DiportSd.
6. The build defaults `S32CT_GENERATED_DIR` to the bundled `s32ct_config`
   submodule. To use your own freshly generated project instead, set
   `S32CT_GENERATED_DIR` to its root before running CMake. The build expects
   the layout `$S32CT_GENERATED_DIR/{board,generate/include,generate/src}`. A
   typical value after running the example wizard:
   `S32CT_GENERATED_DIR=~/workspaceS32DS.3.6.2/lwip_S32Z27X_FreeRTOS_R52`.

The generated files **carry the NXP Confidential and Proprietary licence
header** (inherited from the templates) and must **not** be committed to a
public repository. They are kept in the **private** submodule
`actuation_module/freertos_s32z2/s32ct_config`
(`autowarefoundation/autoware-safety-island-s32z2-config`), clonable only by
developers with access:

```bash
git submodule update --init actuation_module/freertos_s32z2/s32ct_config
```

Developers without access to the private submodule regenerate the project
themselves via the S32 Config Tools workflow above and point
`S32CT_GENERATED_DIR` at it. The repo's `.gitignore` also excludes
`actuation_module/freertos_s32z2/generated/` so stray local copies stay out of
git.

### Pitfalls observed on Ubuntu 24.04 + S32DS 3.6.2

- The S32CT runtime calls codegen scripts by lowercase relative paths
  (`../mcu/mcu_codegen.js`, `../gpt/gpt_codegen.js`, …) but the
  shipped directories are mixed-case (`Mcu/`, `Gpt/`). On a
  case-sensitive filesystem the scripts are not found and code
  generation silently skips Gpt / Mcu / Port. Fix by creating
  lowercase symlinks alongside the capitalised dirs:

  ```bash
  cd /usr/local/NXP/S32DS.3.6.2/eclipse/mcu_data/components/PlatformSDK_S32ZE
  sudo bash -c 'for d in */; do
      n=$(basename "$d"); lower=$(echo "$n" | tr "[:upper:]" "[:lower:]")
      [ "$n" = "$lower" ] && continue
      [ -L "$lower" ] && continue
      ln -s "$n" "$lower"
  done'
  ```

- The first time the imported project is opened, S32CT shows a
  *Migration to Other Component Versions* dialog. Under Xvfb that
  SWT dialog renders as a black rectangle in `xwd` / VNC output and
  blocks the workbench. Dismiss it via xdotool:

  ```bash
  DISPLAY=:99 xdotool key --window \
      "$(DISPLAY=:99 xdotool search --name 'Migration to Other Component Versions' | head -1)" Escape
  ```

- The lwip example's default Gpt component points its
  `Pit Hardware Module` at `CE_PIT_0` (capture-edge PIT). For our use
  (FreeRTOS tick) change it to `PIT_0` and set
  `GptChannelTickFrequency` to the configured PIT clock and
  `GptClockReferencePoint` to the PIT_CLK signal exported by the
  Mcu/Clock_Ip configuration.

## Build

```bash
export S32_RTD_PATH=...
export FREERTOS_PATH=...
export LWIP_PATH=...
export S32CT_GENERATED_DIR=~/workspaceS32DS.3.6.2/lwip_S32Z27X_FreeRTOS_R52

cmake -S actuation_module/freertos_s32z2 -B build-s32z2 \
    -DCMAKE_TOOLCHAIN_FILE=$PWD/actuation_module/freertos_s32z2/cmake/arm-cortex-r52.cmake
cmake --build build-s32z2 -j
```

Output (once PB configs are in place): `build-s32z2/actuation_freertos_s32z2.elf`.

The CycloneDDS cross-build is a separate phase that runs first and produces
the static `libddsc.a` consumed by the main target:

```bash
./actuation_module/freertos_s32z2/scripts/build-cdds-target.sh
```

## Flash

The NXP `nxp_s32dbg` west runner supports **debug**, not `flash`. Use
`west debug` with the `--batch` GDB option to load + run the ELF non-
interactively. This matches the CES 2026 demo's `west_debug.sh` workflow
(kept in an out-of-tree `MRM_repo/` working directory on the dev host).

```bash
# s32dbg is a GUI tool; it needs an X display even in batch mode.
export DISPLAY=:99
pgrep -x Xvfb >/dev/null || \
    nohup Xvfb :99 -screen 0 1024x768x24 > /tmp/xvfb.log 2>&1 &

source ~/zephyr-env/bin/activate
west debug \
    --s32ds-path=/usr/local/NXP/S32DS.3.6.2 \
    -d build-s32z2 \
    --tool-opt='--batch'
```

The `--s32ds-path` argument points to the S32 Design Studio install; the
runner shells out to the bundled `S32Debugger` Python scripts and
`arm-none-eabi-gdb-py`.

**Without `DISPLAY=:99` + Xvfb, `west debug` fails with**
`Target connection failed ... CCS: connection to server refused`. The
production `run_before.sh` / `run_after.sh` in `MRM_repo` start Xvfb
themselves in their STEP 4; if running the verify scripts standalone,
they handle the Xvfb bootstrap.

### Demo doc retry recipe

If `west debug` fails with `CCS: connection to server refused`, the demo
scripts (`run_before.sh` / `run_after.sh`) retry up to three times after
each failure with this cleanup:

```bash
pkill -9 -f "gta|s32dbg|arm-none-eabi-gdb" 2>/dev/null || true
# free port 45000/tcp
rm -rf /tmp/*nxp_s32dbg* /tmp/tmp*nxp_s32dbg*
sleep 5
```

If three retries still fail, the SoC connection itself is the problem —
inspect power, J6 JTAG cable seating, J14/J17/J18 jumpers, and S2 reset
button.

## Verify B-1

```bash
S32_RTD_PATH=... \
FREERTOS_PATH=... \
LWIP_PATH=... \
  ./actuation_module/freertos_s32z2/scripts/verify-b1.sh
```

Expected last line: `B-1 verification OK (N heartbeats)` against the
`actuation alive ticks=` marker captured from `/dev/ttyUSB0`.

## Verify B-2

Prerequisite: a DHCP server reachable by the kit's Ethernet 0. If the LAN
has no DHCP, stand one up on the development host:

```bash
sudo apt install dnsmasq
sudo dnsmasq --interface=<your-NIC> --dhcp-range=192.168.50.100,192.168.50.150,1h \
             --bind-interfaces --no-daemon
```

Then run the verification:

```bash
S32_RTD_PATH=... \
FREERTOS_PATH=... \
LWIP_PATH=... \
  ./actuation_module/freertos_s32z2/scripts/verify-b2.sh
```

Expected last line: `B-2 verification OK`.

Three captured logs land under `/tmp/`:
- `/tmp/freertos-s32z2-b2-uart.log` — kit UART9 timeline (`/dev/ttyUSB0`)
- `/tmp/freertos-s32z2-b2-edge-pub.log` — host-side publisher
- `/tmp/freertos-s32z2-b2-edge-sub.log` — host-side subscriber

Attach all three to the B-2 PR body.

## Resolved NXP paths

Pinned in `CMakeLists.txt` per the three-env-var layout above:

- FreeRTOS Cortex-R52 port: `$FREERTOS_PATH/Source/portable/GCC/ARM_CR52_GIC/port.c` (self-contained; no `portASM.S`)
- FreeRTOS kernel headers: `$FREERTOS_PATH/Source/include`
- Startup / linker:
  `$FREERTOS_PATH/examples/S32DS/FreeRTOS_Toggle_Led_Example_S32Z2XX_R52/Project_Settings/{Startup_Code,Linker_Files}/`
  (referenced in-place — NXP-confidential, never copied into the repo)
- RTD MCAL drivers: `$S32_RTD_PATH/RTD/{BaseNXP,Mcu,Mcl,Port,Platform,Uart,Gpt,Eth_43_NETC}_TS_T31D53M20I1R0/{include,src}/`
  (PIT is part of `Gpt_TS_*`, not a standalone `Pit_TS_*`)
- lwIP source: `$LWIP_PATH/lwip/src/{api,core,core/ipv4,netif}/*.c`
- NETC ↔ lwIP `netif` glue: `$LWIP_PATH/code/ports/netif/ethif/rtd/generic/eth_port.c` (function `ethif_ethernetif_init`)
- lwIP `arch/cc.h`: `$LWIP_PATH/code/ports/platform/generic/gcc/setting/arch/cc.h` (gate `USING_RTD=1`, `S32Z27`)
