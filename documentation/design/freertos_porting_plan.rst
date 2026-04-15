..
 # Copyright (c) 2025-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

###########################################
Zephyr to FreeRTOS Porting Plan
###########################################

.. note::

   **Status: design proposal.** This page describes the planned approach
   for adding a FreeRTOS backend alongside the existing Zephyr one. No
   phases listed below have been implemented yet. The shipping firmware
   is Zephyr-only.

********
Overview
********
This document outlines a practical path for supporting FreeRTOS alongside the existing Zephyr implementation.
The goal is to preserve Zephyr while adding a second RTOS backend.
The safest approach is to keep the actuation logic shared and move RTOS-specific behavior behind a small platform layer.

****************
Current State
****************
The repository is currently built around Zephyr.
- `build.sh` runs `west build`
- `actuation_module/CMakeLists.txt` requires Zephyr
- Kconfig and `prj_actuation.conf` define many runtime settings
- board setup lives in Zephyr board config and devicetree overlays
- some common headers include Zephyr APIs directly

Much of the application logic is already portable.
- DDS is wrapped behind local classes
- logging is wrapped behind local helpers
- timer behavior is implemented in project code

This means the port is mostly a platform-integration effort, not a controller rewrite.

************
Key Points
************
The main Zephyr coupling points are build, runtime services, and board setup.

Build and config coupling:
- BUILD SYSTEM
- CONFIGURATION SYSTEM

Runtime coupling:
- node, clock, dds, logger, timer
- Zephyr test entrypoints

The blocker is not DDS.
The CycloneDDS repository already contains FreeRTOS-specific build paths,
including support documented around FreeRTOS with `lwIP`.
That reduces the amount of DDS work required, but it does not remove the need
for RTOS, network stack, and board-level integration in this repository.

******************
Target Design
******************
The cleanest structure is a dual-backend model.
- one shared actuation core
- one Zephyr backend
- one FreeRTOS backend

The shared core should contain controller logic, generated message code, DDS wrapper interfaces, the logger interface, and timer logic.
Each RTOS backend should contain task and mutex implementation, sleep and clock services, network bring-up, board-specific initialization, and configuration wiring.

This keeps the controller code readable and avoids scattering RTOS-specific conditionals through the application.

***************
Core Changes
***************
The current build is Zephyr-native.
We should split it into a portable core plus RTOS-specific wrappers.
Recommended changes:
- extract a platform-neutral core library
- keep a Zephyr wrapper target around that core
- add a FreeRTOS wrapper target around the same core
- keep IDL generation host-side and reusable for both targets

Today common code reads `CONFIG_*` values directly.
That works for Zephyr but does not scale to multi-RTOS support.
Introduce a small config layer that exposes values such as DDS domain id, DDS interface name, log level, stack size, network timeout, and SNTP enablement.
Zephyr can still source these from Kconfig.
FreeRTOS can source them from generated headers or board configuration.

`Node` is the most important refactoring target.
Right now it assumes pthread-style task creation, external raw stack memory, Zephyr stack macros at the call site, and cancellation-based shutdown.
Task creation should be owned by the platform backend.

`Clock` currently includes Zephyr SNTP and Zephyr time headers directly.
Networking also mixes Zephyr APIs into common code and uses fixed sleep delays for DHCP.
The platform layer should expose operations such as initialize network, wait for network ready, choose DDS interface, and optionally synchronize time after the network is up.

**************
Porting Plan
**************
Phase 1: prepare the codebase.
- extract a shared core target
- keep Zephyr builds working
- separate Zephyr wrapper code from shared logic

Phase 2: add the platform abstraction layer.
- abstract tasking
- abstract time and sleep
- abstract config access
- abstract network readiness

Phase 3: reimplement Zephyr on top of the abstraction.
- no intended behavior changes
- use this phase to prove the new platform boundary is correct

Phase 4: add a FreeRTOS simulator build.
- validate startup and shutdown
- validate logging and timers
- validate DDS publisher/subscriber startup

Phase 5: port one real FreeRTOS board.
- integrate FreeRTOS with `lwIP`
- integrate CycloneDDS FreeRTOS support
- bring up Ethernet, time, and DDS

Phase 6: tune and harden.
- stack sizing
- heap sizing
- `lwIP` memory sizing
- startup timing
- end-to-end validation

******************
Board and Risks
******************
A large part of the current platform behavior is hidden in Zephyr board files.
That includes console UART selection, PHY configuration, MDIO initialization order, SRAM placement, MAC configuration, and DDS network interface naming.
All of that must be reproduced in the FreeRTOS BSP or board startup code.

The most practical rollout is:
1. FreeRTOS simulator first
2. one real Ethernet-capable board second

The first embedded target should be the board with the strongest available
FreeRTOS BSP, Ethernet support, and clock support.

The biggest risks are:
- maturity of the chosen FreeRTOS BSP
- Ethernet and `lwIP` integration on the first board
- thread-local storage support for CycloneDDS
- hidden assumptions currently masked by Zephyr startup behavior

There are also medium-level risks around configuration migration, memory sizing, and synchronization behavior once Zephyr POSIX assumptions are removed.

*******************
First Milestone
*******************
The best first milestone is not a full embedded FreeRTOS port.
The best first milestone is:
1. refactor the platform layer
2. keep Zephyr green
3. stand up a FreeRTOS simulator build
4. get DDS pub/sub working there

Once that is stable, the hardware port becomes much lower risk.
