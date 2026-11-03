// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "MixerRouting.h"
#include <algorithm>

namespace vibera
{
// Internal CONTROL/audio admission, not a user-facing model mutation. New
// public reuse operations still need their canonical Controller transaction.
struct StoppedEffectChangePlan
{
    int insert = 0;
    bool appending = false;
    std::vector<PluginSpec> added, removed;
};
inline juce::Result planStoppedEffectChange(const Project &source, const Project &destination, int insert,
                                            StoppedEffectChangePlan &output)
{
    if (auto valid = source.validate(); valid.failed())
        return juce::Result::fail("effects.invalidSource: " + valid.getErrorMessage());
    if (auto valid = destination.validate(); valid.failed())
        return juce::Result::fail("effects.invalidDestination: " + valid.getErrorMessage());
    const auto *old = mixerInsert(source, insert), *next = mixerInsert(destination, insert);
    if (!old || !next)
        return juce::Result::fail("effects.unavailableInsert: Existing stable insert required");
    if (old->effects.empty() == next->effects.empty())
        return juce::Result::fail(
            "effects.unsupportedDelta: Only empty-chain append or its complete removal inverse is admitted");
    auto normalized = destination;
    auto *target = const_cast<MixerInsert *>(mixerInsert(normalized, insert));
    target->effects = old->effects;
    target->effectsBypassed = old->effectsBypassed;
    normalized.nextId = source.nextId; // Allocation/history metadata only; never native identity.
    if (!normalized.state().isEquivalentTo(source.state()))
        return juce::Result::fail(
            "effects.unsupportedDelta: Channels, routes, other chains, music and bindings must remain exact");
    StoppedEffectChangePlan plan;
    plan.insert = insert;
    plan.appending = old->effects.empty();
    plan.added = plan.appending ? next->effects : std::vector<PluginSpec>{};
    plan.removed = plan.appending ? std::vector<PluginSpec>{} : old->effects;
    output = std::move(plan);
    return juce::Result::ok();
}
} // namespace vibera
