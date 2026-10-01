// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// FreeRTOS X5H entry point. Boots the R-Car BSP, then launches one of two
// task graphs from the same file, selected by X5H_NETIF_ONLY (set only on
// the netif_only_x5h CMake target -- see CMakeLists.txt's own comment on
// that target):
//   - Normal build (actuation_x5h): board -> actuation_task() ->
//     actuation_main() (main.cpp's configure_network() + Controller). This
//     is Task 7's own wiring; Tasks 3-6 built everything actuation_main()
//     needs but never called it.
//   - X5H_NETIF_ONLY build (netif_only_x5h, the Stage 2 board artifact):
//     board -> netif_only_task() -> configure_network() only, then idle.
//     lwIP answers ICMP on its own tcpip thread with nothing else running,
//     proving the RPMsg transport + netif work before ever trusting the
//     full actuation link on top.
// Either way, rpmsg_transport_init() (Task 7) runs first, inside
// configure_network()'s own lwip_bring_up_blocking() (see lwip_bringup.c) --
// nothing here calls it directly.
//
// The ELF's frozen memory layout (.text at 0x11600000, .resource_table at
// 0x96650000 with the exact vdev/vring contents check-elf-contract.sh
// checks) comes entirely from linking the BSP sample's rsc_table.c (see
// CMakeLists.txt's X5H_BSP_RPMSG_SOURCES) and the BSP's own linker scripts.
// No runtime code below needs to execute for the contract check to pass.

#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

#include "interrupts.h"
#include "pfc/r_pfc_api.h"
#include "device_tree_x5h.h"

// RPMSG_POLL_TASK_PRIORITY, plus TCPIP_THREAD_PRIO through the lwip/opt.h
// that header includes. Both targets already compile and link this
// transport, so neither pays anything new for it, and the assertions below
// need both priorities by name rather than by remembered value -- a number
// copied into this file is exactly the failure being fixed.
#include "rpmsg_transport.h"

// Task 18's diagnostic surface: the replacement exception vector table
// (installed at the top of main() below), the never-deleted liveness
// beacon, and the launcher-handle registration those two share. See
// x5h_diag.h for what each piece exists to catch. Compiled into BOTH
// targets, for the same reason the priority assertions below are outside
// the X5H_NETIF_ONLY split: netif_only_x5h is the cheap isolation build
// reached for first whenever the board misbehaves, and it should not be the
// one image that cannot say why it stopped.
#include "x5h_diag.h"

#include "common/actuation_param_profile.h"

// ---- task priorities ----
//
// CORRECTED (this replaces a stated invariant an earlier whole-branch review
// looked straight at, reasoned about, and cleared): the launcher below was
// created at configMAX_PRIORITIES - 2 (== 30), above every network task in
// this image. On the board that produced an actuation firmware whose
// rpmsg-eth channel came up and then transmitted nothing at all -- inbound
// frames counted up, outbound stayed at zero, ARP never resolved, and the
// Linux side logged its virtio_rpmsg send path giving up waiting for the
// remote to return a tx buffer, on a 15 s cadence. The netif_only_x5h image,
// on the identical transport, link and MTU but with its launcher at
// tskIDLE_PRIORITY + 1, had a clean symmetric link; the launcher's priority
// was the entire delta between the two builds.
//
// The trap, spelled out because it is what defeated the review: this task
// DOES block. It reaches pthread_join, via
// Controller::wait_for_completion() at src/main.cpp:57, and the review
// cleared priority 30 on exactly that ground. What that misses is
// everything the task runs BEFORE reaching that line -- configure_network()
// and then the whole Controller constructor, CycloneDDS participant creation
// plus Eigen MPC construction. With configUSE_TIME_SLICING == 0 (rcar_bsp's
// FreeRTOSConfig.h) nothing below can preempt a task that has not blocked
// yet, so for that entire stretch tcpip_thread (4) and rpmsg_poll_task (5)
// never ran -- and with them stopped, nothing they drive happened either:
// no inbound frame was delivered into the stack, no ARP retry went out, and
// no tx buffer was ever returned to Linux. "It blocks eventually" is not the
// property that matters here; "it cannot hold the CPU while the network has
// work to do" is.
//
// THE SCHEDULER FACT EVERY BOUND BELOW RESTS ON, stated once and up front.
// CORRECTED: an earlier version of this block asserted the opposite, that
// "under configUSE_TIME_SLICING == 0 equal priorities never round-robin".
// That is false, it survived a review, and it went on to mislead a later
// investigation into a different defect. It is corrected here rather than
// only deleted, because the bounds below are only as good as the mechanism
// they are justified by.
//
// FreeRTOS DOES round-robin equal-priority READY tasks. Selection is
// taskSELECT_HIGHEST_PRIORITY_TASK(), and this build has
// configUSE_PORT_OPTIMISED_TASK_SELECTION == 0 (rcar_bsp's own
// FreeRTOSConfig.h), so that macro is rcar_bsp/FreeRTOS/Source/tasks.c:
// 178-193 -- a listGET_OWNER_OF_NEXT_ENTRY() under the kernel's own comment
// at tasks.c:189-190: "indexes through the list, so the tasks of the same
// priority get an equal share of the processor time." (The port-optimised
// variant at tasks.c:219-227 does the same, so nothing here depends on
// which one is compiled.) configUSE_TIME_SLICING == 0 removes exactly one
// thing: the TICK-driven switch at tasks.c:4810-4841. It does not pin the
// CPU to one task. Every other context switch -- a task blocking, a
// taskYIELD(), a higher-priority task becoming ready or blocking again --
// still rotates the ready list, and on this port tcpip_thread and
// rpmsg_poll_task generate those constantly.
//
// So the two hazards are of different KINDS, and keeping them apart is what
// the earlier version got wrong:
//   - An equal-priority tie is a SHARING hazard. A CPU-bound task at equal
//     priority takes roughly half the CPU from its peer and badly delays
//     it. It does not hold the CPU until it blocks.
//   - A task at STRICTLY HIGHER priority preempts outright, indefinitely,
//     for as long as it stays runnable. That needs no time-slicing argument
//     at all -- it is plain priority.
// The bounds that fix the actual defect are the strictly-below ones, and
// they are load-bearing because of the second bullet, not the first.
//
// tskIDLE_PRIORITY + 2 (== 2). Every one of these bounds is asserted below
// rather than only described:
//   - strictly BELOW TCPIP_THREAD_PRIO (4) and RPMSG_POLL_TASK_PRIORITY (5).
//     This is the bound that fixes the defect, and it is the one that rests
//     on plain strict priority: both network tasks preempt this one
//     outright, so bring-up -- and the DDS traffic that follows -- can never
//     be starved by whatever the launcher happens to be doing.
//   - strictly ABOVE the pthreads at tskIDLE_PRIORITY + 1 (hardcoded in
//     include/platform/freertos/x5h/pthread.h, which exports no macro to
//     reference from here, so the expression is spelled out below rather
//     than mirrored under a second name). What this bound is worth, stated
//     at its corrected strength rather than its old inflated one: at 1 the
//     launcher would tie with the controller pthread it spawns through
//     Controller::spin(), and per the correction above that is a sharing
//     hazard -- during any stretch in which the launcher runs without
//     blocking, the pthread would get roughly half the CPU instead of all
//     of it. That window is small here (the launcher reaches pthread_join
//     at src/main.cpp:57 a few statements after spin()), so this bound buys
//     a deliberate, documented ordering rather than preventing a hang. It
//     is kept because it is free and because "do not tie with what you
//     create" is still the right default; it is no longer claimed to be
//     preventing starvation, because it is not.
//   - not 3, which is configTIMER_TASK_PRIORITY (vendor-fixed): at 3 the
//     launcher would tie with the FreeRTOS timer service instead. Same
//     corrected strength -- during the launcher's long non-blocking startup
//     stretch the timer service would be delayed by sharing, not stopped.
//
// One consequence that is invisible from this file but decided by this
// constant: CycloneDDS's own ddsrt threads are not the pthread.h ones.
// cyclonedds/src/ddsrt/src/threads/freertos/threads.c inherits the CALLING
// task's priority whenever attr->schedPriority is 0 -- the default nothing
// in ddsi overrides -- and those threads are created inside the Controller
// constructor, i.e. on this task. So this value is also the priority of
// every CycloneDDS internal thread: at 30 they too landed above the tcpip
// thread; at 2 they land below it, which is what the network stack needs.
//
// Now say the rest of that out loud, because it is the half a reader would
// otherwise reconstruct wrongly, and because a comfortable half-truth about
// a priority is exactly what this whole change exists to undo. Those ddsi
// threads land at EXACTLY this value. The launcher and the threads it
// creates inside dds_create_participant() are therefore an equal-priority
// TIE at 2.
//
// CORRECTED: this paragraph used to say that tie "does not round-robin:
// the ddsi threads cannot preempt the constructor that spawned them, and
// get the CPU only when the launcher itself blocks". Per the scheduler
// correction at the top of this block, that is false -- the ddsi threads
// share the CPU with the constructor round-robin from the moment they are
// created, and each of them makes progress during it. The tie costs
// throughput, not liveness.
//
// So moving the launcher from 30 to 2 RELOCATES this tie; it does not remove
// it, and no value available here would. 3 is configTIMER_TASK_PRIORITY and
// anything at or below 1 crosses the pthread.h threads, so every candidate
// ties with something. The tie is accepted, not overlooked, for two reasons:
// the property this fix is about is untouched by it -- tcpip_thread (4) and
// rpmsg_poll_task (5) sit STRICTLY ABOVE both sides of the tie, so they
// preempt it outright and no resolution of it can starve the network -- and
// the contended window is bounded by construction, ending a few statements
// after the threads are created. What this tie does mean is that the
// assertion below guards the pthread.h half of "do not tie with what you
// create" and nothing more; do not read it as covering the ddsi threads,
// because it cannot.
#define ACTUATION_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

// A comment is not a guard, and comments are exactly what failed here, so
// every bound listed above is asserted, not merely described -- including
// the "not 3" one, which none of the other three assertions would catch.
//
// Deliberately outside the X5H_NETIF_ONLY split, so BOTH targets compile it.
// netif_only_x5h is the cheap isolation build reached for first whenever the
// link misbehaves, and it links the same transport and the same lwipopts.h
// priority map; a regression in that map should fail its build too, not only
// the full actuation link. The constant is unused in that image (its own
// launcher is netif_only_task) but the relationship it asserts is a property
// of the shared priority map, which is what is being protected.
static_assert(ACTUATION_TASK_PRIORITY < TCPIP_THREAD_PRIO,
              "ACTUATION_TASK_PRIORITY must stay strictly below "
              "TCPIP_THREAD_PRIO (lwip_port/lwipopts.h): the actuation "
              "launcher runs configure_network() and the entire Controller "
              "constructor before it ever blocks, i.e. it is CPU-bound for "
              "that whole stretch -- which is exactly the case where its "
              "priority relative to the tcpip thread decides how much CPU "
              "the network stack gets. ABOVE the tcpip thread, a runnable "
              "launcher preempts it outright and the stack gets none of it; "
              "at EQUAL priority the two round-robin and the stack gets "
              "roughly half, which for a bring-up handshake is still a bad "
              "delay. Strictly below is the only placement that leaves the "
              "stack able to run whenever it has work. On the board the "
              "above-the-stack version produced an rpmsg-eth channel that "
              "announced itself and then transmitted nothing. Lower the "
              "launcher; do not raise it to match a network thread.");
static_assert(ACTUATION_TASK_PRIORITY < RPMSG_POLL_TASK_PRIORITY,
              "ACTUATION_TASK_PRIORITY must stay strictly below "
              "RPMSG_POLL_TASK_PRIORITY (rpmsg_transport.h): the poll task "
              "is what drains inbound RPMsg notifications and returns tx "
              "buffers to Linux. A launcher ABOVE it stops both outright "
              "while the launcher stays runnable; at EQUAL priority it "
              "halves the poll task's CPU during the same CPU-bound "
              "startup stretch. Either way the Linux side reports its "
              "virtio_rpmsg send path timing out waiting for the remote to "
              "return a tx buffer.");
static_assert(ACTUATION_TASK_PRIORITY > (tskIDLE_PRIORITY + 1),
              "ACTUATION_TASK_PRIORITY must stay strictly above the pthread "
              "priority hardcoded in include/platform/freertos/x5h/pthread.h "
              "(tskIDLE_PRIORITY + 1) -- the controller's own pthread and "
              "every other pthread_create() on this port. CORRECTED: this "
              "message used to claim that equal priorities never round-robin "
              "under configUSE_TIME_SLICING=0 and that a launcher tied with "
              "the pthreads would hold the CPU until it blocked. They do "
              "round-robin (see the ACTUATION_TASK_PRIORITY comment block "
              "for the kernel citations), so a tie here would cost the "
              "pthread roughly half the CPU during the launcher's "
              "non-blocking stretches, not all of it. The bound is kept "
              "because it is free and because 'do not tie with what you "
              "create' is the right default, not because it prevents a "
              "hang. Note the exact scope of it either way: it keeps the "
              "launcher clear of the pthread.h threads, NOT of every thread "
              "it creates. CycloneDDS's ddsrt threads inherit the "
              "launcher's own priority and are a deliberate, accepted "
              "equal-priority tie at this value -- see the "
              "ACTUATION_TASK_PRIORITY comment block for why that tie is "
              "tolerable and why no value here could avoid it.");
static_assert(ACTUATION_TASK_PRIORITY != configTIMER_TASK_PRIORITY,
              "ACTUATION_TASK_PRIORITY must not equal "
              "configTIMER_TASK_PRIORITY (3, vendor-fixed in rcar_bsp's own "
              "FreeRTOSConfig.h): that would tie the launcher with the "
              "FreeRTOS timer service. CORRECTED, same as the bound above: "
              "the two would round-robin, so the timer service would be "
              "DELAYED by sharing the CPU through the launcher's CPU-bound "
              "startup stretch, not stopped by it. Still worth avoiding, "
              "and still worth asserting rather than leaving as a comment, "
              "precisely because the other three assertions all pass at 3 "
              "(3<4, 3<5, 3>1), so nothing else here would catch it.");

#ifdef X5H_NETIF_ONLY
#include "platform/freertos/x5h/freertos_network.h"
#else
// actuation_main is main.cpp's main() renamed via -Dmain=actuation_main
// (CMakeLists.txt's set_source_files_properties on src/main.cpp). It calls
// configure_network() itself, once, before constructing the Controller --
// see src/main.cpp.
extern "C" int actuation_main(void);
#endif

// Mirrors sample_apps/hello_world/main.c's prvSetupHardware(). Irq_Setup()
// brings up the GIC; pfcInitModules(getModuleConfigs()) performs the PFC
// pin-muxing that routes SCIF1's physical TX/RX pins to the console (UART_ID
// selects SCIF1 -- without the pin-mux call, the peripheral registers alone
// don't reach the physical pins).
static void setup_hardware(void) {
    portDISABLE_INTERRUPTS();
    Irq_Setup();
    (void)pfcInitModules(getModuleConfigs());
}

#ifdef X5H_NETIF_ONLY

static void netif_only_task(void *pvParameters) {
    (void)pvParameters;
    int ret = configure_network();
    if (ret != 0) {
        printf("configure_network failed: %d\n", ret);
    }
    // Nothing runs on top of the netif in this build -- lwIP's own tcpip
    // thread already answers ARP/ICMP on its own. Idle forever rather than
    // vTaskDelete(nullptr): unlike actuation_task's failure path below, a
    // ping-only artifact has nothing further to hand off to, so there is no
    // "done, delete self" moment -- staying alive and quiet is the correct
    // terminal state whether configure_network() succeeded or not.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#else

static void actuation_task(void *pvParameters) {
    (void)pvParameters;
    // Network bring-up is owned by actuation_main() (main.cpp's
    // configure_network() call), which runs it once before constructing the
    // Controller. The body of this function mirrors
    // freertos_s32z2/freertos_main.cpp's own actuation_task() -- don't call
    // configure_network() here too, a second lwip_bring_up_blocking()
    // re-inits tcpip/netif and hangs.
    //
    // CORRECTED (this replaces a stated invariant a later board session
    // found to be false): the sentence removed from here claimed the mirror
    // was "verbatim; the reasoning is identical on this port." The BODY is
    // the same and should stay that way. The reasoning is not transferable,
    // and treating it as if it were is how the launcher below ended up at
    // priority 30. What actually got carried across was an arithmetic
    // expression, not a relationship: configMAX_PRIORITIES - 2 resolves to
    // 14 out of 16 on S32Z2 (its own FreeRTOSConfig.h) and to 30 out of 32
    // here, and the two ports place their network threads at completely
    // different levels -- S32Z2 at tcpip 12 / NETC RX poll 13, this port at
    // tcpip 4 / rpmsg poll 5. The sibling's scheduler settings differ too
    // (configUSE_TIME_SLICING is 1 there, 0 here). One expression, two
    // unrelated positions in two unrelated priority spaces.
    //
    // What is verified about THIS port, on this board, is the part that
    // matters: nothing moves a frame here unless a plain FreeRTOS task is
    // scheduled. There is no MAC and no networking interrupt of any kind --
    // inbound frames reach lwIP only when rpmsg_poll_task drains the
    // virtqueue, and Linux only gets its TX buffers back when that same task
    // runs. A launcher above them stops the link outright, which is exactly
    // what the board showed. Keep the body mirrored; never mirror the launch
    // priority (see ACTUATION_TASK_PRIORITY above).
    int ret = actuation_main();
    printf("actuation_main returned %d\n", ret);
    // Before vTaskDelete(nullptr), not after: the beacon holds this task's
    // handle to report its stack high-water mark, and once this task is
    // deleted the idle task frees the TCB and stack that handle points at.
    // Reading a high-water mark from a freed TCB is a use-after-free, so the
    // handle has to be dropped while it is still valid -- and only this
    // task can do that, since nothing else knows when it is about to go.
    x5h_diag_clear_launcher();
    vTaskDelete(nullptr);
}

#endif  // X5H_NETIF_ONLY

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    // Report loudly before spinning, so a too-small task stack names itself
    // instead of silently hanging.
    fprintf(stderr, "FreeRTOS: STACK OVERFLOW in task '%s'\n", pcTaskName ? pcTaskName : "?");
    for (;;) {}
}

int main(void) {
    // First statement of main(), before setup_hardware() and before the
    // first printf: this is the earliest point in the boot path that is
    // ours to write. Everything before it belongs to the frozen vendor BSP
    // -- boot.S programs VBAR to the vendor table and calls SystemInit(),
    // which sets up the MPU, runs __libc_init_array() and brings up the
    // console, then boot.S calls here. Installing the diagnostic table here
    // covers the whole of setup_hardware(), every task creation, the
    // scheduler, and all of actuation_main(); it does NOT cover SystemInit()
    // itself, which is the price of not editing rcar_bsp/.
    x5h_diag_install_vectors();
    setup_hardware();
#if defined(X5H_DIAG_TASK_TABLE) && (X5H_DIAG_TASK_TABLE)
    // Task 33 (R3f): capture MPIDR and the tick PPI's not-yet-programmed
    // priority byte while the scheduler -- and therefore
    // vConfigureTickInterrupt() -- has not run. Must sit after
    // setup_hardware() (needs the GICR base Irq_Setup() plants) and before
    // vTaskStartScheduler(); the beacon prints the line built from it.
    x5h_diag_gic_capture_boot();
#endif
#ifdef X5H_NETIF_ONLY
    printf("FreeRTOS X5H (netif-only) starting"
           " (actuation_param_profile=" ACTUATION_PARAM_PROFILE_NAME ")...\n");
#else
    printf("FreeRTOS X5H actuation starting"
           " (actuation_param_profile=" ACTUATION_PARAM_PROFILE_NAME ")...\n");
#endif

    TaskHandle_t task_handle = nullptr;
#ifdef X5H_NETIF_ONLY
    // No Controller construction on this path (no CycloneDDS/Eigen linked
    // into this target at all -- see CMakeLists.txt's netif_only_x5h
    // source list), so this task needs nothing beyond configure_network()'s
    // own call depth (lwip_bring_up_blocking() -> rpmsg_transport_init() ->
    // the OpenAMP/libmetal setup chain -- all bounded, non-recursive C).
    // configMINIMAL_STACK_SIZE * 4 gives headroom over that chain without
    // reaching for the 128 KiB the actuation launcher below needs for a
    // completely different reason (CycloneDDS + Eigen MPC construction).
    BaseType_t rc = xTaskCreate(netif_only_task, "netif_only",
                                 configMINIMAL_STACK_SIZE * 4, nullptr,
                                 tskIDLE_PRIORITY + 1, &task_handle);
#else
    // Stack: 32768 words (128 KiB), matching
    // freertos_s32z2/freertos_main.cpp's own actuation_task launcher stack
    // exactly, not a fresh guess -- this is the SAME
    // autoware_mpc_lateral_controller component, the SAME
    // CycloneDDS-participant + Eigen-MPC construction path, on the SAME
    // Cortex-R52 + NEON ABI (see that file's own comment for the original
    // overflow-then-fix history: a 256 KiB stack there was not enough).
    // That half of the sibling-port argument still holds: the two launchers
    // execute the same startup code, so they need the same stack depth.
    //
    // CORRECTED (this replaces a stated invariant a later board session
    // found to be false): the priority half of that same sibling-port
    // argument does NOT hold, and this call used to carry it across too, as
    // configMAX_PRIORITIES - 2 (== 30). Identical startup code says how much
    // stack it needs; it says nothing about what may be preempted while it
    // runs. And configMAX_PRIORITIES - 2 is an expression, not a
    // relationship -- 14 of 16 there, 30 of 32 here, over two priority maps
    // that place their network threads nowhere near each other (see
    // actuation_task()'s own comment above). ACTUATION_TASK_PRIORITY,
    // defined and compile-time asserted at the top of this file, is the
    // corrected value together with the reasoning behind it; nothing else
    // about this call changes.
    //
    // configUSE_TASK_FPU_SUPPORT=2 (this file's CMakeLists.txt) means plain
    // xTaskCreate is correct here, unlike S32Z2's xTaskCreateFpu: every task
    // already gets FPU register context reserved at creation on this port,
    // with no per-task opt-in call required (see that CMakeLists.txt
    // section's own detailed comment).
    BaseType_t rc = xTaskCreate(actuation_task, "actuation", 32768, nullptr,
                                 ACTUATION_TASK_PRIORITY, &task_handle);
#endif
    if (rc != pdPASS) {
        printf("xTaskCreate failed: %ld\n", (long)rc);
        for (;;) {}
    }

    // The liveness beacon (Task 18 / R3), created before the scheduler
    // starts so its first line lands as early as the tick allows and so no
    // window exists in which the image is running but unmonitored. It takes
    // the launcher's handle because the stack it reports on is the one that
    // runs the DDS creation chain; task_handle is that task on either
    // branch above. Deliberately never deleted -- see x5h_diag.c.
    //
    // A failure here is reported but not fatal, matching how
    // rpmsg_transport.c treats its own diagnostics-only task: an image that
    // boots without a beacon is exactly as useful as this image was before
    // Task 18, whereas refusing to boot over it would be strictly worse.
    if (x5h_diag_start_beacon(task_handle) != pdPASS) {
        printf("x5h_diag_start_beacon failed; continuing without a beacon\n");
    }

    vTaskStartScheduler();
    for (;;) {}
    return 1;
}
