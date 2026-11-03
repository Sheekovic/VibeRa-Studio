// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppSourceTransportAcceptance.h"
#include "DynamicInstrumentLatencyAcceptance.h"
#include "../src/InitialSourceEpochFence.h"

// ORIGINAL actual initial AudioEngine + distinct original dynamic SDK source.
// SyntheticDevice is an actual native callback driver, not OS/hardware input.
// Include after existing require/parameter/heap/timing/awaitProjection helpers.
namespace initial_source_epoch_acceptance
{
using namespace vibera;
inline float literal(int channel, int sourceFrame, int on)
{
    if (sourceFrame < 0 || on < 0 || sourceFrame < on)
        return 0.f;
    const int numerator =
        channel == 0 ? (sourceFrame * 17 + 5) % 251 - 125 : -((sourceFrame * 23 + 11) % 241 - 120);
    return float((channel == 0 ? 61 : 68) * numerator) / 262144.f; // Native gain.25, velocity127, pitch60.
}
__declspec(noinline) inline void application(const juce::File &folder, const PluginSpec &stored,
                                             bool patternMode, bool pendingControl)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh original actual source epoch attempt");
    initial_app_source_acceptance::Storage settings(folder.getChildFile("settings"));
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
    project.tempo = 120;
    project.channels[0].instrument = stored;
    project.channels[0].instrument->id = project.allocate();
    project.channels[0].mixer = 1;
    project.channels[0].gain = 1;
    project.channels[0].pan = 0;
    const auto channel = project.channels[0].id, plugin = project.channels[0].instrument->id;
    project.patterns.resize(1);
    project.patterns[0].length = 64;
    project.patterns[0].notes.clear();
    const auto pattern = project.patterns[0].id;
    project.placements = {{project.allocate(), pattern, 0, 0, 64, 0}};
    for (auto &insert : project.mixer)
    {
        insert.effects.clear();
        insert.routes.clear();
        insert.gain = 1;
        insert.pan = 0;
        if (insert.index)
            insert.routes.push_back({0, 1.f});
    }
    require(project.validate().wasOk(), "Real closed native0 one-instrument/no-FX Project");
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
    require(device && device->getCurrentBufferSizeSamples() == 256, "Actual unchanged native Q256/48k");
    AudioEngine::InitialSourceTransportOptions options;
    options.quantum = 256;
    options.explicitProviderFrames = 256;
    options.sessionGeneration = 2901;
    options.membershipGeneration = 2903;
    options.audioOnlyMixerPorts = true;
    options.audioZeroHistoryProofs = true;
    options.zeroStageSourceRoutes = true;
    options.routingController = &controller;
    require(audio->prepareInitialSourceTransport(project, options).status ==
                    AudioEngine::InitialSourceTransportStatus::configured &&
                awaitProjection(*audio, project, patternMode, pattern).wasOk(),
            "Actual fresh initial source policy installed");
    auto host = audio->parameterHost(plugin);
    require(host && host->declaredLatency() == PluginLatencyStamp{0, 1} &&
                host->transportLatencySamples() == 512,
            "Original dynamic Instrument genuinely starts native0/K2Q512");
    const auto pid = host->processId();
    const auto uuid = host->runtimeIdentity();
    const auto reset = host->midiResetGeneration();
    const auto initialStats = host->statistics();
    const auto starts = device->starts, stops = device->stops;
    auto epochs = audio->initialSourceEpochObservations();
    require(epochs.size() == 1 && epochs[0].plugin == plugin && epochs[0].runtime && !epochs[0].failure &&
                epochs[0].expected.identity == host->pcmIdentity() &&
                epochs[0].expected.native == PluginLatencyStamp{0, 1},
            "Real once-prepared same Host/Runtime/plan guard, no warm origin inference");
    const auto runtime = epochs[0].runtime;
    juce::AudioBuffer<float> actual(2, 8192), expected(2, 8192), block(2, 256);
    actual.clear();
    expected.clear();
    int written = 0, on = -1;
    juce::String rows = "phase,start,frames,error,new,delete,ms,sourceCalls,headEnd,hostCursor,submitted,"
                        "sourceHealthy,sourceStatus\n";
    const auto retain = [&]
    {
        dynamic_instrument_latency::rawStereo(folder.getChildFile("actual-stereo.f32"), actual, written);
        dynamic_instrument_latency::rawStereo(folder.getChildFile("expected-stereo.f32"), expected, written);
        require(folder.getChildFile("callbacks.csv").replaceWithText(rows),
                "Retain every original PCM/callback before assertions");
    };
    std::exception_ptr failure;
    try
    {
        audio->withInitialSourceTransportContext(
            [&](te::Edit &edit, te::EditPlaybackContext &context)
            {
                auto *head = context.getNodePlayHead();
                require(head, "Real local native source PlayHead");
                audio->setMidiTarget(channel);
                audio->setLooping(false);
                audio->playStop();
                dispatchMessages(20);
                const auto origin = head->getReferenceSampleRange().getEnd();
                const auto callback = [&](const char *phase, int frames, bool mayRefuse)
                {
                    require(frames > 0 && frames <= 256 && written + frames <= actual.getNumSamples(),
                            "Prepared unchanged actual callback span");
                    block.setSize(2, frames, false, false, true);
                    block.clear(); // CONTROL, before probe/callback.
                    const auto before = audio->projectionDiagnostics();
                    const auto headBefore = head->getReferenceSampleRange();
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    const auto began = juce::Time::getHighResolutionTicks();
                    {
                        struct Probe
                        {
                            const bool old = callbackAllocationProbe;
                            Probe()
                            {
                                callbackAllocationProbe = true;
                            }
                            ~Probe()
                            {
                                callbackAllocationProbe = old;
                            }
                        } probe;
                        device->process(block);
                    }
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
                    const auto after = audio->projectionDiagnostics();
                    const auto state = audio->initialSourceTransportReadback();
                    const bool refused = state.fixedEpochConfigured && !state.fixedEpochHealthy;
                    double error = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                        {
                            // Two PREDECLARED outcomes: healthy exact fixed musical
                            // waveform, or typed source-fault defined zero. No phase fit.
                            const auto wanted = refused ? 0.f : literal(c, written + n - 512, on);
                            const auto heard = block.getSample(c, n);
                            actual.setSample(c, written + n, heard);
                            expected.setSample(c, written + n, wanted);
                            error = std::isfinite(heard) ? std::max(error, std::abs(double(heard) - wanted))
                                                         : std::numeric_limits<double>::infinity();
                        }
                    rows += juce::String(phase) + "," + juce::String(written) + "," + juce::String(frames) +
                            "," + juce::String(error, 12) + "," +
                            juce::String(callbackAllocations.load() - allocations) + "," +
                            juce::String(callbackDestructions.load() - destructions) + "," +
                            juce::String(ms, 9) + "," + juce::String(after.channels[0].renderCalls) + "," +
                            juce::String(head->getReferenceSampleRange().getEnd()) + "," +
                            juce::String(host->packetFrame()) + "," +
                            juce::String(host->statistics().submitted) + "," +
                            juce::String(int(state.fixedEpochHealthy)) + "," +
                            juce::String(int(state.status)) + "\n";
                    written += frames;
                    retain();
                    require(error == 0 && callbackAllocations.load() == allocations &&
                                callbackDestructions.load() == destructions &&
                                ms <= 1000. * frames / 48000. && after.edit == before.edit &&
                                after.playbackContext == before.playbackContext &&
                                after.channels[0].sampler == before.channels[0].sampler &&
                                host->processId() == pid && host->runtimeIdentity() == uuid &&
                                host->midiResetGeneration() == reset && device->starts == starts &&
                                device->stops == stops,
                            "Every actual PCM sample/identity/heap/destruction/deadline retained");
                    if (refused)
                        require(mayRefuse &&
                                    state.status == AudioEngine::InitialSourceTransportStatus::sourceFault &&
                                    !state.pairedClosed && !state.native.scopeOpen,
                                "Honest failed closed source outcome, never completed silence");
                    else
                        require(!state.sourceExchangeFaulted && state.pairedClosed &&
                                    after.channels[0].renderCalls == before.channels[0].renderCalls + 1 &&
                                    head->getReferenceSampleRange().getStart() == headBefore.getEnd() &&
                                    head->getReferenceSampleRange().getEnd() == origin + written &&
                                    host->packetFrame() == written % 256 &&
                                    host->statistics().submitted ==
                                        initialStats.submitted + std::uint64_t(written / 256) &&
                                    host->statistics().missed == initialStats.missed &&
                                    host->statistics().dropped == initialStats.dropped,
                                "All healthy startup/held frames have exact native/source/packet clocks and "
                                "closed real scopes");
                    return refused;
                };
                const auto drive = [&](const char *phase, int frames)
                {
                    dynamic_instrument_latency::waitProcessed(*host);
                    require(!callback(phase, frames, false), "Baseline fixed epoch stays healthy");
                };
                for (int n = 0; n < 8; ++n)
                    drive("all_startup_frames", 256);
                audio->midiInput.capture.begin();
                audio->typingInput.capture.begin();
                on = written;
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("physical_on", 127);
                audio->typingInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                for (int frames : {64, 64, 65})
                    drive("physical_typing_same_key", frames);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 2,
                        "Two real private owners and one native note");
                CommandArgs note;
                note.channel = channel;
                note.pattern = pattern;
                note.beat = 32;
                note.length = 1;
                note.pitch = 72;
                note.value = .5;
                require(controller.execute("notes.add", note).wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Real future score edit with exact same fence");
                drive("real_note_edit", 256);
                require(controller.undoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Real canonical note Undo");
                drive("real_note_Undo", 256);
                require(controller.redoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Real canonical note Redo");
                drive("real_note_Redo", 256);
                {
                    CommandArgs route;
                    route.channel = channel;
                    route.mixer = 2;
                    std::unique_ptr<PreparedControllerChange> command;
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> candidate;
                    require(controller.prepareStructuralCommand("channel.mixer", route, command).wasOk() &&
                                command &&
                                audio->prepareInitialSourceChannelRouting(controller, *command, candidate)
                                        .status == AudioEngine::ChannelRoutingStatus::ready,
                            "Actual native private source-route preparation with held owner");
                    candidate.reset();
                    command.reset();
                }
                drive("FIRST_after_cancel_exact_guard_retained", 256);
                epochs = audio->initialSourceEpochObservations();
                require(epochs.size() == 1 && epochs[0].runtime == runtime && !epochs[0].failure,
                        "Cancellation cannot recreate/reset the warm guard");
                dynamic_instrument_latency::waitProcessed(*host);
                const auto metadata = parameterCatalogue(*host, true);
                require(nativeParameter(metadata, 90).value == 1. / 1024 &&
                            nativeParameter(metadata, 91).value == 0 &&
                            nativeParameter(metadata, 92).value == 1. / 1024,
                        "Actual dynamic worker held one On/no Off, no retrigger");
                const auto model = project.state();
                const auto revision = controller.revision;
                const auto undoName = controller.undo.getUndoDescription(),
                           redoName = controller.undo.getRedoDescription();
                const auto request = host->setParameter(17, 2.f / 3, metadata.generation);
                require(request != 0, "Genuine supported native mode257 control request");
                if (pendingControl)
                {
                    // Exactly ONE nonblocking real interleaving attempt; no DSP
                    // wait, stamp injection, artificial load or retry-until-race.
                    const auto beforeStamp = host->declaredLatency();
                    const bool refused = callback("one_pending_control_interleaving_attempt", 256, true);
                    const auto seen = audio->initialSourceEpochObservations();
                    const auto guard = seen[0].failure ? seen[0].failure->guard
                                                       : std::optional<FixedSourcePcmEpochGuard::Failure>{};
                    const bool consumedRace =
                        refused && guard && guard->fault == FixedSourcePcmEpochGuard::Fault::nativeEpoch;
                    require(
                        folder.getChildFile("post-prefetch-race.json")
                            .replaceWithText(
                                "{\"attemptBound\":1,\"actualCallbacks\":1,\"consumedEpochRaceObserved\":" +
                                juce::String(consumedRace ? "true" : "false") +
                                ",\"beforeNative\":" + juce::String(beforeStamp.nativeSamples) +
                                ",\"beforeGeneration\":" + juce::String(beforeStamp.generation) +
                                ",\"qualification\":\"" +
                                juce::String(
                                    consumedRace
                                        ? "actual typed consumed-epoch rejection observed"
                                        : "fixture limitation: post-prefetch consumed race unobserved") +
                                "\"}"),
                        "Retain truthful bounded actual interleaving outcome");
                }
                const auto acknowledgedControl = parameterResult(*host, request);
                const auto controlStamp = host->declaredLatency();
                require(folder.getChildFile("native-control-ack.txt").replaceWithText(
                            "result=" + juce::String(acknowledgedControl.result) +
                            " actual=" + juce::String(acknowledgedControl.value, 17) +
                            " native=" + juce::String(controlStamp.nativeSamples) +
                            " generation=" + juce::String(controlStamp.generation)),
                        "Retain native acknowledgement separately before assertions");
                require(acknowledgedControl.result == 0,
                        "Real native mode control acknowledged, not yet a processed-packet declaration");
                dynamic_instrument_latency::waitProcessed(*host); // CONTROL only: finish existing prefix.
                const auto acknowledgedStamp = host->declaredLatency();
                if (acknowledgedStamp == PluginLatencyStamp{0, 1})
                {
                    // Worker publishes a declaration after actual audio processing,
                    // not merely after controller acknowledgement. Produce ONE real
                    // packet; its currently owed K2 output still belongs to the old
                    // epoch. Preserve exact PCM or an honest typed boundary refusal.
                    callback("one_actual_packet_after_native_control_ack", 256, true);
                    dynamic_instrument_latency::waitProcessed(*host); // Never in DSP.
                }
                const auto publishedStamp = host->declaredLatency();
                require(folder.getChildFile("native-declaration-publication.txt").replaceWithText(
                            "acknowledgedNative=" + juce::String(acknowledgedStamp.nativeSamples) +
                            " acknowledgedGeneration=" + juce::String(acknowledgedStamp.generation) +
                            " publishedNative=" + juce::String(publishedStamp.nativeSamples) +
                            " publishedGeneration=" + juce::String(publishedStamp.generation)),
                        "Retain actual native-control versus packet-declaration evidence");
                require(publishedStamp == PluginLatencyStamp{257, 2},
                        "Real processed worker packet publishes exact changed declaration between callbacks");
                const auto before = audio->projectionDiagnostics();
                const auto clock = head->getReferenceSampleRange();
                const auto cursor = host->packetFrame();
                const auto stats = host->statistics();
                const auto selected = context.getGraphSelection().selectedSerial;
                const auto generation = audio->initialSourceTransportReadback().completedExchangeGeneration;
                MidiClock::Position inputBefore, typingBefore;
                require(audio->midiInput.recordingPosition(inputBefore) &&
                            audio->typingInput.recordingPosition(typingBefore),
                        "Actual coherent private input clocks before refusal");
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 64, juce::uint8(127)));
                tracktion::AudioTrack *nativeA = nullptr;
                for (auto *track : te::getAudioTracks(edit))
                    if (track->state.hasProperty("viberaMixerIndex") &&
                        int(track->state["viberaMixerIndex"]) == 1)
                        nativeA = track;
                require(nativeA, "Real native Mixer Track for unconsumed inbox witness");
                auto inboxes = audio->initialMixerTrackMidiObservations();
                const auto readA = [&](const auto &values)
                {
                    const auto found = std::find_if(values.begin(), values.end(),
                                                    [](const auto &value) { return value.mixer == 1; });
                    require(found != values.end(), "Actual per-Track owning inbox observation exists");
                    return *found;
                };
                const auto baselineInbox = readA(inboxes);
                nativeA->injectLiveMidiMessage(juce::MidiMessage::noteOn(4, 67, juce::uint8(100)), 801);
                inboxes = audio->initialMixerTrackMidiObservations();
                const auto queued = readA(inboxes);
                require(queued.accepted == baselineInbox.accepted + 1 &&
                            queued.delivered == baselineInbox.delivered,
                        "Actual native inbox accepted/pending before known source refusal");
                require(callback("FIRST_known_declaration_refusal", 127, true),
                        "Known changed source cannot process graph/input/clock");
                const auto after = audio->projectionDiagnostics();
                const auto closed = audio->initialSourceTransportReadback();
                const auto failed = audio->initialSourceEpochObservations();
                inboxes = audio->initialMixerTrackMidiObservations();
                const auto retained = readA(inboxes);
                MidiClock::Position inputAfter, typingAfter;
                require(audio->midiInput.recordingPosition(inputAfter) &&
                            audio->typingInput.recordingPosition(typingAfter),
                        "Actual coherent private input clocks after refusal");
                require(folder.getChildFile("source-failure.txt")
                            .replaceWithText(
                                "sourceFault=" + juce::String(int(closed.status)) +
                                " membership=" + juce::String(int(closed.membership.status)) +
                                " scopesOpen=" + juce::String(int(closed.native.scopeOpen)) +
                                " headBefore=" + juce::String(clock.getEnd()) +
                                " headAfter=" + juce::String(head->getReferenceSampleRange().getEnd()) +
                                " cursorBefore=" + juce::String(cursor) +
                                " cursorAfter=" + juce::String(host->packetFrame()) +
                                " accepted=" + juce::String(retained.accepted) +
                                " delivered=" + juce::String(retained.delivered) +
                                " generationBefore=" + juce::String(generation) + " generationAfter=" +
                                juce::String(audio->initialSourceTransportReadback().completedExchangeGeneration)),
                        "Retain actual failed clock/queue/scope/selection facts before assertions");
                require(
                    failed.size() == 1 && failed[0].runtime == runtime && failed[0].failure &&
                        failed[0].failure->guard &&
                        ((failed[0].failure->guard->fault ==
                              FixedSourcePcmEpochGuard::Fault::declarationChanged &&
                          failed[0].failure->guard->declaration == PluginLatencyStamp{257, 2}) ||
                         (pendingControl &&
                          failed[0].failure->guard->fault == FixedSourcePcmEpochGuard::Fault::nativeEpoch &&
                          failed[0].failure->guard->consumed &&
                          failed[0].failure->guard->consumed->stamp == PluginLatencyStamp{257, 2})) &&
                        closed.membership.status == source_membership::Status::fixedSourceEpochRefused &&
                        !closed.native.scopeOpen && !closed.pairedClosed &&
                        after.channels[0].renderCalls == before.channels[0].renderCalls &&
                        after.channels[0].heldInputKeys == 2 && head->getReferenceSampleRange() == clock &&
                        host->packetFrame() == cursor && host->statistics().submitted == stats.submitted &&
                        host->statistics().missed == stats.missed && retained.runtime == queued.runtime &&
                        retained.accepted == queued.accepted && retained.delivered == queued.delivered &&
                        inputAfter.seconds == inputBefore.seconds && inputAfter.wall == inputBefore.wall &&
                        inputAfter.block == inputBefore.block && inputAfter.playing == inputBefore.playing &&
                        typingAfter.seconds == typingBefore.seconds &&
                        typingAfter.wall == typingBefore.wall &&
                        context.getGraphSelection().selectedSerial == selected &&
                        audio->initialSourceTransportReadback().completedExchangeGeneration == generation &&
                        project.state().isEquivalentTo(model) && controller.revision == revision &&
                        controller.undo.getUndoDescription() == undoName &&
                        controller.undo.getRedoDescription() == redoName && host->alive() &&
                        host->processId() == pid && host->runtimeIdentity() == uuid &&
                        host->midiResetGeneration() == reset,
                    "Typed source-only failure preserves input/clock/native queue/held "
                    "owners/model/history/worker; no success/rollback/reset");
                const auto first = *failed[0].failure;
                require(callback("sticky_refusal_no_retry_or_rearm", 64, true),
                        "Source failure stays closed");
                const auto again = audio->initialSourceEpochObservations();
                require(again[0].failure->reason == first.reason &&
                            again[0].failure->guard->declaration == first.guard->declaration &&
                            again[0].failure->guard->logicalFrame == first.guard->logicalFrame,
                        "Candidate/callback repetition cannot overwrite first source fault");
                const auto saved = dynamic_instrument_latency::state(*host);
                require(folder.getChildFile("dirty-mode257-gain25.state").replaceWithText(saved),
                        "Retain real dirty state after source refusal");
                dynamic_instrument_latency::versionedState(saved);
                const auto final = parameterCatalogue(*host, true);
                require(host->alive() && nativeParameter(final, 17).value == 2.f / 3 &&
                            nativeParameter(final, 77).value == .25 &&
                            nativeParameter(final, 90).value == 1. / 1024 &&
                            nativeParameter(final, 91).value == 0 &&
                            nativeParameter(final, 92).value == 1. / 1024,
                        "Source fence preserves actual dirty native controls/state and held voice; worker "
                        "not shut down");
                MidiCapture::Event captured;
                for (int pitch : {60, 64})
                    require(audio->midiInput.capture.pop(captured) && captured.channel == channel &&
                                captured.bytes[0] == 0x90 && captured.bytes[1] == pitch &&
                                captured.bytes[2] == 127,
                            "Actual physical-programmatic capture retains held and pending On after refusal");
                require(!audio->midiInput.capture.pop(captured) && !audio->midiInput.capture.failed.load() &&
                            audio->typingInput.capture.pop(captured) && captured.channel == channel &&
                            captured.bytes[0] == 0x90 && captured.bytes[1] == 60 &&
                            !audio->typingInput.capture.pop(captured) &&
                            !audio->typingInput.capture.failed.load(),
                        "Independent typing capture retained exactly once, no synthetic held boolean or lost "
                        "recording data");
                audio->playStop(); // Explicit CONTROL disposal follows retained evidence, no recovery claim.
            });
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    retain();
    if (failure)
        std::rethrow_exception(failure);
}
// Direct actual Host capture: deliberately no active app observer here. A single
// call really contains old/new completed packet generations; this is independent
// emitted-metadata evidence, NOT a fabricated application race or graph success.
__declspec(noinline) inline void realMixedCapture(const juce::File &folder, const PluginSpec &spec)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh real mixed-generation evidence");
    PluginHost host(PluginHost::TransportPolicy::twoQuanta);
    require(host.start(spec, 48000, 256, true).wasOk(), "Actual original SDK offline K2 source");
    FixedSourcePcmEpochGuard guard({host.pcmIdentity(), host.declaredLatency(), 512});
    juce::AudioBuffer<float> pcm(2, 512), actual(2, 1024), expected(2, 1024);
    pcm.clear();
    actual.clear();
    expected.clear();
    std::array<PluginPcmSegment, 4> storage;
    PluginPcmCapture capture(storage, host.pcmIdentity());
    juce::String facts = "callStart,offset,count,outputPacket,consumedSequence,native,generation,dry\n";
    const auto record = [&](int start, int count)
    {
        for (int c = 0; c < 2; ++c)
            actual.copyFrom(c, start, pcm, c, 0, count);
        for (const auto &span : capture.segments())
            facts += juce::String(start) + "," + juce::String(span.offset) + "," + juce::String(span.count) +
                     "," + juce::String(span.outputPacket) + "," +
                     juce::String(span.consumed ? span.consumed->sequence : 0) + "," +
                     juce::String(span.consumed ? span.consumed->stamp.nativeSamples : -1) + "," +
                     juce::String(span.consumed ? span.consumed->stamp.generation : 0) + "," +
                     juce::String(span.dryDelaySamples) + "\n";
    };
    const ipc::MidiEvent on{0, {0x90, 60, 127}, 3};
    host.process(pcm, 0, 256, &on, 1, 0., 120, true, false, 4, 4, nullptr, 0, 0, true, nullptr, 0, 1,
                 &capture);
    record(0, 256);
    const bool firstStartup = guard.validate(capture, 256);
    dynamic_instrument_latency::waitProcessed(
        host); // CONTROL: prove packet0 finished native0 BEFORE mode change.
    const auto metadata = parameterCatalogue(host, true);
    require(parameterResult(host, host.setParameter(17, 1.f / 3, metadata.generation)).result == 0,
            "Actual native31 before next submitted packet");
    host.process(pcm, 0, 256, nullptr, 0, 256. / 48000, 120, true, false, 4, 4, nullptr, 0, 0, true, nullptr,
                 0, 1, &capture);
    record(256, 256);
    const bool secondStartup = guard.validate(capture, 256);
    host.process(pcm, 0, 512, nullptr, 0, 512. / 48000, 120, true, false, 4, 4, nullptr, 0, 0, true, nullptr,
                 0, 1, &capture);
    record(512, 512);
    bool exact = true;
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 512; ++n)
            exact &= actual.getSample(c, n) == 0.f;
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 512; ++n)
        {
            const int frame = n < 256 ? n : n - 31;
            const auto wanted = literal(c, frame, 0), value = pcm.getSample(c, n);
            actual.setSample(c, 512 + n, value);
            expected.setSample(c, 512 + n, wanted);
            exact &= value == wanted;
        }
    dynamic_instrument_latency::rawStereo(folder.getChildFile("actual-stereo.f32"), actual, 1024);
    dynamic_instrument_latency::rawStereo(folder.getChildFile("expected-stereo.f32"), expected, 1024);
    require(folder.getChildFile("actual-segments.csv").replaceWithText(facts),
            "Retain actual completed epoch segments before assertions");
    require(firstStartup && secondStartup && exact && capture.status == PluginPcmReceiptStatus::complete &&
                capture.count == 2 && capture.storage[0].consumed &&
                capture.storage[0].consumed->stamp == PluginLatencyStamp{0, 1} &&
                capture.storage[1].consumed &&
                capture.storage[1].consumed->stamp == PluginLatencyStamp{31, 2} &&
                !guard.validate(capture, 512) &&
                guard.failure()->fault == FixedSourcePcmEpochGuard::Fault::nativeEpoch &&
                guard.failure()->segment == 1 && guard.failure()->logicalFrame == 512 && host.alive(),
            "Actual mixed-generation whole call refuses second segment without inventing consumed/graph "
            "completion");
}
__declspec(noinline) inline void tests(const juce::File &folder, const juce::File &binary)
{
    require(!folder.exists() && folder.createDirectory().wasOk() && binary.existsAsFile(),
            "Fresh bounded genuine SDK source suite");
    std::vector<juce::PluginDescription> found;
    require(PluginCatalog::scanOne(binary, found).wasOk() && found.size() == 1 && found[0].isInstrument &&
                found[0].name == "VibeRa Dynamic Instrument Fixture",
            "Actual distinct original dynamic Instrument|Synth descriptor");
    PluginSpec spec{1, found[0].name, found[0].createXml()->toString(), {}, true, false};
    const auto stored = initial_app_source_acceptance::capturedOriginal(spec);
    juce::String identities;
    for (const auto &path : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), binary})
        identities += path.getFullPathName() + " sha256=" + juce::SHA256(path).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact application/worker/original fixture identities");
    std::exception_ptr first;
    juce::String cases;
    for (int mode = 0; mode < 4; ++mode)
    {
        const auto name = juce::String(mode == 0   ? "pattern_known"
                                       : mode == 1 ? "song_known"
                                       : mode == 2 ? "one_pending_attempt"
                                                   : "actual_mixed_capture");
        try
        {
            if (mode == 3)
                realMixedCapture(folder.getChildFile(name), stored);
            else
                application(folder.getChildFile(name), stored, mode != 1, mode == 2);
            cases += name + "=completed\n";
        }
        catch (const std::exception &error)
        {
            if (!first)
                first = std::current_exception();
            cases += name + "=failed:" + juce::String(error.what()) + "\n";
        }
        require(folder.getChildFile("cases.txt").replaceWithText(cases),
                "Retain every independent native source outcome");
    }
    if (first)
        std::rethrow_exception(first);
}
} // namespace initial_source_epoch_acceptance
