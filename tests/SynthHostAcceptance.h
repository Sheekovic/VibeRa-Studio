// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "synth/SynthRegistry.h"
#include "synth/SynthState.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

// Include after Acceptance.cpp's actual MIDI/solo helpers, readWav, virtual
// device and callback probes. These are native worker/host tests, not a scan of
// an external VST3 and not evidence of physical MIDI, hardware or listening.
static void synthHostAcceptance(const juce::File &folder)
{
    const auto began = std::chrono::steady_clock::now();
    auto buildRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    while (buildRoot != buildRoot.getParentDirectory() &&
           !(buildRoot.getFileName() == "windows" && buildRoot.getParentDirectory().getFileName() == "build"))
        buildRoot = buildRoot.getParentDirectory();
    require(buildRoot.getFileName() == "windows" && folder.isAChildOf(buildRoot) &&
                folder.createDirectory().wasOk(),
            "Native Synth host acceptance requires an owned build/windows output child");
    juce::StringArray checks;
    juce::Array<juce::var> observations, performance;
    juce::String phase = "started";
    const auto receipt = folder.getChildFile("synth-host-verification.json");
    const auto elapsed = [&]
    { return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); };
    const auto writeReceipt = [&](bool passed, const juce::String &error = {})
    {
        auto result = juce::var(new juce::DynamicObject);
        auto *object = result.getDynamicObject();
        object->setProperty("passed", passed);
        object->setProperty("phase", phase);
        object->setProperty("checks", checks);
        object->setProperty("observations", observations);
        object->setProperty("performance", performance);
        object->setProperty("seconds", elapsed());
        object->setProperty("virtualSampleRate", 48000);
        object->setProperty("outerFrames", 256);
        object->setProperty("physicalMidi", false);
        object->setProperty("hardware", false);
        object->setProperty("listening", false);
        object->setProperty("scope", "Actual built-in Synth worker, prepared host/virtual-device MIDI, "
                                     "typing, preview, Pattern/Song edits, native state and stereo exports. "
                                     "CONTROL telemetry is eventual, not packet-timestamp coherent.");
        if (error.isNotEmpty())
            object->setProperty("error", error);
        require(receipt.replaceWithText(juce::JSON::toString(result)), "Retain native Synth host receipt");
    };
    const auto check = [&](bool value, const char *name)
    {
        require(value, name);
        checks.add(name);
    };
    const auto stage = [&](const juce::String &name)
    {
        phase = name;
        writeReceipt(false);
        require(elapsed() < 160, "Native Synth host acceptance retains a bounded execution budget");
    };
    const auto specFor = [&](const char *presetId)
    {
        const auto *preset = synth::findFactoryPreset(presetId);
        require(preset != nullptr, "Native Synth host fixture uses an actual factory identity");
        auto native = preset->values;
        const auto put = [&](synth::ParameterId id, float value)
        { native[synth::parameterIndex(id)] = value; };
        for (const auto id : {synth::ParameterId::eqEnabled, synth::ParameterId::compressorEnabled,
                              synth::ParameterId::chorusEnabled, synth::ParameterId::delayEnabled,
                              synth::ParameterId::reverbEnabled})
            put(id, 0);
        put(synth::ParameterId::attackMs, 1);
        put(synth::ParameterId::decayMs, 1);
        put(synth::ParameterId::sustain, 1);
        put(synth::ParameterId::releaseMs, 5);
        put(synth::ParameterId::filterType, 3);
        put(synth::ParameterId::warmth, 0);
        put(synth::ParameterId::space, 0);
        put(synth::ParameterId::motion, 0);
        put(synth::ParameterId::pitchBend, 0);
        put(synth::ParameterId::modWheel, 0);
        put(synth::ParameterId::vibratoDepthCents, 0);
        put(synth::ParameterId::detuneCents, 0);
        put(synth::ParameterId::oscillatorType, 0);
        put(synth::ParameterId::oscillatorBlend, 0);
        put(synth::ParameterId::masterDb, -6);
        synth::Values values{};
        for (const auto &parameter : synth::parameterSpecs())
        {
            const auto range = synth::synthParameterRange(parameter);
            const auto index = synth::parameterIndex(parameter.id);
            values[index] = range.convertTo0to1(range.snapToLegalValue(native[index]));
        }
        const auto state = synth::makeSynthNativeState(values);
        synth::Values validated{};
        require(synth::parseSynthNativeState(state.getData(), int(state.getSize()), validated).wasOk() &&
                    validated == values,
                "Native Synth host fixture is complete strict native state");
        const auto description = synth::nativeDescription();
        return PluginSpec{1, description.name, description.createXml()->toString(), state.toBase64Encoding(),
                          true};
    };
    writeReceipt(false);
    try
    {
        const auto dry = specFor("pop-round"), piano = specFor("pop-velvet-upright");
        stage("existing-native-midi-typing");
        const auto inputFolder = folder.getChildFile("native-midi-typing");
        require(inputFolder.createDirectory().wasOk(), "Native input helper output");
        midiInputTests(inputFolder, &dry);
        check(
            true,
            "Existing actual host MIDI/typing stopped/playing and shared-key workflow accepts native Synth");
        stage("existing-native-pattern-song-edits");
        const auto soloFolder = folder.getChildFile("native-pattern-song-edits");
        require(soloFolder.createDirectory().wasOk(), "Native ordinary-edit helper output");
        channelSoloLiveTests(soloFolder, &dry);
        check(true,
              "Existing Pattern/Song mute/solo/Undo/Redo retains native Synth worker and held input PCM");

        const auto focused = [&](const PluginSpec &original, const PluginSpec &replacement, const char *name)
        {
            for (const bool patternMode : {true, false})
            {
                stage(juce::String(name) +
                      (patternMode ? "-pattern-input-lifecycle" : "-song-input-lifecycle"));
                auto audioOwner = std::make_unique<AudioEngine>(false, juce::File{}, 256);
                auto &audio = *audioOwner;
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
                require(device != nullptr, "Focused native Synth virtual device");
                auto project = Project::empty();
                project.channels.resize(1);
                auto &channel = project.channels.front();
                channel.source = "builtin:bass";
                channel.gain = 1;
                channel.instrument = original;
                channel.instrument->id = project.allocate();
                const auto channelId = channel.id;
                const auto patternId = project.patterns.front().id;
                const auto originalPluginId = channel.instrument->id;
                project.mixer.front().gain = 1;
                project.patterns.front().length = 64;
                project.patterns.front().autoLength = false;
                project.patterns.front().notes.clear();
                project.placements = {{project.allocate(), patternId, 0, 0, 64, 0}};
                audio.setMetronome(false);
                audio.setCountInBars(0);
                require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                        "Focused native Synth host prepares from its original descriptor/state");
                audio.setMidiTarget(channelId);
                audio.setLooping(true);
                auto host = audio.preparedPluginHost(project, originalPluginId);
                require(host && host->alive(), "Focused native Synth has a real owned worker");
                const auto originalWorker = host->processId();
                juce::AudioBuffer<float> block(2, 256);
                std::array<double, 4096> timing{};
                std::size_t timed = 0;
                int callbackCount = 0;
                float lastPeak = 0;
                const auto allocations = callbackAllocations.load(),
                           destructions = callbackDestructions.load();
                const auto tick = [&](int count)
                {
                    float maximum = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        const auto start = juce::Time::getHighResolutionTicks();
                        {
                            const juce::ScopedValueSetter<bool> guard(callbackAllocationProbe, true);
                            device->process(block);
                        }
                        const auto ms = 1000 * juce::Time::highResolutionTicksToSeconds(
                                                   juce::Time::getHighResolutionTicks() - start);
                        require(timed < timing.size(), "Focused native Synth callback timing capacity");
                        timing[timed++] = ms;
                        ++callbackCount;
                        for (int c = 0; c < block.getNumChannels(); ++c)
                            for (int frame = 0; frame < block.getNumSamples(); ++frame)
                                require(std::isfinite(block.getSample(c, frame)),
                                        "Native Synth host callback PCM remains finite");
                        lastPeak = block.getMagnitude(0, block.getNumSamples());
                        maximum = std::max(maximum, lastPeak);
                        dispatchMessages(6);
                    }
                    require(elapsed() < 160, "Focused native Synth lifecycle remains bounded");
                    return maximum;
                };
                const auto voices = [&]
                {
                    if (!host)
                        return -1;
                    const auto meter = host->nativeOutputMeter();
                    return meter.status == PluginNativeMeterStatus::ready && (meter.synthState >> 63) != 0
                               ? int(meter.synthState & 255)
                               : -1;
                };
                const auto waitFor = [&](int expectedVoices, bool audible, const char *caseName)
                {
                    bool matched = false;
                    for (int i = 0; i < 120; ++i)
                    {
                        tick(1);
                        if (i >= 12 && voices() == expectedVoices &&
                            (audible ? lastPeak > 1e-5f : lastPeak < 1e-8f))
                        {
                            matched = true;
                            break;
                        }
                    }
                    require(matched, std::string(caseName) + "; voices=" + std::to_string(voices()) +
                                         "; last peak=" + std::to_string(lastPeak));
                    checks.add(juce::String(name) + (patternMode ? " Pattern: " : " Song: ") + caseName);
                    auto row = juce::var(new juce::DynamicObject);
                    row.getDynamicObject()->setProperty("case", caseName);
                    row.getDynamicObject()->setProperty("fixture", name);
                    row.getDynamicObject()->setProperty("mode", patternMode ? "Pattern" : "Song");
                    row.getDynamicObject()->setProperty("voices", voices());
                    row.getDynamicObject()->setProperty("lastBlockPeak", lastPeak);
                    row.getDynamicObject()->setProperty("callbacks", callbackCount);
                    row.getDynamicObject()->setProperty("worker", int(host->processId()));
                    observations.add(row);
                };
                const auto physical = [&](const juce::MidiMessage &message)
                { audio.midiInput.handleIncomingMidiMessage(nullptr, message); };
                const auto typing = [&](bool on)
                {
                    audio.typingInput.handleIncomingMidiMessage(
                        nullptr, on ? juce::MidiMessage::noteOn(1, 60, juce::uint8(100))
                                    : juce::MidiMessage::noteOff(1, 60));
                };
                waitFor(0, false, "prepared native host is silent with no notes");
                const auto ready = host->nativeOutputMeter().synthState;
                juce::MemoryBlock initialState;
                synth::Values initialValues{};
                require(decodePluginState(original.state, initialState) &&
                            synth::parseSynthNativeState(initialState.getData(), int(initialState.getSize()),
                                                         initialValues)
                                .wasOk(),
                        "Original native source identity remains strictly validated");
                const auto actual = [&](synth::ParameterId id)
                {
                    const auto range = synth::synthParameterRange(synth::parameterSpec(id));
                    return int(std::lround(range.snapToLegalValue(
                        range.convertFrom0to1(initialValues[synth::parameterIndex(id)]))));
                };
                check((ready & (std::uint64_t(1) << 32)) != 0 &&
                          int((ready >> 24) & 3u) == actual(synth::ParameterId::sourceType) &&
                          int((ready >> 8) & 65535u) == actual(synth::ParameterId::factoryPreset) &&
                          bool(ready & (std::uint64_t(1) << 33)) ==
                              (actual(synth::ParameterId::sourceType) != 0),
                      "Actual native worker reports the exact requested source/bank and real sample "
                      "availability");
                physical(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
                waitFor(1, true, "stopped physical-route note produces actual native PCM");
                physical(juce::MidiMessage::controllerEvent(1, 64, 127));
                physical(juce::MidiMessage::noteOff(1, 60));
                waitFor(1, true, "actual native sustain pedal holds a released physical-route note");
                physical(juce::MidiMessage::controllerEvent(1, 64, 0));
                waitFor(0, false,
                        "actual native pedal release finishes the short envelope without a stuck voice");
                check(audio.previewChannelNote(channelId, 60, true, 100),
                      "Prepared native instrument admits stopped preview through the existing host lane");
                waitFor(1, true, "stopped preview reaches actual native DSP");
                physical(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
                tick(16);
                check(audio.previewChannelNote(channelId, 60, false), "Native preview note-off is admitted");
                waitFor(1, true, "preview release preserves a same-pitch physical-route owner");
                physical(juce::MidiMessage::noteOff(1, 60));
                waitFor(0, false, "final physical-route owner releases the shared native note");
                typing(true);
                waitFor(1, true, "stopped typing-route note reaches the real native worker");
                check(audio.previewChannelNote(channelId, 60, true, 100), "Shared typed preview is admitted");
                tick(16);
                audio.stopChannelPreview(channelId);
                waitFor(1, true, "preview panic preserves a same-pitch typing owner");
                typing(false);
                waitFor(0, false, "final typing owner releases after preview panic");
                project.patterns.front().notes = {{project.allocate(), channelId, 0, 64, 60, .8f, false}};
                require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                        "Native score prepares");
                audio.seekBeat(0);
                audio.playStop();
                waitFor(1, true, "actual native Pattern or Song score starts its sustained gate");
                typing(true);
                check(audio.previewChannelNote(channelId, 60, true, 100),
                      "Playing native preview is admitted");
                tick(16);
                audio.stopChannelPreview(channelId);
                waitFor(1, true, "playing preview panic preserves shared score and typing owners");
                project.channels.front().muted = true;
                require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                        "Native score mute publishes");
                waitFor(1, true, "score mute releases only score ownership while typed native PCM continues");
                audio.stop();
                waitFor(1, true, "transport stop preserves intentionally held live typing monitoring");
                typing(false);
                waitFor(0, false, "final live release leaves stopped muted native channel silent");
                project.channels.front().muted = false;
                require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                        "Native score unmute publishes");
                audio.seekBeat(0);
                audio.playStop();
                waitFor(1, true, "native score retriggers normally after unmute");
                audio.stop();
                waitFor(0, false, "transport stop releases the score-only native gate");
                physical(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
                waitFor(1, true, "native note precedes explicit physical-route panic");
                audio.midiInput.panic();
                waitFor(0, false, "explicit physical-route panic clears actual native held notes");
                check(audio.previewChannelNote(channelId, 60, true, 100), "Final native preview is admitted");
                waitFor(1, true, "native preview note precedes its own scoped panic");
                audio.stopChannelPreview(channelId);
                waitFor(0, false, "scoped preview panic leaves no native voice or residual dry PCM");
                check(host->processId() == originalWorker && host->alive() && !host->deliveryFailed(),
                      "Ordinary native MIDI/preview/score edits preserve the exact healthy worker");
                if (patternMode)
                {
                    project.patterns.front().notes.clear();
                    require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                            "Clear stopped native score");
                    physical(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
                    waitFor(1, true, "native note is actually held before instrument removal");
                    host.reset(); // CONTROL retirement; never release the last worker handle in DSP.
                    project.channels.front().instrument.reset();
                    require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                            "Remove held native instrument");
                    tick(32);
                    check(lastPeak < 1e-8f && !audio.preparedPluginHost(project, originalPluginId),
                          "Removing a held native instrument retires its audible route without a stuck note");
                    project.channels.front().instrument = replacement;
                    project.channels.front().instrument->id = project.allocate();
                    require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                            "Replace native instrument");
                    audio.setMidiTarget(channelId);
                    host = audio.preparedPluginHost(project, project.channels.front().instrument->id);
                    check(host && host->alive() && host->processId() != originalWorker,
                          "Replacement owns a new actual native worker rather than the removed instance");
                    waitFor(0, false, "replacement native bank inherits no stale held-input voice");
                    physical(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
                    waitFor(1, true, "replacement native bank receives fresh real input PCM");
                    physical(juce::MidiMessage::noteOff(1, 60));
                    waitFor(0, false, "replacement native bank releases normally");
                }
                check(callbackAllocations.load() == allocations &&
                          callbackDestructions.load() == destructions,
                      "Focused native host callbacks allocate and destroy no instrumented C++ heap objects");
                std::sort(timing.begin(), timing.begin() + std::ptrdiff_t(timed));
                auto row = juce::var(new juce::DynamicObject);
                auto *result = row.getDynamicObject();
                result->setProperty("fixture", name);
                result->setProperty("mode", patternMode ? "Pattern" : "Song");
                result->setProperty("callbacks", int(timed));
                result->setProperty("medianMs", timing[timed / 2]);
                result->setProperty("p99Ms", timing[(timed - 1) * 99 / 100]);
                result->setProperty("maximumMs", timing[timed - 1]);
                result->setProperty("wholeCallbackDeadlineMs", 256000. / 48000.);
                result->setProperty("deadlineMisses",
                                    int(std::count_if(timing.begin(), timing.begin() + std::ptrdiff_t(timed),
                                                      [](double ms) { return ms > 256000. / 48000.; })));
                performance.add(row);
                check(
                    timing[(timed - 1) * 99 / 100] < 256000. / 48000.,
                    "Focused native host whole256-frame callback p99 fits the actual virtual-device period");
            }
        };
        focused(dry, piano, "dry-synthesis");
        focused(piano, dry, "recorded-piano");

        stage("two-native-instruments-routing-capture");
        Project captured;
        const auto projectFile = folder.getChildFile("native-two-instruments.vibra");
        auto multi = Project::empty();
        multi.channels.resize(2);
        multi.mixer.front().gain = 1;
        multi.patterns.front().length = 4;
        multi.patterns.front().autoLength = false;
        multi.patterns.front().notes.clear();
        for (int index = 0; index < 2; ++index)
        {
            auto &channel = multi.channels[std::size_t(index)];
            channel.source = "builtin:bass";
            channel.instrument = index == 0 ? dry : piano;
            channel.instrument->id = multi.allocate();
            channel.gain = 1;
            channel.pan = index == 0 ? -1.f : 1.f;
            channel.mixer = index + 1;
            multi.patterns.front().notes.push_back({multi.allocate(), channel.id, 0, 4, 60, .8f, false});
        }
        multi.placements = {{multi.allocate(), multi.patterns.front().id, 0, 0, 4, 0}};
        require(setMixerRoute(multi, 1, 3, true, true).wasOk(),
                "Prepare separate native instrument bus route");
        const auto multiAllocations = callbackAllocations.load(),
                   multiDestructions = callbackDestructions.load();
        {
            auto owner = std::make_unique<AudioEngine>(false, juce::File{}, 256);
            auto &audio = *owner;
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
            require(device && awaitProjection(audio, multi, true, multi.patterns.front().id).wasOk(),
                    "Two native instruments prepare on the real virtual output");
            auto left = audio.preparedPluginHost(multi, multi.channels[0].instrument->id);
            auto right = audio.preparedPluginHost(multi, multi.channels[1].instrument->id);
            check(left && right && left != right && left->alive() && right->alive() &&
                      left->processId() != right->processId(),
                  "Two native Synth instances own independent live worker processes and banks");
            const auto leftWorker = left->processId(), rightWorker = right->processId();
            juce::AudioBuffer<float> block(2, 256);
            const auto tick = [&](int blocks)
            {
                for (int i = 0; i < blocks; ++i)
                {
                    {
                        const juce::ScopedValueSetter<bool> guard(callbackAllocationProbe, true);
                        device->process(block);
                    }
                    dispatchMessages(6);
                }
            };
            audio.setMetronome(false);
            audio.setCountInBars(0);
            audio.setLooping(true);
            audio.playStop();
            tick(40);
            check(block.getMagnitude(0, 0, 256) > 1e-5f && block.getMagnitude(1, 0, 256) > 1e-5f,
                  "Two actual native banks produce independent left/right Pattern score PCM");
            multi.mixer[1].muted = true;
            require(awaitProjection(audio, multi, true, multi.patterns.front().id).wasOk(),
                    "Mute native left bus");
            tick(20);
            check(block.getMagnitude(0, 0, 256) < 1e-8f && block.getMagnitude(1, 0, 256) > 1e-5f,
                  "Muting the routed synthesis bus preserves the independent recorded native instrument");
            multi.mixer[1].muted = false;
            require(awaitProjection(audio, multi, true, multi.patterns.front().id).wasOk(),
                    "Unmute native left bus");
            tick(20);
            check(block.getMagnitude(0, 0, 256) > 1e-5f && left->processId() == leftWorker &&
                      right->processId() == rightWorker,
                  "Native bus unmute restores score PCM while both original workers remain alive");
            audio.stop();
            tick(24);
            check(block.getMagnitude(0, 256) < 1e-8f,
                  "Two native score instruments release on transport stop");
            check(callbackAllocations.load() == multiAllocations &&
                      callbackDestructions.load() == multiDestructions,
                  "Two native instrument callbacks allocate and destroy no instrumented C++ heap objects");
            require(audio.requestPluginState(multi).wasOk(), "Actual two-worker native capture admitted");
            auto captureResult = juce::Result::ok();
            bool ready = false;
            const auto deadline = juce::Time::getMillisecondCounter() + 12000;
            while (!(ready = audio.pollPluginState(captured, captureResult)) &&
                   juce::Time::getMillisecondCounter() < deadline)
                dispatchMessages(3); // CONTROL capture; do not submit audio while workers are paused.
            check(ready && captureResult.wasOk(),
                  "Actual current native states are captured from both workers");
        }
        check(captured.channels.size() == 2 && captured.channels[0].instrument &&
                  captured.channels[1].instrument &&
                  captured.channels[0].instrument->id != captured.channels[1].instrument->id &&
                  saveProject(captured, projectFile).wasOk(),
              "Two native instruments save with distinct persistent identities and captured state");
        Project reopened;
        check(loadProject(projectFile, reopened).wasOk() && reopened.state().isEquivalentTo(captured.state()),
              "Two native instruments and their bus routing reopen with exact captured model state");
        for (const auto &channel : reopened.channels)
        {
            juce::MemoryBlock state;
            synth::Values values{};
            check(decodePluginState(channel.instrument->state, state) &&
                      synth::parseSynthNativeState(state.getData(), int(state.getSize()), values).wasOk(),
                  "Reopened captured native instrument state passes the unchanged strict parser");
        }
        stage("two-native-instruments-offline-export");
        const auto render = [&](const Project &project, const char *name)
        {
            auto owner = std::make_unique<AudioEngine>(false);
            const auto file = folder.getChildFile(name);
            check(owner->renderSync(project, file, 48000, 24).wasOk(),
                  "Actual multi-native stereo WAV export completes");
            double rate = 0;
            auto pcm = readWav(file, rate);
            check(rate == 48000 && pcm.getNumChannels() == 2 && pcm.getNumSamples() == 96000,
                  "Actual multi-native export has exact four-beat stereo48k duration");
            return pcm;
        };
        const auto originalPcm = render(multi, "native-two-original.wav");
        const auto reopenedPcm = render(reopened, "native-two-reopened.wav");
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < reopenedPcm.getNumSamples(); ++frame)
                require(originalPcm.getSample(channel, frame) == reopenedPcm.getSample(channel, frame),
                        "Two-native captured save/reopen export preserves exact actual PCM");
        check(reopenedPcm.getMagnitude(0, 0, 96000) > 1e-5f && reopenedPcm.getMagnitude(1, 0, 96000) > 1e-5f,
              "Reopened multi-native export contains both real instrument sources");
        auto leftMuted = reopened;
        leftMuted.mixer[1].muted = true;
        const auto rightOnly = render(leftMuted, "native-two-recorded-only.wav");
        check(rightOnly.getMagnitude(0, 0, 96000) < 1e-6f && rightOnly.getMagnitude(1, 0, 96000) > 1e-5f,
              "Offline routed synthesis-bus mute isolates the real recorded native instrument");
        auto rightMuted = reopened;
        rightMuted.mixer[2].muted = true;
        const auto leftOnly = render(rightMuted, "native-two-synthesis-only.wav");
        check(leftOnly.getMagnitude(1, 0, 96000) < 1e-6f && leftOnly.getMagnitude(0, 0, 96000) > 1e-5f,
              "Offline recorded-bus mute isolates the independent native synthesis instrument");

        stage("native-parameter-automation-offline-export");
        Controller automation;
        automation.replace(reopened);
        const auto masterId =
            synth::synthParameterBaseId + static_cast<juce::uint32>(synth::ParameterId::masterDb);
        const auto masterRange =
            synth::synthParameterRange(synth::parameterSpec(synth::ParameterId::masterDb));
        const auto loud = masterRange.convertTo0to1(-6.f), quiet = masterRange.convertTo0to1(-18.f);
        CommandArgs create;
        create.target = reopened.channels[0].instrument->id;
        create.length = 4;
        create.lane = 1;
        create.binding = PluginBinding{masterId, 1, 0, loud, "Master", "dB"};
        check(automation.execute("automation.plugin.create", create).wasOk(),
              "Canonical automation command creates the fixed native Synth Master parameter clip");
        auto &source = automation.project.automation.sources.front();
        source.points = {{automation.project.allocate(), 0, loud},
                         {automation.project.allocate(), 2, quiet, AutomationShape::hold},
                         {automation.project.allocate(), 4, quiet, AutomationShape::hold}};
        check(automation.project.validate().wasOk(),
              "Actual native parameter hold curve retains a valid bound project");
        const auto automatedPcm = render(automation.project, "native-two-master-automation.wav");
        const auto earlyRatio =
            automatedPcm.getRMSLevel(0, 12000, 12000) / reopenedPcm.getRMSLevel(0, 12000, 12000);
        const auto lateRatio =
            automatedPcm.getRMSLevel(0, 60000, 12000) / reopenedPcm.getRMSLevel(0, 60000, 12000);
        check(std::isfinite(earlyRatio) && std::isfinite(lateRatio) && std::abs(earlyRatio - 1.f) < .001f &&
                  std::abs(lateRatio - std::pow(10.f, -12.f / 20.f)) < .001f,
              "Actual native parameter event rendering changes only the synthesis gain by exactly12dB");
        for (int frame = 0; frame < automatedPcm.getNumSamples(); ++frame)
            require(automatedPcm.getSample(1, frame) == reopenedPcm.getSample(1, frame),
                    "Automating one native instance preserves the independent recorded instance PCM");
        checks.add("Native Master automation preserves the other native instance's exact exported PCM");
        auto gainObservation = juce::var(new juce::DynamicObject);
        gainObservation.getDynamicObject()->setProperty("case", "native-Master-beat2-hold-transition");
        gainObservation.getDynamicObject()->setProperty("parameterId", int(masterId));
        gainObservation.getDynamicObject()->setProperty("earlyGainRatio", earlyRatio);
        gainObservation.getDynamicObject()->setProperty("lateGainRatio", lateRatio);
        observations.add(gainObservation);
        phase = "complete";
        writeReceipt(true);
    }
    catch (const std::exception &error)
    {
        writeReceipt(false, error.what());
        throw;
    }
}
