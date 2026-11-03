// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerGroups.h"
namespace vibera
{
// Read-only canonical dependency inspection. Availability is MODEL eligibility
// with explicit deletion policy, not an asynchronous audio admission/result.
inline juce::var mixerDeletionReferences(const Project &project, int id)
{
    auto *state = new juce::DynamicObject;
    juce::Array<juce::var> channels, incoming, automation, groups;
    const auto *insert = mixerInsert(project, id);
    const auto detectors = mixerDetectorReferences(project, id);
    const auto matches = [&](AutomationKey key)
    {
        if (key.kind == AutomationParameter::mixerSend &&
            (key.owner == id || key.parameter == std::uint32_t(id)))
            return true;
        if ((key.kind == AutomationParameter::mixerGain || key.kind == AutomationParameter::mixerPan ||
             key.kind == AutomationParameter::mixerStereoSeparation) &&
            key.owner == id)
            return true;
        return insert && key.kind == AutomationParameter::pluginNormalized &&
               std::any_of(insert->effects.begin(), insert->effects.end(),
                            [&](const auto &effect) { return effect.id == key.owner; });
    };
    for (const auto &channel : project.channels) if (channel.mixer == id) channels.add(juce::int64(channel.id));
    for (const auto &source : project.mixer)
        for (const auto &route : source.routes) if (route.target == id) incoming.add(source.index);
    for (const auto &source : project.automation.sources)
        if (std::any_of(source.links.begin(), source.links.end(), [&](const auto &link) { return matches(link.key); }))
            automation.add(juce::int64(source.id));
    int initialized = 0;
    for (const auto &value : project.automation.initialValues) initialized += matches(value.key);
    for (const auto &group : project.mixerGroups)
        if (std::find(group.members.begin(), group.members.end(), id) != group.members.end()) groups.add(group.id);
    state->setProperty("insert", id); state->setProperty("exists", insert != nullptr);
    state->setProperty("masterProtected", id == 0); state->setProperty("assignedChannels", channels);
    state->setProperty("incomingRoutes", incoming); state->setProperty("automationSources", automation);
    state->setProperty("initializedTargets", initialized); state->setProperty("groups", groups);
    state->setProperty("effects", insert ? int(insert->effects.size()) : 0);
    state->setProperty("soloSession", bool(project.mixerSolo));
    state->setProperty("detectors", detectors);
    state->setProperty("available", insert && id != 0 && !project.mixerSolo && detectors.size() == 0);
    state->setProperty("errorCode", !insert             ? "mixer.unavailableInsert"
                                    : id == 0           ? "mixer.masterProtected"
                                    : project.mixerSolo ? "mixer.soloActive"
                                    : detectors.size()  ? "mixer.detectorReferenced"
                                                        : "");
    state->setProperty("reason",
                       "Model eligibility only. Use mixer.insert.delete with explicit mixerDelete policy; "
                       "shared application admission also requires no "
                       "recording/take/gesture/native/file/routing preparation. Poll the returned actual "
                       "chain job through audio activation; a brief structural interruption is allowed.");
    state->setProperty(
        "requiredDeletePolicy",
        "Master protected. Explicit surviving replacement for assigned Channels/incoming sends; preserve "
        "levels, reject self/cycle/duplicate-target collisions. Explicitly remove FX/automation/group "
        "membership and finish solo first; never prune external controller profiles. Removed stable IDs do "
        "not recycle within the current projectIdentity session, including Undo branches; reopening an "
        "earlier snapshot creates a new identity and reservation session.");
    return juce::var(state);
}
} // namespace vibera
