// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <set>

namespace vibera
{
inline juce::Result applyChannelManagement(Project &project, const juce::String &id, const CommandArgs &args)
{
    auto targets = args.ids;
    if (targets.empty() && args.channel)
        targets.push_back(args.channel);
    std::set<Id> chosen;
    if (targets.empty() || targets.size() > size_t(Project::maxChannels))
        return juce::Result::fail("Choose 1.." + juce::String(Project::maxChannels) + " Channels");
    for (auto target : targets)
        if (!project.channel(target) || !chosen.insert(target).second)
            return juce::Result::fail("Channel targets are unavailable or duplicated");
    if (id == "channel.clone")
    {
        if (project.channels.size() + chosen.size() > size_t(Project::maxChannels))
            return juce::Result::fail("Cloning would exceed the " + juce::String(Project::maxChannels) + " Channel limit");
        auto original = project.channels;
        juce::StringArray names;
        for (const auto &channel : original)
            names.add(channel.name);
        project.channels.clear();
        for (const auto &channel : original)
        {
            project.channels.push_back(channel);
            if (!chosen.contains(channel.id))
                continue;
            auto clone = channel;
            clone.id = project.allocate();
            for (int suffix = 2;; ++suffix)
            {
                clone.name = channel.name + " #" + juce::String(suffix);
                if (!names.contains(clone.name, true))
                    break;
            }
            names.add(clone.name);
            if (clone.instrument)
                clone.instrument->id = project.allocate();
            if (project.solo)
                clone.muted = std::find(project.solo->mutedBefore.begin(), project.solo->mutedBefore.end(),
                                        channel.id) != project.solo->mutedBefore.end();
            project.channels.push_back(std::move(clone));
        }
        // Clone settings/instrument only. Score/placements/automation links stay
        // on the original; Copy/Paste explicitly transfers the current score.
    }
    else if (id == "channel.delete")
    {
        std::set<Id> plugins;
        for (const auto &channel : project.channels)
            if (chosen.contains(channel.id) && channel.instrument)
                plugins.insert(channel.instrument->id);
        auto removedKey = [&](AutomationKey key)
        {
            return ((key.kind == AutomationParameter::channelGain ||
                     key.kind == AutomationParameter::channelPan ||
                     key.kind == AutomationParameter::samplerModulation) &&
                    chosen.contains(key.owner)) ||
                   (key.kind == AutomationParameter::pluginNormalized && plugins.contains(key.owner));
        };
        for (auto &source : project.automation.sources)
            std::erase_if(source.links, [&](const auto &link) { return removedKey(link.key); });
        std::erase_if(project.automation.initialValues,
                      [&](const auto &value) { return removedKey(value.key); });
        std::erase_if(project.automation.modulators,
                      [&](const auto &modulator) { return removedKey(modulator.link.key); });
        for (auto &macro : project.automation.macros)
            std::erase_if(macro.targets, [&](const auto &link) { return removedKey(link.key); });
        for (auto &pattern : project.patterns)
        {
            std::erase_if(pattern.notes, [&](const auto &note) { return chosen.contains(note.channel); });
            std::erase_if(pattern.channelLoops,
                          [&](const auto &loop) { return chosen.contains(loop.channel); });
        }
        std::erase_if(project.audioPlacements,
                      [&](const auto &clip) { return chosen.contains(clip.channel); });
        std::erase_if(project.samplerControllerLinks,
                      [&](const auto &link) { return chosen.contains(link.channel); });
        std::erase_if(project.channels, [&](const auto &channel) { return chosen.contains(channel.id); });
        // Keep orphaned automation shapes/placements for explicit relinking.
        // No source sample, plugin binary or other user file is ever deleted.
    }
    else if (id == "channel.moveUp")
    {
        for (size_t index = 1; index < project.channels.size(); ++index)
            if (chosen.contains(project.channels[index].id) &&
                !chosen.contains(project.channels[index - 1].id))
                std::swap(project.channels[index], project.channels[index - 1]);
    }
    else if (id == "channel.moveDown")
    {
        for (size_t index = project.channels.size(); index > 1; --index)
            if (chosen.contains(project.channels[index - 2].id) &&
                !chosen.contains(project.channels[index - 1].id))
                std::swap(project.channels[index - 2], project.channels[index - 1]);
    }
    else
        return juce::Result::fail("Unsupported Channel management command");
    return juce::Result::ok();
}
} // namespace vibera
