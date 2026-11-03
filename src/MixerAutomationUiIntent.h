// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerAutomationTarget.h"
namespace vibera
{
// A delayed UI intent is never retargeted through current selection or row position.
struct MixerAutomationUiIntent
{
    juce::String project;
    std::uint64_t revision = 0;
    int insert = 0;
    MixerAutomationControl control = MixerAutomationControl::gain;
    std::optional<int> sendTarget; // Appended; existing four-field intents retain their meaning.
    juce::Result validate(const Project &current, const juce::String &identity, std::uint64_t currentRevision,
                          MixerAutomationTarget &target) const
    {
        if (identity != project || currentRevision != revision)
            return juce::Result::fail("automation.staleIntent: Project changed; reopen the automation menu");
        return resolveMixerAutomationTarget(current, insert, control, target, sendTarget);
    }
};
enum class MixerAutomationUiAction
{
    create,
    initialize,
    removeInitialization,
    link,
    select
};
struct MixerAutomationUiChoice
{
    MixerAutomationTarget target;
    float minimum = 0, maximum = 2;
    bool linked = false;
};
inline std::vector<MixerAutomationUiChoice> mixerAutomationUiChoices(const Project &project,
                                                                     const AutomationSource &source)
{
    std::vector<MixerAutomationUiChoice> result;
    // Retain all legacy gain rows/indices, then append explicit bipolar targets.
    for (auto control : {MixerAutomationControl::gain, MixerAutomationControl::pan,
                         MixerAutomationControl::stereoSeparation})
        for (const auto &insert : project.mixer)
        {
            MixerAutomationUiChoice choice;
            if (resolveMixerAutomationTarget(project, insert.index, control, choice.target).failed())
                continue;
            const auto link = std::find_if(source.links.begin(), source.links.end(),
                                           [&](const auto &item) { return item.key == choice.target.key; });
            choice.linked = link != source.links.end();
            choice.minimum = choice.linked ? link->minimum : choice.target.minimum;
            choice.maximum = choice.linked ? link->maximum : choice.target.maximum;
            result.push_back(std::move(choice));
        }
    // Preserve all original gain/pan/separation row indices; append actual edges.
    for (const auto &insert : project.mixer)
        for (const auto &route : insert.routes)
        {
            MixerAutomationUiChoice choice;
            if (resolveMixerAutomationTarget(project, insert.index, MixerAutomationControl::send,
                                             choice.target, route.target)
                    .failed())
                continue;
            const auto link = std::find_if(source.links.begin(), source.links.end(),
                                           [&](const auto &item) { return item.key == choice.target.key; });
            choice.linked = link != source.links.end();
            choice.minimum = choice.linked ? link->minimum : choice.target.minimum;
            choice.maximum = choice.linked ? link->maximum : choice.target.maximum;
            result.push_back(std::move(choice));
        }
    return result;
}
} // namespace vibera
