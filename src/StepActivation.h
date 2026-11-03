// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vibera::step_memory
{
// Original CONTROL-only score operation. Templates bind the real Note/vector
// without copying their non-activation fields or owning any audio resources.
enum class Retention
{
    discard,
    mute
};
enum class Error
{
    none,
    invalidChannel,
    invalidWindow,
    invalidBeat,
    invalidPeriod,
    invalidPolicy
};
struct Cell
{
    std::int64_t channel = 0;
    double beat = 0;
    Error error = Error::none;
};
inline Cell resolveCell(std::int64_t channel, double visibleBeat, double window, double period = 0) noexcept
{
    if (channel < 1)
        return {0, 0, Error::invalidChannel};
    if (!std::isfinite(window) || window <= 0 || window > 1000000)
        return {0, 0, Error::invalidWindow};
    if (!std::isfinite(visibleBeat) || visibleBeat < 0 || visibleBeat >= window ||
        std::abs(visibleBeat * 4 - std::round(visibleBeat * 4)) > .00001)
        return {0, 0, Error::invalidBeat};
    if (!std::isfinite(period) || period < 0 ||
        (period != 0 &&
         (period < .25 || period > 1000000 || std::abs(period * 4 - std::round(period * 4)) > .00001)))
        return {0, 0, Error::invalidPeriod};
    // Normalize once. A repeated visible cell still refers to its one source
    // chord, not to a duplicate score event or synthetic occurrence identity.
    return {channel, period > 0 ? std::fmod(visibleBeat, period) : visibleBeat, Error::none};
}
template <class Note> bool matches(const Note &note, const Cell &cell) noexcept
{
    return cell.error == Error::none && note.channel == cell.channel && note.step &&
           std::abs(note.beat - cell.beat) < .00001;
}
struct Result
{
    Error error = Error::none;
    bool changed = false;
    bool needsNote = false;
    std::size_t active = 0, inactive = 0;
};
template <class Notes> Result inspect(const Notes &notes, const Cell &cell) noexcept
{
    Result result;
    result.error = cell.error;
    if (result.error == Error::none && cell.channel < 1)
        result.error = Error::invalidChannel;
    if (result.error == Error::none && (!std::isfinite(cell.beat) || cell.beat < 0 || cell.beat >= 1000000 ||
                                        std::abs(cell.beat * 4 - std::round(cell.beat * 4)) > .00001))
        result.error = Error::invalidBeat;
    if (result.error != Error::none)
        return result;
    for (const auto &note : notes)
        if (matches(note, cell))
        {
            if (note.muted)
                ++result.inactive;
            else
                ++result.active;
        }
    return result;
}
template <class Notes> Result set(Notes &notes, const Cell &cell, bool enabled, Retention retention)
{
    auto before = inspect(notes, cell);
    if (before.error != Error::none)
        return before;
    if (retention != Retention::discard && retention != Retention::mute)
        return {Error::invalidPolicy};
    if (enabled && before.active + before.inactive == 0)
    {
        // The canonical caller validates capacity/ID admission, allocates exactly
        // one ordinary default Note, then commits one candidate/history action.
        // No mutation/identity consumption occurs in this helper on this path.
        before.needsNote = true;
        return before;
    }
    if (!enabled && retention == Retention::discard)
    {
        const auto count = std::erase_if(notes, [&](const auto &note) { return matches(note, cell); });
        return {Error::none, count != 0, false, 0, 0};
    }
    bool changed = false;
    for (auto &note : notes)
        if (matches(note, cell) && note.muted == enabled)
        {
            note.muted = !enabled;
            changed = true;
        }
    auto after = inspect(notes, cell);
    after.changed = changed;
    return after;
}
} // namespace vibera::step_memory
