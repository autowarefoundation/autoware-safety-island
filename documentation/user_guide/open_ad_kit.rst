..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

######################################
Open AD Kit integration (release pins)
######################################

Open AD Kit consumes the Safety Island from a pinned GitHub release instead of
building it. In the planning-simulation CI cell, the FreeRTOS POSIX binary
runs on the same host as Autoware: Autoware on DDS domain 1, the Safety Island
on domain 2, and ``ros2/domain_bridge`` between them.

**************
Release assets
**************

Every release from v0.2.0 on carries these assets, each listed in
``sha256sums.txt``:

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Asset
     - Contents
   * - ``actuation_freertos``
     - FreeRTOS POSIX runtime, linux/amd64. Domain 2, ``DDS_ONLY`` output,
       compile-time DDS interface ``lo``. Built in the 22.04-based
       devcontainer; release CI runs it in plain ``ubuntu:22.04`` and
       ``ubuntu:24.04`` containers before publishing.
   * - ``bridge-config.yaml``
     - The Safety Island's domain-bridge topic contract
       (``demo/bridge/bridge-config.yaml``). Release CI checks it against
       :doc:`../design/topics`.
   * - ``cyclonedds-domain2.xml``
     - The DDS setting every domain-2 writer must use
       (``demo/bridge/cyclonedds-domain2.xml``): ``MaxMessageSize`` 1400 B on
       domain 2 only, no interface. See `Domain-2 message size`_.
   * - ``zephyr-fvp.elf``, ``zephyr-s32z.elf``
     - Zephyr images (not used by the Open AD Kit cell).
   * - ``sha256sums.txt``
     - SHA-256 of every asset above.

Pin by release and checksum rather than copying the files:

.. code-block:: console

  $ TAG=v0.2.0
  $ BASE=https://github.com/autowarefoundation/autoware-safety-island/releases/download/${TAG}
  $ curl -fsSL -O "${BASE}/actuation_freertos" -O "${BASE}/bridge-config.yaml" \
      -O "${BASE}/cyclonedds-domain2.xml" -O "${BASE}/sha256sums.txt"
  $ sha256sum --check --ignore-missing sha256sums.txt
  $ chmod +x actuation_freertos

The binary needs only glibc, ``libstdc++`` and ``libgcc``, which every Ubuntu
base image ships. CycloneDDS is linked statically.

*********************************
Selecting the DDS interface
*********************************

``SI_DDS_INTERFACE`` overrides the compile-time
``CONFIG_DDS_NETWORK_INTERFACE`` at runtime (POSIX build only), so one binary
serves loopback CI and a LAN:

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - ``SI_DDS_INTERFACE``
     - Effect
   * - unset
     - Use the compile-time value (``lo`` in releases).
   * - ``lo``, ``eth0``, ...
     - Select the interface by name.
   * - ``192.168.1.10``
     - Select the interface that owns this IPv4 address.
   * - ``auto`` or empty
     - Let CycloneDDS pick an interface.

The startup log records the choice and where it came from:

.. code-block:: text

  Network interface (by name): lo [SI_DDS_INTERFACE]
  Actuation Safety Island is Live
  Waiting for acceleration data

``Waiting for ... data`` repeats until the bridged inputs arrive. An interface
that does not exist makes DDS domain creation fail and the process exit.

``lo`` is not multicast-capable on Linux, so CycloneDDS disables multicast on
it and discovers peers by unicast. Pin the bridge's domain 2 to the same
interface, for example with ``demo/cyclonedds.posix.xml`` and
``SAFETY_ISLAND_DDS_INTERFACE=lo``.

*********************
Domain-2 message size
*********************

The bridge, and anything else that writes to domain 2, **must** load
``cyclonedds-domain2.xml``.

The Safety Island receives UDP datagrams of at most 2048 bytes
(``cfg.rmsg_chunk_size`` in ``actuation_module/include/common/dds/config.hpp``),
and the sender decides the datagram size. CycloneDDS's default
``MaxMessageSize`` is 14720 B, so on loopback a whole ``Trajectory`` (about
15 KB) leaves in one datagram. The Safety Island logs
``recvUC: ... truncated to 2048`` and drops the datagram. The sample arrives
late, if at all, and the Safety Island latches ``SI_STOP`` on the trajectory
timeout. Capping domain 2 at 1400 B makes CycloneDDS fragment
large samples and also keeps each datagram inside a 1500-byte MTU.

The file sets nothing but domain 2's ``MaxMessageSize``, so it merges with your
own configuration. List it after your file in ``CYCLONEDDS_URI``:

.. code-block:: console

  $ export CYCLONEDDS_URI="file:///path/to/your-cyclonedds.xml,file:///path/to/cyclonedds-domain2.xml"

Your file keeps the interface and discovery settings. The demo does the same:
``demo/common.env`` lists ``demo/cyclonedds*.xml`` first and
``demo/bridge/cyclonedds-domain2.xml`` second. CI rejects any change that adds
other settings to the file or raises the size above the Safety Island's receive
limit.

The other runtime switches (``SI_SUPERVISION_MODE``, ``SI_TRAJECTORY_SOURCE``,
``SI_LEGACY_CONTROL_CMD``, ``SI_OPERATION_MODE_DURABILITY``) are described in
:doc:`../design/vp_si_control_contract` and :doc:`../design/topics`.

.. code-block:: console

  $ SI_DDS_INTERFACE=lo ./actuation_freertos

**************************************
Keeping Autoware's own controller off
**************************************

The Safety Island publishes ``/control/trajectory_follower/control_cmd`` and
the bridge forwards it to domain 1, so Autoware's own trajectory follower must
not publish the same topic.

Autoware with control module presets
  Set the control module preset's ``trajectory_follower_mode`` to ``none``.
  The option is declared in
  ``autoware_control_config/config/preset/default_preset.yaml``:

  .. code-block:: yaml

    launch:
      - arg:
          name: trajectory_follower_mode
          default: none

  Either mount the edited ``default_preset.yaml`` over the installed one, or
  install it as a new preset and select it with ``control_module_preset``.
  The demo does the latter: ``demo/launch/safety_island_preset.yaml`` is
  mounted as ``safety_island_preset.yaml`` and Autoware is launched with
  ``control_module_preset:=safety_island``. No launch file is replaced.

Remove the follower's diagnostic from the graph
  With the follower off, ``controller_node_exe`` never publishes its
  ``control_state`` diagnostic. ``/autoware/control/performance_monitoring/control_state``
  stays stale, and ``change_to_autonomous`` fails with "The target mode is not
  available". Whatever the launch method, remove that node from the
  diagnostic graph.

  With component launches (``autoware.launch.xml``, as Open AD Kit does),
  pass an e2e-style graph as ``diagnostic_graph_aggregator_graph_path``. It
  follows the same pattern as ``autoware_launch``'s ``autoware-main-e2e.yaml``.
  ``demo/launch/autoware-safety-island.yaml`` is that file; install it next to
  ``autoware-main.yaml`` in ``autoware_launch/config/system/diagnostics/``:

  .. code-block:: yaml

    files:
      - { path: $(dirname)/autoware-main.yaml }

    edits:
      - { type: remove, path: /autoware/control/performance_monitoring/control_state }

  ``planning_simulator.launch.xml`` does not yet accept the argument: it
  hard-codes ``autoware-main.yaml``. An autoware_launch PR, "feat(autoware_launch):
  expose diagnostic_graph_aggregator_graph_path in planning_simulator", adds it.
  Until the demo image includes that change, the demo mounts
  ``demo/launch/autoware-main.yaml`` over the image's ``autoware-main.yaml``.
  That file is the image's file verbatim plus the same ``remove`` edit.
  ``.github/workflows/demo-diag-graph.yml`` checks it against the pinned image.
  Both are marked with a TODO to delete once the argument is available.

Older Autoware images (``tier4_control_launch``)
  Images that predate the control module presets, such as
  ``universe-20250207``, have no ``none`` mode. The demo no longer supports
  them. If you must run one, mount the stub ``demo/launch/control.launch.xml``
  from release v0.1.0 over ``tier4_control_launch/launch/control.launch.xml``.
  It keeps the follower node but remaps all its inputs and outputs to
  ``*_no_publish`` topics. Such images also need the trajectory remap
  described in :doc:`../design/topics`.

.. note::

   The Safety Island subscribes to ``/control/trajectory``, the trajectory
   gate output that Autoware's own follower reads in ``autoware_launch``
   main. Published Autoware images up to ``universe-humble-20261102`` do not
   contain the trajectory gate yet: their follower reads
   ``/planning/trajectory`` and nothing publishes ``/control/trajectory``.
   With such an image, add a remap to ``control/trajectory`` in your bridge.
   The demo does this with ``demo/bridge/bridge-overlay.yaml``, which its
   bridge entrypoint merges into the released ``bridge-config.yaml``. The
   released contract itself stays 1:1.
