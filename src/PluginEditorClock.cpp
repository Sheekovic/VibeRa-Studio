// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditorClock.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdexcept>
#include <limits>
namespace vibera
{
std::uint32_t pluginEditorMilliseconds()
{
    static const std::uint64_t frequency = []
    {
        LARGE_INTEGER value{};
        if (!QueryPerformanceFrequency(&value) || value.QuadPart <= 0 ||
            std::uint64_t(value.QuadPart) > std::numeric_limits<std::uint64_t>::max() / 1000)
            throw std::runtime_error(
                "Windows performance counter unavailable; native editor deadline cannot be established");
        return std::uint64_t(value.QuadPart);
    }();
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value) || value.QuadPart < 0)
        throw std::runtime_error("Windows performance counter failed; native editor outcome is unknown");
    const auto ticks = std::uint64_t(value.QuadPart);
    // Divide before multiplying: avoids uptime*frequency overflow. QPC's
    // boot-fixed frequency/epoch is common to all processes on this machine.
    return std::uint32_t((ticks / frequency) * 1000 + (ticks % frequency) * 1000 / frequency);
}
} // namespace vibera
