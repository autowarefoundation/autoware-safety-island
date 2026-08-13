// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP compiler/platform abstraction header for FreeRTOS on the R-Car X5H
// (Cortex-R52, arm-none-eabi/newlib via fetch-toolchain.sh).
//
// lwip/arch.h (in the lwip/ submodule) unconditionally does
// `#include "arch/cc.h"` before defining anything else, then guards every
// one of its own definitions with `#ifndef` so a port only has to override
// what does not already have a safe default. Almost everything already has
// a GCC-safe default there (BYTE_ORDER, LWIP_PLATFORM_DIAG/ASSERT,
// PACK_STRUCT_*, stdint/stddef/inttypes usage). This file supplies the small
// handful of things that do NOT have a safe default for us:
//   - sys_prot_t: the return type of sys_arch_protect()/argument of
//     sys_arch_unprotect() (see sys_arch.h/.c). Not defined by arch.h at
//     all -- every port must supply it.
//   - LWIP_ERRNO_STDINCLUDE: newlib's <errno.h> already declares `errno`
//     and the POSIX E* codes lwIP's sockets layer expects, so let lwIP
//     #include it instead of declaring its own errno/E* set (which would
//     collide with newlib's when both sockets.c and any newlib-using TU
//     see conflicting definitions in the same link).
#ifndef PLATFORM_FREERTOS_X5H_LWIP_ARCH_CC_H_
#define PLATFORM_FREERTOS_X5H_LWIP_ARCH_CC_H_

#include <stdint.h>

// sys_arch_protect()/sys_arch_unprotect() (lwip_port/sys_arch.c) implement
// lwIP's critical-section nesting via taskENTER_CRITICAL()/
// taskEXIT_CRITICAL(), which do not themselves return or consume a token.
// A plain int is enough to round-trip protect()'s return value to the
// matching unprotect() call; lwIP never inspects its contents.
typedef int sys_prot_t;

// Use newlib's <errno.h>/errno instead of lwIP's own built-in E* set.
#define LWIP_ERRNO_STDINCLUDE 1

#endif  // PLATFORM_FREERTOS_X5H_LWIP_ARCH_CC_H_
