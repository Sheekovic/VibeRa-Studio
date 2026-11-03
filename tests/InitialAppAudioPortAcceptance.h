// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppSourceTransportAcceptance.h"
#include <thread>
namespace initial_app_ports_acceptance
{
inline void hookContract()
{
    using namespace vibera;
    auto audio = std::make_unique<AudioEngine>(false);
    te::Edit::Options options{audio->engine, te::createEmptyEdit(audio->engine), {}};
    options.role = te::Edit::forRendering;
    auto ordinary = te::Edit::createEdit(options);
    require(bool(ordinary), "Actual default native Edit");
    tracktion::graph::PlayHead head;
    tracktion::graph::PlayHeadState headState(head);
    te::ProcessState process(headState, ordinary->tempoSequence);
    te::CreateNodeParams params{process};
    bool defaultFailed = false;
    std::thread defaultThread(
        [&]
        {
            try
            {
                ordinary->configureNodeGraphForPreparation(params);
            }
            catch (...)
            {
                defaultFailed = true;
            }
        });
    defaultThread.join();
    require(!defaultFailed, "Default-empty hook does not impose a new message-thread requirement");
    struct State
    {
        int calls = 0;
        te::Edit *edit = nullptr;
        te::ProcessState *process = nullptr;
    };
    auto state = std::make_shared<State>();
    std::weak_ptr<State> weak = state;
    options.editState = te::createEmptyEdit(audio->engine);
    options.configureNodeGraphForPreparation = [weak](te::Edit &edit, te::CreateNodeParams &actual)
    {
        const auto owned = weak.lock();
        if (!owned)
            throw std::invalid_argument("Original hook owner gone");
        ++owned->calls;
        owned->edit = &edit;
        owned->process = &actual.processState;
    };
    auto bound = te::Edit::createEdit(options);
    require(bool(bound), "Actual owning Options callback copied into Edit");
    const auto before = bound->state.createCopy();
    bound->configureNodeGraphForPreparation(params);
    require(state->calls == 1 && state->edit == bound.get() && state->process == &process &&
                bound->state.isEquivalentTo(before),
            "Exact CONTROL Edit/params passed without serialization/ValueTree mutation");
    bool refused = false;
    std::thread background(
        [&]
        {
            try
            {
                bound->configureNodeGraphForPreparation(params);
            }
            catch (const std::invalid_argument &e)
            {
                refused = juce::String(e.what()).startsWith("edit.graphPreparation.control:");
            }
        });
    background.join();
    require(refused && state->calls == 1, "Configured hook refuses before invocation on foreign thread");
    state.reset();
    require(weak.expired(), "Weak callback does not extend app policy ownership");
    refused = false;
    try
    {
        bound->configureNodeGraphForPreparation(params);
    }
    catch (const std::invalid_argument &e)
    {
        refused = juce::String(e.what()) == "Original hook owner gone";
    }
    require(refused && bound->state.isEquivalentTo(before),
            "Expired explicit owner refuses without a replacement graph/state mutation");
    bound.reset();
    ordinary.reset(); // Native owners end before Engine; no PluginPtr callback cycle.
}
inline float sampleFrame(int channel, int frame) noexcept
{
    return channel == 0 ? .125f + (frame % 53 == 3 ? .25f : 0.f) : -.0625f - (frame % 47 == 17 ? .125f : 0.f);
}
inline void sampleCase(const juce::File &folder, bool patternMode)
{
    using namespace vibera;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual app sample-port attempt");
    initial_app_source_acceptance::Storage storage(folder.getChildFile("settings"));
    const auto sampleFile = folder.getChildFile("original-independent-stereo.wav");
    {
        juce::AudioBuffer<float> pcm(2, 65536);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < pcm.getNumSamples(); ++n)
                pcm.setSample(c, n, sampleFrame(c, n));
        auto stream = sampleFile.createOutputStream();
        require(bool(stream), "Original owned sample output");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()) && writer->flush(),
                "Original binary-fraction sample written/flushed, no copied assets");
    }
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
    auto &channel = project.channels.front();
    channel.instrument.reset();
    channel.audioClip = false;
    channel.source = sampleFile.getFullPathName();
    channel.gain = 1;
    channel.pan = 0;
    channel.mixer = 1;
    channel.sampler = {};
    channel.sampler.root = 60;
    channel.sampler.envelope = true;
    channel.sampler.attack = channel.sampler.decay = channel.sampler.release = 0;
    channel.sampler.sustain = 1;
    for (auto &insert : project.mixer)
    {
        insert.gain = 1;
        insert.pan = 0;
        insert.effects.clear();
    }
    project.patterns.resize(1);
    auto &pattern = project.patterns.front();
    pattern.length = 64;
    pattern.notes.clear();
    const auto channelId = channel.id, patternId = pattern.id;
    project.placements = {{project.allocate(), patternId, 0, 0, 64, 0}};
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    auto &manager = audio->engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(device && device->getCurrentBufferSizeSamples() == 256,
            "Actual unchanged256-frame sample provider");
    AudioEngine::InitialSourceTransportOptions options;
    options.quantum = 256;
    options.explicitProviderFrames = 256;
    options.sessionGeneration = patternMode ? 301 : 303;
    options.membershipGeneration = patternMode ? 307 : 309;
    options.audioOnlyMixerPorts = true;
    require(audio->prepareInitialSourceTransport(project, options).status ==
                    AudioEngine::InitialSourceTransportStatus::configured &&
                awaitProjection(*audio, project, patternMode, patternId).wasOk(),
            "Actual initial sample+18-strip projection");
    const auto baseline = audio->projectionDiagnostics();
    const int starts = device->starts, stops = device->stops;
    std::array<juce::AudioBuffer<float>, 4> buffers;
    const std::array<int, 4> lengths{64, 65, 127, 256};
    for (std::size_t i = 0; i < lengths.size(); ++i)
        buffers[i].setSize(2, lengths[i]);
    juce::AudioBuffer<float> actual(2, 16384), expected(2, 16384);
    actual.clear();
    expected.clear();
    int framesWritten = 0, physicalOn = -1, typingOn = -1, physicalOff = -1, typingOff = -1;
    juce::String csv = "phase,start,frames,error,ms,new,delete,sourceCalls,held,serial,portBlocks,"
                       "nonemptyMidi,terminalFault\n";
    std::exception_ptr first;
    try
    {
        audio->withInitialSourceTransportContext(
            [&](te::Edit &edit, te::EditPlaybackContext &context)
            {
                require(edit.closedNativeIO && !context.nativeIOAttachmentRefused(),
                        "Closed IO proved, no false MIDI properties");
                auto *head = context.getNodePlayHead();
                require(head != nullptr, "Actual sample native playhead");
                audio->setMidiTarget(channelId);
                audio->setLooping(false);
                audio->playStop();
                dispatchMessages(20);
                const auto origin = head->getReferenceSampleRange().getEnd();
                const auto drive = [&](const char *phase, int count)
                {
                    juce::AudioBuffer<float> *block = nullptr;
                    for (std::size_t i = 0; i < lengths.size(); ++i)
                        if (count == lengths[i])
                            block = &buffers[i];
                    require(block && framesWritten + count <= actual.getNumSamples(),
                            "Preallocated actual sample callback span");
                    const auto before = audio->projectionDiagnostics();
                    const auto allocations = callbackAllocations.load(),
                               deletions = callbackDestructions.load();
                    const auto start = juce::Time::getHighResolutionTicks();
                    {
                        struct Probe
                        {
                            bool previous = callbackAllocationProbe;
                            Probe()
                            {
                                callbackAllocationProbe = true;
                            }
                            ~Probe()
                            {
                                callbackAllocationProbe = previous;
                            }
                        } probe;
                        device->process(*block);
                    }
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
                    double error = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < count; ++n)
                        {
                            const int t = framesWritten + n;
                            float wanted = 0;
                            if (physicalOn >= 0 && t >= physicalOn && (physicalOff < 0 || t < physicalOff))
                                wanted += sampleFrame(c, t - physicalOn);
                            if (typingOn >= 0 && t >= typingOn && (typingOff < 0 || t < typingOff))
                                wanted += sampleFrame(c, t - typingOn);
                            const float value = block->getSample(c, n);
                            actual.setSample(c, t, value);
                            expected.setSample(c, t, wanted);
                            error = std::isfinite(value) ? std::max(error, std::abs(double(value) - wanted))
                                                         : std::numeric_limits<double>::infinity();
                        }
                    const auto after = audio->projectionDiagnostics();
                    const auto ports = audio->initialAudioPortReadback();
                    const auto source = audio->initialSourceTransportReadback();
                    const auto range = head->getReferenceSampleRange();
                    csv += juce::String(phase) + "," + juce::String(framesWritten) + "," +
                           juce::String(count) + "," + juce::String(error, 12) + "," + juce::String(ms, 6) +
                           "," + juce::String(callbackAllocations.load() - allocations) + "," +
                           juce::String(callbackDestructions.load() - deletions) + "," +
                           juce::String(
                               juce::int64(after.channels[0].renderCalls - before.channels[0].renderCalls)) +
                           "," + juce::String(after.channels[0].heldInputKeys) + "," +
                           juce::String(juce::int64(source.native.physicalSerial)) + "," +
                           juce::String(juce::int64(ports.audioBlocks)) + "," +
                           juce::String(juce::int64(ports.nonemptyAudioMidi)) + "," +
                           juce::String(int(ports.terminalFault)) + "\n";
                    framesWritten += count;
                    require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                            "Every actual sample callback retained before oracle");
                    require(
                        error == 0 && callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == deletions && ms <= 1000. * count / 48000. &&
                            range.getEnd() == origin + framesWritten &&
                            range.getStart() == origin + framesWritten - count &&
                            before.edit == baseline.edit && after.edit == baseline.edit &&
                            before.playbackContext == after.playbackContext &&
                            after.playbackContext == baseline.playbackContext &&
                            after.channels[0].renderCalls == before.channels[0].renderCalls + 1 &&
                            after.headPlaying && after.transportPlaying && ports.configured &&
                            ports.tracks == 18 && ports.audioBlocks > 0 && ports.nonemptyAudioMidi == 0 &&
                            !ports.terminalFault &&
                            source.status == AudioEngine::InitialSourceTransportStatus::ready &&
                            source.pairedClosed && source.memberCount == 0 &&
                            source.native.sourceQuota == count && source.native.sourceProcessed == count &&
                            device->starts == starts && device->stops == stops,
                        "Actual sample full PCM/reference/held-source/ports/deadline/heap/lifecycle oracle");
                    dispatchMessages(2);
                };
                for (int i = 0; i < 4; ++i)
                    drive("startup_all_frames", 256);
                audio->midiInput.capture.begin();
                audio->typingInput.capture.begin();
                physicalOn = framesWritten;
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("physical_on", 127);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Actual sample physical lane owner");
                typingOn = framesWritten;
                audio->typingInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("typing_on_distinct_voice_phase", 64);
                drive("held_partial", 65);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 2,
                        "Two actual sample input bank owners");
                privateGraph(*audio, edit, context, drive);
                CommandArgs note;
                note.channel = channelId;
                note.pattern = patternId;
                note.beat = 32;
                note.length = 1;
                note.pitch = 72;
                note.value = .5;
                require(controller.execute("notes.add", note).wasOk() &&
                            audio->rebuild(project, patternMode, patternId).wasOk(),
                        "Actual canonical future score update under ports");
                drive("immutable_note_update", 256);
                require(controller.undoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, patternId).wasOk(),
                        "Actual canonical Undo under retained ports");
                drive("note_undo", 256);
                require(controller.redoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, patternId).wasOk(),
                        "Actual canonical Redo under retained ports");
                drive("note_redo", 256);
                physicalOff = framesWritten;
                audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("physical_off_typing_continues", 127);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Typing voice retained after physical release");
                typingOff = framesWritten;
                audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("typing_off", 65);
                for (int i = 0; i < 3; ++i)
                    drive("complete_release_tail", 256);
                captured(audio->midiInput, channelId);
                captured(audio->typingInput, channelId);
            });
    }
    catch (...)
    {
        first = std::current_exception();
    }
    for (const auto &[name, pcm] :
         {std::pair{"actual-planar-f32.raw", &actual}, std::pair{"expected-planar-f32.raw", &expected}})
    {
        auto output = folder.getChildFile(name).createOutputStream();
        require(output && output->setPosition(0) && output->truncate().wasOk(),
                "Owned complete sample raw evidence");
        for (int c = 0; c < 2; ++c)
            require(output->write(pcm->getReadPointer(c), std::size_t(framesWritten) * sizeof(float)),
                    "Complete sample PCM retained");
        output->flush();
        require(output->getStatus().wasOk(), "Actual sample raw output flush/status");
    }
    if (first)
        std::rethrow_exception(first);
}
} // namespace initial_app_ports_acceptance

static void initialAppAudioPortTests(const juce::File &folder, const vibera::PluginSpec &instrument,
                                     const vibera::PluginSpec &effect)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual app audio-port evidence");
    require(instrument.instrument && instrument.name == "VibeRa Instrument Runtime Fixture" &&
                !effect.instrument && effect.name == "VibeRa Parameter Fixture",
            "Exact original SDK inputs, no arbitrary-provider DSP claim");
    auto captured = initial_app_source_acceptance::capturedOriginal(instrument);
    initial_app_ports_acceptance::hookContract();
    std::exception_ptr first;
    juce::String cases;
    const auto run = [&](const juce::String &name, auto &&body)
    {
        try
        {
            body();
            cases += name + "=completed\n";
        }
        catch (const std::exception &error)
        {
            if (!first)
                first = std::current_exception();
            cases += name + "=failed:" + juce::String(error.what()) + "\n";
        }
        require(folder.getChildFile("cases.txt").replaceWithText(cases),
                "Each independent app port result retained");
    };
    for (bool pattern : {true, false})
    {
        const auto mode = pattern ? juce::String("pattern") : juce::String("song");
        run("sample-" + mode, [&]
            { initial_app_ports_acceptance::sampleCase(folder.getChildFile("sample-" + mode), pattern); });
        for (int bound : {256, 512})
        {
            const auto name = "sdk-" + mode + "-bound" + juce::String(bound);
            run(name,
                [&]
                {
                    initial_app_source_acceptance::run(folder.getChildFile(name), captured, effect, pattern,
                                                       bound, true);
                });
        }
    }
    if (first)
        std::rethrow_exception(first);
}
