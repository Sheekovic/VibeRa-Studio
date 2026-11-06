// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "MixerNativeIdentity.h"
#include "MixerRouting.h"
#include <algorithm>
#include <optional>
#include <set>

namespace vibera
{
inline bool isMixerInsertLifecycleCommand(const juce::String &id) noexcept
{
    return id == "mixer.insert.create" || id == "mixer.insert.delete";
}
// Canonical private-candidate mutations only. Caller provides a session-reserved
// ID; no native track/bus retention or unseen runtime sidechain proof is implied.
inline juce::Result createMixerInsert(Project &project, Id reserved, const juce::String &requestedName)
{
    if (project.mixerSolo)
        return juce::Result::fail("mixer.soloActive: Restore Mixer solo before changing insert membership");
    if (project.mixer.size() >= MixerNativeIdentity::capacity)
        return juce::Result::fail("mixer.insertLimit: Master plus128 inserts maximum");
    if (reserved < 1 || reserved > 1000000000 || reserved < project.nextId ||
        std::any_of(
            project.mixer.begin(), project.mixer.end(),
            [&](const auto &row) { return Id(row.index) >= reserved; }))
        return juce::Result::fail("mixer.idExhausted: Fresh bounded Mixer identity required");
    const auto name = requestedName.isEmpty() ? "Insert " + juce::String(reserved) : requestedName;
    if (name != name.trim() || name.length() > 64 || name.containsAnyOf("\r\n\t"))
        return juce::Result::fail("mixer.invalidName: Use1..64 trimmed characters without tabs/newlines");
    project.mixer.emplace_back(int(reserved), name, 1.f, 0.f, false);
    project.nextId = std::max(project.nextId, reserved + 1);
    return juce::Result::ok();
}
inline juce::Result deleteMixerInsert(Project &project, int id, const MixerInsertDeletePolicy &policy)
{
    if (id == 0)
        return juce::Result::fail("mixer.masterProtected: Master cannot be deleted");
    const auto *old = mixerInsert(project, id);
    if (!old)
        return juce::Result::fail("mixer.unavailableInsert: Existing Mixer identity required");
    if (project.mixerSolo)
        return juce::Result::fail("mixer.soloActive: Restore Mixer solo before changing insert membership");
    if (mixerDetectorReferences(project, id).size() != 0)
        return juce::Result::fail("mixer.detectorReferenced: Clear surviving effect detector assignments "
                                  "before deleting this source");
    if (policy.replacement && (*policy.replacement == id || !mixerInsert(project, *policy.replacement)))
        return juce::Result::fail("mixer.invalidReplacement: Existing surviving replacement insert required");
    if (!old->effects.empty() && !policy.removeEffects)
        return juce::Result::fail(
            "mixer.effectsPresent: Explicit removal of this insert's captured effects required");
    std::set<Id> removedPlugins;
    for (const auto &effect : old->effects)
        removedPlugins.insert(effect.id);
    const auto removedTarget = [&](AutomationKey key)
    {
        return (key.kind == AutomationParameter::mixerSend &&
                (key.owner == id || key.parameter == std::uint32_t(id))) ||
               ((key.kind == AutomationParameter::mixerGain || key.kind == AutomationParameter::mixerPan ||
                 key.kind == AutomationParameter::mixerStereoSeparation) &&
                key.owner == id) ||
               (key.kind == AutomationParameter::pluginNormalized && removedPlugins.contains(key.owner));
    };
    bool automation =
        std::any_of(project.automation.initialValues.begin(), project.automation.initialValues.end(),
                    [&](const auto &value) { return removedTarget(value.key); });
    for (const auto &source : project.automation.sources)
        automation = automation || std::any_of(source.links.begin(), source.links.end(),
                                               [&](const auto &link) { return removedTarget(link.key); });
    if (automation && !policy.removeAutomation)
        return juce::Result::fail(
            "mixer.automationReferenced: Explicit target-link/initialization removal required");
    const bool grouped = std::any_of(
        project.mixerGroups.begin(), project.mixerGroups.end(), [&](const auto &group)
        { return std::find(group.members.begin(), group.members.end(), id) != group.members.end(); });
    if (grouped && !policy.removeGroupMembership)
        return juce::Result::fail(
            "mixer.groupReferenced: Explicit display-group membership removal required");
    const bool assigned = std::any_of(project.channels.begin(), project.channels.end(),
                                      [&](const auto &channel)
                                      {
                                          return channel.mixer == id ||
                                                 (channel.instrument &&
                                                  std::find(channel.instrument->extraOutputMixers.begin(),
                                                            channel.instrument->extraOutputMixers.end(),
                                                            id) != channel.instrument->extraOutputMixers.end());
                                      });
    bool incoming = false;
    for (const auto &row : project.mixer)
        for (const auto &route : row.routes)
            if (route.target == id)
            {
                incoming = true;
                if (policy.replacement &&
                    (row.index == *policy.replacement ||
                     std::any_of(row.routes.begin(), row.routes.end(),
                                 [&](const auto &r) { return r.target == *policy.replacement; })))
                    return juce::Result::fail(
                        "mixer.routeCollision: Replacement creates self or duplicate send; no level merge");
            }
    if ((assigned || incoming) && !policy.replacement)
        return juce::Result::fail(
            "mixer.replacementRequired: Explicit surviving target for Channels/incoming sends required");
    if (policy.replacement)
    {
        for (auto &channel : project.channels)
        {
            if (channel.mixer == id)
                channel.mixer = *policy.replacement;
            if (channel.instrument)
                for (auto &target : channel.instrument->extraOutputMixers)
                    if (target == id)
                        target = *policy.replacement;
        }
        for (auto &row : project.mixer)
            for (auto &route : row.routes)
                if (route.target == id)
                    route.target = *policy.replacement;
    }
    if (policy.removeAutomation)
    {
        for (auto &source : project.automation.sources)
            std::erase_if(source.links, [&](const auto &link) { return removedTarget(link.key); });
        std::erase_if(project.automation.initialValues,
                      [&](const auto &value) { return removedTarget(value.key); });
        // Curves, points, placements and other target links remain intact,
        // including an empty unlinked source. No implicit music deletion.
    }
    if (policy.removeGroupMembership)
        for (auto &group : project.mixerGroups)
            std::erase(group.members, id);
    std::erase_if(project.mixer, [&](const auto &row) { return row.index == id; });
    if (auto valid = validateMixerRouting(project); valid.failed())
        return juce::Result::fail("mixer.routeCycle: " + valid.getErrorMessage());
    return juce::Result::ok();
}
// Original endpoint proof, never inferred from an action's human-readable name.
// Replays the SAME mutator on a copy and demands whole-document equivalence.
inline bool pureMixerInsertHistory(const Project &before, const Project &after)
{
    std::vector<int> added, removed;
    for (const auto &row : before.mixer)
        if (!mixerInsert(after, row.index))
            removed.push_back(row.index);
    for (const auto &row : after.mixer)
        if (!mixerInsert(before, row.index))
            added.push_back(row.index);
    auto candidate = before;
    if (added.size() == 1 && removed.empty())
    {
        const auto *row = mixerInsert(after, added.front());
        if (createMixerInsert(candidate, row->index, row->name).failed())
            return false;
    }
    else if (removed.size() == 1 && added.empty())
    {
        MixerInsertDeletePolicy policy;
        policy.removeEffects = policy.removeAutomation = policy.removeGroupMembership = true;
        const int id = removed.front();
        const auto replacement = [&](int target)
        {
            if (policy.replacement && *policy.replacement != target)
                return false;
            policy.replacement = target;
            return true;
        };
        for (const auto &channel : before.channels)
            if (channel.mixer == id)
            {
                const auto *next = after.channel(channel.id);
                if (!next || !replacement(next->mixer))
                    return false;
            }
        for (const auto &row : before.mixer)
            if (row.index != id)
            {
                const auto *next = mixerInsert(after, row.index);
                if (!next || next->routes.size() != row.routes.size())
                    return false;
                for (std::size_t i = 0; i < row.routes.size(); ++i)
                    if (row.routes[i].target == id && !replacement(next->routes[i].target))
                        return false;
            }
        if (deleteMixerInsert(candidate, id, policy).failed())
            return false;
    }
    else
        return false;
    return candidate.state().isEquivalentTo(after.state());
}
} // namespace vibera
