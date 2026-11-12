..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

..
  # Trailing whitespace on purpose
.. |cspell:disable-line| replace:: \

#########################
Changelog & Release Notes
#########################

Releases are listed newest-first. Unreleased work lives under
:ref:`Unreleased <unreleased>` until it is tagged.

.. note::

   Version numbering restarted at **v0.1.0** when the project was renamed
   and restructured as *Autoware Safety Island*. Legacy releases (v1.0,
   v2.0) from the previous project structure are preserved at the bottom of
   this page for historical reference.

.. _unreleased:

**********
Unreleased
**********

Added
-----

- CI replays the recorded follower inputs into the FreeRTOS POSIX controller,
  compares its steering with Autoware's own follower on the same bag, and
  checks that it latches ``SI_STOP`` when the inputs stop
  (``.github/scripts/run-freertos-posix-replay-ci.sh``).

Changed
-------

- **CAN status frame** ``0x102`` **(breaking for CAN receivers).** Byte 0 now
  carries the supervisor decision (NORMAL / SI_STOP / HOLD) next to the output
  mode, byte 6 the fault id, and byte 7 a CRC-8 over all three command
  payloads; the timestamp shrinks to 16 bits and flag ``0x10`` marks the
  layout. ``demo/can_carla_bridge/decoder.py`` rejects a command whose CRC
  does not match and status frames in the earlier layout. The CAN-FD frame
  ``0x103`` carries the same bytes. See ``documentation/design/can_output.rst``.
- The control period, the per-source arrival watchdogs and the re-enable
  window run on a monotonic clock, so a wall-clock step no longer stalls the
  control tick, hides a dead source or latches a spurious ``SI_STOP``.

Fixed
-----

- A subscription whose DDS reader could not be created is now a startup
  failure. It used to be reported as created, and the Safety Island waited in
  ``HOLD`` for samples that could not arrive.
- ``Node`` no longer cancels and joins a thread that was never started.
- The MPC's "trajectory shape is changed" line is throttled.
- ``build.sh`` no longer leaves an empty component in ``LD_LIBRARY_PATH``.

Removed
-------

- Code that was no longer built or called: ``input_staleness_gate.hpp`` and its
  host test, ``autoware_osqp_interface``, ``qp_solver_osqp.cpp``, and the
  unused ``timeout_thr_sec`` parameter with its ``isTimeOut`` helper.

******
v0.3.0
******

Targets the Autoware 2.0.0 release, as Open AD Kit 2.0 does.

Changed
-------

- The Safety Island reads the Autoware trajectory from ``/planning/trajectory``,
  the trajectory Autoware 2.0's own follower reads, instead of
  ``/control/trajectory``, which only Autoware's ``main`` publishes (after its
  trajectory gate). ``bridge-config.yaml`` bridges it 1:1. An integration on
  Autoware ``main`` needs a remap in its own bridge; see
  ``documentation/design/topics.rst``.
- The demo runs Autoware ``universe-humble-2.0.0``.

Removed
-------

- ``demo/bridge/bridge-overlay.yaml`` and its merge in the bridge entrypoint:
  Autoware 2.0.0 publishes the topic the contract bridges.

******
v0.2.0
******

Prepares the FreeRTOS POSIX release binary and the bridge contract for
pinning by Open AD Kit (:doc:`user_guide/open_ad_kit`).

Added
-----

- Release asset ``bridge-config.yaml`` (the domain-bridge topic contract,
  ``demo/bridge/bridge-config.yaml``), listed in ``sha256sums.txt``. CI and
  the release workflow check it against ``documentation/design/topics.rst``.
- Release asset ``cyclonedds-domain2.xml``: ``MaxMessageSize`` 1400 B on
  domain 2, which every domain-2 writer must load through ``CYCLONEDDS_URI``.
  Without it, the Safety Island truncates an unfragmented ``Trajectory`` at
  2048 bytes, drops it, and latches ``SI_STOP`` on the trajectory timeout. The demo's ``cyclonedds*.xml`` take
  the value from this file, and CI checks the file against the Safety
  Island's receive limit.
- FreeRTOS POSIX: ``SI_DDS_INTERFACE`` overrides the compile-time
  ``CONFIG_DDS_NETWORK_INTERFACE`` at runtime (name, IPv4 address or
  ``auto``), so one binary runs on loopback and on a LAN. The startup log
  names the interface and its source.
- Release workflow runs the published ``actuation_freertos`` in plain
  ``ubuntu:22.04`` and ``ubuntu:24.04`` containers before publishing.
- Release workflow publishes a SemVer pre-release tag (``v0.2.0-rc.1``) as a
  GitHub pre-release, so it does not become the latest release. Tags still
  have to be on ``main``.
- Release workflow builds with a pinned devcontainer and no caches, runs the
  Zephyr FVP validation, requires a changelog section for the tag, and attests
  the build provenance of its assets.
- Open AD Kit integration guide: release pins, ``SI_DDS_INTERFACE`` and
  ``trajectory_follower_mode: none``.
- Closed-loop CAN/CARLA path: Safety Island ``CAN_ONLY`` and host CAN-CARLA
  bridge. Open AD Kit does not ship its side of this path: Open AD Kit PR #146
  was closed without merging.
- Zephyr FVP TAP UDP CAN tunnel: one 48-byte datagram per command onto the
  same ``vcan0`` decoder as FreeRTOS POSIX. Native FVP CAN stays loopback.

Changed
-------

- The Safety Island reads the Autoware trajectory from ``/control/trajectory``
  (the trajectory gate output that Autoware's own follower reads) instead of
  ``/planning/scenario_planning/trajectory``. ``bridge-config.yaml`` bridges
  it 1:1, and the contract check rejects any ``remap``. Autoware without the
  trajectory gate needs a remap in its own bridge; see
  ``documentation/design/topics.rst``.
- The demo runs Autoware ``universe-humble-20261029`` and disables
  Autoware's follower with the ``safety_island`` control module preset
  (``trajectory_follower_mode: none``). The stub
  ``demo/launch/control.launch.xml`` is removed. Until the demo image
  publishes ``/control/trajectory``, the demo bridge merges
  ``demo/bridge/bridge-overlay.yaml`` into the contract to remap
  ``/planning/trajectory`` to ``/control/trajectory``.
- Autoware with ``trajectory_follower_mode: none`` must remove
  ``/autoware/control/performance_monitoring/control_state`` from its
  diagnostic graph, or ``change_to_autonomous`` fails.
  ``demo/launch/autoware-safety-island.yaml`` is the e2e-style graph for
  ``diagnostic_graph_aggregator_graph_path``. Until
  ``planning_simulator.launch.xml`` accepts that argument (autoware_launch
  PR pending), the demo mounts a CI-checked copy of the image's
  ``autoware-main.yaml`` with the same edit.
- ``build.sh`` and the documentation now present the supported runtime target
  matrix: ``zephyr-fvp``, ``zephyr-s32z``, ``freertos-posix``, and
  ``freertos-s32z2``.
- Added a first-class ``freertos-posix`` user guide and aligned architecture,
  troubleshooting, and timing notes with the runtime target matrix.
- ``freertos-s32z2`` can be selected through ``build.sh --platform``; it remains
  a hardware-specific target that requires NXP-licensed SDK inputs.

Fixed
-----

- The v0.1.0 ``actuation_freertos`` exited on start with "DDS network
  interface not set" because it was built with an empty interface. An empty
  interface now means CycloneDDS auto-selects, and the POSIX build defaults
  to ``lo``.
- Wait for ``/system/operation_mode/state`` before the first control tick.
- Ingest the MPC trajectory once per control tick instead of also in
  ``isReady()``.
- Drop per-tick PID INFO logs that blocked the S32Z2 UART on the control
  thread.
- Align AVH and user-guide ELF paths with ``build.sh`` platform defaults
  (``build/<platform>/zephyr/zephyr.elf``).
- Key CycloneDDS CI/release caches on the submodule gitlink SHA.
- Correct documented Zephyr/CycloneDDS pins, public submodule init, and S32Z
  DDS/network runbooks.

******
v0.1.0
******

First release of *Autoware Safety Island*. Major restructuring from the
previous project: Autoware components are now vendored directly into the
application, the separate ``Actuation Service`` / ``Message Converter`` /
``Actuation Player`` split has been retired, and a platform abstraction
layer enables a FreeRTOS POSIX runtime alongside the Zephyr target.

New features
------------

- Direct integration of Autoware modules (MPC lateral, PID longitudinal,
  trajectory follower node) compiled as part of the application.
- ``fvp_baser_aemv8r_smp`` target for Arm Fixed Virtual Platform simulation.
- ``s32z270dc2_rtu0_r52@D`` target for NXP S32Z270 hardware.
- AVH deployment flow via ``avh.py``, using the ``aem8r64-lan9c111``
  instance flavor.
- Simplified top-level ``build.sh`` replacing the previous multi-step build.
- Platform abstraction layer (``actuation_module/include/platform/``) with
  Zephyr and FreeRTOS backends, keeping controller logic fully shared.
- FreeRTOS POSIX runtime build (``actuation_module/freertos/``) using
  FreeRTOS-Kernel V11.1.0, buildable on any Linux host.
- CI pipeline verifying both Zephyr (FVP) and FreeRTOS POSIX builds on
  every pull request and daily.
- Release workflow publishing ``zephyr-fvp.elf``, ``zephyr-s32z.elf``,
  ``actuation_freertos``, and ``sha256sums.txt`` on every ``v*.*.*`` tag.

Changed
-------

- Removed dependency on a separate Autoware workspace and pre-compiled
  binaries.
- Replaced the ROS 2-based *Message Converter* and *Actuation Player* with
  direct DDS communication and integrated control logic.
- ``network_config`` refactored from header-only to a proper declaration
  (``include/common/dds/network_config.hpp``) and Zephyr-specific source
  (``src/common/dds/network_config.cpp``).
- Devcontainer image published at
  ``ghcr.io/autowarefoundation/autoware-safety-island:devcontainer``.

Limitations
-----------

- A devicetree overlay at
  ``actuation_module/boards/s32z270dc2_rtu0_r52@D.overlay`` is used as a
  workaround to set a unique MAC address for the NXP S32Z270DC2_R52 board,
  which otherwise reuses the same MAC on every build
  (tracked in `Zephyr Project #61478
  <https://github.com/zephyrproject-rtos/zephyr/issues/61478>`_).
- FreeRTOS support is currently POSIX runtime only; real hardware port
  (Phases 5–6) is future work.

Third-party repositories
------------------------

.. code-block:: yaml
    :substitutions:

    name:   cyclonedds
    url:    https://github.com/eclipse-cyclonedds/cyclonedds.git
    |cspell:disable-line|branch: master
    commit: f7688ce709e53f408e30706ebc27bd052c03d693

    name:   zephyr
    url:    https://github.com/zephyrproject-rtos/zephyr.git
    branch: main
    commit: 339cd5a45fd2ebba064ef462b71c657336ca0dfe

    name:   freertos-kernel
    url:    https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    tag:    V11.1.0
    commit: dbf70559b27d39c1fdb68dfb9a32140b6a6777a0

.. _legacy:

********************************************
Legacy releases (previous project structure)
********************************************

The releases below predate the rename and restructuring to *Autoware Safety
Island*. They are preserved here for historical reference only and are not
upgrade-compatible with v0.1.0.

****
v2.0
****

New features
------------

- Lighter deployment mode that does not require the full Autoware pipeline on
  the main compute:

  - *Actuation Player* component that replays recorded messages as DDS
    traffic.
  - *Packet Analyzer* that validates actuation commands against a reference
    recording.

Changed
-------

- Simplified the user guide; new Dockerfile replaces the previous
  multi-step reproduce instructions. Board flashing moved from the IDE to the
  command line.
- Autoware updated to the 2023.10 release, moving the underlying ROS 2
  distribution from Galactic to Humble.
- Zephyr updated to 3.5.0 (with an additional patch for S32Z support beyond
  the release tag).
- CycloneDDS updated for compatibility with the new Zephyr version.
- Autoware pipeline (main compute) and Actuation Service (safety island) now
  use distinct ROS domain IDs.

Limitations
-----------

- A devicetree overlay at
  ``actuation_module/boards/s32z270dc2_rtu0_r52@D.overlay`` is used as a
  workaround to set a unique MAC address for the NXP S32Z270DC2_R52 board,
  which otherwise reuses the same MAC on every build
  (tracked in `Zephyr Project #61478
  <https://github.com/zephyrproject-rtos/zephyr/issues/61478>`_).
- The AVA Developer Platform and the S32Z need to share a sub-network.
- RViz2 has been seen to crash on machines with `NVIDIA Optimus
  <https://en.wikipedia.org/wiki/Nvidia_Optimus>`_ graphics
  (``libGL error: failed to create drawable``). Run the visualizer on a
  different machine when this occurs.

Resolved issues
---------------

- The S32Z no longer needs to be re-flashed between runs.
- Official Zephyr support for the S32 Debug Probe has landed, so the IDE
  workaround and manual register pokes are no longer required.

Third-party repositories
------------------------

.. code-block:: yaml
    :substitutions:

    name:   autoware
    url:    https://github.com/autowarefoundation/autoware.git
    branch: release/2023.10
    commit: 78e5f575b258598e6460e6f04cc00211e7e7e604

    name:   cyclonedds
    url:    https://github.com/eclipse-cyclonedds/cyclonedds.git
    |cspell:disable-line|branch: master
    commit: f7688ce709e53f408e30706ebc27bd052c03d693

    name:   zephyr
    url:    https://github.com/zephyrproject-rtos/zephyr.git
    branch: main
    commit: 339cd5a45fd2ebba064ef462b71c657336ca0dfe

****
v1.0
****

Initial release: Pure Pursuit controller as the Zephyr application, with
autoware.universe driving the main pipeline.

Limitations
-----------

- No official support for the NXP S32 Debug Probe (debugging the S32Z
  required workarounds).
- The AVA Developer Platform and the S32Z need to share a sub-network.

Known issues
------------

- The S32Z must be re-flashed before each run of the demo
  (tracked in `CycloneDDS #1682
  <https://github.com/eclipse-cyclonedds/cyclonedds/issues/1682>`_).

Third-party repositories
------------------------

.. code-block:: yaml
    :substitutions:

    name:   autoware
    url:    https://github.com/autowarefoundation/autoware.git
    branch: main
    commit: 3a9bbd0142b453563469b8a3a6d232e98a51280a

    name:   cyclonedds
    url:    https://github.com/eclipse-cyclonedds/cyclonedds.git
    |cspell:disable-line|branch: master
    commit: 87b31771ad4dda92afccc6ad1cb84cb7f752b66b

    name:   zephyr
    url:    https://github.com/zephyrproject-rtos/zephyr.git
    branch: main
    commit: 07c6af3b8c35c1e49186578ca61a25c76e2fb308
