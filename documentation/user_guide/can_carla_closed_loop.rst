..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

########################################
Closed-loop CAN and CARLA
########################################

Drive a CARLA ego from Safety Island control over classic CAN while Autoware
plans against the simulator. Design: :doc:`../design/can_carla_integration`.

This is a **host demo**. GitHub Actions never starts CARLA or Open AD Kit.

**********************
Two repositories
**********************

- **Open AD Kit PR #146** ``deployments/safety-island-carla-simulation/``:
  CARLA, domain-bridge, sensors-only ``autoware_carla_interface`` and its
  deployment configuration. Autoware's follower stays running for Auto/Engage
  but does not actuate CARLA.
- **This repository**: ``freertos-posix`` ``CAN_ONLY``, ``vcan0``, and
  ``demo/can_carla_bridge``.

Open AD Kit's deployment and sensors-only overlay are maintained in
`PR #146 <https://github.com/autowarefoundation/openadkit/pull/146>`_.

**********************
Host loop
**********************

GPU host, Docker NVIDIA runtime, and the ``vcan`` kernel module. Open AD Kit
also needs large UDP buffers; see that project's CARLA simulation docs.

1. Start Open AD Kit (no ``--drive``):

   .. code-block:: console

     $ cd /path/to/openadkit
     $ git fetch origin pull/146/head
     $ git switch --detach 540fd13792c6e47546974ab7f9c9df5f575405e2
     $ ./openadkit run safety-island-carla-simulation --gpu

   Do not start ``carla-simulation`` first and recreate
   ``carla-interface``. That leaves a second ego in CARLA and breaks NDT.

2. From this repository:

   .. code-block:: console

     $ sudo ip link add vcan0 type vcan
     $ sudo ip link set up vcan0
     $ ./build.sh --platform freertos-posix -d build/freertos-posix \
         --control-output CAN_ONLY --dds-interface lo
     $ SAFETY_ISLAND_CAN_IFACE=vcan0 ./build/freertos-posix/actuation_freertos
     $ python3 demo/can_carla_bridge/bridge.py --interface vcan0 --ego-role ego_vehicle

3. In RViz: set a goal, engage. ``candump vcan0`` should show ``0x100`` /
   ``0x101`` / ``0x102``.

Confirm Autoware is not applying ``VehicleControl`` to the ego. The overlay
skips ``apply_control()``; the CAN bridge applies
``VehicleAckermannControl``. Autoware Auto/Engage is required so SI sees
``AUTONOMOUS``; it does not mean Autoware drives CARLA.

**********************
DDS
**********************

Open AD Kit pins domain 1 to loopback. Its closed-loop deployment's
``cyclonedds.xml`` pins domains 1 and 2 to ``lo``.
Build the Safety Island with ``--dds-interface lo``. The domain-bridge
forwards the five controller inputs 1 → 2 and does not forward
``control_cmd`` (``CAN_ONLY``). Open AD Kit publishes
``/planning/trajectory``; the bridge remaps it to
``/planning/scenario_planning/trajectory`` on domain 2 for the SI
subscriber. ``/system/operation_mode/state`` is bridged with
``transient_local`` durability so a late-joining SI still sees
``AUTONOMOUS``.

**********************
Pins
**********************

The commands above check out Open AD Kit PR #146 at commit
``540fd13792c6e47546974ab7f9c9df5f575405e2`` for repeatable runs.
Its ``deployments/safety-island-carla-simulation/config.env`` pins CARLA
and component images by digest.

**********************
Tests
**********************

Privilege-free (no CARLA):

.. code-block:: console

  $ python3 demo/can_carla_bridge/test_decoder.py
  $ python3 demo/can_carla_bridge/test_bridge.py

The deployment contract test lives in Open AD Kit:

.. code-block:: console

  $ python3 deployments/safety-island-carla-simulation/test_contract.py
