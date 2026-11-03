// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace vibera
{
struct NoteRepeatRate
{
    int mode;
    const char *label;
    double intervalBeats;
};
inline const NoteRepeatRate *noteRepeatRate(int mode) noexcept
{
    static constexpr std::array<NoteRepeatRate, 15> rates{{{0, "Off", 0},
                                                           {1, "1/4", 1},
                                                           {2, "1/8 dotted", .75},
                                                           {3, "1/4 triplet", 2. / 3},
                                                           {4, "1/8", .5},
                                                           {5, "1/16 dotted", .375},
                                                           {6, "1/8 triplet", 1. / 3},
                                                           {7, "1/16", .25},
                                                           {8, "1/32 dotted", .1875},
                                                           {9, "1/16 triplet", 1. / 6},
                                                           {10, "1/32", .125},
                                                           {11, "1/64 dotted", .09375},
                                                           {12, "1/32 triplet", 1. / 12},
                                                           {13, "1/64", .0625},
                                                           {14, "1/64 triplet", 1. / 24}}};
    return mode >= 0 && mode < int(rates.size()) ? &rates[size_t(mode)] : nullptr;
}
inline double noteRepeatInterval(int mode) noexcept
{
    const auto *rate = noteRepeatRate(mode);
    return rate ? rate->intervalBeats : 0;
}
inline double noteRepeatOffset(int mode, std::uint32_t ordinal) noexcept
{
    return noteRepeatInterval(mode) * double(ordinal);
}
inline double noteRepeatGateDuration(int mode, double duration, std::uint32_t ordinal) noexcept
{
    return mode == 0 ? duration
                     : std::min(noteRepeatInterval(mode), duration - noteRepeatOffset(mode, ordinal));
}
struct NoteRepeatWindow
{
    std::uint32_t first = 0, count = 0;
};
// Original pure CONTROL preparation math. Onsets are origin+k*interval and
// k*interval<duration, within [windowStart,windowEnd). No iteration over hidden
// triggers; first is the stable source ordinal, never a renumbered visible row.
// False leaves output untouched; callers reject invalid/unrepresentable plans.
inline bool noteRepeatVisibleWindow(int mode, double origin, double duration, double windowStart,
                                    double windowEnd, NoteRepeatWindow &output) noexcept
{
    const auto *rate = noteRepeatRate(mode);
    if (!rate || !std::isfinite(origin) || !std::isfinite(duration) || !std::isfinite(windowStart) ||
        !std::isfinite(windowEnd) || origin < 0 || duration <= 0 || windowStart < 0 ||
        windowEnd < windowStart)
        return false;
    NoteRepeatWindow next;
    if (windowStart == windowEnd || origin >= windowEnd || windowStart - origin >= duration)
    {
        output = next;
        return true;
    }
    if (mode == 0)
    {
        next.count = origin >= windowStart ? 1 : 0;
        output = next;
        return true;
    }
    const auto interval = rate->intervalBeats;
    const double firstIndex = std::ceil(std::max(0., windowStart - origin) / interval);
    const double endIndex = std::ceil(std::min(duration, windowEnd - origin) / interval);
    constexpr std::uint64_t ordinalEnd = std::uint64_t(std::numeric_limits<std::uint32_t>::max()) + 1;
    if (!std::isfinite(firstIndex) || !std::isfinite(endIndex) || firstIndex < 0 || endIndex < 0 ||
        firstIndex > double(ordinalEnd) || endIndex > double(ordinalEnd))
        return false;
    auto first = std::uint64_t(firstIndex), end = std::uint64_t(endIndex);
    const auto at = [&](std::uint64_t k) { return origin + double(k) * interval; };
    const auto insideEnd = [&](std::uint64_t k)
    { return double(k) * interval < duration && at(k) < windowEnd; };
    // Correct only floating division/addition boundary rounding; never scan a
    // potentially enormous hidden prefix. Two neighboring candidates suffice.
    for (int i = 0; i < 2 && first > 0 && at(first - 1) >= windowStart; ++i)
        --first;
    for (int i = 0; i < 2 && first < ordinalEnd && at(first) < windowStart; ++i)
        ++first;
    for (int i = 0; i < 2 && end > 0 && !insideEnd(end - 1); ++i)
        --end;
    for (int i = 0; i < 2 && end < ordinalEnd && insideEnd(end); ++i)
        ++end;
    if ((first > 0 && at(first - 1) >= windowStart) || (first < ordinalEnd && at(first) < windowStart) ||
        (end > 0 && !insideEnd(end - 1)) || (end < ordinalEnd && insideEnd(end)))
        return false;
    if (first >= end)
    {
        output = next;
        return true;
    }
    if (first >= ordinalEnd || end - first > std::numeric_limits<std::uint32_t>::max())
        return false;
    next.first = std::uint32_t(first);
    next.count = std::uint32_t(end - first);
    output = next;
    return true;
}
} // namespace vibera
