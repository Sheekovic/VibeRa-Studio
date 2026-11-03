// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace vibera
{
inline bool isChannelAppearanceCommand(const juce::String &id)
{
    return id == "channel.properties" || id == "rack.properties" || id == "rack.sort";
}
inline juce::Result applyChannelAppearance(Project &project, const juce::String &id, const CommandArgs &args)
{
    const auto fail = [](const juce::String &message)
    { return juce::Result::fail("channelAppearance.invalid: " + message); };
    const auto ids = id == "channel.properties" ? std::vector<Id>{args.channel} : args.ids;
    std::set<Id> unique;
    if (ids.empty() || ids.size() > 128)
        return fail("Choose 1-128 explicit Channels");
    for (const auto target : ids)
        if (!project.channel(target) || !unique.insert(target).second)
            return fail("Channel targets must exist and be distinct");
    if (id == "rack.sort")
    {
        if (!args.channelSort || args.appearance)
            return fail("Sorting requires channelSort only");
        auto request = *args.channelSort;
        std::vector<size_t> slots;
        std::vector<Channel> channels;
        for (size_t index = 0; index < project.channels.size(); ++index)
            if (unique.count(project.channels[index].id))
            {
                slots.push_back(index);
                channels.push_back(project.channels[index]);
            }
        const auto type = [](const Channel &channel)
        {
            return channel.instrument ? 0 : channel.audioClip ? 2 : 1;
        };
        // Document-assigned colours only: theme-following Channels form one
        // stable group first. A global theme edit never changes document order.
        const auto colourKey = [](juce::uint32 colour)
        {
            if (colour == 0)
                return -1.0;
            const double red = (colour >> 16) & 255, green = (colour >> 8) & 255, blue = colour & 255;
            const double hi = std::max({red, green, blue}), lo = std::min({red, green, blue});
            if (hi == lo)
                return 7.0;
            double hue = hi == red     ? (green - blue) / (hi - lo)
                         : hi == green ? 2 + (blue - red) / (hi - lo)
                                       : 4 + (red - green) / (hi - lo);
            return hue < 0 ? hue + 6 : hue;
        };
        if (request.by != ChannelSortRequest::By::name && request.by != ChannelSortRequest::By::colour &&
            request.by != ChannelSortRequest::By::mixer && request.by != ChannelSortRequest::By::type)
            return fail("Unknown sort criterion");
        std::stable_sort(channels.begin(), channels.end(),
                         [&](const Channel &left, const Channel &right)
                         {
                             int comparison = 0;
                             if (request.by == ChannelSortRequest::By::name)
                                 comparison = left.name.compareIgnoreCase(right.name);
                             else
                             {
                                 const auto a =
                                     request.by == ChannelSortRequest::By::mixer  ? double(left.mixer)
                                     : request.by == ChannelSortRequest::By::type ? double(type(left))
                                                                                  : colourKey(left.colour);
                                 const auto b =
                                     request.by == ChannelSortRequest::By::mixer  ? double(right.mixer)
                                     : request.by == ChannelSortRequest::By::type ? double(type(right))
                                                                                  : colourKey(right.colour);
                                 comparison = a < b ? -1 : a > b ? 1 : 0;
                             }
                             return request.descending ? comparison > 0 : comparison < 0;
                         });
        for (size_t index = 0; index < slots.size(); ++index)
            project.channels[slots[index]] = std::move(channels[index]);
        return juce::Result::ok();
    }
    if (!args.appearance || args.channelSort)
        return fail("A nonempty appearance patch is required");
    const auto &patch = *args.appearance;
    if ((!patch.name && !patch.colour && !patch.icon) || (id == "rack.properties" && patch.name))
        return fail("Bulk properties support colour/icon; names require one explicit Channel");
    const auto name = patch.name ? patch.name->trim() : juce::String();
    if (patch.name && (name.isEmpty() || name.length() > 256 || name.containsAnyOf("\r\n")))
        return fail("Name must have 1-256 characters on one line");
    if (patch.colour && !validChannelColour(*patch.colour))
        return fail("Colour must be zero (theme) or opaque ARGB");
    if (patch.icon && !validChannelIcon(*patch.icon))
        return fail("Unknown original Channel icon");
    for (const auto target : ids)
    {
        auto &channel = *project.channel(target);
        if (patch.name)
            channel.name = name;
        if (patch.colour)
            channel.colour = *patch.colour;
        if (patch.icon)
            channel.icon = *patch.icon;
    }
    return juce::Result::ok();
}
} // namespace vibera
