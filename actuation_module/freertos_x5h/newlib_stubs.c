// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Newlib syscall stubs not already covered by --specs=nosys.specs (-lnosys
// already supplies default _exit/_read/_write/_close/_fstat/_isatty/_lseek/
// _kill/_getpid/_sbrk stubs for this target -- see
// rcar_bsp/.../CMakeLists.txt's own add_link_options()). Ported from
// freertos_s32z2/newlib_stubs.c, trimmed to only what x5h's link actually
// needs: gethostname and clock_gettime. Unlike S32Z2's NXP RTD toolchain
// build of libstdc++ (which uses gettimeofday for std::chrono, per that
// file's own comment), this target's vanilla Arm GNU Toolchain 13.2.Rel1
// libstdc++ build calls clock_gettime directly for
// std::chrono::steady_clock::now()/system_clock::now() -- confirmed via the
// undefined-reference set at link time (clock_gettime and both *_clock::now()
// symbols, but neither gettimeofday nor _gettimeofday).

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

// CycloneDDS' ddsrt_gethostname stringifies whatever this returns into the
// participant's "hostname" field; the host name has no operational meaning
// on the safety island.
int gethostname(char *name, size_t len)
{
    if (name == NULL || len == 0) {
        errno = EINVAL;
        return -1;
    }
    const char *id = "x5h";
    strncpy(name, id, len);
    name[len - 1] = '\0';
    return 0;
}

// std::chrono::steady_clock::now()/system_clock::now() (and CycloneDDS'
// own wall-clock/monotonic readings) land here. xTaskGetTickCount is the
// only clock available before network time sync runs (Task 6).
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    (void)clk_id;
    if (tp == NULL) {
        errno = EFAULT;
        return -1;
    }
    TickType_t ticks = xTaskGetTickCount();
    uint64_t total_ms = (uint64_t)ticks * 1000U / (uint64_t)configTICK_RATE_HZ;
    tp->tv_sec = (time_t)(total_ms / 1000U);
    tp->tv_nsec = (long)((total_ms % 1000U) * 1000000U);
    return 0;
}
