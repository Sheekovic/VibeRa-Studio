// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "NoteSlice.h"
#include <cstdlib>
namespace vibera
{
inline juce::Result parseMacroSliceBeat(const juce::String &text, NoteSliceLine &result)
{
    const auto number = text.trim().toStdString();
    if (number.empty())
        return juce::Result::fail("Enter a number in beats, for example0.5");
    char *end = nullptr;
    const auto value = std::strtod(number.c_str(), &end);
    if (end == number.c_str() || end != number.c_str() + number.size())
        return juce::Result::fail("Enter a number in beats, for example0.5");
    NoteSliceLine line;
    line.startBeat = line.endBeat = value;
    line.grid = 0;
    if (auto r = validateSliceLine(line); r.failed())
        return r;
    result = line;
    return juce::Result::ok();
}
} // namespace vibera
