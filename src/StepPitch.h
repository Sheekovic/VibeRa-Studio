// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepVelocity.h"

namespace vibera
{
// CONTROL-only. Gesture previews use their immutable opening Project; all
// validation precedes writes, including when the caller aliases next/baseline.
inline juce::Result applyStepPitch(Project &next, const Project &baseline, const CommandArgs &args)
{
    auto *pattern = next.pattern(args.pattern);
    const auto *channel = next.channel(args.channel);
    const auto *original = baseline.pattern(args.pattern);
    if (!pattern || !channel || channel->audioClip || !original)
        return juce::Result::fail("Choose a current playable Channel and Pattern");
    const auto mode = args.text.isEmpty() ? juce::String("set") : args.text;
    if (mode != "set" && mode != "reset" && mode != "ramp" && mode != "scale")
        return juce::Result::fail("Pitch mode must be set, reset, ramp or scale");
    const auto midiPitch = [](double value)
    { return std::isfinite(value) && value >= 0 && value <= 127 && value == std::round(value); };
    if (mode != "reset" && (args.pitch < 0 || args.pitch > 127))
        return juce::Result::fail("Pitch must be an integral MIDI value 0-127");
    if (mode == "ramp" && (!midiPitch(args.offset) || !std::isfinite(args.beat) ||
                           !std::isfinite(args.length) || args.beat < 0 || args.beat >= pattern->length ||
                           args.beat + args.length < 0 || args.beat + args.length >= pattern->length ||
                           std::abs(args.beat * 4 - std::round(args.beat * 4)) > .00001 ||
                           std::abs(args.length * 4 - std::round(args.length * 4)) > .00001))
        return juce::Result::fail(
            "Ramp endpoints must be in-loop sixteenth-note beats with MIDI pitches 0-127");
    if (args.ids.size() > 100000)
        return juce::Result::fail("Too many pitch targets");
    std::map<Id, Note *> current;
    // Store values, not pointers into baseline: even direct in-place calls
    // transpose every note from the same pre-edit anchor and pitch snapshot.
    std::map<Id, std::pair<double, int>> before;
    for (auto &note : pattern->notes)
        if (graphStep(note, channel->id, pattern->length))
            current.emplace(note.id, &note);
    for (const auto &note : original->notes)
        if (graphStep(note, channel->id, original->length))
            before.emplace(note.id, std::pair{note.beat, note.pitch});
    std::set<Id> targets;
    if (args.ids.empty())
        for (const auto &[id, note] : current)
            targets.insert(id);
    else
        for (auto id : args.ids)
            if (!current.contains(id) || !targets.insert(id).second)
                return juce::Result::fail(
                    "Pitch targets must be unique current active step-note IDs in this Channel");
    if (targets.empty())
        return juce::Result::fail("This Channel has no active step events in the current Pattern");
    if (mode == "scale" || mode == "ramp")
        for (auto id : targets)
            if (!before.contains(id) || before.at(id).first != current.at(id)->beat)
                return juce::Result::fail(
                    "Step targets changed since gesture start; finish or cancel and rediscover");
    if (mode == "scale" && (!targets.contains(args.target) || !before.contains(args.target)))
        return juce::Result::fail("Scale anchor must be one of the targeted active step-note IDs");
    for (auto id : targets)
    {
        auto &note = *current.at(id);
        if (mode == "set" || mode == "reset")
            note.pitch = mode == "reset" ? 60 : args.pitch;
        else if (mode == "scale")
            note.pitch =
                juce::jlimit(0, 127, before.at(id).second + args.pitch - before.at(args.target).second);
        else
        {
            note.pitch = before.at(id).second;
            const auto low = std::min(args.beat, args.beat + args.length);
            const auto high = std::max(args.beat, args.beat + args.length);
            if (note.beat >= low - 1.e-8 && note.beat <= high + 1.e-8)
            {
                const auto amount = args.length == 0 ? 1. : (note.beat - args.beat) / args.length;
                note.pitch =
                    juce::jlimit(0, 127, int(std::round(args.pitch + amount * (args.offset - args.pitch))));
            }
        }
    }
    return juce::Result::ok();
}
} // namespace vibera
