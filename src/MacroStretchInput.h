// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "NoteStretch.h"
#include <cstdlib>
namespace vibera
{
inline juce::Result parseMacroStretchRatio(const juce::String &text, double &result)
{
    auto number = text.trim().toStdString();
    char *end = nullptr;
    if (number.empty())
        return juce::Result::fail("Enter a stretch ratio, for example2 or0.5");
    const auto value = std::strtod(number.c_str(), &end);
    if (end == number.c_str() || end != number.c_str() + number.size())
        return juce::Result::fail("Enter one complete numeric stretch ratio");
    if (auto r = validateStretchRatio(value); r.failed())
        return r;
    result = value;
    return juce::Result::ok();
}
} // namespace vibera
