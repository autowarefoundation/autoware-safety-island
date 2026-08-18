// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 31: per-task emulated TLS for the R-Car X5H FreeRTOS image.
//
// The pinned arm-none-eabi toolchain (13.2.Rel1) is built `Thread model:
// single`, so libgcc's emutls runtime -- the code gcc lowers every
// `__thread` access to on targets without native TLS -- serves every caller
// ONE address per variable: its __gthread_active_p() stub returns 0
// unconditionally and its __emutls_get_address() then uses a single global
// pointer in the variable's control record. Every `ddsrt_thread_local` in
// CycloneDDS (tsd_thread_state, thread_context, log_buffer,
// freelist_inner_idx in this image) is therefore shared by all FreeRTOS
// tasks instead of being per-thread -- confirmed from the linked ELF at
// branch time, twice independently (the link resolved __emutls_get_address
// to libgcc's single-thread-model copy, whose one-global-pointer path the
// disassembly showed unconditionally taken).
//
// This file replaces that runtime. It defines the two entry points the
// compiler emits calls to -- __emutls_get_address() and
// __emutls_register_common() -- in an object linked directly into both
// executables, so the linker resolves every reference here and never pulls
// libgcc's emutls.o member (including its __gthread_active_p()) into the
// link at all. Storage becomes genuinely per-task: a per-task pointer
// array hung off a dedicated FreeRTOS thread-local-storage slot.
//
// CLEAN-ROOM: written against the emulated-TLS control-record contract (a
// record of four pointer-size words { size, align, loc, templ }; loc
// zero-initialized and owned by the runtime; templ NULL meaning zero-fill),
// with the record layout verified at branch time against the __emutls_v.*
// records this toolchain emits into the linked ELF. No libgcc source
// text was consulted or copied.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "x5h_emutls.h"

// One control record per __thread variable, emitted by the compiler as
// __emutls_v.<name>; every access to the variable is compiled into
// __emutls_get_address(&__emutls_v.<name>). This runtime uses `loc` as a
// 1-based index into each task's value array; 0 means "no index assigned
// yet", which is how the compiler initializes the record.
typedef struct {
    uintptr_t size;
    uintptr_t align;
    uintptr_t loc;
    void *templ;
} x5h_emutls_control_t;

// Per-context value array. slot[i] holds the context's value block for
// index i+1, NULL until that context first touches the variable.
typedef struct {
    uintptr_t count;
    void *slot[];
} x5h_emutls_array_t;

// Slot choice: no compiled code in either image stores anything in ANY
// FreeRTOS thread-local-storage slot (tree grep, re-checkable any time:
// the only vTaskSetThreadLocalStoragePointer caller outside the kernel and
// this file is lwIP's contrib/ports/freertos/sys_arch.c, which is not
// built -- the compiled port is lwip_port/sys_arch.c). The last slot is
// used anyway,
// leaving slot 0 free for lwIP's own LWIP_NETCONN_SEM_PER_THREAD
// convention should it ever be enabled.
#define X5H_EMUTLS_TLS_SLOT (configNUM_THREAD_LOCAL_STORAGE_POINTERS - 1)
_Static_assert(X5H_EMUTLS_TLS_SLOT >= 0 &&
               X5H_EMUTLS_TLS_SLOT < configNUM_THREAD_LOCAL_STORAGE_POINTERS,
               "the x5h emutls runtime needs one FreeRTOS thread-local-"
               "storage slot (configNUM_THREAD_LOCAL_STORAGE_POINTERS in "
               "the R-Car BSP FreeRTOSConfig.h must stay >= 1)");

// Arrays grow in steps of this many slots, so the whole image today (five
// __thread variables in the diagnostic build, four in the default one)
// fits in each task's first allocation.
#define X5H_EMUTLS_GROW 8U

// Count of assigned variable indices; 1-based so that a zero `loc` word
// means unassigned. Written only under a critical section.
static uintptr_t s_emutls_next_index;

// The pre-scheduler ("bootstrap") context's array. __emutls_get_address()
// can run before vTaskStartScheduler() -- from C++ static constructors
// (SystemInit()'s __libc_init_array()) or from main()/setup_hardware() --
// when no current task exists, so this static stands in for the missing
// task slot. Values written then stay visible only to pre-scheduler code:
// every task starts with an empty slot and re-initializes each variable
// from its template on first touch. That loses nothing in this image --
// its only __thread variables are CycloneDDS's four ddsrt_thread_local
// objects (plus the diagnostic build's self-test probe), all first touched
// from tasks, since every DDS call happens in actuation_main()'s task or
// its children. Never freed.
static x5h_emutls_array_t *s_boot_array;

// The ARM_CR52 port's IRQ nesting depth (port.c): nonzero exactly while an
// interrupt handler is running.
extern uint32_t ulPortInterruptNesting;

static x5h_emutls_array_t *emutls_current_array(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return s_boot_array;
    }
    return (x5h_emutls_array_t *)
        pvTaskGetThreadLocalStoragePointer(NULL, X5H_EMUTLS_TLS_SLOT);
}

static void emutls_set_current_array(x5h_emutls_array_t *arr)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        s_boot_array = arr;
    } else {
        vTaskSetThreadLocalStoragePointer(NULL, X5H_EMUTLS_TLS_SLOT, arr);
    }
}

// Allocates and initializes one value block. Over-allocated so the returned
// pointer can honour obj->align beyond malloc's own 8-byte guarantee, and
// so malloc's raw pointer can be recorded one word below the returned
// pointer for emutls_free_value().
static void *emutls_alloc_value(const x5h_emutls_control_t *obj)
{
    uintptr_t align =
        (obj->align > sizeof(void *)) ? obj->align : sizeof(void *);
    uint8_t *raw = (uint8_t *)malloc(obj->size + align + sizeof(void *));
    uintptr_t val;

    if (raw == NULL) {
        // Same response as ddsrt_malloc()'s to a failed allocation; the
        // strong abort() in x5h_diag.c reports it on the console.
        abort();
    }
    val = ((uintptr_t)raw + sizeof(void *) + (align - 1U)) & ~(align - 1U);
    ((void **)val)[-1] = raw;
    if (obj->templ != NULL) {
        memcpy((void *)val, obj->templ, obj->size);
    } else {
        memset((void *)val, 0, obj->size);
    }
    return (void *)val;
}

static void emutls_free_value(void *val)
{
    free(((void **)val)[-1]);
}

// The routine every `__thread` access in the image resolves to. Invalid
// from interrupt context (it can take a critical section and call
// malloc()), and the port's nesting counter makes that checkable for the
// cost of one load.
void *__emutls_get_address(void *control)
{
    x5h_emutls_control_t *obj = (x5h_emutls_control_t *)control;
    x5h_emutls_array_t *arr;
    uintptr_t idx;
    void *val;

    configASSERT(ulPortInterruptNesting == 0UL);

    // Index assignment happens once per variable for the image's whole
    // life; after that this path is the plain load. A critical section
    // rather than a FreeRTOS mutex, because this must also work before the
    // scheduler starts (the CR52 port's vPortEnterCritical()/
    // vPortExitCritical() nest correctly there and leave the pre-scheduler
    // interrupt mask alone) and at any task call depth; it guards only the
    // increment-and-publish, never an allocation.
    idx = obj->loc;
    if (idx == 0U) {
        taskENTER_CRITICAL();
        idx = obj->loc;
        if (idx == 0U) {
            s_emutls_next_index++;
            idx = s_emutls_next_index;
            obj->loc = idx;
        }
        taskEXIT_CRITICAL();
    }

    arr = emutls_current_array();
    if (arr == NULL || arr->count < idx) {
        // First touch of this index by this context: grow its array. Only
        // the owning context ever reads or replaces its own array, so no
        // lock is needed here.
        uintptr_t cap =
            (idx + (X5H_EMUTLS_GROW - 1U)) & ~(uintptr_t)(X5H_EMUTLS_GROW - 1U);
        uintptr_t old_count = (arr != NULL) ? arr->count : 0U;
        x5h_emutls_array_t *grown = (x5h_emutls_array_t *)
            malloc(sizeof(*grown) + cap * sizeof(void *));

        if (grown == NULL) {
            abort();
        }
        grown->count = cap;
        if (old_count != 0U) {
            memcpy(grown->slot, arr->slot, old_count * sizeof(void *));
        }
        memset(&grown->slot[old_count], 0,
               (cap - old_count) * sizeof(void *));
        emutls_set_current_array(grown);
        free(arr);
        arr = grown;
    }

    val = arr->slot[idx - 1U];
    if (val == NULL) {
        val = emutls_alloc_value(obj);
        arr->slot[idx - 1U] = val;
    }
    return val;
}

// Emitted for common-linkage (tentative-definition) __thread variables:
// generated constructor code calls it to merge the size/align/template
// seen by different translation units into one record before any access.
// Nothing in either image references it today (no call site in either
// linked ELF at branch time; `arm-none-eabi-nm` re-checks that in
// seconds), but a
// definition must live HERE anyway: if a future object did emit the call
// and only libgcc defined it, the linker would pull libgcc's emutls.o --
// and its second, conflicting __emutls_get_address -- back into the link.
// A template is only ever adopted together with a size >= the recorded
// one, so emutls_alloc_value() can never memcpy past a template's end.
void __emutls_register_common(void *control, uintptr_t size,
                              uintptr_t align, void *templ)
{
    x5h_emutls_control_t *obj = (x5h_emutls_control_t *)control;

    if (obj->size < size) {
        obj->size = size;
        obj->templ = templ;
    } else if (obj->size == size && obj->templ == NULL) {
        obj->templ = templ;
    }
    if (obj->align < align) {
        obj->align = align;
    }
}

// Reclamation. The pthread shim (include/platform/freertos/x5h/pthread.h)
// calls this for every task it deletes, before the vTaskDelete() that can
// lead to the TCB -- where the slot lives -- being freed, and the
// diagnostic build's TLS self-test tasks (x5h_diag.c) call it on
// themselves before self-deleting. Every other task in either image lives
// until the SoC resets, with two end-of-life self-deletes that each leak
// one task's TLS storage by design: the actuation launcher after
// actuation_main() returns, and a ddsrt worker on a DDS shutdown this
// image never performs.
void x5h_emutls_task_cleanup(TaskHandle_t task)
{
    x5h_emutls_array_t *arr = (x5h_emutls_array_t *)
        pvTaskGetThreadLocalStoragePointer(task, X5H_EMUTLS_TLS_SLOT);

    if (arr == NULL) {
        return;
    }
    vTaskSetThreadLocalStoragePointer(task, X5H_EMUTLS_TLS_SLOT, NULL);
    for (uintptr_t i = 0; i < arr->count; i++) {
        if (arr->slot[i] != NULL) {
            emutls_free_value(arr->slot[i]);
        }
    }
    free(arr);
}
