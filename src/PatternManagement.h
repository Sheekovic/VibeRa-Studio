// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <set>

// Pure model helpers for bulk Pattern management and Pattern-only rendering.
// "Empty" means no notes and no Channel loops. "Unplaced" means no Playlist
// placement references the Pattern. Every helper keeps at least one Pattern.
namespace vibera::pattern_management
{
inline constexpr std::size_t maximumPatterns = 1024;

inline bool empty(const Pattern &p)
{
    return p.notes.empty() && p.channelLoops.empty();
}
inline bool placed(const Project &project, Id pattern)
{
    return std::any_of(project.placements.begin(), project.placements.end(),
                       [&](const auto &clip) { return clip.pattern == pattern; });
}
inline juce::Result uniqueExisting(const Project &project, const std::vector<Id> &ids)
{
    if (ids.empty() || ids.size() > maximumPatterns)
        return juce::Result::fail("patternBulk.invalidSelection: Select 1..1024 Patterns");
    std::set<Id> seen;
    for (const auto id : ids)
        if (!project.pattern(id) || !seen.insert(id).second)
            return juce::Result::fail("patternBulk.invalidSelection: Unknown or duplicate Pattern ID");
    return juce::Result::ok();
}
// Each independent copy (new Pattern and note IDs) follows its own source.
inline juce::Result clone(Project &project, const std::vector<Id> &ids, std::vector<Id> *created = nullptr)
{
    if (auto valid = uniqueExisting(project, ids); valid.failed())
        return valid;
    if (project.patterns.size() + ids.size() > maximumPatterns)
        return juce::Result::fail("patternBulk.limit: Clone would exceed 1024 Patterns");
    for (const auto id : ids)
    {
        const auto index = std::distance(project.patterns.data(), project.pattern(id));
        auto copy = project.patterns[size_t(index)];
        copy.id = project.allocate();
        copy.name += " copy";
        for (auto &note : copy.notes)
            note.id = project.allocate();
        if (created)
            created->push_back(copy.id);
        project.patterns.insert(project.patterns.begin() + index + 1, std::move(copy));
    }
    return juce::Result::ok();
}
// Removes the Patterns and every Playlist placement/clip-group membership of them.
inline void erase(Project &project, const std::set<Id> &targets)
{
    std::set<Id> removedClips;
    for (const auto &clip : project.placements)
        if (targets.count(clip.pattern))
            removedClips.insert(clip.id);
    std::erase_if(project.placements, [&](const auto &clip) { return targets.count(clip.pattern) != 0; });
    for (auto &group : project.clipGroups)
        std::erase_if(group.members, [&](Id member) { return removedClips.count(member) != 0; });
    std::erase_if(project.clipGroups, [](const auto &group) { return group.members.size() < 2; });
    std::erase_if(project.patterns, [&](const auto &p) { return targets.count(p.id) != 0; });
    if (project.patterns.empty())
        project.patterns.push_back(
            {project.allocate(), "Pattern 1", project.numerator * 4. / project.denominator, {}});
}
inline juce::Result remove(Project &project, const std::vector<Id> &ids)
{
    if (auto valid = uniqueExisting(project, ids); valid.failed())
        return valid;
    erase(project, {ids.begin(), ids.end()});
    return juce::Result::ok();
}
// mode "empty": empty AND unplaced. mode "unplaced": every unplaced Pattern.
inline juce::Result purge(Project &project, const juce::String &mode, std::vector<Id> *removed = nullptr)
{
    if (mode != "empty" && mode != "unplaced")
        return juce::Result::fail("patternPurge.invalidMode: Choose empty or unplaced");
    std::set<Id> targets;
    for (const auto &p : project.patterns)
        if (!placed(project, p.id) && (mode == "unplaced" || empty(p)))
            targets.insert(p.id);
    if (targets.empty())
        return juce::Result::fail(mode == "empty" ? "patternPurge.none: No empty unused Patterns"
                                                  : "patternPurge.none: Every Pattern is placed in the Playlist");
    if (removed)
        removed->assign(targets.begin(), targets.end());
    erase(project, targets);
    return juce::Result::ok();
}
// Song document that plays exactly one Pattern once from beat0 (Pattern render).
inline juce::Result renderProject(const Project &source, Id pattern, Project &out)
{
    const auto *p = source.pattern(pattern);
    if (!p)
        return juce::Result::fail("patternRender.missingPattern: Existing Pattern required");
    if (!(p->length > 0))
        return juce::Result::fail("patternRender.emptyLength: Pattern has no length");
    out = source;
    out.placements = {{out.allocate(), pattern, 0, 0, p->length, 0, false}};
    out.audioPlacements.clear();
    out.clipGroups.clear();
    out.automation.placements.clear();
    return juce::Result::ok();
}
} // namespace vibera::pattern_management
