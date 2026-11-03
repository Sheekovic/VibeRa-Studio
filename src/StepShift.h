// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepVelocity.h"
#include "NoteTiming.h"

namespace vibera
{
struct StepShiftInfo
{
    const char *id, *command, *field, *units;
    double minimum, maximum, defaultValue;
};
inline const StepShiftInfo *stepShiftInfo() noexcept
{
    static constexpr StepShiftInfo info{"shift", "steps.shift", "shiftPercent", "percent", 0, 100, 0};
    return &info;
}
// CONTROL-only. Gesture previews use their immutable opening Project; all
// validation precedes writes, including when the caller aliases next/baseline.
inline juce::Result applyStepShift(Project &next, const Project &baseline, const CommandArgs &args)
{
    auto *pattern = next.pattern(args.pattern);
    const auto *channel = next.channel(args.channel);
    const auto *original = baseline.pattern(args.pattern);
    if (!pattern || !channel || channel->audioClip || !original)
        return juce::Result::fail("Choose a current playable Channel and Pattern");
    const auto mode = args.text.isEmpty() ? juce::String("set") : args.text;
    if (mode != "set" && mode != "reset" && mode != "ramp" && mode != "scale")
        return juce::Result::fail("Shift mode must be set, reset, ramp or scale");
    const auto valid = [](double value) { return std::isfinite(value) && value >= 0 && value <= 100; };
    if (mode != "reset" && !valid(args.value))
        return juce::Result::fail("Shift must be finite percent0..100");
    if (mode == "ramp" && (!valid(args.offset) || !std::isfinite(args.beat) || !std::isfinite(args.length) ||
                           args.beat < 0 || args.beat >= pattern->length || args.beat + args.length < 0 ||
                           args.beat + args.length >= pattern->length ||
                           std::abs(args.beat * 4 - std::round(args.beat * 4)) > .00001 ||
                           std::abs(args.length * 4 - std::round(args.length * 4)) > .00001))
        return juce::Result::fail(
            "Ramp endpoints must be in-loop sixteenth-note beats with Shift percent0..100");
    if (args.ids.size() > 100000)
        return juce::Result::fail("Too many Shift targets");
    std::map<Id, Note *> current;
    // Store values, not pointers into baseline: even direct in-place calls
    // scale every note from the same pre-edit anchor and Shift snapshot.
    std::map<Id, std::pair<double, double>> before;
    for (auto &note : pattern->notes)
        if (graphStep(note, channel->id, pattern->length))
            current.emplace(note.id, &note);
    for (const auto &note : original->notes)
        if (graphStep(note, channel->id, original->length))
            before.emplace(note.id, std::pair{note.beat, note.shiftPercent});
    std::set<Id> targets;
    if (args.ids.empty())
        for (const auto &[id, note] : current)
            targets.insert(id);
    else
        for (auto id : args.ids)
            if (!current.contains(id) || !targets.insert(id).second)
                return juce::Result::fail(
                    "Shift targets must be unique current active step-note IDs in this Channel");
    if (targets.empty())
        return juce::Result::fail("This Channel has no active step events in the current Pattern");
    for (auto id : targets)
        if (!valid(current.at(id)->shiftPercent))
            return juce::Result::fail("Invalid source Shift percent");
    if (mode == "scale" || mode == "ramp")
        for (auto id : targets)
            if (!before.contains(id) || before.at(id).first != current.at(id)->beat ||
                !valid(before.at(id).second))
                return juce::Result::fail(
                    "Step targets changed since gesture start; finish or cancel and rediscover");
    if (mode == "scale" && (!targets.contains(args.target) || !before.contains(args.target)))
        return juce::Result::fail("Scale anchor must be one of the targeted active step-note IDs");
    for (auto id : targets)
    {
        auto &note = *current.at(id);
        if (mode == "set" || mode == "reset")
            note.shiftPercent = mode == "reset" ? 0 : args.value;
        else if (mode == "scale")
        {
            const auto anchor = before.at(args.target).second;
            const auto value = before.at(id).second;
            // Positive anchor: proportional ratio. Zero anchor: additive lift.
            note.shiftPercent =
                juce::jlimit(0., 100., anchor > 0 ? value * args.value / anchor : value + args.value);
        }
        else
        {
            note.shiftPercent = before.at(id).second;
            const auto low = std::min(args.beat, args.beat + args.length);
            const auto high = std::max(args.beat, args.beat + args.length);
            if (note.beat >= low - 1.e-8 && note.beat <= high + 1.e-8)
            {
                const auto amount = args.length == 0 ? 1. : (note.beat - args.beat) / args.length;
                note.shiftPercent = juce::jlimit(0., 100., args.value + amount * (args.offset - args.value));
            }
        }
    }
    return juce::Result::ok();
}
} // namespace vibera
