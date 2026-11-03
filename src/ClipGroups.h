// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <map>
#include <set>
#include <unordered_set>

namespace vibera
{
// Message/model thread only. No group traversal or allocation belongs in DSP.
inline std::set<Id> projectClipIds(const Project &p)
{
    std::set<Id> ids;
    for (const auto &c : p.placements)
        ids.insert(c.id);
    for (const auto &c : p.audioPlacements)
        ids.insert(c.id);
    for (const auto &c : p.automation.placements)
        ids.insert(c.id);
    return ids;
}
inline std::vector<Id> expandedClipSelection(const Project &p, const std::vector<Id> &seeds)
{
    if (p.clipGroups.empty())
        return seeds;
    std::vector<Id> result;
    std::unordered_set<Id> seen;
    for (auto id : seeds)
        if (seen.insert(id).second)
            result.push_back(id);
    for (const auto &group : p.clipGroups)
        if (std::any_of(group.members.begin(), group.members.end(), [&](Id id) { return seen.contains(id); }))
            for (auto id : group.members)
                if (seen.insert(id).second)
                    result.push_back(id);
    return result;
}
inline void pruneClipGroups(Project &p)
{
    const auto valid = projectClipIds(p);
    for (auto &g : p.clipGroups)
        std::erase_if(g.members, [&](Id id) { return !valid.contains(id); });
    std::erase_if(p.clipGroups, [](const auto &g) { return g.members.size() < 2; });
}
inline juce::Result changeClipGroup(Project &p, const std::vector<Id> &selection, bool create)
{
    const auto valid = projectClipIds(p);
    std::set<Id> selected;
    for (auto id : selection)
        if (!valid.contains(id) || !selected.insert(id).second)
            return juce::Result::fail("Select existing distinct arrangement Clips");
    if (selected.size() < (create ? 2u : 1u))
        return juce::Result::fail(create ? "Select at least two Clips to group" : "Select a Clip to ungroup");
    if (!create)
    {
        std::erase_if(p.clipGroups,
                      [&](const auto &g)
                      {
                          return std::any_of(g.members.begin(), g.members.end(),
                                             [&](Id id) { return selected.contains(id); });
                      });
        return juce::Result::ok();
    }
    for (const auto &g : p.clipGroups)
        if (std::set<Id>(g.members.begin(), g.members.end()) == selected)
            return juce::Result::ok();
    for (auto &g : p.clipGroups)
        std::erase_if(g.members, [&](Id id) { return selected.contains(id); });
    std::erase_if(p.clipGroups, [](const auto &g) { return g.members.size() < 2; });
    p.clipGroups.push_back({p.allocate(), {selected.begin(), selected.end()}});
    return juce::Result::ok();
}
inline void duplicateClipGroups(Project &p, const std::map<Id, Id> &copies)
{
    const auto original = p.clipGroups;
    for (const auto &g : original)
    {
        std::vector<Id> members;
        for (auto id : g.members)
            if (auto it = copies.find(id); it != copies.end())
                members.push_back(it->second);
        if (members.size() > 1)
            p.clipGroups.push_back({p.allocate(), std::move(members)});
    }
}
inline void extendSlicedClipGroups(Project &p, const std::map<Id, Id> &copies)
{
    for (auto &g : p.clipGroups)
    {
        const auto original = g.members;
        for (auto id : original)
            if (auto it = copies.find(id); it != copies.end())
                g.members.push_back(it->second);
    }
}
inline juce::Result moveClipSelection(Project &p, const std::vector<Id> &targets, double beat, int lane,
                                      double length)
{
    if (!std::isfinite(beat) || !std::isfinite(length) || std::abs(beat) > 1000000 ||
        std::abs(length) > 1000000 || std::abs(double(lane)) > 127)
        return juce::Result::fail(
            "Clip movement requires finite beat/length deltas within1000000 and lane delta-127..127");
    const std::set<Id> selected(targets.begin(), targets.end());
    if (selected.empty())
        return juce::Result::ok();
    double minimumLength = -1000000, maximumLength = 1000000;
    auto boundLength = [&](double current, double minimum, double maximum)
    {
        minimumLength = std::max(minimumLength, minimum - current);
        maximumLength = std::min(maximumLength, maximum - current);
    };
    for (const auto &c : p.placements)
        if (selected.contains(c.id))
            boundLength(c.length, std::min(.25, c.length), std::min(4096., 1000000 - c.beat));
    for (const auto &c : p.audioPlacements)
        if (selected.contains(c.id))
        {
            const auto *source = p.channel(c.channel);
            if (!source)
                return juce::Result::fail("Unavailable Audio Clip source");
            boundLength(p.audioEndBeat(c.beat, c.duration) - c.beat,
                        std::min(.000001, c.duration) * p.songTempo().bpmAt(c.beat) / 60.,
                        p.audioEndBeat(c.beat, source->audioDuration - c.offset) - c.beat);
        }
    for (const auto &c : p.automation.placements)
        if (selected.contains(c.id))
        {
            const auto *source = p.automationSource(c.source);
            if (!source)
                return juce::Result::fail("Unavailable Automation Clip source");
            boundLength(c.length, std::min(.000001, c.length), source->length - c.offset);
        }
    if (minimumLength > maximumLength)
        return juce::Result::fail("Incompatible selected Clip bounds");
    const auto duration = juce::jlimit(minimumLength, maximumLength, length);
    double minimumBeat = -1000000, maximumBeat = 1000000;
    int minimumLane = -127, maximumLane = 127;
    auto boundPosition = [&](double position, double extent, int row)
    {
        minimumBeat = std::max(minimumBeat, -position);
        maximumBeat = std::min(maximumBeat, 1000000 - position - extent - duration);
        minimumLane = std::max(minimumLane, -row);
        maximumLane = std::min(maximumLane, 127 - row);
    };
    for (const auto &c : p.placements)
        if (selected.contains(c.id))
            boundPosition(c.beat, c.length, c.lane);
    for (const auto &c : p.audioPlacements)
        if (selected.contains(c.id))
            boundPosition(c.beat, p.audioEndBeat(c.beat, c.duration) - c.beat, c.lane);
    for (const auto &c : p.automation.placements)
        if (selected.contains(c.id))
            boundPosition(c.beat, c.length, c.lane);
    if (minimumBeat > maximumBeat || minimumLane > maximumLane)
        return juce::Result::fail("Incompatible selected Clip positions");
    const auto delta = juce::jlimit(minimumBeat, maximumBeat, beat);
    const auto rows = juce::jlimit(minimumLane, maximumLane, lane);
    for (auto &c : p.placements)
        if (selected.contains(c.id))
        {
            c.beat += delta;
            c.lane += rows;
            c.length += duration;
        }
    for (auto &c : p.audioPlacements)
        if (selected.contains(c.id))
        {
            c.beat += delta;
            c.lane += rows;
            c.duration = p.songSeconds(p.audioEndBeat(c.beat, c.duration) + duration) - p.songSeconds(c.beat);
        }
    for (auto &c : p.automation.placements)
        if (selected.contains(c.id))
        {
            c.beat += delta;
            c.lane += rows;
            c.length += duration;
        }
    return juce::Result::ok();
}
} // namespace vibera
