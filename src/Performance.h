// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <cmath>
#include <map>

// Performance mode: launched Patterns are written into the Song as clips at the next
// launch boundary (one bar by default) and re-placed every time they end, until stopped,
// so a live performance is recorded straight into the arrangement.
namespace vibera::performance
{
constexpr int firstLane = 64; // Performance zone: launched Patterns get their own lanes from here.

struct Pad
{
    Id pattern = 0;
    int lane = firstLane;
    double nextBeat = 0; // Where the next clip starts.
    bool stopping = false;
};

// Next launch boundary at or after `beat` for a quantum in beats (0 = immediately).
inline double nextBoundary(double beat, double quantum) noexcept
{
    if (quantum <= 0)
        return beat;
    return std::ceil(beat / quantum - 1.e-9) * quantum;
}

// Clips due within `lookahead` beats of the playhead, advancing each pad. Returns
// (pad pattern, lane, beat, length) for each clip to place.
struct Due
{
    Id pattern;
    int lane;
    double beat, length;
};
inline std::vector<Due> due(std::map<Id, Pad> &pads, const Project &project, double playhead, double lookahead)
{
    std::vector<Due> out;
    for (auto &[id, pad] : pads)
    {
        const auto *part = project.pattern(pad.pattern);
        if (!part || pad.stopping)
            continue;
        const double length = std::max(.25, part->length);
        while (pad.nextBeat <= playhead + lookahead)
        {
            out.push_back({pad.pattern, pad.lane, pad.nextBeat, length});
            pad.nextBeat += length;
        }
    }
    return out;
}
} // namespace vibera::performance
