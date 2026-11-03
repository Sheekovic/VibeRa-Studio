// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ChannelAppearance.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace vibera::channel_icons
{
inline const auto &ids = channelAppearanceIconIds;

inline bool valid(const juce::String &id)
{
    return validChannelIcon(id);
}

// Original monochrome vectors on a20-unit canvas. The caller owns colour and
// placement; no native plugin artwork or external image resources are used.
inline void draw(juce::Graphics &g, juce::Rectangle<float> bounds, const juce::String &id,
                 juce::Colour colour)
{
    if (id == "none" || !valid(id))
        return;
    const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    if (!(side > 0.f))
        return;
    const auto square = bounds.withSizeKeepingCentre(side, side);
    const juce::Graphics::ScopedSaveState saved(g);
    g.addTransform(juce::AffineTransform::scale(side / 20.f).translated(square.getX(), square.getY()));
    g.setColour(colour);
    const juce::PathStrokeType stroke(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path path;
    if (id == "drum")
    {
        g.drawEllipse(3.f, 3.f, 14.f, 5.f, 1.35f);
        path.startNewSubPath(3.f, 5.5f);
        path.lineTo(3.f, 14.f);
        path.quadraticTo(10.f, 19.f, 17.f, 14.f);
        path.lineTo(17.f, 5.5f);
        g.strokePath(path, stroke);
        g.drawLine(6.f, 8.5f, 6.f, 14.5f, 1.35f);
        g.drawLine(14.f, 8.5f, 14.f, 14.5f, 1.35f);
    }
    else if (id == "hat")
    {
        path.startNewSubPath(7.f, 7.f);
        path.quadraticTo(10.f, 3.f, 13.f, 7.f);
        g.strokePath(path, stroke);
        g.drawEllipse(2.f, 7.f, 16.f, 3.5f, 1.35f);
        g.drawLine(4.f, 12.f, 16.f, 12.f, 1.35f);
        g.drawLine(10.f, 10.5f, 10.f, 17.f, 1.35f);
        g.drawLine(10.f, 16.f, 6.f, 18.f, 1.35f);
        g.drawLine(10.f, 16.f, 14.f, 18.f, 1.35f);
    }
    else if (id == "keys")
    {
        g.drawRoundedRectangle(2.5f, 3.f, 15.f, 14.f, 1.4f, 1.35f);
        g.drawLine(7.5f, 10.f, 7.5f, 17.f, 1.1f);
        g.drawLine(12.5f, 10.f, 12.5f, 17.f, 1.1f);
        g.fillRoundedRectangle(6.f, 3.5f, 3.f, 7.f, .6f);
        g.fillRoundedRectangle(11.f, 3.5f, 3.f, 7.f, .6f);
    }
    else if (id == "bass")
    {
        path.startNewSubPath(9.4f, 10.2f);
        path.cubicTo(7.f, 8.5f, 6.4f, 11.1f, 4.9f, 10.8f);
        path.cubicTo(1.1f, 10.2f, 1.4f, 16.8f, 4.6f, 17.9f);
        path.cubicTo(8.3f, 19.3f, 11.4f, 14.4f, 9.5f, 12.2f);
        path.closeSubPath();
        g.fillPath(path);
        g.drawLine(8.f, 12.5f, 15.5f, 4.3f, 2.f);
        path.clear();
        path.startNewSubPath(14.4f, 5.5f);
        path.lineTo(13.6f, 4.6f);
        path.lineTo(16.8f, 1.5f);
        path.lineTo(18.1f, 2.8f);
        path.closeSubPath();
        g.fillPath(path);
    }
    else if (id == "synth")
    {
        g.drawRoundedRectangle(2.f, 3.f, 16.f, 14.f, 2.f, 1.35f);
        path.startNewSubPath(4.f, 10.f);
        path.lineTo(6.5f, 6.5f);
        path.lineTo(9.f, 12.5f);
        path.lineTo(12.f, 7.f);
        path.lineTo(16.f, 10.f);
        g.strokePath(path, stroke);
        for (const auto x : {6.f, 10.f, 14.f})
            g.fillEllipse(x - .7f, 14.f, 1.4f, 1.4f);
    }
    else if (id == "mic")
    {
        g.fillRoundedRectangle(7.f, 2.f, 6.f, 10.f, 3.f);
        path.startNewSubPath(4.5f, 8.f);
        path.lineTo(4.5f, 10.f);
        path.cubicTo(4.5f, 17.f, 15.5f, 17.f, 15.5f, 10.f);
        path.lineTo(15.5f, 8.f);
        g.strokePath(path, stroke);
        g.drawLine(10.f, 15.2f, 10.f, 18.f, 1.35f);
        g.drawLine(6.5f, 18.f, 13.5f, 18.f, 1.35f);
    }
    else if (id == "sample")
    {
        g.drawLine(2.f, 10.f, 18.f, 10.f, 1.1f);
        constexpr std::array<float, 7> heights{3.f, 7.f, 12.f, 16.f, 9.f, 5.f, 2.f};
        for (size_t i = 0; i < heights.size(); ++i)
        {
            const auto x = 3.4f + float(i) * 2.2f;
            g.drawLine(x, 10.f - heights[i] * .5f, x, 10.f + heights[i] * .5f, 1.35f);
        }
    }
    else if (id == "audio")
    {
        path.startNewSubPath(2.5f, 7.5f);
        path.lineTo(6.f, 7.5f);
        path.lineTo(10.5f, 3.5f);
        path.lineTo(10.5f, 16.5f);
        path.lineTo(6.f, 12.5f);
        path.lineTo(2.5f, 12.5f);
        path.closeSubPath();
        g.fillPath(path);
        path.clear();
        path.startNewSubPath(13.f, 6.5f);
        path.quadraticTo(17.f, 10.f, 13.f, 13.5f);
        path.startNewSubPath(15.f, 3.5f);
        path.quadraticTo(21.f, 10.f, 15.f, 16.5f);
        g.strokePath(path, stroke);
    }
}
} // namespace vibera::channel_icons
