// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Theme.h"
#include <array>
#include <cstdint>
#include <functional>
namespace vibera
{
class MidiImportChannels final : public juce::Component
{
    class Tile final : public juce::TextButton
    {
      public:
        std::function<void()> changed;
        void mouseDown(const juce::MouseEvent &event) override
        {
            if (event.mods.isRightButtonDown())
            {
                setToggleState(false, juce::dontSendNotification);
                if (changed)
                    changed();
            }
            else
                juce::TextButton::mouseDown(event);
        }
    };
    std::array<Tile, 16> tiles;
    juce::TextButton all{"All"}, none{"None"};

  public:
    std::function<void()> changed;
    MidiImportChannels()
    {
        setName("MIDI channels (left: include, right: exclude)");
        setSize(280, 82);
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            auto &tile = tiles[i];
            tile.setButtonText(juce::String(int(i) + 1));
            tile.setTooltip("Left-click includes this MIDI channel; right-click excludes it");
            tile.changed = [this]
            {
                if (changed)
                    changed();
            };
            tile.onClick = [this, i]
            {
                tiles[i].setToggleState(true, juce::dontSendNotification);
                if (changed)
                    changed();
            };
            addAndMakeVisible(tile);
        }
        for (auto *button : {&all, &none})
            addAndMakeVisible(button);
        all.onClick = [this] { setMask(65535); };
        none.onClick = [this] { setMask(0); };
        setMask(65535);
    }
    std::uint32_t mask() const
    {
        std::uint32_t value = 0;
        for (size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].getToggleState())
                value |= 1u << i;
        return value;
    }
    void setMask(std::uint32_t value)
    {
        for (size_t i = 0; i < tiles.size(); ++i)
            tiles[i].setToggleState((value & (1u << i)) != 0, juce::dontSendNotification);
        if (changed)
            changed();
    }
    juce::TextButton &channelButton(int index)
    {
        return tiles[size_t(juce::jlimit(0, 15, index))];
    }
    void resized() override
    {
        const int cell = getWidth() / 8;
        for (int i = 0; i < 16; ++i)
            tiles[size_t(i)].setBounds((i % 8) * cell, (i / 8) * 27, cell - 3, 24);
        all.setBounds(0, 57, 52, 24);
        none.setBounds(57, 57, 52, 24);
    }
};
inline bool midiDropShowsOptions(juce::ModifierKeys modifiers)
{
    return modifiers.isAltDown() || !modifiers.isShiftDown();
}
} // namespace vibera
