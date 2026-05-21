/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>

extern void start_app_task(void);

void vAssertCalled(const char *pcFile, unsigned long ulLine) {
    fprintf(stderr, "ASSERT: %s:%lu\n", pcFile, ulLine);
    fflush(stderr);
    exit(2);
}

void vApplicationMallocFailedHook(void) {
    fprintf(stderr, "ERROR: malloc failed\n");
    exit(3);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    fprintf(stderr, "ERROR: stack overflow in task '%s'\n", pcTaskName);
    exit(4);
}

int main(void) {
    start_app_task();
    vTaskStartScheduler();
    /* Should never reach here. */
    return 1;
}
