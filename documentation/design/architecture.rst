..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##############
Architecture
##############

The safety island hosts the Autoware trajectory follower and talks to the
main compute over DDS. This page describes how the pieces fit together at
runtime on the supported runtime targets. The controller logic is shared
across backends via a platform abstraction layer
(``actuation_module/include/platform/``); Zephyr and FreeRTOS targets provide
different backend implementations under that layer.

************************
Two-domain DDS topology
************************

The main compute and the safety island live on separate CycloneDDS
domains:

- **Domain 1** — Autoware main compute (full ROS 2 graph).
- **Domain 2** — Safety island runtime target (CycloneDDS, no ROS 2 runtime).

A ``ros2 domain_bridge`` instance running on the main compute forwards
exactly the topics the safety island needs between the two domains. The
list is fixed in ``demo/bridge/bridge-config.yaml``.

.. mermaid::

   graph LR
       subgraph "Main compute (domain 1)"
           Autoware[Autoware Universe]
           Bridge[domain_bridge]
       end

        subgraph "Safety island (domain 2)"
            Firmware[Safety island runtime<br/>Controller Node]
        end

       Autoware -- trajectory, odometry,<br/>steering, accel,<br/>operation_mode --> Bridge
       Bridge -- forwarded topics --> Firmware
       Firmware -- control_cmd --> Bridge
       Bridge -- control_cmd --> Autoware

Full topic list with message types: :doc:`topics`.

The default AVH and hardware demo domains are configured through
``demo/cyclonedds.xml``. The FreeRTOS POSIX local validation flow uses
``demo/cyclonedds.posix.xml`` to pin Domain 2 to a multicast-capable host
interface. The key tunables are shared across the two domains:

- ``MaxMessageSize = 1400B`` — matches the safety-island MTU.
- ``AllowMulticast = spdp`` — SPDP discovery over multicast, application
  traffic unicast.
- In ``demo/cyclonedds.xml``, Domain 2 pins its interface to ``tap0`` for the
  AVH VPN or Zephyr FVP TAP path.
- In ``demo/cyclonedds.posix.xml``, Domain 2 pins its interface to
  ``SAFETY_ISLAND_DDS_INTERFACE`` for the FreeRTOS POSIX local path.
- If discovery stalls, verify the selected interface — see
  :doc:`/user_guide/troubleshooting`.

**********************
Controller node
**********************

The entry point is ``actuation_module/src/main.cpp``. It brings up DHCP,
optionally syncs time via SNTP (``CONFIG_ENABLE_SNTP``), and instantiates
a single ``Controller`` node. The controller is defined in
``actuation_module/src/autoware/autoware_trajectory_follower_node/src/controller_node.cpp``.

At construction the controller:

1. Declares a control period. Default **150 ms**. Upstream Autoware uses
   30 ms; the safety island runs slower because the MPC solve is the
   dominant cost on a Cortex-R class core.
2. Declares a stale-output timeout of **0.5 s**. The ``isTimeOut`` helper still
   exists in ``controller_node.cpp``, but the call is currently disabled in the
   timer callback. Missing inputs are handled by ``processData``, which skips
   the tick and logs a throttled "Waiting for ..." message.
3. Instantiates the lateral controller (``mpc`` — only mode currently
   supported) and the longitudinal controller (``pid``).
4. Creates five subscriptions and three publishers. See :doc:`topics`.
5. Starts a periodic timer that runs ``callbackTimerControl`` every
   ``ctrl_period``.

****************
RTOS primitives
****************

The ``common`` abstraction layer under ``actuation_module/include/common/``
hides runtime-specific primitives behind small interfaces:

- ``node/node.hpp`` — thread and timer wrappers. The common code uses a
  POSIX-style API; the platform layer supplies compatible stack and threading
  definitions for Zephyr and FreeRTOS.
- ``dds/`` — CycloneDDS publisher/subscriber templates with ROS 2
  topic-name translation.
- ``clock/clock.hpp`` — monotonic clock, optional SNTP-backed wall clock.
- ``logger/logger.hpp`` — throttled logging.

This layering means replacing the RTOS backend is a question of swapping
these four headers, not rewriting the controller. The FreeRTOS backend
under ``include/platform/freertos/`` demonstrates this in practice.

****************
Build pipeline
****************

``build.sh`` drives the selected runtime target pipeline.

For Zephyr targets (``zephyr-fvp`` and ``zephyr-s32z``):

1. Compiles the CycloneDDS host tools (IDLC) under ``build/cyclonedds_host``.
2. Invokes ``west build`` with the selected target
   (``fvp_baser_aemv8r_smp`` or ``s32z270dc2_rtu0_r52@D``).
3. Produces ``build/actuation_module/zephyr/zephyr.elf``.

For ``freertos-posix``:

1. Builds the CycloneDDS host tools.
2. Builds a static CycloneDDS target library for the POSIX runtime.
3. Configures and builds ``actuation_module/freertos``.
4. Produces ``build/freertos-posix/actuation_freertos`` unless ``-d`` selects a
   different directory.

For ``freertos-s32z2``:

1. Builds the CycloneDDS host tools.
2. Cross-builds CycloneDDS for Cortex-R52 using the S32Z2 toolchain file.
3. Configures and builds ``actuation_module/freertos_s32z2`` with NXP SDK and
   S32 Config Tools inputs.
4. Produces ``build/freertos-s32z2/actuation_freertos_s32z2.elf`` unless ``-d``
   selects a different directory.

IDL messages under
``actuation_module/src/autoware/autoware_msgs/`` are compiled to C by
IDLC and linked into the firmware. The Autoware C++ packages
(``autoware_mpc_lateral_controller``, etc.) are compiled directly against
the selected runtime backend; there is no ROS 2 runtime on the safety island.
