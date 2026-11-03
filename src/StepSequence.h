// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <cmath>
namespace vibera
{
// Explicit Pattern length, in quarter-note beats. No display-width/64-step cap.
inline int stepSequenceSize(const Pattern &pattern)
{
    return std::max(1, int(std::ceil(pattern.length * 4.)));
}
// Dormant notes beyond the explicit loop remain editable in Piano Roll, but do
// not replace the current loop's step grid with a melody strip.
inline bool stepSequenceIsMelodic(const Pattern &pattern, Id channel)
{
    return std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto &note)
                       { return note.channel == channel && !note.step && note.beat < pattern.length; });
}
} // namespace vibera
