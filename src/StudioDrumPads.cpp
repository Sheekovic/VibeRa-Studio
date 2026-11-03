// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"

namespace vibera
{
// Drum Pads: a 4x4 pad grid that plays the first 16 sampler Channels of the Rack. A hit
// selects the pad's Channel as the live input target and plays its root key through the
// same path as the computer keyboard, so hits sound during playback and are captured by
// MIDI recording. Velocity follows where the pad is struck (top = loudest). Keys
// 1-4 / Q-R / A-F / Z-V play the pads while the window has focus. "Load kit folder..."
// adds up to 16 samples from a folder as new Channels in one step each.
void Studio::showDrumPads()
{
    struct Pads final : juce::Component, juce::Timer
    {
        Studio &s;
        juce::TextButton loadKit{"Load kit folder..."};
        std::unique_ptr<juce::FileChooser> chooser;
        int down = -1;
        std::array<float, 16> flash{};
        const char *keys = "1234qwerasdfzxcv"; // Keyboard rows, top to bottom.

        explicit Pads(Studio &studio) : s(studio)
        {
            addAndMakeVisible(loadKit);
            loadKit.onClick = [this] { chooseKit(); };
            setWantsKeyboardFocus(true);
            startTimerHz(30);
        }
        std::vector<Id> channels() const
        {
            std::vector<Id> result;
            for (const auto &c : s.controller.project.channels)
                if (!c.audioClip && !c.instrument && result.size() < 16)
                    result.push_back(c.id);
            return result;
        }
        juce::Rectangle<int> padBounds(int index) const
        {
            const auto area = getLocalBounds().reduced(10).withTrimmedBottom(44);
            const int w = area.getWidth() / 4, h = area.getHeight() / 4;
            // Row 0 at the bottom, like hardware pad grids (pad 1 bottom-left).
            const int column = index % 4, row = 3 - index / 4;
            return {area.getX() + column * w, area.getY() + row * h, w, h};
        }
        void hit(int index, float velocity)
        {
            const auto ids = channels();
            if (index < 0 || index >= int(ids.size()))
                return;
            release();
            const auto *channel = s.controller.project.channel(ids[size_t(index)]);
            if (!channel)
                return;
            if (s.selectedChannel != channel->id)
            {
                s.selectedChannel = channel->id;
                s.audio.setMidiTarget(channel->id);
            }
            const int pitch = channel->sampler.root < 0 ? 60 : channel->sampler.root;
            down = pitch;
            s.performanceInput.set(1, pitch, true, juce::jlimit(1, 127, juce::roundToInt(velocity * 127)));
            flash[size_t(index)] = 1;
            repaint();
        }
        void release()
        {
            if (down >= 0)
                s.performanceInput.set(1, down, false, 0);
            down = -1;
        }
        int padAt(juce::Point<int> p) const
        {
            for (int i = 0; i < 16; ++i)
                if (padBounds(i).contains(p))
                    return i;
            return -1;
        }
        void mouseDown(const juce::MouseEvent &e) override
        {
            const int index = padAt(e.getPosition());
            if (index < 0)
                return;
            const auto pad = padBounds(index);
            const float velocity = 1.f - .7f * float(e.y - pad.getY()) / float(std::max(1, pad.getHeight()));
            hit(index, velocity);
        }
        void mouseUp(const juce::MouseEvent &) override { release(); }
        bool keyPressed(const juce::KeyPress &key) override
        {
            const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            for (int i = 0; i < 16; ++i)
                if (c == juce::juce_wchar(keys[i]))
                {
                    // Pads 1-4 are the bottom row: map keyboard rows top-down onto it.
                    const int row = 3 - i / 4, index = row * 4 + i % 4;
                    hit(index, .85f);
                    return true;
                }
            return false;
        }
        bool keyStateChanged(bool isKeyDown) override
        {
            if (!isKeyDown)
                release();
            return false;
        }
        void timerCallback() override
        {
            bool changed = false;
            for (auto &f : flash)
                if (f > 0)
                {
                    f = std::max(0.f, f - .12f);
                    changed = true;
                }
            if (changed)
                repaint();
        }
        void chooseKit()
        {
            chooser = std::make_unique<juce::FileChooser>("Choose a folder of drum samples", juce::File{});
            auto safe = juce::Component::SafePointer<Pads>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [safe](const juce::FileChooser &fc)
                                 {
                                     if (safe && fc.getResult().isDirectory())
                                         safe->loadFolder(fc.getResult());
                                 });
        }
        void loadFolder(const juce::File &folder)
        {
            auto files = folder.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            files.sort();
            int added = 0;
            const int room = std::max(0, Project::maxChannels - int(s.controller.project.channels.size()));
            for (const auto &file : files)
            {
                if (added >= 16 || added >= room)
                    break;
                CommandArgs args;
                args.text = file.getFullPathName();
                if (s.controller.execute("channel.add", args).wasOk())
                    ++added;
            }
            s.refresh();
            s.status = added > 0 ? "Drum kit: added " + juce::String(added) + " sample Channel(s) from " + folder.getFileName()
                                 : "No audio files in " + folder.getFileName();
            repaint();
        }
        void paint(juce::Graphics &g) override
        {
            g.fillAll(bg);
            const auto ids = channels();
            for (int i = 0; i < 16; ++i)
            {
                const auto pad = padBounds(i).reduced(5).toFloat();
                const auto *channel = i < int(ids.size()) ? s.controller.project.channel(ids[size_t(i)]) : nullptr;
                const auto colour = channel ? (channel->colour != 0 ? juce::Colour(channel->colour) : channelColour(i)) : grid;
                g.setColour(panel.brighter(.04f));
                g.fillRoundedRectangle(pad, 8.f);
                g.setColour(colour.withAlpha(channel ? .25f + .6f * flash[size_t(i)] : .08f));
                g.fillRoundedRectangle(pad, 8.f);
                g.setColour(channel ? colour.withAlpha(.8f) : grid);
                g.drawRoundedRectangle(pad.reduced(.5f), 8.f, channel && s.selectedChannel == channel->id ? 2.f : 1.f);
                g.setColour(channel ? ink : muted);
                g.setFont(juce::FontOptions(14.f, juce::Font::bold));
                g.drawFittedText(channel ? channel->name : "Empty", pad.reduced(8).toNearestInt(), juce::Justification::centred, 2);
                g.setColour(muted);
                g.setFont(juce::FontOptions(10.f));
                const int keyIndex = (3 - i / 4) * 4 + i % 4;
                g.drawText(juce::String(i + 1) + "  [" + juce::String::charToString(juce::juce_wchar(keys[keyIndex])).toUpperCase() + "]",
                           pad.reduced(8, 6).toNearestInt(), juce::Justification::bottomLeft);
            }
        }
        void resized() override { loadKit.setBounds(10, getHeight() - 38, 150, 30); }
    };
    if (!drumPadsWindow)
    {
        drumPadsWindow = std::make_unique<juce::DocumentWindow>("Drum Pads", panel, juce::DocumentWindow::closeButton, true);
        drumPadsWindow->setUsingNativeTitleBar(false);
        drumPadsWindow->setTitleBarHeight(30);
        drumPadsWindow->setResizable(true, true);
        drumPadsWindow->setContentOwned(new Pads(*this), true);
        drumPadsWindow->centreWithSize(520, 560);
    }
    drumPadsWindow->setVisible(true);
    drumPadsWindow->toFront(true);
    if (auto *content = drumPadsWindow->getContentComponent())
        content->grabKeyboardFocus();
}
} // namespace vibera
