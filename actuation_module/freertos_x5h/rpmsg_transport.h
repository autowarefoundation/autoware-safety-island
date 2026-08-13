// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// RPMsg transport ops consumed by rpmsg_netif.c's lwIP glue. Task 6 codes
// against this header and ships a stub implementation (rpmsg_transport.c)
// that always fails, so this branch builds and the contract/budget checks
// stay green with no real transport underneath. Task 7 replaces
// rpmsg_transport.c with the real OpenAMP/RPMsg endpoint; these signatures
// do not change.
//
// The inbound half of this contract is not a function pointer registered
// through this header: Task 7's own rpmsg endpoint rx callback calls
// rpmsg_netif_rx() (declared in rpmsg_netif.h) directly, once the vrings
// with the Linux side are up. That callback fires from task context, not
// an ISR -- the vendor BSP's own rpmsg sample (rcar_bsp/.../sample_apps/
// rpmsg_sample/rpmsg-echo.c) already establishes that pattern: it polls
// the endpoint from inside a plain FreeRTOS task's loop, never a hardware
// interrupt handler. rpmsg_netif_rx()'s own comment explains why that
// matters on this port.
#ifndef RPMSG_TRANSPORT_H
#define RPMSG_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

// Brings up the RPMsg endpoint (service name RPMSG_ETH_SERVICE) and blocks
// until the vrings with the Linux side are ready. Returns 0 on success,
// non-zero otherwise. Task 7 implements this for real; the Task 6 stub
// always returns -1.
int rpmsg_transport_init(void);

// Sends one already-framed Ethernet frame (buf/len, at most
// RPMSG_ETH_MAX_FRAME bytes -- enforced by rpmsg_netif_core_tx() before this
// is ever called) over the RPMsg endpoint. Returns 0 on success, non-zero
// otherwise. Task 7 implements this for real; the Task 6 stub always
// returns -1.
int rpmsg_transport_send(const void *buf, unsigned len);

#ifdef __cplusplus
}
#endif

#endif  // RPMSG_TRANSPORT_H
