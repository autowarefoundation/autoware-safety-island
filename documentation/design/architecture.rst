..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##############
Architecture
##############

The safety island hosts the Autoware trajectory follower and talks to the
main compute over DDS. This page describes how the pieces fit together at
runtime on the Zephyr backend. The controller logic is shared across
backends via a platform abstraction layer
(``actuation_module/include/platform/``); a FreeRTOS POSIX simulator uses
the same core with a different backend under ``include/platform/freertos/``.

************************
Two-domain DDS topology
************************

The main compute and the safety island live on separate CycloneDDS
domains:

- **Domain 1** — Autoware main compute (full ROS 2 graph).
- **Domain 2** — Safety island (Zephyr + CycloneDDS, no ROS 2 runtime).

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
           Firmware[Zephyr firmware<br/>Controller Node]
       end

       Autoware -- trajectory, odometry,<br/>steering, accel,<br/>operation_mode --> Bridge
       Bridge -- forwarded topics --> Firmware
       Firmware -- control_cmd --> Bridge
       Bridge -- control_cmd --> Autoware

Full topic list with message types: :doc:`topics`.

Both domains are configured through ``demo/cyclonedds.xml``. The key
tunables are shared across the two domains:

- ``MaxMessageSize = 1400B`` — matches the safety-island MTU.
- ``AllowMulticast = spdp`` — SPDP discovery over multicast, application
  traffic unicast.
- Domain 2 pins its interface to ``tap0`` (the VPN tunnel). If your
  VPN creates a different interface, update this file — see
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
2. Declares a stale-output timeout of **0.5 s**. If the last control
   command produced by either the lateral or longitudinal controller is
   older than this, the controller refuses to publish it
   (``isTimeOut`` in ``controller_node.cpp``). Missing inputs are handled
   separately: ``processData`` skips the tick and logs a throttled
   "Waiting for ..." message.
3. Instantiates the lateral controller (``mpc`` — only mode currently
   supported) and the longitudinal controller (``pid``).
4. Creates five subscriptions and three publishers. See :doc:`topics`.
5. Starts a periodic timer that runs ``callbackTimerControl`` every
   ``ctrl_period``.

****************
RTOS primitives
****************

The ``common`` abstraction layer under ``actuation_module/include/common/``
hides Zephyr specifics behind small interfaces:

- ``node/node.hpp`` — thread and timer wrappers. Uses the pthread API
  (``CONFIG_POSIX_API=y``) on top of Zephyr-allocated stacks; the caller
  still provides stack memory via ``K_THREAD_STACK_DEFINE``.
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

``build.sh`` drives the whole pipeline:

1. Compiles the CycloneDDS host tools (IDLC) under ``build/cyclonedds_host``.
2. Invokes ``west build`` with the selected target
   (``fvp_baser_aemv8r_smp`` or ``s32z270dc2_rtu0_r52@D``).
3. Produces ``build/actuation_module/zephyr/zephyr.elf``.

IDL messages under
``actuation_module/src/autoware/autoware_msgs/`` are compiled to C by
IDLC and linked into the firmware. The Autoware C++ packages
(``autoware_mpc_lateral_controller``, etc.) are compiled directly against
Zephyr; there is no ROS 2 runtime on the safety island.
