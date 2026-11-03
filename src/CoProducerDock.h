// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CoProducerChat.h"
#include "CoProducerPanel.h"

namespace vibera
{
// The CoProducer side panel, docked at the right edge of the main window and never
// modal: the workspace stays fully usable while it is open. "Chat" is the AI producer;
// "MIDI ideas" is the local model generator, rebuilt from the current selection
// whenever it is idle so its context never goes stale.
class CoProducerDock final : public juce::Component, private juce::Timer
{
    Studio &studio;
    ToolbarButton chatTab{"Chat"}, midiTab{"MIDI ideas"}, fresh{"New chat"}, settings{"AI settings"},
        close{"Close CoProducer"};
    CoProducerChat chat;
    juce::Viewport midiView;
    CoProducerPanel *midi = nullptr; // Owned by midiView.
    juce::String midiContext;
    int tab = 0;

    juce::String contextKey() const
    {
        juce::String key = studio.controller.projectIdentity;
        for (const auto &channel : studio.controller.project.channels)
            key << "|" << juce::String(channel.id) << ":" << channel.name;
        return key;
    }
    void rebuildMidi()
    {
        midiView.setViewedComponent(midi = new CoProducerPanel(studio), true);
        midiContext = contextKey();
        layoutMidi();
    }
    void layoutMidi()
    {
        if (!midi)
            return;
        const int width = midiView.getWidth() - midiView.getScrollBarThickness();
        midi->setSize(width, std::max(midiView.getHeight(), midi->preferredHeight(width)));
    }
    void timerCallback() override
    {
        // Pick up new or renamed Channels and project changes while nothing is pending.
        if (tab == 1 && midi && midi->idle() && contextKey() != midiContext)
            rebuildMidi();
        fresh.setEnabled(!studio.aiBusy());
    }

  public:
    explicit CoProducerDock(Studio &s) : studio(s), chat(s)
    {
        for (auto *b : {&chatTab, &midiTab})
        {
            b->setClickingTogglesState(false);
            b->setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(.22f));
            addAndMakeVisible(b);
        }
        chatTab.setTooltip("Chat with the AI producer (Settings > AI chooses the provider)");
        midiTab.setTooltip("Generate MIDI parts with the local CoProducer models");
        chatTab.onClick = [this] { setTab(0); };
        midiTab.onClick = [this] { setTab(1); };
        fresh.setTooltip("Start a new conversation");
        fresh.onClick = [this] { chat.newChat(); };
        settings.setIcon(ToolbarButton::Icon::settings);
        settings.setTooltip("AI providers, models and local music engines");
        settings.onClick = [this] { studio.performAction("view.settings"); };
        close.setIcon(ToolbarButton::Icon::close);
        close.setTooltip("Close CoProducer");
        close.onClick = [this] { studio.hideCoProducer(); };
        midiView.setScrollBarsShown(true, false);
        midiView.setScrollBarThickness(8);
        for (auto *c : std::initializer_list<juce::Component *>{&fresh, &settings, &close, &chat, &midiView})
            addAndMakeVisible(c);
        setTab(0);
        startTimerHz(2);
    }
    int currentTab() const
    {
        return tab;
    }
    void setTab(int next)
    {
        tab = juce::jlimit(0, 1, next);
        chatTab.setToggleState(tab == 0, juce::dontSendNotification);
        midiTab.setToggleState(tab == 1, juce::dontSendNotification);
        chat.setVisible(tab == 0);
        fresh.setVisible(tab == 0);
        midiView.setVisible(tab == 1);
        if (tab == 1 && (!midi || midi->idle()))
            rebuildMidi(); // Opening the tab reads the current selection, like the old window did.
        resized();
    }
    CoProducerChat &chatPanel()
    {
        return chat;
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
        g.setColour(grid);
        g.fillRect(0, 0, 1, getHeight());
        g.fillRect(0, 44, getWidth(), 1);
        juce::Path star;
        star.startNewSubPath(22, 11);
        star.quadraticTo(23, 19, 30, 22);
        star.quadraticTo(23, 25, 22, 33);
        star.quadraticTo(21, 25, 14, 22);
        star.quadraticTo(21, 19, 22, 11);
        star.closeSubPath();
        g.setColour(accent);
        g.fillPath(star);
        g.setColour(ink);
        g.setFont(juce::FontOptions(vibera::uiFont, 15.f, juce::Font::bold));
        g.drawText("CoProducer", 36, 0, 110, 44, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto header = getLocalBounds().removeFromTop(44).reduced(8, 8);
        header.removeFromLeft(132);
        close.setBounds(header.removeFromRight(28));
        header.removeFromRight(4);
        settings.setBounds(header.removeFromRight(28));
        header.removeFromRight(6);
        if (fresh.isVisible())
        {
            fresh.setBounds(header.removeFromRight(76));
            header.removeFromRight(8);
        }
        const int tabs = std::min(header.getWidth(), 176);
        auto strip = header.removeFromLeft(tabs);
        chatTab.setBounds(strip.removeFromLeft(tabs * 2 / 5));
        midiTab.setBounds(strip);
        const auto body = getLocalBounds().withTrimmedTop(45).withTrimmedLeft(1);
        chat.setBounds(body);
        midiView.setBounds(body);
        layoutMidi();
    }
};
} // namespace vibera
