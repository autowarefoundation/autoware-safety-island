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
#include <cstdint>
#include <cstring>
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

// ---- C malloc family ----
// The operator new/delete overrides above only cover C++ `new`. Eigen's
// aligned_malloc() (and any other std::malloc user) calls the C library malloc,
// which on arm-none-eabi newlib is backed by _sbrk + the 16 KiB newlib_heap in
// newlib_stubs.c. MPC::generateMPCMatrix() allocates Eigen matrices that exceed
// 16 KiB, so malloc returned NULL -> Eigen threw std::bad_alloc -> std::terminate
// -> abort -> _exit. That parked the controller thread in _exit while the network
// threads kept running, so the board still answered ICMP but never published a
// control_cmd. Route the C malloc family at the same 3 MiB heap_4 pool as
// operator new. An 8-byte header records the request size (for realloc) and keeps
// the returned pointer 8-byte aligned, since pvPortMalloc is 8-byte aligned.
namespace {
constexpr std::size_t kMallocHeader = 8U;
}

extern "C" {

void *malloc(std::size_t size)
{
    if (size > SIZE_MAX - kMallocHeader) {
        return nullptr;  // size + kMallocHeader would wrap; reject rather than under-allocate
    }
    void *raw = pvPortMalloc(size + kMallocHeader);
    if (raw == nullptr) {
        return nullptr;
    }
    *static_cast<std::size_t *>(raw) = size;
    return static_cast<char *>(raw) + kMallocHeader;
}

void free(void *ptr)
{
    if (ptr != nullptr) {
        vPortFree(static_cast<char *>(ptr) - kMallocHeader);
    }
}

void *calloc(std::size_t nmemb, std::size_t size)
{
    const std::size_t total = nmemb * size;
    if (size != 0U && total / size != nmemb) {
        return nullptr;  // multiplication overflow
    }
    void *p = malloc(total);
    if (p != nullptr) {
        std::memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, std::size_t size)
{
    if (ptr == nullptr) {
        return malloc(size);
    }
    if (size == 0U) {
        free(ptr);
        return nullptr;
    }
    const std::size_t old_size = *reinterpret_cast<std::size_t *>(static_cast<char *>(ptr) - kMallocHeader);
    void *np = malloc(size);
    if (np != nullptr) {
        std::memcpy(np, ptr, old_size < size ? old_size : size);
        free(ptr);
    }
    return np;
}

}  // extern "C"
