// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace vibera
{
inline int patternNumerator(const Project &project, const Pattern &pattern) noexcept
{
    return pattern.numerator == 0 ? project.numerator : pattern.numerator;
}
inline int patternDenominator(const Project &project, const Pattern &pattern) noexcept
{
    return pattern.denominator == 0 ? project.denominator : pattern.denominator;
}
inline double patternBarBeats(const Project &project, const Pattern &pattern) noexcept
{
    return patternNumerator(project, pattern) * 4. / patternDenominator(project, pattern);
}
inline std::pair<int, int> playbackMeter(const Project &project, bool patternMode, Id selected) noexcept
{
    if (patternMode)
        if (const auto *pattern = project.pattern(selected))
            return {patternNumerator(project, *pattern), patternDenominator(project, *pattern)};
    return {project.numerator, project.denominator};
}
inline void refreshAutomaticPatternLength(const Project &project, Pattern &pattern)
{
    if (!pattern.autoLength)
        return;
    const double bar = patternBarBeats(project, pattern);
    double end = bar;
    // Stored score data, including muted steps, determines extent. Generated
    // repetitions must never feed back into their own playback horizon.
    for (const auto &note : pattern.notes)
        end = std::max(end, note.beat + note.length);
    for (const auto &loop : pattern.channelLoops)
        if (loop.mode == ChannelLoopMode::fixed)
            end = std::max(end, loop.length);
    pattern.length = std::max(bar, std::ceil((end - 1.e-9) / bar) * bar);
}
} // namespace vibera
