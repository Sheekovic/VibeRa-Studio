// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <charconv>
namespace vibera
{
inline juce::Result parseSamplerModulationMacro(const juce::String &command, const juce::String &input,
                                                CommandArgs &args)
{
    const auto *parameter = samplerModulationParameter(
        command.fromFirstOccurrenceOf("channel.sampler.modulation.", false, false).toStdString());
    const auto destination = input.upToFirstOccurrenceOf(":", false, false).trim();
    const int target = samplerModulationTarget(destination.toStdString());
    const auto numeric = input.fromFirstOccurrenceOf(":", false, false).trim().toStdString();
    double value = 0;
    const auto parsed = std::from_chars(numeric.data(), numeric.data() + numeric.size(), value);
    if (!parameter || target < 0 || numeric.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != numeric.data() + numeric.size() || (target == 1 && parameter->id.substr(0, 3) != "lfo"))
        return juce::Result::fail("samplerModulation.invalidMacro: Use destination:value, e.g. cutoff:0.5");
    SamplerModulation probe;
    if (!setSamplerModulationValue(probe, *parameter, value))
        return juce::Result::fail("samplerModulation.invalidValue: Value outside the parameter range");
    args.text = destination;
    args.value = value;
    args.enabled = value != 0;
    return juce::Result::ok();
}
} // namespace vibera
