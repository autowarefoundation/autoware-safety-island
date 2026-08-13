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
    if (p == NULL) return;
    pbuf_take(p, frame, (u16_t)len);
    if (s_netif->input(p, s_netif) != ERR_OK) pbuf_free(p);
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
    unsigned char frame[RPMSG_ETH_MAX_FRAME];
    if (p->tot_len > sizeof frame) return ERR_MEM;
    pbuf_copy_partial(p, frame, p->tot_len, 0);
    return rpmsg_netif_core_tx(&s_ops, &s_stats, frame, p->tot_len) == 0 ? ERR_OK : ERR_IF;
}

err_t rpmsg_netif_init(struct netif *nif) {
    s_netif = nif;
    nif->name[0] = 'r'; nif->name[1] = 'p';
    nif->mtu = RPMSG_ETH_MTU;
    nif->hwaddr_len = 6;
    static const unsigned char mac[6] = {0x02, 0x5c, 0x52, 0x00, 0x00, 0x02};
    memcpy(nif->hwaddr, mac, 6);
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    nif->output = etharp_output;
    nif->linkoutput = rpmsg_netif_linkoutput;
    return ERR_OK;
}
