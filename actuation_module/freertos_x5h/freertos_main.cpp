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
    // Controller -- mirrors freertos_s32z2/freertos_main.cpp's own
    // actuation_task() verbatim; the reasoning is identical on this port.
    // Don't call configure_network() here too -- a second
    // lwip_bring_up_blocking() re-inits tcpip/netif and hangs.
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
    // 32768 words (128 KiB): matches freertos_s32z2/freertos_main.cpp's own
    // actuation_task launcher stack exactly, not a fresh guess -- this is
    // the SAME autoware_mpc_lateral_controller component, the SAME
    // CycloneDDS-participant + Eigen-MPC construction path, on the SAME
    // Cortex-R52 + NEON ABI (see that file's own comment for the original
    // overflow-then-fix history: a 256 KiB stack there was not enough).
    // configUSE_TASK_FPU_SUPPORT=2 (this file's CMakeLists.txt) means plain
    // xTaskCreate is correct here, unlike S32Z2's xTaskCreateFpu: every task
    // already gets FPU register context reserved at creation on this port,
    // with no per-task opt-in call required (see that CMakeLists.txt
    // section's own detailed comment).
    BaseType_t rc = xTaskCreate(actuation_task, "actuation", 32768, nullptr,
                                 configMAX_PRIORITIES - 2, &task_handle);
#endif
    if (rc != pdPASS) {
        printf("xTaskCreate failed: %ld\n", (long)rc);
        for (;;) {}
    }

    vTaskStartScheduler();
    for (;;) {}
    return 1;
}
