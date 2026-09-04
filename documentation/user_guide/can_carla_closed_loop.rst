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
subscriber.

**********************
Pins
**********************

``demo/carla-closed-loop/pins.env`` records the Open AD Kit git SHA and
image refs after the first working host run. CARLA 0.9.16 is already
digest-pinned. Component tags stay floating until that run.

**********************
Known issues
**********************

The ego can stop a few metres past the RViz goal. Autoware's trajectory
ends at the goal with zero speed; the Safety Island PID then loses the
stop line (``calcLongitudinalOffsetToSegment`` returns NaN once the ego
is beyond the last point) and the CAN-to-CARLA Ackermann mapping adds
braking delay. This is on the SI control/actuation path, not planning.

Start the domain-bridge and Safety Island **before** clicking Auto.
``/system/operation_mode/state`` is latched and not periodic; a bridge
that joins after Engage may never forward ``AUTONOMOUS``, and SI stays
STOPPED.

**********************
Tests
**********************

Privilege-free (no CARLA):

.. code-block:: console

  $ python3 demo/can_carla_bridge/test_decoder.py
  $ python3 demo/can_carla_bridge/test_bridge.py
  $ python3 demo/carla-closed-loop/test_contract.py
