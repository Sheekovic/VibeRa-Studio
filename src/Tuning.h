// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// Per-Channel 12-tone tuning tables: cents added to every note of a pitch class. Built
// from a 12-note Scala (.scl) scale on a tonic, or from a maqam preset.
namespace vibera::tuning
{
using Table = std::array<double, 12>;

// Parses Scala text: '!' comments, a description, a note count, then one pitch per line
// (cents with a '.', or a ratio a/b or an integer). The implicit 1/1 is degree 0 and the
// last pitch is the period. Only 12-note scales of a 1200-cent period map to keys.
inline juce::Result parseScala(const juce::String &text, int tonic, Table &out)
{
    juce::StringArray lines;
    for (auto line : juce::StringArray::fromLines(text))
        if (!line.trimStart().startsWith("!"))
            lines.add(line.trim());
    if (lines.size() < 2)
        return juce::Result::fail("Not a Scala scale");
    const int count = lines[1].getIntValue();
    if (count != 12 || lines.size() < 2 + count)
        return juce::Result::fail("Only 12-note Scala scales map onto the keyboard (this one has " +
                                  juce::String(count) + ")");
    std::array<double, 13> cents{};
    for (int i = 0; i < count; ++i)
    {
        auto value = lines[2 + i].upToFirstOccurrenceOf(" ", false, false).trim();
        double c = 0;
        if (value.contains("."))
            c = value.getDoubleValue();
        else
        {
            const double numerator = value.upToFirstOccurrenceOf("/", false, false).getDoubleValue();
            const double denominator = value.contains("/") ? value.fromFirstOccurrenceOf("/", false, false).getDoubleValue() : 1.;
            if (!(numerator > 0) || !(denominator > 0))
                return juce::Result::fail("Invalid Scala pitch: " + value);
            c = 1200. * std::log2(numerator / denominator);
        }
        if (!std::isfinite(c))
            return juce::Result::fail("Invalid Scala pitch: " + value);
        cents[size_t(i + 1)] = c;
    }
    if (std::abs(cents[12] - 1200.) > .5)
        return juce::Result::fail("Only scales with an octave period map onto the keyboard");
    for (int degree = 0; degree < 12; ++degree)
    {
        const double offset = cents[size_t(degree)] - 100. * degree;
        if (std::abs(offset) > 100)
            return juce::Result::fail("A degree is more than a semitone from its key");
        out[size_t((tonic + degree) % 12)] = offset;
    }
    return juce::Result::ok();
}

// Any Scala scale as its degree list: N pitches in cents after the implicit 0, the last being
// the period (usually 1200). Used for keyboard-mapped scales of any size (non-12 layouts).
inline juce::Result parseScalaDegrees(const juce::String &text, std::vector<double> &out)
{
    juce::StringArray lines;
    for (auto line : juce::StringArray::fromLines(text))
        if (!line.trimStart().startsWith("!"))
            lines.add(line.trim());
    if (lines.size() < 2)
        return juce::Result::fail("Not a Scala scale");
    const int count = lines[1].getIntValue();
    if (count < 1 || count > 128 || lines.size() < 2 + count)
        return juce::Result::fail("A Scala scale needs 1..128 pitches");
    std::vector<double> degrees;
    for (int i = 0; i < count; ++i)
    {
        auto value = lines[2 + i].upToFirstOccurrenceOf(" ", false, false).trim();
        double c = 0;
        if (value.contains("."))
            c = value.getDoubleValue();
        else
        {
            const double numerator = value.upToFirstOccurrenceOf("/", false, false).getDoubleValue();
            const double denominator =
                value.contains("/") ? value.fromFirstOccurrenceOf("/", false, false).getDoubleValue() : 1.;
            if (!(numerator > 0) || !(denominator > 0))
                return juce::Result::fail("Invalid Scala pitch: " + value);
            c = 1200. * std::log2(numerator / denominator);
        }
        if (!std::isfinite(c) || c <= 0 || c > 4800 || (!degrees.empty() && c <= degrees.back()))
            return juce::Result::fail("Scala pitches must rise above 0 (at most 4800 cents): " + value);
        degrees.push_back(c);
    }
    out = std::move(degrees);
    return juce::Result::ok();
}
// Keyboard mapping of a degree list: `root` plays unchanged, each key up/down is the next/
// previous degree, repeating every period. Returns cents to add to the key's equal pitch.
inline double scaleDeviation(const std::vector<double> &degrees, int root, int key) noexcept
{
    if (degrees.empty())
        return 0;
    const int count = int(degrees.size()), steps = key - root;
    const int period = int(std::floor(double(steps) / count));
    const int degree = steps - period * count;
    const double cents = period * degrees.back() + (degree == 0 ? 0. : degrees[size_t(degree - 1)]);
    return cents - 100. * steps;
}
// Maqam presets: the half-flat degrees (relative to the tonic) sit 50 cents below the key.
inline bool maqamTable(const juce::String &name, int tonic, Table &out)
{
    out.fill(0);
    std::vector<int> halfFlat;
    if (name == "rast")
        halfFlat = {4, 11};
    else if (name == "bayati" || name == "saba")
        halfFlat = {2};
    else if (name == "sikah")
        halfFlat = {0, 7};
    else if (name == "jiharkah")
        halfFlat = {11};
    else if (name != "equal")
        return false;
    for (int degree : halfFlat)
        out[size_t((tonic + degree) % 12)] = -50;
    return true;
}
} // namespace vibera::tuning
