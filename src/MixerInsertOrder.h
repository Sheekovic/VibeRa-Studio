// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <algorithm>
#include <optional>
#include <type_traits>

namespace vibera
{
inline bool isMixerInsertOrderCommand(const juce::String &id) noexcept
{
    return id == "mixer.move" || id == "mixer.moveLeft" || id == "mixer.moveRight";
}
inline std::optional<int> mixerInsertPosition(const Project &project, int source) noexcept
{
    for (size_t i = 0; i < project.mixer.size(); ++i)
        if (project.mixer[i].index == source)
            return int(i);
    return {};
}
struct MixerInsertOrderPlan
{
    int source = 0, from = 0, to = 0;
    bool noOp() const noexcept
    {
        return from == to;
    }
};
inline juce::Result planMixerInsertOrder(const Project &project, const juce::String &command,
                                         const CommandArgs &args, MixerInsertOrderPlan &output)
{
    if (!isMixerInsertOrderCommand(command))
        return juce::Result::fail("mixer.unsupportedOperation: Expected an insert order command");
    const auto from = mixerInsertPosition(project, args.mixer);
    if (!from)
        return juce::Result::fail("mixer.unavailableInsert: Existing stable Mixer ID required");
    if (args.mixer == 0)
        return juce::Result::fail("mixer.masterProtected: Master row cannot move");
    int to = *from;
    if (command == "mixer.move")
    {
        if (!args.mixerPosition || *args.mixerPosition < 0 ||
            *args.mixerPosition >= int(project.mixer.size()))
            return juce::Result::fail(
                "mixer.invalidPosition: Explicit existing zero-based display row required");
        to = *args.mixerPosition;
        if (project.mixer[size_t(to)].index == 0)
            return juce::Result::fail("mixer.masterProtected: Master display row remains reserved");
    }
    else
    {
        if (args.mixerPosition)
            return juce::Result::fail(
                "mixer.invalidArguments: Relative order commands do not take mixerPosition");
        const int direction = command == "mixer.moveLeft" ? -1 : 1;
        int candidate = *from + direction;
        while (candidate >= 0 && candidate < int(project.mixer.size()) &&
               project.mixer[size_t(candidate)].index == 0)
            candidate += direction;
        if (candidate >= 0 && candidate < int(project.mixer.size()))
            to = candidate;
        // Boundary is a genuine no-op. No clamping of explicit absolute positions.
    }
    output = {args.mixer, *from, to};
    return juce::Result::ok();
}
inline juce::Result applyMixerInsertOrder(Project &project, const juce::String &command,
                                          const CommandArgs &args)
{
    MixerInsertOrderPlan plan;
    if (auto result = planMixerInsertOrder(project, command, args, plan); result.failed())
        return result;
    static_assert(std::is_nothrow_swappable_v<MixerInsert>);
    const int direction = plan.to > plan.from ? 1 : -1;
    for (int position = plan.from; position != plan.to;)
    {
        int next = position + direction;
        while (project.mixer[size_t(next)].index == 0)
            next += direction;
        std::swap(project.mixer[size_t(position)], project.mixer[size_t(next)]);
        position = next;
    }
    // Complete values moved. Stable IDs, routes, effects/state, automation and
    // external bindings are never renumbered. No native/DSP work in this helper.
    return juce::Result::ok();
}
struct MixerInsertOrderIntent
{
    juce::String project;
    std::uint64_t revision = 0;
    int source = 0;
    std::vector<int> order;
    static MixerInsertOrderIntent capture(const Project &document, const juce::String &identity,
                                          std::uint64_t revision, int source)
    {
        MixerInsertOrderIntent result{identity, revision, source, {}};
        for (const auto &insert : document.mixer)
            result.order.push_back(insert.index);
        return result;
    }
    juce::Result validate(const Project &document, const juce::String &identity, std::uint64_t current) const
    {
        if (project != identity || revision != current || order.size() != document.mixer.size())
            return juce::Result::fail("mixer.staleOrder: Reopen the insert order menu");
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] != document.mixer[i].index)
                return juce::Result::fail("mixer.staleOrder: Mixer display order changed");
        if (!mixerInsertPosition(document, source))
            return juce::Result::fail("mixer.unavailableInsert: Source insert no longer exists");
        return juce::Result::ok();
    }
};
inline juce::var mixerInsertOrderState(const Project &document, int selected)
{
    auto *result = new juce::DynamicObject;
    juce::Array<juce::var> order;
    for (const auto &insert : document.mixer)
        order.add(insert.index);
    result->setProperty("order", order);
    result->setProperty("selectedInsert", selected);
    const auto position = mixerInsertPosition(document, selected);
    result->setProperty("position", position ? juce::var(*position) : juce::var());
    for (const auto direction : {"Left", "Right"})
    {
        CommandArgs args;
        args.mixer = selected;
        MixerInsertOrderPlan plan;
        const bool available =
            planMixerInsertOrder(document, "mixer.move" + juce::String(direction), args, plan).wasOk() &&
            !plan.noOp();
        result->setProperty("canMove" + juce::String(direction), available);
    }
    result->setProperty("units", "zero-based display row; index is stable Mixer identity, not row");
    return juce::var(result);
}
} // namespace vibera
