..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

########################
Autoware Safety Island
########################

The Autoware Safety Island is a standalone Zephyr RTOS application that
generates vehicle control commands in Autoware-compatible format and publishes
them over DDS. It runs Autoware's MPC lateral and PID longitudinal controllers
on an Arm safety-class processor, without requiring any modification to the
Autoware codebase on the main compute.

If you are new here, start with the :doc:`user_guide/quickstart` to build and
run the FVP target locally, then read :doc:`overview` for the architecture.

.. toctree::
   :maxdepth: 2
   :caption: Contents

   overview
   user_guide/index
   design/index
   license_file_link
   changelog
