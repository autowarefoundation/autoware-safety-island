// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 7: the real OpenAMP/RPMsg transport, replacing the Task 6 stub. This
// is a new file (Apache-2.0, this project's own), not a licensed copy of any
// BSP source -- but its control flow is deliberately modelled on, not
// rewritten from, the vendor BSP's own rpmsg sample
// (rcar_bsp/.../sample_apps/rpmsg_sample/rpmsg-echo.c, BSD-3-Clause,
// (c) 2025 Renesas Electronics Corporation): platform_init() ->
// platform_create_rpmsg_vdev() -> rpmsg_create_ept(), then a task-context
// poll loop calling platform_poll(). Two differences from that sample, both
// called out in the task brief: the endpoint's service name is
// RPMSG_ETH_SERVICE ("rpmsg-eth"), not "rpmsg-client-sample", and the rx
// callback forwards each message to rpmsg_netif_rx() (rpmsg_netif.h)
// instead of echoing it back. A third, structural difference: the sample's
// echoTask() does setup (platform_init/vdev/endpoint) and the poll loop in
// the same FreeRTOS task; here setup runs synchronously inside
// rpmsg_transport_init() (called from lwip_bring_up_blocking(), see
// lwip_bringup.c), and only the poll loop gets its own dedicated task -- see
// that task's own stack-sizing comment below for why this split matters.
#include <stdio.h>
#include <string.h>

#include <openamp/open_amp.h>

#include "FreeRTOS.h"
#include "task.h"

#include "platform_info.h"
#include "rsc_table.h"

#include "rpmsg_netif.h"        /* rpmsg_netif_rx() -- our rx callback target */
#include "rpmsg_netif_core.h"   /* RPMSG_ETH_SERVICE */
#include "rpmsg_transport.h"

#define LPRINTF(format, ...) printf(format, ##__VA_ARGS__); vTaskDelay(10);
#define LPERROR(format, ...) LPRINTF("ERROR: " format, ##__VA_ARGS__)

static struct rpmsg_endpoint s_ept;
static void *s_platform;

// ---- rx path -- must never run from ISR context ----
//
// This callback fires synchronously from inside platform_poll() (below),
// which itself only ever runs from rpmsg_poll_task's own FreeRTOS task loop
// -- never from the MFIS interrupt handler. Confirmed by reading the real
// ISR: drivers/mfis/mfis.c's mfis_interrupt_cb only records int_source/
// recv_message and invokes a lightweight no-op callback (see
// remoteproc_rcar.c's x5h_proc_interrupt_cb, "return;", installed as
// mfis.cb_function by x5h_proc_init()); all of the actual OpenAMP/virtqueue
// notification processing happens later, when platform_poll() (called only
// from task context here) observes mfis->int_source set and calls
// remoteproc_get_notification(). rpmsg_netif_rx()'s own comment explains why
// task context is a hard requirement on this port (SYS_ARCH_PROTECT ->
// taskENTER_CRITICAL, asserted against from ISR context by
// common/ARM_CR52/port.c). If this callback chain ever needed to run from a
// genuine interrupt handler, task-context delivery would have to be added
// (e.g. a queue handed to a task) before calling rpmsg_netif_rx() -- it does
// not today, so no such indirection exists here.
static int ept_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
                   uint32_t src, void *priv) {
    (void)ept;
    (void)src;
    (void)priv;
    rpmsg_netif_rx(data, (unsigned)len);
    return RPMSG_SUCCESS;
}

static void ept_unbind(struct rpmsg_endpoint *ept) {
    (void)ept;
    // The Linux-side rpmsg-eth driver tore down its endpoint (module unload,
    // reboot, crash). rpmsg_destroy_ept() below releases our local endpoint
    // state; there is no reconnect protocol on this link (none is specified
    // by the frozen contract this task inherits -- see rpmsg_transport.h),
    // so a subsequent rpmsg_transport_send() will simply fail (s_ept is
    // destroyed) rather than silently going nowhere. Unlike the vendor
    // sample's rpmsg_service_unbind() (which sets a shutdown_req flag that
    // its own task loop polls and then exits to an idle spin), this port has
    // no equivalent "stop everything" state to enter: rpmsg_poll_task must
    // keep calling platform_poll() regardless, since MFIS/virtqueue-level
    // traffic (e.g. a future re-bind after Linux reloads its driver) is
    // channel-level, independent of any one endpoint's lifetime.
    LPERROR("rpmsg-eth endpoint unbound by remote\r\n");
    rpmsg_destroy_ept(&s_ept);
}

// ---- poll task ----
//
// Stack size, justified rather than copied from configMINIMAL_STACK_SIZE
// (the vendor sample's own choice for echoTask, which -- unlike this task --
// also carries the one-time setup call chain: platform_init/
// platform_create_rpmsg_vdev/rpmsg_create_ept, none of which run on this
// task's stack because rpmsg_transport_init() below runs them synchronously
// on ITS OWN caller's stack (configure_network()'s task) before this task is
// even created).
//
// What actually runs on this task's stack, every iteration: platform_poll()
// -> remoteproc_get_notification() -> the OpenAMP virtqueue rx-processing
// loop -> ept_cb() (this file) -> rpmsg_netif_rx() -> rpmsg_netif_core_rx()
// -> glue_rx_deliver() (rpmsg_netif.c) -> lwIP's pbuf_alloc()/pbuf_take() ->
// s_netif->input() (tcpip_input(), which only posts a pointer to the tcpip
// thread's mailbox -- see lwip_bringup.c's netif_add() call site -- so
// nothing in lwIP's own protocol stack ever recurses onto THIS task's
// stack). None of those frames hold a stack-local buffer sized anywhere
// near RPMSG_ETH_MAX_FRAME (476B): unlike the vendor sample's own
// rpmsg_endpoint_cb() (`char payload[RPMSG_BUFFER_SIZE]`, a 512-byte
// stack-local it copies into before echoing), ept_cb() above forwards the
// data pointer straight through with no local copy, and glue_rx_deliver()
// copies directly into a pool-allocated pbuf, not a stack buffer -- the same
// class of hazard Task 6's report already found and fixed on the tx side
// (rpmsg_netif.c's s_tx_frame, hoisted to file scope for exactly this
// reason) does not recur here because there is no per-call local frame
// buffer on this path at all.
//
// configMINIMAL_STACK_SIZE * 2 (2048 bytes / 512 words): matches this exact
// vendor tree's own precedent for tasks with real (non-trivial, multi-frame)
// call depth below a poll loop -- e.g. sample_apps/i2c_app, wcrc_app,
// rt_dmac_app, smmu_app all use configMINIMAL_STACK_SIZE * 2 for comparable
// "poll a peripheral, walk a driver call chain" tasks -- while staying well
// below the *10 this tree reserves for genuinely heavy per-call state (e.g.
// drivers/virtio/r_virtio.c's Virtio_Task, or sample_apps/smmu_app's
// smmu_core.c page-table walker), neither of which applies to the shallow
// chain above. Verified against build.sh's actual link (see task-7-report.md
// for the -fstack-usage/objdump evidence gathered after the first
// successful build): this is not a guess left unchecked the way Task 6's
// 488-byte tx frame was before its own review caught it.
#define RPMSG_POLL_TASK_STACK_WORDS (configMINIMAL_STACK_SIZE * 2)

static void rpmsg_poll_task(void *pv) {
    (void)pv;
    for (;;) {
        platform_poll(s_platform);
        vTaskDelay(1);
    }
}

int rpmsg_transport_init(void) {
    int ret = platform_init(MFIS_CHAN, &s_platform);
    if (ret) {
        LPERROR("platform_init failed: %d\r\n", ret);
        return -1;
    }

    struct rpmsg_device *rpdev = platform_create_rpmsg_vdev(
        s_platform, 0, VIRTIO_DEV_DEVICE, NULL, NULL);
    if (!rpdev) {
        LPERROR("platform_create_rpmsg_vdev failed\r\n");
        return -1;
    }

    ret = rpmsg_create_ept(&s_ept, rpdev, RPMSG_ETH_SERVICE,
                            RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                            ept_cb, ept_unbind);
    if (ret) {
        LPERROR("rpmsg_create_ept failed: %d\r\n", ret);
        return -1;
    }

    TaskHandle_t poll_task_handle = NULL;
    BaseType_t rc = xTaskCreate(rpmsg_poll_task, "rpmsg_poll",
                                 RPMSG_POLL_TASK_STACK_WORDS, NULL,
                                 tskIDLE_PRIORITY + 1, &poll_task_handle);
    if (rc != pdPASS) {
        LPERROR("xTaskCreate(rpmsg_poll) failed: %ld\r\n", (long)rc);
        rpmsg_destroy_ept(&s_ept);
        return -1;
    }

    LPRINTF("rpmsg-eth endpoint created (addr=%u)\r\n", (unsigned)s_ept.addr);
    return 0;
}

int rpmsg_transport_send(const void *buf, unsigned len) {
    int ret = rpmsg_send(&s_ept, (void *)buf, (int)len);
    return (ret >= 0) ? 0 : -1;
}
