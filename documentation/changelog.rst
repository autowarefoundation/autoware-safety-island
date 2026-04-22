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

.. _unreleased:

*************************
Unreleased (since v2.0)
*************************

Major refactor: Autoware components are now vendored directly into the Zephyr
application. The previous ``Actuation Service`` / ``Message Converter`` /
``Actuation Player`` split has been retired, along with the separate Autoware
workspace.

New features
============

- Direct integration of Autoware modules (MPC lateral, PID longitudinal,
  trajectory follower node) into the Zephyr application.
- ``fvp_baser_aemv8r_smp`` target for Arm Fixed Virtual Platform simulation.
- AVH deployment flow via ``avh.py``, using the
  ``aem8r64-lan9c111`` instance flavor.
- Simplified top-level ``build.sh`` replaces the previous multi-step build.
- Platform abstraction layer (``actuation_module/include/platform/``) with
  Zephyr and FreeRTOS backends, keeping controller logic fully shared.
- FreeRTOS POSIX simulator build (``actuation_module/freertos/``) using
  FreeRTOS-Kernel V11.1.0, buildable on any Linux host.
- CI pipeline (``build-ci.yml``) verifying both Zephyr (FVP) and FreeRTOS
  simulator builds on every pull request and daily.
- Release workflow (``release.yml``) publishing ``zephyr-fvp.elf``,
  ``zephyr-s32z.elf``, ``actuation_freertos``, and ``sha256sums.txt`` on
  every ``v*.*.*`` tag.

Changed
=======

- Removed the dependency on a separate Autoware workspace and pre-compiled
  binaries. Autoware components are now compiled as part of the Zephyr
  application.
- Replaced the ROS 2-based *Message Converter* and *Actuation Player* with
  direct DDS communication and integrated control logic.
- ``network_config`` refactored from a header-only implementation to a
  proper declaration (``include/common/dds/network_config.hpp``) and
  Zephyr-only source file (``src/common/dds/network_config.cpp``), guarded
  by a compile-time error if included in non-Zephyr builds.
- Devcontainer image moved to
  ``ghcr.io/autowarefoundation/autoware-safety-island:devcontainer``.

Third-party repositories
========================

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

****
v2.0
****

New features
============

- Lighter deployment mode that does not require the full Autoware pipeline on
  the main compute:

  - *Actuation Player* component that replays recorded messages as DDS
    traffic.
  - *Packet Analyzer* that validates actuation commands against a reference
    recording.

Changed
=======

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
===========

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
===============

- The S32Z no longer needs to be re-flashed between runs.
- Official Zephyr support for the S32 Debug Probe has landed, so the IDE
  workaround and manual register pokes are no longer required.

Third-party repositories
========================

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
===========

- No official support for the NXP S32 Debug Probe (debugging the S32Z
  required workarounds).
- The AVA Developer Platform and the S32Z need to share a sub-network.

Known issues
============

- The S32Z must be re-flashed before each run of the demo
  (tracked in `CycloneDDS #1682
  <https://github.com/eclipse-cyclonedds/cyclonedds/issues/1682>`_).

Third-party repositories
========================

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
