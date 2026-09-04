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

- **Open AD Kit** ``deployments/safety-island-carla-simulation/``: CARLA,
  sensors-only ``autoware_carla_interface``. Autoware's trajectory follower
  stays running so Auto/Engage works; it must not actuate CARLA.
- **This repository**: domain-bridge, ``freertos-posix`` ``CAN_ONLY``,
  ``vcan0``, and ``demo/can_carla_bridge``.

Do not vendor Open AD Kit sources here. Set ``SAFETY_ISLAND_REPO`` to this
checkout when starting the Open AD Kit deployment so it can bind-mount
the sensors-only overlay.

**********************
Host loop
**********************

GPU host, Docker NVIDIA runtime, and the ``vcan`` kernel module. Open AD Kit
also needs large UDP buffers; see that project's CARLA simulation docs.

1. Start Open AD Kit (no ``--drive``):

   .. code-block:: console

     $ export SAFETY_ISLAND_REPO=/path/to/autoware-safety-island
     $ cd /path/to/openadkit
     $ ./openadkit run safety-island-carla-simulation --gpu

   Do not start ``carla-simulation`` first and recreate
   ``carla-interface``. That leaves a second ego in CARLA and breaks NDT.

2. From this repository:

   .. code-block:: console

     $ sudo ip link add vcan0 type vcan
     $ sudo ip link set up vcan0
     $ docker compose -f demo/carla-closed-loop/docker-compose.yaml up -d
     $ ./build.sh --platform freertos-posix -d build/freertos-posix \
         --control-output CAN_ONLY --dds-interface lo
     $ SAFETY_ISLAND_CAN_IFACE=vcan0 ./build/freertos-posix/actuation_freertos
     $ python3 demo/can_carla_bridge/bridge.py --interface vcan0 --role ego_vehicle

3. In RViz: set a goal, engage. ``candump vcan0`` should show ``0x100`` /
   ``0x101`` / ``0x102``.

Confirm Autoware is not applying ``VehicleControl`` to the ego. The overlay
skips ``apply_control()``; the CAN bridge applies
``VehicleAckermannControl``. Autoware Auto/Engage is required so SI sees
``AUTONOMOUS``; it does not mean Autoware drives CARLA.

**********************
DDS
**********************

Open AD Kit pins domain 1 to loopback. Closed-loop CycloneDDS in
``demo/carla-closed-loop/cyclonedds.xml`` pins domains 1 and 2 to ``lo``.
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

``demo/carla-closed-loop/pins.env`` records the Open AD Kit git SHA and
image refs after the first working host run. CARLA 0.9.16 is already
digest-pinned. Component tags stay floating until that run.

**********************
Tests
**********************

Privilege-free (no CARLA):

.. code-block:: console

  $ python3 demo/can_carla_bridge/test_decoder.py
  $ python3 demo/can_carla_bridge/test_bridge.py
  $ python3 demo/carla-closed-loop/test_contract.py
  $ python3 demo/can_tunnel_bridge/test_datagram.py

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
      --role ego_vehicle

FVP is not real-time; use a larger ``--timeout`` than the 0.5 s POSIX default.
Closed-loop still starts Open AD Kit ``safety-island-carla-simulation`` first.
