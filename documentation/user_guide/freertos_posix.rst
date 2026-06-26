..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

###############
FreeRTOS POSIX
###############

The ``freertos-posix`` runtime target runs the safety-island application on a
Linux host using the FreeRTOS POSIX port. It is the FreeRTOS local validation
path: useful for DDS discovery, domain-bridge, controller-loop, and CAN encoder
checks without S32Z2 hardware or NXP SDK inputs.

Use ``freertos-posix`` when you want to validate FreeRTOS behavior locally. Use
``freertos-s32z2`` only for the hardware-specific S32Z2 path.

*************
Prerequisites
*************

- Repository submodules initialized with ``git submodule update --init --recursive``.
- A Linux environment with CMake, a C++17 compiler, Eigen3, and the repository's
  FreeRTOS and CycloneDDS submodules. The development container from
  :doc:`quickstart` is the recommended environment.
- For end-to-end DDS validation with the demo containers, a multicast-capable
  host interface such as ``eth0`` or ``wlp2s0``.

*************
Build
*************

From the repository root:

.. code-block:: console

  $ ./build.sh --platform freertos-posix -d build/freertos-posix

``build.sh`` builds the host ``idlc``, builds a static CycloneDDS target
library for the POSIX runtime, and then builds the application.

Output:

.. code-block:: text

  build/freertos-posix/actuation_freertos

The default DDS interface is ``lo`` so local smoke builds do not need network
setup. To communicate with the demo containers or another host, select the
host interface explicitly:

.. code-block:: console

  $ ./build.sh --platform freertos-posix -d build/freertos-posix \
      --dds-interface wlp2s0 \
      --control-output DDS_ONLY

Replace ``wlp2s0`` with the interface that should carry domain-2 DDS traffic.

*************
Run Locally
*************

Run the built executable from the repository root:

.. code-block:: console

  $ ./build/freertos-posix/actuation_freertos

With the default loopback DDS interface this starts the runtime and waits for
input topics on domain 2. It is useful as a smoke test for startup and logging.

***********************
Run With The Demo Stack
***********************

The demo compose override mounts ``demo/cyclonedds.posix.xml`` and pins domain
2 to ``SAFETY_ISLAND_DDS_INTERFACE``. Start the Autoware, bridge, and visualizer
containers from the ``demo/`` directory:

.. code-block:: console

  $ cd demo
  $ SAFETY_ISLAND_DDS_INTERFACE=wlp2s0 \
      docker compose -f docker-compose.yaml -f docker-compose.posix.yaml up -d

Return to the repository root, then build and run the runtime using the same
interface:

.. code-block:: console

  $ cd ..
  $ ./build.sh --platform freertos-posix -d build/freertos-posix \
      --dds-interface wlp2s0 \
      --control-output DDS_ONLY
  $ ./build/freertos-posix/actuation_freertos

Verify bridged control commands from the ``demo/`` directory:

.. code-block:: console

  $ cd demo
  $ docker compose -f docker-compose.yaml -f docker-compose.posix.yaml exec safety-island-bridge bash -lc \
      'source /opt/ros/humble/setup.bash && source /opt/autoware/setup.bash && ROS_DOMAIN_ID=1 ros2 topic echo --once /control/trajectory_follower/control_cmd'

If discovery stalls, check that ``SAFETY_ISLAND_DDS_INTERFACE`` and
``--dds-interface`` name the same multicast-capable host interface. See
:doc:`troubleshooting`.

*************
Test Builds
*************

``freertos-posix`` supports the unit, DDS publisher, DDS subscriber, and CAN
output test flags documented in :doc:`testing`. It does not support the Zephyr
DDS loopback test.
