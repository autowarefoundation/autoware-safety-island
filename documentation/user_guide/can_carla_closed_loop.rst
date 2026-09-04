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
     $ git checkout FETCH_HEAD
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

Open AD Kit's ``deployments/safety-island-carla-simulation/config.env`` pins
CARLA and component images by digest. Record ``git rev-parse HEAD`` in the
Open AD Kit checkout alongside the Safety Island commit when you run the demo
so a later rerun can use the same pair.

**********************
Tests
**********************

Privilege-free (no CARLA):

.. code-block:: console

  $ python3 demo/can_carla_bridge/test_decoder.py
  $ python3 demo/can_carla_bridge/test_bridge.py
  $ python3 demo/can_tunnel_bridge/test_datagram.py

The deployment contract test lives in Open AD Kit:

.. code-block:: console

  $ python3 deployments/safety-island-carla-simulation/test_contract.py

**********************
Zephyr FVP TAP
**********************

Same open-loop and closed-loop host demos, with Safety Island on
``zephyr-fvp --network tap`` instead of ``freertos-posix``. Frames still
land on ``vcan0``. Native FVP CAN stays loopback-only.

.. code-block:: console

  $ sudo ip tuntap add dev tap0 mode tap user "$(id -un)" 2>/dev/null || true
  $ sudo ip addr replace 192.168.10.1/24 dev tap0
  $ sudo ip link set dev tap0 up
  $ sudo ip link add vcan0 type vcan
  $ sudo ip link set up vcan0
  $ python3 demo/can_tunnel_bridge/gateway.py --bind 192.168.10.1 --port 5555
  $ ./build.sh --platform zephyr-fvp --network tap --control-output CAN_ONLY \
      -d build/zephyr-fvp-tap-can
  $ west build -d build/zephyr-fvp-tap-can --target run
  $ python3 demo/can_carla_bridge/bridge.py --interface vcan0 --timeout 5 \
      --ego-role ego_vehicle

FVP is not real-time; use a larger ``--timeout`` than the 0.5 s POSIX default.
Closed-loop still starts Open AD Kit ``safety-island-carla-simulation`` first.
Its FVP TAP profile binds the domain-bridge's domain 2 to ``tap0``.
