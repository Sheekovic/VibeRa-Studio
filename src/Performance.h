// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <cmath>
#include <algorithm>
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
    int plays = 0;              // Clips placed for the current Pattern since it was launched/followed.
    std::uint32_t random = 1;   // Deterministic per-pad sequence for any/other.
};

inline bool validFollowAction(const juce::String &action)
{
    return action.isEmpty() || juce::StringArray{"again", "next", "previous", "first", "last", "any", "other", "stop"}
                                   .contains(action);
}
// The Pattern a Follow Action leads to (0 = stop). Order is the project's Pattern order.
inline Id followTarget(const Project &project, Id current, const juce::String &action, std::uint32_t &random)
{
    const auto &list = project.patterns;
    if (list.empty() || action == "stop")
        return 0;
    const auto at = std::find_if(list.begin(), list.end(), [&](const auto &p) { return p.id == current; });
    const auto index = at == list.end() ? 0 : int(at - list.begin()), count = int(list.size());
    random = random * 1664525u + 1013904223u;
    const auto roll = int((random >> 8) % std::uint32_t(count));
    if (action == "next")
        return list[size_t((index + 1) % count)].id;
    if (action == "previous")
        return list[size_t((index + count - 1) % count)].id;
    if (action == "first")
        return list.front().id;
    if (action == "last")
        return list.back().id;
    if (action == "any")
        return list[size_t(roll)].id;
    if (action == "other")
        return count < 2 ? current : list[size_t((index + 1 + roll % (count - 1)) % count)].id;
    return current; // again / none
}

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
        while (!pad.stopping && pad.nextBeat <= playhead + lookahead)
        {
            const auto *part = project.pattern(pad.pattern);
            if (!part)
                break;
            const double length = std::max(.25, part->length);
            out.push_back({pad.pattern, pad.lane, pad.nextBeat, length});
            pad.nextBeat += length;
            // Follow Action: after enough plays continue with another Pattern on the same lane.
            if (part->followAction.isNotEmpty() && ++pad.plays >= std::max(1, part->followAfter))
            {
                pad.plays = 0;
                const auto target = followTarget(project, pad.pattern, part->followAction, pad.random);
                if (target == 0)
                    pad.stopping = true;
                else
                    pad.pattern = target;
            }
        }
    }
    return out;
}
} // namespace vibera::performance
