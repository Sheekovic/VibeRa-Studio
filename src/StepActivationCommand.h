// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepActivation.h"
#include "Project.h"
#include "PatternExtent.h"
#include <limits>

namespace vibera
{
// Original CONTROL-only canonical candidate adapter. Source cells use the
// absolute musical position, independent of repeated playback occurrences.
inline juce::Result applyStepActivation(Project &project, Id patternId, Id channelId, double visibleBeat,
                                        bool enabled, step_memory::Retention retention)
{
    const auto *source = project.pattern(patternId);
    const auto *channel = project.channel(channelId);
    if (!source || !channel || channel->audioClip)
        return juce::Result::fail("stepMemory.missingTarget: Choose a current playable Channel and Pattern");
    if (const auto valid = project.validate(); valid.failed())
        return juce::Result::fail("stepMemory.invalidSource: " + valid.getErrorMessage());
    // A shrinking automatic playback period must never redirect an edit to a
    // different source chord. Fixed-loop ghosts also remain visualization;
    // editing a later column does not silently alter an earlier source cell.
    // The bounded reference dispatch/writer study supports this addressing.
    const auto cell = step_memory::resolveCell(channelId, visibleBeat, source->length);
    if (cell.error != step_memory::Error::none)
        return juce::Result::fail(
            "stepMemory.invalidCell: In-window sixteenth-note source position required");
    Project candidate = project;
    auto *pattern = candidate.pattern(patternId);
    const auto edited = step_memory::set(pattern->notes, cell, enabled, retention);
    if (edited.error != step_memory::Error::none)
        return juce::Result::fail("stepMemory.invalidPolicy: Use mute or discard retention");
    if (edited.needsNote)
    {
        if (pattern->notes.size() >= 100000)
            return juce::Result::fail("stepMemory.capacity: Pattern source-note limit reached");
        if (candidate.nextId < 1 || candidate.nextId == std::numeric_limits<Id>::max())
            return juce::Result::fail("stepMemory.identityCapacity: No safe stable Note ID available");
        pattern->notes.push_back({candidate.allocate(), channelId, cell.beat, .25, 60, .8f, true});
    }
    if (!edited.changed && !edited.needsNote)
        return juce::Result::ok();
    refreshAutomaticPatternLength(candidate, *pattern);
    if (auto valid = candidate.validate(); valid.failed())
        return juce::Result::fail("stepMemory.invalidCandidate: " + valid.getErrorMessage());
    project = std::move(candidate);
    return juce::Result::ok();
}
} // namespace vibera
