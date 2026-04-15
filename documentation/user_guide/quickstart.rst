..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##########
Quickstart
##########

This guide builds the safety island firmware locally for the Arm Fixed
Virtual Platform (``fvp_baser_aemv8r_smp``). The produced ELF runs either
on a local FVP simulator or, more commonly, on Arm Virtual Hardware in the
cloud.

For running the full Autoware + safety island loop on AVH, follow
:doc:`avh` after building. For real hardware on the NXP S32Z270DC2, see
:doc:`s32z_board`.

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

**************************
Build the Zephyr firmware
**************************

.. code-block:: console

  $ ./build.sh

``build.sh`` with no arguments compiles the default FVP target. It builds the
CycloneDDS host-side IDL compiler, then invokes ``west build`` for
``fvp_baser_aemv8r_smp``.

The resulting binary is written to:

.. code-block:: text

  build/actuation_module/zephyr/zephyr.elf

Other ``build.sh`` flags are documented in :doc:`testing` and :doc:`s32z_board`.

**************************
What to do next
**************************

- Deploy the firmware to an AVH instance: :doc:`avh`.
- Run the full Autoware + safety island demo: :doc:`avh` (section
  *Running the Demo*).
- Flash a physical S32Z board: :doc:`s32z_board`.
- Understand the runtime: :doc:`/design/architecture` and
  :doc:`/design/topics`.
