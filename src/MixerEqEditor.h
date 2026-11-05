// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Theme.h"
#include "MixerEq.h"
#include <functional>

// The Mixer insert EQ editor: a live response curve over a log-frequency grid with three band
// handles (drag: frequency left/right, gain up/down; double-click: flat band). Every edit goes
// through `apply(band, gainDb, hz)` (the canonical mixer.eq command) inside one gesture.
namespace vibera
{
class MixerEqEditor final : public juce::Component, private juce::Timer
{
  public:
    std::function<MixerEqSettings()> read;                      // Current insert settings.
    std::function<void(int band, float gainDb, float hz)> apply; // One canonical edit.
    std::function<void()> beginGesture, endGesture;               // One Undo step per drag.
    std::function<void()> reset;                                  // Flat EQ.
    juce::String title;

    MixerEqEditor()
    {
        setSize(560, 300);
        startTimerHz(15);
    }
    void paint(juce::Graphics &g) override
    {
        const auto settings = read ? read() : MixerEqSettings{};
        g.fillAll(panel);
        auto area = plot();
        g.setColour(bg.brighter(.1f));
        g.fillRoundedRectangle(area, 6);
        // Grid: decades and dB lines.
        g.setFont(juce::Font(uiFont, 10.f, juce::Font::plain));
        for (double hz : {50., 100., 200., 500., 1000., 2000., 5000., 10000.})
        {
            const float x = xOf(hz);
            g.setColour(grid.withAlpha(.6f));
            g.drawVerticalLine(int(x), area.getY(), area.getBottom());
            g.setColour(muted);
            g.drawText(hz >= 1000 ? juce::String(hz / 1000, 0) + "k" : juce::String(int(hz)),
                       juce::Rectangle<float>(x - 16, area.getBottom() + 2, 32, 12), juce::Justification::centred);
        }
        for (int db : {-12, -6, 0, 6, 12})
        {
            const float y = yOf(float(db));
            g.setColour(db == 0 ? grid.brighter(.6f) : grid.withAlpha(.6f));
            g.drawHorizontalLine(int(y), area.getX(), area.getRight());
            g.setColour(muted);
            g.drawText((db > 0 ? "+" : "") + juce::String(db), juce::Rectangle<float>(area.getX() - 30, y - 6, 26, 12),
                       juce::Justification::centredRight);
        }
        // Response curve with a soft fill to the 0 dB line.
        juce::Path curve, fill;
        for (int i = 0; i <= 240; ++i)
        {
            const double hz = 20. * std::pow(1000., i / 240.);
            const float x = xOf(hz), y = yOf(MixerEqProcessor::responseDb(settings, hz));
            i == 0 ? curve.startNewSubPath(x, y) : curve.lineTo(x, y);
        }
        fill = curve;
        fill.lineTo(area.getRight(), yOf(0));
        fill.lineTo(area.getX(), yOf(0));
        fill.closeSubPath();
        g.setColour(accent.withAlpha(.16f));
        g.fillPath(fill);
        g.setColour(accent);
        g.strokePath(curve, juce::PathStrokeType(2.f));
        // Band handles and labels.
        const juce::Colour colours[]{juce::Colour(0xffe8a33d), juce::Colour(0xff6fcf7f), juce::Colour(0xff4fb3d9)};
        const char *names[]{"Low", "Mid", "High"};
        for (int band = 0; band < 3; ++band)
        {
            const auto p = handle(settings, band);
            g.setColour(colours[band].withAlpha(band == dragging ? 1.f : .9f));
            g.fillEllipse(p.x - 7, p.y - 7, 14, 14);
            g.setColour(juce::Colours::black.withAlpha(.5f));
            g.drawEllipse(p.x - 7, p.y - 7, 14, 14, 1.2f);
            const auto row = juce::Rectangle<float>(12 + band * 180.f, float(getHeight()) - 30, 170, 22);
            g.setColour(colours[band]);
            g.fillEllipse(row.getX(), row.getCentreY() - 4, 8, 8);
            g.setColour(ink);
            g.setFont(juce::Font(uiFont, 12.f, juce::Font::plain));
            const auto hz = settings.freq[size_t(band)];
            g.drawText(juce::String(names[band]) + "  " +
                           (hz >= 1000 ? juce::String(hz / 1000, 2) + " kHz" : juce::String(int(hz)) + " Hz") + "  " +
                           (settings.gain[size_t(band)] > 0 ? "+" : "") + juce::String(settings.gain[size_t(band)], 1) + " dB",
                       row.withTrimmedLeft(14), juce::Justification::centredLeft);
        }
        g.setColour(muted);
        g.setFont(juce::Font(uiFont, 11.f, juce::Font::plain));
        g.drawText("Drag a band: left/right frequency, up/down gain. Double-click: flat. Right-click: reset all.",
                   juce::Rectangle<float>(12, 6, float(getWidth()) - 24, 16), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        const auto settings = read ? read() : MixerEqSettings{};
        if (e.mods.isPopupMenu())
        {
            if (reset)
                reset();
            return;
        }
        dragging = -1;
        float best = 18;
        for (int band = 0; band < 3; ++band)
            if (const auto d = handle(settings, band).getDistanceFrom(e.position); d < best)
                best = d, dragging = band;
        if (dragging >= 0 && beginGesture)
            beginGesture();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragging < 0 || !apply)
            return;
        const auto area = plot();
        const float x = juce::jlimit(area.getX(), area.getRight(), e.position.x);
        const float y = juce::jlimit(area.getY(), area.getBottom(), e.position.y);
        const double hz = 20. * std::pow(1000., (x - area.getX()) / area.getWidth());
        const float gainDb = std::round(float(18. - 36. * (y - area.getY()) / area.getHeight()) * 10.f) / 10.f;
        apply(dragging, juce::jlimit(-18.f, 18.f, gainDb), float(juce::jlimit(20., 20000., std::round(hz))));
        repaint();
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        if (dragging >= 0 && endGesture)
            endGesture();
        dragging = -1;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        const auto settings = read ? read() : MixerEqSettings{};
        for (int band = 0; band < 3; ++band)
            if (handle(settings, band).getDistanceFrom(e.position) < 18 && apply)
                apply(band, 0, settings.freq[size_t(band)]);
        repaint();
    }

  private:
    int dragging = -1;
    void timerCallback() override
    {
        repaint();
    }
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft(40).withTrimmedRight(14).withTrimmedTop(28).withTrimmedBottom(52);
    }
    float xOf(double hz) const
    {
        const auto area = plot();
        return area.getX() + float(std::log10(hz / 20.) / 3.) * area.getWidth();
    }
    float yOf(float db) const
    {
        const auto area = plot();
        return area.getY() + (18.f - juce::jlimit(-18.f, 18.f, db)) / 36.f * area.getHeight();
    }
    juce::Point<float> handle(const MixerEqSettings &settings, int band) const
    {
        return {xOf(settings.freq[size_t(band)]), yOf(settings.gain[size_t(band)])};
    }
};
} // namespace vibera
