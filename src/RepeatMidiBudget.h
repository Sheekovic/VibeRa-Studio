// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "PluginProtocol.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace vibera
{
// CONTROL only. Reserve room for gate reconciliation and ordinary live input;
// the existing merged-input overflow guard remains authoritative for bursts.
// This admission never changes the device buffer or drops prepared score events.
inline juce::Result validateRepeatMidiBudget(const Project &project, bool patternMode, Id pattern, int frames,
                                             double rate)
{
    std::set<Id> repeatSources, repeatChannels;
    for (const auto &part : project.patterns)
        if (!patternMode || part.id == pattern)
            for (const auto &note : part.notes)
                if (note.repeatMode != 0)
                    if (const auto *channel = project.channel(note.channel); channel && channel->instrument)
                        repeatSources.insert(note.id);
    if (repeatSources.empty())
        return juce::Result::ok();
    if (frames < 1 || !std::isfinite(rate) || rate <= 0)
        return juce::Result::fail("repeat.invalidDevice: Invalid hosted Repeat timing configuration");
    const auto events = sequence(project, patternMode, pattern);
    for (const auto &note : events)
        if (repeatSources.contains(note.note) && note.repeatOrdinal > 0)
            repeatChannels.insert(note.channel);
    std::map<Id, std::vector<std::pair<double, bool>>> edges;
    for (const auto &note : events)
        if (repeatChannels.contains(note.channel))
        {
            edges[note.channel].push_back({note.beat * 60 / project.tempo, true});
            edges[note.channel].push_back({(note.beat + note.length) * 60 / project.tempo, false});
        }
    constexpr int gateBudget = 128; // Actual LiveMidi fixed gate table.
    constexpr int scoreBudget = ipc::events - 2 * gateBudget - 64;
    const double duration = frames / rate;
    for (auto &[channel, times] : edges)
    {
        // Pair order places offs before ons at an equal timestamp, exactly as
        // the prepared event stream. Reserve reconciliation for at most128
        // actual spanning notes, not an unbounded seek/edit chase with stealing.
        std::sort(times.begin(), times.end());
        int active = 0;
        for (const auto &[time, on] : times)
        {
            (void)time;
            if (on)
            {
                if (++active > gateBudget)
                    return juce::Result::fail(
                        "repeat.midiGateCapacity: This hosted score exceeds128 overlapping gates; "
                        "a seek or edit cannot safely reconcile the Repeat channel.");
            }
            else
                --active;
        }
        size_t first = 0;
        for (size_t end = 0; end < times.size(); ++end)
        {
            // Half-open callback; equal off/on timestamps count independently.
            while (first < end && times[end].first - times[first].first >= duration)
                ++first;
            if (end - first + 1 > scoreBudget)
                return juce::Result::fail(
                    "repeat.midiCapacity: This hosted score has too many simultaneous retriggers for "
                    "the current processing block. Use fewer simultaneous repeats; the score and "
                    "device configuration were not changed.");
        }
    }
    return juce::Result::ok();
}
} // namespace vibera
