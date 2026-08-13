// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// lwIP OS abstraction layer (lwip/src/include/lwip/sys.h) implemented over
// plain FreeRTOS primitives for the R-Car X5H (Cortex-R52) target.
//
// Mapping, matching every other lwIP FreeRTOS port (e.g. lwip/contrib's own
// ports/freertos):
//   sys_sem_*    -> FreeRTOS binary semaphores
//   sys_mutex_*  -> FreeRTOS mutexes (xSemaphoreCreateMutex)
//   sys_mbox_*   -> FreeRTOS queues of void* (capacity from the caller's
//                   `size` argument, e.g. TCPIP_MBOX_SIZE/
//                   DEFAULT_UDP_RECVMBOX_SIZE in lwipopts.h)
//   sys_thread_new -> xTaskCreate
//   sys_now      -> xTaskGetTickCount() * portTICK_PERIOD_MS
//   sys_arch_protect/unprotect -> taskENTER_CRITICAL/taskEXIT_CRITICAL
//     (enabled via SYS_LIGHTWEIGHT_PROT=1 in lwipopts.h; see arch/cc.h for
//     the sys_prot_t typedef this pairs with)
//
// Network bring-up itself (netif_add, dhcp, etc.) is out of scope here --
// that is Task 6's job (see include/platform/freertos/x5h/lwip_init.h). This
// file only has to satisfy sys.h's link-time contract so the full
// actuation_x5h image links.

#include "lwip/sys.h"
#include "lwip/opt.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// ---- Semaphores ----

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    *sem = xSemaphoreCreateBinary();
    if (*sem == NULL) {
        return ERR_MEM;
    }
    if (count > 0) {
        // xSemaphoreCreateBinary() starts empty (as if just taken); give it
        // once up front so an initial count of 1 is actually available to
        // the first sys_arch_sem_wait() caller.
        xSemaphoreGive(*sem);
    }
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t *sem) {
    xSemaphoreGive(*sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    TickType_t start = xTaskGetTickCount();
    if (xSemaphoreTake(*sem, ticks) == pdTRUE) {
        return (u32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
    }
    return SYS_ARCH_TIMEOUT;
}

void sys_sem_free(sys_sem_t *sem) {
    if (*sem != NULL) {
        vSemaphoreDelete(*sem);
        *sem = NULL;
    }
}

int sys_sem_valid(sys_sem_t *sem) {
    return (sem != NULL && *sem != NULL) ? 1 : 0;
}

void sys_sem_set_invalid(sys_sem_t *sem) {
    *sem = NULL;
}

// ---- Mutexes ----

err_t sys_mutex_new(sys_mutex_t *mutex) {
    *mutex = xSemaphoreCreateMutex();
    return (*mutex != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t *mutex) {
    xSemaphoreTake(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex) {
    xSemaphoreGive(*mutex);
}

void sys_mutex_free(sys_mutex_t *mutex) {
    if (*mutex != NULL) {
        vSemaphoreDelete(*mutex);
        *mutex = NULL;
    }
}

int sys_mutex_valid(sys_mutex_t *mutex) {
    return (mutex != NULL && *mutex != NULL) ? 1 : 0;
}

void sys_mutex_set_invalid(sys_mutex_t *mutex) {
    *mutex = NULL;
}

// ---- Mailboxes (queues of void*) ----

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    *mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
    return (*mbox != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    xQueueSendToBack(*mbox, &msg, portMAX_DELAY);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
    return (xQueueSendToBack(*mbox, &msg, 0) == pdTRUE) ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg) {
    BaseType_t woken = pdFALSE;
    err_t ret = (xQueueSendToBackFromISR(*mbox, &msg, &woken) == pdTRUE) ? ERR_OK : ERR_MEM;
    portYIELD_FROM_ISR(woken);
    return ret;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    TickType_t start = xTaskGetTickCount();
    void *dummy;
    void **out = (msg != NULL) ? msg : &dummy;
    if (xQueueReceive(*mbox, out, ticks) == pdTRUE) {
        return (u32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
    }
    return SYS_ARCH_TIMEOUT;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) {
    void *dummy;
    void **out = (msg != NULL) ? msg : &dummy;
    if (xQueueReceive(*mbox, out, 0) == pdTRUE) {
        return 0;
    }
    return SYS_MBOX_EMPTY;
}

void sys_mbox_free(sys_mbox_t *mbox) {
    if (*mbox != NULL) {
        vQueueDelete(*mbox);
        *mbox = NULL;
    }
}

int sys_mbox_valid(sys_mbox_t *mbox) {
    return (mbox != NULL && *mbox != NULL) ? 1 : 0;
}

void sys_mbox_set_invalid(sys_mbox_t *mbox) {
    *mbox = NULL;
}

// ---- Threads ----

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
                             int stacksize, int prio) {
    TaskHandle_t task = NULL;
    // stacksize is given in bytes by lwIP callers; xTaskCreate wants it in
    // StackType_t words (4 bytes each on this ILP32 target).
    BaseType_t ok = xTaskCreate((TaskFunction_t)thread, name,
                                 (uint16_t)(stacksize / sizeof(StackType_t)),
                                 arg, (UBaseType_t)prio, &task);
    // sys_thread_new() is documented as MUST NOT FAIL; a NULL return here
    // means the caller (tcpip_init, typically) proceeds with a NULL task
    // handle instead of us silently pretending success.
    configASSERT(ok == pdPASS);
    return task;
}

// ---- Time ----

u32_t sys_now(void) {
    return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// ---- Critical-section protection (SYS_LIGHTWEIGHT_PROT, see lwipopts.h) ----

sys_prot_t sys_arch_protect(void) {
    taskENTER_CRITICAL();
    return 0;
}

void sys_arch_unprotect(sys_prot_t pval) {
    (void)pval;
    taskEXIT_CRITICAL();
}

// ---- Init ----

void sys_init(void) {
    // No global lwIP OS state to initialise beyond what the FreeRTOS kernel
    // itself already sets up before main() runs.
}
