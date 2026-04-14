// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
//
// FreeRTOS POSIX simulator entry point.
// Starts the FreeRTOS scheduler, then launches the actuation main() as a task.

#include <cstdlib>
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

// Forward-declare the actuation module main (renamed to actuation_main via -Dmain=actuation_main)
extern int actuation_main(void);

static void actuation_task(void *pvParameters) {
    (void)pvParameters;
    int ret = actuation_main();
    fprintf(stderr, "actuation_main returned %d\n", ret);
    vTaskDelete(NULL);
}

// Static allocation support (C linkage required by FreeRTOS kernel)
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

static StaticTask_t xTimerTaskTCBBuffer;
static StackType_t xTimerStack[configTIMER_TASK_STACK_DEPTH];

extern "C" {

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *pulIdleTaskStackSize) {
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
    *ppxIdleTaskStackBuffer = xIdleStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *pulTimerTaskStackSize) {
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCBBuffer;
    *ppxTimerTaskStackBuffer = xTimerStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

} // extern "C"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    fprintf(stderr, "FreeRTOS POSIX simulator starting...\n");

    // Create the main actuation task with a large stack (matches Zephyr CONFIG_MAIN_STACK_SIZE)
    BaseType_t ret = xTaskCreate(
        actuation_task,
        "actuation",
        32768,  // Stack size in words (128KB on 64-bit)
        NULL,
        configMAX_PRIORITIES - 2,  // High priority
        NULL
    );

    if (ret != pdPASS) {
        fprintf(stderr, "Failed to create actuation task\n");
        return 1;
    }

    // Start the scheduler (never returns on POSIX port)
    vTaskStartScheduler();

    // Should never reach here
    fprintf(stderr, "Scheduler exited unexpectedly\n");
    return 1;
}
