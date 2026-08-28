..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##########################
NXP S32Z270DC2 (real HW)
##########################

This guide builds the Zephyr ``zephyr-s32z`` runtime target for the NXP S32Z270DC2
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

  $ ./build.sh --platform zephyr-s32z

The ``@D`` variant selects the RTU0 core on domain D, which is the
configuration the board overlay targets. ``build.sh`` maps ``zephyr-s32z`` to
the Zephyr board target ``s32z270dc2_rtu0_r52@D``.

Resulting binary:

.. code-block:: text

  build/zephyr-s32z/zephyr/zephyr.elf

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
another host) must be on the same L2 network. The Zephyr image requests DHCP
— watch the console for ``dhcpv4`` log lines announcing the acquired address.

Do not reuse the AVH compose defaults for this path. ``demo/cyclonedds.xml``
pins domain 2 to ``tap0``, which is the AVH VPN / FVP TAP interface, not the
board Ethernet. Point domain 2 at the host NIC (or IP) on the same LAN as
the board. ``demo/cyclonedds-s32z2.xml`` is a starting template for a
``192.168.0.0/24`` bench; copy it and set ``NetworkInterface`` to the host
interface if the LAN differs.

From ``demo/``, replace the TAP XML that compose bind-mounts
(``./cyclonedds.xml`` → ``/autoware/cyclonedds.xml``) with the LAN template
before bringing the stack up:

.. code-block:: console

  $ cp cyclonedds-s32z2.xml cyclonedds.xml
  $ # edit Domain 2 NetworkInterface to the host NIC or IP on the board LAN
  $ docker compose up

If you have trouble with discovery or dropped messages, see
:doc:`troubleshooting`.
