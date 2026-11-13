// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "ScoreTools.h"
#include "InstrumentWindow.h"
#include "SamplerGestures.h"
#include "SamplerControllerContracts.h"
#include "Settings.h"
#include "../tests/SyntheticDevice.h"
#include "../tests/ControllerBindingAcceptance.h"
#include "../tests/SamplerVoiceControllerAcceptance.h"

namespace vibera
{
juce::Result Studio::selectChannelChecked(Id channel)
{
    if (!controller.project.channel(channel))
        return juce::Result::fail("Unknown Channel");
    if (channel != selectedChannel)
    {
        if (auto finished = finishMidiTake(); finished.failed())
            return juce::Result::fail("take_pending: " + finished.getErrorMessage());
        releaseTypingNotes();
    }
    selectedChannel = channel;
    refresh();
    needsRebuild = true;
    return juce::Result::ok();
}
bool Studio::navigateSamplerView(const juce::String &action)
{
    const auto found = instrumentWindows.find(selectedChannel);
    if (found == instrumentWindows.end())
        return false;
    auto *range = dynamic_cast<SamplerRangeView *>(
        found->second->controlsComponent()->findChildWithID("instrument.sampler.waveform"));
    if (!range)
        return false;
    range->navigation(action);
    return true;
}
juce::String Studio::detectedChord() const
{
    // Keys held on the typing keyboard / pads name the chord first; otherwise the selected notes do.
    auto held = performanceInput.heldPitches();
    for (const int pitch : audio.midiInput.heldKeys.pitches()) // Keys down on the hardware MIDI input.
        if (std::find(held.begin(), held.end(), pitch) == held.end())
            held.push_back(pitch);
    if (const auto live = score::detectChord(held); live.isNotEmpty())
        return live;
    std::vector<int> pitches;
    if (const auto *pattern = controller.project.pattern(selectedPattern))
        for (const auto &note : pattern->notes)
            if (std::find(selectedNotes.begin(), selectedNotes.end(), note.id) != selectedNotes.end())
                pitches.push_back(note.pitch);
    return score::detectChord(pitches);
}
double Studio::positionSeconds() const
{
    const double beat = audio.positionBeat();
    return patternMode ? beat * 60. / std::max(1., double(controller.project.tempo)) : controller.project.songSeconds(beat);
}
juce::File Studio::projectBackupsFolder() const
{
    const auto folder = profileFile().getSiblingFile("backups");
    const auto own = projectFile == juce::File{} ? juce::File{}
                                                 : folder.getChildFile(projectFile.getFileNameWithoutExtension());
    return own.isDirectory() ? own : folder;
}
juce::Result Studio::setControllerPorts(const juce::String &input, const juce::String &output)
{
    // The control port must not be the performance input: its notes would also play the Channel.
    if (input.isNotEmpty() && input == audio.midiInput.identifier())
        return juce::Result::fail("Use a separate control port, or learn mappings on Performance input with "
                                  "control port Off.");
    return midiController.setPorts(input, output);
}
void Studio::loadLaunchkeyPreset()
{
    midiController.launchkeyPreset();
    midiController.save();
    midiController.setPorts(midiController.inputIdentifier(), midiController.outputIdentifier());
}
juce::Result Studio::connectLaunchkey(juce::String &message)
{
    juce::String performance, control, feedback;
    for (const auto &device : juce::MidiInput::getAvailableDevices())
        if (device.name.containsIgnoreCase("LKMK3"))
        {
            if (device.name.containsIgnoreCase("MIDIIN2"))
                control = device.identifier;
            else
                performance = device.identifier;
        }
    for (const auto &device : juce::MidiOutput::getAvailableDevices())
        if (device.name.containsIgnoreCase("LKMK3") && device.name.containsIgnoreCase("MIDIOUT2"))
            feedback = device.identifier;
    if (control.isEmpty() || feedback.isEmpty())
    {
        message = "Launchkey MK3 DAW ports not found; select ports manually.";
        return juce::Result::fail("controller.launchkeyNotFound: " + message);
    }
    if (!performance.isEmpty() && audio.midiInput.identifier() != performance)
        audio.midiInput.setDevice(performance);
    const auto result = midiController.setPorts(control, feedback);
    message = result.wasOk() ? juce::String("Launchkey connected. Load MK3 preset to enable DAW mode.")
                             : result.getErrorMessage();
    return result;
}
void Studio::invokeController(const ControllerInvocation &value)
{
    if (value.automationSession && (!automationTouch || value.automationSession != automationTouch->session))
        return;
    if (automationTouchDraining && !value.parameter)
        return; // Stop/Cancel already owns the cutoff.
    if (automationTouch && value.parameter &&
        (value.automationSession != automationTouch->session || !value.automationClockValid))
    {
        if (value.automationSession == automationTouch->session && !value.automationClockValid)
        {
            automationTouch->phase = MixerAutomationTake::Phase::invalid;
            automationTouch->inputFailed = true;
            audio.midiInput.controls.endAutomationStamp();
            automationTouch->error =
                "automationRecording.invalidClock: Producer could not read the actual recording clock";
        }
        status = "automationRecording.staleInput: Unstamped pre-arm value was not recorded or applied";
        if (controllerMcpInjection)
            controllerMcpRejected = true;
        return;
    }
    const juce::ScopedValueSetter<unsigned> stampSession(automationTouchInputSession,
                                                         value.automationSession);
    const juce::ScopedValueSetter<double> stampSeconds(automationTouchInputSeconds, value.automationSeconds);
    const juce::ScopedValueSetter<bool> stampValid(automationTouchInputValid, value.automationClockValid);
    if (controllerMcpInjection && !value.parameter)
    {
        static const juce::StringArray safe{"transport.play",
                                            "transport.loop",
                                            "transport.stop",
                                            "transport.mode",
                                            "transport.record",
                                            "transport.metronome",
                                            "transport.countIn",
                                            "edit.undo",
                                            "edit.redo",
                                            "midi.panic",
                                            "view.rack",
                                            "view.piano",
                                            "view.playlist",
                                            "view.mixer",
                                            "view.restoreLayout",
                                            "controller.bankPrevious",
                                            "controller.bankNext",
                                            "clipboard.copy",
                                            "clipboard.paste",
                                            "notes.duplicate",
                                            "notes.mute",
                                            "notes.unmute",
                                            "piano.toolMute",
                                            "piano.toolSlice",
                                            "notes.slice",
                                            "notes.stretch",
                                            "mixer.rename",
                                            "mixer.colour",
                                            "mixer.mute",
                                            "mixer.phaseInvert",
                                            "mixer.stereoSwap",
                                            "mixer.solo",
                                            "mixer.soloConnected",
                                            "mixer.unsolo",
                                            "mixer.unmuteAll",
                                            "mixer.route",
                                            "mixer.routeOnly",
                                            "mixer.send",
                                            "notes.quantize",
                                            "clip.duplicate",
                                            "clip.unique",
                                            "clip.group",
                                            "clip.ungroup",
                                            "selection.all",
                                            "selection.none",
                                            "selection.cancel",
                                            "channel.sampler.cutItself",
                                            "channel.sampler.normalize",
                                            "channel.sampler.mono",
                                            "channel.sampler.portamento",
                                            "channel.sampler.fullPortamento"};
        if (!safe.contains(value.action) && !value.action.startsWith("workspace.") &&
            !value.action.startsWith("piano.") && !value.action.startsWith("playlist.") &&
            !value.action.startsWith("macro."))
        {
            controllerMcpRejected = true;
            status = "Mapped action not exposed through MCP; filesystem/dialog boundary preserved";
            return;
        }
    }
    const bool fixedSampler = value.projectLink > 0;
    if (fixedSampler)
    {
        if (!midiController.projectInvocationCurrent(value, controller.project, controller.projectIdentity) ||
            !value.samplerAvailable)
        {
            status = !value.samplerAvailable
                         ? "controller.samplerUnavailable: Fixed target is dormant; link retained"
                         : "controller.staleInvocation: Project link changed; input ignored";
            if (controllerMcpInjection)
                controllerMcpRejected = true;
            return;
        }
    }
    else if (value.binding >= midiController.bindings.size())
        return;
    if (value.binding >= controllerPickup.size())
        return;
    if (auto *modal = juce::Component::getCurrentlyModalComponent();
        modal && modal != settingsWindow.getComponent())
        return;
    if (value.action == "controller.bankPrevious" || value.action == "controller.bankNext")
    {
        midiController.moveBank(value.action == "controller.bankNext" ? 1 : -1,
                                controllerMixerCount(controller.project));
        status = controllerMixerBankLabel(controller.project, midiController.mixerBank);
        repaint();
        return;
    }
    const bool mappedMixerControl = value.action == "mixer.gain" || value.action == "mixer.pan" ||
                                    value.action == "mixer.stereoSeparation" ||
                                    value.action == "mixer.send" || value.action == "mixer.mute" ||
                                    value.action == "mixer.phaseInvert" || value.action == "mixer.stereoSwap";
    int mixerTarget = value.mixer < 0 ? selectedInsert : value.mixer;
    if (mappedMixerControl)
        if (auto resolved = resolveControllerMixerTarget(controller.project, value.mixer, value.mixerBanked,
                                                         value.mixerBankOffset, selectedInsert, mixerTarget);
            resolved.failed())
        {
            status = resolved.getErrorMessage();
            if (controllerMcpInjection)
                controllerMcpRejected = true;
            repaint();
            return;
        }
    const bool voiceMode = isSamplerVoiceModeCommand(value.action);
    const bool voiceFlag = value.action == "channel.sampler.mono" ||
                           value.action == "channel.sampler.portamento" ||
                           value.action == "channel.sampler.fullPortamento";
    if (voiceMode && controller.hasGesture())
    {
        status = "controller.editBusy: Finish the current gesture before a mapped sampler voice edit";
        if (controllerMcpInjection)
            controllerMcpRejected = true;
        return;
    }
    if (!value.parameter)
    {
        if (voiceFlag)
        {
            CommandArgs args;
            args.channel = selectedChannel;
            if (const auto *channel = controller.project.channel(selectedChannel))
                args.enabled =
                    !(value.action == "channel.sampler.mono"         ? channel->sampler.mono
                      : value.action == "channel.sampler.portamento" ? channel->sampler.portamento
                                                                     : channel->sampler.fullPortamento);
            command(value.action, args); // Same canonical role/history/refusal path as UI/MCP.
            if (controllerMcpInjection && status != "Ready")
                controllerMcpRejected = true;
            return;
        }
        if (value.action == "channel.sampler.normalize" || value.action == "channel.sampler.reverse" ||
            value.action == "channel.sampler.envelope" || value.action == "channel.sampler.loop" ||
            value.action == "channel.sampler.pingPong" || value.action == "channel.sampler.fitTempo")
        {
            if (const auto *channel = controller.project.channel(selectedChannel))
            {
                const auto &sample = channel->sampler;
                const bool enabled = value.action == "channel.sampler.normalize"  ? sample.normalize
                                     : value.action == "channel.sampler.reverse"  ? sample.reverse
                                     : value.action == "channel.sampler.envelope" ? sample.envelope
                                     : value.action == "channel.sampler.loop"     ? sample.loop
                                     : value.action == "channel.sampler.pingPong" ? sample.pingPong
                                                                                  : sample.fitTempo;
                CommandArgs args;
                args.channel = selectedChannel;
                args.enabled = !enabled;
                command(value.action, args);
            }
            return;
        }
        if (value.action == "mixer.mute" || value.action == "mixer.phaseInvert" || value.action == "mixer.stereoSwap")
        {
            const int index = mixerTarget;
            for (const auto &insert : controller.project.mixer)
                if (insert.index == index)
                {
                    CommandArgs args;
                    args.mixer = index;
                    args.enabled = value.action == "mixer.mute" ? !insert.muted :
                        value.action == "mixer.phaseInvert" ? !insert.phaseInverted : !insert.stereoSwapped;
                    command(value.action, args);
                    return;
                }
            return;
        }
        if (value.action.startsWith("sampler.view."))
        {
            openInstrument(selectedChannel);
            if (auto found = instrumentWindows.find(selectedChannel); found != instrumentWindows.end())
                if (auto *range = dynamic_cast<SamplerRangeView *>(
                        found->second->controlsComponent()->findChildWithID("instrument.sampler.waveform")))
                    range->navigation(value.action);
            return;
        }
        if (controller.hasGesture())
            return;
        const auto previousFocus = focus;
        if (value.action.startsWith("playlist."))
            focus = Scope::playlist;
        const bool handled = performAction(value.action);
        if (value.action.startsWith("playlist."))
            focus = previousFocus;
        if (!handled)
        {
            status = "Controller action unavailable in current context";
            repaint();
        }
        return;
    }
    if (controller.hasGesture())
        return; // Never merge a fader update into a pointer edit.
    CommandArgs args;
    args.pattern = selectedPattern;
    args.channel = fixedSampler ? value.channel : selectedChannel;
    args.ids = selectedNotes;
    args.mixer = mappedMixerControl ? mixerTarget : value.mixer < 0 ? selectedInsert : value.mixer;
    double current = 0;
    juce::String target =
        controller.projectIdentity + value.action + ":" + juce::String(value.bindingStateGeneration);
    if (value.action.startsWith("mixer."))
        target += ":" + juce::String(args.mixer);
    if (value.action == "mixer.send")
        target += ":" + juce::String(value.sendTarget);
    else if (value.action.startsWith("channel."))
        target += ":" + juce::String(args.channel);
    if (fixedSampler)
        target += ":" + juce::String(value.projectLink) + ":" + juce::String(int(value.samplerParameter));
    auto *channel = controller.project.channel(args.channel);
    if (value.action == "project.tempo")
        current = controller.project.tempo;
    else if (value.action == "pattern.length")
    {
        current = controller.project.pattern(selectedPattern)->length;
        target += ":" + juce::String(selectedPattern);
    }
    else if (value.action == "notes.velocity")
    {
        if (selectedNotes.empty())
            return;
        for (const auto &note : controller.project.pattern(selectedPattern)->notes)
            if (note.id == selectedNotes.front())
                current = note.velocity;
        target += ":" + juce::String(selectedNotes.front());
    }
    else if (value.action == "mixer.send")
    {
        const auto *edge = mixerRoute(controller.project, args.mixer, value.sendTarget);
        if (!edge)
        {
            status = "controller.missingSendTarget: Bound source/destination route is unavailable; mapping "
                     "retained";
            if (controllerMcpInjection)
                controllerMcpRejected = true;
            repaint();
            return;
        }
        args.target = value.sendTarget;
        current = edge->gain;
    }
    else if (value.action.startsWith("mixer."))
    {
        bool found = false;
        for (const auto &insert : controller.project.mixer)
            if (insert.index == args.mixer)
            {
                found = true;
                current = value.action == "mixer.pan" ? insert.pan :
                    value.action == "mixer.stereoSeparation" ? insert.stereoSeparation : insert.gain;
            }
        if (!found)
        {
            status = "This controller slot has no Mixer insert in the current bank";
            repaint();
            return;
        }
    }
    else if (!channel)
    {
        if (voiceMode)
        {
            command(value.action, args); // Canonical missing-owner refusal, never selected fallback.
            if (controllerMcpInjection && status != "Ready")
                controllerMcpRejected = true;
        }
        return;
    }
    else if (value.action == "channel.gain")
        current = channel->gain;
    else if (value.action == "channel.pan")
        current = channel->pan;
    else if (fixedSampler)
    {
        SamplerAutomationTarget resolved;
        if (resolveSamplerAutomationTarget(controller.project, value.channel, value.samplerParameter,
                                           resolved)
                .failed())
            return;
        args.text = value.samplerDestination;
        current = resolved.manual;
    }
    else if (value.action.startsWith("channel.sampler."))
    {
        const auto &sample = channel->sampler;
        if (voiceMode && (channel->instrument || channel->audioClip))
        {
            command(value.action, args); // Let canonical command refuse before pickup consumes anything.
            if (controllerMcpInjection && status != "Ready")
                controllerMcpRejected = true;
            return;
        }
        if (value.action == "channel.sampler.mono")
            current = sample.mono ? 1 : 0;
        else if (value.action == "channel.sampler.portamento")
            current = sample.portamento ? 1 : 0;
        else if (value.action == "channel.sampler.fullPortamento")
            current = sample.fullPortamento ? 1 : 0;
        else if (value.action == "channel.sampler.slideTime")
            current = sample.slideTime;
        else if (value.action == "channel.sampler.gateBeats")
            current = sample.gateBeats;
        else if (value.action == "channel.sampler.root")
            current = sample.root;
        else if (value.action == "channel.sampler.cents")
            current = sample.cents;
        else if (value.action == "channel.sampler.start")
            current = sample.start;
        else if (value.action == "channel.sampler.end")
            current = sample.end;
        else if (value.action == "channel.sampler.loopStart")
            current = sample.loopStart;
        else if (value.action == "channel.sampler.loopEnd")
            current = sample.loopEnd;
        else if (value.action == "channel.sampler.attack")
            current = sample.attack;
        else if (value.action == "channel.sampler.decay")
            current = sample.decay;
        else if (value.action == "channel.sampler.sustain")
            current = sample.sustain;
        else if (value.action == "channel.sampler.release")
            current = sample.release;
        else if (value.action == "channel.sampler.sourceTempo")
            current = sample.sourceTempo;
        else if (value.action == "channel.sampler.cut")
            current = sample.cut;
        else if (value.action == "channel.sampler.cutBy")
            current = sample.cutBy;
        else if (value.action == "channel.sampler.maxPoly")
            current = sample.maxPoly;
        else if (value.action == "channel.sampler.filterType")
            current = sample.filterType;
        else if (value.action == "channel.sampler.filterCutoff")
            current = sample.filterCutoff;
        else if (value.action == "channel.sampler.filterResonance")
            current = sample.filterResonance;
        else
            return;
    }
    else
        return;
    const double span = value.maximum - value.minimum;
    auto &pickup = controllerPickup[value.binding];
    const double normalized = juce::jlimit(0., 1., (current - value.minimum) / span);
    if (pickup.target != target || (pickup.caught && std::abs(normalized - pickup.applied) > .001))
        pickup = {target, -1, -1, false};
    if (value.pickup && !pickup.caught)
    {
        pickup.caught =
            std::abs(normalized - value.normalized) <= 1.5 / 127. ||
            (pickup.previous >= 0 && (pickup.previous - normalized) * (value.normalized - normalized) <= 0);
        pickup.previous = value.normalized;
        if (!pickup.caught)
        {
            status = "Controller pickup: move to current value";
            repaint();
            return;
        }
    }
    args.value =
        value.delta ? juce::jlimit(value.minimum, value.maximum, current + value.delta) : value.value;
    if (value.action == "channel.sampler.root" || value.action == "channel.sampler.cut" ||
        value.action == "channel.sampler.cutBy" || value.action == "channel.sampler.maxPoly" ||
        value.action == "channel.sampler.filterType")
        args.value = std::round(args.value);
    if (fixedSampler)
        if (const auto parameter = lookupSamplerAutomationParameter(value.samplerParameter);
            parameter && parameter->kind == SamplerAutomationValueKind::boolean)
            args.enabled = args.value >= .5;
    if (voiceFlag)
        args.enabled = args.value >= .5;
    command(value.action, args);
    if (voiceMode && controllerMcpInjection && status != "Ready")
        controllerMcpRejected = true;
    if (status == "Ready")
        pickup.applied = ((voiceFlag ? (args.enabled ? 1. : 0.) : args.value) - value.minimum) / span;
}
juce::Result Studio::verifyControllers(const juce::File &folder)
{
    stopTimer();
    if (auto result = folder.createDirectory(); result.failed())
        return result;
    auto fail = [](const juce::String &message) { return juce::Result::fail(message); };
    const auto bindingOutput = folder.getChildFile("binding-" + juce::Uuid().toString());
    if (auto result = test::controller_binding::verify(bindingOutput); result.failed())
        return result;
    {
        // This instance is already isolated and hardware=false. Exercise the
        // production dispatcher gate without enabling real hardware/settings.
        const juce::ScopedValueSetter<bool> publicApi(controllerTestMode, false);
        if (auto result = test::controller_binding::verifyPublicMcp(*this); result.failed())
            return result;
    }
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
        return fail("Controller virtual device unavailable");
    auto project = Project::demo();
    project.tempo = 240;
    project.channels.back().pan = 1;
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    selectedChannel = project.channels.back().id;
    refresh();
    midiController.launchkeyPreset();
    auto send = [&](int number, int value)
    {
        audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(16, number, value), 1);
        midiController.drain();
        dispatchMessages(1);
    };
    const auto originalGain = controller.project.mixer[1].gain;
    send(53, 0);
    if (controller.project.mixer[1].gain != originalGain)
        return fail("Pickup jumped to remote fader value");
    send(53, 127);
    if (controller.project.mixer[1].gain != 1.5f)
        return fail("Pickup did not catch crossing target");
    performAction("edit.undo");
    if (controller.project.mixer[1].gain != originalGain)
        return fail("Controller value Undo not exact");
    performAction("edit.redo");
    if (controller.project.mixer[1].gain != 1.5f)
        return fail("Controller value Redo not exact");
    send(103, 127);
    send(103, 0);
    send(103, 127);
    send(103, 0);
    if (midiController.mixerBank != 16)
        return fail("Bank buttons did not reach Insert17");
    for (auto &binding : midiController.bindings)
        binding.pickup = false;
    midiController.publish();
    send(53, 64);
    if (std::abs(controller.project.mixer[17].gain - 1.5 * 64 / 127.) > 1e-6)
        return fail("Final insert fader mapping");
    send(61, 64);
    if (std::abs(controller.project.mixer[0].gain - 1.5 * 64 / 127.) > 1e-6)
        return fail("Banked Master mapping");
    midiController.mixerBank = 0;
    const auto beforeLearn = controller.project.state();
    midiController.armLearn(0);
    send(90, 127);
    if (midiController.learnIndex != -1 || midiController.bindings[0].number != 90 || audio.playing() ||
        !beforeLearn.isEquivalentTo(controller.project.state()))
        return fail("Learn invoked a command");
    // Additive actual Studio controller dispatch, before any callback playback.
    // No native variable-universe/PCM claim: restore the exact original project
    // before the unchanged live controller workload below prepares its graph.
    {
        const auto savedProject = controller.project;
        const auto savedProfile = midiController.state();
        const int savedSelection = selectedInsert;
        auto variable = savedProject;
        for (auto &row : variable.mixer)
            if (row.index == 17)
                row.index = 500;
        std::swap(variable.mixer[1], variable.mixer[17]);
        controller.replace(variable);
        selectedInsert = 1;
        ControllerBinding mapped;
        mapped.channel = 16;
        mapped.number = 93;
        mapped.mode = 1;
        mapped.action = "mixer.gain";
        mapped.mixer = 500;
        mapped.maximum = 2;
        mapped.pickup = false;
        midiController.bindings = {mapped};
        midiController.publish();
        const juce::ScopedValueSetter<bool> semanticInjection(controllerMcpInjection, true);
        const auto inject = [&](int value)
        {
            controllerMcpRejected = false;
            audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(16, 93, value), 1);
            midiController.drain();
        };
        const auto unaffected = mixerInsert(controller.project, 1)->gain;
        inject(64);
        if (controllerMcpRejected ||
            std::abs(mixerInsert(controller.project, 500)->gain - 128. / 127) > 1.e-6 ||
            mixerInsert(controller.project, 1)->gain != unaffected || selectedInsert != 1)
            return fail("Sparse fixed controller gain retargeted selected/display insert");
        for (const auto *action : {"mixer.pan", "mixer.stereoSeparation"})
        {
            mapped.action = action;
            mapped.minimum = -1;
            mapped.maximum = 1;
            midiController.bindings = {mapped};
            midiController.publish();
            inject(127);
            const auto *target = mixerInsert(controller.project, 500);
            if (controllerMcpRejected ||
                (mapped.action == "mixer.pan" ? target->pan : target->stereoSeparation) != 1)
                return fail("Sparse fixed controller bipolar target did not resolve actual insert");
        }
        for (const auto *action : {"mixer.mute", "mixer.phaseInvert", "mixer.stereoSwap"})
        {
            mapped.action = action;
            mapped.mode = 0;
            midiController.bindings = {mapped};
            midiController.publish();
            inject(127);
            inject(0);
            const auto *target = mixerInsert(controller.project, 500);
            if (controllerMcpRejected || !(mapped.action == "mixer.mute"          ? target->muted
                                           : mapped.action == "mixer.phaseInvert" ? target->phaseInverted
                                                                                  : target->stereoSwapped))
                return fail("Sparse fixed controller button target did not resolve actual insert");
        }
        mapped.action = "mixer.gain";
        mapped.mode = 1;
        mapped.minimum = 0;
        mapped.maximum = 2;
        mapped.mixer = -1;
        selectedInsert = 500;
        midiController.bindings = {mapped};
        midiController.publish();
        inject(127);
        if (controllerMcpRejected || mixerInsert(controller.project, 500)->gain != 2)
            return fail("Selected sparse controller target failed");
        mapped.mixer = 1;
        mapped.banked = true;
        midiController.mixerBank = 0;
        midiController.bindings = {mapped};
        midiController.publish();
        inject(0);
        if (controllerMcpRejected || mixerInsert(controller.project, 500)->gain != 0)
            return fail("Controller bank ignored reordered current display position");
        auto sendProject = controller.project;
        auto sendSource = std::find_if(sendProject.mixer.begin(), sendProject.mixer.end(),
                                       [](const auto &row) { return row.index == 500; });
        sendSource->routes = {{0, .75f}, {2, .25f}};
        controller.replace(sendProject);
        const auto sparseAutomationBaseline = controller.project.state();
        const auto sparseArgs = juce::JSON::parse(
            R"({"command":"automation.init","args":{"mixer":500,"automationControl":"send","automationSendTarget":2,"value":0.5}})");
        (void)mcpCall("vibera_command", sparseArgs);
        const AutomationKey sparseKey{AutomationParameter::mixerSend, 500, 2};
        const auto sparseValue = std::find_if(controller.project.automation.initialValues.begin(),
                                              controller.project.automation.initialValues.end(),
                                              [&](const auto &item) { return item.key == sparseKey; });
        if (sparseValue == controller.project.automation.initialValues.end() || sparseValue->value != .5f)
            return fail("Public automation command rejected/retargeted stable sparse Mixer ID");
        performAction("edit.undo");
        if (!controller.project.state().isEquivalentTo(sparseAutomationBaseline))
            return fail("Sparse send initialization Undo does not restore exact original model");
        mapped.action = "mixer.send";
        mapped.mixer = 500;
        mapped.banked = false;
        mapped.sendTarget = 2;
        mapped.minimum = 0;
        mapped.maximum = 2;
        midiController.bindings = {mapped};
        midiController.publish();
        const auto sendBefore = controller.project.state();
        const auto unchangedGain = mixerInsert(controller.project, 500)->gain;
        inject(127);
        if (controllerMcpRejected || !mixerRoute(controller.project, 500, 2) ||
            mixerRoute(controller.project, 500, 2)->gain != 2 ||
            mixerRoute(controller.project, 500, 0)->gain != .75f ||
            mixerInsert(controller.project, 500)->gain != unchangedGain)
            return fail("Controller send changed the wrong edge/control");
        performAction("edit.undo");
        if (!controller.project.state().isEquivalentTo(sendBefore))
            return fail("Send controller Undo does not restore exact canonical state");
        performAction("edit.redo");
        if (mixerRoute(controller.project, 500, 2)->gain != 2)
            return fail("Send controller Redo does not restore exact edge level");
        const auto sendProfile = midiController.state();
        const auto sendProfileFile = folder.getChildFile("controller-send-profile.json");
        if (midiController.save(sendProfileFile).failed() ||
            juce::JSON::toString(juce::JSON::parse(sendProfileFile)) != juce::JSON::toString(sendProfile))
            return fail("Actual saved controller profile lost explicit send destination");
        mapped.sendTarget = 0;
        midiController.bindings = {mapped};
        midiController.publish();
        inject(0);
        if (controllerMcpRejected || mixerRoute(controller.project, 500, 0)->gain != 0 ||
            mixerRoute(controller.project, 500, 2)->gain != 2)
            return fail("Controller Master send ignored explicit destination0");
        auto missingEdge = controller.project;
        auto source = std::find_if(missingEdge.mixer.begin(), missingEdge.mixer.end(),
                                   [](const auto &row) { return row.index == 500; });
        std::erase_if(source->routes, [](const auto &edge) { return edge.target == 2; });
        controller.replace(missingEdge);
        mapped.sendTarget = 2;
        midiController.bindings = {mapped};
        midiController.publish();
        const auto missingEdgeBefore = controller.project.state();
        inject(127);
        if (!controllerMcpRejected || !status.startsWith("controller.missingSendTarget:") ||
            !controller.project.state().isEquivalentTo(missingEdgeBefore) ||
            midiController.bindings.front().sendTarget != 2)
            return fail("Missing controller send silently retargeted or changed project/profile");
        controllerMcpRejected = false;
        mapped.action = "mixer.gain";
        mapped.sendTarget = -1;
        mapped.mixer = 500;
        mapped.banked = false;
        midiController.bindings = {mapped};
        midiController.publish();
        auto removed = controller.project;
        std::erase_if(removed.mixer, [](const auto &row) { return row.index == 500; });
        controller.replace(removed);
        selectedInsert = 0;
        const auto deletedBefore = controller.project.state();
        inject(127);
        if (!controllerMcpRejected || !status.startsWith("controller.missingMixerTarget:") ||
            midiController.bindings.front().mixer != 500 ||
            !controller.project.state().isEquivalentTo(deletedBefore))
            return fail("Deleted fixed controller target silently remapped or profile changed");
        controller.replace(savedProject);
        selectedInsert = savedSelection;
        if (auto restored = midiController.importState(savedProfile); restored.failed())
            return restored;
        controllerMcpRejected = false;
        if (!folder.getChildFile("controller-variable-targets.txt")
                 .replaceWithText(
                     "PASS actual shared Studio injected gain/pan/separation/mute/phase/swap, selected/fixed "
                     "sparse IDs, "
                     "bank display resolution and typed missing-target refusal. CONTROL only before "
                     "unchanged original playback; no variable native graph/audio claim.\n"))
            return fail("Cannot retain variable controller dispatch evidence");
    }
    midiController.launchkeyPreset();
    for (auto &binding : midiController.bindings)
        binding.pickup = false;
    midiController.publish();
    juce::String evidence;
    juce::AudioBuffer<float> buffer(2, 256);
    patternMode = true;
    if (auto result = audio.rebuild(controller.project, true, selectedPattern); result.failed())
        return result;
    needsRebuild = false;
    audio.seekBeat(0);
    audio.playStop();
    dispatchMessages(20);
    for (int tick = 0; tick < 12; ++tick)
        device->process(buffer);
    record.onClick();
    if (!midiTaking || !audio.midiInput.capture.active.load())
        return fail("Controller recording fixture did not arm");
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(90)));
    for (int edit = 0; edit < 8; ++edit)
    {
        send(53, 90 + edit);
        timerCallback();
        device->process(buffer);
        if (!midiTaking || !audio.midiInput.capture.active.load())
            return fail("Fader interrupted active MIDI take");
    }
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    device->process(buffer);
    stop.onClick();
    if (midiTaking)
        return fail("Controller recording fixture did not finish");
    evidence += "Injected performance note capture remains active through eight mapped fader edits; Stop "
                "finishes normally.\n";
    for (bool pattern : {true, false})
    {
        patternMode = pattern;
        mode.setButtonText(pattern ? "Pattern" : "Song");
        if (auto result = audio.rebuild(controller.project, pattern, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        audio.seekBeat(0);
        audio.playStop();
        dispatchMessages(20);
        for (int tick = 0; tick < 12; ++tick)
            device->process(buffer);
        const auto before = audio.projectionDiagnostics();
        const auto position = audio.positionBeat();
        const int starts = device->starts, stops = device->stops;
        double maximumMs = 0;
        int callbacks = 0, overruns = 0;
        std::array<double, 1280> durations{};
        std::array<std::uint64_t, 1280> cycles{};
        const auto beforeGains = audio.mixerGraphInitialisations();
        for (int edit = 0; edit < 160; ++edit)
        {
            send(53, edit % 128);
            send(21, 127 - edit % 128);
            timerCallback();
            if (edit % 32 == 16)
            {
                performAction("edit.undo");
                performAction("edit.redo");
                timerCallback();
            }
            for (int tick = 0; tick < 8; ++tick)
            {
#if JUCE_WINDOWS
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                const bool cycleClock = QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore) != 0;
#endif
                const auto start = juce::Time::getMillisecondCounterHiRes();
                device->process(buffer);
                const double elapsed = juce::Time::getMillisecondCounterHiRes() - start;
#if JUCE_WINDOWS
                if (cycleClock && QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) &&
                    cyclesAfter >= cyclesBefore)
                    cycles[size_t(callbacks)] = cyclesAfter - cyclesBefore;
#endif
                durations[size_t(callbacks)] = elapsed;
                maximumMs = std::max(maximumMs, elapsed);
                if (elapsed >= 256000. / 48000.)
                    ++overruns;
                ++callbacks;
            }
        }
        const auto after = audio.projectionDiagnostics();
        const bool retainedGains = beforeGains == audio.mixerGraphInitialisations();
        // All observation/output work is outside the measured callback. Cycle counts are raw CPU-clock
        // evidence, not a conversion to milliseconds or an explanation of a scheduling outlier.
        juce::String samples = "callback,wallMs,threadCycles,deadlineOverrun\n";
        for (int i = 0; i < callbacks; ++i)
            samples += juce::String(i) + "," + juce::String(durations[size_t(i)], 6) + "," +
                       juce::String(juce::int64(cycles[size_t(i)])) + "," +
                       juce::String(int(durations[size_t(i)] >= 256000. / 48000.)) + "\n";
        folder.getChildFile(pattern ? "controller-pattern-callbacks.csv" : "controller-song-callbacks.csv")
            .replaceWithText(samples);
        const juce::String invariant =
            (pattern ? "Pattern" : "Song") + juce::String(" callbacks=") + juce::String(callbacks) +
            " overruns=" + juce::String(overruns) + " maxMs=" + juce::String(maximumMs, 6) +
            " playing=" + juce::String(int(audio.playing())) +
            " position=" + juce::String(audio.positionBeat(), 6) +
            " baselinePosition=" + juce::String(position, 6) +
            " retainedDevice=" + juce::String(int(starts == device->starts && stops == device->stops)) +
            " retainedEdit=" + juce::String(int(before.edit == after.edit)) +
            " retainedContext=" + juce::String(int(before.playbackContext == after.playbackContext)) +
            " retainedReplacementCount=" + juce::String(int(before.replacements == after.replacements)) +
            " retainedGainStages=" + juce::String(int(retainedGains)) + "\n";
        folder.getChildFile(pattern ? "controller-pattern-invariants.txt" : "controller-song-invariants.txt")
            .replaceWithText(invariant);
        if (!audio.playing() || audio.positionBeat() < position || starts != device->starts ||
            stops != device->stops || before.edit != after.edit ||
            before.playbackContext != after.playbackContext || before.replacements != after.replacements ||
            !retainedGains || overruns)
            return fail("Controller live playback lifecycle/deadline invariant failed: " + invariant);
        for (size_t i = 0; i < before.channels.size(); ++i)
            if (before.channels[i].graphInitialisations != after.channels[i].graphInitialisations)
                return fail("Controller edit rebuilt sample graph");
        evidence +=
            (pattern ? "Pattern" : "Song") + juce::String(": ") + juce::String(callbacks) +
            " virtual callbacks,320 fader/pan events plus Undo/Redo; maxMs=" + juce::String(maximumMs, 4) +
            "; zero overruns, retained transport/Edit/context/device/start/stop/sample graphs.\n";
        send(116, 127);
        send(116, 0);
        dispatchMessages(10);
        if (audio.playing())
            return fail("Mapped Stop did not stop transport");
    }
    SettingsContent settings(*this);
    settings.setSize(840, 920);
    settings.setCurrentTabIndex(7);
    auto image = settings.createComponentSnapshot(settings.getLocalBounds());
    auto output = folder.getChildFile("controllers-settings.png").createOutputStream();
    if (!output)
        return fail("Controller Settings output unavailable");
    output->setPosition(0);
    output->truncate();
    if (!juce::PNGImageFormat{}.writeImageToStream(image, *output))
        return fail("Controller Settings PNG failed");
    const auto savedProfile = midiController.state();
    const auto profileFile = folder.getChildFile("controller-profile.json");
    if (midiController.save(profileFile).failed() ||
        midiController.importState(juce::JSON::parse(profileFile)).failed() ||
        juce::JSON::toString(savedProfile) != juce::JSON::toString(midiController.state()))
        return fail("File controller profile round trip");
    if (auto result = verifySamplerControllerWorkflow(
            folder.getChildFile("sampler-controller-" + juce::Uuid().toString()));
        result.failed())
        return result;
    if (auto result = test::sampler_voice_controller::verify(
            *this, folder.getChildFile("sampler-voice-controller-" + juce::Uuid().toString()));
        result.failed())
        return result;
    folder.getChildFile("controller-verification.txt")
        .replaceWithText("PASS\n" + evidence +
                         "Actual native application command dispatcher, MIDI inbox injection and settings "
                         "component rendering; pickup/crossing, Undo/Redo, "
                         "Learn without action, final insert/Master banking and mapped Stop. No physical "
                         "Launchkey gesture, MIDI driver, hardware or listening qualification.\n");
    return juce::Result::ok();
}
#include "StudioSamplerController.inc"
#include "../tests/SamplerControllerUiAcceptance.inc"
} // namespace vibera
