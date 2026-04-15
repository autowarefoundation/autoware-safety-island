..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

################
Troubleshooting
################

Common failure modes seen while bringing the safety island up, with the fix
or workaround.

********************************************
VPN interface is not ``tap0``
********************************************

``demo/cyclonedds.xml`` pins domain 2 to the interface named ``tap0``. If
your OpenVPN client creates the tunnel with a different name (for example
``tun0`` or ``tap1``), CycloneDDS silently fails to find a route to the
safety island.

Fix: edit ``demo/cyclonedds.xml`` and update the ``name`` attribute of the
domain 2 ``NetworkInterface`` to match the interface that ``ip a`` reports
after the VPN comes up.

********************************************
Domain bridge is running but no traffic
********************************************

Check, in order:

1. Both sides use the correct domain IDs. Main compute uses domain 1,
   safety island uses domain 2. The bridge is configured in
   ``demo/bridge/bridge-config.yaml``.
2. ``CYCLONEDDS_URI`` points at ``demo/cyclonedds.xml`` on the host
   (``docker compose up`` sets this in ``demo/docker-compose.yaml``).
3. SPDP multicast actually reaches the peer. OpenVPN in ``tap`` mode
   forwards multicast by default, but middle hops may not. To confirm,
   watch for SPDP traffic on the tunnel:

   .. code-block:: console

     $ sudo tcpdump -i tap0 'udp and dst net 239.255.0.0/16'

4. The firmware has booted past DHCP. Watch the serial console for a
   Zephyr ``dhcpv4`` log line announcing the acquired address.

********************************************
Messages are truncated or dropped at size
********************************************

The safety island runs on Zephyr with an MTU of 1500 B, which leaves a
safe payload ceiling of 1400 B. ``demo/cyclonedds.xml`` sets
``MaxMessageSize`` to ``1400B`` on both domains to match. If you raise
this on either side without a corresponding Zephyr change, large Trajectory
messages will be silently dropped.

********************************************
Firmware appears hung for ~10 s at boot
********************************************

This is intentional. ``actuation_module/src/main.cpp`` blocks for
``CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX`` seconds (10 by default) before
starting the Controller Node, giving DHCP time to acquire a lease and —
if ``CONFIG_ENABLE_SNTP`` is on — SNTP time to sync. Only assume a
failure if nothing happens after ~30 s.

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
