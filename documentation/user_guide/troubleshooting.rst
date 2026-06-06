..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

################
Troubleshooting
################

Common failure modes seen while bringing the safety island up, with the fix
or workaround.

***********************************************
DDS interface does not match the runtime path
***********************************************

The demo DDS configuration pins domain 2 to a specific interface. If that
interface does not exist or does not carry multicast, CycloneDDS discovery will
stall.

For AVH, S32Z hardware, and Zephyr FVP TAP flows, ``demo/cyclonedds.xml`` pins
domain 2 to ``tap0``. If OpenVPN or local TAP setup creates a different
interface, edit ``demo/cyclonedds.xml`` and update the domain-2
``NetworkInterface`` name.

For ``freertos-posix``, the compose override uses
``demo/cyclonedds.posix.xml`` and reads ``SAFETY_ISLAND_DDS_INTERFACE``. Use the
same interface for the containers and the runtime build:

.. code-block:: console

  $ cd demo
  $ SAFETY_ISLAND_DDS_INTERFACE=wlp2s0 docker compose -f docker-compose.yaml -f docker-compose.posix.yaml up -d
  $ cd ..
  $ ./build.sh --platform freertos-posix -d build/freertos-posix --dds-interface wlp2s0

********************************************
Domain bridge is running but no traffic
********************************************

Check, in order:

1. Both sides use the correct domain IDs. Main compute uses domain 1,
   safety island uses domain 2. The bridge is configured in
   ``demo/bridge/bridge-config.yaml``.
2. ``CYCLONEDDS_URI`` points at the expected file inside the containers.
   The default compose file mounts ``demo/cyclonedds.xml``; the FreeRTOS POSIX
   override mounts ``demo/cyclonedds.posix.xml``.
3. SPDP multicast actually reaches the peer. OpenVPN in ``tap`` mode forwards
   multicast by default, but middle hops may not. To confirm, watch for SPDP
   traffic on the selected domain-2 interface. For AVH/TAP flows this is
   usually ``tap0``; for ``freertos-posix`` use the
   ``SAFETY_ISLAND_DDS_INTERFACE`` value:

   .. code-block:: console

     $ sudo tcpdump -i tap0 'udp and dst net 239.255.0.0/16'

4. For Zephyr DHCP builds, the firmware has booted past DHCP. Watch the serial
   console for a Zephyr ``dhcpv4`` log line announcing the acquired address.

********************************************
Messages are truncated or dropped at size
********************************************

The Zephyr and demo network paths assume a 1500 B MTU, which leaves a safe DDS
payload ceiling of 1400 B. ``demo/cyclonedds.xml`` and
``demo/cyclonedds.posix.xml`` set ``MaxMessageSize`` to ``1400B`` on both
domains to match. If you raise this on either side without validating the
runtime target and bridge path, large Trajectory messages may be silently
dropped.

********************************************
Firmware appears hung for ~10 s at boot
********************************************

On Zephyr DHCP builds this is intentional. ``actuation_module/src/main.cpp``
waits for ``CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX`` seconds before starting the
Controller Node, giving DHCP time to acquire a lease and, if
``CONFIG_ENABLE_SNTP`` is on, SNTP time to sync. The default Zephyr DHCP delay is
10 seconds. This does not apply to ``freertos-posix`` or to Zephyr FVP TAP builds
where DHCP is disabled.

*************************************************************
RViz2 crashes with ``libGL error: failed to create drawable``
*************************************************************

Observed on machines with
`NVIDIA Optimus <https://en.wikipedia.org/wiki/Nvidia_Optimus>`_ graphics.
No reliable workaround; run the visualizer on a different machine.

********************************************
AVH console logs stop streaming
********************************************

``avh.py`` streams the WebSocket console to ``./log/<timestamp>.log.ansi``
and stdout. If the stream cuts out, the instance is almost always still
running — reconnect with:

.. code-block:: console

  $ ./avh.py --ssh

to open a fresh console session.
