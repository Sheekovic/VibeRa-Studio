// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/AudioEngine.h"
#include "InitialAppAudioPortFixture.h"
#include <array>
#include <limits>

// Include after genuine parameter/awaitProjection/SyntheticDevice/heap helpers.
// Actual AudioEngine initial path, not a replacement native Signal or direct
// Host processing fixture. Serialized actual physical callbacks, not hardware.
namespace initial_app_source_acceptance
{
using namespace vibera;
struct Storage final
{
    juce::String previous = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
    explicit Storage(const juce::File &path)
    {
        require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", path.getFullPathName().toWideCharPointer()) ==
                    0,
                "Owned initial app source settings");
    }
    ~Storage()
    {
        _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer());
    }
};
inline PluginSpec capturedOriginal(const PluginSpec &source)
{
    PluginHost seed;
    require(seed.start(source, 48000, 256, true).wasOk(), "Actual original opaque-state seed worker");
    const auto catalogue = parameterCatalogue(seed);
    require(parameterResult(seed, seed.setParameter(77, .25f, catalogue.generation)).result == 0,
            "Actual native seed gain acknowledged, no invented base64 state");
    const auto token = seed.requestState();
    require(token != 0, "Actual opaque seed capture admitted");
    PluginSpec result = source;
    result.state.clear();
    bool captured = false;
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (!(captured = seed.readState(token, result.state)) &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        seed.service();
        dispatchMessages(2);
    }
    require(captured && result.state.isNotEmpty(), "Actual acknowledged opaque state, not metadata presence");
    const auto fresh = parameterCatalogue(seed, true);
    const auto &gain = nativeParameter(fresh, 77);
    result.automationBindings = {{gain.id, gain.flags, gain.steps, .25f, gain.name, gain.label}};
    return result;
}
inline void run(const juce::File &folder, const PluginSpec &stored, const PluginSpec &originalEffect,
                bool patternMode, int providerFrames, bool audioOnlyMixerPorts = false,
                bool activeSourceEffect = false)
{
    using Status = AudioEngine::InitialSourceTransportStatus;
    require(!activeSourceEffect ||
                (audioOnlyMixerPorts && originalEffect.name == "VibeRa Parameter Fixture" &&
                 !originalEffect.instrument && !originalEffect.bypass && originalEffect.wet == 1 &&
                 originalEffect.state.isNotEmpty()),
            "Active effect case requires explicit ports and genuine captured native0 gain fixture");
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual app source attempt");
    Storage storage(folder.getChildFile("settings"));
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
    project.channels[0].instrument = stored;
    project.channels[0].instrument->id = project.allocate();
    project.channels[0].gain = 1;
    project.channels[0].pan = 0;
    project.channels[0].mixer = 1;
    const auto channel = project.channels[0].id;
    const auto instrument = project.channels[0].instrument->id;
    for (auto &insert : project.mixer)
    {
        insert.gain = 1;
        insert.pan = 0;
        insert.effects.clear();
    }
    auto unusedEffect = originalEffect;
    unusedEffect.id = project.allocate();
    project.mixer[activeSourceEffect ? 1 : 2].effects.push_back(unusedEffect);
    // Default remains the original pruned-bank scenario. Only the explicit new
    // active case puts native0 gain.5 on the actual source route before Gate.
    if (activeSourceEffect)
        project.mixer[1].effectsBypassed = false;
    project.patterns.front().length = 64;
    project.patterns.front().autoLength = false; // Fixed held-input observation window.
    project.patterns.front().notes.clear();
    const auto pattern = project.patterns.front().id;
    project.placements = {{project.allocate(), pattern, 0, 0, 64, 0}};
    // Exact saved state is round-tripped BEFORE private worker construction.
    const auto file = folder.getChildFile("initial-state.vibra");
    Project restored;
    require(saveProject(project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.state().isEquivalentTo(project.state()) &&
                restored.plugin(instrument)->state == stored.state,
            "Real project roundtrip retains actual captured opaque native state");
    require(!activeSourceEffect ||
                (restored.plugin(unusedEffect.id) &&
                 restored.plugin(unusedEffect.id)->state == originalEffect.state &&
                 restored.mixer[1].effects.size() == 1 && restored.mixer[2].effects.empty()),
            "Active native effect descriptor/state lives on insert1, not the unused bus");
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
    require(device && device->getCurrentBufferSizeSamples() == 256 &&
                device->getCurrentSampleRate() == 48000.,
            "Actual unchanged48k256 provider configuration");
    AudioEngine::InitialSourceTransportOptions options;
    options.audioOnlyMixerPorts = audioOnlyMixerPorts;
    options.quantum = 256;
    options.maximumSourceScalePpm = 1000000;
    options.explicitProviderFrames = providerFrames;
    options.sessionGeneration = patternMode ? 101 : 103;
    options.membershipGeneration = patternMode ? 107 : 109;
    const auto original = project.state();
    auto bad = options;
    bad.quantum = 257;
    require(audio->prepareInitialSourceTransport(project, bad).status == Status::invalidBudget &&
                audio->initialSourceTransportReadback().status == Status::none &&
                audio->projectionDiagnostics().edit == 0 && project.state().isEquivalentTo(original),
            "Refused initial quantum has no destination/native/model state mutation");
    bad = options;
    bad.sessionGeneration = 0;
    require(audio->prepareInitialSourceTransport(project, bad).status == Status::invalidProject &&
                audio->initialSourceTransportReadback().status == Status::none,
            "Invalid generation cannot publish a source identity");
    require(audio->prepareInitialSourceTransport(project, options).status == Status::configured,
            "Explicit initial source policy prepared without workers/context");
    const int expectedPackets = providerFrames == 256 ? 2 : 3;
    const int transport = expectedPackets * 256; // Independent declared512/768, never PCM-fitted.
    const int musicalDelay = activeSourceEffect ? 2 * transport : transport;
    const double musicalGain =
        activeSourceEffect ? .125 : .25; // Instrument.25 × native affine gain.5 + bias0.
    const auto configured = audio->initialSourceTransportReadback();
    require(configured.status == Status::configured && configured.quantum == 256 &&
                configured.packets == expectedPackets && configured.transportSamples == transport &&
                configured.physicalFrames == providerFrames &&
                configured.maximumSourceFrames == providerFrames && !configured.nativeKnown &&
                !configured.membershipKnown,
            "Configured declaration is not graph/worker/audio completion");
    require(awaitProjection(*audio, project, patternMode, pattern).wasOk(),
            "Actual initial AudioEngine rebuild path");
    const auto portPrepared = audio->initialAudioPortReadback();
    require(portPrepared.configured == audioOnlyMixerPorts &&
                (!audioOnlyMixerPorts ||
                 (portPrepared.tracks == project.channels.size() + project.mixer.size() - 1 &&
                  portPrepared.runtimeOwners >= 6 && portPrepared.portsPrepared >= 4 &&
                  !portPrepared.terminalFault)),
            "Actual app baseline source/controls/Mixer port policy prepared, default stays absent");
    const auto host = audio->parameterHost(instrument);
    require(host && host->alive() && host->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                host->transportPacketCount() == expectedPackets &&
                host->transportLatencySamples() == transport && host->latencySamples() == transport &&
                host->declaredLatency().nativeSamples == 0 &&
                nativeParameter(parameterCatalogue(*host), 77).value == .25f,
            "Actual worker restored opaque gain and advertises literal KQ+native0");
    const auto pending = audio->initialSourceTransportReadback();
    require(pending.status == Status::pending && !pending.pairedClosed && pending.nativeKnown &&
                pending.membershipKnown && pending.memberCount == 2 &&
                pending.membership.phase == source_membership::Phase::prepared,
            "Actual native initial context/session installed before any processing, truthfully pending");
    const auto effectHost = audio->parameterHost(unusedEffect.id);
    require(effectHost && effectHost != host &&
                effectHost->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                effectHost->transportLatencySamples() == transport &&
                effectHost->declaredLatency().nativeSamples == 0,
            "Actual second native0 effect worker participates even when zero-input/pruned");
    require(!activeSourceEffect ||
                (nativeParameter(parameterCatalogue(*effectHost), 0).value == .5 &&
                 nativeParameter(parameterCatalogue(*effectHost), 0xfedcba98u).value == 0 &&
                 effectHost->latencySamples() == transport),
            "Actual restored active native gain.5/bias0 and KQ+native0 declaration");
    const auto effectPid = effectHost->processId();
    const auto effectInstance = effectHost->runtimeIdentity();
    const auto effectReset = effectHost->midiResetGeneration();
    const auto pid = host->processId();
    const auto instance = host->runtimeIdentity();
    const auto reset = host->midiResetGeneration();
    const auto starts = device->starts, stops = device->stops;
    juce::AudioBuffer<float> actual(2, 32768), expected(2, 32768);
    actual.clear();
    expected.clear();
    std::array<std::unique_ptr<juce::AudioBuffer<float>>, 5> buffers;
    const std::array<int, 5> sizes{64, 65, 127, 256, 512};
    for (std::size_t i = 0; i < sizes.size(); ++i)
        buffers[i] = std::make_unique<juce::AudioBuffer<float>>(2, sizes[i]);
    juce::String csv = "phase,frame,frames,cursorBefore,cursorAfter,missed,dropped,error,ms,new,delete,held,"
                       "sourceSerial,closedSerial,quota\n";
    int framesWritten = 0;
    juce::int64 on = -1, off = -1;
    bool all = true;
    juce::String failure;
    try
    {
        audio->withInitialSourceTransportContext(
            [&](te::Edit &edit, te::EditPlaybackContext &context)
            {
                require(edit.initialSourceGeneration == options.sessionGeneration && edit.closedNativeIO &&
                            !context.nativeIOAttachmentRefused() && !context.sourceLifecycleInvalidated(),
                        "Actual immutable closedNativeIO excludes native attachments, not graph MIDI flags");
                auto *head = context.getNodePlayHead();
                require(head != nullptr, "Actual initial local playhead");
                audio->setMidiTarget(channel);
                audio->setLooping(false);
                audio->playStop();
                dispatchMessages(20);
                const auto origin = head->getReferenceSampleRange().getEnd();
                const auto initialStats = host->statistics();
                const auto initialEffectStats = effectHost->statistics();
                juce::String effectCsv =
                    "phase,frame,frames,sourceCursor,effectCursor,sourceSubmitted,effectSubmitted,"
                    "sourceMissed,effectMissed,sourceDropped,effectDropped,sourceConsumed,effectConsumed,"
                    "totalDeclaredLatency,sourceQuota,effectAccountedFramesCumulative\n";
                const auto drive = [&](const char *phase, int frames)
                {
                    juce::AudioBuffer<float> *output = nullptr;
                    for (std::size_t i = 0; i < sizes.size(); ++i)
                        if (sizes[i] == frames)
                            output = buffers[i].get();
                    require(output && frames <= providerFrames, "Prepared admitted actual physical span");
                    const auto before = audio->projectionDiagnostics();
                    const auto beforeCursor = host->packetFrame();
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
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
                        device->process(*output);
                    }
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
                    const auto after = audio->projectionDiagnostics();
                    const auto source = audio->initialSourceTransportReadback();
                    const auto range = head->getReferenceSampleRange();
                    double error = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                        {
                            const auto index = juce::int64(framesWritten + n) - musicalDelay;
                            const bool held = on >= 0 && index >= on && (off < 0 || index < off);
                            const float wanted =
                                !held ? 0.f
                                      : float(musicalGain *
                                              (c == 0 ? (61. / 512 + 1. / 4096) : -(68. / 512 + 1. / 8192)));
                            const float observed = output->getSample(c, n);
                            actual.setSample(c, framesWritten + n, observed);
                            expected.setSample(c, framesWritten + n, wanted);
                            error = std::max(error, std::abs(double(observed) - wanted));
                            if (!std::isfinite(observed))
                                error = std::numeric_limits<double>::infinity();
                        }
                    framesWritten += frames;
                    dispatchMessages(6);
                    host->service(); // CONTROL after WHOLE physical callback.
                    if (activeSourceEffect)
                        effectHost->service();
                    const auto stats = host->statistics();
                    const auto effectStats = effectHost->statistics();
                    const auto sourceConsumed = host->consumedLatency(),
                               effectConsumed = effectHost->consumedLatency();
                    const auto consumedSequence = juce::int64((framesWritten - 1) / 256) - expectedPackets;
                    const bool consumedExpected = consumedSequence >= 0;
                    const bool activeEffectOk =
                        !activeSourceEffect ||
                        (effectHost->packetFrame() == framesWritten % 256 &&
                         effectStats.submitted ==
                             initialEffectStats.submitted + std::uint64_t(framesWritten / 256) &&
                         effectStats.missed == initialEffectStats.missed &&
                         effectStats.dropped == initialEffectStats.dropped &&
                         effectStats.parametersRejected == initialEffectStats.parametersRejected &&
                         effectHost->midiResetGeneration() == effectReset &&
                         context.getGraphSelection().latencySamples == musicalDelay &&
                         bool(sourceConsumed) == consumedExpected &&
                         bool(effectConsumed) == consumedExpected &&
                         (!consumedExpected ||
                          (sourceConsumed->stamp.nativeSamples == 0 &&
                           effectConsumed->stamp.nativeSamples == 0 &&
                           sourceConsumed->stamp.generation == host->declaredLatency().generation &&
                           effectConsumed->stamp.generation == effectHost->declaredLatency().generation &&
                           sourceConsumed->sequence == std::uint64_t(consumedSequence) &&
                           effectConsumed->sequence == std::uint64_t(consumedSequence))));
                    const int nativeSlices = (frames + 255) / 256;
                    const bool ok =
                        error == 0 && activeEffectOk && callbackAllocations.load() == allocations &&
                        callbackDestructions.load() == destructions && ms <= 1000. * frames / 48000. &&
                        before.channels.size() == 1 && after.channels.size() == 1 &&
                        after.channels[0].renderCalls == before.channels[0].renderCalls + nativeSlices &&
                        before.edit == after.edit && before.playbackContext == after.playbackContext &&
                        after.headPlaying && after.transportPlaying &&
                        range.getEnd() == origin + framesWritten &&
                        range.getStart() == origin + framesWritten - std::min(frames, 256) &&
                        source.status == Status::ready && source.pairedClosed &&
                        source.native.sourceProcessed == frames && source.native.physicalFrames == frames &&
                        source.native.sourceQuota == frames &&
                        source.membership.lastClosedSerial == source.native.physicalSerial &&
                        host->packetFrame() == framesWritten % 256 &&
                        stats.submitted == initialStats.submitted + std::uint64_t(framesWritten / 256) &&
                        stats.missed == initialStats.missed && stats.dropped == initialStats.dropped &&
                        stats.parametersRejected == initialStats.parametersRejected &&
                        host->transportOutcome().succeeded() && host->processId() == pid &&
                        host->runtimeIdentity() == instance && host->midiResetGeneration() == reset &&
                        effectHost->processId() == effectPid &&
                        effectHost->runtimeIdentity() == effectInstance &&
                        effectHost->transportOutcome().succeeded() && source.memberCount == 2 &&
                        device->starts == starts && device->stops == stops &&
                        device->getCurrentBufferSizeSamples() == 256;
                    all &= ok;
                    csv += juce::String(phase) + "," + juce::String(framesWritten - frames) + "," +
                           juce::String(frames) + "," + juce::String(beforeCursor) + "," +
                           juce::String(host->packetFrame()) + "," + juce::String(juce::int64(stats.missed)) +
                           "," + juce::String(juce::int64(stats.dropped)) + "," + juce::String(error, 12) +
                           "," + juce::String(ms, 6) + "," +
                           juce::String(callbackAllocations.load() - allocations) + "," +
                           juce::String(callbackDestructions.load() - destructions) + "," +
                           juce::String(after.channels[0].heldInputKeys) + "," +
                           juce::String(juce::int64(source.native.physicalSerial)) + "," +
                           juce::String(juce::int64(source.membership.lastClosedSerial)) + "," +
                           juce::String(source.native.sourceQuota) + "\n";
                    if (activeSourceEffect)
                    {
                        effectCsv += juce::String(phase) + "," + juce::String(framesWritten - frames) + "," +
                                     juce::String(frames) + "," + juce::String(host->packetFrame()) + "," +
                                     juce::String(effectHost->packetFrame()) + "," +
                                     juce::String(juce::uint64(stats.submitted)) + "," +
                                     juce::String(juce::uint64(effectStats.submitted)) + "," +
                                     juce::String(juce::uint64(stats.missed)) + "," +
                                     juce::String(juce::uint64(effectStats.missed)) + "," +
                                     juce::String(juce::uint64(stats.dropped)) + "," +
                                     juce::String(juce::uint64(effectStats.dropped)) + "," +
                                     (sourceConsumed ? juce::String(juce::uint64(sourceConsumed->sequence))
                                                     : "absent") +
                                     "," +
                                     (effectConsumed ? juce::String(juce::uint64(effectConsumed->sequence))
                                                     : "absent") +
                                     "," + juce::String(context.getGraphSelection().latencySamples) + "," +
                                     juce::String(source.native.sourceQuota) + "," +
                                     juce::String(juce::uint64(
                                         (effectStats.submitted - initialEffectStats.submitted) * 256 +
                                         std::uint64_t(effectHost->packetFrame()))) +
                                     "\n";
                        require(folder.getChildFile("active-effect-callbacks.csv").replaceWithText(effectCsv),
                                "Retain actual two-worker PCM/packet/clock facts before assertion");
                    }
                    require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                            "Retain every actual app callback before oracle");
                    const auto portNow = audio->initialAudioPortReadback();
                    require(!audioOnlyMixerPorts ||
                                (portNow.configured && portNow.audioBlocks > 0 &&
                                 portNow.nonemptyAudioMidi == 0 && !portNow.terminalFault),
                            "Every actually processed app audio port has empty actual MIDI output");
                    require(
                        ok,
                        "Actual initial app KQ PCM/clock/owner/deadline/scope failure; preserve evidence");
                };
                for (int i = 0; i < 8; ++i)
                    drive("all_startup_frames", 256);
                if (audioOnlyMixerPorts)
                {
                    audio->midiInput.capture.begin();
                    audio->typingInput.capture.begin();
                }
                on = framesWritten;
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("physical_on", 127);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Actual physical key owner");
                audio->typingInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("typing_same_pitch", 64);
                drive("partial_before_old_boundary", 64);
                drive("same_callback_crosses_logical_boundary", 65);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 2,
                        "Two actual owners, one native note");
                // Real canonical score edit/Undo/Redo at a future beat: no invented
                // DSP input or silence exclusion. Held physical voice must continue.
                CommandArgs note;
                note.channel = channel;
                note.pattern = pattern;
                note.beat = 32;
                note.length = 1;
                note.pitch = 72;
                note.value = .5;
                require(controller.execute("notes.add", note).wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Initial policy keeps ordinary immutable note edits available");
                drive("note_edit_unaffected_held_voice", 256);
                require(controller.undoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Canonical note Undo retained");
                drive("note_undo_unaffected_held_voice", 256);
                require(controller.redoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Canonical note Redo retained");
                for (int i = 0; i < 5; ++i)
                    drive("held_full_PCM", 256);
                if (audioOnlyMixerPorts)
                    initial_app_ports_acceptance::privateGraph(*audio, edit, context, drive);
                if (providerFrames == 512)
                    drive("genuine_whole_physical512_not_two_fake_callbacks", 512);
                audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("physical_off_typing_still_holds", 127);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Shared typing owner retains sound");
                off = framesWritten;
                audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("final_typing_off", 65);
                for (int i = 0; i < 5; ++i)
                    drive("all_release_tail_frames", 256);
                if (activeSourceEffect)
                {
                    // Preserve every original tail callback, then cover the
                    // actual literal two-worker release and a complete silentQ.
                    while (juce::int64(framesWritten) < off + musicalDelay + 256)
                        drive("active_effect_complete_release_tail_frames", 256);
                    require(juce::int64(framesWritten) >= off + musicalDelay + 256,
                            "Full active-FX release plus one silent quantum checked, no tail exclusion");
                }
                if (audioOnlyMixerPorts)
                {
                    initial_app_ports_acceptance::captured(audio->midiInput, channel);
                    initial_app_ports_acceptance::captured(audio->typingInput, channel);
                }
                const auto catalogue = parameterCatalogue(*host, true);
                require(nativeParameter(catalogue, 90).value == 1. / 1024 &&
                            nativeParameter(catalogue, 91).value == 1. / 1024,
                        "Actual native one on/one off counters, no duplicate/retrigger or lost release");
                require(parameterResult(*host, host->setParameter(77, .33f, catalogue.generation)).result ==
                            0,
                        "Genuine native dirty control state before rejected reconfiguration");
                const auto beforeConfig = audio->initialSourceTransportReadback();
                require(
                    audio->prepareInitialSourceTransport(project, options).status == Status::invalidOwner &&
                        audio->initialSourceTransportReadback().deviceEpoch == beforeConfig.deviceEpoch &&
                        host->processId() == pid && host->runtimeIdentity() == instance &&
                        std::abs(nativeParameter(parameterCatalogue(*host, true), 77).value - .33f) < .00001f,
                    "Incumbent initial setup refusal cannot replace or reset exact worker/native state");
                auto changed = project;
                changed.channel(channel)->mixer = 2;
                std::unique_ptr<AudioEngine::PreparedChannelRouting> plan;
                require(
                    audio->prepareChannelRouting(changed, plan).status ==
                            AudioEngine::ChannelRoutingStatus::unsupportedDelta &&
                        !plan,
                    "Unqualified structural/membership/PDC changes explicitly refuse, no fake completion");
                // Genuine oversize physical callback: failure is NOT a successful
                // muted music callback. No Host/cursor/MIDI/reference input advances.
                juce::AudioBuffer<float> oversized(2, providerFrames + 256);
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < oversized.getNumSamples(); ++n)
                        oversized.setSample(c, n, .375f);
                const auto statsBefore = host->statistics();
                const auto cursor = host->packetFrame();
                const auto effectCursor = effectHost->packetFrame();
                const auto effectBefore = effectHost->statistics();
                const auto reference = head->getReferenceSampleRange();
                const auto beforeSource = audio->projectionDiagnostics();
                const auto allocations = callbackAllocations.load(),
                           destructions = callbackDestructions.load();
                const auto started = juce::Time::getHighResolutionTicks();
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
                    device->process(oversized);
                }
                const auto failedMs =
                    SyntheticCallbackTiming::milliseconds(started, juce::Time::getHighResolutionTicks());
                bool failedOutput = true;
                auto raw =
                    folder.getChildFile("oversized-explicit-failure-planar-f32.raw").createOutputStream();
                require(bool(raw) && raw->setPosition(0) && raw->truncate().wasOk(),
                        "Owned explicit failed-callback evidence");
                for (int c = 0; c < 2; ++c)
                {
                    for (int n = 0; n < oversized.getNumSamples(); ++n)
                        failedOutput &= oversized.getSample(c, n) == 0.f;
                    require(raw->write(oversized.getReadPointer(c),
                                       std::size_t(oversized.getNumSamples()) * sizeof(float)),
                            "All failed-callback samples retained");
                }
                raw->flush();
                require(raw->getStatus().wasOk(), "Failed-callback evidence flush/status");
                const auto failed = audio->initialSourceTransportReadback();
                require(failed.status == Status::sourceFault && failed.native.failed &&
                            failed.native.status == te::SourceBurstSession::Status::physicalBurstExceeded &&
                            host->packetFrame() == cursor &&
                            host->statistics().submitted == statsBefore.submitted &&
                            host->statistics().missed == statsBefore.missed &&
                            host->statistics().dropped == statsBefore.dropped &&
                            (!activeSourceEffect ||
                             (effectHost->packetFrame() == effectCursor &&
                              effectHost->statistics().submitted == effectBefore.submitted &&
                              effectHost->statistics().missed == effectBefore.missed &&
                              effectHost->statistics().dropped == effectBefore.dropped &&
                              effectHost->midiResetGeneration() == effectReset)) &&
                            head->getReferenceSampleRange() == reference &&
                            audio->projectionDiagnostics().channels[0].renderCalls ==
                                beforeSource.channels[0].renderCalls &&
                            callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions && failedOutput &&
                            failedMs <= 1000. * oversized.getNumSamples() / 48000.,
                        "Actual over-budget outer refuses before source/worker/clock mutation, typed sticky "
                        "failure");
            });
    }
    catch (const std::exception &error)
    {
        all = false;
        failure = error.what();
    }
    const auto write = [&](const char *name, const juce::AudioBuffer<float> &data)
    {
        auto output = folder.getChildFile(name).createOutputStream();
        require(bool(output) && output->setPosition(0) && output->truncate().wasOk(),
                "Fresh complete PCM stream");
        for (int c = 0; c < 2; ++c)
            require(output->write(data.getReadPointer(c), std::size_t(framesWritten) * sizeof(float)),
                    "All planar PCM samples retained");
        output->flush();
        require(output->getStatus().wasOk(), "Raw evidence flush/status");
    };
    write("actual-planar-f32.raw", actual);
    write("expected-planar-f32.raw", expected);
    require(folder.getChildFile("result.txt")
                .replaceWithText("pass=" + juce::String(int(all)) + "\ntransport=" + juce::String(transport) +
                                 "\nmusicalDelay=" + juce::String(musicalDelay) +
                                 "\nactiveSourceEffect=" + juce::String(int(activeSourceEffect)) +
                                 "\nframes=" + juce::String(framesWritten) + "\nfailure=" + failure +
                                 "\nNOT qualified: default1Q, native fixedcost1frame, structural PDC, live "
                                 "membership replacement, installed providers, hardware/listening.\n"),
            "Actual terminal initial app result retained");
    require(all, "Initial app source experiment failed; retained complete PCM/clock/source evidence");
}
} // namespace initial_app_source_acceptance

static void initialAppSourceTransportTests(const juce::File &folder, const PluginSpec &genuine,
                                           const PluginSpec &effect)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "New bounded initial app source evidence");
    require(genuine.instrument && genuine.name == "VibeRa Instrument Runtime Fixture",
            "Exact original SDK instrument, no arbitrary-provider oracle");
    require(!effect.instrument && effect.name == "VibeRa Parameter Fixture",
            "Exact original native0 SDK effect for complete fixed membership");
    auto captured = initial_app_source_acceptance::capturedOriginal(genuine);
    std::exception_ptr first;
    juce::String outcomes;
    for (const int maximum : {256, 512})
        for (const bool pattern : {true, false})
        {
            const auto name = "bound-" + juce::String(maximum) + (pattern ? "-pattern" : "-song");
            try
            {
                initial_app_source_acceptance::run(folder.getChildFile(name), captured, effect, pattern,
                                                   maximum);
                outcomes += name + "=completed\n";
            }
            catch (const std::exception &error)
            {
                if (!first)
                    first = std::current_exception();
                outcomes += name + "=failed:" + juce::String(error.what()) + "\n";
            }
            require(folder.getChildFile("cases.txt").replaceWithText(outcomes),
                    "Every independent initial app case preserved");
        }
    if (first)
        std::rethrow_exception(first);
}
