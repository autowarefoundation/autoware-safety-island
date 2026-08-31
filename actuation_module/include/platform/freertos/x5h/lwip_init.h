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
// Task 3 scaffold note: the RPMsg netif does not exist yet (it lands in
// Task 6). freertos_main.cpp provides a temporary weak stub returning 0 so
// this scaffold links; Task 6 replaces it with a real implementation.
int lwip_bring_up_blocking(void);

#ifdef __cplusplus
}
#endif

#endif  // PLATFORM__FREERTOS__X5H__LWIP_INIT_H_
