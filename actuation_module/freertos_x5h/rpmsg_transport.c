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
/* RPMSG_POLL_TASK_PRIORITY (moved there so freertos_main.cpp's ordering
   assertions and this file's xTaskCreate() share one definition), and
   TCPIP_THREAD_PRIO via the lwip/opt.h that header now includes -- which is
   why this file no longer includes lwip/tcpip.h for that macro itself. */
#include "rpmsg_transport.h"

// wrapped in do/while(0) (review finding, Minor): without it, `if (cond)
// LPRINTF(...);` with no braces only guards the printf() call -- the
// vTaskDelay(10) after it runs unconditionally, every time, regardless of
// `cond`. None of today's call sites happen to be written that way, but an
// unbraced two-statement macro is a latent footgun for the next one that is.
#define LPRINTF(format, ...) do { printf(format, ##__VA_ARGS__); vTaskDelay(10); } while (0)
#define LPERROR(format, ...) LPRINTF("ERROR: " format, ##__VA_ARGS__)

// A frame that does not fit inside one RPMsg buffer after OpenAMP's own
// header would be silently truncated or corrupted by rpmsg_trysend() rather
// than caught at compile time. RPMSG_ETH_MAX_FRAME (rpmsg_netif_core.h) is a
// frozen wire constant and RPMSG_BUFFER_SIZE comes from <openamp/open_amp.h>,
// already included above. A future change to either side -- a larger MTU, a
// smaller vring buffer -- fails the build here instead of failing silently on
// the wire.
//
// The header size is spelled as a literal rather than sizeof(struct rpmsg_hdr)
// because that struct is declared in OpenAMP's *internal* header
// (rpmsg_internal.h), not the public open_amp.h: sizeof() on it is an
// incomplete type here and does not compile. Reaching into the internal header
// to recover one integer would couple this target to OpenAMP's private layout
// for no benefit. 16 is the on-wire RPMsg header size the spec's buffer budget
// is already derived from (512-byte buffer - 16-byte header = 496 payload),
// so it is the same number the frozen constants were computed against.
#define RPMSG_HDR_BYTES 16
_Static_assert(RPMSG_ETH_MAX_FRAME <= RPMSG_BUFFER_SIZE - RPMSG_HDR_BYTES,
               "RPMSG_ETH_MAX_FRAME must fit within one RPMsg buffer after the header");

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
// chain above. Verified against build.sh's actual link, using
// -fstack-usage/objdump evidence gathered after the first successful build:
// this is not a guess left unchecked the way rpmsg_netif.c's own 488-byte
// tx frame was before an earlier review caught it (see that file's
// s_tx_frame comment).
#define RPMSG_POLL_TASK_STACK_WORDS (configMINIMAL_STACK_SIZE * 2)

// ---- poll task priority -- review findings (Important #1 of an earlier
// whole-branch round; then corrected a second time after the Stage 3 board
// session found the priority map this block asserts was itself wrong) ----
//
// CORRECTED, ROUND 1 (this replaced a stated invariant that a later
// whole-branch review found to be false): the original comment claimed the
// poll task sat "strictly BELOW actuation_task (configMAX_PRIORITIES - 2 ==
// 30): the controller must always win CPU contention." That reasoning
// treated actuation_task's own priority (30) as if it were the controller's
// priority. It is not: the controller's real work -- the MPC/PID loop --
// runs on a pthread spawned by include/platform/freertos/x5h/pthread.h's
// pthread_create(), which hardcodes tskIDLE_PRIORITY + 1 (== 1) with no way
// to override it from this file.
//
// CORRECTED, ROUND 2 (this round -- the same block, wrong again for a
// different reason, and this time it cost a board session): round 1 went on
// to claim that actuation_task "blocks forever in pthread_join once
// started", and concluded from that the second bound below "buys nothing
// today". Both halves were false in the way that mattered. actuation_task
// does reach pthread_join (Controller::wait_for_completion(),
// src/main.cpp:57) -- but only after running configure_network() AND the
// entire Controller constructor, CycloneDDS participant creation plus Eigen
// MPC construction, at priority 30, above every network task in this image.
// With configUSE_TIME_SLICING == 0 nothing below 30 can preempt a task that
// has not yet blocked, so for that whole stretch neither this poll task (5)
// nor the tcpip thread (4) ran at all. The board showed exactly that: the
// rpmsg-eth channel announced itself (so rpmsg_transport_init() below had
// run to completion) and then transmitted nothing -- inbound frames counted
// up on the Linux side, outbound stayed
// at zero, ARP never resolved, and the Linux side logged its virtio_rpmsg
// send path giving up waiting for the remote to return a tx buffer, on a
// 15 s cadence. The netif_only_x5h image, on the identical transport, link
// and MTU but with its launcher at tskIDLE_PRIORITY + 1, had a clean
// symmetric link -- which is what isolated the launcher's priority as the
// entire delta between the two builds.
//
// "It blocks in pthread_join" was true and was still the wrong test. The
// test is whether it holds the CPU while the network has work to do, and
// everything before src/main.cpp:57 does. freertos_main.cpp now launches it
// at ACTUATION_TASK_PRIORITY (tskIDLE_PRIORITY + 2 == 2) and asserts the
// ordering at compile time rather than describing it; see that file for why
// 2 and not 1 or 3. The corrected map, against rcar_bsp's own
// FreeRTOSConfig.h (configMAX_PRIORITIES 32, configUSE_PREEMPTION 1,
// configUSE_TIME_SLICING 0, configTIMER_TASK_PRIORITY 3):
//
//   31  rpmsg_vdev_hb        (transient, deleted after vdev bring-up)
//    5  rpmsg_poll_task      (this task -- TCPIP_THREAD_PRIO + 1)
//    4  tcpip_thread         (TCPIP_THREAD_PRIO, set in lwipopts.h)
//    3  FreeRTOS timer service (configTIMER_TASK_PRIORITY, vendor-fixed)
//    2  actuation_task       (ACTUATION_TASK_PRIORITY, freertos_main.cpp)
//       -- and with it every CycloneDDS ddsrt thread, which inherit this
//       priority rather than pthread.h's: ddsrt's FreeRTOS threads.c takes
//       the CALLING task's priority whenever attr->schedPriority is 0 (the
//       default nothing in ddsi overrides), and they are created inside the
//       Controller constructor, i.e. on actuation_task
//    1  the controller's own pthread, and anything else pthread.h creates
//       (tskIDLE_PRIORITY + 1, hardcoded there)
//
// Chosen value here is unchanged, TCPIP_THREAD_PRIO + 1; what moved is its
// definition, now in rpmsg_transport.h so freertos_main.cpp's assertions and
// this file's xTaskCreate() cannot disagree. Both bounds, restated as the
// properties that actually hold:
//   - Strictly ABOVE TCPIP_THREAD_PRIO: the poll task's only real-time job
//     is draining MFIS/virtqueue notifications promptly so rx frames reach
//     tcpip_input()'s mailbox with low latency; delivery into that mailbox
//     is itself non-blocking (sys_mbox_trypost(), see the stack comment
//     above), so raising this task above the tcpip thread cannot starve the
//     tcpip thread of CPU -- it only ever preempts it for the short, bounded
//     duration of one platform_poll()/ept_cb() pass, then blocks again on
//     vTaskDelay(1).
//   - Strictly ABOVE everything that runs application code: the launcher at
//     2, the CycloneDDS threads that inherit its priority, and the pthreads
//     at 1. This bound REPLACES, and inverts, round 1's "strictly BELOW
//     actuation_task (30)" -- after this change the poll task sits above the
//     launcher, and that is the point, not an accident of renumbering. The
//     property is not "the controller must win CPU contention" (it never
//     was); it is that the two tasks which actually move frames -- this one
//     and the tcpip thread -- must always be able to preempt whatever the
//     application is doing, including a startup path that runs for a long
//     time without blocking. Losing that does not produce a slow link, it
//     produces a link that never transmits at all. The half of this bound
//     that can realistically drift is the launcher's value, not this one's,
//     which is why the compile-time guard lives in freertos_main.cpp.
//
// Out of scope for this fix: whether the controller pthread itself
// (priority 1, set in pthread.h) should be raised. pthread_create() there
// is a shared POSIX-compat shim, not specific to this transport, and
// repricing it is a separate change this file does not make.

// Review finding (Important 5): rpmsg_netif_get_stats() -- the glue's own
// rx-drop counters plus the frozen core tx/rx counters -- had no caller
// anywhere in the tree. An operator on a slow serial console had no way to
// see, e.g., a steadily climbing rx_drop_input_err (tcpip mailbox full)
// short of attaching a debugger. Printed here, from this task, on a 5 s
// throttle, and only when at least one counter has actually moved since the
// last print -- so a healthy link stays silent and a struggling one is
// visible without flooding the console every tick.
#define RPMSG_NETIF_STATS_PRINT_PERIOD_TICKS pdMS_TO_TICKS(5000)

static void rpmsg_netif_print_stats_if_changed(void) {
    static TickType_t s_last_print_ticks;
    static rpmsg_netif_glue_stats s_last;
    static int s_have_last;

    TickType_t now = xTaskGetTickCount();
    if (s_have_last && (TickType_t)(now - s_last_print_ticks) < RPMSG_NETIF_STATS_PRINT_PERIOD_TICKS) {
        return;
    }

    rpmsg_netif_glue_stats cur;
    rpmsg_netif_get_stats(&cur);

    if (s_have_last && memcmp(&cur, &s_last, sizeof(cur)) == 0) {
        s_last_print_ticks = now;
        return;
    }

    LPRINTF("rpmsg_netif stats: tx_ok=%u tx_drop_oversize=%u tx_err=%u"
            " rx_ok=%u rx_drop_oversize=%u"
            " rx_drop_no_netif=%u rx_drop_no_pbuf=%u rx_drop_input_err=%u\r\n",
            cur.core.tx_ok, cur.core.tx_drop_oversize, cur.core.tx_err,
            cur.core.rx_ok, cur.core.rx_drop_oversize,
            cur.rx_drop_no_netif, cur.rx_drop_no_pbuf, cur.rx_drop_input_err);

    s_last = cur;
    s_have_last = 1;
    s_last_print_ticks = now;
}

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
        rpmsg_netif_print_stats_if_changed();
        vTaskDelay(1);
    }
}

// ---- heartbeat task -- review finding (Important 4) ----
//
// platform_create_rpmsg_vdev() (vendor BSP, platform_rcar.c) reaches
// OpenAMP's rpmsg_init_vdev(), which for the VIRTIO_DEV_DEVICE role this
// port uses ends in rpmsg_virtio_wait_remote_ready(): a loop polling the
// shared vdev status byte until the Linux-side rpmsg-eth driver sets
// VIRTIO_CONFIG_STATUS_DRIVER_OK, with no timeout and no progress output of
// its own. Whatever task calls rpmsg_transport_init() (configure_network()
// -> lwip_bring_up_blocking()) therefore sits in that loop for as long as
// Linux takes to bind. Without an independent, higher-priority task that
// blocks on a real timer, a board session has no way to tell "still waiting
// for Linux to bind" from "hung" -- both look like silence on the console.
//
// CORRECTED (the premise the rest of this comment used to rest on): it
// previously stated that the caller runs "on actuation_task, priority
// configMAX_PRIORITIES - 2 == 30", and that "nothing else in this image is
// registered above priority 30 except this heartbeat task". The caller is
// still actuation_task, but it now runs at ACTUATION_TASK_PRIORITY (== 2,
// see freertos_main.cpp) -- that 30 was the starvation defect, not a fact to
// build on -- so both statements are stale. Two things are worth recording
// rather than just renumbering:
//
//   - The wait's only concession to other tasks is metal_yield(), and on
//     this build that is libmetal's `generic` processor backend, where
//     metal_cpu_yield() expands to nothing at all (the fetched libmetal has
//     no arm/ variant, so lib/processor/generic/cpu.h is what gets
//     installed). It is a pure busy spin -- not even a taskYIELD(). No
//     choice of caller priority makes it hand the CPU over voluntarily.
//   - Which is precisely why this task's mechanism still holds, and why its
//     priority must NOT be lowered to track the caller's. It runs because
//     (a) it sits at configMAX_PRIORITIES - 1 (31), above every other task
//     in the image, the spinning caller included, and (b) it blocks in
//     vTaskDelay() -- a real tick-driven wait serviced by the tick
//     interrupt, not a busy-yield that depends on the spinner cooperating.
//     A task at or below the caller's priority would still be invisible for
//     the whole spin; this one is not.
//
// RPMSG_VDEV_HEARTBEAT_PRIORITY therefore stays at 31 and only its
// justification changes: the requirement is "above everything in the image",
// and with the launcher down at 2 that margin is wider than before, not
// narrower. The task is deleted the moment platform_create_rpmsg_vdev()
// returns (success or failure); it does not bound the wait itself (the
// vendor call still has no timeout), it only makes the wait observable.
//
// One real behaviour change the lower caller priority introduces, recorded
// here because this is the only place the wait is documented: at 30 the spin
// froze every task below it, so the tcpip thread (4) and the FreeRTOS timer
// service (3) made no progress for its whole duration. At 2 both can now
// preempt it -- preemption is priority-driven here (configUSE_PREEMPTION 1)
// and does not need the spinning task to yield. That is harmless at this
// point in the sequence: rpmsg_poll_task does not exist yet (it is created
// further down, after the endpoint), no CycloneDDS thread exists yet (the
// Controller is constructed later), no netif has been added yet, and the
// caller holds no lwIP core lock here -- lwip_bringup.c takes
// LOCK_TCPIP_CORE() only after rpmsg_transport_init() has returned -- so the
// tcpip thread waking on its own cyclic-timer schedule has nothing to
// contend with it for.
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
