# CAN-CARLA bridge

Open-loop host path for PR 1: `freertos-posix` writes classic frames to
SocketCAN; this process decodes `0x100` / `0x101` / `0x102` and applies
`carla.VehicleAckermannControl`.

## Host setup

```bash
sudo ip link add vcan0 type vcan
sudo ip link set up vcan0
pip install -r requirements.txt
```

Build and run the Safety Island (repository root):

```bash
./build.sh --platform freertos-posix -d build/freertos-posix \
  --control-output CAN_ONLY --dds-interface lo
SAFETY_ISLAND_CAN_IFACE=vcan0 ./build/freertos-posix/actuation_freertos
```

Feed canned DDS inputs on domain 2 (rosbag or `--dds-publisher`). Then:

```bash
python3 bridge.py --interface vcan0 --dry-run
python3 bridge.py --interface vcan0 --host 127.0.0.1 --port 2000 --ego-role ego_vehicle
```

`--dry-run` prints decoded Ackermann fields and does not need CARLA.
The live bridge requires exactly one vehicle with `role_name=hero` by default;
use `--ego-role NAME` for another role or `--ego-id ID` to select a specific
vehicle. It fails rather than driving an arbitrary vehicle if selection is
missing or ambiguous.
For Open AD Kit, pass `--ego-role ego_vehicle` (see
`demo/carla-closed-loop/README.md`). Watchdog timeout defaults to 0.5 s.
