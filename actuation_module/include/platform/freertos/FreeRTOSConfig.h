// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
//
// FreeRTOS kernel configuration for the POSIX simulator.
// Based on FreeRTOS-Kernel/examples/template_configuration/FreeRTOSConfig.h

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// Scheduler
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMINIMAL_STACK_SIZE                ((unsigned short)4096)
#define configMAX_TASK_NAME_LEN                 32
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

// Memory
#define configTOTAL_HEAP_SIZE                   ((size_t)(4 * 1024 * 1024))  // 4MB for sim
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configAPPLICATION_ALLOCATED_HEAP         0

// Synchronization
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               20
#define configUSE_QUEUE_SETS                    0

// Timers
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                20
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

// Priorities
#define configMAX_PRIORITIES                    10

// Task notifications
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3

// Debug and trace
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

// Co-routines (not used)
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

// POSIX port specific
#define configNUMBER_OF_CORES                   1

// Required for vTaskDelete
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTimerPendFunctionCall          1

#endif  // FREERTOS_CONFIG_H
