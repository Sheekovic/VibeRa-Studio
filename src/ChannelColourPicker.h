// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Theme.h"
#include <array>
#include <functional>
#include <optional>

namespace vibera
{
// A blank draft deliberately carries no patch; zero explicitly restores the theme.
inline juce::Result readChannelColourDraft(juce::String text, std::optional<juce::uint32> &output)
{
    text = text.trim();
    if (text.isEmpty())
    {
        output.reset();
        return juce::Result::ok();
    }
    if (text.equalsIgnoreCase("theme"))
    {
        output = 0;
        return juce::Result::ok();
    }
    if (text.startsWithChar('#'))
        text = text.substring(1);
    if (text.length() != 6 || !text.containsOnly("0123456789abcdefABCDEF"))
        return juce::Result::fail("Use Theme or a six-digit colour such as #29D7C5.");
    output = juce::uint32(0xff000000u | text.getHexValue32());
    return juce::Result::ok();
}

inline std::array<juce::Colour, 16> channelAppearancePalette()
{
    std::array<juce::Colour, 16> colours;
    for (size_t i = 0; i < channelColours.size(); ++i)
    {
        colours[i] = channelColours[i].withAlpha(1.f);
        colours[i + 8] = colours[i].interpolatedWith(juce::Colour(0xffe4eef2), .36f).withAlpha(1.f);
    }
    return colours;
}

// Message-thread component. The owner retains the draft and decides when to commit it.
class ChannelColourPicker final : public juce::Component, private juce::ChangeListener
{
    class Swatch final : public juce::TextButton
    {
        void paintButton(juce::Graphics &graphics, bool over, bool down) override
        {
            const auto colour = findColour(juce::TextButton::buttonColourId);
            const auto bounds = getLocalBounds().toFloat().reduced(1.f);
            graphics.setColour(down ? colour.darker(.1f) : over ? colour.brighter(.08f) : colour);
            graphics.fillRoundedRectangle(bounds, 3.f);
            graphics.setColour(getToggleState() ? colour.contrasting(1.f) : grid);
            graphics.drawRoundedRectangle(bounds, 3.f, getToggleState() ? 2.f : 1.f);
            graphics.setColour(colour.contrasting(1.f));
            graphics.setFont(juce::Font(vibera::uiFont, 12.f, juce::Font::plain));
            graphics.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        }
    };

  public:
    ChannelColourPicker(bool bulk, juce::Colour inheritedTheme)
        : bulkMode(bulk), themeColour(inheritedTheme.withAlpha(1.f)), palette(channelAppearancePalette()),
          spectrum(juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders |
                   juce::ColourSelector::showColourspace)
    {
        setComponentID("channelColourPicker");
        setTitle("Channel colour");
        for (size_t i = 0; i < swatches.size(); ++i)
        {
            auto &button = swatches[i];
            button.setButtonText(juce::String(int(i + 1)));
            button.setComponentID("channelColourPreset." + juce::String(int(i)));
            button.setColour(juce::TextButton::buttonColourId, palette[i]);
            button.setColour(juce::TextButton::buttonOnColourId, palette[i]);
            const auto text = palette[i].contrasting(1.f);
            button.setColour(juce::TextButton::textColourOffId, text);
            button.setColour(juce::TextButton::textColourOnId, text);
            button.setTooltip("Palette " + juce::String(int(i + 1)) + " - " + colourText(palette[i]));
            button.onClick = [this, i] { choose(colourText(palette[i])); };
            addAndMakeVisible(button);
        }
        spectrum.setComponentID("channelColourSpectrum");
        spectrum.setColour(juce::ColourSelector::backgroundColourId, panel);
        spectrum.setColour(juce::ColourSelector::labelTextColourId, ink);
        spectrum.addChangeListener(this);
        addAndMakeVisible(spectrum);
        theme.setButtonText("Theme");
        theme.setComponentID("channelColourTheme");
        theme.setTooltip("Restore the inherited channel theme colour");
        theme.onClick = [this] { choose("Theme"); };
        addAndMakeVisible(theme);
        keep.setButtonText("Keep unchanged");
        keep.setComponentID("channelColourKeep");
        keep.onClick = [this] { choose({}); };
        addChildComponent(keep);
        keep.setVisible(bulkMode);
        status.setColour(juce::Label::textColourId, muted);
        status.setFont(juce::Font(12.f));
        status.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(status);
        setDraft(bulkMode ? juce::String{} : juce::String("Theme"));
        setSize(420, 320);
    }

    ~ChannelColourPicker() override
    {
        spectrum.removeChangeListener(this);
    }

    std::function<void(const juce::String &)> onChoose;

    // Synchronising an owner's text field never emits another choice notification.
    void setDraft(const juce::String &value)
    {
        const juce::ScopedValueSetter<bool> guard(synchronizing, true);
        // Discard an older asynchronous spectrum notification before accepting an
        // owner's newer text draft. It must not turn synchronization into a choice.
        spectrum.dispatchPendingMessages();
        draft = value;
        std::optional<juce::uint32> parsed;
        const auto result = readChannelColourDraft(draft, parsed);
        if (result.failed())
        {
            status.setText("Invalid colour - enter Theme or #RRGGBB", juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, orange);
        }
        else
        {
            const auto colour = parsed && *parsed != 0 ? juce::Colour(*parsed) : themeColour;
            spectrum.setCurrentColour(colour, juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, muted);
            status.setText(!parsed ? (bulkMode ? "Keep each channel's colour unchanged" : "No colour change")
                           : *parsed == 0 ? "Theme - inherited channel colour"
                                          : "Custom - " + colourText(colour),
                           juce::dontSendNotification);
        }
        status.setTooltip(status.getText());
        for (size_t i = 0; i < swatches.size(); ++i)
            swatches[i].setToggleState(parsed && *parsed == palette[i].getARGB(), juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        g.setColour(panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 7.f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        status.setBounds(area.removeFromBottom(20));
        auto actions = area.removeFromBottom(28);
        theme.setBounds(
            actions.removeFromLeft(bulkMode ? actions.getWidth() / 2 : actions.getWidth()).reduced(2, 0));
        if (bulkMode)
            keep.setBounds(actions.reduced(2, 0));
        area.removeFromBottom(4);
        auto presets = area.removeFromTop(60);
        for (int row = 0; row < 2; ++row)
        {
            auto line = presets.removeFromTop(30);
            const int width = line.getWidth();
            for (int column = 0; column < 8; ++column)
                swatches[size_t(row * 8 + column)].setBounds(
                    line.getX() + width * column / 8 + 1, line.getY() + 1,
                    width * (column + 1) / 8 - width * column / 8 - 2, 28);
        }
        area.removeFromTop(4);
        spectrum.setBounds(area);
    }

  private:
    static juce::String colourText(juce::Colour colour)
    {
        return "#" + juce::String::toHexString(int(colour.getARGB() & 0x00ffffffu))
                         .paddedLeft('0', 6)
                         .toUpperCase();
    }

    void choose(juce::String value)
    {
        setDraft(value);
        // Copy before calling out: the owner's callback may destroy this component.
        auto callback = onChoose;
        if (callback)
            callback(value);
    }

    void changeListenerCallback(juce::ChangeBroadcaster *source) override
    {
        if (source == &spectrum && !synchronizing)
            choose(colourText(spectrum.getCurrentColour().withAlpha(1.f)));
    }

    const bool bulkMode;
    const juce::Colour themeColour;
    const std::array<juce::Colour, 16> palette;
    juce::String draft;
    bool synchronizing = false;
    std::array<Swatch, 16> swatches;
    juce::ColourSelector spectrum;
    juce::TextButton theme, keep;
    juce::Label status;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelColourPicker)
};
} // namespace vibera
