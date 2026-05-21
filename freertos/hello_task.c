/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

static void hello_task(void *arg) {
    (void)arg;
    printf("hello freertos\n");
    fflush(stdout);
    vTaskDelete(NULL);
}

void start_app_task(void) {
    xTaskCreate(hello_task, "hello", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}
