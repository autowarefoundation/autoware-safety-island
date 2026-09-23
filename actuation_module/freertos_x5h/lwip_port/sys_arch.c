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
// Network bring-up itself (netif_add, dhcp, etc.) is out of scope in this
// file -- that lives in freertos_x5h/lwip_bringup.c, against the RPMsg netif
// glue in freertos_x5h/rpmsg_netif.{h,c} (see
// include/platform/freertos/x5h/lwip_init.h). This file only has to satisfy
// sys.h's link-time contract so the full actuation_x5h image links.
//
// ISR-safety note (review round 1): sys_arch_protect()/sys_arch_unprotect()
// below use taskENTER_CRITICAL()/taskEXIT_CRITICAL(), which
// common/ARM_CR52/port.c's own vPortEnterCritical() (around line 547)
// explicitly documents and asserts is never called from an ISR context --
// "Only API functions that end in FromISR can be used in an interrupt."
// SYS_ARCH_PROTECT/SYS_ARCH_UNPROTECT are therefore task-context-only on
// this port. rpmsg_netif.c's RPMsg receive ISR path honours this: it does
// not free pbufs (or touch any lwIP state guarded by
// SYS_ARCH_PROTECT/UNPROTECT) directly from the ISR -- received buffers are
// handed off to task context via sys_mbox_trypost_fromisr(), already
// implemented below.

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
    // Review finding (Minor): xQueueCreate()'s own contract asserts
    // uxQueueLength > 0 (configASSERT -> __BKPT on this port, an immediate
    // hard fault, not a graceful error return). A caller that asks for
    // size <= 0 -- e.g. a lwipopts.h mailbox-size macro left at its
    // lwip/opt.h default of 0 -- would previously crash inside
    // xQueueCreate() instead of getting this function's own documented
    // ERR_MEM failure path. lwipopts.h now sets every mailbox-size macro
    // this port's sys_mbox_new() callers use to a positive value, so this
    // guard is defence-in-depth against a future lwipopts.h regression, not
    // a path exercised today.
    if (size <= 0) {
        *mbox = NULL;
        return ERR_MEM;
    }
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
    // Returns ERR_OK rather than lwIP's canonical ERR_NEED_SCHED on the
    // success path (review round 1 note, not a logic change): lwIP's own
    // sys_arch.h documents ERR_NEED_SCHED as an optional signal telling the
    // ISR caller a context switch is now due, so it can request one itself
    // after this call returns. We do not use that signal because
    // portYIELD_FROM_ISR(woken) already requests the switch right here,
    // synchronously, using the xHigherPriorityTaskWoken value FreeRTOS's
    // own xQueueSendToBackFromISR() just gave us -- so there is nothing
    // left for a caller-side ERR_NEED_SCHED branch to do differently.
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
    //
    // configSTACK_DEPTH_TYPE (uint32_t on this port, see FreeRTOSConfig.h),
    // not a (uint16_t) truncating cast (review round 1 fix): xTaskCreate's
    // own uxStackDepth parameter is typed configSTACK_DEPTH_TYPE, and a
    // (uint16_t) cast here silently wrapped any request of 256 KiB or more
    // (65536 words) down modulo 65536 -- e.g. exactly 256 KiB wrapped to 0,
    // handing xTaskCreate a zero-word stack instead of asserting or
    // failing loudly. No caller in this codebase currently requests a
    // stack that large, but the cast was a latent bug for any future one
    // (or a future TCPIP_THREAD_STACKSIZE increase) to hit silently.
    BaseType_t ok = xTaskCreate((TaskFunction_t)thread, name,
                                 (configSTACK_DEPTH_TYPE)(stacksize / (int)sizeof(StackType_t)),
                                 arg, (UBaseType_t)prio, &task);
    // sys_thread_new() is documented as MUST NOT FAIL. configASSERT() halts
    // (it is FreeRTOSConfig.h's own vAssertCalled(), not a `return`), so a
    // failed xTaskCreate() here stops execution rather than letting the
    // caller (tcpip_init, typically) silently proceed with a NULL task
    // handle -- reworded from a previous revision of this comment that
    // described this as "returning NULL", which does not match what
    // configASSERT() actually does.
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
