// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Route C++ dynamic allocation to the FreeRTOS heap_4 pool (pvPortMalloc),
// NOT newlib's malloc.
//
// arm-none-eabi libstdc++'s default operator new calls newlib malloc, which is
// backed by _sbrk + the tiny 16 KiB newlib_heap in newlib_stubs.c. The Autoware
// controller allocates far more than that during construction (e.g.
// TrajectoryMsg reserves a ~22 KiB std::vector<TrajectoryPoint>), so the default
// operator new threw std::bad_alloc -> std::terminate -> abort while the 3 MiB
// ucHeap sat unused. Overriding the global operator new/delete here points all
// C++ allocation at the same heap_4 pool CycloneDDS already uses, so there is a
// single 3 MiB heap instead of two. heap_4's pvPortMalloc is task-safe (it
// suspends the scheduler / takes a critical section internally).

#include <cstddef>
#include <new>

#include "FreeRTOS.h"

void *operator new(std::size_t size)
{
    void *p = pvPortMalloc(size != 0U ? size : 1U);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return pvPortMalloc(size != 0U ? size : 1U);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return pvPortMalloc(size != 0U ? size : 1U);
}

void operator delete(void *p) noexcept
{
    vPortFree(p);
}

void operator delete[](void *p) noexcept
{
    vPortFree(p);
}

// Sized deletes (C++14): the compiler may emit these instead of the unsized
// forms. heap_4 tracks the block size itself, so the hint is ignored.
void operator delete(void *p, std::size_t) noexcept
{
    vPortFree(p);
}

void operator delete[](void *p, std::size_t) noexcept
{
    vPortFree(p);
}

void operator delete(void *p, const std::nothrow_t &) noexcept
{
    vPortFree(p);
}

void operator delete[](void *p, const std::nothrow_t &) noexcept
{
    vPortFree(p);
}
