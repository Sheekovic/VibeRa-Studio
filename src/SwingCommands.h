// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <cmath>
#include <set>

namespace vibera
{
inline bool isSwingCommand(const juce::String &id)
{
    return id == "project.swing" || id == "channel.swingMix" || id == "channel.truncateSwing" ||
           id == "rack.swingMix" || id == "rack.truncateSwing";
}
inline juce::Result applySwingCommand(Project &project, const juce::String &id, const CommandArgs &args)
{
    if (!id.endsWith("truncateSwing") && (!std::isfinite(args.value) || args.value < 0 || args.value > 1))
        return juce::Result::fail("Swing value must be finite and normalized 0-1");
    if (id == "project.swing")
    {
        project.swing = args.value;
        return juce::Result::ok();
    }
    const auto ids = id.startsWith("rack.") ? args.ids : std::vector<Id>{args.channel};
    if (ids.empty() || ids.size() > 128)
        return juce::Result::fail("Select 1..128 instrument Channels for Swing");
    std::set<Id> seen;
    for (auto target : ids)
        if (const auto *channel = project.channel(target);
            !channel || channel->audioClip || !seen.insert(target).second)
            return juce::Result::fail(
                "Swing requires unique current instrument Channel IDs, not Audio Clips");
    for (auto target : ids)
    {
        auto *channel = project.channel(target);
        if (id.endsWith("truncateSwing"))
            channel->truncateSwing = args.enabled;
        else
            channel->swingMix = args.value;
    }
    return juce::Result::ok();
}
} // namespace vibera
