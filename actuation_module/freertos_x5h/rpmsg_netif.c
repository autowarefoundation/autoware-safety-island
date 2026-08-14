// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP netif backed by an RPMsg endpoint: one Ethernet frame per message.
#include <string.h>

#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"

#include "rpmsg_netif.h"
#include "rpmsg_netif_core.h"
#include "rpmsg_transport.h"   /* Task 7: rpmsg_transport_send */

static struct netif *s_netif;
static rpmsg_netif_stats s_stats;

// Counts frames dropped because they arrived before netif_add() installed
// s_netif below (see the NULL check in glue_rx_deliver()). Deliberately not
// folded into rpmsg_netif_stats: that struct is Task 5's frozen contract
// (rpmsg_netif_core.h) and is not ours to extend.
static unsigned s_rx_drop_no_netif;

// Counts frames dropped because pbuf_alloc() returned NULL (PBUF_POOL
// exhausted). Same reasoning as s_rx_drop_no_netif above: this is glue-level
// bookkeeping, not part of Task 5's frozen rpmsg_netif_stats contract.
static unsigned s_rx_drop_no_pbuf;

// Outbound frame staging buffer. File-scope static, not a linkoutput()
// stack-local: RPMSG_ETH_MAX_FRAME is 476 bytes, and every task on this port
// (including the socket-send caller under LOCK_TCPIP_CORE(), see
// lwip_bringup.c) runs on a 1 KiB-class FreeRTOS stack. A 476-byte
// stack-local here previously stacked on top of the caller's own frame on
// the same path -- see the fix report in task-6-report.md for the
// objdump-measured worst case. linkoutput() is only ever called with
// LOCK_TCPIP_CORE() held (directly by tcpip_thread(), or synchronously by
// tcpip_api_call() from the caller's own thread -- see lwip_bringup.c's
// core-locking comment for the citations), so a single shared buffer here
// cannot be raced by two callers.
static unsigned char s_tx_frame[RPMSG_ETH_MAX_FRAME];

static int glue_tx(void *ctx, const void *frame, unsigned len) {
    (void)ctx;
    return rpmsg_transport_send(frame, len);
}

static void glue_rx_deliver(void *ctx, const void *frame, unsigned len) {
    (void)ctx;
    // lwip_bringup.c calls rpmsg_transport_init() BEFORE netif_add() (the
    // netif's linkoutput needs the transport to already exist), so a frame
    // can arrive over RPMsg before s_netif is installed. Without this guard
    // that window is a NULL netif dereference below; drop and count instead.
    if (s_netif == NULL) {
        s_rx_drop_no_netif++;
        return;
    }
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (p == NULL) {
        s_rx_drop_no_pbuf++;
        return;
    }
    if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
        // Cannot happen today (pbuf_alloc(PBUF_POOL) above guarantees at
        // least len bytes of payload), but pbuf_take's contract does allow
        // a mismatch return; do not hand a partially-filled pbuf to input().
        pbuf_free(p);
        return;
    }
    if (s_netif->input(p, s_netif) != ERR_OK) pbuf_free(p);
}

void rpmsg_netif_get_stats(rpmsg_netif_glue_stats *out) {
    out->core = s_stats;
    out->rx_drop_no_netif = s_rx_drop_no_netif;
    out->rx_drop_no_pbuf = s_rx_drop_no_pbuf;
}

static const rpmsg_netif_ops s_ops = { glue_tx, glue_rx_deliver, 0 };

// Called directly by the transport's rx path (Task 7's rpmsg endpoint
// callback), once per inbound message. Task-context only: pbuf_alloc/
// pbuf_take/pbuf_free and lwIP's netif->input all go through
// SYS_ARCH_PROTECT (taskENTER_CRITICAL on this port -- see lwip_port/
// sys_arch.c's own comment), and common/ARM_CR52/port.c asserts if that is
// ever entered from an ISR. This is safe as long as the caller follows the
// pattern the vendor BSP's own rpmsg sample already establishes
// (rcar_bsp/.../sample_apps/rpmsg_sample/rpmsg-echo.c polls the endpoint
// from inside a plain FreeRTOS task's loop, never a hardware interrupt
// handler) -- Task 7's transport must do the same, not call this from a
// genuine ISR.
void rpmsg_netif_rx(const void *msg, unsigned len) {
    rpmsg_netif_core_rx(&s_ops, &s_stats, msg, len);
}

static err_t rpmsg_netif_linkoutput(struct netif *nif, struct pbuf *p) {
    (void)nif;
    // Cap the copy at the buffer size, but still pass the pbuf's real
    // tot_len to rpmsg_netif_core_tx() below: that is what lets the core's
    // own oversize check (rpmsg_netif_core.c: `len > RPMSG_ETH_MAX_FRAME`)
    // fire and count tx_drop_oversize correctly instead of us silently
    // returning ERR_MEM here and bypassing that counter. The core checks
    // len before it ever reads frame, so a truncated copy on the oversize
    // path is never read.
    u16_t copy_len = (p->tot_len <= sizeof(s_tx_frame)) ? p->tot_len : (u16_t)sizeof(s_tx_frame);
    pbuf_copy_partial(p, s_tx_frame, copy_len, 0);
    switch (rpmsg_netif_core_tx(&s_ops, &s_stats, s_tx_frame, p->tot_len)) {
        case 0:  return ERR_OK;
        case -1: return ERR_MEM;  /* oversize */
        default: return ERR_IF;   /* transport error */
    }
}

err_t rpmsg_netif_init(struct netif *nif) {
    s_netif = nif;
    nif->name[0] = 'r'; nif->name[1] = 'p';
    nif->mtu = RPMSG_ETH_MTU;
    nif->hwaddr_len = ETH_HWADDR_LEN;
    static const unsigned char mac[ETH_HWADDR_LEN] = {0x02, 0x5c, 0x52, 0x00, 0x00, 0x02};
    memcpy(nif->hwaddr, mac, ETH_HWADDR_LEN);
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_ETHERNET;
    nif->output = etharp_output;
    nif->linkoutput = rpmsg_netif_linkoutput;
    return ERR_OK;
}
