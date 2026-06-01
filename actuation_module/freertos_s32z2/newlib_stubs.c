// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Minimal newlib syscall stubs for a bare-metal FreeRTOS image.
//
// libgcc and libc reference these for abort/exit/printf paths even when the
// application doesn't deliberately use them. We give them no-op implementations
// because the firmware never returns to a host OS — abort/exit hang, file I/O
// fails, etc.

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

// C++ ABI hook: __cxa_atexit stashes the per-translation-unit dso handle so
// shared-object teardown can dispatch destructors. A bare-metal image only
// ever needs the symbol to exist.
void *__dso_handle = (void *)0;

// _close, _fstat, _isatty, _lseek, _read, _kill, _getpid, _write are
// implemented in board_init.c (UART-routed console + EBADF stubs).

void _exit(int status)
{
    (void)status;
    for (;;) {}
}

int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; errno = ENOENT; return -1; }

// arm-none-eabi libstdc++ (13.2) builds std::chrono::system_clock AND
// steady_clock on gettimeofday (_GLIBCXX_USE_CLOCK_REALTIME/MONOTONIC are
// undefined, _GLIBCXX_USE_GETTIMEOFDAY=1), so Clock::now(), the periodic Timer,
// and the logger timestamp all land here -- NOT in clock_gettime below. Leaving
// *tv unfilled gave garbage/negative times (e.g. "[12:29:03.-87]"). Derive it
// from the tick count like clock_gettime so the two agree.
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv == NULL) {
        errno = EFAULT;
        return -1;
    }
    TickType_t ticks = xTaskGetTickCount();
    uint64_t total_us = (uint64_t)ticks * 1000000U / (uint64_t)configTICK_RATE_HZ;
    tv->tv_sec = (time_t)(total_us / 1000000U);
    tv->tv_usec = (long)(total_us % 1000000U);
    return 0;
}

// Tiny static heap for any incidental newlib malloc (FreeRTOS code uses
// pvPortMalloc + heap_4, so this just covers stragglers like libc internals).
#define NEWLIB_HEAP_SIZE (16 * 1024)
static char newlib_heap[NEWLIB_HEAP_SIZE];

void *_sbrk(intptr_t incr)
{
    static char *brk = newlib_heap;
    char *prev = brk;
    if (brk + incr > newlib_heap + NEWLIB_HEAP_SIZE) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

// POSIX sleep — main.cpp uses it for the DHCP startup delay. Implemented in
// terms of vTaskDelay so we don't block the scheduler.
unsigned int sleep(unsigned int seconds)
{
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000U));
    return 0;
}

// CycloneDDS' ddsrt_gethostname stringifies whatever this returns into the
// participant's "hostname" field; the host name has no operational meaning
// on the safety island.
int gethostname(char *name, size_t len)
{
    if (name == NULL || len == 0) {
        errno = EINVAL;
        return -1;
    }
    const char *id = "s32z2";
    strncpy(name, id, len);
    name[len - 1] = '\0';
    return 0;
}

// CycloneDDS uses clock_gettime for wall-clock/monotonic readings. xTaskGetTickCount
// is the only clock we have before SNTP runs, so derive both from it.
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
