// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "PatternChannelLoops.h"
#include <set>

namespace vibera
{
// The Automation document playback compiles (control thread, off the audio callback).
// Pattern automation (Pattern::automation, v56) holds windows of project automation sources at
// Pattern beats. Song mode: it plays wherever the Pattern is placed (clipped to each placement's
// window), next to the Playlist's Automation Clips. Pattern mode: only the selected Pattern's
// automation plays (Playlist clips do not). `songMode` says how to compile the result.
struct AudibleAutomation
{
    AutomationDocument document;
    bool songMode = true;
};
inline AudibleAutomation audibleAutomation(const Project &p, bool patternMode, Id selected)
{
    AudibleAutomation result;
    auto next = std::max<std::int64_t>(p.nextId, 1) + (std::int64_t(1) << 40); // Ids unused by the project.
    if (patternMode)
    {
        const auto *pattern = p.pattern(selected);
        if (!pattern || pattern->automation.empty())
        {
            result.document = p.automation;
            result.songMode = false; // Unchanged: no automation in Pattern mode.
            return result;
        }
        std::set<std::int64_t> used;
        std::set<AutomationKey> keys;
        for (const auto &window : pattern->automation)
            if (!window.muted)
                used.insert(window.source);
        for (const auto &source : p.automation.sources)
            if (used.count(source.id))
            {
                result.document.sources.push_back(source);
                for (const auto &link : source.links)
                    keys.insert(link.key);
            }
        for (const auto &value : p.automation.initialValues)
            if (keys.count(value.key))
                result.document.initialValues.push_back(value);
        for (auto window : pattern->automation)
            if (!window.muted && used.count(window.source))
            {
                window.id = next++;
                window.lane = 0;
                result.document.placements.push_back(window);
            }
        return result;
    }
    result.document = p.automation;
    // A muted Playlist track silences its Automation Clips like any clip mute.
    for (auto &placement : result.document.placements)
        placement.muted = placement.muted || p.laneMuted(placement.lane);
    for (const auto &clip : p.placements)
    {
        const auto *pattern = p.pattern(clip.pattern);
        if (!pattern || pattern->automation.empty() || clip.muted || p.laneMuted(clip.lane))
            continue;
        forEachClipRepeat(clip.length, clip.offset, pattern->length,
                          [&](double position, double length, double offset, std::uint32_t)
                          {
                              const double from = offset, to = offset + length; // Pattern beats shown.
                              for (const auto &window : pattern->automation)
                              {
                                  const double start = std::max(from, window.beat),
                                               end = std::min(to, window.beat + window.length);
                                  if (window.muted || !(end > start + 1.e-9))
                                      continue;
                                  AutomationPlacement placed;
                                  placed.id = next++;
                                  placed.source = window.source;
                                  placed.beat = clip.beat + position + (start - from);
                                  placed.length = end - start;
                                  placed.offset = window.offset + (start - window.beat);
                                  placed.lane = juce::jlimit(0, 127, clip.lane);
                                  result.document.placements.push_back(placed);
                              }
                          });
    }
    return result;
}
} // namespace vibera
