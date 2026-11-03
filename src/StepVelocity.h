// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <map>
#include <set>
#include <cmath>

namespace vibera
{
inline bool graphStep(const Note &note, Id channel, double length)
{
    return note.channel == channel && note.step && note.beat >= 0 && note.beat < length &&
           std::abs(note.beat * 4 - std::round(note.beat * 4)) < .00001;
}
// Control-thread command only. Baseline is the canonical gesture start, otherwise
// the current Project. Repeated scaling/ramp previews never compound rounding.
inline juce::Result applyStepVelocity(Project &next, const Project &baseline, const CommandArgs &args)
{
    auto *pattern = next.pattern(args.pattern);
    const auto *channel = next.channel(args.channel);
    const auto *original = baseline.pattern(args.pattern);
    if (!pattern || !channel || channel->audioClip || !original)
        return juce::Result::fail("Choose a current playable Channel and Pattern");
    const auto mode = args.text.isEmpty() ? juce::String("set") : args.text;
    if (mode != "set" && mode != "reset" && mode != "ramp" && mode != "scale")
        return juce::Result::fail("Velocity mode must be set, reset, ramp or scale");
    auto normalized = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (mode != "reset" && !normalized(args.value))
        return juce::Result::fail("Velocity must be finite and normalized 0-1");
    if (mode == "ramp" && (!normalized(args.offset) || !std::isfinite(args.beat) ||
                           !std::isfinite(args.length) || args.beat < 0 || args.beat >= pattern->length ||
                           args.beat + args.length < 0 || args.beat + args.length >= pattern->length ||
                           std::abs(args.beat * 4 - std::round(args.beat * 4)) > .00001 ||
                           std::abs(args.length * 4 - std::round(args.length * 4)) > .00001))
        return juce::Result::fail("Ramp endpoints must be in-loop sixteenth-note beats with values 0-1");
    if (args.ids.size() > 100000)
        return juce::Result::fail("Too many velocity targets");
    std::map<Id, Note *> current;
    std::map<Id, const Note *> before;
    for (auto &note : pattern->notes)
        if (graphStep(note, channel->id, pattern->length))
            current.emplace(note.id, &note);
    for (const auto &note : original->notes)
        if (graphStep(note, channel->id, original->length))
            before.emplace(note.id, &note);
    std::set<Id> targets;
    if (args.ids.empty())
        for (const auto &[id, note] : current)
            targets.insert(id);
    else
        for (auto id : args.ids)
            if (!current.contains(id) || !targets.insert(id).second)
                return juce::Result::fail(
                    "Velocity targets must be unique current active step-note IDs in this Channel");
    if (targets.empty())
        return juce::Result::fail("This Channel has no active step events in the current Pattern");
    if (mode == "scale" || mode == "ramp")
        for (auto id : targets)
            if (!before.contains(id) || before.at(id)->beat != current.at(id)->beat)
                return juce::Result::fail(
                    "Step targets changed since gesture start; finish or cancel and rediscover");
    if (mode == "scale" && (!targets.contains(args.target) || !before.contains(args.target)))
        return juce::Result::fail("Scale anchor must be one of the targeted active step-note IDs");
    const auto low = std::min(args.beat, args.beat + args.length);
    const auto high = std::max(args.beat, args.beat + args.length);
    for (auto id : targets)
    {
        auto &note = *current.at(id);
        if (mode == "set" || mode == "reset")
            note.velocity = mode == "reset" ? .8f : float(args.value);
        else if (mode == "scale")
        {
            const auto anchor = before.at(args.target)->velocity;
            const auto value = before.at(id)->velocity;
            // A zero anchor has no ratio: apply the same additive lift instead.
            note.velocity =
                float(juce::jlimit(0., 1., anchor > 0 ? value * args.value / anchor : value + args.value));
        }
        else
        {
            note.velocity = before.at(id)->velocity;
            if (note.beat >= low - 1.e-8 && note.beat <= high + 1.e-8)
            {
                const auto amount = args.length == 0 ? 1. : (note.beat - args.beat) / args.length;
                note.velocity = float(juce::jlimit(0., 1., args.value + amount * (args.offset - args.value)));
            }
        }
    }
    return juce::Result::ok();
}
} // namespace vibera
