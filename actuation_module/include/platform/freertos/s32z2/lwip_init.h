// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__S32Z2__LWIP_INIT_H_
#define PLATFORM__FREERTOS__S32Z2__LWIP_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

// Initialise lwIP, bring up the NETC interface, request DHCP, and block until
// a lease arrives (with a timeout). Returns 0 on success, non-zero otherwise.
int lwip_bring_up_blocking(void);

#ifdef __cplusplus
}
#endif

#endif  // PLATFORM__FREERTOS__S32Z2__LWIP_INIT_H_
