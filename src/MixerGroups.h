// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "MixerControls.h"
#include <limits>
#include <set>

namespace vibera
{
inline const MixerGroup *mixerGroup(const Project &project, const juce::String &id)
{
    const auto it = std::find_if(project.mixerGroups.begin(), project.mixerGroups.end(),
                                 [&](const auto &group) { return group.id == id; });
    return it == project.mixerGroups.end() ? nullptr : &*it;
}
inline juce::String mixerGroupFor(const Project &project, int insert)
{
    for (const auto &group : project.mixerGroups)
        if (std::find(group.members.begin(), group.members.end(), insert) != group.members.end())
            return group.id;
    return {};
}
inline juce::String mixerGroupColourActionLabel(juce::uint32 storedColour)
{
    return storedColour == 0 ? "Reset group insert colours to theme" : "Apply this insert colour to group";
}
inline bool validMixerGroupId(const juce::String &id)
{
    return id.length() == 32 && !juce::Uuid(id).isNull() && juce::Uuid(id).toString() == id;
}
inline bool isMixerGroupCommand(const juce::String &id)
{
    return id.startsWith("mixer.group.") || id == "mixer.selection.rename" ||
           id == "mixer.selection.colour";
}
inline juce::Result mixerGroupName(const Project &project, const juce::String &name,
                                   const juce::String &except = {})
{
    if (name.isEmpty() || name != name.trim() || name.length() > 64 || name.containsAnyOf("\r\n\t"))
        return juce::Result::fail("mixer.group.invalidName: Use1..64 trimmed characters without tabs/newlines");
    for (const auto &group : project.mixerGroups)
        if (group.id != except && group.name.equalsIgnoreCase(name))
            return juce::Result::fail("mixer.group.duplicateName: A display group already uses that name");
    return juce::Result::ok();
}
inline bool validMixerColour(double value)
{
    return std::isfinite(value) && value == std::floor(value) && value >= 0 && value <= 4294967295. &&
           (value == 0 || value >= 4278190080.);
}
inline juce::Result mixerMemberTargets(const Project &project, const std::vector<Id> &ids,
                                       bool allowMaster, std::vector<int> &output)
{
    if (ids.empty() || ids.size() > 129)
        return juce::Result::fail("mixer.group.invalidMembers: Select1..129 existing stable insert IDs");
    std::set<int> requested;
    for (const auto id : ids)
        if (id < (allowMaster ? 0 : 1) || id > std::numeric_limits<int>::max() ||
            !mixerInsert(project, int(id)) || !requested.insert(int(id)).second)
            return juce::Result::fail("mixer.group.invalidMembers: Missing/duplicate/Master group member");
    std::vector<int> next;
    for (const auto &insert : project.mixer)
        if (requested.count(insert.index)) next.push_back(insert.index);
    output.swap(next); // Display order only; no ordinal becomes an ID.
    return juce::Result::ok();
}
// Runs only on Controller's private candidate. Validation and all allocation
// precede the ordinary prepared Controller commit; no DSP work is added.
inline juce::Result applyMixerGroup(Project &project, const juce::String &id, const CommandArgs &args)
{
    if (id == "mixer.selection.rename" || id == "mixer.selection.colour")
    {
        if (args.mixerGroup)
            return juce::Result::fail("mixer.group.invalidArguments: Bulk insert edits take ids, not a group");
        std::vector<int> targets;
        if (auto r = mixerMemberTargets(project, args.ids, true, targets); r.failed()) return r;
        const auto prefix = args.text.trim();
        if (id.endsWith("rename") && (prefix.isEmpty() || prefix.length() > 55 || prefix.containsAnyOf("\r\n\t")))
            return juce::Result::fail("mixer.group.invalidName: Bulk rename needs a1..55 character prefix");
        if (id.endsWith("colour") && !validMixerColour(args.value))
            return juce::Result::fail("mixer.group.invalidColour: Use theme0 or opaque ARGB");
        int number = 1;
        for (const auto target : targets)
        {
            auto found = std::find_if(project.mixer.begin(), project.mixer.end(),
                                       [&](const auto &insert) { return insert.index == target; });
            if (id.endsWith("rename")) found->name = prefix + " " + juce::String(number++);
            else found->colour = juce::uint32(args.value);
        }
        return juce::Result::ok();
    }
    if (id == "mixer.group.create")
    {
        if (args.mixerGroup)
            return juce::Result::fail("mixer.group.invalidArguments: New group identity is allocated by the application");
        if (project.mixerGroups.size() >= 128)
            return juce::Result::fail("mixer.group.limit: At most128 display groups");
        if (auto r = mixerGroupName(project, args.text); r.failed()) return r;
        std::vector<int> members;
        if (auto r = mixerMemberTargets(project, args.ids, false, members); r.failed()) return r;
        if (project.mixerSolo && project.mixerSolo->group.isNotEmpty())
            return juce::Result::fail("mixer.group.soloActive: Restore group solo before changing membership");
        for (auto &group : project.mixerGroups)
            std::erase_if(group.members, [&](int member)
                          { return std::find(members.begin(), members.end(), member) != members.end(); });
        std::sort(members.begin(), members.end());
        project.mixerGroups.push_back({juce::Uuid().toString(), args.text, 0, std::move(members)});
        return juce::Result::ok();
    }
    if (!args.mixerGroup || (!args.mixerGroup->isEmpty() && !validMixerGroupId(*args.mixerGroup)))
        return juce::Result::fail("mixer.group.invalidArguments: Explicit canonical group UUID required");
    if (id == "mixer.group.assign")
    {
        if (args.mixerGroup->isNotEmpty() && !mixerGroup(project, *args.mixerGroup))
            return juce::Result::fail("mixer.group.unavailable: Group no longer exists");
        std::vector<int> members;
        if (auto r = mixerMemberTargets(project, args.ids, false, members); r.failed()) return r;
        if (project.mixerSolo && project.mixerSolo->group.isNotEmpty())
            return juce::Result::fail("mixer.group.soloActive: Restore group solo before changing membership");
        for (auto &group : project.mixerGroups)
        {
            std::erase_if(group.members, [&](int member)
                          { return std::find(members.begin(), members.end(), member) != members.end(); });
            if (group.id == *args.mixerGroup)
            {
                group.members.insert(group.members.end(), members.begin(), members.end());
                std::sort(group.members.begin(), group.members.end()); // Storage order, never audio identity.
            }
        }
        return juce::Result::ok();
    }
    auto found = std::find_if(project.mixerGroups.begin(), project.mixerGroups.end(),
                              [&](const auto &group) { return group.id == *args.mixerGroup; });
    if (found == project.mixerGroups.end())
        return juce::Result::fail("mixer.group.unavailable: Group no longer exists");
    if (id == "mixer.group.rename")
    {
        if (auto r = mixerGroupName(project, args.text, found->id); r.failed()) return r;
        found->name = args.text;
    }
    else if (id == "mixer.group.colour")
    {
        if (!validMixerColour(args.value))
            return juce::Result::fail("mixer.group.invalidColour: Use theme0 or opaque ARGB");
        found->colour = juce::uint32(args.value);
    }
    else if (id == "mixer.group.colourMembers")
    {
        const auto *source = mixerInsert(project, args.mixer);
        if (!source || args.mixer <= 0)
            return juce::Result::fail(
                "mixer.group.invalidColourSource: Existing non-Master source insert required");
        if (std::find(found->members.begin(), found->members.end(), args.mixer) == found->members.end())
            return juce::Result::fail(
                "mixer.group.nonMemberColourSource: Source insert must belong to this exact group");
        if (std::any_of(found->members.begin(), found->members.end(),
                        [&](int member) { return member <= 0 || !mixerInsert(project, member); }))
            return juce::Result::fail("mixer.group.invalidMembers: Every group member must still exist");
        // Copy the stored value, not a rendered pixel or a global theme setting.
        // Sentinel0 resets EACH member to its own theme/default presentation.
        const auto sampledColour = source->colour;
        for (auto &insert : project.mixer)
            if (std::find(found->members.begin(), found->members.end(), insert.index) != found->members.end())
                insert.colour = sampledColour;
    }
    else if (id == "mixer.group.delete")
    {
        if (project.mixerSolo && project.mixerSolo->group == found->id)
            return juce::Result::fail("mixer.group.soloActive: Restore this group's solo before dissolving it");
        project.mixerGroups.erase(found); // Dissolve metadata only; all inserts/routes/FX survive.
    }
    else if (id == "mixer.group.mute")
    {
        // Same explicit policy as the existing single-insert manual mute:
        // changing the mask ends restore ownership; it does not restore it.
        project.mixerSolo.reset();
        for (auto &insert : project.mixer)
            if (std::find(found->members.begin(), found->members.end(), insert.index) != found->members.end())
                insert.muted = args.enabled;
    }
    else if (id == "mixer.group.solo" || id == "mixer.group.soloConnected" || id == "mixer.group.unsolo")
    {
        if (id.endsWith("unsolo") || !args.enabled)
        {
            if (!project.mixerSolo || project.mixerSolo->group != found->id)
                return juce::Result::fail("mixer.group.soloOwner: This group does not own the current solo");
            for (auto &insert : project.mixer)
                insert.muted = args.restore.value_or(true) &&
                    std::find(project.mixerSolo->mutedBefore.begin(), project.mixerSolo->mutedBefore.end(), insert.index) != project.mixerSolo->mutedBefore.end();
            project.mixerSolo.reset();
        }
        else
        {
            if (found->members.empty())
                return juce::Result::fail("mixer.group.empty: Add members before soloing the group");
            if (!project.mixerSolo)
            {
                project.mixerSolo = Project::MixerSoloState{found->members.front(), false, {}};
                for (const auto &insert : project.mixer)
                    if (insert.muted) project.mixerSolo->mutedBefore.push_back(insert.index);
            }
            project.mixerSolo->insert = found->members.front();
            project.mixerSolo->group = found->id;
            project.mixerSolo->connected = id.endsWith("soloConnected");
            reconcileMixerSolo(project);
        }
    }
    else return juce::Result::fail("mixer.group.unsupportedOperation: Unknown group operation");
    return juce::Result::ok();
}
} // namespace vibera
