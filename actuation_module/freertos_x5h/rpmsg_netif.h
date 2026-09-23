// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Public interface of the lwIP-facing RPMsg netif glue (rpmsg_netif.c).
#ifndef RPMSG_NETIF_H
#define RPMSG_NETIF_H

#include "lwip/err.h"
#include "rpmsg_netif_core.h"

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

// Read-only snapshot of this glue's counters: Task 5's frozen core stats
// (rpmsg_netif_core.h's rpmsg_netif_stats -- tx_ok/tx_drop_oversize/tx_err/
// rx_ok/rx_drop_oversize) plus three glue-level rx drop counters the core
// cannot see on its own. The core's rx_ok counts a frame as soon as it is
// handed to rx_deliver -- that means "accepted for delivery", not
// "delivered to lwIP". rx_drop_no_netif, rx_drop_no_pbuf, and
// rx_drop_input_err below are what tell the difference; see
// glue_rx_deliver() in rpmsg_netif.c.
typedef struct {
    rpmsg_netif_stats core;
    unsigned rx_drop_no_netif;   /* frame arrived before netif_add() installed the netif */
    unsigned rx_drop_no_pbuf;    /* pbuf_alloc() failed (PBUF_POOL_SIZE exhausted) */
    unsigned rx_drop_input_err;  /* review finding, Important: s_netif->input()
                                  * rejected the pbuf (e.g. the tcpip mailbox --
                                  * TCPIP_MBOX_SIZE in lwipopts.h -- is full).
                                  * Previously silently freed with no counter at
                                  * all; an operator on a slow serial console had
                                  * no way to see this class of drop. */
} rpmsg_netif_glue_stats;

void rpmsg_netif_get_stats(rpmsg_netif_glue_stats *out);

#ifdef __cplusplus
}
#endif

#endif  // RPMSG_NETIF_H
