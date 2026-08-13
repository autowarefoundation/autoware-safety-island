// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 3: freertos-x5h scaffold entry point. Boots the R-Car BSP, prints a
// boot banner once per second, and starts the FreeRTOS scheduler. Does NOT
// start the actuation module, bring up lwIP for real, or open an RPMsg
// endpoint -- those land in Tasks 4, 6, and 7 respectively.
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

// Task 6 replaced the Task 3 weak lwip_bring_up_blocking() stub that used to
// live here with a real definition (freertos_x5h/lwip_bringup.c). Deleted
// outright rather than merely overridden: a weak symbol that is silently
// displaced is also silently *kept* if the real definition ever fails to
// link, which would report the network as up when nothing exists.

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

static void main_task(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        printf("X5H_SCAFFOLD_ALIVE\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    // Report loudly before spinning, so a too-small task stack names itself
    // instead of silently hanging.
    fprintf(stderr, "FreeRTOS: STACK OVERFLOW in task '%s'\n", pcTaskName ? pcTaskName : "?");
    for (;;) {}
}

int main(void) {
    setup_hardware();
    printf("FreeRTOS X5H scaffold starting...\n");

    TaskHandle_t main_task_handle = nullptr;
    BaseType_t rc = xTaskCreate(main_task, "main", configMINIMAL_STACK_SIZE, nullptr,
                                tskIDLE_PRIORITY + 1, &main_task_handle);
    if (rc != pdPASS) {
        printf("xTaskCreate failed: %ld\n", (long)rc);
        for (;;) {}
    }

    vTaskStartScheduler();
    for (;;) {}
    return 1;
}
