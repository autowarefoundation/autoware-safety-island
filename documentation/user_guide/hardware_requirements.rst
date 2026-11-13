..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

#####################
Hardware Requirements
#####################

.. note::

   **Preliminary.** The memory figures were measured on the built images. The
   CPU clock figure is an estimate and has not been measured on a board.
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
     - 1× Arm Cortex-R52 (Armv8-R), single core. About 200–450 MHz for
       the 150 ms control period.
     - Estimate from FVP instruction counts (see `CPU`_)
   * - RAM
     - At least 4–5 MiB, 8 MiB recommended
     - Measured (see `Memory`_). Code runs from RAM on both boards.
   * - Storage
     - At least 2 MiB
     - Measured image size is 1.2–1.7 MiB
   * - Network
     - Ethernet for DDS over UDP, or RPMsg when the Safety Island shares an
       SoC with Linux (R-Car X5H)
     - DDS link to the Autoware main compute
   * - Vehicle bus
     - At least one CAN-FD channel
     - Needed only for the ``CAN_ONLY`` and ``DDS_AND_CAN`` control outputs
   * - Timer
     - Hardware timer with a 1 kHz tick
     - RTOS scheduling
   * - Not needed
     - Linux, MMU, GPU, NPU
     - Runs on FreeRTOS or Zephyr

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
     - Detects CPU faults (ASIL-D). Both tested boards run the R52 in
       lock-step.
   * - ECC on RAM
     - Detects and corrects memory bit errors.
   * - MPU
     - Keeps tasks from writing to each other's memory.
   * - Isolation from the main compute
     - Separate core, ideally with separate clock and power domains, so the
       Safety Island keeps running when the main compute fails.
   * - Hardware watchdog
     - Resets a hung core. The current code does not use one; its arrival
       watchdogs are software timers.

***********
Measurement
***********

Measured on commit ``dea86dd``.

Memory
======

Section sizes come from the ELF section headers.

.. list-table::
   :widths: 22 16 10 22 30
   :header-rows: 1

   * - Image
     - Code + rodata
     - Data
     - BSS + stacks
     - Heap
   * - ``zephyr-s32z``
     - 1.19 MiB
     - 40 KiB
     - 2.10 MiB (1.5 MiB thread stacks)
     - Rest of the 7 MB RAM region (~3.6 MiB)
   * - ``freertos-x5h``
     - 1.72 MiB
     - 13 KiB
     - 1.73 MiB
     - 5 MiB reserved. The image uses 84.6% of the 10 MiB Core1 slot.

The heap high-water mark on FVP was about 0.9 MiB. FVP runs 64-bit code, so
the R52 should use less.

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

- The R52 runs AArch32 code, so it needs 1.0–1.3× the instructions FVP's
  AArch64 code needs.
- The R52 takes 1–2 cycles per instruction.

With these assumptions, one period is 12–32 million cycles. Keeping CPU load at
or below 50% then needs:

- **About 200–450 MHz** for the current 150 ms control period.
- **About 750 MHz–1.9 GHz** for a 30 Hz (33 ms) control period, which is
  marginal for a single R52.

To replace the estimate with a measurement, read the R52 cycle counter
(``PMCCNTR``) around the control step on a board.
