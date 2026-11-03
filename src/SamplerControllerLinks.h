// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerAutomationParameters.h"
#include <juce_core/juce_core.h>
#include <cstdint>
#include <cmath>

namespace vibera
{
// Project-owned fixed destination. MIDI channel is deliberately named separately
// from the stable score Channel. No device handle, runtime pointer or global prefs.
struct SamplerControllerLink
{
    std::int64_t id = 0, channel = 0;
    std::uint32_t parameter = 0;
    int port = 0, midiChannel = 0, cc = 0, layer = 0;
    double minimum = 0, maximum = 1;
    bool pickup = true, enabled = true;
    juce::String label;
    bool operator==(const SamplerControllerLink &) const = default;
};
inline constexpr size_t samplerControllerLinkLimit = 512;
inline bool samplerControllerSourceOverlaps(const SamplerControllerLink &a,
                                            const SamplerControllerLink &b) noexcept
{
    return a.enabled && b.enabled && a.port == b.port && a.cc == b.cc && a.layer == b.layer &&
           (a.midiChannel == 0 || b.midiChannel == 0 || a.midiChannel == b.midiChannel);
}
// CONTROL-only metadata validation. id0 is a not-yet-allocated create candidate;
// Project validation owns actual positive/global ID uniqueness and owner existence.
inline juce::Result validateSamplerControllerLink(const SamplerControllerLink &link)
{
    const auto parameter = lookupSamplerAutomationParameter(link.parameter);
    if (link.id < 0 || link.id > 1000000000 || link.channel < 1 || link.channel > 1000000000 ||
        link.port < 0 || link.port > 1 || link.midiChannel < 0 || link.midiChannel > 16 || link.cc < 0 ||
        link.cc > 127 || (link.port == 0 && (link.cc == 64 || link.cc == 120 || link.cc == 123)) ||
        link.layer < 0 || link.layer > 3 || link.label.length() > 120 || link.label.containsAnyOf("\r\n\t"))
        return juce::Result::fail("samplerController.invalidLink: Invalid identity/source/label");
    if (!parameter || !samplerAutomationValueValid(*parameter, link.minimum) ||
        !samplerAutomationValueValid(*parameter, link.maximum) || link.minimum >= link.maximum)
        return juce::Result::fail(
            "samplerController.invalidRange: Known parameter and ascending physical endpoints required");
    return juce::Result::ok();
}
} // namespace vibera
