// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
#include "WindowChrome.h"
#include "PlaylistWaveforms.h"
#include "SamplerGestures.h"
#include "SamplerTempo.h"
#include "SamplerFilterSettings.h"
#include "SamplerModulationPanel.h"
#include "MixerTargetChoices.h"
#include <type_traits>
#include <charconv>
namespace vibera
{
class Studio::InstrumentWindow final : public juce::DocumentWindow
{
    WindowChrome chrome;
    static constexpr int samplerControlsMinimumHeight = 2131;
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
        juce::ToggleButton keepPitch{"Keep pitch"};
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
                                juce::String(samplerFilterQ(values[1]), 2) +
                                "\nPreview source is dry; play the keyboard to audition Channel processing."
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
            if (!detecting)
                tempoInfo.setText(tempoError.isNotEmpty() ? tempoError : c->sampler.sourceTempo > 0
                                      ? "Source " + juce::String(c->sampler.sourceTempo, 2) +
                                            " BPM | Resample (changes pitch)"
                                      : "Source tempo unknown | Resample (changes pitch)",
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

            rangeView.setVisible(!audioClip);
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
                    const auto parsed = std::from_chars(first, last, value);
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
            timerCallback();
            startTimer(200);
            setSize(620, samplerControlsMinimumHeight);
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
            timerCallback();
            if (auto *viewport = findParentComponentOfClass<juce::Viewport>())
                viewport->setViewPosition(0, modulationPanel.getY());
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
            auto r = juce::Rectangle<float>(18, 88, float(getWidth() - 36), 104);
            g.setColour(bg);
            g.fillRoundedRectangle(r, 4);
            g.setColour(accent);
            if (audioClip)
            {
                if (owner)
                    if (auto *c = owner->controller.project.channel(channel))
                        waveforms.get(c->source).drawChannels(g, r.toNearestInt(), 0, c->audioDuration, 1.f);
                return;
            }
        }
        void resized() override
        {
            int width = getWidth() - 36;
            heading.setBounds(18, 8, width, 28);
            source.setBounds(18, 42, width, 30);
            info.setBounds(18, 196, width, 26);
            rangeView.setBounds(18, 88, width, 104);
            modulationPanel.setBounds(18, 1463, width, 552);
            keyboardHint.setBounds(18, 2023, width, 24);
            keyboard.setBounds(18, 2051, width, 70);
            filterHeading.setBounds(18, 1275, width, 24);
            filterTypeLabel.setBounds(18, 1303, 145, 28);
            filterType.setBounds(168, 1303, width - 150, 28);
            for (size_t i = 0; i < filterSliders.size(); ++i)
            {
                const int y = 1339 + int(i) * 36;
                filterLabels[i].setBounds(18, y, 145, 28);
                filterSliders[i].setBounds(168, y, width - 150, 28);
            }
            filterHint.setBounds(18, 1411, width, 46);
            int tempoWidth = (width - 18) / 4;
            setTempo.setBounds(18, 228, tempoWidth, 28);
            detectTempo.setBounds(24 + tempoWidth, 228, tempoWidth, 28);
            fitTempo.setBounds(30 + 2 * tempoWidth, 228, tempoWidth, 28);
            fitLength.setBounds(36 + 3 * tempoWidth, 228, tempoWidth, 28);
            keepPitch.setBounds(18, 262, 120, 28);
            tempoInfo.setBounds(144, 262, width - 126, 28);
            gainLabel.setBounds(18, 298, 68, 28);
            gain.setBounds(90, 298, width - 72, 28);
            panLabel.setBounds(18, 332, 68, 28);
            pan.setBounds(90, 332, width - 72, 28);
            mixerLabel.setBounds(18, 368, 100, 28);
            mixer.setBounds(120, 368, width - 102, 28);
            const int buttonWidth = (width - 24) / 5;
            int buttonX = 18;
            for (auto *button : {&load, &preview, &previewProcessed, &piano, &showKeys})
            {
                button->setBounds(buttonX, 410, buttonWidth, 28);
                buttonX += buttonWidth + 6;
            }
            const int column = (width - 24) / 3;
            for (size_t i = 0; i < voiceSliders.size(); ++i)
            {
                const int x = 18 + int(i) * (column + 12);
                voiceLabels[i].setBounds(x, 450, column, 24);
                voiceSliders[i].setBounds(x, 478, column, 28);
            }
            voiceModeHeading.setBounds(18, 550, width, 22);
            const int modeColumn = (width - 16) / 3;
            for (size_t i = 0; i < voiceModeToggles.size(); ++i)
                voiceModeToggles[i].setBounds(18 + int(i) * (modeColumn + 8), 576, modeColumn, 28);
            for (size_t i = 0; i < voiceModeSliders.size(); ++i)
            {
                const int y = 610 + int(i) * 36;
                voiceModeLabels[i].setBounds(18, y, 145, 28);
                voiceModeSliders[i].setBounds(168, y, width - 150, 28);
            }
            voiceModeHint.setBounds(18, 680, width, 52);
            cutItself.setBounds(18, 514, 126, 28);
            voiceHint.setBounds(152, 514, width - 134, 28);
            reverse.setBounds(18, 740, 110, 28);
            normalize.setBounds(134, 740, 110, 28);
            hq.setBounds(248, 740, 60, 28);
            envelope.setBounds(312, 740, width - 294, 28);
            loop.setBounds(18, 774, 190, 28);
            pingPong.setBounds(218, 774, 150, 28);
            fileLoop.setBounds(374, 774, std::max(110, width - 356), 28);
            for (size_t i = 0; i < samplerSliders.size(); ++i)
            {
                int y = 812 + int(i) * 35;
                samplerLabels[i].setBounds(18, y, 145, 28);
                samplerSliders[i].setBounds(168, y, width - 150, 28);
            }
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
                std::max(560, getWidth() - scrollBarThickness),
                std::max(samplerControlsMinimumHeight, getHeight() - scrollBarThickness));
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
        centreWithSize(660, 680);
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
