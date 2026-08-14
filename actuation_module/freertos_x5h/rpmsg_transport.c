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

#include "lwip/tcpip.h"         /* TCPIP_THREAD_PRIO -- see RPMSG_POLL_TASK_PRIORITY below */
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

// ---- poll task priority -- review finding (Important 2) ----
//
// Previously tskIDLE_PRIORITY + 1 (== 1) with no justification recorded.
// That number happens to collide with lwIP's own tcpip thread priority:
// TCPIP_THREAD_PRIO is not defined in this port's lwipopts.h (grep
// confirmed), so lwIP's own lwip/opt.h default of 1 applies -- pulled in
// here via lwip/tcpip.h so the macro is programmatically visible instead of
// a hardcoded magic number that could silently drift out of sync. With
// configUSE_TIME_SLICING == 0 (FreeRTOSConfig.h, this target), FreeRTOS does
// NOT round-robin ready tasks of equal priority on a time-slice tick -- a
// ready task only yields the CPU to an equal-priority peer when it blocks or
// explicitly calls taskYIELD(). Sitting the poll task at the same priority
// as the tcpip thread therefore risked exactly the kind of scheduling
// starvation this review is about, just on the rx side instead of tx.
//
// Chosen value: TCPIP_THREAD_PRIO + 1 (== 2). Rationale for both bounds:
//   - Strictly ABOVE TCPIP_THREAD_PRIO (1): the poll task's only real-time
//     job is draining MFIS/virtqueue notifications promptly so rx frames
//     reach tcpip_input()'s mailbox with low latency; delivery into that
//     mailbox is itself non-blocking (sys_mbox_trypost(), see the stack
//     comment above), so raising this task above the tcpip thread cannot
//     starve the tcpip thread of CPU -- it only ever preempts it for the
//     short, bounded duration of one platform_poll()/ept_cb() pass, then
//     blocks again on vTaskDelay(1).
//   - Strictly BELOW actuation_task (configMAX_PRIORITIES - 2 == 30, see
//     freertos_main.cpp): the controller must always win CPU contention
//     against network/IPC housekeeping; nothing about this transport's rx
//     path is allowed to delay the control loop. A gap of 28 priority
//     levels between this task (2) and the controller (30) leaves plenty of
//     room for any future intermediate task without needing to renumber
//     this one.
#define RPMSG_POLL_TASK_PRIORITY (TCPIP_THREAD_PRIO + 1)

static void rpmsg_poll_task(void *pv) {
    (void)pv;
    for (;;) {
        platform_poll(s_platform);
        // Lost-kick backstop (Important 3): platform_rcar.c's platform_poll()
        // (frozen, byte-identical to the vendor BSP sample -- must not be
        // modified) reads mfis->int_source, calls remoteproc_get_notification()
        // only if non-zero, then unconditionally clears mfis->int_source = 0.
        // mfis->int_source is a plain (non-volatile-qualified in the struct
        // definition) uint16_t written by the real MFIS ISR
        // (x5h_proc_interrupt_cb, see the rx-path comment above); a kick that
        // lands after platform_poll()'s read but before its clear is
        // overwritten by the clear and never observed by that call. Because
        // platform_rcar.c cannot be edited, the backstop has to live here
        // instead: call remoteproc_get_notification() again, unconditionally,
        // every tick, regardless of what platform_poll() just saw.
        // remoteproc_get_notification() (openamp/remoteproc.h) processes
        // whatever the virtqueues currently have pending and is a correctly
        // idempotent no-op when there is nothing new -- it does not depend on
        // int_source at all, so it cannot itself lose or re-lose a kick.
        // RSC_NOTIFY_ID_ANY (0xFFFFFFFFU) tells it to check all vrings rather
        // than one specific notify id, matching platform_poll()'s own call.
        remoteproc_get_notification((struct remoteproc *)s_platform,
                                     RSC_NOTIFY_ID_ANY);
        vTaskDelay(1);
    }
}

// ---- heartbeat task -- review finding (Important 4) ----
//
// platform_create_rpmsg_vdev() (vendor BSP, platform_info_common.c) blocks
// internally waiting for the Linux-side rpmsg-eth driver to reach
// VIRTIO_CONFIG_S_DRIVER_OK on the shared vdev status byte, with no timeout
// and no progress output of its own. Whatever task calls
// rpmsg_transport_init() (configure_network() -> lwip_bring_up_blocking(),
// which review confirms runs on actuation_task, priority
// configMAX_PRIORITIES - 2 == 30) therefore cannot yield this wait down to
// any task below its own priority in any way a caller could observe: the
// wait loop's internal taskYIELD()/metal_cpu_yield() calls only let
// equal-or-higher-priority ready tasks run, and nothing else in this image
// is registered above priority 30 except this heartbeat task and FreeRTOS's
// own idle/timer tasks. Without an independent, higher-priority task that
// blocks on a real timer (not a busy-yield), a board session has no way to
// tell "still waiting for Linux to bind" from "hung" -- both look like
// silence on the console.
//
// Fix: spawn a task at configMAX_PRIORITIES - 1 (31, one above
// actuation_task) that vTaskDelay()s on a real tick-driven timeout and
// prints progress; vTaskDelay() blocks on the tick interrupt independent of
// what actuation_task is doing, so -- unlike a same-or-lower-priority
// task -- it is guaranteed to run periodically regardless of how long
// platform_create_rpmsg_vdev()'s wait takes. It is deleted the moment
// platform_create_rpmsg_vdev() returns (success or failure); it does not
// bound the wait itself (the vendor call still has no timeout), it only
// makes the wait observable.
#define RPMSG_VDEV_HEARTBEAT_PRIORITY (configMAX_PRIORITIES - 1)
#define RPMSG_VDEV_HEARTBEAT_STACK_WORDS configMINIMAL_STACK_SIZE
#define RPMSG_VDEV_HEARTBEAT_PERIOD_TICKS pdMS_TO_TICKS(2000)

static void rpmsg_vdev_heartbeat_task(void *pv) {
    (void)pv;
    unsigned n = 0;
    for (;;) {
        vTaskDelay(RPMSG_VDEV_HEARTBEAT_PERIOD_TICKS);
        LPRINTF("rpmsg_transport_init: still waiting for Linux rpmsg-eth"
                " bind (DRIVER_OK), %u s elapsed\r\n", ++n * 2);
    }
}

int rpmsg_transport_init(void) {
    int ret = platform_init(MFIS_CHAN, &s_platform);
    if (ret) {
        LPERROR("platform_init failed: %d\r\n", ret);
        // Preserve the real code (Minor #5): nothing has been allocated yet
        // at this point, so there is nothing to release on this path.
        return ret;
    }

    TaskHandle_t heartbeat_handle = NULL;
    xTaskCreate(rpmsg_vdev_heartbeat_task, "rpmsg_vdev_hb",
                RPMSG_VDEV_HEARTBEAT_STACK_WORDS, NULL,
                RPMSG_VDEV_HEARTBEAT_PRIORITY, &heartbeat_handle);
    // No failure check: if the heartbeat task itself cannot be created
    // (allocation failure), that is not fatal to bring-up -- it only means
    // this one wait is silent, same as before this fix. Falling through to
    // the blocking call below is preferable to failing transport init over
    // a diagnostics-only task.

    struct rpmsg_device *rpdev = platform_create_rpmsg_vdev(
        s_platform, 0, VIRTIO_DEV_DEVICE, NULL, NULL);

    if (heartbeat_handle) {
        vTaskDelete(heartbeat_handle);
    }

    if (!rpdev) {
        LPERROR("platform_create_rpmsg_vdev failed\r\n");
        // Distinct code (Minor #5): platform_create_rpmsg_vdev() itself
        // returns a pointer, not an error code, so there is no underlying
        // value to preserve here -- -2 distinguishes this stage from the
        // platform_init() and rpmsg_create_ept() failure paths below.
        return -2;
    }

    ret = rpmsg_create_ept(&s_ept, rpdev, RPMSG_ETH_SERVICE,
                            RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                            ept_cb, ept_unbind);
    if (ret) {
        LPERROR("rpmsg_create_ept failed: %d\r\n", ret);
        // Leak, unavoidable (Minor #5): rpdev's underlying
        // rpmsg_virtio_device (and the vdev/vring state
        // platform_create_rpmsg_vdev() allocated above) is not released on
        // this path. The vendor BSP's platform_release_rpmsg_vdev()
        // (platform_rcar.c, frozen) is an empty stub -- it takes no action
        // on any platform/vdev -- so there is no real release call this
        // file could make instead. This mirrors the same stub-imposed leak
        // on the xTaskCreate() failure path just below.
        return ret;
    }

    TaskHandle_t poll_task_handle = NULL;
    BaseType_t rc = xTaskCreate(rpmsg_poll_task, "rpmsg_poll",
                                 RPMSG_POLL_TASK_STACK_WORDS, NULL,
                                 RPMSG_POLL_TASK_PRIORITY, &poll_task_handle);
    if (rc != pdPASS) {
        LPERROR("xTaskCreate(rpmsg_poll) failed: %ld\r\n", (long)rc);
        rpmsg_destroy_ept(&s_ept);
        // Same unavoidable vdev leak as above (platform_release_rpmsg_vdev()
        // is a no-op stub). rc is preserved rather than collapsed to -1:
        // this exact xTaskCreate() (tasks.c, prvCreateTask() path, checked
        // against this vendor tree) only ever returns pdPASS (1, already
        // excluded by the rc != pdPASS guard above) or
        // errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY (-1) -- never pdFAIL (0),
        // which would otherwise be misread as success by this function's
        // own "non-zero on failure" contract (rpmsg_transport.h).
        return (int)rc;
    }

    LPRINTF("rpmsg-eth endpoint created (addr=%u)\r\n", (unsigned)s_ept.addr);
    return 0;
}

int rpmsg_transport_send(const void *buf, unsigned len) {
    // rpmsg_trysend() (wait=false), not rpmsg_send() (wait=true): review
    // found rpmsg_send()'s blocking path
    // (rpmsg_virtio_get_tx_payload_buffer(), OpenAMP) polls the tx ring for
    // up to RPMSG_TICK_COUNT/RPMSG_TICKS_PER_INTERVAL = 15000 iterations of
    // metal_sleep_usec(1000) -- up to 15 seconds -- when the ring is full.
    // This function is reached from rpmsg_netif_linkoutput() (rpmsg_netif.c),
    // which lwIP only ever calls with LOCK_TCPIP_CORE() held (see
    // lwip_bringup.c's core-locking comment); a Linux side that stops
    // draining the ring for one frame would therefore freeze lwIP's core
    // lock -- and every socket/netconn call on it, DDS included -- for up to
    // 15 s. rpmsg_trysend() returns -ENOMEM immediately instead of blocking;
    // the caller (rpmsg_netif_core_tx(), Task 5's frozen core) already
    // counts any non-zero return as st->tx_err++ and reports ERR_IF up
    // through rpmsg_netif_linkoutput()'s switch, so lwIP just drops this one
    // frame and moves on -- the same outcome a real link-layer drop would
    // have, not a firmware-wide stall.
    int ret = rpmsg_trysend(&s_ept, buf, (int)len);
    return (ret >= 0) ? 0 : -1;
}
