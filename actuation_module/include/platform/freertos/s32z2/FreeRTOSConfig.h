// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// FreeRTOS kernel configuration for the NXP S32Z2 (Cortex-R52) target.
// Companion to actuation_module/freertos_s32z2/board_init.c.

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

// Base clock of the Cortex-R52 generic timer (CNTPCT), which generic_timer.c
// uses as `period = (configCPU_CLOCK_HZ / cnt_div) / configTICK_RATE_HZ`. This
// is the 40 MHz system-counter domain, NOT the 800 MHz RTU0_CORE_CLK — the
// generic timer runs on its own fixed clock. Value matches NXP's reference
// lwip_S32Z27X_FreeRTOS_R52 FreeRTOSConfig.h (we share its S32CT clock config).
// The previous 240 MHz placeholder made every tick 6x too long (~6 ms), so a
// pdMS_TO_TICKS(30000) wait took ~180 s instead of 30 s.
#define configCPU_CLOCK_HZ                  ((uint32_t)40000000UL)

// Use the Cortex-R52 physical timer (CNTP_*) rather than the virtual timer.
// Required by NXP's generic_timer.c to pick a TIMER_INT_ID at preprocess time.
#define configUSE_PHYSICAL_TIMER            1

#define configUSE_PREEMPTION                1
#define configUSE_IDLE_HOOK                 0
#define configUSE_TICK_HOOK                 0
#define configTICK_RATE_HZ                  1000U
#define configMAX_PRIORITIES                16
#define configMINIMAL_STACK_SIZE            ((unsigned short)512)
// Stack depths are in WORDS. The controller node thread needs >=64K words
// (256+ KiB). configSTACK_DEPTH_TYPE must be 32-bit: with the default 16-bit
// type, prvInitialiseNewTask stored uxStackDepth via strh and a 65536-word
// (256 KiB) or 131072-word (512 KiB) depth truncated to 0 -> the stack paint
// memset wrote 0 bytes and pxTopOfStack was set to pxStack-1, so the very first
// context switch tripped vApplicationStackOverflowHook (pxStack[0] != 0xa5)
// even though the task had not run. Forcing uint32_t fixes the truncation.
#define configSTACK_DEPTH_TYPE              uint32_t
#define configMAX_TASK_NAME_LEN             32
#define configUSE_16_BIT_TICKS              0
#define configIDLE_SHOULD_YIELD             1
#define configUSE_TIME_SLICING              1

// CycloneDDS allocates everything (participant, readers/writers, history
// caches, serdata, worker-thread stacks) through ddsrt_malloc -> pvPortMalloc,
// i.e. this FreeRTOS heap. 96 KiB is exhausted during dds_create_domain
// (pvPortMalloc returns NULL -> ddsrt_malloc -> abort). int_sram_dram (512 KiB,
// where .sram_data lands) is already ~500 KiB full, so the heap cannot grow
// there. Instead the application owns ucHeap (configAPPLICATION_ALLOCATED_HEAP)
// and places it in the 7 MiB int_sram code region via the .freertos_heap
// section in heap_in_sram.ld (freertos_main.cpp defines the array).
#define configTOTAL_HEAP_SIZE               ((size_t)(3U * 1024U * 1024U))
#define configSUPPORT_STATIC_ALLOCATION     1
#define configSUPPORT_DYNAMIC_ALLOCATION    1
#define configAPPLICATION_ALLOCATED_HEAP    1

#define configUSE_MUTEXES                   1
#define configUSE_RECURSIVE_MUTEXES         1
#define configUSE_COUNTING_SEMAPHORES       1
#define configQUEUE_REGISTRY_SIZE           20

#define configUSE_TIMERS                    1
#define configTIMER_TASK_PRIORITY           (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH            20
#define configTIMER_TASK_STACK_DEPTH        (configMINIMAL_STACK_SIZE * 4)

#define configUSE_TASK_NOTIFICATIONS        1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3

#define configCHECK_FOR_STACK_OVERFLOW      2
// CycloneDDS' FreeRTOS port queries task state via uxTaskGetSystemState /
// vTaskGetInfo / eTaskGetState (rusage, threads). Those require the trace
// facility.
#define configUSE_TRACE_FACILITY            1
#define configGENERATE_RUN_TIME_STATS       0
#define configUSE_CO_ROUTINES               0

// Cortex-R52 GIC interrupt-priority configuration. The R52 GIC500 supports
// 256 priority levels (8 bits). Tasks running below the syscall priority can
// call FreeRTOS API from ISRs; ISRs above this level are kernel-transparent.
#define configPRIO_BITS                              8
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      0x0F
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

// NXP's ARM_CR52_GIC port (port.c::vPortYield) reads per-task TLS pointers.
// Set this >0 so pvTaskGetThreadLocalStoragePointer is built into the kernel.
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 1

#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_uxTaskGetStackHighWaterMark 0
#define INCLUDE_xTimerPendFunctionCall      1
// CycloneDDS' FreeRTOS port references these.
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_eTaskGetState               1
#define INCLUDE_xTaskGetIdleTaskHandle      1

// Tick source: the Cortex-R52 generic timer (NXP generic_timer.c). Its
// SysTick_Handler is aliased to the port's xPortSysTickHandler via a linker
// --defsym in CMakeLists.txt, and the generic-timer path owns tick setup and
// acknowledgement. We therefore define no configSETUP_TICK_INTERRUPT() /
// configCLEAR_TICK_INTERRUPT() hooks — the earlier PIT-backed hooks were a
// second, unused tick source (Pit_Ip_* placeholder stubs) and were removed.

#endif  // FREERTOS_CONFIG_H
