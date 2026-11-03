// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <algorithm>
#include <set>

namespace vibera
{
inline Id audibleSoloChannel(const Project &project)
{
    Id result = 0;
    if (project.channels.size() < 2)
        return 0;
    for (const auto &channel : project.channels)
        if (!channel.muted)
        {
            if (result != 0)
                return 0;
            result = channel.id;
        }
    return result;
}
inline bool channelIsSoloed(const Project &project, Id channel)
{
    return project.solo ? project.solo->channel == channel : audibleSoloChannel(project) == channel;
}
inline bool channelIsSoloedInScope(const Project &project, Id channel, bool groupScoped)
{
    return project.solo ? project.solo->channel == channel && project.solo->groupScoped == groupScoped
                        : !groupScoped && audibleSoloChannel(project) == channel;
}
inline bool soloContains(const std::vector<Id> &ids, Id id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
inline void restoreChannelSolo(Project &project, bool restore = true)
{
    if (!project.solo)
        return;
    for (auto &channel : project.channels)
        if (!channel.muteLocked && soloContains(project.solo->affected, channel.id))
            channel.muted = restore && soloContains(project.solo->mutedBefore, channel.id);
}
inline bool belongsToChannelSolo(const Channel &channel, const Project::SoloState &solo)
{
    return !channel.muteLocked && (!solo.groupScoped || channel.group == solo.group);
}
inline juce::Result applyChannelMuteCommand(Project &project, const juce::String &id, const CommandArgs &args)
{
    if (args.soloScope &&
        (id != "channel.solo" || (*args.soloScope != "global" && *args.soloScope != "group")))
        return juce::Result::fail("channel.solo.invalidScope: Expected global or group on channel.solo");
    if (id == "channel.solo" || id == "channel.unsolo")
    {
        if (id == "channel.solo" && !project.channel(args.channel))
            return juce::Result::fail("Solo target Channel is unavailable");
        if (id == "channel.solo" && project.channel(args.channel)->muteLocked)
            return juce::Result::fail("channel.solo.locked: Unlock the target's mute state before soloing");
        const bool exit = id == "channel.unsolo" || !args.enabled;
        if (exit)
        {
            if (project.solo && id == "channel.solo" && args.channel != project.solo->channel)
                return juce::Result::fail("Unsolo target does not own the current solo");
            if (!project.solo && (id == "channel.unsolo" || audibleSoloChannel(project) != args.channel))
                return juce::Result::ok();
            if (project.solo)
                restoreChannelSolo(project, args.restore.value_or(true));
            else
                for (auto &channel : project.channels)
                    if (!channel.muteLocked)
                        channel.muted = false; // Legacy one-audible mask, with no owned baseline.
            project.solo.reset();
        }
        else
        {
            if (!project.solo)
            {
                project.solo = Project::SoloState{args.channel, {}};
                for (const auto &channel : project.channels)
                    if (channel.muted)
                        project.solo->mutedBefore.push_back(channel.id);
            }
            else
                restoreChannelSolo(project); // Leave the previous scope at the first saved baseline.
            project.solo->channel = args.channel;
            project.solo->groupScoped = args.soloScope.value_or("global") == "group";
            project.solo->group = project.solo->groupScoped ? project.channel(args.channel)->group : 0;
            project.solo->affected.clear();
            for (auto &channel : project.channels)
                if (belongsToChannelSolo(channel, *project.solo))
                {
                    project.solo->affected.push_back(channel.id);
                    channel.muted = channel.id != args.channel;
                }
        }
        return juce::Result::ok();
    }
    auto targets = args.ids;
    const bool selected = id == "rack.muteSelected" || id == "rack.unmuteSelected" || id == "rack.muteLock";
    const bool locking = id == "channel.muteLock" || id == "rack.muteLock";
    if (targets.empty() && !selected)
        targets.push_back(args.channel);
    if (targets.empty() || targets.size() > 128)
        return juce::Result::fail("Select one or more Channels to mute");
    std::set<Id> unique;
    for (auto target : targets)
        if (!project.channel(target) || !unique.insert(target).second)
            return juce::Result::fail("Mute targets contain unavailable or duplicate Channel IDs");
    if (locking)
    {
        const bool changed = std::any_of(targets.begin(), targets.end(), [&](Id target)
                                         { return project.channel(target)->muteLocked != args.enabled; });
        if (changed)
        {
            // A new explicit lock never changes the audible mask or allows a
            // later stale restore to overwrite the newly protected state.
            project.solo.reset();
            for (auto target : targets)
                project.channel(target)->muteLocked = args.enabled;
        }
        return juce::Result::ok();
    }
    if (!selected && std::any_of(targets.begin(), targets.end(),
                                 [&](Id target) { return project.channel(target)->muteLocked; }))
        return juce::Result::fail("channel.mute.locked: Unlock the target's mute state before editing it");
    std::erase_if(targets, [&](Id target) { return project.channel(target)->muteLocked; });
    if (targets.empty())
        return juce::Result::ok();
    if (selected && std::none_of(targets.begin(), targets.end(), [&](Id target)
                                 { return project.channel(target)->muted != (id == "rack.muteSelected"); }))
        return juce::Result::ok();
    // A manual change leaves the current audible mask in place and ends the
    // restore session. Undo restores both the mask and the original baseline.
    project.solo.reset();
    for (auto target : targets)
        project.channel(target)->muted = id == "rack.unmuteSelected" ? false
                                         : id == "rack.muteSelected" ? true
                                                                     : args.enabled;
    return juce::Result::ok();
}
inline void reconcileSoloChannels(const Project &before, Project &next)
{
    if (!next.solo)
        return;
    if (!next.channel(next.solo->channel) || next.channel(next.solo->channel)->muteLocked ||
        (next.solo->groupScoped && next.solo->group && !next.channelGroup(next.solo->group)))
    {
        restoreChannelSolo(next);
        next.solo.reset();
        return;
    }
    std::erase_if(next.solo->mutedBefore, [&](Id id) { return !next.channel(id); });
    if (next.solo->groupScoped && next.channel(next.solo->channel)->group != next.solo->group)
    {
        restoreChannelSolo(next);
        next.solo.reset(); // Moving the owner does not silently transfer the solo to another group.
        return;
    }
    const auto previous = next.solo->affected;
    next.solo->affected.clear();
    for (auto &channel : next.channels)
    {
        if (!before.channel(channel.id) && channel.muted)
            next.solo->mutedBefore.push_back(channel.id);
        if (belongsToChannelSolo(channel, *next.solo))
        {
            next.solo->affected.push_back(channel.id);
            channel.muted = channel.id != next.solo->channel;
        }
        else if (!channel.muteLocked && soloContains(previous, channel.id))
            channel.muted = soloContains(next.solo->mutedBefore, channel.id);
    }
}
} // namespace vibera
