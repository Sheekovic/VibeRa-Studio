// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vibera
{
// CONTROL canonical model operation. Moving the existing complete value retains
// plugin ID, opaque state, descriptor, bypass and automation bindings.
inline juce::Result moveMixerEffect(Project &project, Id id, int destination)
{
    for (auto &insert : project.mixer)
        for (size_t slot = 0; slot < insert.effects.size(); ++slot)
            if (insert.effects[slot].id == id)
            {
                if (destination < 0 || destination >= int(insert.effects.size()))
                    return juce::Result::fail(
                        "Effect destination must be an existing zero-based slot in its own Mixer chain");
                if (int(slot) == destination)
                    return juce::Result::ok();
                auto effect = std::move(insert.effects[slot]);
                insert.effects.erase(insert.effects.begin() + std::ptrdiff_t(slot));
                insert.effects.insert(insert.effects.begin() + destination, std::move(effect));
                return juce::Result::ok();
            }
    return juce::Result::fail(
        "Reorder requires an existing Mixer effect ID, not an instrument or Channel ID");
}

// An owned UI intent, not a Controller preview/transaction and not a DSP plan.
// Capture/readback on CONTROL only. No project/audio changes until canonical
// command dispatch on drop. Revision + exact order protect delayed menu/drag work.
struct MixerEffectOrderIntent
{
    juce::String project, descriptor;
    std::uint64_t revision = 0;
    Id source = 0;
    int insert = 0, sourceSlot = 0, destination = -1;
    std::vector<Id> order;

    static std::unique_ptr<MixerEffectOrderIntent> capture(const Project &document,
                                                           const juce::String &identity,
                                                           std::uint64_t revision, int insert, Id source)
    {
        for (const auto &chain : document.mixer)
            if (chain.index == insert)
                for (size_t index = 0; index < chain.effects.size(); ++index)
                    if (chain.effects[index].id == source)
                    {
                        auto intent = std::make_unique<MixerEffectOrderIntent>();
                        intent->project = identity;
                        intent->revision = revision;
                        intent->insert = insert;
                        intent->source = source;
                        intent->sourceSlot = int(index);
                        intent->descriptor = chain.effects[index].descriptor;
                        for (const auto &effect : chain.effects)
                            intent->order.push_back(effect.id);
                        return intent;
                    }
        return {};
    }
    juce::Result validate(const Project &document, const juce::String &identity,
                          std::uint64_t currentRevision, int selectedInsert) const
    {
        if (identity != project || currentRevision != revision || selectedInsert != insert)
            return juce::Result::fail("Effect order context changed; start the reorder again");
        if (sourceSlot < 0 || size_t(sourceSlot) >= order.size() || order[size_t(sourceSlot)] != source)
            return juce::Result::fail("Effect order intent is invalid");
        for (const auto &chain : document.mixer)
            if (chain.index == insert)
            {
                if (chain.effects.size() != order.size())
                    break;
                for (size_t index = 0; index < order.size(); ++index)
                    if (chain.effects[index].id != order[index])
                        return juce::Result::fail("Effect order changed; start the reorder again");
                const auto *effect = document.plugin(source);
                if (effect && effect->descriptor == descriptor)
                    return juce::Result::ok();
                break;
            }
        return juce::Result::fail("Effect was replaced or removed; start the reorder again");
    }
};
} // namespace vibera
