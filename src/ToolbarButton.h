// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Theme.h"

namespace vibera
{
// Original vector glyphs: labels remain available to accessibility and tooltips.
class ToolbarButton : public juce::TextButton
{
  public:
    enum class Icon
    {
        none,
        play,
        stop,
        record,
        loop,
        group,
        undo,
        redo,
        folder,
        save,
        exportFile,
        settings,
        clone,
        browser,
        rack,
        piano,
        playlist,
        mixer,
        metronome,
        keyboard,
        tools,
        close,
        maximise,
        detach,
        dock,
        draw,
        paint,
        slip,
        slice,
        mute,
        select,
        erase,
        zoom,
        menu,
        velocity,
        spark,
        mic
    };
    using juce::TextButton::TextButton;
    // Icon-only buttons show their name on hover when no explicit tooltip was set.
    juce::String getTooltip() override
    {
        const auto tip = juce::TextButton::getTooltip();
        return tip.isNotEmpty() || icon == Icon::none ? tip : getButtonText();
    }
    void setIcon(Icon next)
    {
        icon = next;
        repaint();
    }
    Icon getIcon() const
    {
        return icon;
    }

    void paintButton(juce::Graphics &g, bool over, bool down) override
    {
        if (icon == Icon::none)
        {
            juce::TextButton::paintButton(g, over, down);
            return;
        }
        getLookAndFeel().drawButtonBackground(g, *this, findColour(buttonColourId), over, down);
        juce::Graphics::ScopedSaveState saved(g);
        const float side = juce::jlimit(12.f, 20.f, float(std::min(getWidth(), getHeight()) - 12));
        g.addTransform(juce::AffineTransform::scale(side / 24.f)
                           .translated((getWidth() - side) * .5f, (getHeight() - side) * .5f));
        g.setColour((icon == Icon::record ? juce::Colour(0xffef6576) : ink)
                        .withMultipliedAlpha(isEnabled() ? 1.f : .35f));
        juce::Path p;
        auto line = [&](float x, float y, float xx, float yy) { g.drawLine(x, y, xx, yy, 1.8f); };
        auto rect = [&](float x, float y, float w, float h)
        { g.drawRoundedRectangle(x, y, w, h, 1.f, 1.8f); };
        switch (icon)
        {
        case Icon::group:
            rect(3, 5, 10, 7);
            rect(11, 12, 10, 7);
            line(9, 10, 15, 14);
            break;
        case Icon::loop:
            p.startNewSubPath(5, 8);
            p.lineTo(18, 8);
            p.quadraticTo(21, 8, 21, 12);
            p.startNewSubPath(19, 16);
            p.lineTo(6, 16);
            p.quadraticTo(3, 16, 3, 12);
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(5, 8, 9, 4);
            line(5, 8, 9, 12);
            line(19, 16, 15, 12);
            line(19, 16, 15, 20);
            break;
        case Icon::velocity:
            for (int i = 0; i < 5; ++i)
            {
                const float height = float(5 + (i * 7) % 14);
                g.fillRoundedRectangle(float(3 + i * 4), 21.f - height, 2.5f, height, .7f);
            }
            break;
        case Icon::play:
            if (getButtonText() == "Pause")
            {
                g.fillRect(5.f, 4.f, 5.f, 16.f);
                g.fillRect(14.f, 4.f, 5.f, 16.f);
            }
            else
            {
                p.addTriangle(6, 3, 21, 12, 6, 21);
                g.fillPath(p);
            }
            break;
        case Icon::stop:
            g.fillRoundedRectangle(4, 4, 16, 16, 2);
            break;
        case Icon::record:
            g.fillEllipse(4, 4, 16, 16);
            break;
        case Icon::undo:
        case Icon::redo:
            if (icon == Icon::redo)
                g.addTransform(juce::AffineTransform(-1, 0, 24, 0, 1, 0));
            p.startNewSubPath(4, 9);
            p.cubicTo(18, 1, 24, 14, 17, 20);
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(4, 9, 4, 3);
            line(4, 9, 10, 9);
            break;
        case Icon::folder:
        case Icon::browser:
            p.startNewSubPath(3, 20);
            p.lineTo(3, 5);
            p.lineTo(10, 5);
            p.lineTo(12, 8);
            p.lineTo(21, 8);
            p.lineTo(21, 20);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(3, 11, 21, 11);
            break;
        case Icon::save:
            rect(3, 3, 18, 18);
            rect(7, 3, 9, 6);
            rect(7, 14, 10, 7);
            break;
        case Icon::exportFile:
            line(12, 3, 12, 16);
            line(7, 11, 12, 16);
            line(12, 16, 17, 11);
            p.startNewSubPath(3, 15);
            p.lineTo(3, 21);
            p.lineTo(21, 21);
            p.lineTo(21, 15);
            g.strokePath(p, juce::PathStrokeType(1.8f));
            break;
        case Icon::settings:
            for (int i = 0; i < 8; ++i)
            {
                float a = i * juce::MathConstants<float>::pi / 4;
                line(12 + 8 * std::cos(a), 12 + 8 * std::sin(a), 12 + 11 * std::cos(a),
                     12 + 11 * std::sin(a));
            }
            g.drawEllipse(5, 5, 14, 14, 2);
            g.drawEllipse(9, 9, 6, 6, 1.8f);
            break;
        case Icon::clone:
            rect(3, 7, 13, 14);
            rect(8, 3, 13, 14);
            break;
        case Icon::rack:
            for (int row = 0; row < 3; ++row)
            {
                line(2, 5 + row * 7, 7, 5 + row * 7);
                for (int col = 0; col < 3; ++col)
                    g.fillRoundedRectangle(float(10 + col * 5), float(3 + row * 7), 3, 4, 1);
            }
            break;
        case Icon::keyboard:
        case Icon::piano:
            rect(2, 4, 20, 16);
            for (int i = 1; i < 5; ++i)
                line(2 + i * 4, 4, 2 + i * 4, 20);
            for (int i : {0, 1, 3})
                g.fillRect(float(4 + i * 4), 4.f, 3.f, 9.f);
            break;
        case Icon::playlist:
            rect(2, 3, 20, 18);
            line(7, 3, 7, 21);
            g.fillRoundedRectangle(9, 6, 10, 3, 1);
            g.fillRoundedRectangle(13, 11, 7, 3, 1);
            g.fillRoundedRectangle(9, 16, 6, 3, 1);
            break;
        case Icon::mixer:
            for (int i = 0; i < 3; ++i)
            {
                float x = 5.f + 7 * i, y = 7.f + 5 * (i % 2);
                line(x, 3, x, 21);
                rect(x - 2, y, 4, 4);
            }
            break;
        case Icon::metronome:
            p.addTriangle(5, 21, 12, 3, 19, 21);
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(12, 15, 20, 5);
            line(5, 21, 19, 21);
            break;
        case Icon::mic:
            // Audio input: capsule, cradle and stand.
            g.drawRoundedRectangle(9.f, 2.5f, 6.f, 11.f, 3.f, 1.8f);
            p.startNewSubPath(5.5f, 10.5f);
            p.quadraticTo(5.5f, 17.f, 12.f, 17.f);
            p.quadraticTo(18.5f, 17.f, 18.5f, 10.5f);
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(12, 17, 12, 21);
            line(8, 21, 16, 21);
            break;
        case Icon::spark:
            // CoProducer: a large four-point star with a small companion star.
            p.startNewSubPath(10, 3);
            p.quadraticTo(11, 10, 17, 11.5f);
            p.quadraticTo(11, 13, 10, 20);
            p.quadraticTo(9, 13, 3, 11.5f);
            p.quadraticTo(9, 10, 10, 3);
            p.closeSubPath();
            g.fillPath(p);
            p.clear();
            p.startNewSubPath(19, 2);
            p.quadraticTo(19.4f, 4.6f, 22, 5);
            p.quadraticTo(19.4f, 5.4f, 19, 8);
            p.quadraticTo(18.6f, 5.4f, 16, 5);
            p.quadraticTo(18.6f, 4.6f, 19, 2);
            p.closeSubPath();
            g.fillPath(p);
            break;
        case Icon::tools:
            p.startNewSubPath(5, 21);
            p.lineTo(13, 13);
            p.cubicTo(20, 16, 24, 9, 20, 4);
            p.lineTo(17, 9);
            p.lineTo(13, 8);
            p.lineTo(14, 3);
            p.cubicTo(7, 2, 6, 8, 9, 11);
            p.lineTo(2, 18);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.8f));
            break;
        case Icon::draw:
            p.startNewSubPath(3, 21);
            p.lineTo(5, 14);
            p.lineTo(17, 2);
            p.lineTo(22, 7);
            p.lineTo(10, 19);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(5, 14, 10, 19);
            break;
        case Icon::paint:
            p.startNewSubPath(11, 13);
            p.lineTo(19, 3);
            p.lineTo(22, 6);
            p.lineTo(14, 16);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.8f));
            p.clear();
            p.startNewSubPath(12, 14);
            p.cubicTo(3, 11, 10, 20, 2, 21);
            p.cubicTo(16, 24, 16, 18, 12, 14);
            g.fillPath(p);
            break;
        case Icon::slip:
            line(2, 12, 22, 12);
            line(2, 12, 7, 7);
            line(2, 12, 7, 17);
            line(22, 12, 17, 7);
            line(22, 12, 17, 17);
            line(10, 3, 10, 7);
            line(14, 17, 14, 21);
            break;
        case Icon::slice:
            g.drawEllipse(2, 15, 6, 6, 1.8f);
            g.drawEllipse(16, 15, 6, 6, 1.8f);
            line(6, 16, 19, 3);
            line(18, 16, 5, 3);
            break;
        case Icon::mute:
            p.startNewSubPath(2, 9);
            p.lineTo(7, 9);
            p.lineTo(12, 4);
            p.lineTo(12, 20);
            p.lineTo(7, 15);
            p.lineTo(2, 15);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(16, 8, 22, 16);
            line(22, 8, 16, 16);
            break;
        case Icon::select:
            for (int i = 3; i < 21; i += 6)
            {
                line(float(i), 3, float(i + 3), 3);
                line(float(i), 21, float(i + 3), 21);
                line(3, float(i), 3, float(i + 3));
                line(21, float(i), 21, float(i + 3));
            }
            break;
        case Icon::erase:
            p.startNewSubPath(3, 15);
            p.lineTo(14, 3);
            p.lineTo(22, 11);
            p.lineTo(12, 21);
            p.lineTo(9, 21);
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.8f));
            line(7, 11, 16, 19);
            line(11, 21, 22, 21);
            break;
        case Icon::zoom:
            g.drawEllipse(3, 3, 13, 13, 1.8f);
            line(14, 14, 22, 22);
            line(6, 9.5f, 13, 9.5f);
            line(9.5f, 6, 9.5f, 13);
            break;
        case Icon::menu:
            for (int i = 0; i < 3; ++i)
                line(4, 6.f + 6 * i, 20, 6.f + 6 * i);
            break;
        case Icon::close:
            line(5, 5, 19, 19);
            line(19, 5, 5, 19);
            break;
        case Icon::maximise:
            rect(3, 3, 18, 18);
            break;
        case Icon::detach:
            rect(3, 9, 12, 12);
            line(11, 13, 21, 3);
            line(14, 3, 21, 3);
            line(21, 3, 21, 10);
            break;
        case Icon::dock:
            rect(3, 3, 18, 18);
            line(3, 8, 21, 8);
            line(12, 10, 12, 18);
            line(9, 15, 12, 18);
            line(12, 18, 15, 15);
            break;
        case Icon::none:
            break;
        }
    }

  private:
    Icon icon = Icon::none;
};
} // namespace vibera
