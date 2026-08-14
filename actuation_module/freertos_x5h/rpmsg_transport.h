// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// RPMsg transport ops consumed by rpmsg_netif.c's lwIP glue. Task 6 coded
// against this header and shipped a stub implementation (rpmsg_transport.c)
// that always failed, so that branch built and the contract/budget checks
// stayed green with no real transport underneath. Task 7 has since replaced
// rpmsg_transport.c with the real OpenAMP/RPMsg endpoint (platform_init() ->
// platform_create_rpmsg_vdev() -> rpmsg_create_ept(), a dedicated poll task,
// and a heartbeat task for the vdev bring-up wait -- see that file); these
// signatures did not change in the process, and remain the frozen contract
// between this header and rpmsg_netif.c.
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
// until the vrings with the Linux side are ready. Returns 0 on success, a
// distinct negative value per failing stage otherwise (see
// rpmsg_transport.c's own comments at each return site).
int rpmsg_transport_init(void);

// Sends one already-framed Ethernet frame (buf/len, at most
// RPMSG_ETH_MAX_FRAME bytes -- enforced by rpmsg_netif_core_tx() before this
// is ever called) over the RPMsg endpoint. Returns 0 on success, non-zero
// otherwise -- including when the underlying rpmsg_trysend() reports the tx
// ring is full (see rpmsg_transport.c); the caller (rpmsg_netif_core_tx())
// already treats any non-zero return as a dropped-frame/tx_err, so this is
// not distinguished further here.
//
// Buffer ownership: the caller (rpmsg_netif.c's glue_tx(), backed by a
// single file-scope static frame buffer -- see rpmsg_netif.c's s_tx_frame
// comment) retains ownership of buf. The implementation must have fully
// consumed buf -- copied it into the OpenAMP vring, or otherwise finished
// reading it -- before returning; it must not retain the pointer or read
// from it asynchronously afterwards, since the caller is free to overwrite
// s_tx_frame's contents on its very next call.
int rpmsg_transport_send(const void *buf, unsigned len);

// Symmetric note on the inbound side, which has no function pointer of its
// own in this header: rpmsg_netif_rx() (rpmsg_netif.h) is called directly by
// Task 7's rpmsg endpoint rx callback with a buffer the callback owns.
// rpmsg_netif_rx()'s call chain (rpmsg_netif_core_rx() -> glue_rx_deliver())
// copies that data synchronously via pbuf_take() before returning, so the
// caller is free to release or reuse its rx buffer as soon as
// rpmsg_netif_rx() returns -- no asynchronous access is made to it.

#ifdef __cplusplus
}
#endif

#endif  // RPMSG_TRANSPORT_H
