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
// tskIDLE_PRIORITY + 2 (== 2). Every one of the three bounds is
// load-bearing:
//   - strictly BELOW TCPIP_THREAD_PRIO (4) and RPMSG_POLL_TASK_PRIORITY (5).
//     This is the bound that fixes the defect: both network tasks preempt
//     this one, so bring-up -- and the DDS traffic that follows -- can never
//     be starved by whatever the launcher happens to be doing.
//   - strictly ABOVE the pthreads at tskIDLE_PRIORITY + 1 (hardcoded in
//     include/platform/freertos/x5h/pthread.h, which exports no macro to
//     reference from here, so the expression is spelled out below rather
//     than mirrored under a second name). Not 1: that would tie the launcher
//     to the very threads it creates, and under configUSE_TIME_SLICING == 0
//     a tie means whichever task the scheduler picked holds the CPU until it
//     blocks -- the same hazard lwipopts.h's own thread-priority comment
//     warns about.
//   - not 3, which is configTIMER_TASK_PRIORITY (vendor-fixed).
//
// One consequence that is invisible from this file but decided by this
// constant: CycloneDDS's own ddsrt threads are not the pthread.h ones.
// cyclonedds/src/ddsrt/src/threads/freertos/threads.c inherits the CALLING
// task's priority whenever attr->schedPriority is 0 -- the default nothing
// in ddsi overrides -- and those threads are created inside the Controller
// constructor, i.e. on this task. So this value is also the priority of
// every CycloneDDS internal thread: at 30 they too landed above the tcpip
// thread; at 2 they land below it, which is what the network stack needs.
#define ACTUATION_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

// A comment is not a guard, and comments are exactly what failed here, so
// the ordering is asserted rather than merely described.
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
              "constructor before it ever blocks, and with "
              "configUSE_TIME_SLICING=0 a launcher at or above the lwIP "
              "tcpip thread starves the network stack for that whole "
              "stretch. On the board that produced an rpmsg-eth channel "
              "that announced itself and then transmitted nothing. Lower "
              "the launcher; do not raise it to match a network thread.");
static_assert(ACTUATION_TASK_PRIORITY < RPMSG_POLL_TASK_PRIORITY,
              "ACTUATION_TASK_PRIORITY must stay strictly below "
              "RPMSG_POLL_TASK_PRIORITY (rpmsg_transport.h): the poll task "
              "is what drains inbound RPMsg notifications and returns tx "
              "buffers to Linux. A launcher at or above it stops both, and "
              "the Linux side reports its virtio_rpmsg send path timing out "
              "waiting for the remote to return a tx buffer.");
static_assert(ACTUATION_TASK_PRIORITY > (tskIDLE_PRIORITY + 1),
              "ACTUATION_TASK_PRIORITY must stay strictly above the "
              "DDS/controller pthread priority (tskIDLE_PRIORITY + 1, "
              "hardcoded in include/platform/freertos/x5h/pthread.h): with "
              "configUSE_TIME_SLICING=0 equal-priority tasks never "
              "round-robin, so a launcher tied to the threads it creates "
              "holds the CPU until it blocks -- whichever way the scheduler "
              "happens to pick.");

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
    setup_hardware();
#ifdef X5H_NETIF_ONLY
    printf("FreeRTOS X5H (netif-only) starting...\n");
#else
    printf("FreeRTOS X5H actuation starting...\n");
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

    vTaskStartScheduler();
    for (;;) {}
    return 1;
}
