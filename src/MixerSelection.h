// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerGroups.h"
#include "MixerInsertOrder.h"

namespace vibera
{
// CONTROL workspace state only: never changes routing source behind the caller,
// sends, musical history or document persistence. Publish a fully prepared value.
struct MixerSelection
{
    juce::String project;
    std::vector<int> ids;
    int active = 1, anchor = 1;
    bool contains(int id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
    static juce::Result prepare(const Project &document, const juce::String &identity,
                                const MixerSelection &current, const std::vector<int> &requested,
                                const juce::String &mode, std::optional<int> active, MixerSelection &output)
    {
        if (mode != "replace" && mode != "add" && mode != "remove" && mode != "toggle" && mode != "range")
            return juce::Result::fail("mixer.selection.invalidMode: Use replace/add/remove/toggle/range");
        if (requested.empty() || requested.size() > 129)
            return juce::Result::fail("mixer.selection.invalidTargets: Nonempty existing ID list required");
        std::set<int> targets;
        for (int id : requested)
            if (!mixerInsert(document, id) || !targets.insert(id).second)
                return juce::Result::fail("mixer.selection.invalidTargets: Missing/duplicate Mixer identity");
        if (active && !mixerInsert(document, *active))
            return juce::Result::fail("mixer.selection.unavailable: Active source is unavailable");
        auto next = current;
        if (current.project != identity)
            next = {identity, {}, requested.front(), requested.front()};
        next.project = identity;
        std::erase_if(next.ids, [&](int id) { return !mixerInsert(document, id); });
        if (mode == "replace") next.ids.clear();
        if (mode == "range")
        {
            if (requested.size() != 1 || !mixerInsert(document, next.anchor))
                return juce::Result::fail("mixer.selection.invalidRange: One destination and an existing anchor required");
            const auto a = mixerInsertPosition(document, next.anchor), b = mixerInsertPosition(document, requested.front());
            next.ids.clear();
            for (int position = std::min(*a, *b); position <= std::max(*a, *b); ++position)
                next.ids.push_back(document.mixer[size_t(position)].index);
        }
        else
            for (int id : requested)
            {
                const bool existing = next.contains(id);
                if (mode == "remove" || (mode == "toggle" && existing))
                    std::erase(next.ids, id);
                else if (!existing) next.ids.push_back(id);
            }
        const int source = active.value_or(requested.back());
        if (next.ids.empty())
            return juce::Result::fail("mixer.selection.empty: Keep at least one selected insert");
        next.active = next.contains(source) ? source : next.contains(next.active) ? next.active : next.ids.front();
        if (active && next.active != *active)
            return juce::Result::fail("mixer.selection.invalidActive: Explicit active source must remain selected");
        if (mode != "range") next.anchor = next.active;
        std::vector<int> ordered;
        for (const auto &insert : document.mixer)
            if (next.contains(insert.index)) ordered.push_back(insert.index);
        next.ids.swap(ordered);
        output = std::move(next);
        return juce::Result::ok();
    }
};
} // namespace vibera
