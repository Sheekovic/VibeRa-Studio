// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "PatternExtent.h"
#include <cmath>
#include <set>
#include <map>
#include <limits>

namespace vibera
{
inline juce::String channelLoopModeName(ChannelLoopMode mode)
{
    switch (mode)
    {
    case ChannelLoopMode::fixed:
        return "fixed";
    case ChannelLoopMode::step:
        return "step";
    case ChannelLoopMode::beat:
        return "beat";
    case ChannelLoopMode::bar:
        return "bar";
    }
    return {};
}
inline std::optional<ChannelLoopMode> channelLoopMode(const juce::String &name)
{
    for (auto mode :
         {ChannelLoopMode::fixed, ChannelLoopMode::step, ChannelLoopMode::beat, ChannelLoopMode::bar})
        if (channelLoopModeName(mode) == name)
            return mode;
    return {};
}
inline const ChannelLoop *channelLoop(const Pattern &pattern, Id channel)
{
    for (const auto &loop : pattern.channelLoops)
        if (loop.channel == channel)
            return &loop;
    return nullptr;
}
inline double channelLoopLength(const Project &project, const Pattern &pattern, Id channel)
{
    const auto *loop = channelLoop(pattern, channel);
    if (!loop)
        return 0;
    if (loop->mode == ChannelLoopMode::fixed)
        return loop->length;
    const double beat = 4. / patternDenominator(project, pattern);
    const double unit = loop->mode == ChannelLoopMode::step   ? .25
                        : loop->mode == ChannelLoopMode::beat ? beat
                                                              : beat * patternNumerator(project, pattern);
    double end = 0;
    for (const auto &note : pattern.notes)
        if (note.channel == channel && !note.muted && note.velocity > 0 && note.beat < pattern.length)
            end = std::max(end, std::min(pattern.length, note.beat + note.length));
    // The last sounding end on an exact boundary does not add an empty unit.
    return std::max(unit, std::ceil(end / unit) * unit);
}
struct PatternOccurrence
{
    Note note;
    std::uint32_t iteration = 0;
};
// CONTROL only. Shared raw score expansion for scheduling and Burn. Never expands
// in DSP. Keep source IDs; synthetic tags disambiguate repetitions at compilation.
inline std::vector<PatternOccurrence> rawPatternOccurrences(const Project &project, const Pattern &pattern)
{
    std::vector<PatternOccurrence> result;
    std::map<Id, double> periods;
    for (const auto &loop : pattern.channelLoops)
        periods.emplace(loop.channel, channelLoopLength(project, pattern, loop.channel));
    for (const auto &note : pattern.notes)
    {
        const auto found = periods.find(note.channel);
        const double period = found == periods.end() ? 0 : found->second;
        if (period == 0)
        {
            result.push_back({note, 0});
            continue;
        }
        if (note.beat >= period || note.beat >= pattern.length)
            continue;
        const auto count = std::uint32_t(std::ceil((pattern.length - note.beat) / period));
        for (std::uint32_t iteration = 0; iteration < count; ++iteration)
        {
            auto occurrence = note;
            occurrence.beat += double(iteration) * period;
            result.push_back({occurrence, iteration});
        }
    }
    return result;
}
inline juce::Result validateChannelLoops(const Project &project, const Pattern &pattern)
{
    if (pattern.channelLoops.size() > 128)
        return juce::Result::fail("channelLoop.capacity: At most128 Channel loops per Pattern");
    std::set<Id> seen;
    for (const auto &loop : pattern.channelLoops)
    {
        const auto *channel = project.channel(loop.channel);
        if (!channel || channel->audioClip || !seen.insert(loop.channel).second ||
            channelLoopModeName(loop.mode).isEmpty() || !std::isfinite(loop.length) ||
            (loop.mode == ChannelLoopMode::fixed ? loop.length < .25 || loop.length > 1000000 ||
                                                       loop.length * 4 != std::floor(loop.length * 4)
                                                 : loop.length != 1))
            return juce::Result::fail(
                "channelLoop.invalid: Existing distinct score Channel and valid mode/length required");
    }
    if (!pattern.channelLoops.empty())
    {
        std::map<Id, double> periods;
        for (const auto &loop : pattern.channelLoops)
            periods.emplace(loop.channel, channelLoopLength(project, pattern, loop.channel));
        double count = 0;
        for (const auto &note : pattern.notes)
        {
            if (!std::isfinite(note.beat) || note.beat < 0 || !std::isfinite(note.length) || note.length <= 0)
                return juce::Result::fail("channelLoop.invalidNote: Finite positive source timing required");
            const auto found = periods.find(note.channel);
            const double period = found == periods.end() ? 0 : found->second;
            count += period == 0 ? 1
                     : note.beat < period && note.beat < pattern.length
                         ? std::ceil((pattern.length - note.beat) / period)
                         : 0;
            if (count > 100000)
                return juce::Result::fail(
                    "channelLoop.capacity: Expanded Pattern would exceed100000 score occurrences");
        }
    }
    return juce::Result::ok();
}
inline Id loopOccurrencePlacement(Id placement, std::uint32_t iteration) noexcept
{
    // MCP IDs are <=1e9; larger legacy Clip IDs are admitted only if the product
    // fits signed64 (checked by Project validation). Pattern playback uses0. A Pattern is at
    // most1e6 beats and the smallest period is.25: this negative namespace is
    // injective, bounded, stable under edits, and never serialized as a Clip ID.
    return iteration == 0 ? placement : -(placement * 4000001 + Id(iteration));
}
} // namespace vibera
