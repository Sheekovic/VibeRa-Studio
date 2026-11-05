// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Theme.h"
namespace vibera
{
// UI-only position thumb. Its edges resize the visible range; no document ownership.
class ViewportBar final : public juce::Component, public juce::SettableTooltipClient
{
    bool vertical;
    int edge = 0;
    double start = 0, span = 0, domain = 0;
    juce::Rectangle<float> thumb() const
    {
        auto v = read();
        double length = vertical ? getHeight() : getWidth();
        double size = juce::jlimit(0., length, std::max(20., length * v[1] / std::max(v[1], v[2])));
        double position = v[2] > v[1] ? (length - size) * v[0] / (v[2] - v[1]) : 0.;
        return vertical ? juce::Rectangle<float>(3, float(position), float(getWidth() - 6), float(size))
                        : juce::Rectangle<float>(float(position), 3, float(size), float(getHeight() - 6));
    }

  public:
    bool allowZoom = true;
    std::function<std::array<double, 3>()> read;
    std::function<void(double, double)> change;
    // Optional overview drawn under the thumb (the Playlist draws its clips: a song overview).
    std::function<void(juce::Graphics &, juce::Rectangle<float>)> background;
    explicit ViewportBar(bool v) : vertical(v)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(bg);
        if (!read)
            return;
        auto r = thumb();
        if (background)
        {
            background(g, getLocalBounds().toFloat());
            // With an overview the thumb is a frame, so the clips under it stay visible.
            g.setColour(ink.withAlpha(isMouseOverOrDragging() ? .16f : .09f));
            g.fillRoundedRectangle(r.expanded(0, 2), 3);
            g.setColour(ink.withAlpha(isMouseOverOrDragging() ? .7f : .45f));
            g.drawRoundedRectangle(r.expanded(0, 2).reduced(.5f), 3, 1.f);
        }
        else
        {
            g.setColour(muted.withAlpha(isMouseOverOrDragging() ? .85f : .6f));
            g.fillRoundedRectangle(r, 3);
        }
        if (!allowZoom)
            return;
        g.setColour(muted);
        if (vertical)
        {
            g.fillRect(r.withHeight(2));
            g.fillRect(r.withTop(r.getBottom() - 2));
        }
        else
        {
            g.fillRect(r.withWidth(2));
            g.fillRect(r.withLeft(r.getRight() - 2));
        }
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (!read || !change)
            return;
        auto v = read();
        start = v[0];
        span = v[1];
        domain = v[2];
        auto r = thumb();
        double p = vertical ? e.y : e.x, first = vertical ? r.getY() : r.getX(),
               last = vertical ? r.getBottom() : r.getRight();
        edge = allowZoom ? (std::abs(p - first) < 7 ? -1 : std::abs(p - last) < 7 ? 1 : 0) : 0;
        if (!r.contains(e.position))
        {
            start = juce::jlimit(0., std::max(0., domain - span),
                                 p / (vertical ? getHeight() : getWidth()) * domain - span * .5);
            change(start, span);
        }
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!change)
            return;
        double delta = (vertical ? e.getDistanceFromDragStartY() : e.getDistanceFromDragStartX()) * domain /
                       std::max(1, vertical ? getHeight() : getWidth());
        double next = start, length = span;
        if (edge < 0)
        {
            next = juce::jlimit(0., start + span - .125, start + delta);
            length = start + span - next;
        }
        else if (edge > 0)
            length = juce::jlimit(.125, std::max(.125, domain - start), span + delta);
        else
            next = juce::jlimit(0., std::max(0., domain - span), start + delta);
        change(next, length);
        repaint();
    }
};
} // namespace vibera
