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

#include "platform/freertos/x5h/lwip_init.h"

// Task 6 replaces this with the real lwIP-over-RPMsg bring-up. Declared weak
// so a later task's strong definition silently takes over; until then this
// stub satisfies configure_network()'s link-time dependency on
// lwip_bring_up_blocking() (unused by this scaffold's main(), but declared in
// platform/freertos/x5h/lwip_init.h and reachable once Task 4 wires
// configure_network() in).
extern "C" __attribute__((weak)) int lwip_bring_up_blocking(void) {
    return 0;
}

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
