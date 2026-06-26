..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##############
FreeRTOS S32Z2
##############

This guide covers the ``freertos-s32z2`` runtime target: FreeRTOS on the NXP
S32Z2 Cortex-R52 hardware path. This target is hardware-specific and requires
NXP-licensed SDK packages and S32 Config Tools generated output.

The detailed bring-up notes live in ``actuation_module/freertos_s32z2/README.md``
and :doc:`/design/freertos-s32z2-bringup`.

.. note::

   ``freertos-s32z2`` is not a local validation target. Validate FreeRTOS DDS
   and controller behavior locally with :doc:`freertos_posix`, then treat
   ``freertos-s32z2`` as a bench-only hardware build that needs S32Z2-specific
   validation.

*************
Prerequisites
*************

Export the NXP package roots and the generated S32 Config Tools project before
building:

.. code-block:: console

  $ export S32_RTD_PATH=/path/to/RTD_S32ZE_2.0.1/_jar_family/S32DS/software/PlatformSDK_S32ZE
  $ export FREERTOS_PATH=/path/to/FreeRTOS_S32ZE_4.0.0/_jar/S32DS/software/PlatformSDK_S32ZE/FreeRTOS
  $ export LWIP_PATH=/path/to/TCPIP_S32ZE_3.0.0/_jar/S32DS/software/PlatformSDK_S32ZE/stacks/tcpip
  $ export S32CT_GENERATED_DIR=/path/to/lwip_S32Z27X_FreeRTOS_R52

If ``S32CT_GENERATED_DIR`` is not set, the build looks for the private
``actuation_module/freertos_s32z2/s32ct_config`` submodule.

*****
Build
*****

.. code-block:: console

  $ ./build.sh --platform freertos-s32z2 -d build/freertos-s32z2 \
      --dds-interface 192.168.0.105 \
      --control-output DDS_ONLY

``build.sh`` builds the host ``idlc``, cross-builds CycloneDDS for S32Z2, and
then builds the firmware.

Output:

.. code-block:: text

  build/freertos-s32z2/actuation_freertos_s32z2.elf

******
Status
******

``freertos-s32z2`` is not part of the local validation flow. Treat it as a
hardware target that requires bench validation after build changes. The
implementation depends on NXP-generated/private inputs and hardware bring-up
state that are not exercised by the public CI jobs.
