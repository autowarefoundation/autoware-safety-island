..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##########
Quickstart
##########

This guide builds the default ``zephyr-fvp`` runtime target for the Arm Fixed
Virtual Platform (``fvp_baser_aemv8r_smp``). The produced ELF runs on a local
FVP model and can also be used with Arm Virtual Hardware workflows.

For local FreeRTOS validation, see :doc:`freertos_posix`. For running the full
Autoware + safety island loop on AVH, follow :doc:`avh` after building. For
real hardware on the NXP S32Z270DC2, see :doc:`s32z_board`.

*************
Prerequisites
*************

- Ubuntu 22.04 (validated) with Docker installed.
- Enough free disk space for the development container image plus the
  Zephyr SDK and build artifacts.

*********************
Clone the repository
*********************

.. code-block:: console

  $ git clone https://github.com/autowarefoundation/autoware-safety-island.git
  $ cd autoware-safety-island
  $ git submodule update --init --recursive

***********************************
Enter the development container
***********************************

The ``launch-dev-container.sh`` script pulls and runs the
``ghcr.io/autowarefoundation/autoware-safety-island:devcontainer`` image, which
has the Zephyr SDK, ``west``, and the Python tooling pre-installed.

.. code-block:: console

  $ ./launch-dev-container.sh

All build commands below are run **inside** this container.

************************
Build the default target
************************

.. code-block:: console

  $ ./build.sh --platform zephyr-fvp

``build.sh`` with no arguments also compiles ``zephyr-fvp``. It builds the
CycloneDDS host-side IDL compiler, then invokes ``west build`` for
``fvp_baser_aemv8r_smp``.

The resulting binary is written to:

.. code-block:: text

  build/actuation_module/zephyr/zephyr.elf

``build.sh --platform`` selects a runtime target:

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Target
     - Purpose
   * - ``zephyr-fvp``
     - Zephyr on Arm FVP for local validation / AVH.
   * - ``zephyr-s32z``
     - Zephyr on S32Z hardware.
   * - ``freertos-posix``
     - FreeRTOS POSIX runtime for local validation.
   * - ``freertos-s32z2``
     - FreeRTOS on S32Z2 hardware, requiring NXP SDK inputs.

Other ``build.sh`` flags are documented in :doc:`testing`, :doc:`freertos_posix`,
and :doc:`s32z_board`.

**************************
What to do next
**************************

- Deploy the firmware to an AVH instance: :doc:`avh`.
- Run the full Autoware + safety island demo: :doc:`avh` (section
  *Running the Demo*).
- Validate the FreeRTOS local runtime: :doc:`freertos_posix`.
- Flash a physical S32Z board: :doc:`s32z_board`.
- Understand the runtime: :doc:`/design/architecture` and
  :doc:`/design/topics`.
