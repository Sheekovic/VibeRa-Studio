// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepVelocity.h"
#include <limits>

namespace vibera
{
enum class StepExpression
{
    pan,
    fine,
    modX,
    modY
};
struct StepExpressionInfo
{
    StepExpression property;
    const char *id, *command, *field, *units;
    double minimum, maximum, defaultValue;
};
inline const StepExpressionInfo *stepExpressionInfo(StepExpression property) noexcept
{
    static constexpr StepExpressionInfo pan{StepExpression::pan, "pan", "steps.pan", "pan",
                                            "normalized",        -1,    1,           0};
    static constexpr StepExpressionInfo fine{
        StepExpression::fine, "fine", "steps.fine", "fineCents", "cents", -100, 100, 0};
    static constexpr StepExpressionInfo modX{
        StepExpression::modX, "modX", "steps.modX", "modX", "normalized", 0, 1, .5};
    static constexpr StepExpressionInfo modY{
        StepExpression::modY, "modY", "steps.modY", "modY", "normalized", 0, 1, .5};
    switch (property)
    {
    case StepExpression::pan:
        return &pan;
    case StepExpression::fine:
        return &fine;
    case StepExpression::modX:
        return &modX;
    case StepExpression::modY:
        return &modY;
    }
    return nullptr;
}
inline double stepExpressionValue(const Note &note, StepExpression property) noexcept
{
    switch (property)
    {
    case StepExpression::pan:
        return note.pan;
    case StepExpression::fine:
        return note.fineCents;
    case StepExpression::modX:
        return note.modX;
    case StepExpression::modY:
        return note.modY;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
inline bool setStepExpressionValue(Note &note, StepExpression property, double value) noexcept
{
    const auto *info = stepExpressionInfo(property);
    if (!info || !std::isfinite(value) || value < info->minimum || value > info->maximum)
        return false;
    switch (property)
    {
    case StepExpression::pan:
        note.pan = value;
        return true;
    case StepExpression::fine:
        note.fineCents = value;
        return true;
    case StepExpression::modX:
        note.modX = value;
        return true;
    case StepExpression::modY:
        note.modY = value;
        return true;
    }
    return false;
}
// CONTROL-only canonical model edit. DSP composition is original, not an FL
// algorithm claim: clamp(global normalized filter value + 2 * (noteMod - .5), 0, 1).
// Mod X offsets cutoff and Mod Y offsets resonance; their neutral is .5.
// Model edits alone do not qualify actual DSP/native response.
// The baseline is the actual gesture start, otherwise the canonical current score.
inline juce::Result applyStepExpression(Project &next, const Project &baseline, const CommandArgs &args,
                                        StepExpression property)
{
    const auto *info = stepExpressionInfo(property);
    if (!info)
        return juce::Result::fail("stepExpression.invalidProperty: Expected pan, fine, modX or modY");
    auto *pattern = next.pattern(args.pattern);
    const auto *channel = next.channel(args.channel);
    const auto *original = baseline.pattern(args.pattern);
    const auto *originalChannel = baseline.channel(args.channel);
    if (!pattern || !original || !channel || !originalChannel)
        return juce::Result::fail("stepExpression.missingTarget: Existing Pattern and Channel required");
    if (channel->audioClip || channel->instrument || originalChannel->audioClip ||
        originalChannel->instrument)
        return juce::Result::fail(
            "stepExpression.unsupportedTarget: Per-note Pan/Fine/Mod X/Y requires the local sampler");
    const auto mode = args.text.isEmpty() ? juce::String("set") : args.text;
    if (mode != "set" && mode != "reset" && mode != "scale" && mode != "ramp")
        return juce::Result::fail("stepExpression.invalidMode: Expected set, reset, scale or ramp");
    const auto valid = [info](double value)
    { return std::isfinite(value) && value >= info->minimum && value <= info->maximum; };
    if (mode != "reset" && !valid(args.value))
        return juce::Result::fail(
            "stepExpression.invalidValue: Finite value within the property range required");
    if (mode == "ramp" && (!valid(args.offset) || !std::isfinite(args.beat) || !std::isfinite(args.length) ||
                           args.beat < 0 || args.beat >= pattern->length || args.beat + args.length < 0 ||
                           args.beat + args.length >= pattern->length ||
                           std::abs(args.beat * 4 - std::round(args.beat * 4)) > .00001 ||
                           std::abs(args.length * 4 - std::round(args.length * 4)) > .00001))
        return juce::Result::fail(
            "stepExpression.invalidRamp: In-window quarter-step endpoints and valid values required");
    if (args.ids.size() > 100000)
        return juce::Result::fail("stepExpression.capacity: At most100000 source-note targets");
    const auto valueOf = [property](const Note &note) { return stepExpressionValue(note, property); };
    std::map<Id, Note *> current;
    std::map<Id, std::pair<double, double>> before;
    for (auto &note : pattern->notes)
        if (graphStep(note, channel->id, pattern->length))
            current.emplace(note.id, &note);
    for (const auto &note : original->notes)
        if (graphStep(note, channel->id, original->length))
            before.emplace(note.id, std::pair{note.beat, valueOf(note)});
    std::set<Id> targets;
    if (args.ids.empty())
        for (const auto &[id, note] : current)
            targets.insert(id);
    else
        for (auto id : args.ids)
            if (!current.contains(id) || !targets.insert(id).second)
                return juce::Result::fail(
                    "stepExpression.invalidTargets: Distinct current step IDs in this Channel required");
    if (targets.empty())
        return juce::Result::fail("stepExpression.noTargets: This Channel has no eligible step events");
    for (auto id : targets)
    {
        if (!valid(valueOf(*current.at(id))))
            return juce::Result::fail("stepExpression.invalidSource: Invalid current note expression");
        if ((mode == "scale" || mode == "ramp") &&
            (!before.contains(id) || before.at(id).first != current.at(id)->beat ||
             !valid(before.at(id).second)))
            return juce::Result::fail(
                "stepExpression.staleTargets: Finish or cancel and rediscover source IDs");
    }
    if (mode == "scale" && !targets.contains(args.target))
        return juce::Result::fail(
            "stepExpression.invalidAnchor: Anchor must belong to the targeted source notes");
    for (auto id : targets)
    {
        auto &note = *current.at(id);
        double value = mode == "reset" ? info->defaultValue : args.value;
        if (mode == "scale")
            value = juce::jlimit(info->minimum, info->maximum,
                                 before.at(id).second + args.value - before.at(args.target).second);
        if (mode == "ramp")
        {
            value = before.at(id).second;
            if (note.beat >= std::min(args.beat, args.beat + args.length) - 1.e-8 &&
                note.beat <= std::max(args.beat, args.beat + args.length) + 1.e-8)
            {
                const auto fraction = args.length == 0 ? 1. : (note.beat - args.beat) / args.length;
                value = juce::jlimit(info->minimum, info->maximum,
                                     args.value + fraction * (args.offset - args.value));
            }
        }
        (void)setStepExpressionValue(note, property, value);
    }
    return juce::Result::ok();
}
} // namespace vibera
