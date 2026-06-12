// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Brings up lwIP on the NXP S32Z2 NETC Ethernet controller using a static IP.
// Called from configure_network() in
// include/platform/freertos/s32z2/freertos_network.h.

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

#include "platform/freertos/s32z2/lwip_init.h"

// NETC controller bring-up. The NXP RTD lwIP port (eth_port.c::
// ethif_low_level_init) only calls Eth_ProvideRxBuffer + Eth_SetControllerMode
// (ACTIVE); it ASSUMES the application has already run Eth_43_NETC_Init to set
// up the Station Interface, RX/TX BD rings, and MAC — exactly what NXP's
// device.c::device_init() does before bringing up lwIP. Our board_init() never
// did this, so the RX ring stayed empty and the board received zero frames.
// We invoke the minimal init subset here (poll mode needs no Platform/MRU IRQ
// plumbing): OsIf for the driver's timeout loops, the integrated switch, then
// the controller. PRECOMPILE_SUPPORT=STD_ON so each takes NULL_PTR.
#include "OsIf.h"
#include "EthSwt_43_NETC.h"
#include "Eth_43_NETC.h"

// Static IP (DHCP disabled). Overridable at build time via -D. Board is
// .105/24; the gateway points at the bench host (.101), which is the DDS peer on
// the same /24 — same-subnet traffic to it needs no router, this just gives a
// sane default route. Must match CONFIG_DDS_NETWORK_INTERFACE so the DDS layer
// can bind its interface by this IP.
#ifndef LWIP_STATIC_IP
#define LWIP_STATIC_IP       "192.168.0.105"
#endif
#ifndef LWIP_STATIC_NETMASK
#define LWIP_STATIC_NETMASK  "255.255.255.0"
#endif
#ifndef LWIP_STATIC_GW
#define LWIP_STATIC_GW       "192.168.0.101"
#endif

// NXP-provided NETC <-> lwIP glue from
// $LWIP_PATH/code/ports/netif/ethif/rtd/generic/eth_port.c.
extern err_t ethif_ethernetif_init(struct netif *netif);

static struct netif s_netif;

static void tcpip_init_done(void *arg) {
    SemaphoreHandle_t *done = (SemaphoreHandle_t *)arg;
    xSemaphoreGive(*done);
}

#define NETC_REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

// Enable the NETC ETH0 RX RGMII clock (MC_CGM_1 MUX_7, source ETH0_EXT_RX_CLK =
// the PHY's RXC). Clock_Ip_Init(config0) selected the source but left the mux
// switch incomplete (SELSTAT stuck at 0) and the divider disabled, because the
// PHY's RXC was not toggling that early in boot. Re-trigger the switch (keep
// SELCTL, set CLK_SW bit2; poll SWIP clear) and enable the divider
// (DE=1, DIV=0 => /1). Without it the MAC RX state machine has no clock and
// receives 0 frames while TX (SoC-sourced TXC) works.
//
// The switch only takes while the PHY's RXC is actually toggling, i.e. after
// the PHY has link. When this firmware is loaded over a previous live session
// the link never dropped and one trigger suffices; from a cold/hung/reset
// state the PHY is still autonegotiating (~2-3 s) when bring-up reaches this
// point, the trigger silently fails (SELSTAT stays 0) and RX stays clockless
// forever — the recurring "board boots Live but is mute on the wire" RX death.
// So verify SELSTAT==SELCTL in CSS and retry with a delay until the switch
// actually takes (up to ~10 s to cover autoneg from a cold start).
#define MC_CGM_1_MUX_7_CSC 0x408304C0U
#define MC_CGM_1_MUX_7_CSS 0x408304C4U
#define MC_CGM_1_MUX_7_DC0 0x408304C8U
static void s32z2_enable_eth0_rx_clock(void) {
    uint32_t css = 0U;
    for (int attempt = 0; attempt < 100; ++attempt) {
        uint32_t csc = NETC_REG32(MC_CGM_1_MUX_7_CSC);
        NETC_REG32(MC_CGM_1_MUX_7_CSC) = (csc & 0x3F000000U) | 0x4U;  /* keep SELCTL, set CLK_SW */
        for (int t = 0; t < 200000; ++t) {        /* wait for SWIP (CSS bit16) to clear */
            if ((NETC_REG32(MC_CGM_1_MUX_7_CSS) & 0x10000U) == 0U) break;
        }
        css = NETC_REG32(MC_CGM_1_MUX_7_CSS);
        if (((css >> 24) & 0x3FU) == ((csc >> 24) & 0x3FU)) break;  /* SELSTAT == SELCTL */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("lwip: eth0 RX clock mux CSS=0x%08lx (%s)\n", (unsigned long)css,
           (((css >> 24) & 0x3FU) != 0U) ? "switched" : "FAILED - RX will be dead");
    NETC_REG32(MC_CGM_1_MUX_7_DC0) = 0x80000000U; /* DE=1, DIV=0 (/1) */
    for (volatile int i = 0; i < 20000; ++i) { }
}

// Bench note (2026-06-12): do NOT read the port MAC RMON counters from this
// firmware. Both Eth_43_NETC_GetRxStats(0,..) and EthSwt_43_NETC_GetRxStats(0,
// port,..) DATA-ABORT on this part — the counter register space is the same
// AXI region the debugger cannot read (project_s32z2_netc_debugger_unreachable)
// — and the abort handler is a spin, so the board dies ~3 s after "Live".
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

    // Initialise the NETC controller BEFORE netif_add() (netif_add ->
    // ethif_ethernetif_init -> ethif_low_level_init, which only sets the
    // controller ACTIVE and assumes the BD rings/SI/MAC are already configured).
    // Runs in this task's thread context so the driver's OsIf timeout loops have
    // a live tick. Eth_T_EnableIRQs() is deliberately omitted: poll mode services
    // RX from a thread, avoiding the RX-ISR FPU-corruption and GIC/MRU walls.
    printf("lwip: initialising NETC controller...\n");
    OsIf_Init(NULL_PTR);
    EthSwt_43_NETC_Init(NULL_PTR);
    Eth_43_NETC_Init(NULL_PTR);
    s32z2_enable_eth0_rx_clock();

    ip4_addr_t ipaddr, netmask, gw;
    // ip4addr_aton returns 0 on a malformed string; without the check an
    // invalid LWIP_STATIC_* build override would silently configure 0.0.0.0.
    if (!ip4addr_aton(LWIP_STATIC_IP, &ipaddr) ||
        !ip4addr_aton(LWIP_STATIC_NETMASK, &netmask) ||
        !ip4addr_aton(LWIP_STATIC_GW, &gw)) {
        printf("lwip: invalid static IP configuration\n");
        return -4;
    }
    if (netif_add(&s_netif, &ipaddr, &netmask, &gw, NULL,
                  ethif_ethernetif_init, tcpip_input) == NULL) {
        printf("lwip: netif_add failed\n");
        return -3;
    }
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);

    char ip_str[16];
    ip4addr_ntoa_r(netif_ip4_addr(&s_netif), ip_str, sizeof(ip_str));
    printf("lwip: static IP %s\n", ip_str);
    // Announce ourselves so peers learn our IP->MAC without having to solicit.
    etharp_gratuitous(&s_netif);
    return 0;
}
