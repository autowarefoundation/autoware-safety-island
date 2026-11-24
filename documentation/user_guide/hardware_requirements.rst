..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

#####################
Hardware Requirements
#####################

.. note::

   **Preliminary.** The static memory figures were measured on the built
   images; the heap figure comes from FVP. The CPU clock figure is an estimate
   and has not been measured on a board.
   Automated measurement is tracked in `issue #84
   <https://github.com/autowarefoundation/autoware-safety-island/issues/84>`_.

****************
Required to run
****************

.. list-table::
   :widths: 20 45 35
   :header-rows: 1

   * - Item
     - Requirement
     - Basis
   * - CPU
     - 1× Arm Cortex-R52 (Armv8-R), single core. About 160–660 MHz for
       the 150 ms control period.
     - Estimate from FVP instruction counts (see `CPU`_)
   * - RAM
     - At least 4–5 MiB, 8 MiB recommended
     - Static footprint measured on the images, heap from FVP (estimate); see
       `Memory`_. Code runs from RAM on both boards.
   * - Storage
     - At least 2 MiB
     - Measured image size is 1.2–1.7 MiB
   * - Network
     - Ethernet for DDS over UDP, or RPMsg when the Safety Island shares an
       SoC with Linux (R-Car X5H). IPv4 only, with an MTU of at least 1500
       bytes: the Safety Island receives datagrams of at most 2048 bytes and
       does not reassemble IP fragments.
     - DDS link to the Autoware main compute. Every domain-2 writer caps its
       datagrams at 1400 B (``demo/bridge/cyclonedds-domain2.xml``, see
       :doc:`open_ad_kit`)
   * - Vehicle bus
     - One classic CAN 2.0 channel (8-byte frames). CAN-FD is optional and
       only used by ``freertos-posix``.
     - Needed only for the ``CAN_ONLY`` and ``DDS_AND_CAN`` control outputs.
       ``zephyr-s32z`` sends classic CAN (``DDS_AND_CAN`` by default);
       ``freertos-s32z2`` and ``freertos-x5h`` have no CAN output.
   * - Timer
     - A system timer with 1 ms resolution, such as the R52 generic timer
     - RTOS scheduling; the board images run a 1 kHz tick
   * - Not needed
     - Linux on the Safety Island core, MMU, GPU, NPU
     - Runs on FreeRTOS or Zephyr. On R-Car X5H, Linux on another core loads
       it and is its RPMsg peer.

***********************************
Recommended for a safety deployment
***********************************

The code runs without these, but a deployment that controls a vehicle should
have them.

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Item
     - Why
   * - Lock-step core
     - Detects CPU faults (ASIL-D). The S32Z2 board runs its R52 RTU0 in
       lock-step (the reset default; see :doc:`../design/freertos-s32z2-bringup`).
   * - ECC on RAM
     - Detects and corrects memory bit errors.
   * - MPU
     - Keeps tasks from writing to each other's memory.
   * - Isolation from the main compute
     - Separate core, ideally with separate clock and power domains, so the
       Safety Island keeps running when the main compute fails.
   * - Hardware watchdog
     - Resets a hung core. The current code does not use one. Its arrival
       watchdogs are age checks run on the software control timer, so they stop
       if that timer stalls.

***********
Measurement
***********

Measured on ``dea86dd``, the branch of pull request #77 before it was merged
to ``main`` as ``578252a``. The ``zephyr-s32z`` figures are the same for the
v0.3.0 release image, and the ``freertos-x5h`` image differs by 720 bytes.

Memory
======

Section sizes come from the ELF section headers.

.. list-table::
   :widths: 22 16 10 22 30
   :header-rows: 1

   * - Image
     - Code + rodata
     - Data
     - Zero-initialised
     - Heap
   * - ``zephyr-s32z``
     - 1.19 MiB
     - 40 KiB
     - 2.10 MiB: about 0.7 MiB thread stacks, 0.8 MiB network buffers,
       0.2 MiB Ethernet DMA rings and 0.4 MiB BSS
     - Rest of the 7 MiB RAM region (~3.7 MiB)
   * - ``freertos-x5h``
     - 1.72 MiB
     - 13 KiB
     - 1.73 MiB
     - 5 MiB reserved. The image uses 84.6% of the 10 MiB Core1 slot.

The heap high-water mark on FVP was about 0.9 MiB. FVP runs 64-bit code, so
the R52 should use less. On FreeRTOS, a thread created without its own stack
takes it from the heap, which Zephyr's static stacks do not, so ``freertos-x5h``
needs more heap than this figure. ``freertos-s32z2`` was not measured; its
FreeRTOS heap is 3 MiB.

CPU
===

The control step was instrumented with the PMU "instructions retired" counter
on FVP. The repository rosbag was then replayed into it, and 229 control steps
were recorded.

.. list-table::
   :widths: 40 20 20 20
   :header-rows: 1

   * - Stage (instructions)
     - p50
     - p95
     - max
   * - MPC lateral
     - 7.5M
     - 8.7M
     - 8.8M
   * - PID longitudinal
     - 0.30M
     - 0.46M
     - 0.55M
   * - Whole control step
     - 7.9M
     - 9.2M
     - 9.3M
   * - Whole 150 ms period (incl. DDS, network, logging)
     - 9.5M
     - 11.8M
     - 12.3M

FVP has no timing model: its cycle counter always equals its instruction
count. The clock figure therefore rests on two assumptions:

- The R52 runs AArch32 code, so it needs 1.0–2.0× the instructions FVP's
  AArch64 code needs. The upper end covers the MPC: its Eigen kernels use
  AArch64's double-precision vector instructions (``fmla .2d``), and AArch32
  Advanced SIMD has none, so on the R52 they run as scalar code.
- The R52 takes 1–2 cycles per instruction.

With these assumptions, one period is 12–49 million cycles. Keeping CPU load at
or below 50% then needs:

- **About 160–660 MHz** for the current 150 ms control period, or about
  160–430 MHz if the AArch32 code needs at most 1.3× the instructions.
- **About 750 MHz–3 GHz** for a 30 Hz (33 ms) control period, which a single
  R52 may not reach. This keeps the whole period's DDS and logging load of the
  150 ms measurement, so it is conservative.

The FVP instrumentation is not in the repository yet (issue #84). To replace
the estimate with a measurement, read the R52 cycle counter (``PMCCNTR``)
around the control step on a board. On ``freertos-x5h`` the counter already
runs from boot (``EnablePMU()`` in ``vendor_patched/system_rcar_gen5.c``).
