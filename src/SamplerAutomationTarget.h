// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "SamplerAutomationParameters.h"

namespace vibera
{
inline bool isSamplerAutomationTargetCommand(const juce::String &id)
{
    return id == "automation.sampler.create" || id == "automation.sampler.init" ||
           id == "automation.sampler.init.remove" || id == "automation.sampler.link" ||
           id == "automation.sampler.unlink" || id == "automation.sampler.range";
}
struct SamplerAutomationTarget
{
    AutomationKey key;
    SamplerAutomationParameter parameter;
    double manual = 0;
    bool available = false;
    juce::String name;
    bool contains(double value) const noexcept
    {
        return samplerAutomationValueValid(parameter, value);
    }
};
// CONTROL-only canonical resolution. No selected-Channel fallback or native access.
// Existing dormant targets may be inspected/unlinked when their Channel changes role;
// new curves and initialization require a supported local sample instrument.
inline juce::Result resolveSamplerAutomationTarget(const Project &project, Id channel, std::uint32_t code,
                                                   SamplerAutomationTarget &out, bool allowDormant = false)
{
    const auto metadata = lookupSamplerAutomationParameter(code);
    if (!metadata)
        return juce::Result::fail("automation.unsupportedSamplerParameter: Unknown destination/field code");
    const auto *owner = project.channel(channel);
    if (!owner)
        return juce::Result::fail("automation.missingSampler: Existing stable Channel ID required");
    const bool available = !owner->instrument && !owner->audioClip;
    if (!available && !allowDormant)
        return juce::Result::fail("automation.samplerUnavailable: Local sample instrument required; "
                                  "native/plugin and Audio Clip targets are unsupported");
    const auto manual = samplerModulationValue(owner->sampler.modulation[size_t(metadata->registryTarget)],
                                               *metadata->registryParameter);
    if (!samplerAutomationValueValid(*metadata, manual))
        return juce::Result::fail("automation.invalidSamplerState: Current sampler parameter is invalid");
    out = {{AutomationParameter::samplerModulation, channel, code},
           *metadata,
           manual,
           available,
           owner->name + " - " + juce::String(metadata->target.data(), metadata->target.size()) + " / " +
               juce::String(metadata->label.data(), metadata->label.size())};
    return juce::Result::ok();
}
} // namespace vibera
