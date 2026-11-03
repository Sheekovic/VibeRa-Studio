// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace vibera
{
// Original CONTROL-only presentation projection. Ordinals are window-lifetime
// tokens: removed targets leave tombstones, never aliases for a different index.
// An already-open JUCE popup owns an old menu copy; its result remains safe.
class MixerTargetChoices
{
  public:
    struct Choice
    {
        int ordinal = 0, index = 0;
        juce::String name;
    };
    MixerTargetChoices(const Project &source, const juce::String &identity, Id targetChannel)
        : projectIdentity(identity), channel(targetChannel)
    {
        const auto result = refresh(source, identity);
        if (result.failed())
            throw std::invalid_argument(result.getErrorMessage().toStdString());
    }
    bool matches(const Project &current, const juce::String &identity) const noexcept
    {
        if (identity != projectIdentity || !current.channel(channel) ||
            current.mixer.size() != choices.size())
            return false;
        for (std::size_t i = 0; i < choices.size(); ++i)
            if (choices[i].index != current.mixer[i].index || choices[i].name != current.mixer[i].name)
                return false;
        return true;
    }
    juce::Result refresh(const Project &current, const juce::String &identity)
    {
        if (identity != projectIdentity)
            return juce::Result::fail("routing.staleMenu: Project changed; reopen the Mixer target menu");
        if (!current.channel(channel))
            return juce::Result::fail("routing.staleChannel: Original Channel no longer exists");
        // Prepare both tables before publishing either. An allocation failure
        // leaves the old mapping/menu snapshot available; no document mutation.
        auto nextTargets = targetsByOrdinal;
        std::vector<Choice> nextChoices;
        nextChoices.reserve(current.mixer.size());
        for (const auto &insert : current.mixer)
        {
            auto found = std::find(nextTargets.begin(), nextTargets.end(), insert.index);
            int ordinal = 0;
            if (found != nextTargets.end())
                ordinal = int(found - nextTargets.begin()) + 1;
            else
            {
                if (nextTargets.size() >= std::size_t(std::numeric_limits<int>::max()))
                    return juce::Result::fail("routing.menuCapacity: Mixer menu ordinal capacity exhausted");
                nextTargets.push_back(insert.index);
                ordinal = int(nextTargets.size());
            }
            nextChoices.push_back({ordinal, insert.index, insert.name});
        }
        choices.swap(nextChoices);
        targetsByOrdinal.swap(nextTargets);
        return juce::Result::ok();
    }
    const std::vector<Choice> &items() const noexcept
    {
        return choices;
    }
    int ordinalForTarget(int index) const noexcept
    {
        const auto found = std::find_if(choices.begin(), choices.end(),
                                        [index](const auto &choice) { return choice.index == index; });
        return found == choices.end() ? 0 : found->ordinal;
    }
    // Output changes only after source session, original channel and captured
    // target still exist. The caller then runs the SAME canonical channel.mixer.
    juce::Result resolve(const Project &current, const juce::String &identity, int ordinal,
                         CommandArgs &output) const
    {
        if (identity != projectIdentity)
            return juce::Result::fail("routing.staleMenu: Project changed; reopen the Mixer target menu");
        if (!current.channel(channel))
            return juce::Result::fail("routing.staleChannel: Original Channel no longer exists");
        if (ordinal <= 0 || std::size_t(ordinal) > targetsByOrdinal.size())
            return juce::Result::fail("routing.invalidMenuChoice: Unknown Mixer menu item");
        const auto target = targetsByOrdinal[std::size_t(ordinal - 1)];
        if (target < 0 || std::none_of(
                              current.mixer.begin(), current.mixer.end(),
                              [target](const auto &insert) { return insert.index == target; }))
            return juce::Result::fail("routing.staleTarget: Original Mixer target no longer exists");
        output.channel = channel;
        output.mixer = target;
        return juce::Result::ok();
    }

  private:
    juce::String projectIdentity;
    Id channel = 0;
    std::vector<Choice> choices;
    std::vector<int> targetsByOrdinal;
};
} // namespace vibera
