// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 5: rpmsg_netif_core implementation. See rpmsg_netif_core.h for the
// contract this delivers.
#include "rpmsg_netif_core.h"

int rpmsg_netif_core_tx(const rpmsg_netif_ops *ops, rpmsg_netif_stats *st, const void *frame, unsigned len) {
    if (len > RPMSG_ETH_MAX_FRAME) {
        st->tx_drop_oversize++;
        return -1;
    }
    if (ops->tx(ops->ctx, frame, len) != 0) {
        st->tx_err++;
        return -2;
    }
    st->tx_ok++;
    return 0;
}

int rpmsg_netif_core_rx(const rpmsg_netif_ops *ops, rpmsg_netif_stats *st, const void *msg, unsigned len) {
    if (len > RPMSG_ETH_MAX_FRAME) {
        st->rx_drop_oversize++;
        return -1;
    }
    ops->rx_deliver(ops->ctx, msg, len);
    st->rx_ok++;
    return 0;
}
