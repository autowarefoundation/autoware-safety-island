# Demo

The demo runs Autoware, the DDS domain bridge, and the visualizer using host networking.

Run compose commands from this `demo/` directory.

## AVH / Hardware Safety Island

The default compose file uses `cyclonedds.xml`, where DDS Domain 2 is pinned to `tap0` for the AVH VPN or hardware tunnel path.

```bash
docker compose up -d
```

## FreeRTOS POSIX Safety Island

For the local FreeRTOS POSIX runtime, use the POSIX compose override. It mounts `cyclonedds.posix.xml`, which keeps Domain 1 autodetected and pins Domain 2 to a multicast-capable host interface.

```bash
SAFETY_ISLAND_DDS_INTERFACE=wlp2s0 docker compose -f docker-compose.yaml -f docker-compose.posix.yaml up -d
```

Build and run the FreeRTOS POSIX runtime from the repository root with the same interface:

```bash
./build.sh --platform freertos-posix -d build/freertos-posix \
  --dds-interface wlp2s0 \
  --control-output DDS_ONLY

./build/freertos-posix/actuation_freertos
```

Replace `wlp2s0` with the multicast-capable interface on your host, for example `eth0`.

From this `demo/` directory, verify the bridge output:

```bash
docker compose -f docker-compose.yaml -f docker-compose.posix.yaml exec safety-island-bridge bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/autoware/setup.bash && ROS_DOMAIN_ID=1 ros2 topic echo --once /control/trajectory_follower/control_cmd'
```

## Zephyr FVP Safety Island

The Zephyr FVP runtime uses the default compose file because `cyclonedds.xml` already pins DDS Domain 2 to `tap0`.

Build the Zephyr FVP runtime from the repository root. Set `ARMFVP_BIN_PATH` to the directory containing `FVP_BaseR_AEMv8R` if it is not already on the FVP search path.

```bash
export ARMFVP_BIN_PATH=/path/to/fvp/bin

./build.sh --platform zephyr-fvp --network tap -d build/zephyr-fvp-tap
```

Set `FVP_TAP_INTERFACE` before building if your TAP interface is not named `tap0`; the TAP interface name is embedded in the generated FVP runner command.

Set up the host TAP interface:

```bash
sudo ip tuntap add dev tap0 mode tap user "$(id -un)" 2>/dev/null || true
sudo ip addr replace 192.168.10.1/24 dev tap0
sudo ip link set dev tap0 up multicast on
sudo ip link set dev tap0 promisc on
```

From this `demo/` directory, start the demo containers:

```bash
docker compose up -d
```

Run FVP:

```bash
west build -d build/zephyr-fvp-tap --target run
```

From this `demo/` directory, verify the bridge output:

```bash
docker compose exec safety-island-bridge bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/autoware/setup.bash && ROS_DOMAIN_ID=1 ros2 topic echo --once /control/trajectory_follower/control_cmd'
```
