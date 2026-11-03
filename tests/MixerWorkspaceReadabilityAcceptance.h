// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <stdexcept>

namespace vibera::acceptance::mixerWorkspaceReadability
{
// Actual component geometry only. Invoke with the first two display-order IDs
// and last ID of the original18-insert fixture. Rendering, native Windows/DPI,
// scrolling interaction, audio and human readability need separate evidence.
template <class EditorType> inline void geometry(EditorType &editor, int first, int second, int last)
{
    const auto old = editor.getBounds();
    struct Restore
    {
        EditorType &editor;
        juce::Rectangle<int> bounds;
        ~Restore()
        {
            editor.setBounds(bounds);
        }
    } restore{editor, old};
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    // These are logical component extents; raster/Windows scaling is independent.
    for (const auto size :
         {juce::Point<int>{1668, 890}, {2308, 1238}, {960, 320}, {760, 210}, {680, 210}, {760, 180}})
    {
        editor.setSize(size.x, size.y);
        const auto name = editor.mixerNameBounds(first);
        const auto next = editor.mixerNameBounds(second);
        const auto enable = editor.mixerMuteBounds(first);
        const auto pan = editor.mixerPanBounds(first);
        const auto meter = editor.mixerMeterBounds(first);
        const auto fader = editor.mixerFaderBounds(first);
        const auto route = editor.mixerRouteButtonBounds(first);
        const auto send = editor.mixerSendLevelBounds(first);
        const auto readout = editor.mixerGainReadoutBounds(first);
        const auto layout = editor.effectSlotsLayout();
        const int edge = layout.pane.isEmpty() ? editor.getWidth() : layout.pane.getX();
        const int stride = next.getX() - name.getX();
        require(stride >= 108 && name.getWidth() >= 99 && name.getHeight() >= 24,
                "Name and strip preserve readable logical width without shrinking18 inserts");
        require(editor.mixerNameBounds(last).getX() - name.getX() == 17 * stride,
                "All18 strips remain present at exact independent display positions");
        require(enable.getWidth() >= 26 && enable.getHeight() >= 26 && pan.getWidth() >= 63 &&
                    pan.getHeight() >= 26,
                "Enable and pan retain separate practical click targets");
        require(!enable.intersects(pan) && !name.intersects(pan) && !name.intersects(enable) &&
                    !meter.intersects(pan) && !meter.intersects(fader),
                "Name/order, enable, pan, signal-reset and gain targets stay independent");
        require(editor.mixerControlHit(name, name.getCentre().toFloat()) &&
                    editor.mixerControlHit(enable, enable.getCentre().toFloat()) &&
                    editor.mixerControlHit(pan, pan.getCentre().toFloat()),
                "Shared geometry is the actual input geometry, not only paint bounds");
        require(!editor.mixerControlHit(name, {float(edge), float(name.getCentreY())}) &&
                    !editor.mixerControlHit(name, {-1.f, float(name.getCentreY())}),
                "Clipped strip geometry cannot steal effects-pane or outside input");
        require(route.getY() == size.y - 42 && route.getWidth() == 26 && route.getHeight() == 27 &&
                    send.getWidth() == 28 && send.getHeight() == 27 && !route.intersects(send),
                "Routing and send footer targets remain separate and above navigation");
        require(editor.mixerFaderTravel() <= 280 && fader.getBottom() < route.getY() &&
                    editor.mixerGainY(2.f) == 100.f && readout.getBottom() <= route.getY(),
                "Maximized travel cap, footer clearance and existing gain origin remain unchanged");
        juce::Rectangle<int> previousTick;
        const int step = editor.mixerFaderTravel() < 72 ? 60 : editor.mixerFaderTravel() < 180 ? 30 : 12;
        for (int db = 0; db >= -60; db -= step)
        {
            const auto tick = editor.mixerMeterTickLabelBounds(first, db);
            if (tick.isEmpty())
                continue;
            require(!tick.intersects(previousTick) && !tick.intersects(readout) && !tick.intersects(route) &&
                        !tick.intersects(pan),
                    "Actual compact meter label rectangles never overlap each other or controls");
            previousTick = tick;
        }
        for (const float gain : {0.f, .25f, 1.f, 1.75f, 2.f})
        {
            require(std::abs(editor.mixerGainAtY(editor.mixerGainY(gain)) - gain) < 1.e-6,
                    "Paint and actual gain mapping preserve independent literal linear factors");
            const auto thumb = editor.mixerFaderThumbBounds(first, gain);
            require(thumb.getWidth() >= 32 && thumb.getHeight() >= 16 && !thumb.intersects(pan.toFloat()) &&
                        !thumb.intersects(readout.toFloat()) && !thumb.intersects(meter.toFloat()),
                    "Wider actual painted thumb clears pan, signal reset and gain text at endpoints");
        }
        require((size.x >= 700) != layout.pane.isEmpty(),
                "Existing compact-effects-pane breakpoint is retained");
        if (!layout.pane.isEmpty())
            require(layout.pane.getWidth() <= 240 && layout.wet(0).getWidth() >= 28 &&
                        layout.bypass(0).getWidth() == 24 && layout.chainBypass.getWidth() == 26,
                    "Native editor, wet, power and chain targets retain original effects geometry");
    }
}
} // namespace vibera::acceptance::mixerWorkspaceReadability
