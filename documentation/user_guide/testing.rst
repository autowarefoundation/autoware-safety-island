..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

########
Testing
########

The repository ships standalone test programs that share the same build system
as the main application. Each one replaces the controller with a small driver
that exercises one subsystem in isolation.

Pass at most one of the flags below; if more than one is passed, only the
last takes effect.

``zephyr-fvp`` and ``zephyr-s32z`` support all test flags. ``freertos-posix``
supports unit, DDS publisher, DDS subscriber, and CAN output tests.
``freertos-s32z2`` does not support test build flags.

**********************************
Unit test (node + DDS round-trip)
**********************************

.. code-block:: console

  $ ./build.sh --unit-test

Builds ``actuation_module/test/unit_test.cpp``. The test creates a DDS
publisher and subscriber on the same topic, drives a node timer, and checks
that messages arrive and callbacks fire as expected. Useful as a smoke test
after changes to ``include/common/node/`` or ``include/common/dds/``.

**********************
DDS publisher only
**********************

.. code-block:: console

  $ ./build.sh --dds-publisher

Builds ``actuation_module/test/dds_pub.cpp``. The firmware creates a DDS
writer and publishes sample messages in a loop. Pair with a DDS subscriber
on another machine (for example ``ddsperf``) to validate discovery and
network path.

**********************
DDS subscriber only
**********************

.. code-block:: console

  $ ./build.sh --dds-subscriber

Builds ``actuation_module/test/dds_sub.cpp``. The firmware creates a DDS
reader and logs every message it receives. Pair with an external publisher
to validate the inbound path.

**********
CAN output
**********

.. code-block:: console

  $ ./build.sh --can-output-test

Builds ``actuation_module/test/can_output_test.cpp``. The test validates the
control-command to CAN-frame encoder and, on ``freertos-posix``, the mock CAN
backend used by CI.

************
DDS loopback
************

.. code-block:: console

  $ ./build.sh --dds-loopback-test

Builds ``actuation_module/test/dds_loopback_test.cpp`` for Zephyr targets.

**********************
Running tests
**********************

Zephyr test binaries are produced at ``build/actuation_module/zephyr/zephyr.elf``
and are deployed in exactly the same way as the main application:

- AVH: upload via ``./avh.py`` — see :doc:`avh`.
- S32Z: flash via ``west flash`` — see :doc:`s32z_board`.
- Local FVP: boot the ELF with a locally installed Arm FVP simulator.

``freertos-posix`` test binaries are produced as
``build/freertos-posix/actuation_freertos`` unless ``-d`` selects another build
directory. Run them directly on the host, for example:

.. code-block:: console

  $ ./build.sh --platform freertos-posix --can-output-test
  $ ./build/freertos-posix/actuation_freertos

Clean between builds if switching flags:

.. code-block:: console

  $ ./build.sh -c
