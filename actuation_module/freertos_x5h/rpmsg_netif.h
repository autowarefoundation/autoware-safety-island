// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Public interface of the lwIP-facing RPMsg netif glue (rpmsg_netif.c).
#ifndef RPMSG_NETIF_H
#define RPMSG_NETIF_H

#include "lwip/err.h"

struct netif;

#ifdef __cplusplus
extern "C" {
#endif

// netif_add()'s init callback for the RPMsg-backed Ethernet netif: wires up
// the MAC, MTU, flags, and output functions. Always returns ERR_OK.
err_t rpmsg_netif_init(struct netif *nif);

// Delivers one inbound Ethernet frame received over RPMsg into lwIP. Called
// directly by the transport's rx path (Task 7's rpmsg endpoint callback) --
// see rpmsg_netif.c's own comment on this function for why task context is
// required and ISR context is not safe here.
void rpmsg_netif_rx(const void *msg, unsigned len);

#ifdef __cplusplus
}
#endif

#endif  // RPMSG_NETIF_H
