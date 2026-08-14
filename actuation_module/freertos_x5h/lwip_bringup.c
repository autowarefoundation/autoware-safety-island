// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Brings up lwIP on the RPMsg-backed Ethernet netif for the R-Car X5H Core1
// actuation module. Modeled on freertos_s32z2/lwip_bringup.c's tcpip_init +
// semaphore-wait pattern verbatim; that file's NETC controller bring-up
// block is replaced here with rpmsg_transport_init() (Task 7's OpenAMP
// endpoint; Task 6 ships a stub that always returns -1 -- see
// rpmsg_transport.c). Called from configure_network() in
// include/platform/freertos/x5h/freertos_network.h.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"

#include "platform/freertos/x5h/lwip_init.h"
#include "rpmsg_netif.h"
#include "rpmsg_transport.h"

// Static IP (DHCP disabled). Overridable at build time via -D, exactly as
// on S32Z2. CR52 side of the RPMsg-backed point-to-point /24 link to Linux:
// we are .2, Linux is .1 (also the DDS peer on the same /24, so the gateway
// needs no router). Must match CONFIG_DDS_NETWORK_INTERFACE so the DDS
// layer can bind its interface by this IP.
#ifndef LWIP_STATIC_IP
#define LWIP_STATIC_IP       "172.16.52.2"
#endif
#ifndef LWIP_STATIC_NETMASK
#define LWIP_STATIC_NETMASK  "255.255.255.0"
#endif
#ifndef LWIP_STATIC_GW
#define LWIP_STATIC_GW       "172.16.52.1"
#endif

static struct netif s_netif;

static void tcpip_init_done(void *arg) {
    SemaphoreHandle_t *done = (SemaphoreHandle_t *)arg;
    xSemaphoreGive(*done);
}

int lwip_bring_up_blocking(void) {
    SemaphoreHandle_t tcpip_done = xSemaphoreCreateBinary();
    if (tcpip_done == NULL) {
        return -1;
    }

    tcpip_init(tcpip_init_done, &tcpip_done);
    if (xSemaphoreTake(tcpip_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        printf("lwip: tcpip_init timed out\n");
        // Don't delete tcpip_done on this path: the tcpip thread's init-done
        // callback may still hold and give it. This path is fatal anyway.
        return -2;
    }
    // The init-done callback has fired (the take succeeded), so it is safe to
    // release the one-shot semaphore.
    vSemaphoreDelete(tcpip_done);

    // Bring up the RPMsg transport BEFORE netif_add(): rpmsg_netif_init()
    // (below, via netif_add) wires the netif's linkoutput straight to
    // rpmsg_transport_send(), so the endpoint needs to already exist. This
    // ordering also means an inbound frame can arrive before s_netif is
    // installed; rpmsg_netif.c's glue_rx_deliver() guards against that
    // window (see its own comment) rather than relying on ordering alone.
    printf("lwip: initialising RPMsg transport...\n");
    if (rpmsg_transport_init() != 0) {
        printf("lwip: rpmsg_transport_init failed\n");
        return -5;
    }

    ip4_addr_t ipaddr, netmask, gw;
    // ip4addr_aton returns 0 on a malformed string; without the check an
    // invalid LWIP_STATIC_* build override would silently configure 0.0.0.0.
    if (!ip4addr_aton(LWIP_STATIC_IP, &ipaddr) ||
        !ip4addr_aton(LWIP_STATIC_NETMASK, &netmask) ||
        !ip4addr_aton(LWIP_STATIC_GW, &gw)) {
        printf("lwip: invalid static IP configuration\n");
        return -4;
    }
    // netif_add()/netif_set_default()/netif_set_up()/etharp_gratuitous() all
    // mutate netif_list and per-netif state that lwIP's own internals (ARP
    // timer, tcpip_thread()'s netif->input dispatch, any socket/netconn call
    // reaching in via tcpip_api_call()) touch only while holding
    // LOCK_TCPIP_CORE(). LWIP_TCPIP_CORE_LOCKING defaults to 1 in
    // lwip/src/include/lwip/opt.h and is not overridden by this project's
    // lwipopts.h, so every one of those other paths already runs locked:
    // tcpip_thread() (lwip/src/api/tcpip.c) holds the lock for its whole
    // main loop except while blocked in tcpip_mbox_fetch(), and
    // tcpip_api_call() locks/calls/unlocks synchronously in the caller's own
    // thread. This bring-up thread is the one caller that was touching the
    // same state unlocked -- inherited verbatim from the S32Z2 bring-up
    // pattern this file is modeled on -- so we take the lock here
    // explicitly. tcpip_init() (called above) creates lock_tcpip_core
    // before starting tcpip_thread, and we only reach this point after that
    // thread has already signalled tcpip_init_done, so the mutex is
    // guaranteed to exist by now.
    LOCK_TCPIP_CORE();
    struct netif *added = netif_add(&s_netif, &ipaddr, &netmask, &gw, NULL,
                                     rpmsg_netif_init, tcpip_input);
    char ip_str[16] = {0};
    if (added != NULL) {
        netif_set_default(&s_netif);
        netif_set_up(&s_netif);
        ip4addr_ntoa_r(netif_ip4_addr(&s_netif), ip_str, sizeof(ip_str));
        // Announce ourselves so peers learn our IP->MAC without having to solicit.
        etharp_gratuitous(&s_netif);
    }
    UNLOCK_TCPIP_CORE();

    if (added == NULL) {
        printf("lwip: netif_add failed\n");
        return -3;
    }
    printf("lwip: static IP %s\n", ip_str);
    return 0;
}
