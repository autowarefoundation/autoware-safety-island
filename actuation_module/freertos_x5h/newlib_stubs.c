// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Newlib syscall stubs not already covered by --specs=nosys.specs (-lnosys
// already supplies default _exit/_read/_write/_close/_fstat/_isatty/_lseek/
// _kill/_getpid/_sbrk stubs for this target -- see
// rcar_bsp/.../CMakeLists.txt's own add_link_options()). Ported from
// freertos_s32z2/newlib_stubs.c, trimmed to only what x5h's link actually
// needs: gethostname, clock_gettime and _gettimeofday.
//
// On this target's vanilla Arm GNU Toolchain 13.2.Rel1 libstdc++ build,
// std::chrono::system_clock::now() and steady_clock::now() both call
// gettimeofday(), not clock_gettime() directly -- verified by disassembling
// the linked ELF and reading the bl target inside each *_clock::now(), not
// by the undefined-reference set at link time: gettimeofday is defined by
// libc and _gettimeofday by libnosys, so neither can ever appear as an
// *undefined* reference regardless of whether anything calls it, and their
// absence from that set proves nothing. clock_gettime is reached
// separately, by CycloneDDS' own dds_time().

#include <errno.h>
#include <string.h>
#include <sys/time.h>
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

// Shared tick->time conversion for clock_gettime and _gettimeofday below.
// Both clocks must be built from the same source (xTaskGetTickCount, the
// only clock available before network time sync runs -- Task 6) via the
// same arithmetic, or the two will silently drift apart from each other.
static uint64_t ticks_to_ms_(TickType_t ticks)
{
    return (uint64_t)ticks * 1000U / (uint64_t)configTICK_RATE_HZ;
}

// std::chrono::steady_clock::now()/system_clock::now() call gettimeofday()
// on this target (see file header), and CycloneDDS' own wall-clock/
// monotonic readings call clock_gettime() -- both land here.
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    (void)clk_id;
    if (tp == NULL) {
        errno = EFAULT;
        return -1;
    }
    uint64_t total_ms = ticks_to_ms_(xTaskGetTickCount());
    tp->tv_sec = (time_t)(total_ms / 1000U);
    tp->tv_nsec = (long)((total_ms % 1000U) * 1000000U);
    return 0;
}

// std::chrono::system_clock::now()/steady_clock::now() call gettimeofday(),
// a thin newlib wrapper that calls _gettimeofday_r, which calls this.
// Override _gettimeofday itself, not gettimeofday or _gettimeofday_r: the
// libnosys archive member for _gettimeofday is only pulled in when
// _gettimeofday is still undefined at link time, so a strong definition in
// our own object means that member (which writes nothing to *tp and
// returns -ENOSYS) is never linked in at all. Same mechanism Task 19 used
// for ddsrt_getprocessname/ddsrt_getpid on this target.
int _gettimeofday(struct timeval *tp, void *tzp)
{
    (void)tzp;
    if (tp == NULL) {
        errno = EFAULT;
        return -1;
    }
    uint64_t total_ms = ticks_to_ms_(xTaskGetTickCount());
    tp->tv_sec = (time_t)(total_ms / 1000U);
    tp->tv_usec = (suseconds_t)((total_ms % 1000U) * 1000U);
    return 0;
}
