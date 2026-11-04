// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#pragma once
#include "Commands.h"
#include <charconv>
#include <cmath>
namespace vibera
{
// Context supplies the source insert. Target:amount is bounded and fully consumed.
// For route, amount is0/1; for send it is a percentage; routeOnly uses target only.
inline juce::Result parseMixerMacro(const juce::String &action, const juce::String &input, CommandArgs &out)
{
    const auto text = input.trim().toStdString();
    const auto colon = text.find(':');
    const auto targetText = text.substr(0, colon);
    int target = -1;
    auto parsed = vibera::number_text::fromChars(targetText.data(), targetText.data() + targetText.size(), target);
    if (parsed.ec != std::errc{} || parsed.ptr != targetText.data() + targetText.size() || target < 0)
        return juce::Result::fail("Enter an existing destination insert index (0=Master)");
    CommandArgs next = out;
    next.target = target;
    if (action == "mixer.routeOnly")
    {
        if (colon != std::string::npos)
            return juce::Result::fail("Route only accepts a destination index");
    }
    else
    {
        if (colon == std::string::npos)
            return juce::Result::fail("Use destination:amount (e.g.2:50)");
        const auto amount = text.substr(colon + 1);
        double value = 0;
        auto number = vibera::number_text::fromChars(amount.data(), amount.data() + amount.size(), value);
        if (number.ec != std::errc{} || number.ptr != amount.data() + amount.size() || !std::isfinite(value))
            return juce::Result::fail("Amount must be a complete finite number");
        if (action == "mixer.send")
        {
            if (value < 0 || value > 200)
                return juce::Result::fail("Send percentage must be0..200");
            next.value = value / 100;
        }
        else
        {
            if (value != 0 && value != 1)
                return juce::Result::fail("Enable is0 or1");
            next.enabled = value == 1;
        }
    }
    out = std::move(next);
    return juce::Result::ok();
}
} // namespace vibera
