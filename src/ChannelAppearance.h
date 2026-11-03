// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <algorithm>
#include <optional>

namespace vibera
{
inline constexpr std::array<const char *, 9> channelAppearanceIconIds{
    "none", "drum", "hat", "keys", "bass", "synth", "mic", "sample", "audio"};
inline bool validChannelIcon(const juce::String &id)
{
    return std::any_of(channelAppearanceIconIds.begin(), channelAppearanceIconIds.end(),
                       [&](const char *value) { return id == value; });
}
inline bool validChannelColour(juce::uint32 colour) noexcept
{
    return colour == 0 || (colour >> 24) == 0xff;
}
struct ChannelAppearancePatch
{
    std::optional<juce::String> name;
    std::optional<juce::uint32> colour;
    std::optional<juce::String> icon;
};
struct ChannelSortRequest
{
    enum class By
    {
        name,
        colour,
        mixer,
        type
    };
    By by = By::name;
    bool descending = false;
};
} // namespace vibera
