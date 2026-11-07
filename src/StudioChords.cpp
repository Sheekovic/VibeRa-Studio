// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "ChordProgression.h"

namespace vibera
{
// Tools > Create a chord progression: key, scale, chord count, octave, chord length and a
// Conventional..Adventurous slider. Generate rolls a new progression; clicking a chord rolls
// only that chord; Accept writes the chords (and an optional bass line) into the selected
// Pattern for the selected Channel as one Undo step.
void Studio::showChordProgression()
{
    struct Panel final : juce::Component
    {
        Studio &s;
        juce::ComboBox key, scale, count, octave, length;
        juce::Slider adventure{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
        juce::ToggleButton bass{"Bass line"}, replace{"Replace the Channel's notes"};
        juce::TextButton generate{"Generate"}, accept{"Accept"}, cancel{"Cancel"};
        juce::Label adventureLabels, adventureRight, status;
        std::vector<chords::Chord> progression;
        std::uint32_t seed = std::uint32_t(juce::Time::getMillisecondCounter());

        explicit Panel(Studio &studio) : s(studio)
        {
            for (int i = 0; i < 12; ++i)
                key.addItem(chords::noteName(i), i + 1);
            key.setSelectedId(1);
            scale.addItemList(chords::scaleNames(), 1);
            scale.setSelectedId(1);
            for (int i = 2; i <= 8; ++i)
                count.addItem(juce::String(i) + " chords", i);
            count.setSelectedId(4);
            for (int i = 2; i <= 6; ++i)
                octave.addItem("Octave " + juce::String(i), i);
            octave.setSelectedId(4);
            length.addItemList({"1 beat", "2 beats", "1 bar", "2 bars"}, 1);
            length.setSelectedId(3);
            adventure.setRange(0, 1);
            adventure.setValue(.3);
            adventureLabels.setText("Conventional", juce::dontSendNotification);
            adventureLabels.setColour(juce::Label::textColourId, muted);
            adventureRight.setText("Adventurous", juce::dontSendNotification);
            adventureRight.setJustificationType(juce::Justification::centredRight);
            adventureRight.setColour(juce::Label::textColourId, muted);
            bass.setToggleState(true, juce::dontSendNotification);
            replace.setToggleState(true, juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, muted);
            for (auto *c : std::initializer_list<juce::Component *>{&key, &scale, &count, &octave, &length, &adventure,
                                                                    &adventureLabels, &adventureRight, &bass, &replace, &generate,
                                                                    &accept, &cancel, &status})
                addAndMakeVisible(c);
            for (auto *box : {&key, &scale, &count, &octave})
                box->onChange = [this] { roll(); };
            length.onChange = [this] { repaint(); };
            adventure.onDragEnd = [this] { roll(); };
            generate.onClick = [this]
            {
                seed = seed * 1664525u + 1013904223u;
                roll();
            };
            accept.onClick = [this] { write(); };
            cancel.onClick = [this]
            {
                if (auto *window = findParentComponentOfClass<juce::DocumentWindow>())
                    window->setVisible(false);
            };
            roll();
        }
        chords::Settings settings() const
        {
            chords::Settings st;
            st.key = key.getSelectedId() - 1;
            st.scale = scale.getSelectedId() - 1;
            st.count = count.getSelectedId();
            st.octave = octave.getSelectedId();
            st.adventure = adventure.getValue();
            st.seed = seed;
            return st;
        }
        double chordBeats() const
        {
            const double bar = s.controller.project.numerator * 4. / s.controller.project.denominator;
            switch (length.getSelectedId())
            {
            case 1: return 1;
            case 2: return 2;
            case 4: return bar * 2;
            default: return bar;
            }
        }
        void roll()
        {
            chords::Generator generator(settings());
            progression = generator.generate();
            repaint();
        }
        juce::Rectangle<int> cardArea() const { return {12, 128, getWidth() - 24, 96}; }
        juce::Rectangle<int> card(int i) const
        {
            const auto area = cardArea();
            const int w = area.getWidth() / std::max(1, int(progression.size()));
            return {area.getX() + i * w, area.getY(), w, area.getHeight()};
        }
        void mouseUp(const juce::MouseEvent &e) override
        {
            for (int i = 0; i < int(progression.size()); ++i)
                if (card(i).contains(e.getPosition()))
                {
                    seed = seed * 22695477u + 1u;
                    chords::Generator generator(settings());
                    progression[size_t(i)] = generator.reroll(progression, size_t(i));
                    generator.voice(progression);
                    repaint();
                    return;
                }
        }
        void write()
        {
            const auto *channel = s.controller.project.channel(s.selectedChannel);
            if (!channel || channel->audioClip)
            {
                status.setText("Select an instrument or sampler Channel first", juce::dontSendNotification);
                return;
            }
            CommandArgs a;
            a.pattern = s.selectedPattern;
            a.channel = channel->id;
            a.enabled = !replace.getToggleState(); // notes.import blends when enabled.
            const double beats = chordBeats();
            for (size_t i = 0; i < progression.size(); ++i)
            {
                const double beat = double(i) * beats;
                for (int pitch : progression[i].pitches)
                    a.recordedNotes.push_back({0, channel->id, beat, beats, pitch, .78f, false});
                if (bass.getToggleState())
                {
                    int root = 12 * octave.getSelectedId() + progression[i].root; // An octave below the chords.
                    while (root >= progression[i].pitches.front())
                        root -= 12;
                    a.recordedNotes.push_back({0, channel->id, beat, beats, root, .85f, false});
                }
            }
            const auto revision = s.controller.revision;
            s.command("notes.import", a);
            if (s.controller.revision == revision)
            {
                status.setText(s.status, juce::dontSendNotification);
                return;
            }
            s.status = "Chord progression written to " + channel->name + " (Undo removes it)";
            s.refresh();
            if (auto *window = findParentComponentOfClass<juce::DocumentWindow>())
                window->setVisible(false);
        }
        void paint(juce::Graphics &g) override
        {
            g.fillAll(bg);
            label(g, "Key / scale", {12, 8, 120, 18}, muted, 11);
            label(g, "Chords / octave / length", {270, 8, 220, 18}, muted, 11);
            const auto area = cardArea();
            g.setColour(panel.darker(.2f));
            g.fillRoundedRectangle(area.toFloat(), 6.f);
            for (int i = 0; i < int(progression.size()); ++i)
            {
                const auto c = card(i).reduced(4).toFloat();
                g.setColour(panel.brighter(.06f));
                g.fillRoundedRectangle(c, 6.f);
                g.setColour(channelColour(progression[size_t(i)].degree).withAlpha(.9f));
                g.fillRoundedRectangle(c.withHeight(3.f), 1.5f);
                // Mini note lines: one per chord tone, height by pitch.
                const auto &p = progression[size_t(i)].pitches;
                for (int pitch : p)
                {
                    const float y = c.getBottom() - 12.f - float(pitch - p.front()) * 2.2f;
                    g.setColour(accent.withAlpha(.6f));
                    g.fillRect(c.getX() + 10.f, y, c.getWidth() - 20.f, 2.f);
                }
                g.setColour(ink);
                g.setFont(juce::Font(vibera::uiFont, 18.f, juce::Font::bold));
                g.drawText(progression[size_t(i)].name, c.withTrimmedBottom(30).toNearestInt(), juce::Justification::centred);
                g.setColour(muted);
                g.setFont(juce::Font(vibera::uiFont, 10.f, juce::Font::plain));
                g.drawText(juce::String(i + 1), c.reduced(6, 4).toNearestInt(), juce::Justification::topLeft);
            }
            label(g, "Click a chord to roll just that chord", {12, area.getBottom() + 2, 300, 18}, muted, 10);
        }
        void resized() override
        {
            key.setBounds(12, 28, 70, 26);
            scale.setBounds(88, 28, 170, 26);
            count.setBounds(270, 28, 100, 26);
            octave.setBounds(376, 28, 100, 26);
            length.setBounds(482, 28, 100, 26);
            adventure.setBounds(12, 68, getWidth() - 140, 24);
            adventureLabels.setBounds(12, 92, 160, 18);
            adventureRight.setBounds(getWidth() - 140 - 160 + 12, 92, 160, 18);
            generate.setBounds(getWidth() - 120, 66, 108, 32);
            bass.setBounds(12, 252, 120, 24);
            replace.setBounds(140, 252, 230, 24);
            status.setBounds(12, getHeight() - 40, getWidth() - 250, 28);
            cancel.setBounds(getWidth() - 228, getHeight() - 42, 100, 32);
            accept.setBounds(getWidth() - 120, getHeight() - 42, 108, 32);
        }
    };
    if (!chordWindow)
    {
        chordWindow = std::make_unique<juce::DocumentWindow>("Create a chord progression", panel,
                                                             juce::DocumentWindow::closeButton, true);
        chordWindow->setUsingNativeTitleBar(false);
        chordWindow->setTitleBarHeight(30);
        chordWindow->setResizable(true, true);
        chordWindow->setContentOwned(new Panel(*this), true);
        chordWindow->centreWithSize(640, 340);
    }
    chordWindow->setVisible(true);
    bringWindowToFront(*chordWindow);
}
} // namespace vibera
