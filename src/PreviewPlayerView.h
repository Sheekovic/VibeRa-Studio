// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "Theme.h"

namespace vibera
{
class PreviewPlayerView final : public juce::Component, public juce::SettableTooltipClient
{
    Studio &studio;

  public:
    explicit PreviewPlayerView(Studio &owner) : studio(owner)
    {
        setTooltip("Shift-click the waveform to play from that position; Play preview plays to the end");
    }
    void paint(juce::Graphics &g) override
    {
        const auto &info = studio.audio.auditionInfo();
        g.fillAll(bg);
        auto area = getLocalBounds().reduced(3);
        auto caption = area.removeFromTop(14);
        auto time = caption.removeFromRight(45);
        label(g, info.name.isEmpty() ? "Select audio to preview" : info.name, caption, muted, 10);
        if (info.seconds <= 0)
            return;
        label(g, juce::String(info.seconds, 2) + " s", time, muted, 10);
        const float middle = float(area.getCentreY());
        const float height = area.getHeight() * .5f;
        g.setColour(grid);
        g.drawHorizontalLine(int(middle), float(area.getX()), float(area.getRight()));
        g.setColour(accent.withAlpha(.75f));
        for (size_t bin = 0; bin < info.peaks.size(); ++bin)
        {
            const float x = area.getX() + (bin + .5f) * area.getWidth() / info.peaks.size();
            const auto &peak = info.peaks[bin];
            g.drawLine(x, middle - juce::jlimit(-1.f, 1.f, peak.high) * height, x,
                       middle - juce::jlimit(-1.f, 1.f, peak.low) * height);
        }
        if (studio.audio.auditioning())
        {
            const float x = area.getX() + studio.audio.auditionPosition() * area.getWidth();
            g.setColour(orange);
            g.drawVerticalLine(int(x), float(area.getY()), float(area.getBottom()));
        }
    }
    void mouseUp(const juce::MouseEvent &event) override
    {
        if (!event.mods.isLeftButtonDown() || !event.mods.isShiftDown() || event.position.y < 17)
            return;
        const auto result = studio.audio.seekAudition((event.position.x - 3) / std::max(1, getWidth() - 6));
        if (result.failed())
            studio.status = result.getErrorMessage();
        repaint();
    }
};
} // namespace vibera
