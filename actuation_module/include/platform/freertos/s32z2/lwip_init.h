// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__S32Z2__LWIP_INIT_H_
#define PLATFORM__FREERTOS__S32Z2__LWIP_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

// Initialise lwIP (tcpip_init), bring up the NETC interface, and configure a
// static IP, blocking until the TCP/IP stack is ready. Returns 0 on success,
// non-zero otherwise.
int lwip_bring_up_blocking(void);

#ifdef __cplusplus
}
#endif

#endif  // PLATFORM__FREERTOS__S32Z2__LWIP_INIT_H_
