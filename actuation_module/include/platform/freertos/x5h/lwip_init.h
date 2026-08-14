// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__X5H__LWIP_INIT_H_
#define PLATFORM__FREERTOS__X5H__LWIP_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

// Initialise lwIP (tcpip_init), bring up the RPMsg netif, and configure a
// static IP, blocking until the TCP/IP stack is ready. Returns 0 on success,
// non-zero otherwise.
//
// Implemented in freertos_x5h/lwip_bringup.c (Task 6), against the RPMsg
// netif glue in freertos_x5h/rpmsg_netif.{h,c}. The RPMsg transport itself
// (freertos_x5h/rpmsg_transport.{h,c}) is a Task 6 stub that always fails
// until Task 7 replaces it with the real OpenAMP endpoint.
int lwip_bring_up_blocking(void);

#ifdef __cplusplus
}
#endif

#endif  // PLATFORM__FREERTOS__X5H__LWIP_INIT_H_
