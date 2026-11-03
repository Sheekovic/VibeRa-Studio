// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>

namespace vibera
{
// One layout for painting, pointer hit tests and component acceptance. All
// dimensions are logical pixels; JUCE supplies desktop scale separately.
struct MixerEffectSlots
{
    enum class Action
    {
        none,
        add,
        open,
        bypass,
        wet,
        chainBypass,
        reorder,
        menu
    };
    struct Hit
    {
        Action action = Action::none;
        int slot = -1;
    };
    static constexpr int rowHeight = 58;
    juce::Rectangle<int> pane, rows, add, chainBypass;

    MixerEffectSlots(int width, int height, int stripWidth, int bar)
    {
        if (stripWidth >= width || height < 100)
            return;
        pane = {stripWidth, 30, std::max(0, width - stripWidth - bar), std::max(0, height - 30 - bar)};
        add = {pane.getRight() - 34, 34, 26, 26};
        chainBypass = {pane.getRight() - 64, 34, 26, 26};
        rows = {pane.getX() + 8, 68, std::max(0, pane.getWidth() - 16), std::max(0, pane.getBottom() - 96)};
    }
    int visibleRows() const
    {
        return rows.getHeight() / rowHeight;
    }
    int clampedOffset(int requested, int effects) const
    {
        return juce::jlimit(0, std::max(0, effects + 1 - visibleRows()), requested);
    }
    juce::Rectangle<int> row(int visible) const
    {
        if (visible < 0 || visible >= visibleRows())
            return {};
        return {rows.getX(), rows.getY() + visible * rowHeight, rows.getWidth(), rowHeight - 2};
    }
    juce::Rectangle<int> bypass(int visible) const
    {
        auto r = row(visible);
        return r.isEmpty() ? r : juce::Rectangle<int>{r.getX() + 2, r.getY() + 2, 24, 24};
    }
    juce::Rectangle<int> menu(int visible) const
    {
        auto r = row(visible);
        return r.isEmpty() ? r : juce::Rectangle<int>{r.getRight() - 26, r.getY() + 2, 24, 24};
    }
    juce::Rectangle<int> grip(int visible) const
    {
        auto r = row(visible);
        return r.isEmpty() ? r : juce::Rectangle<int>{r.getRight() - 54, r.getY() + 2, 24, 24};
    }
    juce::Rectangle<int> name(int visible) const
    {
        auto r = row(visible);
        return r.isEmpty()
                   ? r
                   : juce::Rectangle<int>{r.getX() + 30, r.getY(), std::max(0, r.getWidth() - 86), 28};
    }
    juce::Rectangle<int> wet(int visible) const
    {
        auto r = row(visible);
        return r.isEmpty() ? r : juce::Rectangle<int>{r.getX() + 2, r.getY() + 28, 28, 28};
    }
    juce::Rectangle<int> wetLabel(int visible) const
    {
        auto r = row(visible);
        return r.isEmpty()
                   ? r
                   : juce::Rectangle<int>{r.getX() + 36, r.getY() + 30, std::max(0, r.getWidth() - 42), 24};
    }
    Hit hit(juce::Point<int> p, int offset, int effects) const
    {
        if (!pane.contains(p))
            return {};
        if (chainBypass.contains(p))
            return {Action::chainBypass, -1};
        if (add.contains(p))
            return {Action::add, -1};
        offset = clampedOffset(offset, effects);
        for (int visible = 0; visible < visibleRows(); ++visible)
        {
            const int slot = offset + visible;
            if (slot > effects)
                break;
            if (!row(visible).contains(p))
                continue;
            if (slot == effects)
                return {Action::add, slot};
            if (bypass(visible).contains(p))
                return {Action::bypass, slot};
            if (wet(visible).contains(p))
                return {Action::wet, slot};
            if (grip(visible).contains(p))
                return {Action::reorder, slot};
            if (menu(visible).contains(p))
                return {Action::menu, slot};
            if (name(visible).contains(p))
                return {Action::open, slot};
            return {};
        }
        return {};
    }
};
} // namespace vibera
