// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#pragma once
#include "Studio.h"
#include "WindowChrome.h"
#include "PlaylistWaveforms.h"
#include "SamplerGestures.h"
#include "SamplerTempo.h"
#include "SamplerFilterSettings.h"
#include "SamplerModulationPanel.h"
#include "MixerTargetChoices.h"
#include <set>
#include <type_traits>
#include <charconv>
namespace vibera
{
class Studio::InstrumentWindow final : public juce::DocumentWindow
{
    WindowChrome chrome;
    static constexpr int samplerControlsMinimumHeight = 600;
    class Controls final : public juce::Component, private juce::Timer
    {
        juce::Component::SafePointer<Studio> owner;
        Id channel;
        juce::String viewIdentity, gestureIdentity, gestureName;
        std::uint64_t gestureSerial = 0;
        bool dragBlocked = false;
        SamplerKeyboard keyboard;
        SamplerModulationPanel modulationPanel;
        int modulationTarget = 2;
        SamplerRangeView rangeView;
        juce::Label keyboardHint;
        juce::Label heading, gainLabel, panLabel, mixerLabel, info;
        juce::TextEditor source;
        juce::Slider gain, pan;
        juce::ComboBox filterType;
        juce::Label filterHeading, filterTypeLabel, filterHint;
        std::array<juce::Slider, 2> filterSliders;
        std::array<juce::Label, 2> filterLabels;
        std::array<bool, 2> filterDragging{};
        const std::array<juce::String, 2> filterActions{"filterCutoff", "filterResonance"};
        int filterSyncedId = 2;
        std::array<juce::Slider, 13> samplerSliders;
        std::array<juce::Label, 13> samplerLabels;
        std::array<juce::Slider, 3> voiceSliders;
        std::array<juce::Label, 3> voiceLabels;
        const std::array<juce::String, 3> voiceActions{"cut", "cutBy", "maxPoly"};
        juce::TextButton cutItself{"Cut itself"};
        juce::Label voiceHint;
        juce::Label voiceModeHeading, voiceModeHint;
        std::array<juce::ToggleButton, 3> voiceModeToggles;
        const std::array<juce::String, 3> voiceModeFlags{"mono", "portamento", "fullPortamento"};
        std::array<juce::Slider, 2> voiceModeSliders;
        std::array<juce::Label, 2> voiceModeLabels;
        const std::array<juce::String, 2> voiceModeActions{"slideTime", "gateBeats"};
        std::array<bool, 2> voiceModeDragging{}, voiceModeStrokeActive{};
        const std::array<juce::String, 13> samplerNames{
            "Root MIDI (-1 Auto)", "Pitch (cents)",   "Source start (%)",
            "Source end (%)",      "Loop start (%)",  "Loop end (%)",
            "Attack (seconds)",    "Decay (seconds)", "Sustain",
            "Release (seconds)",   "Fade in (seconds)", "Fade out (seconds)",
            "Loop crossfade (s)"};
        const std::array<juce::String, 13> samplerActions{
            "root",  "cents",   "start",   "end",    "loopStart", "loopEnd",      "attack",
            "decay", "sustain", "release", "fadeIn", "fadeOut",   "loopCrossfade"};
        juce::TextButton setTempo{"Set tempo..."}, detectTempo{"Detect tempo"}, fitTempo{"Fit to tempo"},
            fitLength{"Fit length..."};
        juce::Label tempoInfo;
        juce::ToggleButton keepPitch{"Keep pitch"}; // Replaced on screen by the Mode list (kept for its state).
        // Time stretching: PITCH (cents), MUL (length x), TIME (beats, 0 = off) and Mode.
        std::array<juce::Slider, 3> stretchKnobs;
        std::array<juce::Label, 3> stretchLabels;
        const std::array<juce::String, 3> stretchActions{"stretchPitch", "timeMul", "timeBeats"};
        juce::ComboBox stretchMode;
        juce::Label stretchModeLabel;
        bool detecting = false;
        juce::String tempoOperation, tempoError;
        juce::ToggleButton reverse{"Reverse"}, normalize{"Normalize"}, envelope{"Volume envelope"};
        juce::ToggleButton hq{"HQ"};
        juce::ToggleButton loop{"Use loop points"}, pingPong{"Ping pong loop"};
        juce::TextButton fileLoop{"Use file loop"};
        juce::ComboBox mixer;
        MixerTargetChoices mixerTargets;
        int mixerSyncedOrdinal = 0;
        // Rebuild presentation only when the universe changes. Preserve a queued
        // asynchronous choice until its actual onChange handler consumes it.
        void syncMixerChoices(const Channel& current, bool forceSelection = false)
        {
            const int raw = int(mixer.getSelectedIdAsValue().getValue());
            const bool queuedChoice = raw != mixerSyncedOrdinal;
            if (!mixerTargets.matches(owner->controller.project, owner->controller.projectIdentity))
            {
                auto next = mixerTargets;
                const auto result = next.refresh(owner->controller.project, owner->controller.projectIdentity);
                if (result.failed())
                {
                    mixer.setEnabled(false);
                    historyError(result.getErrorMessage());
                    return;
                }
                juce::PopupMenu nextMenu;
                for (const auto& choice : next.items()) nextMenu.addItem(choice.ordinal, choice.name);
                static_assert(std::is_nothrow_move_assignable_v<MixerTargetChoices>);
                // PopupMenu move assignment is noexcept. Do not clear(): it
                // resets JUCE's pending Value and could erase a queued choice.
                *mixer.getRootMenu() = std::move(nextMenu);
                mixerTargets = std::move(next);
            }
            mixer.setEnabled(true);
            if (forceSelection || !queuedChoice)
            {
                mixerSyncedOrdinal = mixerTargets.ordinalForTarget(current.mixer);
                mixer.setSelectedId(mixerSyncedOrdinal, juce::dontSendNotification);
            }
        }
        juce::TextButton previewProcessed{"Preview processed"};
        juce::TextButton load{"Load sample..."}, preview{"Preview source"}, piano{"Piano roll"},
            showKeys{"Keyboard"};
        PlaylistWaveforms waveforms{*this};
        bool audioClip = false;
        std::shared_ptr<const Sample> sample;
        std::unique_ptr<juce::FileChooser> chooser;
        bool updating = false;
        std::uint64_t processedPreviewSerial = 0;
        void syncFilter(const Channel &current, bool forceType = false)
        {
            const bool available = !current.instrument && !current.audioClip;
            filterType.setEnabled(available);
            filterHeading.setEnabled(available);
            filterTypeLabel.setEnabled(available);
            filterHint.setEnabled(available);
            // Preserve an asynchronous ComboBox choice until its handler consumes it.
            const int pendingId = int(filterType.getSelectedIdAsValue().getValue());
            if (forceType || pendingId == filterSyncedId)
            {
                filterSyncedId = current.sampler.filterType + 1;
                filterType.setSelectedId(filterSyncedId, juce::dontSendNotification);
            }
            const std::array<double, 2> values{current.sampler.filterCutoff, current.sampler.filterResonance};
            for (size_t i = 0; i < filterSliders.size(); ++i)
            {
                auto &slider = filterSliders[i];
                if (!filterDragging[i] && !slider.isMouseButtonDown())
                    slider.setValue(values[i] * 100., juce::dontSendNotification);
                slider.setEnabled(available);
                filterLabels[i].setEnabled(available);
            }
            auto *device = owner->audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
            const double rate = device ? device->getCurrentSampleRate() : 48000.;
            filterHint.setText(
                available ? juce::String(samplerFilterTypeName(current.sampler.filterType)) + " | " +
                                juce::String(samplerFilterCutoffHz(values[0], rate), 1) + " Hz" +
                                (device ? " (device rate)" : " (48 kHz reference)") + " | Q " +
                                juce::String(samplerFilterQ(values[1]), 2)
                          : "Sampler filtering is unavailable for an Audio Clip or native instrument.",
                juce::dontSendNotification);
        }
        void syncVoiceModes(const Channel &current)
        {
            const bool available = !current.instrument && !current.audioClip;
            const auto &values = current.sampler;
            const std::array<bool, 3> flags{values.mono, values.portamento, values.fullPortamento};
            for (size_t i = 0; i < voiceModeToggles.size(); ++i)
            {
                voiceModeToggles[i].setEnabled(available);
                voiceModeToggles[i].setToggleState(flags[i], juce::dontSendNotification);
            }
            const std::array<double, 2> displayed{values.slideTime * 1000., values.gateBeats};
            for (size_t i = 0; i < voiceModeSliders.size(); ++i)
            {
                if (!voiceModeDragging[i] && !voiceModeSliders[i].isMouseButtonDown())
                    voiceModeSliders[i].setValue(displayed[i], juce::dontSendNotification);
                voiceModeSliders[i].setEnabled(available);
                voiceModeLabels[i].setEnabled(available);
            }
            voiceModeHeading.setEnabled(available);
            voiceModeHint.setEnabled(available);
            voiceModeHint.setText(available
                                      ? "Mono: overlap only. Porta: last-target glide across gaps.\n"
                                        "Full porta + Porta bypass Gate. Preview source stays dry."
                                      : "Voice modes are unavailable for Audio Clips and native instruments.",
                                  juce::dontSendNotification);
        }
        // Familiar three-page channel settings: Sample, Envelope / instrument, Miscellaneous.
        int page = 0;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> sections;
        juce::Rectangle<int> waveformArea;
        static juce::Rectangle<int> pageTab(int index)
        {
            return {10 + index * 48, 9, 42, 42};
        }
        void setPage(int next)
        {
            next = juce::jlimit(0, 2, next);
            if (next == page)
                return;
            rangeView.finish(true);
            keyboard.release();
            page = next;
            resized();
            timerCallback();
        }
        void knob(juce::Slider &slider, juce::Label &caption, const juce::String &text)
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setPopupDisplayEnabled(true, true, this);
            if (slider.getTooltip().isEmpty())
                slider.setTooltip(caption.getText());
            caption.setText(text, juce::dontSendNotification);
            caption.setFont(juce::Font(10.5f, juce::Font::bold));
            caption.setJustificationType(juce::Justification::centred);
            caption.setColour(juce::Label::textColourId, muted);
            caption.setInterceptsMouseClicks(false, false);
        }
        void restyle()
        {
            const std::array<juce::String, 13> captions{"ROOT",  "PITCH", "SMP START", "SMP END", "LOOP START",
                                                        "LOOP END", "ATT", "DEC", "SUS", "REL", "IN", "OUT",
                                                        "CROSSFADE"};
            for (size_t i = 0; i < samplerSliders.size(); ++i)
                knob(samplerSliders[i], samplerLabels[i], captions[i]);
            samplerSliders[1].setDoubleClickReturnValue(true, 0);
            gain.setTooltip("Channel volume");
            pan.setTooltip("Channel panning");
            knob(gain, gainLabel, "VOL");
            knob(pan, panLabel, "PAN");
            gain.setDoubleClickReturnValue(true, .7);
            pan.setDoubleClickReturnValue(true, 0);
            knob(filterSliders[0], filterLabels[0], "CUTOFF");
            knob(filterSliders[1], filterLabels[1], "RES");
            knob(voiceModeSliders[0], voiceModeLabels[0], "SLIDE");
            knob(voiceModeSliders[1], voiceModeLabels[1], "GATE");
            const std::array<juce::String, 3> stretchCaptions{"PITCH", "MUL", "TIME"};
            for (size_t i = 0; i < stretchKnobs.size(); ++i)
                knob(stretchKnobs[i], stretchLabels[i], stretchCaptions[i]);
            stretchModeLabel.setText("MODE", juce::dontSendNotification);
            stretchModeLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            stretchModeLabel.setJustificationType(juce::Justification::centred);
            stretchModeLabel.setColour(juce::Label::textColourId, muted);
            mixerLabel.setText("TRACK", juce::dontSendNotification);
            mixerLabel.setFont(juce::Font(10.5f, juce::Font::bold));
            mixerLabel.setJustificationType(juce::Justification::centred);
            mixerLabel.setColour(juce::Label::textColourId, muted);
            mixer.setTooltip("Mixer insert this Channel plays through");
            const std::array<juce::String, 3> voiceCaptions{"Cut", "By", "Max"};
            for (size_t i = 0; i < voiceSliders.size(); ++i)
            {
                voiceLabels[i].setText(voiceCaptions[i], juce::dontSendNotification);
                voiceSliders[i].updateText();
            }
            filterType.setTooltip(filterType.getTooltip() + " Play the keyboard to audition Channel processing.");
            heading.setFont(juce::Font(15.f, juce::Font::bold));
            heading.setColour(juce::Label::textColourId, accent);
            info.setFont(juce::Font(12.f));
            info.setColour(juce::Label::textColourId, muted);
            tempoInfo.setFont(juce::Font(12.f));
            filterHint.setFont(juce::Font(11.f));
            keyboardHint.setText("Root note: right-click a key to set it, left-click to audition",
                                 juce::dontSendNotification);
            hq.setButtonText("HQ interpolation");
            keepPitch.setButtonText("Keep pitch (stretch)");
            load.setButtonText("Load...");
            preview.setButtonText("Preview");
            for (auto *hidden : std::initializer_list<juce::Component *>{&showKeys, &filterHeading, &filterTypeLabel,
                                                                         &voiceModeHeading, &voiceModeHint, &voiceHint})
                hidden->setVisible(false);
        }
        void mouseDown(const juce::MouseEvent &e) override
        {
            for (int i = 0; i < 3; ++i)
                if (pageTab(i).contains(e.getPosition()))
                    setPage(i);
        }
        void paintPageTabs(juce::Graphics &g)
        {
            g.setColour(bg.darker(.15f));
            g.fillRect(0, 0, getWidth(), 60);
            for (int i = 0; i < 3; ++i)
            {
                const auto r = pageTab(i).toFloat();
                const bool on = page == i;
                if (on)
                {
                    g.setColour(accent.withAlpha(.14f));
                    g.fillRoundedRectangle(r, 6.f);
                }
                g.setColour(on ? accent : ink.withAlpha(.6f));
                const auto c = r.getCentre();
                juce::Path icon;
                if (i == 0) // Waveform.
                {
                    icon.startNewSubPath(c.x - 15, c.y);
                    const std::array<float, 9> h{3, 9, 14, 7, 12, 5, 9, 4, 2};
                    for (size_t k = 0; k < h.size(); ++k)
                    {
                        const float x = c.x - 12 + float(k) * 3.f;
                        icon.lineTo(x, c.y - h[k]);
                        icon.lineTo(x + 1.5f, c.y + h[k]);
                    }
                    icon.lineTo(c.x + 15, c.y);
                    g.strokePath(icon, juce::PathStrokeType(1.6f));
                }
                else if (i == 1) // Envelope.
                {
                    icon.startNewSubPath(c.x - 14, c.y + 10);
                    icon.lineTo(c.x - 6, c.y - 10);
                    icon.lineTo(c.x + 4, c.y + 2);
                    icon.lineTo(c.x + 14, c.y + 10);
                    g.strokePath(icon, juce::PathStrokeType(1.8f));
                    for (const auto p : {juce::Point<float>(c.x - 14, c.y + 10), juce::Point<float>(c.x - 6, c.y - 10),
                                         juce::Point<float>(c.x + 4, c.y + 2), juce::Point<float>(c.x + 14, c.y + 10)})
                        g.drawEllipse(p.x - 3, p.y - 3, 6, 6, 1.5f);
                }
                else // Wrench.
                {
                    icon.addEllipse(c.x + 2, c.y - 13, 12, 12);
                    g.strokePath(icon, juce::PathStrokeType(2.6f));
                    g.drawLine(c.x + 4, c.y - 3, c.x - 11, c.y + 12, 4.f);
                    g.setColour(bg.darker(.15f));
                    g.fillRect(c.x + 9, c.y - 15, 6.f, 6.f);
                }
            }
        }
        void paintSections(juce::Graphics &g)
        {
            for (const auto &[box, title] : sections)
            {
                g.setColour(bg.withAlpha(.55f));
                g.fillRoundedRectangle(box.toFloat(), 5.f);
                g.setColour(grid);
                g.drawRoundedRectangle(box.toFloat().reduced(.5f), 5.f, 1.f);
                if (title.isNotEmpty())
                {
                    g.setColour(ink);
                    g.setFont(juce::Font(14.f, juce::Font::bold));
                    g.drawText(title, box.getX() + 10, box.getY() + 4, box.getWidth() - 20, 20,
                               juce::Justification::centredLeft);
                }
            }
        }
        void layoutPages()
        {
            const int W = getWidth(), H = getHeight();
            sections.clear();
            waveformArea = {};
            auto knobAt = [](juce::Slider &s, juce::Label &l, int x, int y, int width = 64)
            {
                s.setBounds(x + (width - 42) / 2, y, 42, 42);
                l.setBounds(x - 4, y + 42, width + 8, 14);
            };
            // Header (every page): channel name, VOL / PAN / PITCH knobs and the TRACK insert.
            heading.setBounds(160, 8, std::max(80, W - 160 - 290), 22);
            info.setBounds(160, 32, std::max(80, W - 160 - 290), 20);
            mixer.setBounds(W - 92, 10, 80, 26);
            mixerLabel.setBounds(W - 92, 40, 80, 14);
            knobAt(samplerSliders[1], samplerLabels[1], W - 154, 3, 56);
            knobAt(gain, gainLabel, W - 212, 3, 56);
            knobAt(pan, panLabel, W - 270, 3, 56);
            const int top = 68, left = 10, gap = 8;
            const int half = (W - 2 * left - gap) / 2, right = left + half + gap;
            std::set<juce::Component *> visible{&heading, &info, &mixer, &mixerLabel, &samplerSliders[1],
                                                &samplerLabels[1], &gain, &gainLabel, &pan, &panLabel};
            auto show = [&](std::initializer_list<juce::Component *> items)
            {
                for (auto *c : items)
                    visible.insert(c);
            };
            if (page == 0)
            {
                sections.push_back({{left, top, half, 66}, "File"});
                source.setBounds(left + 10, top + 30, half - 196, 26);
                load.setBounds(left + half - 180, top + 30, 82, 26);
                preview.setBounds(left + half - 92, top + 30, 82, 26);
                sections.push_back({{left, top + 72, half, 66}, "Content"});
                hq.setBounds(left + 10, top + 102, 150, 26);
                previewProcessed.setBounds(left + half - 160, top + 102, 150, 26);
                sections.push_back({{left, top + 144, half, 148}, "Playback"});
                loop.setBounds(left + 10, top + 172, 140, 24);
                pingPong.setBounds(left + 152, top + 172, 130, 24);
                fileLoop.setBounds(left + half - 112, top + 172, 102, 24);
                const int playKnob = (half - 20) / 3;
                for (int k = 0; k < 3; ++k)
                {
                    const size_t i = k == 2 ? 12 : size_t(4 + k);
                    knobAt(samplerSliders[i], samplerLabels[i], left + 10 + k * playKnob, top + 218, playKnob);
                }
                sections.push_back({{right, top, W - right - left, 152}, "Time stretching"});
                const int sw = W - right - left - 20, column = sw / 4;
                for (int k = 0; k < 3; ++k)
                    knobAt(stretchKnobs[size_t(k)], stretchLabels[size_t(k)], right + 10 + k * column, top + 26, column);
                stretchModeLabel.setBounds(right + 10 + 3 * column, top + 30, column, 14);
                stretchMode.setBounds(right + 14 + 3 * column, top + 50, column - 8, 24);
                const int bw = (sw - 18) / 4;
                setTempo.setBounds(right + 10, top + 90, bw, 26);
                detectTempo.setBounds(right + 16 + bw, top + 90, bw, 26);
                fitTempo.setBounds(right + 22 + 2 * bw, top + 90, bw, 26);
                fitLength.setBounds(right + 28 + 3 * bw, top + 90, bw, 26);
                tempoInfo.setBounds(right + 10, top + 122, sw, 22);
                sections.push_back({{right, top + 158, W - right - left, 134}, "Precomputed effects"});
                normalize.setBounds(right + 10, top + 186, 120, 24);
                reverse.setBounds(right + 140, top + 186, 110, 24);
                const int fxKnob = (W - right - left - 20) / 4;
                const std::array<size_t, 4> fx{2, 3, 10, 11};
                for (size_t k = 0; k < fx.size(); ++k)
                    knobAt(samplerSliders[fx[k]], samplerLabels[fx[k]], right + 10 + int(k) * fxKnob, top + 218,
                           fxKnob);
                waveformArea = {left, top + 300, W - 2 * left, std::max(80, H - top - 308)};
                sections.push_back({waveformArea, {}});
                rangeView.setBounds(waveformArea.reduced(4));
                show({&source, &load, &preview, &hq, &previewProcessed, &loop, &pingPong, &fileLoop,
                      &samplerSliders[4], &samplerLabels[4], &samplerSliders[5], &samplerLabels[5],
                      &samplerSliders[12], &samplerLabels[12], &setTempo, &detectTempo, &fitTempo, &fitLength,
                      &stretchKnobs[0], &stretchLabels[0], &stretchKnobs[1], &stretchLabels[1], &stretchKnobs[2],
                      &stretchLabels[2], &stretchMode, &stretchModeLabel, &tempoInfo, &normalize, &reverse, &samplerSliders[2], &samplerLabels[2],
                      &samplerSliders[3], &samplerLabels[3], &samplerSliders[10], &samplerLabels[10],
                      &samplerSliders[11], &samplerLabels[11]});
                if (!audioClip)
                    show({&rangeView});
            }
            else
            {
                const int keys = std::max(70, std::min(110, H - 470));
                keyboard.setBounds(left, H - keys - 8, W - 2 * left, keys);
                keyboardHint.setBounds(left + 4, H - keys - 32, W - 2 * left - 8, 22);
                sections.push_back({{left, H - keys - 36, W - 2 * left, keys + 32}, {}});
                show({&keyboard, &keyboardHint});
            }
            if (page == 1)
            {
                const int panelWidth = juce::roundToInt((W - 2 * left) * .68f);
                const int panelHeight = std::max(300, H - top - std::max(70, std::min(110, H - 470)) - 50);
                modulationPanel.setBounds(left, top, panelWidth, panelHeight);
                const int x = left + panelWidth + gap, width = W - x - left;
                sections.push_back({{x, top, width, 150}, "Filter"});
                filterType.setBounds(x + 10, top + 28, width - 20, 24);
                knobAt(filterSliders[0], filterLabels[0], x + 10, top + 60, (width - 20) / 2);
                knobAt(filterSliders[1], filterLabels[1], x + 10 + (width - 20) / 2, top + 60, (width - 20) / 2);
                filterHint.setBounds(x + 8, top + 118, width - 16, 30);
                sections.push_back({{x, top + 156, width, panelHeight - 156}, {}});
                envelope.setBounds(x + 8, top + 160, width - 16, 24);
                const int adsr = (width - 16) / 4;
                for (int k = 0; k < 4; ++k)
                    knobAt(samplerSliders[size_t(6 + k)], samplerLabels[size_t(6 + k)], x + 8 + k * adsr, top + 194,
                           adsr);
                show({&modulationPanel, &filterType, &filterSliders[0], &filterLabels[0], &filterSliders[1],
                      &filterLabels[1], &filterHint, &envelope, &samplerSliders[6], &samplerLabels[6],
                      &samplerSliders[7], &samplerLabels[7], &samplerSliders[8], &samplerLabels[8],
                      &samplerSliders[9], &samplerLabels[9]});
            }
            if (page == 2)
            {
                const int third = (W - 2 * left - 2 * gap) / 3;
                const int x1 = left, x2 = left + third + gap, x3 = left + 2 * (third + gap);
                sections.push_back({{x1, top, third, 150}, "Polyphony"});
                voiceLabels[2].setBounds(x1 + 10, top + 30, 40, 24);
                voiceSliders[2].setBounds(x1 + 52, top + 30, std::min(110, third - 62), 24);
                voiceModeToggles[0].setBounds(x1 + 10, top + 62, 80, 24);
                voiceModeToggles[1].setBounds(x1 + 92, top + 62, 80, 24);
                knobAt(voiceModeSliders[0], voiceModeLabels[0], x1 + 10, top + 90, 64);
                sections.push_back({{x2, top, third, 150}, "Time"});
                knobAt(voiceModeSliders[1], voiceModeLabels[1], x2 + 10, top + 34, 64);
                voiceModeToggles[2].setBounds(x2 + 10, top + 106, third - 20, 24);
                sections.push_back({{x3, top, third, 150}, "Group"});
                voiceLabels[0].setBounds(x3 + 10, top + 30, 40, 24);
                voiceSliders[0].setBounds(x3 + 52, top + 30, std::min(110, third - 62), 24);
                voiceLabels[1].setBounds(x3 + 10, top + 60, 40, 24);
                voiceSliders[1].setBounds(x3 + 52, top + 60, std::min(110, third - 62), 24);
                cutItself.setBounds(x3 + 10, top + 94, std::min(130, third - 20), 26);
                sections.push_back({{x1, top + 158, third, 96}, "Root note"});
                knobAt(samplerSliders[0], samplerLabels[0], x1 + 10, top + 184, 64);
                piano.setBounds(x1 + 90, top + 196, std::min(110, third - 100), 26);
                show({&voiceLabels[2], &voiceSliders[2], &voiceModeToggles[0], &voiceModeToggles[1],
                      &voiceModeSliders[0], &voiceModeLabels[0], &voiceModeSliders[1], &voiceModeLabels[1],
                      &voiceModeToggles[2], &voiceLabels[0], &voiceSliders[0], &voiceLabels[1], &voiceSliders[1],
                      &cutItself, &samplerSliders[0], &samplerLabels[0], &piano});
            }
            for (auto *child : getChildren())
                child->setVisible(visible.count(child) > 0);
        }
        void timerCallback() override
        {
            if (!owner)
                return;
            if (viewIdentity != owner->controller.projectIdentity)
            {
                rangeView.finish(true);
                keyboard.release();
                rangeView.navigation("sampler.view.all");
                viewIdentity = owner->controller.projectIdentity;
                tempoError.clear();
            }
            auto *c = owner->controller.project.channel(channel);
            bool valid = c && !c->instrument;
            setEnabled(valid);
            if (!valid)
            {
                heading.setText("Channel removed or instrument changed", juce::dontSendNotification);
                return;
            }
            updating = true;
            // Audio Clips: detect from the file and fit by warping (pitch kept).
            setTempo.setEnabled(c->audioClip || bool(owner->audio.channelSample(channel)));
            detectTempo.setEnabled(setTempo.isEnabled() || detecting);
            detectTempo.setButtonText(detecting ? "Cancel detect" : "Detect tempo");
            fitTempo.setEnabled(setTempo.isEnabled());
            fitLength.setEnabled(setTempo.isEnabled() && !c->audioClip);
            fitTempo.setButtonText(c->sampler.fitTempo ? "Unfit tempo" : "Fit to tempo");
            keepPitch.setEnabled(setTempo.isEnabled() && !c->audioClip);
            keepPitch.setToggleState(c->sampler.stretch, juce::dontSendNotification);
            {
                const std::array<double, 3> shown{c->sampler.stretchPitch, c->sampler.timeMul, c->sampler.timeBeats};
                for (size_t i = 0; i < stretchKnobs.size(); ++i)
                {
                    if (!stretchKnobs[i].isMouseButtonDown())
                        stretchKnobs[i].setValue(shown[i], juce::dontSendNotification);
                    stretchKnobs[i].setEnabled(!c->audioClip);
                    stretchLabels[i].setEnabled(!c->audioClip);
                }
                stretchMode.setSelectedId(c->sampler.stretch ? 2 : 1, juce::dontSendNotification);
                stretchMode.setEnabled(!c->audioClip);
            }
            if (!detecting)
                tempoInfo.setText(tempoError.isNotEmpty() ? tempoError : c->sampler.sourceTempo > 0
                                      ? "Source " + juce::String(c->sampler.sourceTempo, 2) + " BPM" +
                                            (c->sampler.fitTempo ? " | fitted" : "") +
                                            (c->sampler.stretch ? " | Stretch keeps pitch" : " | Resample changes pitch")
                                      : juce::String("Source tempo unknown") +
                                            (c->sampler.stretch ? " | Stretch keeps pitch" : " | Resample changes pitch"),
                                  juce::dontSendNotification);
            audioClip = c->audioClip;
            load.setEnabled(!audioClip);
            preview.setEnabled(!audioClip);
            previewProcessed.setEnabled(!audioClip);
            piano.setEnabled(!audioClip);
            if (audioClip)
                info.setText(juce::String(c->audioDuration, 2) + " seconds | Streamed Audio Clip",
                             juce::dontSendNotification);
            heading.setText(audioClip                          ? "Audio Clip - " + c->name
                            : c->source.startsWith("builtin:") ? "Original " + c->name + " instrument"
                                                               : "Sampler - " + c->name,
                            juce::dontSendNotification);
            if (source.getText() != c->source)
            {
                keyboard.release();
                source.setText(c->source, false);
            }
            gain.setValue(c->gain, juce::dontSendNotification);
            pan.setValue(c->pan, juce::dontSendNotification);
            try { syncMixerChoices(*c); }
            catch (const std::exception& error)
            {
                mixer.setEnabled(false);
                historyError("routing.menuRefreshFailed: " + juce::String(error.what()));
            }
            const auto &settings = c->sampler;
            syncFilter(*c);
            syncVoiceModes(*c);
            modulationPanel.sync(c->sampler.modulation, modulationTarget, !c->instrument && !c->audioClip);
            const std::array<int, 3> voiceValues{settings.cut, settings.cutBy, settings.maxPoly};
            for (size_t i = 0; i < voiceSliders.size(); ++i)
            {
                if (!voiceSliders[i].isMouseButtonDown())
                    voiceSliders[i].setValue(voiceValues[i], juce::dontSendNotification);
                voiceSliders[i].setEnabled(!audioClip);
                voiceLabels[i].setEnabled(!audioClip);
            }
            cutItself.setEnabled(!audioClip);
            const std::array<double, 13> values{
                double(settings.root),    settings.cents,         settings.start * 100, settings.end * 100,
                settings.loopStart * 100, settings.loopEnd * 100, settings.attack,      settings.decay,
                settings.sustain,         settings.release,       settings.fadeIn,      settings.fadeOut,
                settings.loopCrossfade};
            for (size_t i = 0; i < samplerSliders.size(); ++i)
            {
                auto &slider = samplerSliders[i];
                if (!slider.isMouseButtonDown())
                    slider.setValue(values[i], juce::dontSendNotification);
                slider.setEnabled(!audioClip && (i < 6 || i >= 10 || settings.envelope) && (i != 12 || settings.loop));
                samplerLabels[i].setEnabled(slider.isEnabled());
            }
            normalize.setEnabled(!audioClip);
            normalize.setToggleState(settings.normalize, juce::dontSendNotification);
            hq.setEnabled(!audioClip);
            hq.setToggleState(settings.hqInterpolation, juce::dontSendNotification);
            reverse.setEnabled(!audioClip);
            envelope.setEnabled(!audioClip);
            reverse.setToggleState(settings.reverse, juce::dontSendNotification);
            envelope.setToggleState(settings.envelope, juce::dontSendNotification);
            loop.setEnabled(!audioClip);
            pingPong.setEnabled(!audioClip && settings.loop);
            loop.setToggleState(settings.loop, juce::dontSendNotification);
            pingPong.setToggleState(settings.pingPong, juce::dontSendNotification);
            {
                const auto prepared = owner->audio.channelSample(channel);
                fileLoop.setEnabled(!audioClip && prepared && prepared->fileLoopStart >= 0);
            }
            auto next = owner->audio.channelSample(channel);
            if (next != sample)
            {
                sample = std::move(next);
                rangeView.setSample(sample);
            }
            if (sample && !audioClip)
                info.setText(
                    juce::String(sample->data.getNumSamples() / sample->sampleRate, 2) + " seconds  |  " +
                        juce::String(sample->sampleRate, 0) + " Hz  |  root MIDI " +
                        juce::String(settings.root < 0 ? sample->rootNote : settings.root) +
                        (settings.root < 0 ? " (Auto)" : "") + (settings.normalize ? "  |  normalized" : "") +
                        (settings.envelope ? "  |  volume envelope"
                         : sample->oneShot ? "  |  one-shot"
                                           : "  |  note release"),
                    juce::dontSendNotification);

            rangeView.setVisible(!audioClip && page == 0);
            rangeView.setEnabled(!audioClip && bool(sample));
            keyboard.setEnabled(!audioClip && bool(sample));
            showKeys.setEnabled(keyboard.isEnabled());
            keyboardHint.setEnabled(keyboard.isEnabled());
            keyboard.setRoot(settings.root < 0 && sample ? sample->rootNote : settings.root);
            keyboard.applyTheme();
            rangeView.repaint();
            updating = false;
            pollTempo();
            repaint();
        }
        bool ownsControlGesture() const
        {
            return owner && gestureSerial != 0 && gestureIdentity == owner->controller.projectIdentity &&
                   owner->controller.hasGesture() && owner->controller.gestureOwner().isEmpty() &&
                   owner->controller.gestureSerial() == gestureSerial;
        }
        void historyError(const juce::String &message)
        {
            if (owner)
            {
                owner->status = message;
                owner->repaint();
            }
        }
        bool finishControlGesture(bool cancel)
        {
            if (ownsControlGesture())
            {
                const auto result = cancel ? owner->controller.cancelGestureChecked()
                                           : owner->controller.endGestureChecked(gestureName);
                if (result.failed())
                {
                    historyError(result.getErrorMessage());
                    return false; // The original baseline and serial remain available to Escape.
                }
            }
            gestureIdentity.clear();
            gestureName.clear();
            gestureSerial = 0;
            filterDragging.fill(false);
            voiceModeDragging.fill(false);
            modulationPanel.invalidateStroke();
            return true;
        }
        bool beginControlGesture(const juce::String &name)
        {
            if (!owner || viewIdentity != owner->controller.projectIdentity)
                return false;
            if (!finishControlGesture(false))
                return false;
            if ((owner->controller.hasGesture() && owner->controller.gestureOwner().isEmpty()) ||
                !owner->controller.beginGesture())
            {
                historyError("Finish or cancel the current edit before changing this instrument");
                return false;
            }
            gestureIdentity = viewIdentity;
            gestureSerial = owner->controller.gestureSerial();
            gestureName = name;
            return true;
        }
        bool change(const juce::String &action, double value, const juce::String &target = {})
        {
            if (updating || !owner || viewIdentity != owner->controller.projectIdentity || dragBlocked)
                return false;
            if (owner->controller.hasGesture() && owner->controller.gestureOwner().isEmpty() &&
                !ownsControlGesture())
            {
                historyError("Finish or cancel the current edit before changing this instrument");
                return false;
            }
            CommandArgs a;
            a.channel = channel;
            a.value = value;
            a.enabled = value != 0;
            a.text = target;
            owner->command(action, a);
            return owner->status == "Ready";
        }

        juce::var tempoRequest(const juce::String &action, const juce::String &token = {})
        {
            if (action == "capture" || action == "detect")
                tempoError.clear();
            auto args = juce::var(new juce::DynamicObject);
            args.getDynamicObject()->setProperty("action", action);
            if (token.isEmpty())
                args.getDynamicObject()->setProperty("channel", juce::int64(channel));
            else
                args.getDynamicObject()->setProperty("operation", token);
            try
            {
                if (owner)
                    return owner->samplerTempoOperation(args);
            }
            catch (const std::exception &error)
            {
                tempoError = error.what();
                tempoInfo.setText(tempoError, juce::dontSendNotification);
            }
            return {};
        }
        // First Playlist placement of this Audio Clip Channel (the one Fit to tempo warps).
        Id firstClip() const
        {
            if (owner)
                for (const auto &placement : owner->controller.project.audioPlacements)
                    if (placement.channel == channel || (placement.warpBpm > 0 && placement.warpChannel == channel))
                        return placement.id;
            return 0;
        }
        void askClipTempo()
        {
            const auto *c = owner ? owner->controller.project.channel(channel) : nullptr;
            if (!c)
                return;
            juce::String how;
            const double bpm = owner->detectFileTempo(juce::File(c->source), how);
            auto *box = new juce::AlertWindow("Source tempo", "The tempo this clip was played at. It is fitted to the Song tempo with its pitch kept.",
                                              juce::MessageBoxIconType::NoIcon);
            box->addTextEditor("bpm", bpm > 0 ? juce::String(bpm, 2) : juce::String("120"), "BPM");
            box->addButton("Fit", 1, juce::KeyPress(juce::KeyPress::returnKey));
            box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            auto safe = juce::Component::SafePointer<juce::Component>(this);
            box->enterModalState(true, juce::ModalCallbackFunction::create(
                [this, box, safe](int choice)
                {
                    const double value = box->getTextEditorContents("bpm").getDoubleValue();
                    if (choice != 1 || !safe || !owner)
                        return;
                    const auto clip = firstClip();
                    const auto result = clip ? owner->fitAudioClipToTempo(clip, value)
                                             : juce::Result::fail("Place this clip in the Playlist first");
                    tempoInfo.setText(result.wasOk() ? owner->status : result.getErrorMessage(), juce::dontSendNotification);
                }), true);
        }
        void pollTempo()
        {
            if (!detecting || tempoOperation.isEmpty())
                return;
            auto result = tempoRequest("job", tempoOperation);
            if (result["completionState"].toString() == "pending")
                return;
            const auto token = tempoOperation;
            detecting = false;
            tempoOperation.clear();
            detectTempo.setButtonText("Detect tempo");
            if (result["completionState"].toString() == "completed" && isShowing())
                askTempo(double(result["estimateBpm"]), false, true, token);
            else if (result["completionState"].toString() == "failed")
            {
                tempoError =
                    result["error"].toString() + ": set BPM manually or analyze the current source again.";
                tempoInfo.setText(tempoError, juce::dontSendNotification);
            }
        }
        void askTempo(double proposed, bool fit, bool estimated = false, juce::String token = {},
                      bool musicalLength = false)
        {
            if (!owner || !sample)
                return;
            if (token.isEmpty())
                token = tempoRequest("capture")["operation"].toString();
            if (token.isEmpty())
                return;
            auto *dialog = new juce::AlertWindow(
                estimated       ? "Confirm tempo estimate"
                : musicalLength ? "Fit sample length"
                : fit           ? "Fit to tempo"
                                : "Set tempo",
                estimated ? "Estimate only: confirm by ear. Half/double-tempo ambiguity is possible."
                : musicalLength
                    ? "Musical length of the trimmed source. Beats are quarter notes; bars use the current "
                      "project meter. Resample changes pitch."
                    : "Original sample BPM (20-400). Resample changes duration and pitch together.",
                juce::MessageBoxIconType::NoIcon);
            dialog->addTextEditor("bpm",
                                  juce::String(proposed > 0 ? proposed : owner->controller.project.tempo, 2),
                                  musicalLength ? "Source length" : "Source BPM");
            if (musicalLength)
                dialog->addComboBox("units", {"Quarter-note beats", "Project bars"}, "Units");
            dialog->addButton(fit ? "Fit" : "Set", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            juce::Component::SafePointer<Controls> safe(this);
            dialog->enterModalState(
                true,
                juce::ModalCallbackFunction::create(
                    [safe, dialog, token, fit, musicalLength](int choice)
                    {
                        if (!safe || !safe->owner)
                            return;
                        if (!choice)
                        {
                            safe->tempoRequest("cancel", token);
                            return;
                        }
                        const auto text = dialog->getTextEditorContents("bpm").trim();
                        if (text.isEmpty() || !text.containsOnly("0123456789.") ||
                            text.indexOfChar('.') != text.lastIndexOfChar('.'))
                        {
                            safe->tempoError = "Invalid number. No change applied.";
                            safe->tempoInfo.setText(safe->tempoError, juce::dontSendNotification);
                            safe->tempoRequest("cancel", token);
                            return;
                        }
                        auto args = juce::var(new juce::DynamicObject);
                        args.getDynamicObject()->setProperty("action", "apply");
                        args.getDynamicObject()->setProperty("operation", token);
                        const auto field =
                            !musicalLength                                                       ? "sourceBpm"
                            : dialog->getComboBoxComponent("units")->getSelectedItemIndex() == 1 ? "bars"
                                                                                                 : "beats";
                        args.getDynamicObject()->setProperty(field, text.getDoubleValue());
                        if (fit)
                            args.getDynamicObject()->setProperty("fit", true);
                        try
                        {
                            safe->owner->samplerTempoOperation(args);
                            safe->tempoError.clear();
                        }
                        catch (const std::exception &error)
                        {
                            safe->tempoError = error.what();
                            safe->tempoInfo.setText(safe->tempoError, juce::dontSendNotification);
                        }
                    }),
                true);
        }

      public:
        void showPage(int next) { setPage(next); }
        // Explicit UI projection refresh, also used by deterministic component
        // acceptance. No command, musical operation or provider interaction.
        void refreshFromProject()
        {
            timerCallback();
        }
        Controls(Studio &studio, Id id)
            : owner(&studio), channel(id), viewIdentity(studio.controller.projectIdentity),
              mixerTargets(studio.controller.project, studio.controller.projectIdentity, id)
        {
            setLookAndFeel(&studio.getLookAndFeel());
            addAndMakeVisible(modulationPanel);
            modulationPanel.onTarget = [this](int target)
            {
                if (owner)
                    owner->selectSamplerModulationTarget(channel, target);
            };
            modulationPanel.onBegin = [this]
            {
                dragBlocked = !beginControlGesture("Sampler modulation");
                return !dragBlocked;
            };
            modulationPanel.onEnd = [this]
            {
                if (!dragBlocked)
                    finishControlGesture(false);
                dragBlocked = false;
                timerCallback();
            };
            modulationPanel.onContext = [this](int destination, const SamplerModulationParameter &parameter)
            {
                if (!owner || viewIdentity != owner->controller.projectIdentity || destination < 0 ||
                    destination >= int(samplerModulationTargets.size()))
                    return;
                if (!finishControlGesture(false))
                    return;
                modulationPanel.invalidateStroke();
                const auto code = samplerAutomationParameterCode(
                    samplerModulationTargets[size_t(destination)], parameter.id);
                if (code.has_value())
                    owner->samplerAutomationMenu(channel, *code);
            };
            modulationPanel.onChange =
                [this](int destination, const SamplerModulationParameter &parameter, double value)
            {
                juce::Component::SafePointer<Controls> safe(this);
                change("channel.sampler.modulation." + juce::String(parameter.id.data()), value,
                       juce::String(samplerModulationTargets[size_t(destination)].data()));
                if (safe)
                    safe->timerCallback();
            };
            for (auto *c : std::initializer_list<juce::Component *>{&heading, &source, &gainLabel, &panLabel,
                                                                    &mixerLabel, &info, &gain, &pan, &mixer,
                                                                    &load, &preview, &piano})
                addAndMakeVisible(c);
            for (auto *component : std::initializer_list<juce::Component *>{&filterHeading, &filterTypeLabel,
                                                                            &filterType, &filterHint})
                addAndMakeVisible(component);
            filterHeading.setText("Sampler filter", juce::dontSendNotification);
            filterTypeLabel.setText("Type", juce::dontSendNotification);
            filterHint.setFont(juce::Font(12.f));
            filterHint.setJustificationType(juce::Justification::topLeft);
            filterType.setComponentID("instrument.sampler.filterType");
            filterHint.setComponentID("instrument.sampler.filterHint");
            for (int type = 0; type < samplerFilterTypeCount; ++type)
                filterType.addItem(samplerFilterTypeName(type), type + 1);
            filterType.setSelectedId(filterSyncedId, juce::dontSendNotification);
            filterType.setTooltip("Local sampler filter: Off, LP12, LP24, HP12, BP12 or BS12. "
                                  "Off and neutral LP12/LP24 pass the source unchanged. "
                                  "Preview source bypasses Channel processing.");
            filterType.onChange = [this]
            {
                if (updating || !owner)
                    return;
                const int type = filterType.getSelectedId() - 1;
                if (type < 0 || type >= samplerFilterTypeCount)
                    return;
                if (ownsControlGesture() && !finishControlGesture(false))
                    return;
                juce::Component::SafePointer<Controls> safe(this);
                change("channel.sampler.filterType", type);
                if (safe && safe->owner)
                    if (const auto *current = safe->owner->controller.project.channel(safe->channel))
                        safe->syncFilter(*current, true);
            };
            const std::array<juce::String, 2> filterNames{"Cutoff (%)", "Resonance (%)"};
            for (size_t i = 0; i < filterSliders.size(); ++i)
            {
                auto &slider = filterSliders[i];
                addAndMakeVisible(slider);
                addAndMakeVisible(filterLabels[i]);
                filterLabels[i].setText(filterNames[i], juce::dontSendNotification);
                slider.setComponentID("instrument.sampler." + filterActions[i]);
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 28);
                slider.setRange(0, 100, .01);
                slider.setTextValueSuffix(" %");
                slider.setTooltip(
                    i == 0 ? "Normalized cutoff0..100% maps logarithmically from20 Hz to20 kHz, "
                             "capped at45% of the processing sample rate. Current Hz appears below."
                           : "Normalized resonance0..100% maps to Q0.7071..12. Current Q appears below. "
                             "This is filter resonance, not a volume or envelope control.");
                slider.onDragStart = [this, i]
                {
                    dragBlocked = !beginControlGesture("Sampler filter");
                    filterDragging[i] = !dragBlocked;
                };
                slider.onValueChange = [this, i]
                {
                    juce::Component::SafePointer<Controls> safe(this);
                    change("channel.sampler." + filterActions[i], filterSliders[i].getValue() / 100.);
                    if (safe)
                        safe->timerCallback();
                };
                slider.onDragEnd = [this, i]
                {
                    filterDragging[i] = false;
                    if (!dragBlocked)
                        finishControlGesture(false);
                    dragBlocked = false;
                    timerCallback();
                };
            }
            addAndMakeVisible(voiceModeHeading);
            addAndMakeVisible(voiceModeHint);
            voiceModeHeading.setComponentID("instrument.sampler.voiceModeHeading");
            voiceModeHint.setComponentID("instrument.sampler.voiceModeHint");
            voiceModeHeading.setText("Voice modes", juce::dontSendNotification);
            voiceModeHint.setFont(juce::Font(12.f));
            voiceModeHint.setJustificationType(juce::Justification::topLeft);
            const std::array<juce::String, 3> flagNames{"Mono", "Porta", "Full porta"};
            const std::array<juce::String, 3> flagHints{
                "New notes release held predecessors across score, MIDI, typing and preview. Only "
                "overlapping held notes glide; old notes do not resume. Mono ignores Max poly, with "
                "the128-voice engine ceiling retained.",
                "Glide new notes from the last target, including after a gap. Porta alone does not make "
                "playback monophonic. New notes use fresh source/envelope state.",
                "Stored independently. When Porta is on, Full porta bypasses the automatic Gate for "
                "subsequent notes; it does not turn Porta or Mono on."};
            for (size_t i = 0; i < voiceModeToggles.size(); ++i)
            {
                auto &toggle = voiceModeToggles[i];
                addAndMakeVisible(toggle);
                toggle.setButtonText(flagNames[i]);
                toggle.setComponentID("instrument.sampler." + voiceModeFlags[i]);
                toggle.setTooltip(flagHints[i]);
                toggle.onClick = [this, i]
                {
                    if (updating)
                        return;
                    const bool enabled = voiceModeToggles[i].getToggleState();
                    if (ownsControlGesture() && !finishControlGesture(false))
                        return;
                    juce::Component::SafePointer<Controls> safe(this);
                    change("channel.sampler." + voiceModeFlags[i], enabled ? 1. : 0.);
                    if (safe)
                        safe->timerCallback();
                };
            }
            const std::array<juce::String, 2> modeNames{"Slide (milliseconds)", "Gate (quarter beats)"};
            for (size_t i = 0; i < voiceModeSliders.size(); ++i)
            {
                auto &slider = voiceModeSliders[i];
                addAndMakeVisible(slider);
                addAndMakeVisible(voiceModeLabels[i]);
                voiceModeLabels[i].setText(modeNames[i], juce::dontSendNotification);
                slider.setComponentID("instrument.sampler." + voiceModeActions[i]);
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 112, 28);
                slider.setRange(0, i == 0 ? 10000. : 64., 0);
                slider.setSkewFactorFromMidPoint(i == 0 ? 250. : 1.);
                slider.textFromValueFunction = [i](double value)
                {
                    if (i == 1 && value == 0)
                        return juce::String("Off");
                    return juce::String(value, 3, value > 0 && value < .001) + (i == 0 ? " ms" : " beats");
                };
                slider.valueFromTextFunction = [this, i](const juce::String &input)
                {
                    auto text = input.trim();
                    // Accepting an unchanged rounded value box is a true no-op.
                    if (text == voiceModeSliders[i].getTextFromValue(voiceModeSliders[i].getValue()).trim())
                        return voiceModeSliders[i].getValue();
                    if (i == 1 && text.equalsIgnoreCase("Off"))
                        return 0.;
                    const juce::String suffix = i == 0 ? "ms" : "beats";
                    if (text.endsWithIgnoreCase(suffix))
                        text = text.dropLastCharacters(suffix.length()).trimEnd();
                    const auto *first = text.toRawUTF8();
                    const auto *last = first + text.getNumBytesAsUTF8();
                    double value = 0;
                    const auto parsed = vibera::number_text::fromChars(first, last, value);
                    if (text.isEmpty() || parsed.ec != std::errc{} || parsed.ptr != last ||
                        !std::isfinite(value) || value < 0 || value > (i == 0 ? 10000. : 64.))
                    {
                        historyError("samplerVoice.invalidValue: Enter " +
                                     juce::String(i == 0 ? "0..10000 milliseconds"
                                                         : "Off or0..64 quarter-note beats"));
                        return voiceModeSliders[i].getValue();
                    }
                    return value;
                };
                slider.setTooltip(
                    i == 0 ? "Slide time0..10000 ms (stored as0..10 seconds). Used by Mono overlap or Porta. "
                             "Original linear rate-magnitude glide; not FL STEPS:TICKS or an exact FL "
                             "timing-curve claim."
                           : "Automatic logical note-off after0..64 quarter-note beats;0 is Off. Captured "
                             "for subsequent notes, follows current tempo. Natural one-shots may finish "
                             "their source after Gate; Full porta bypasses Gate only when Porta is on.");
                slider.onDragStart = [this, i]
                {
                    voiceModeStrokeActive[i] = true;
                    dragBlocked = !beginControlGesture("Sampler voice modes");
                    voiceModeDragging[i] = !dragBlocked;
                };
                slider.onValueChange = [this, i]
                {
                    // A cancelled or superseded physical stroke cannot re-enter history.
                    if (voiceModeStrokeActive[i] && !ownsControlGesture())
                        return;
                    juce::Component::SafePointer<Controls> safe(this);
                    change("channel.sampler." + voiceModeActions[i],
                           voiceModeSliders[i].getValue() / (i == 0 ? 1000. : 1.));
                    if (safe)
                        safe->timerCallback();
                };
                slider.onDragEnd = [this, i]
                {
                    voiceModeStrokeActive[i] = false;
                    voiceModeDragging[i] = false;
                    if (ownsControlGesture() && !dragBlocked)
                        finishControlGesture(false);
                    dragBlocked = false;
                    timerCallback();
                };
            }
            for (size_t i = 0; i < samplerSliders.size(); ++i)
            {
                auto &slider = samplerSliders[i];
                addAndMakeVisible(slider);
                addAndMakeVisible(samplerLabels[i]);
                samplerLabels[i].setText(samplerNames[i], juce::dontSendNotification);
                slider.setComponentID("instrument.sampler." + samplerActions[i]);
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 28);
                slider.setRange(i == 0   ? -1
                                : i == 1 ? -4800
                                         : 0,
                                i == 0   ? 127
                                : i == 1 ? 4800
                                : i < 6  ? 100
                                : i == 8 ? 1
                                         : 10,
                                i < 2 ? 1 : .001);
                slider.onValueChange = [this, i]
                {
                    change("channel.sampler." + samplerActions[i],
                           samplerSliders[i].getValue() / (i >= 2 && i < 6 ? 100. : 1.));
                    if (owner)
                        if (auto *c = owner->controller.project.channel(channel))
                        {
                            const auto &s = c->sampler;
                            if (i == 2 || i == 3)
                                samplerSliders[i].setValue((i == 2 ? s.start : s.end) * 100.,
                                                           juce::dontSendNotification);
                            if (i == 4 || i == 5)
                                samplerSliders[i].setValue((i == 4 ? s.loopStart : s.loopEnd) * 100.,
                                                           juce::dontSendNotification);
                        }
                };
                slider.onDragStart = [this]
                {
                    dragBlocked = !beginControlGesture("Sampler controls");
                };
                slider.onDragEnd = [this]
                {
                    if (!dragBlocked)
                        finishControlGesture(false);
                    dragBlocked = false;
                };
            }
            for (auto *button : {&setTempo, &detectTempo, &fitTempo, &fitLength})
                addAndMakeVisible(button);
            const std::array<juce::String, 3> voiceNames{"Cut", "Cut by", "Max poly"};
            for (size_t i = 0; i < voiceSliders.size(); ++i)
            {
                auto &slider = voiceSliders[i];
                addAndMakeVisible(slider);
                addAndMakeVisible(voiceLabels[i]);
                voiceLabels[i].setText(voiceNames[i], juce::dontSendNotification);
                slider.setComponentID("instrument.sampler." + voiceActions[i]);
                slider.setSliderStyle(juce::Slider::IncDecButtons);
                slider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_AutoDirection);
                slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 28);
                slider.setRange(0, 99, 1);
                slider.textFromValueFunction = [](double value)
                { return value == 0 ? juce::String("--") : juce::String(int(value)); };
                slider.valueFromTextFunction = [](const juce::String &text)
                { return text.trim() == "--" ? 0. : text.getDoubleValue(); };
                slider.setTooltip(
                    i == 2 ? "0: Auto (128-voice engine ceiling).1..99: oldest voice stops immediately. "
                             "Mono ignores this setting; the128-voice engine ceiling still applies."
                           : "0: no group.1..99: matching Cut/Cut-by channels release through their captured "
                             "envelopes. Self-cut originates from steps, not Piano notes.");
                slider.onValueChange = [this, i]
                {
                    change("channel.sampler." + voiceActions[i], voiceSliders[i].getValue());
                    timerCallback();
                };
                slider.onDragStart = [this]
                {
                    dragBlocked = !beginControlGesture("Sampler voices");
                };
                slider.onDragEnd = [this]
                {
                    if (!dragBlocked)
                        finishControlGesture(false);
                    dragBlocked = false;
                };
            }
            addAndMakeVisible(cutItself);
            addAndMakeVisible(voiceHint);
            cutItself.setComponentID("instrument.sampler.cutItself");
            cutItself.setTooltip(
                "Assign an unused matching Cut/Cut-by pair. To disable, set either group to0 (--).");
            voiceHint.setText("-- = None / Auto; Mono ignores Max poly", juce::dontSendNotification);
            cutItself.onClick = [this]
            {
                if (!owner || updating || viewIdentity != owner->controller.projectIdentity)
                    return;
                CommandArgs args;
                args.channel = channel;
                args.enabled = true;
                owner->command("channel.sampler.cutItself", args);
                timerCallback();
            };
            addAndMakeVisible(tempoInfo);
            addAndMakeVisible(keepPitch);
            addAndMakeVisible(fileLoop);
            fileLoop.setComponentID("instrument.sampler.fileLoop");
            fileLoop.setTooltip("Turn on looping at the loop points stored in the sample file (WAV smpl chunk).");
            fileLoop.onClick = [this]
            {
                if (!owner)
                    return;
                const auto prepared = owner->audio.channelSample(channel);
                if (!prepared || prepared->fileLoopStart < 0)
                    return;
                CommandArgs args;
                args.channel = channel;
                args.beat = prepared->fileLoopStart;
                args.length = prepared->fileLoopEnd;
                args.enabled = true;
                owner->command("channel.sampler.loopPoints", args);
                timerCallback();
            };
            for (size_t i = 0; i < stretchKnobs.size(); ++i)
            {
                auto &knob = stretchKnobs[i];
                addAndMakeVisible(knob);
                addAndMakeVisible(stretchLabels[i]);
                knob.setComponentID("instrument.sampler." + stretchActions[i]);
                if (i == 0)
                {
                    knob.setRange(-1200, 1200, 1);
                    knob.setDoubleClickReturnValue(true, 0);
                    knob.textFromValueFunction = [](double v)
                    { return (v > 0 ? "+" : "") + juce::String(v / 100., 2) + " st"; };
                    knob.setTooltip("PITCH: in Stretch mode, transpose without changing the length; in Resample "
                                    "mode, detune like playback speed. 100 cents per semitone.");
                }
                else if (i == 1)
                {
                    knob.setRange(.25, 4, .001);
                    knob.setSkewFactorFromMidPoint(1.);
                    knob.setDoubleClickReturnValue(true, 1);
                    knob.textFromValueFunction = [](double v) { return juce::String(v * 100., 0) + "%"; };
                    knob.setTooltip("MUL: multiply the sample length (100% = unchanged). Resample also changes the "
                                    "pitch; Stretch keeps it.");
                }
                else
                {
                    knob.setRange(0, 64, .25);
                    knob.setSkewFactorFromMidPoint(4.);
                    knob.setDoubleClickReturnValue(true, 0);
                    knob.textFromValueFunction = [](double v)
                    { return v <= 0 ? juce::String("Off") : juce::String(v, v < 1 ? 2 : 1) + " beats"; };
                    knob.setTooltip("TIME: set the sample length in beats at the project tempo (Off keeps the "
                                    "length; MUL still applies).");
                }
                knob.onValueChange = [this, i] { change("channel.sampler." + stretchActions[i], stretchKnobs[i].getValue()); };
                knob.onDragStart = [this] { dragBlocked = !beginControlGesture("Sampler time stretching"); };
                knob.onDragEnd = [this]
                {
                    if (!dragBlocked)
                        finishControlGesture(false);
                    dragBlocked = false;
                };
            }
            addAndMakeVisible(stretchMode);
            addAndMakeVisible(stretchModeLabel);
            stretchMode.setComponentID("instrument.sampler.stretchMode");
            stretchMode.addItem("Resample", 1);
            stretchMode.addItem("Stretch (keep pitch)", 2);
            stretchMode.setTooltip("Resample: length and pitch change together. Stretch: the length changes and "
                                   "the pitch stays (PITCH transposes on its own).");
            stretchMode.onChange = [this]
            {
                if (updating || !owner || stretchMode.getSelectedId() == 0)
                    return;
                CommandArgs args;
                args.channel = channel;
                args.enabled = stretchMode.getSelectedId() == 2;
                owner->command("channel.sampler.stretch", args);
                timerCallback();
            };
            keepPitch.setComponentID("instrument.sampler.stretch");
            keepPitch.setTooltip("When fitting tempo, time-stretch the sample instead of resampling, so its "
                                 "pitch stays the same. The stretched copy is prepared once per tempo.");
            keepPitch.onClick = [this]
            {
                if (!owner || updating)
                    return;
                CommandArgs args;
                args.channel = channel;
                args.enabled = keepPitch.getToggleState();
                owner->command("channel.sampler.stretch", args);
                timerCallback();
            };
            setTempo.setComponentID("instrument.sampler.setTempo");
            detectTempo.setComponentID("instrument.sampler.detectTempo");
            fitTempo.setComponentID("instrument.sampler.fitTempo");
            fitLength.setComponentID("instrument.sampler.fitLength");
            fitLength.setTooltip("Fit the trimmed sample by quarter-note beats or document bars. Resample changes pitch.");
            fitLength.onClick = [this] { askTempo(4, true, false, {}, true); };
            setTempo.onClick = [this]
            {
                if (owner)
                    if (auto *c = owner->controller.project.channel(channel))
                    {
                        if (c->audioClip)
                            return askClipTempo();
                        askTempo(c->sampler.sourceTempo, false);
                    }
            };
            fitTempo.onClick = [this]
            {
                if (!owner)
                    return;
                if (auto *c = owner->controller.project.channel(channel))
                {
                    if (c->audioClip)
                    {
                        const auto clip = firstClip();
                        const auto result = clip ? owner->fitAudioClipToTempo(clip) : juce::Result::fail("Place this clip in the Playlist first");
                        tempoInfo.setText(result.wasOk() ? owner->status : result.getErrorMessage(), juce::dontSendNotification);
                        return;
                    }
                    if (c->sampler.sourceTempo <= 0)
                    {
                        askTempo(0, true);
                        return;
                    }
                    CommandArgs args;
                    args.channel = channel;
                    args.enabled = !c->sampler.fitTempo;
                    owner->command("channel.sampler.fitTempo", args);
                    timerCallback();
                }
            };
            detectTempo.onClick = [this]
            {
                if (owner)
                    if (auto *c = owner->controller.project.channel(channel); c && c->audioClip)
                    {
                        juce::String how;
                        const double bpm = owner->detectFileTempo(juce::File(c->source), how);
                        tempoInfo.setText(bpm > 0 ? "Detected " + juce::String(bpm, 1) + " BPM (" + how + "). Fit to tempo uses it."
                                                  : juce::String("Could not detect the tempo; use Set tempo..."),
                                          juce::dontSendNotification);
                        return;
                    }
                if (detecting)
                {
                    tempoRequest("cancel", tempoOperation);
                    pollTempo();
                    return;
                }
                const auto result = tempoRequest("detect");
                tempoOperation = result["operation"].toString();
                detecting = tempoOperation.isNotEmpty();
                if (detecting)
                {
                    detectTempo.setButtonText("Cancel detect");
                    tempoInfo.setText("Analyzing loaded sample rhythm...", juce::dontSendNotification);
                }
            };
            rangeView.tempoAction = [this](int action)
            {
                if (action == 2)
                    setTempo.triggerClick();
                if (action == 3)
                    detectTempo.triggerClick();
                if (action == 4)
                    fitTempo.triggerClick();
            };
            addAndMakeVisible(normalize);
            normalize.setComponentID("instrument.sampler.normalize");
            normalize.setTooltip(
                "Normalize the whole loaded stereo sample peak to0dB before Channel/Mixer gain. "
                "File and trim stay unchanged; held voices adjust smoothly. Silence stays silent.");
            addAndMakeVisible(reverse);
            addAndMakeVisible(envelope);
            addAndMakeVisible(loop);
            addAndMakeVisible(pingPong);
            loop.setComponentID("instrument.sampler.loop");
            pingPong.setComponentID("instrument.sampler.pingPong");
            loop.setTooltip("Repeat between loop points while a note is held. Applies to subsequent notes.");
            pingPong.setTooltip("Bounce between loop points. Applies to subsequent notes.");
            loop.onClick = [this]
            {
                if (!updating && owner)
                {
                    CommandArgs args;
                    args.channel = channel;
                    args.enabled = loop.getToggleState();
                    owner->command("channel.sampler.loop", args);
                    timerCallback();
                }
            };
            pingPong.onClick = [this]
            {
                if (!updating && owner)
                {
                    CommandArgs args;
                    args.channel = channel;
                    args.enabled = pingPong.getToggleState();
                    owner->command("channel.sampler.pingPong", args);
                    timerCallback();
                }
            };
            reverse.setComponentID("instrument.sampler.reverse");
            reverse.setTooltip(
                "Reverse subsequent notes; already sounding voices keep their source range and direction.");
            envelope.setTooltip("Enable volume ADSR and note-release response for subsequent notes.");
            samplerSliders[12].setTooltip("Blend the audio just before the loop start into the loop end for "
                                          "click-free loops. Limited to half the loop and the source before it.");
            samplerSliders[10].setTooltip("Fade in over this many seconds of source from the playback start. "
                                          "Applies to subsequent notes; reverse fades from the region end.");
            samplerSliders[11].setTooltip("Fade out over the last seconds of the trimmed region. Looped audio "
                                          "passing the faded points dips, like a precomputed fade.");
            samplerSliders[0].setTooltip("Original-pitch MIDI key. Auto (-1) uses the loaded sample root. "
                                         "Changing it retunes sounding voices smoothly.");
            samplerSliders[1].setTooltip(
                "Resampling pitch: 100 cents per semitone. Pitch changes playback speed and duration.");
            samplerSliders[2].setTooltip("Non-destructive source start for subsequent notes; already "
                                         "sounding voices keep their range.");
            samplerSliders[3].setTooltip(
                "Non-destructive source end for subsequent notes; already sounding voices keep their range.");
            envelope.setComponentID("instrument.sampler.envelope");
            auto toggle = [this](juce::String action, bool enabled)
            {
                if (!updating && owner)
                {
                    CommandArgs args;
                    args.channel = channel;
                    args.enabled = enabled;
                    owner->command(action, args);
                    timerCallback();
                }
            };
            normalize.onClick = [this, toggle]
            { toggle("channel.sampler.normalize", normalize.getToggleState()); };
            reverse.onClick = [this, toggle] { toggle("channel.sampler.reverse", reverse.getToggleState()); };
            addAndMakeVisible(hq);
            hq.setComponentID("instrument.sampler.hqInterpolation");
            hq.setTooltip("High-quality 4-point Hermite interpolation for pitched playback (slightly more CPU). "
                          "Off uses linear interpolation.");
            hq.onClick = [this, toggle] { toggle("channel.sampler.hqInterpolation", hq.getToggleState()); };
            envelope.onClick = [this, toggle]
            { toggle("channel.sampler.envelope", envelope.getToggleState()); };
            preview.setTooltip(
                "Audition the original source, before Channel pitch, range, filter and envelope "
                "settings. Play notes to hear the instrument settings.");
            addAndMakeVisible(previewProcessed);
            previewProcessed.setComponentID("instrument.sampler.previewProcessed");
            previewProcessed.setTooltip("Play the root note for 1.5 s through this Channel with every current "
                                        "setting (range, pitch, fades, filter, envelope, stretch).");
            previewProcessed.onClick = [this]
            {
                if (!owner || owner->audio.playing())
                    return;
                if (auto *c = owner->controller.project.channel(channel))
                {
                    owner->selectedChannel = channel;
                    owner->refresh();
                    owner->previewNote(-1);
                    owner->previewNote(c->sampler.root < 0 ? 60 : c->sampler.root);
                    const auto serial = ++processedPreviewSerial;
                    juce::Component::SafePointer<Controls> safe(this);
                    juce::Timer::callAfterDelay(1500, [safe, serial]
                    {
                        if (safe && safe->owner && safe->processedPreviewSerial == serial)
                            safe->owner->previewNote(-1);
                    });
                }
            };
            rangeView.resolveKey = [this](const juce::KeyPress &key)
            { return owner ? owner->customization.resolve(key, Scope::sampler) : juce::String{}; };
            addAndMakeVisible(rangeView);
            addAndMakeVisible(keyboard);
            addAndMakeVisible(keyboardHint);
            addAndMakeVisible(showKeys);
            showKeys.setComponentID("instrument.sampler.showKeyboard");
            showKeys.onClick = [this]
            {
                if (auto *viewport = findParentComponentOfClass<juce::Viewport>())
                    viewport->setViewPosition(0, keyboard.getBottom() - viewport->getViewHeight() + 6);
            };
            keyboardHint.setText("Keyboard: left-click audition / right-click root",
                                 juce::dontSendNotification);
            keyboard.onNote = [this](int pitch, bool on, int velocity)
            {
                if (owner && viewIdentity == owner->controller.projectIdentity)
                    owner->audio.previewChannelNote(channel, pitch, on, velocity);
            };
            keyboard.onRelease = [this]
            {
                if (owner)
                    owner->audio.stopChannelPreview(channel);
            };
            keyboard.onRoot = [this](int pitch)
            {
                change("channel.sampler.root", pitch);
                timerCallback();
            };
            rangeView.settings = [this]
            {
                if (owner)
                    if (auto *c = owner->controller.project.channel(channel))
                        return c->sampler;
                return SamplerSettings{};
            };
            rangeView.frames = [this] { return sample ? sample->data.getNumSamples() : 0; };
            rangeView.change = [this](bool start, double value)
            {
                change(start ? "channel.sampler.start" : "channel.sampler.end", value);
                timerCallback();
            };
            rangeView.changeRange = [this](double start, double end)
            {
                if (!ownsControlGesture())
                    return;
                CommandArgs args;
                args.channel = channel;
                args.beat = start;
                args.length = end;
                owner->command("channel.sampler.range", args);
                timerCallback();
            };
            rangeView.changeLoop = [this](bool start, double value)
            {
                change(start ? "channel.sampler.loopStart" : "channel.sampler.loopEnd", value);
                timerCallback();
            };
            rangeView.changeLoopRange = [this](double start, double end)
            {
                if (!ownsControlGesture())
                    return;
                CommandArgs args;
                args.channel = channel;
                args.beat = start;
                args.length = end;
                owner->command("channel.sampler.loopRange", args);
                timerCallback();
            };
            rangeView.begin = [this]
            {
                return beginControlGesture("Sample range");
            };
            rangeView.end = [this](bool cancel)
            {
                finishControlGesture(cancel);
            };
            gain.setComponentID("instrument.volume");
            pan.setComponentID("instrument.pan");
            mixer.setComponentID("instrument.mixer");
            piano.setComponentID("instrument.piano");
            source.setReadOnly(true);
            source.setMultiLine(false);
            gainLabel.setText("Volume", juce::dontSendNotification);
            panLabel.setText("Pan", juce::dontSendNotification);
            mixerLabel.setText("Mixer insert", juce::dontSendNotification);
            gain.setRange(0, 2, .001);
            pan.setRange(-1, 1, .001);
            for (auto *slider : {&gain, &pan})
            {
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 28);
                slider->onDragStart = [this]
                {
                    dragBlocked = !beginControlGesture("Instrument controls");
                };
                slider->onDragEnd = [this]
                {
                    if (!dragBlocked)
                        finishControlGesture(false);
                    dragBlocked = false;
                };
            }
            gain.onValueChange = [this] { change("channel.gain", gain.getValue()); };
            pan.onValueChange = [this] { change("channel.pan", pan.getValue()); };
            for (const auto& choice : mixerTargets.items())
                mixer.addItem(choice.name, choice.ordinal);
            mixer.onChange = [this]
            {
                if (updating || !owner) return;
                // getSelectedId() hides removed popup items as zero. The raw
                // Value retains the captured token so resolve can reject it.
                const int ordinal = int(mixer.getSelectedIdAsValue().getValue());
                auto safe = juce::Component::SafePointer<Controls>(this);
                if (ordinal > 0)
                {
                    CommandArgs args;
                    const auto valid = mixerTargets.resolve(owner->controller.project,
                        owner->controller.projectIdentity, ordinal, args);
                    if (valid.failed()) historyError(valid.getErrorMessage());
                    else owner->command("channel.mixer", args);
                }
                // Canonical dispatch may synchronously replace/destroy this UI.
                if (safe && safe->owner)
                    if (const auto* current = safe->owner->controller.project.channel(safe->channel))
                        safe->syncMixerChoices(*current, true);
            };
            preview.onClick = [this]
            {
                if (owner)
                    if (auto *c = owner->controller.project.channel(channel))
                        owner->audio.requestAudition(c->source);
            };
            piano.onClick = [this]
            {
                if (owner)
                {
                    owner->selectedChannel = channel;
                    owner->refresh();
                    owner->showEditor(1);
                }
            };
            load.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser>("Load sample for this Channel", juce::File{},
                                                              "*.wav;*.aiff;*.aif;*.flac");
                juce::Component::SafePointer<Controls> safe(this);
                chooser->launchAsync(
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [safe](const juce::FileChooser &file)
                    {
                        if (safe && safe->owner && file.getResult() != juce::File{})
                            safe->owner->loadSample(file.getResult().getFullPathName(), true, safe->channel);
                    });
            };
            restyle();
            timerCallback();
            startTimer(200);
            setSize(840, samplerControlsMinimumHeight);
        }
        bool selectModulationTarget(int target)
        {
            if (target < 0 || target >= int(samplerModulationTargets.size()) || !owner ||
                viewIdentity != owner->controller.projectIdentity || !finishControlGesture(false))
                return false;
            const auto *current = owner->controller.project.channel(channel);
            if (!current || current->instrument || current->audioClip)
                return false;
            modulationTarget = target;
            if (page != 1)
                setPage(1);
            timerCallback();
            return true;
        }
        int currentModulationTarget() const noexcept
        {
            return modulationTarget;
        }
        bool releaseInteractions()
        {
            rangeView.finish(true);
            keyboard.release();
            dragBlocked = false;
            return finishControlGesture(true);
        }
        ~Controls() override
        {
            if (detecting) tempoRequest("cancel", tempoOperation);
            stopTimer();
            rangeView.finish(true);
            finishControlGesture(true);
            rangeView.end = {};
            keyboard.release();
            keyboard.onNote = {};
            keyboard.onRelease = {};
            setLookAndFeel(nullptr);
        }
        bool keyPressed(const juce::KeyPress &key) override
        {
            if (!owner)
                return false;
            if (key.isKeyCode(juce::KeyPress::escapeKey) && ownsControlGesture())
            {
                rangeView.finish(true);
                finishControlGesture(true);
                dragBlocked = false;
                timerCallback();
                return true;
            }
            if (key.isKeyCode(juce::KeyPress::returnKey) && ownsControlGesture())
            {
                finishControlGesture(false);
                return true;
            }
            const auto action = owner->customization.resolve(key, Scope::sampler);
            if (action.startsWith("sampler.view."))
            {
                rangeView.navigation(action);
                return true;
            }
            const auto previous = owner->focus;
            owner->focus = Scope::sampler;
            const bool handled = owner->keyPressed(key);
            owner->focus = previous;
            return handled;
        }
        void paint(juce::Graphics &g) override
        {
            g.fillAll(panel);
            paintPageTabs(g);
            paintSections(g);
            if (audioClip && page == 0 && !waveformArea.isEmpty() && owner)
                if (auto *c = owner->controller.project.channel(channel))
                {
                    g.setColour(accent);
                    waveforms.get(c->source).drawChannels(g, waveformArea.reduced(6), 0, c->audioDuration, 1.f);
                }
        }
        void resized() override
        {
            layoutPages();
        }
    };

    class ScrollableControls final : public juce::Component
    {
        juce::Viewport viewport;

      public:
        explicit ScrollableControls(juce::Component *controls)
        {
            addAndMakeVisible(viewport);
            viewport.setScrollBarsShown(true, true);
            viewport.setViewedComponent(controls, true);
        }
        void resized() override
        {
            viewport.setBounds(getLocalBounds());
            viewport.getViewedComponent()->setSize(
                std::max(820, getWidth()),
                std::max(samplerControlsMinimumHeight, getHeight()));
        }
        juce::Component *controls() const
        {
            return viewport.getViewedComponent();
        }
    };

  public:
    juce::Component *controlsComponent() const
    {
        return static_cast<ScrollableControls *>(getContentComponent())->controls();
    }
    bool selectModulationTarget(int target)
    {
        return static_cast<Controls *>(controlsComponent())->selectModulationTarget(target);
    }
    void showPage(int page)
    {
        static_cast<Controls *>(controlsComponent())->showPage(page);
    }
    int currentModulationTarget() const
    {
        return static_cast<Controls *>(controlsComponent())->currentModulationTarget();
    }
    void refreshFromProject()
    {
        static_cast<Controls*>(controlsComponent())->refreshFromProject();
    }
    InstrumentWindow(Studio &studio, Id id)
        : DocumentWindow(studio.controller.project.channel(id)->name + " | Channel instrument", panel,
                         closeButton)
    {
        setLookAndFeel(&chrome);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(36);
        setContentOwned(new ScrollableControls(new Controls(studio, id)), true);
        centreWithSize(860, 640);
        setResizable(true, false);
        setResizeLimits(360, 260, 1400, 900);
        roundNativeWindow(*this);
    }
    ~InstrumentWindow() override
    {
        clearContentComponent();
        setLookAndFeel(nullptr);
    }
    void closeButtonPressed() override
    {
        if (static_cast<Controls *>(controlsComponent())->releaseInteractions())
            setVisible(false);
    }
};
} // namespace vibera
