// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoteStretch.h"
#include <cmath>
#include <unordered_set>
namespace vibera
{
juce::Result validateStretchRatio(double ratio)
{
    if (!std::isfinite(ratio) || ratio < 1.e-6 || ratio > 1000000)
        return juce::Result::fail("Stretch ratio must be 0.000001..1000000 (1 means unchanged)");
    return juce::Result::ok();
}
juce::Result planNoteStretch(const Project &project, Id patternId, Id channelId, const std::vector<Id> &ids,
                             double anchor, double ratio, NoteStretchPlan &result)
{
    const auto *pattern = project.pattern(patternId);
    const auto *channel = project.channel(channelId);
    if (!pattern || !channel || channel->audioClip)
        return juce::Result::fail("Select an existing Pattern and playable Channel");
    if (ids.empty() || ids.size() > 100000)
        return juce::Result::fail("Select 1..100000 notes to stretch");
    std::unordered_set<Id> targets(ids.begin(), ids.end());
    if (targets.size() != ids.size())
        return juce::Result::fail("Stretch IDs must be distinct");
    if (!std::isfinite(anchor) || anchor < 0 || anchor > 1000000)
        return juce::Result::fail("Stretch anchor must be 0..1000000 quarter-note beats");
    if (auto r = validateStretchRatio(ratio); r.failed())
        return r;
    NoteStretchPlan plan;
    plan.anchor = anchor;
    plan.minimumRatio = 1.e-6;
    plan.maximumRatio = 1000000;
    plan.end = anchor;
    for (const auto &note : pattern->notes)
    {
        if (!targets.erase(note.id))
            continue;
        if (note.beat < anchor)
            return juce::Result::fail("Stretch anchor cannot follow a targeted note onset");
        plan.end = std::max(plan.end, note.beat + note.length);
        const auto minimum = std::min(note.length, 1. / 960.);
        plan.minimumRatio = std::max(plan.minimumRatio, minimum / note.length);
        plan.maximumRatio =
            std::min(plan.maximumRatio, (1000000. - anchor) / (note.beat + note.length - anchor));
        auto copy = note;
        copy.beat = anchor + (note.beat - anchor) * ratio;
        copy.length = note.length * ratio;
        if (ratio != 1)
            copy.step = false;
        plan.notes.push_back(copy);
    }
    if (!targets.empty())
        return juce::Result::fail("Stretch target does not belong to the Pattern");
    if (ratio + 1.e-12 < plan.minimumRatio || ratio > plan.maximumRatio + 1.e-12)
        return juce::Result::fail("Stretch exceeds the source note minimum or musical-time limit");
    result = std::move(plan);
    return juce::Result::ok();
}
} // namespace vibera
