# CAN UDP tunnel gateway

Host side of the Zephyr FVP TAP path. FVP sends one 48-byte UDP datagram per
classic command (`0x100` / `0x101` / `0x102`) to `192.168.10.1:5555`. This
process validates the datagram and injects the frames onto `vcan0`. The
CAN-CARLA bridge is unchanged.

```bash
sudo ip tuntap add dev tap0 mode tap user "$(id -un)" 2>/dev/null || true
sudo ip addr replace 192.168.10.1/24 dev tap0
sudo ip link set dev tap0 up
sudo ip link add vcan0 type vcan
sudo ip link set up vcan0

python3 gateway.py --bind 192.168.10.1 --port 5555 --interface vcan0
```

Build and run FVP from the repository root:

```bash
./build.sh --platform zephyr-fvp --network tap --control-output CAN_ONLY \
  -d build/zephyr-fvp-tap-can
west build -d build/zephyr-fvp-tap-can --target run
```

Then the same CAN-CARLA bridge as POSIX:

```bash
python3 ../can_carla_bridge/bridge.py --interface vcan0 --timeout 5 --role ego_vehicle
```

FVP is not real-time; use a larger decoder timeout than the 0.5 s POSIX default.

Privilege-free tests:

```bash
python3 test_datagram.py
```
