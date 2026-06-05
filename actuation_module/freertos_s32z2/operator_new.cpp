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
#include <new>

#include "FreeRTOS.h"

// ---- Over-aligned allocation support (C++17) ----
// pvPortMalloc only guarantees portBYTE_ALIGNMENT (8 bytes), but C++17
// over-aligned types — e.g. Eigen objects declared with
// EIGEN_MAKE_ALIGNED_OPERATOR_NEW — route through the align_val_t operator new
// forms and need 16/32/64-byte alignment. Without the overloads below,
// libstdc++ falls back to newlib malloc and its tiny 16 KiB heap (the exact
// failure the unaligned overrides above exist to avoid). We over-allocate from
// the same heap_4 pool and stash the real base pointer in the word just below
// the aligned block so the matching aligned delete can recover it.
namespace {
void *aligned_alloc_from_heap(std::size_t size, std::size_t alignment) noexcept
{
    if (alignment < alignof(std::max_align_t)) {
        alignment = alignof(std::max_align_t);
    }
    // Room for the worst-case alignment shift plus a back-pointer word.
    void *base = pvPortMalloc(size + alignment + sizeof(void *));
    if (base == nullptr) {
        return nullptr;
    }
    auto raw = reinterpret_cast<std::uintptr_t>(base) + sizeof(void *);
    auto aligned = (raw + (alignment - 1U)) & ~static_cast<std::uintptr_t>(alignment - 1U);
    reinterpret_cast<void **>(aligned)[-1] = base;
    return reinterpret_cast<void *>(aligned);
}

void aligned_free_to_heap(void *p) noexcept
{
    if (p != nullptr) {
        vPortFree(reinterpret_cast<void **>(p)[-1]);
    }
}
}  // namespace

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

// ---- Over-aligned operator new/delete (C++17, std::align_val_t) ----
// The throwing forms mirror the unaligned operator new above (throw on OOM).
// Whether to keep throwing on this target — where .ARM.exidx is an empty range
// so unwinding cannot run — is being decided separately; if that resolves to a
// non-throwing/abort policy, both these and the unaligned forms change together.
void *operator new(std::size_t size, std::align_val_t al)
{
    void *p = aligned_alloc_from_heap(size != 0U ? size : 1U, static_cast<std::size_t>(al));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void *operator new[](std::size_t size, std::align_val_t al)
{
    return ::operator new(size, al);
}

void *operator new(std::size_t size, std::align_val_t al, const std::nothrow_t &) noexcept
{
    return aligned_alloc_from_heap(size != 0U ? size : 1U, static_cast<std::size_t>(al));
}

void *operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t &) noexcept
{
    return aligned_alloc_from_heap(size != 0U ? size : 1U, static_cast<std::size_t>(al));
}

void operator delete(void *p, std::align_val_t) noexcept
{
    aligned_free_to_heap(p);
}

void operator delete[](void *p, std::align_val_t) noexcept
{
    aligned_free_to_heap(p);
}

void operator delete(void *p, std::size_t, std::align_val_t) noexcept
{
    aligned_free_to_heap(p);
}

void operator delete[](void *p, std::size_t, std::align_val_t) noexcept
{
    aligned_free_to_heap(p);
}

void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept
{
    aligned_free_to_heap(p);
}

void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept
{
    aligned_free_to_heap(p);
}
