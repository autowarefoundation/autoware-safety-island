// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Minimal hand-rolled pthread shim for FreeRTOS on the R-Car X5H
// (Cortex-R52, R-Car BSP's own ARM_CR52 FreeRTOS port).
//
// Replaces newlib's <pthread.h> (which on arm-none-eabi only ships type
// declarations, no working implementation). The common/ code only uses a
// small slice of pthread (mutex_lock/unlock, create/join/cancel,
// attr_init/setstack/destroy, PTHREAD_MUTEX_INITIALIZER, usleep), so this
// header stays small enough to read in one screen.
//
// Modelled on platform/freertos/s32z2/pthread.h, with one deliberate
// difference: S32Z2's ARM_CR52_GIC port uses a lazy per-task FPU context
// scheme that requires threads to be created with special
// xTaskCreateFpu/xTaskCreateStaticFpu variants (see that file's comment).
// The R-Car BSP's own common/ARM_CR52/port.c has no such function at all
// (confirmed by grepping rcar_bsp/ for xTaskCreateFpu: zero matches
// anywhere in the BSP), so this port uses the plain, standard FreeRTOS
// xTaskCreate/xTaskCreateStatic APIs throughout.
//
// CORRECTED (review round 1; the previous revision of this comment claimed
// the R-Car port saves/restores FPU context "automatically for every task
// whenever configUSE_TASK_FPU_SUPPORT is set" -- that was false as stated:
// configUSE_TASK_FPU_SUPPORT was not set anywhere in this BSP's
// FreeRTOSConfig.h, so it silently defaulted to FreeRTOS.h's own value of 1
// -- lazy, opt-in FPU context via a vPortTaskUsesFPU() call that nothing
// here ever made, so every task created via plain xTaskCreate/
// xTaskCreateStatic ran with NO preserved FPU context at all). Plain
// xTaskCreate/xTaskCreateStatic are correct here ONLY because
// CMakeLists.txt now explicitly forces configUSE_TASK_FPU_SUPPORT=2 on the
// freertos_bsp target (see that file's "FPU context for every task"
// section), which makes pxPortInitialiseStack() unconditionally give every
// task a live FPU context at creation time -- no xTaskCreateFpu-style
// variant needed, but also no "automatic for free" property inherent to
// this port independent of that CMake setting.
//
// pthread_t / pthread_mutex_t / pthread_attr_t typedefs come from newlib's
// <sys/_pthreadtypes.h> so we don't fight the typedefs that <chrono> and
// <time.h> pull in transitively. Backing storage:
//   - pthread_mutex_t (uint32_t) holds a SemaphoreHandle_t pointer-cast;
//     PTHREAD_MUTEX_INITIALIZER == 0 triggers lazy creation on first use.
//   - pthread_t (uint32_t) holds a pointer to a pthread_internal_t we
//     allocate in pthread_create. The wrapped FreeRTOS task is stored
//     there along with a binary semaphore used by pthread_join.

#ifndef PLATFORM_FREERTOS_X5H_PTHREAD_H_
#define PLATFORM_FREERTOS_X5H_PTHREAD_H_

#include <stddef.h>
#include <stdint.h>
// newlib's <sys/_pthreadtypes.h> uses clock_t for one of its struct members.
// Pull <time.h> in first so that typedef is in scope; otherwise the include
// fails with "'clock_t' does not name a type".
#include <time.h>
// Match newlib's <unistd.h> declaration of usleep(useconds_t).
#include <sys/types.h>
#include <sys/_pthreadtypes.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t)0)

// Defined in freertos_x5h/x5h_emutls.c; canonical declaration in
// freertos_x5h/x5h_emutls.h, which the common code including this header
// cannot reach on its include path -- hence this duplicate (keep in sync).
// Frees the per-task emulated-TLS storage that runtime hangs off a TCB
// thread-local-storage slot. Called below before every vTaskDelete() on
// another task: the slot lives inside the TCB, which for dynamic-stack
// tasks the idle task frees after the delete.
void x5h_emutls_task_cleanup(TaskHandle_t task);

typedef struct pthread_internal_s {
    void *(*entry)(void *);
    void *arg;
    TaskHandle_t task;
    SemaphoreHandle_t done;
    // Non-NULL only for static-stack tasks (xTaskCreateStatic, used when
    // pthread_attr_setstack() was called -- e.g. Node's controller thread,
    // which pre-allocates its stack via K_THREAD_STACK_DEFINE to keep it
    // out of the heap and inside the frozen 10 MiB Core1 budget). A static
    // task's TCB is application-owned -- the kernel never frees it -- so we
    // record it here and release it in pthread_join after vTaskDelete. NULL
    // for dynamic-stack tasks, where the kernel owns and frees the TCB.
    StaticTask_t *tcb;
} pthread_internal_t;

static inline int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if ((SemaphoreHandle_t)(uintptr_t)*mutex == NULL) {
        taskENTER_CRITICAL();
        if ((SemaphoreHandle_t)(uintptr_t)*mutex == NULL) {
            *mutex = (pthread_mutex_t)(uintptr_t)xSemaphoreCreateMutex();
        }
        taskEXIT_CRITICAL();
    }
    SemaphoreHandle_t s = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (s == NULL) {
        return -1;
    }
    return xSemaphoreTake(s, portMAX_DELAY) == pdPASS ? 0 : -1;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    SemaphoreHandle_t s = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (s == NULL) {
        return -1;
    }
    return xSemaphoreGive(s) == pdPASS ? 0 : -1;
}

static inline int pthread_attr_init(pthread_attr_t *attr)
{
    if (attr == NULL) {
        return -1;
    }
    attr->is_initialized = 1;
    attr->stackaddr = NULL;
    attr->stacksize = 0;
    return 0;
}

static inline int pthread_attr_setstack(pthread_attr_t *attr,
                                        void *stackaddr,
                                        size_t stacksize)
{
    if (attr == NULL) {
        return -1;
    }
    attr->stackaddr = stackaddr;
    attr->stacksize = (int)stacksize;
    return 0;
}

static inline int pthread_attr_destroy(pthread_attr_t *attr)
{
    if (attr == NULL) {
        return -1;
    }
    attr->is_initialized = 0;
    return 0;
}

static inline void pthread_x5h_entry_(void *p)
{
    pthread_internal_t *info = (pthread_internal_t *)p;
    info->entry(info->arg);
    xSemaphoreGive(info->done);
    vTaskSuspend(NULL);
}

static inline int pthread_create(pthread_t *thread,
                                 const pthread_attr_t *attr,
                                 void *(*start_routine)(void *),
                                 void *arg)
{
    if (thread == NULL || start_routine == NULL) {
        return -1;
    }
    pthread_internal_t *info = (pthread_internal_t *)pvPortMalloc(sizeof(*info));
    if (info == NULL) {
        return -1;
    }
    info->entry = start_routine;
    info->arg = arg;
    info->tcb = NULL;
    info->done = xSemaphoreCreateBinary();
    if (info->done == NULL) {
        vPortFree(info);
        return -1;
    }
    BaseType_t ok = pdFAIL;
    if (attr != NULL && attr->stackaddr != NULL && attr->stacksize > 0) {
        StaticTask_t *tcb = (StaticTask_t *)pvPortMalloc(sizeof(StaticTask_t));
        if (tcb != NULL) {
            info->task = xTaskCreateStatic(
                pthread_x5h_entry_,
                "pthread",
                (uint32_t)((size_t)attr->stacksize / sizeof(StackType_t)),
                info,
                tskIDLE_PRIORITY + 1,
                (StackType_t *)attr->stackaddr,
                tcb);
            ok = (info->task != NULL) ? pdPASS : pdFAIL;
            if (ok == pdPASS) {
                /* The task owns this static TCB for its lifetime; record it so
                 * pthread_join can release it after vTaskDelete (the kernel
                 * never frees an application-provided TCB). */
                info->tcb = tcb;
            } else {
                /* Task creation failed; the TCB is unowned, so free it here --
                 * the shared cleanup below only frees info->done and info. */
                vPortFree(tcb);
            }
        }
    } else {
        configSTACK_DEPTH_TYPE depth = (configSTACK_DEPTH_TYPE)configMINIMAL_STACK_SIZE;
        if (attr != NULL && attr->stacksize > 0) {
            depth = (configSTACK_DEPTH_TYPE)((size_t)attr->stacksize / sizeof(StackType_t));
        }
        ok = xTaskCreate(pthread_x5h_entry_, "pthread", depth, info,
                         tskIDLE_PRIORITY + 1, &info->task);
    }
    if (ok != pdPASS) {
        vSemaphoreDelete(info->done);
        vPortFree(info);
        return -1;
    }
    *thread = (pthread_t)(uintptr_t)info;
    return 0;
}

static inline int pthread_join(pthread_t thread, void **retval)
{
    pthread_internal_t *info = (pthread_internal_t *)(uintptr_t)thread;
    if (info == NULL) {
        return -1;
    }
    /* POSIX pthread_join() has no spurious-wakeup allowance: it must not
     * proceed to tear the thread down until the thread has actually run to
     * completion and given info->done.
     *
     * On this port/config, xSemaphoreTake(..., portMAX_DELAY) is believed to
     * block unconditionally: INCLUDE_vTaskSuspend == 1 in this BSP's
     * FreeRTOSConfig.h, so prvAddCurrentTaskToDelayedList() parks a
     * portMAX_DELAY waiter on the suspended-task list rather than a
     * tick-timed delayed list -- a plain tick-based timeout can therefore
     * never fire for this wait. But INCLUDE_xTaskAbortDelay == 1 too, and
     * xTaskCheckForTimeOut() tests pxCurrentTCB->ucDelayAborted *before* its
     * portMAX_DELAY special case, so a call to xTaskAbortDelay() against the
     * joining task -- from anywhere, including code added later -- would
     * still make this take return pdFALSE with the semaphore ungiven.
     * (vTaskResume()/xTaskResumeFromISR() cannot do the same: FreeRTOS's own
     * prvTaskIsTaskSuspended() checks whether the task's event-list item is
     * linked into any list -- true here, since the take leaves it on the
     * semaphore's wait list -- and refuses to resume it.) Grepping this
     * repo's application code (i.e. everything outside vendor/kernel
     * sources) turns up no call to xTaskAbortDelay() anywhere, so this take
     * cannot return early in practice today. Retry anyway rather than
     * trusting a single take: it costs nothing on the real, single-iteration
     * path, and it is the only response consistent with pthread_join()'s
     * no-spurious-wakeup contract -- an early, non-completion return here
     * must never be treated as "the thread finished," since that leads
     * straight to vTaskDelete()-ing (and freeing the bookkeeping for) a
     * still-running task. */
    while (xSemaphoreTake(info->done, portMAX_DELAY) != pdPASS) {
        /* Not a real completion signal (see above); go back to waiting. */
    }
    if (retval != NULL) {
        *retval = NULL;
    }
    if (info->task != NULL) {
        /* Safe without suspending the thread first: it has already given
         * info->done and it touches no __thread variable between that give
         * and its permanent vTaskSuspend (pthread_x5h_entry_), so its TLS
         * storage is quiescent here. */
        x5h_emutls_task_cleanup(info->task);
        vTaskDelete(info->task);
    }
    /* Release the application-owned static TCB the kernel does not free.
     * vPortFree(NULL) is a no-op, so this is safe for dynamic-stack tasks too.
     * The TCB is freed here (alongside info) rather than in pthread_cancel,
     * which defers teardown to the matching join -- freeing it in both would
     * double-free. */
    vPortFree(info->tcb);
    vSemaphoreDelete(info->done);
    vPortFree(info);
    return 0;
}

static inline int pthread_cancel(pthread_t thread)
{
    pthread_internal_t *info = (pthread_internal_t *)(uintptr_t)thread;
    if (info == NULL || info->task == NULL) {
        return -1;
    }
    TaskHandle_t t = info->task;
    info->task = NULL;
    xSemaphoreGive(info->done);
    /* Unlike pthread_join's target, a cancelled thread may be running
     * anywhere -- including inside __emutls_get_address() replacing the
     * very array the cleanup below frees -- so park it first. It cannot be
     * holding newlib's __malloc_lock at this point (that lock is
     * vTaskSuspendAll(), under which this code could not be running), so
     * freeing its TLS storage afterwards is safe. */
    vTaskSuspend(t);
    x5h_emutls_task_cleanup(t);
    vTaskDelete(t);
    return 0;
}

// useconds_t comes from newlib's <sys/types.h>; matches the prototype in
// newlib's <unistd.h> so the C++ overload resolution sees one declaration.
static inline int usleep(useconds_t useconds)
{
    if (useconds == 0) {
        taskYIELD();
        return 0;
    }
    uint64_t ticks = ((uint64_t)useconds * (uint64_t)configTICK_RATE_HZ
                      + 999999ULL) / 1000000ULL;
    if (ticks == 0) {
        ticks = 1;
    }
    vTaskDelay((TickType_t)ticks);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif  // PLATFORM_FREERTOS_X5H_PTHREAD_H_
