// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerAutomationTarget.h"

namespace vibera
{
// CONTROL-only delayed intent: changing the selected Channel/page cannot retarget it.
struct SamplerAutomationUiIntent
{
    juce::String project;
    std::uint64_t revision = 0;
    Id channel = 0;
    std::uint32_t parameter = 0; // Zero is the valid Volume/enabled target.
    AutomationKey key() const noexcept
    {
        return {AutomationParameter::samplerModulation, channel, parameter};
    }
    juce::Result validate(const Project &current, const juce::String &identity, std::uint64_t currentRevision,
                          SamplerAutomationTarget &target, bool allowDormant = false) const
    {
        if (identity != project || currentRevision != revision)
            return juce::Result::fail(
                "automation.staleIntent: Project changed; reopen the sampler automation menu");
        return resolveSamplerAutomationTarget(current, channel, parameter, target, allowDormant);
    }
};
enum class SamplerAutomationUiAction
{
    create,
    initialize,
    removeInitialization,
    link,
    unlink,
    select
};
} // namespace vibera
