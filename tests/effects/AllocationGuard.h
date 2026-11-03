// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <new>

namespace effects_test
{
inline thread_local bool callback = false;
inline thread_local unsigned allocations = 0, destructions = 0;
struct CallbackScope
{
    CallbackScope()
    {
        callback = true;
    }
    ~CallbackScope()
    {
        callback = false;
    }
};
} // namespace effects_test
void *operator new(std::size_t size)
{
    if (effects_test::callback)
        ++effects_test::allocations;
    if (auto *result = std::malloc(size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void *value) noexcept
{
    if (value && effects_test::callback)
        ++effects_test::destructions;
    std::free(value);
}
void operator delete[](void *value) noexcept
{
    ::operator delete(value);
}
void operator delete(void *value, std::size_t) noexcept
{
    ::operator delete(value);
}
void operator delete[](void *value, std::size_t) noexcept
{
    ::operator delete(value);
}
