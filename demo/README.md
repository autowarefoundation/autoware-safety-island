# Demo

The demo runs Autoware, the DDS domain bridge, and the visualizer using host networking.

Run compose commands from this `demo/` directory.

## AVH / Hardware Safety Island

The default compose file uses `cyclonedds.xml`, where DDS Domain 2 is pinned to `tap0` for the AVH VPN or hardware tunnel path.

```bash
docker compose up -d
```

## FreeRTOS POSIX Safety Island

For the local FreeRTOS POSIX simulator, use the POSIX compose override. It mounts `cyclonedds.posix.xml`, which keeps Domain 1 autodetected and pins Domain 2 to a multicast-capable host interface.

```bash
SAFETY_ISLAND_DDS_INTERFACE=wlp2s0 docker compose -f docker-compose.yaml -f docker-compose.posix.yaml up -d
```

Build and run the simulator from the repository root with the same interface:

```bash
export PATH="$PWD/build-freertos/cdds_host_out/bin:$PATH"
export LD_LIBRARY_PATH="$PWD/build-freertos/cdds_host_out/lib:${LD_LIBRARY_PATH:-}"

cmake actuation_module/freertos -B build-freertos/app \
  -DCDDS_HOST_PREFIX="$PWD/build-freertos/cdds_host_out" \
  -DCDDS_TARGET_PREFIX="$PWD/build-freertos/cdds_target_out" \
  -DCONFIG_DDS_NETWORK_INTERFACE=wlp2s0 \
  -DCONFIG_CONTROL_CMD_OUTPUT_MODE=DDS_ONLY

cmake --build build-freertos/app -j"$(nproc)"
./build-freertos/app/actuation_freertos
```

Replace `wlp2s0` with the multicast-capable interface on your host, for example `eth0`.

From this `demo/` directory, verify the bridge output:

```bash
docker compose -f docker-compose.yaml -f docker-compose.posix.yaml exec safety-island-bridge bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/autoware/setup.bash && ROS_DOMAIN_ID=1 ros2 topic echo --once /control/trajectory_follower/control_cmd'
```

## Zephyr FVP Safety Island

The FVP demo uses the default compose file because `cyclonedds.xml` already pins DDS Domain 2 to `tap0`.

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

Build the Zephyr FVP image from the repository root. Set `ARMFVP_BIN_PATH` to the directory containing `FVP_BaseR_AEMv8R` if it is not already on the FVP search path.

```bash
export ARMFVP_BIN_PATH=/path/to/fvp/bin

./build.sh -t fvp_baser_aemv8r_smp -d build/zephyr-fvp-tap-demo --fvp-tap-demo
```

Set `FVP_TAP_INTERFACE` before building if your TAP interface is not named `tap0`.

Run FVP:

```bash
west build -d build/zephyr-fvp-tap-demo --target run
```

From this `demo/` directory, verify the bridge output:

```bash
docker compose exec safety-island-bridge bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/autoware/setup.bash && ROS_DOMAIN_ID=1 ros2 topic echo --once /control/trajectory_follower/control_cmd'
```
