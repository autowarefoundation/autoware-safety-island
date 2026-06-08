// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Minimal lwipcfg.h stub for the NXP TCP/IP Stack glue
// (arch/cc.h includes this unconditionally at the end of the file). The
// upstream NXP-provided lwipcfg.h is a Tresos/EB template that must be
// generated from a .mex board configuration; for now we keep this stub
// empty so the cross-build of CycloneDDS reaches its actual link stage.
// Concrete configuration is provided via lwipopts.h alongside this file.

#ifndef PLATFORM__FREERTOS__S32Z2__LWIPCFG_H_
#define PLATFORM__FREERTOS__S32Z2__LWIPCFG_H_

#endif  // PLATFORM__FREERTOS__S32Z2__LWIPCFG_H_
