// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "NativeWindowStyle.h"
#include "Theme.h"

namespace vibera
{
class WindowChrome final : public juce::LookAndFeel_V4
{
    int additionalTitleSpace = 0;
    std::unique_ptr<juce::Drawable> symbol =
        juce::Drawable::createFromImageData(BinaryData::viberasymbol_svg, BinaryData::viberasymbol_svgSize);
    class CaptionButton final : public juce::Button
    {
        int kind;
        void paintButton(juce::Graphics &g, bool hover, bool down) override
        {
            if (hover || down)
                g.fillAll(juce::Colour(kind == juce::DocumentWindow::closeButton ? 0xffc54550 : 0xff303c48));
            g.setColour(themeColour(0xffdae5ee));
            auto r = getLocalBounds().toFloat().withSizeKeepingCentre(10, 10);
            if (kind == juce::DocumentWindow::closeButton)
            {
                g.drawLine(r.getX(), r.getY(), r.getRight(), r.getBottom(), 1.2f);
                g.drawLine(r.getRight(), r.getY(), r.getX(), r.getBottom(), 1.2f);
            }
            else if (kind == juce::DocumentWindow::minimiseButton)
                g.drawLine(r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.2f);
            else
            {
                auto *window = findParentComponentOfClass<juce::DocumentWindow>();
                if (window && window->isFullScreen())
                {
                    g.drawRect(r.translated(2, -2).withSize(8, 8), 1.f);
                    g.setColour(themeColour(0xff18222d));
                    g.fillRect(r.translated(0, 1).withSize(8, 8));
                    g.setColour(themeColour(0xffdae5ee));
                    g.drawRect(r.translated(0, 1).withSize(8, 8), 1.f);
                }
                else
                    g.drawRect(r, 1.f);
            }
        }

      public:
        explicit CaptionButton(int type)
            : Button(type == juce::DocumentWindow::closeButton      ? "Close"
                     : type == juce::DocumentWindow::minimiseButton ? "Minimize"
                                                                    : "Maximize / restore"),
              kind(type)
        {
            setTooltip(getName());
            setWantsKeyboardFocus(false);
        }
    };

  public:
    int getDefaultScrollbarWidth() override
    {
        return scrollBarThickness;
    }
    bool areScrollbarButtonsVisible() override
    {
        return false;
    }
    void drawScrollbar(juce::Graphics &g, juce::ScrollBar &bar, int x, int y, int width, int height,
                       bool vertical, int start, int size, bool over, bool down) override
    {
        drawSlimScrollbar(g, bar, x, y, width, height, vertical, start, size, over, down);
    }
    explicit WindowChrome(int extraTitleSpace = 0) : additionalTitleSpace(extraTitleSpace) {}
    juce::Button *createDocumentWindowButton(int type) override
    {
        return new CaptionButton(type);
    }
    void positionDocumentWindowButtons(juce::DocumentWindow &, int x, int y, int width, int height,
                                       juce::Button *minimise, juce::Button *maximise, juce::Button *close,
                                       bool) override
    {
        int right = x + width;
        for (auto *button : {close, maximise, minimise})
            if (button)
            {
                right -= 46;
                button->setBounds(right, y, 46, height);
            }
    }
    void drawDocumentWindowTitleBar(juce::DocumentWindow &window, juce::Graphics &g, int width, int height,
                                    int, int, const juce::Image *, bool active) override
    {
        g.fillAll(themeColour(0xff18222d));
        if (symbol)
            symbol->drawWithin(g, {12.f, 5.f, 26.f, float(height - 10)}, juce::RectanglePlacement::centred,
                               active ? 1.f : .7f);
        g.setColour(active ? themeColour(0xffdae5ee) : muted);
        g.setFont(juce::Font(vibera::uiFont, 14.f, juce::Font::plain));
        g.drawFittedText(window.getName(), 48, 0, juce::jmax(0, width - 198 - additionalTitleSpace), height,
                         juce::Justification::centredLeft, 1);
        g.setColour(themeColour(0xff303e4b));
        g.drawHorizontalLine(height - 1, 0, float(width));
    }
};
} // namespace vibera
