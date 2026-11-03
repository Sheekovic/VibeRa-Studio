// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Editors.h"
#include <stdexcept>

namespace vibera::acceptance::mixerReadability
{
// Optional actual component geometry check. Call from the owned app fixture
// with two existing native insert IDs. It does not establish rendered quality,
// native desktop input, display DPI settings, DSP or human readability.
template <class EditorType> inline void geometry(EditorType &editor, int first, int second)
{
    const auto old = editor.getBounds();
    struct Restore
    {
        EditorType &editor;
        juce::Rectangle<int> bounds;
        ~Restore() { editor.setBounds(bounds); }
    } restore{editor, old};
    auto check = [](bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    for (const auto size : {juce::Point<int>{1668, 890}, {2308, 1238}, {760, 210}})
    {
        editor.setSize(size.x, size.y);
        const auto name = editor.mixerNameBounds(first);
        const auto adjacent = editor.mixerNameBounds(second);
        const auto enable = editor.mixerMuteBounds(first);
        const auto pan = editor.mixerPanBounds(first);
        const auto meter = editor.mixerMeterBounds(first);
        const auto fader = editor.mixerFaderBounds(first);
        check(adjacent.getX() - name.getX() >= 92, "Readable strip width retains horizontal navigation");
        check(name.getWidth() >= 83 && enable.getWidth() >= 26 && enable.getHeight() >= 26,
              "Practical name and enable target dimensions");
        check(pan.getWidth() >= 47 && pan.getHeight() >= 26 && !pan.intersects(enable) &&
                  !pan.intersects(name) && !meter.intersects(pan) && !meter.intersects(fader),
              "Shared meter/enable/pan/fader targets are independent");
        check(editor.mixerFaderTravel() <= 280 && fader.getBottom() < size.y - 42,
              "Bounded fader travel reserves the existing routing footer");
        for (float gain : {0.f, .5f, 1.f, 2.f})
            check(std::abs(editor.mixerGainAtY(editor.mixerGainY(gain)) - gain) < 1.e-6,
                  "Painted fader and mouse gain mapping agree including endpoints");
        const auto layout = editor.effectSlotsLayout();
        check(layout.pane.getWidth() <= 240 && layout.wet(0).getWidth() >= 28 &&
                  layout.chainBypass.getWidth() == 26,
              "Existing two-line effect pane and separate header targets are unchanged");
        check(editor.mixerRouteButtonBounds(first).getY() == size.y - 42,
              "Existing routing footer target has not moved");
    }
}
} // namespace vibera::acceptance::mixerReadability
