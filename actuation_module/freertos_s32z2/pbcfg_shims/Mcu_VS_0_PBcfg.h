// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Sentinel-only shim for Mcu_VS_0_PBcfg.h.
//
// S32 Configuration Tools' Update Code failed to regenerate this file
// for our workspace and left a 1-byte stub at
// $S32CT_GENERATED_DIR/generate/include/Mcu_VS_0_PBcfg.h. RTD's
// Mcu_Cfg.h #include's it and then compares MCU_CFG_* macros against
// MCU_VS_0_PBCFG_* macros expected to be defined here.
//
// This shim defines only the version metadata macros (vendor / AUTOSAR
// release / SW version). It carries no configuration data — runtime MCU
// init must still be performed elsewhere (e.g. inline calls from
// board_init.c). Once S32CT codegen is fixed this shim should be removed.

#ifndef MCU_VS_0_PBCFG_H
#define MCU_VS_0_PBCFG_H

#define MCU_VS_0_PBCFG_VENDOR_ID                       43
#define MCU_VS_0_PBCFG_AR_RELEASE_MAJOR_VERSION        4
#define MCU_VS_0_PBCFG_AR_RELEASE_MINOR_VERSION        7
#define MCU_VS_0_PBCFG_AR_RELEASE_REVISION_VERSION     0
#define MCU_VS_0_PBCFG_SW_MAJOR_VERSION                2
#define MCU_VS_0_PBCFG_SW_MINOR_VERSION                0
#define MCU_VS_0_PBCFG_SW_PATCH_VERSION                1

#endif  // MCU_VS_0_PBCFG_H
