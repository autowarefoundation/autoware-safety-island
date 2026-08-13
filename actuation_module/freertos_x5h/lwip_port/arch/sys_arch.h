// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP OS-abstraction types for the FreeRTOS sys_arch.c port (../sys_arch.c).
// Standard lwIP FreeRTOS port shape: semaphores/mutexes are FreeRTOS
// SemaphoreHandle_t, mailboxes are FreeRTOS QueueHandle_t of void*.
#ifndef PLATFORM_FREERTOS_X5H_LWIP_ARCH_SYS_ARCH_H_
#define PLATFORM_FREERTOS_X5H_LWIP_ARCH_SYS_ARCH_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t sys_mbox_t;
typedef TaskHandle_t sys_thread_t;

#define SYS_MBOX_NULL  ((QueueHandle_t)NULL)
#define SYS_SEM_NULL   ((SemaphoreHandle_t)NULL)
#define SYS_MUTEX_NULL ((SemaphoreHandle_t)NULL)

#endif  // PLATFORM_FREERTOS_X5H_LWIP_ARCH_SYS_ARCH_H_
