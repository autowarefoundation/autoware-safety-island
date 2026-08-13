// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 5: transport-agnostic framing policy for the RPMsg-backed lwIP netif.
// No lwIP, no OS deps -- deliberately OS-free C so it can be unit-tested on
// the host. Task 6's lwIP-facing glue calls into this; Task 7's OpenAMP
// transport sits underneath it via the ops function pointers.
#ifndef RPMSG_NETIF_CORE_H
#define RPMSG_NETIF_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define RPMSG_ETH_SERVICE   "rpmsg-eth"
#define RPMSG_ETH_MTU       462
#define RPMSG_ETH_MAX_FRAME (RPMSG_ETH_MTU + 14)   /* + Ethernet header */

typedef struct {
    int (*tx)(void *ctx, const void *frame, unsigned len); /* 0 on success */
    void (*rx_deliver)(void *ctx, const void *frame, unsigned len);
    void *ctx;
} rpmsg_netif_ops;

typedef struct { unsigned tx_ok, tx_drop_oversize, tx_err, rx_ok, rx_drop_oversize; } rpmsg_netif_stats;

/* Validates and forwards one outbound frame. Returns 0, -1 (oversize), -2 (tx error). */
int rpmsg_netif_core_tx(const rpmsg_netif_ops *ops, rpmsg_netif_stats *st, const void *frame, unsigned len);
/* Validates and delivers one inbound message. Returns 0 or -1 (oversize, dropped). */
int rpmsg_netif_core_rx(const rpmsg_netif_ops *ops, rpmsg_netif_stats *st, const void *msg, unsigned len);

#ifdef __cplusplus
}
#endif

#endif /* RPMSG_NETIF_CORE_H */
