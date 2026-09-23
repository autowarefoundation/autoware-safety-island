// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 31: per-task emulated-TLS runtime. The rationale and the runtime
// itself live in x5h_emutls.c; this header exports the one symbol other
// files call by name (the __emutls_* entry points are only ever called by
// compiler-generated code and need no declaration here).
//
// include/platform/freertos/x5h/pthread.h carries a duplicate of this
// prototype: the common code that includes that header does not have this
// directory on its include path. Keep the two in sync.

#ifndef X5H_EMUTLS_H
#define X5H_EMUTLS_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

// Frees `task`'s emulated-TLS value array and every value in it, and clears
// the thread-local-storage slot the array hangs off. NULL means the calling
// task. Must run before any vTaskDelete() that could lead to the TCB being
// freed (the slot lives inside the TCB), and only once the task can no
// longer touch a __thread variable -- see the call sites in pthread.h for
// the two orderings that guarantee that.
void x5h_emutls_task_cleanup(TaskHandle_t task);

#ifdef __cplusplus
}
#endif

#endif  // X5H_EMUTLS_H
