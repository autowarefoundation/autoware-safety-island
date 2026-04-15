..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##########################
NXP S32Z270DC2 (real HW)
##########################

This guide builds the safety island firmware for the NXP S32Z270DC2
evaluation board (Cortex-R52, RTU0 core) and flashes it using the S32 Debug
Probe.

Before you start, make sure you can complete the :doc:`quickstart` on the
FVP target — the S32Z flow reuses the same development container and build
script.

*********************
Build for the S32Z
*********************

Inside the development container:

.. code-block:: console

  $ ./build.sh -t s32z270dc2_rtu0_r52@D

The ``@D`` variant selects the RTU0 core on domain D, which is the
configuration the board overlay targets.

Resulting binary:

.. code-block:: text

  build/actuation_module/zephyr/zephyr.elf

**********************
Board-specific notes
**********************

The S32Z configuration lives in three files alongside the default
``prj_actuation.conf``:

- ``actuation_module/boards/s32z270dc2_rtu0_r52.overlay`` — base devicetree
  overlay. Places code in ``sram2`` and pins a unique MAC address on
  ``enetc_psi0`` (working around `Zephyr #61478
  <https://github.com/zephyrproject-rtos/zephyr/issues/61478>`_). Picked
  up automatically by Zephyr's standard board-overlay discovery when
  building the ``s32z270dc2_rtu0_r52@D`` target (filename matches the
  board name).
- ``actuation_module/boards/s32z270dc2_rtu0_r52@D.overlay`` — variant used
  by this project. Switches the console to UART9 and pins the PHY to
  index 2. Layered on top of the base overlay via
  ``EXTRA_DTC_OVERLAY_FILE``, which ``build.sh`` sets when ``-t
  s32z270dc2_rtu0_r52@D`` is selected.
- ``actuation_module/boards/s32z270dc2_rtu0_r52_actuation.conf`` — Kconfig
  fragment. Names the DDS interface ``ethernet@74b00000`` and raises the
  NXP S32 RX thread stack to 16 KiB.

**********
Flashing
**********

Flash with the default Zephyr runner for the board (the NXP S32 Debug Probe
is the official supported option):

.. code-block:: console

  $ west flash

Console output comes out over UART9. Configure your serial terminal for
115200 8N1.

****************
Network setup
****************

The S32Z and the Autoware main compute (e.g. an AVA Developer Platform or
another host) must be on the same sub-network. The board is configured for
DHCP and will obtain its address shortly after boot — watch the console for
Zephyr ``dhcpv4`` log lines announcing the acquired address.

Once the board has an address, bring up Autoware on the main compute and
start the domain bridge so the safety island can receive planning data. See
:doc:`avh` (section *Start Autoware*) for the Docker Compose flow.

If you have trouble with discovery or dropped messages, see
:doc:`troubleshooting`.
