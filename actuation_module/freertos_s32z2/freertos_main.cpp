// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// B-2: FreeRTOS entry — bring up the board, configure the network, run the
// actuation controller.

#include <cstdint>
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

#include "platform/freertos/s32z2/board_init.h"

// configAPPLICATION_ALLOCATED_HEAP==1: the application owns the heap_4 heap.
// Place it in the 7 MiB int_sram code region (via the .freertos_heap output
// section in heap_in_sram.ld) instead of the nearly-full 512 KiB int_sram_dram,
// so CycloneDDS (which mallocs through pvPortMalloc) has room. The array is
// NOLOAD, so it does not bloat the ELF.
extern "C" {
uint8_t ucHeap[configTOTAL_HEAP_SIZE]
    __attribute__((section(".freertos_heap"), aligned(8)));
}

// actuation_main is main.cpp's main() renamed via -Dmain=actuation_main.
// CMake adds a -Wl,--defsym alias so the symbol is reachable under the
// unmangled C name that NXP's startup.s expects.
extern "C" int actuation_main(void);

static void actuation_task(void *pvParameters) {
    (void)pvParameters;
    // Network bring-up is owned by actuation_main() (main.cpp's configure_network),
    // which runs it once before constructing the Controller. Don't call it here
    // too -- a second lwip_bring_up_blocking() re-inits tcpip/netif and hangs.
    int ret = actuation_main();
    printf("actuation_main returned %d\n", ret);
    vTaskDelete(nullptr);
}

// Static idle / timer task allocations (unchanged from B-1).
static StaticTask_t xIdleTaskTCB;
static StackType_t  xIdleStack[configMINIMAL_STACK_SIZE];
static StaticTask_t xTimerTaskTCB;
static StackType_t  xTimerStack[configTIMER_TASK_STACK_DEPTH];

extern "C" void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCB, StackType_t **ppxIdleTaskStack,
    configSTACK_DEPTH_TYPE *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCB       = &xIdleTaskTCB;
    *ppxIdleTaskStack     = xIdleStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

extern "C" void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCB, StackType_t **ppxTimerTaskStack,
    configSTACK_DEPTH_TYPE *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCB       = &xTimerTaskTCB;
    *ppxTimerTaskStack     = xTimerStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    // Report loudly over UART (stderr reaches UART on this port) before
    // spinning, so a too-small task stack names itself instead of silently
    // hanging. configCHECK_FOR_STACK_OVERFLOW==2 calls this at switch time.
    fprintf(stderr, "FreeRTOS: STACK OVERFLOW in task '%s'\n",
            pcTaskName ? pcTaskName : "?");
    for (;;) {}
}

int main(void) {
    board_init();
    printf("FreeRTOS S32Z2 actuation starting...\n");

    // actuation_task is the launcher: it brings up the network, then constructs
    // the Controller node and blocks in wait_for_completion(). The deep per-cycle
    // MPC/PID/Eigen work runs on the node's own 256 KiB node_stack
    // (controller_node.cpp, off-heap StaticTask), but the Controller *constructor*
    // (CycloneDDS participant/reader/writer creation + Eigen MPC setup, both
    // stack-heavy) runs HERE on the launcher stack. The POSIX simulator uses
    // 32768 words (128 KiB); the earlier 8192-word (32 KiB) value here was a
    // workaround for the old 96 KiB int_sram_dram heap and overflowed during
    // Controller construction (stack-overflow hook / corrupted-context undef
    // after dds_create_domain). Now that ucHeap lives in the 7 MiB int_sram
    // region (heap_in_sram.ld), the full 128 KiB launcher stack is affordable.
    // xTaskCreateFpu (not xTaskCreate) so the ARM_CR52_GIC port reserves a
    // per-task FPU context and records its TLS pointer. The port disables
    // FPEXC.EN per task and re-enables it lazily in vPortUndefinedInstruction
    // (wired to the undef vector in cp15_arm.S); that handler needs TLS[0] to
    // point at this task's FP save area, which only xTaskCreateFpu sets up.
    TaskHandle_t actuation_handle = nullptr;
    BaseType_t rc = xTaskCreateFpu(
        actuation_task, "actuation", 32768, nullptr,
        configMAX_PRIORITIES - 2, &actuation_handle);
    if (rc != pdPASS) {
        printf("xTaskCreate failed: %ld\n", (long)rc);
        for (;;) {}
    }

    vTaskStartScheduler();
    for (;;) {}
    return 1;
}
