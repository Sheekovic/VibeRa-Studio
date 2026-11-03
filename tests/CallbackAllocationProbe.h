// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <new>
// Test-executable-only instrumentation. Other threads and worker processes are outside this probe.
static thread_local bool callbackAllocationProbe = false;
static std::atomic<unsigned> callbackAllocations{0}, callbackDestructions{0};
// Control-thread fault injection for prepared-graph publication tests only.
// Disabled for normal tests/callbacks, and reset before exception unwinding.
static thread_local int preparationAllocationFailureAfter = -1;
static thread_local bool preparationAllocationFailureHit = false;
static void checkPreparationAllocationFailure()
{
    if (preparationAllocationFailureAfter < 0)
        return;
    if (preparationAllocationFailureAfter-- == 0)
    {
        preparationAllocationFailureAfter = -1;
        preparationAllocationFailureHit = true;
        throw std::bad_alloc{};
    }
}
void *operator new(std::size_t size)
{
    checkPreparationAllocationFailure();
    if (callbackAllocationProbe)
        ++callbackAllocations;
    if (auto *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void *memory) noexcept
{
    if (memory && callbackAllocationProbe)
        ++callbackDestructions;
    std::free(memory);
}
void operator delete[](void *memory) noexcept
{
    ::operator delete(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
void operator delete[](void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
void *operator new(std::size_t size, std::align_val_t alignment)
{
    checkPreparationAllocationFailure();
    if (callbackAllocationProbe)
        ++callbackAllocations;
    if (auto *memory = _aligned_malloc(size ? size : 1, size_t(alignment)))
        return memory;
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}
void operator delete(void *memory, std::align_val_t) noexcept
{
    if (memory && callbackAllocationProbe)
        ++callbackDestructions;
    _aligned_free(memory);
}
void operator delete[](void *memory, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}
void operator delete(void *memory, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}
void operator delete[](void *memory, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}
