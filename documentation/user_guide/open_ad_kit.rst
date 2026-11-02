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
   * - ``zephyr-fvp.elf``, ``zephyr-s32z.elf``
     - Zephyr images (not used by the Open AD Kit cell).
   * - ``sha256sums.txt``
     - SHA-256 of every asset above.

Pin by release and checksum rather than copying the files:

.. code-block:: console

  $ TAG=v0.2.0
  $ BASE=https://github.com/autowarefoundation/autoware-safety-island/releases/download/${TAG}
  $ curl -fsSL -O "${BASE}/actuation_freertos" -O "${BASE}/bridge-config.yaml" -O "${BASE}/sha256sums.txt"
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

Current Autoware (``autoware_launch`` main)
  Set the control module preset's ``trajectory_follower_mode`` to ``none``.
  The option is declared in
  ``autoware_control_config/config/preset/default_preset.yaml``:

  .. code-block:: yaml

    launch:
      - arg:
          name: trajectory_follower_mode
          default: none

  Either mount the edited ``default_preset.yaml`` over the installed one, or
  install it as a new preset (for example ``safety_island_preset.yaml``) and
  launch with ``control_module_preset:=safety_island``. No launch file is
  replaced.

Older Autoware images (``tier4_control_launch``)
  Images that predate the control module presets, such as
  ``ghcr.io/autowarefoundation/autoware:universe-20250207`` used by
  ``demo/docker-compose.yaml``, have no ``none`` mode. For those only, mount
  the stub ``demo/launch/control.launch.xml`` over
  ``tier4_control_launch/launch/control.launch.xml``. The stub keeps the
  follower node but remaps all its inputs and outputs to ``*_no_publish``
  topics. It will be removed once the demo moves to a preset-capable image.

.. note::

   The Safety Island subscribes to ``/control/trajectory``, the trajectory
   gate output that Autoware's own follower reads in ``autoware_launch``
   main. Published Autoware images up to ``universe-humble-20261102`` do not
   contain the trajectory gate yet: their follower reads
   ``/planning/trajectory`` and nothing publishes ``/control/trajectory``.
   With such an image, or with Autoware that still publishes
   ``/planning/scenario_planning/trajectory``, add a remap to
   ``control/trajectory`` in your bridge (:doc:`../design/topics`).
