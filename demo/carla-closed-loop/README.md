# Closed-loop CAN to CARLA

Autoware planning against CARLA, Safety Island as the controller, actuation
over classic CAN into the same host bridge as open-loop.

Autoware plus CARLA compose lives in Open AD Kit
`deployments/safety-island-carla-simulation/`. This directory is the
Safety Island side: domain-bridge, `CAN_ONLY` runtime, `vcan0`, and the
CAN-CARLA bridge.

## Prerequisites

- GPU host with the Docker NVIDIA runtime
- Open AD Kit checkout (set `SAFETY_ISLAND_REPO` to this repository when
  starting the Open AD Kit deployment)
- `vcan` kernel module

## Host loop

From the Open AD Kit checkout (no `--drive`):

```bash
export SAFETY_ISLAND_REPO=/path/to/autoware-safety-island
./openadkit run safety-island-carla-simulation --gpu
```

Do not start `carla-simulation` first and recreate `carla-interface`.

Then from this repository:

```bash
sudo ip link add vcan0 type vcan
sudo ip link set up vcan0

cd demo/carla-closed-loop
docker compose up -d

cd ../..
./build.sh --platform freertos-posix -d build/freertos-posix \
  --control-output CAN_ONLY --dds-interface lo
SAFETY_ISLAND_CAN_IFACE=vcan0 ./build/freertos-posix/actuation_freertos

python3 demo/can_carla_bridge/bridge.py --interface vcan0 --role ego_vehicle
```

In RViz: set a goal, engage Auto. `candump vcan0` should show `0x100` /
`0x101` / `0x102`. Autoware's follower may still compute; it must not apply
`VehicleControl` to the ego. The domain-bridge remaps `/planning/trajectory`
to `/planning/scenario_planning/trajectory` on domain 2.

## Pins

`pins.env` records the Open AD Kit git SHA and image refs after the first
working host run. CARLA is already digest-pinned. Component tags stay
floating until that run.

## CI

GitHub Actions does not start CARLA or Open AD Kit. Contract tests:

```bash
python3 demo/can_carla_bridge/test_decoder.py
python3 demo/can_carla_bridge/test_bridge.py
python3 demo/carla-closed-loop/test_contract.py
```
