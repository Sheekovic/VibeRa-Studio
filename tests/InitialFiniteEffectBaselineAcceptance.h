// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/AudioEngine.h"
#include "../src/PreparedAppMixerAudioPorts.h"
#include "../src/MixerRoutePlugins.h"
#include "StoppedEffectNativeAcceptance.h"
#include "InitialAppAudioPortFixture.h"
// Include after actual SyntheticDevice, callback heap probes and ParameterAcceptance.
// This ORIGINAL friend is the only issuer of the private finite-fixture capability.
namespace vibera
{
struct FiniteEffectBaselineFixture
{
    static juce::File sample(const juce::File &folder)
    {
        const auto path = folder.getChildFile("original-signed-stereo.wav");
        require(!path.exists(), "Never overwrite an existing source file");
        juce::AudioBuffer<float> pcm(2, 16384);
        std::fill_n(pcm.getWritePointer(0), pcm.getNumSamples(), .125f);
        std::fill_n(pcm.getWritePointer(1), pcm.getNumSamples(), -.0625f);
        juce::WavAudioFormat format;
        auto stream = path.createOutputStream();
        require(bool(stream), "Owned sample stream");
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "Original exact PCM24 fractions");
        writer.reset();
        double rate = 0;
        const auto decoded = readWav(path, rate);
        require(rate == 48000 && decoded.getNumChannels() == 2 && decoded.getNumSamples() == 16384,
                "Decoded source shape");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 16384; ++n)
                require(decoded.getSample(c, n) == (c == 0 ? .125f : -.0625f),
                        "All source samples independently exact");
        return path;
    }
    struct Voice
    {
        int on = -1, off = -1;
        double amplitude(int frame) const
        {
            if (on < 0 || frame < on)
                return 0;
            if (off < 0 || frame < off)
                return std::min(1., double(frame - on + 1) / 96.);
            return std::max(0., std::min(1., double(off - on) / 96.) - double(frame - off + 1) / 576.);
        }
    };
    static void raw(const juce::File &file, const juce::AudioBuffer<float> &data, int frames)
    {
        auto stream = file.createOutputStream();
        require(bool(stream), "Owned all-sample evidence stream");
        require(stream->setPosition(0) && stream->truncate().wasOk(), "Truncate exact PCM evidence");
        for (int c = 0; c < 2; ++c)
            require(stream->write(data.getReadPointer(c), std::size_t(frames) * sizeof(float)),
                    "Write all PCM");
        stream->flush();
        require(stream->getStatus().wasOk(), "Complete PCM evidence");
    }
    static void scenario(const juce::File &folder, const juce::File &binary, const PluginSpec &seed,
                         bool patternMode, int refusedFrames = 0)
    {
        using P = experimental::AppMixerAudioPorts;
        using Ready = te::EditPlaybackContext::ReadyGraphChangeStatus;
        require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned finite baseline scenario");
        test::routing_lifetime::Storage storage(folder.getChildFile("settings"));
        Controller controller;
        auto &project = controller.project;
        project.channels.resize(1);
        auto &channel = project.channels.front();
        const auto channelId = channel.id;
        channel.instrument.reset();
        channel.audioClip = false;
        channel.source = sample(folder).getFullPathName();
        channel.gain = 1;
        channel.pan = 0;
        channel.mixer = 1;
        channel.sampler.root = 60;
        channel.sampler.loop = true;
        channel.sampler.envelope = false;
        channel.sampler.normalize = false;
        project.tempo = 120;
        project.patterns.resize(1);
        const auto pattern = project.patterns[0].id;
        project.patterns[0].length = 64;
        project.patterns[0].notes.clear();
        project.placements = {{project.allocate(), pattern, 0, 0, 64, 0}};
        for (auto &m : project.mixer)
        {
            m.effects.clear();
            m.gain = 1;
            m.pan = 0;
            m.muted = false;
            m.effectsBypassed = false;
            m.stereoSeparation = 0;
            m.phaseInverted = false;
            m.stereoSwapped = false;
            m.routes.clear();
            if (m.index)
                m.routes.push_back({0, 1.f});
        }
        auto anchor = seed;
        anchor.id = project.allocate();
        project.mixer[2].effects = {anchor};
        require(project.validate().wasOk(), "Actual sample A and sole original finite anchor B");
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
                    device->getCurrentSampleRate() == 48000,
                "Actual48k256 device before any native/worker source context");
        const auto hash = juce::SHA256(binary).toHexString();
        const auto before = project.state();
        const auto revision = controller.revision;
        const auto badHash = hash.replaceSection(0, 1, hash.startsWithChar('0') ? "1" : "0");
        const auto refused = audio->prepareFiniteEffectBaseline(controller, anchor.id, binary, badHash);
        require(refused.failed() && refused.getErrorMessage().startsWith("finite.baseline.provider:") &&
                    !audio->finiteEffectBaseline && audio->projectionDiagnostics().edit == 0 &&
                    project.state().isEquivalentTo(before) && controller.revision == revision,
                "Mismatched trusted artifact refuses before authority/worker/native context mutation");
        require(audio->prepareFiniteEffectBaseline(controller, anchor.id, binary, hash).wasOk() &&
                    audio->projectionDiagnostics().edit == 0 && audio->currentHosts.empty() &&
                    !audio->initialSourceTransport,
                "Explicit true closed sample baseline is preparation, not worker/session/graph completion");
        require(awaitProjection(*audio, project, patternMode, pattern).wasOk(),
                "Actual ordinary worker preparation + native Edit");
        audio->setMidiTarget(channelId);
        audio->setLooping(false);
        audio->playStop();
        dispatchMessages(20);
        auto host = audio->parameterHost(anchor.id);
        require(host && host->alive(), "Actual anchor owned worker");
        const auto identity = host->runtimeIdentity();
        const auto pid = host->processId();
        const auto resets = host->midiResetGeneration();
        require(folder.getChildFile("inputs.txt")
                    .replaceWithText("module=" + binary.getFullPathName() + "\nsha256=" + hash +
                                     "\nsource=" + channel.source + "\nsourceSha256=" +
                                     juce::SHA256(juce::File(channel.source)).toHexString() +
                                     "\nstate=" + anchor.state + "\ninstance=" + identity +
                                     "\nPID=" + juce::String(pid) + "\n"),
                "Retain actual inputs before callbacks");
        audio->withFiniteEffectBaselineContext(
            controller,
            [&](te::Edit &edit, te::EditPlaybackContext &context, P &ports)
            {
                te::Edit::UndoTransactionInhibitor inhibitTimer(
                    edit); // Existing native timer, no ignored history fields.
                require(edit.initialSourceGeneration != 0 && edit.closedNativeIO &&
                            edit.sourceReferenceDomain() == te::Edit::SourceReferenceDomain::localClosed &&
                            !audio->initialSourceTransport && host->transportLatencySamples() == 256 &&
                            host->latencySamples() == 287,
                        "Actual separately owned LOCAL sample Session; ordinary1Q anchor, no Host "
                        "membership/K fiction");
                auto *head = context.getNodePlayHead();
                require(head, "Actual native head");
                require(context.initialSourcePhase() ==
                            te::EditPlaybackContext::InitialSourcePhase::waitingFirstSerial,
                        "Actual context is pending its genuine first physical source serial");
                const auto sourceBaseline = ports.finiteSourceObservation();
                require(sourceBaseline.known && !sourceBaseline.ready && sourceBaseline.frames == 0 &&
                            sourceBaseline.runtime &&
                            sourceBaseline.generation == edit.initialSourceGeneration,
                        "Configured actual source authority is not graph/PCM completion");
                juce::String sourceRows =
                    "phase,known,ready,fault,generation,serial,open,begins,ends,aborts,frames,referenceStart,"
                    "nativeStatus,nativeFailed,nativeOpen,nativeProcessed\n";
                const auto readSource = [&](const char *phase)
                {
                    const auto x = ports.finiteSourceObservation();
                    sourceRows += juce::String(phase) + "," + juce::String(int(x.known)) + "," +
                                  juce::String(int(x.ready)) + "," + juce::String(x.fault) + "," +
                                  juce::String(x.generation) + "," + juce::String(x.serial) + "," +
                                  juce::String(int(x.open)) + "," + juce::String(x.begins) + "," +
                                  juce::String(x.ends) + "," + juce::String(x.aborts) + "," +
                                  juce::String(x.frames) + "," + juce::String(x.referenceStart) + "," +
                                  juce::String(int(x.native.status)) + "," +
                                  juce::String(int(x.native.failed)) + "," +
                                  juce::String(int(x.native.scopeOpen)) + "," +
                                  juce::String(x.native.sourceProcessed) + "\n";
                    return x;
                };
                const auto origin = head->getReferenceSampleRange().getEnd();
                const auto baseline = audio->projectionDiagnostics();
                const auto starts = device->starts, stops = device->stops;
                const auto model = project.state().toXmlString();
                const auto units = controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
                const auto nativeHistory = edit.getUndoManager().getNumberOfUnitsTakenUpByStoredCommands();
                constexpr int capacity = 16384, Q = 256, D = 287;
                juce::AudioBuffer<float> block(2, Q), actual(2, capacity), expected(2, capacity);
                actual.clear();
                expected.clear();
                Voice physical, typing;
                int frames = 0, expectedKeys = 0;
                juce::String csv = "phase,frame,maxError,ms,new,delete,sourceCalls,headStart,headEnd,"
                                   "selected,posted,submitted,missed,dropped,held,portsFault,rootFault\n";
                juce::String midi = "phase,mixer,runtime,scope,readers,accepted,delivered,on,off,panics,held,"
                                    "ingressFault,terminalFault\n";
                const auto readMidi = [&](const char *phase, int mixer)
                {
                    const auto all = ports.trackMidiObservations();
                    const auto found =
                        std::find_if(all.begin(), all.end(), [=](const auto &x) { return x.mixer == mixer; });
                    require(found != all.end(), "Actual stable Mixer inbox/terminal observation");
                    const auto x = *found;
                    midi += juce::String(phase) + "," + juce::String(mixer) + "," +
                            juce::String::toHexString(juce::pointer_sized_int(x.runtime)) + "," +
                            juce::String::toHexString(juce::pointer_sized_int(x.scope)) + "," +
                            juce::String(x.readers) + "," + juce::String(x.ingress.accepted) + "," +
                            juce::String(x.ingress.delivered) + "," + juce::String(x.terminal.on) + "," +
                            juce::String(x.terminal.off) + "," + juce::String(x.terminal.panicMessages) +
                            "," + juce::String(x.terminal.active) + "," + juce::String(int(x.ingress.fault)) +
                            "," + juce::String(int(x.terminal.fault)) + "\n";
                    return x;
                };
                te::AudioTrack *nativeA = nullptr, *nativeB = nullptr;
                MixerMasterGate *original = nullptr;
                for (auto *t : te::getAudioTracks(edit))
                    if (t->state.hasProperty("viberaMixerIndex"))
                    {
                        if (int(t->state["viberaMixerIndex"]) == 1)
                        {
                            nativeA = t;
                            for (auto *p : t->pluginList)
                                if (auto *g = dynamic_cast<MixerMasterGate *>(p))
                                    original = g;
                        }
                        if (int(t->state["viberaMixerIndex"]) == 2)
                            nativeB = t;
                    }
                require(nativeA && nativeB && original, "Actual sourceA/anchorB Tracks and Gate");
                const auto baseA = readMidi("baseline", 1), baseB = readMidi("baseline", 2);
                const auto retain = [&]()
                {
                    // Preserve every collected sample/row. Successful callbacks
                    // remain in RAM; normal exit or an exception persists them.
                    if (frames > 0)
                    {
                        require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                                "Persist collected callbacks");
                        raw(folder.getChildFile("actual.f32le"), actual, frames);
                        raw(folder.getChildFile("expected.f32le"), expected, frames);
                    }
                    require(folder.getChildFile("source-session.csv").replaceWithText(sourceRows),
                            "Persist collected paired source/session observations");
                    require(folder.getChildFile("native-midi.csv").replaceWithText(midi),
                            "Persist collected MIDI facts");
                };
                const auto exceptionsOnEntry = std::uncaught_exceptions();
                const juce::ScopeGuard failureEvidence{[&]() noexcept
                                                       {
                                                           if (std::uncaught_exceptions() > exceptionsOnEntry)
                                                               try
                                                               {
                                                                   retain();
                                                               }
                                                               catch (...)
                                                               {
                                                                   // Best-effort diagnostics must not mask
                                                                   // the original failure.
                                                               }
                                                       }};
                const auto drive = [&](const char *phase)
                {
                    require(frames + Q <= capacity, "Bounded all-sample storage");
                    const auto beforeCall = audio->projectionDiagnostics();
                    const auto stats = host->statistics();
                    const auto allocations = callbackAllocations.load(),
                               deletions = callbackDestructions.load();
                    const auto began = juce::Time::getHighResolutionTicks();
                    {
                        const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
                        device->process(block);
                    }
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
                    const auto after = audio->projectionDiagnostics();
                    const auto packets = host->statistics();
                    const auto selection = context.getGraphSelection();
                    const auto range = head->getReferenceSampleRange();
                    double error = 0;
                    bool strictZero = true;
                    for (int n = 0; n < Q; ++n)
                    {
                        const auto sourceFrame = frames + n - D;
                        const auto voices = physical.amplitude(sourceFrame) + typing.amplitude(sourceFrame);
                        for (int c = 0; c < 2; ++c)
                        {
                            const auto want = (c == 0 ? .125 : -.0625) * voices;
                            const auto value = block.getSample(c, n);
                            actual.setSample(c, frames + n, value);
                            expected.setSample(c, frames + n, float(want));
                            error = std::isfinite(value) ? std::max(error, std::abs(double(value) - want))
                                                         : std::numeric_limits<double>::infinity();
                            // Exact startup and beyond the a-priori float-release bound.
                            const bool quiet = (physical.on < 0 || sourceFrame < physical.on ||
                                                (physical.off >= 0 && sourceFrame >= physical.off + 600)) &&
                                               (typing.on < 0 || sourceFrame < typing.on ||
                                                (typing.off >= 0 && sourceFrame >= typing.off + 600));
                            if (quiet)
                                strictZero &= value == 0;
                        }
                    }
                    const auto newCalls = callbackAllocations.load() - allocations,
                               deleteCalls = callbackDestructions.load() - deletions;
                    const auto port = ports.observe();
                    csv += juce::String(phase) + "," + juce::String(frames) + "," + juce::String(error, 12) +
                           "," + juce::String(ms, 6) + "," + juce::String(newCalls) + "," +
                           juce::String(deleteCalls) + "," +
                           juce::String(after.channels[0].renderCalls - beforeCall.channels[0].renderCalls) +
                           "," + juce::String(range.getStart()) + "," + juce::String(range.getEnd()) + "," +
                           juce::String(selection.selectedSerial) + "," +
                           juce::String(selection.postedSerial) + "," + juce::String(packets.submitted) +
                           "," + juce::String(packets.missed) + "," + juce::String(packets.dropped) + "," +
                           juce::String(after.channels[0].heldInputKeys) + "," +
                           juce::String(int(ports.terminalFaultControl())) + "," +
                           juce::String(int(ports.exchangeFault())) + "\n";
                    const auto expectedStart = origin + frames;
                    frames += Q;
                    const auto source = readSource(phase);
                    require(source.known && source.ready && !source.open && source.fault == 0 &&
                                source.aborts == 0 && source.generation == edit.initialSourceGeneration &&
                                source.runtime == sourceBaseline.runtime &&
                                source.frames == std::uint64_t(frames) && source.begins == source.ends &&
                                source.ends == std::uint64_t(frames / 256) &&
                                context.initialSourcePhase() ==
                                    te::EditPlaybackContext::InitialSourcePhase::active,
                            "Actual fullQ source/end/clock success, not selected-root alone or accepted-only "
                            "Session");
                    // Independent analytic attack/release. Two voices:576 additions
                    // at <=2^-24 each, *max amplitude.125, plus stereo sum/rounding
                    // gives <9e-6. Fixed1e-5 chosen BEFORE any runtime observation.
                    // One-frame attack or join displacement is >=.125/96 >1e-5.
                    require(
                        error <= 1e-5 && strictZero && newCalls == 0 && deleteCalls == 0 &&
                            ms <= 256000. / 48000.,
                        "Every actual startup/held/cancel/adoption/release sample + unchanged heap/deadline");
                    require(range.getStart() == expectedStart && range.getEnd() == expectedStart + Q &&
                                after.edit == baseline.edit &&
                                after.playbackContext == baseline.playbackContext &&
                                after.channels[0].sampler == baseline.channels[0].sampler &&
                                after.channels[0].sample == baseline.channels[0].sample &&
                                after.channels[0].graphInitialisations ==
                                    baseline.channels[0].graphInitialisations &&
                                after.channels[0].renderCalls == beforeCall.channels[0].renderCalls + 1 &&
                                after.channels[0].heldInputKeys == expectedKeys && device->starts == starts &&
                                device->stops == stops && audio->playing() && !port.nonemptyAudioMidi &&
                                !ports.terminalFaultControl() &&
                                ports.exchangeFault() == experimental::SourceExchangeContinuity::Fault::none,
                            "Actual source/PCM/clock/context/device/ownership retained once");
                    require(host->alive() && host->runtimeIdentity() == identity &&
                                host->processId() == pid && host->midiResetGeneration() == resets &&
                                packets.submitted == stats.submitted + 1 && packets.missed == 0 &&
                                packets.dropped == 0 && packets.midiOverflow == 0 &&
                                project.state().toXmlString() == model && controller.revision == revision &&
                                controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units &&
                                edit.getUndoManager().getNumberOfUnitsTakenUpByStoredCommands() ==
                                    nativeHistory,
                            "Actual unused finite anchor processes once; source/native history and worker "
                            "identity unchanged");
                    dispatchMessages(6); // Existing serialized CONTROL pacing, never DSP wait.
                };
                if (refusedFrames > 0)
                {
                    const auto beforeCall = audio->projectionDiagnostics();
                    const auto reference = head->getReferenceSampleRange();
                    const auto packets = host->statistics();
                    const auto packetFrame = host->packetFrame();
                    nativeA->injectLiveMidiMessage(juce::MidiMessage::noteOn(3, 67, juce::uint8(100)),
                                                   te::MPESourceID(901));
                    nativeB->injectLiveMidiMessage(juce::MidiMessage::noteOn(5, 71, juce::uint8(100)),
                                                   te::MPESourceID(902));
                    const auto pendingA = readMidi("before_refused_span", 1),
                               pendingB = readMidi("before_refused_span", 2);
                    require(pendingA.ingress.accepted == baseA.ingress.accepted + 1 &&
                                pendingA.ingress.delivered == baseA.ingress.delivered &&
                                pendingB.ingress.accepted == baseB.ingress.accepted + 1 &&
                                pendingB.ingress.delivered == baseB.ingress.delivered,
                            "Both actual inboxes have accepted-undelivered MIDI BEFORE physical refusal");
                    audio->midiInput.handleIncomingMidiMessage(
                        nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                    juce::AudioBuffer<float> rejected(2, refusedFrames);
                    rejected.clear();
                    const auto allocations = callbackAllocations.load(),
                               deleted = callbackDestructions.load();
                    const auto began = juce::Time::getHighResolutionTicks();
                    {
                        const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
                        device->process(rejected);
                    }
                    const auto elapsed =
                        SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
                    const bool literalDeadlineMet = elapsed <= 1000. * refusedFrames / 48000.;
                    // This is a genuine synthetic OUTER one-frame refusal, not
                    // an internal slice. Keep its literal measurement. Only an
                    // unadvertised one-frame stress deadline is unqualified;
                    // every advertised size and all other guards stay strict.
                    const bool requiredDeadline =
                        refusedFrames != 1 || device->getAvailableBufferSizes().contains(1);
                    const auto afterCall = audio->projectionDiagnostics();
                    const auto source = readSource("refused_actual_physical_span");
                    const auto afterA = readMidi("after_refused_span", 1),
                               afterB = readMidi("after_refused_span", 2);
                    const auto range = head->getReferenceSampleRange();
                    const auto now = host->statistics();
                    bool silence = true;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < refusedFrames; ++n)
                            silence &= rejected.getSample(c, n) == 0;
                    raw(folder.getChildFile("refused-actual.f32le"), rejected, refusedFrames);
                    require(folder.getChildFile("refusal.txt")
                                .replaceWithText(
                                    "requested=" + juce::String(refusedFrames) +
                                    " ms=" + juce::String(elapsed, 9) +
                                    " deadlineMs=" + juce::String(1000. * refusedFrames / 48000., 9) +
                                    " literalDeadlineMet=" + juce::String(int(literalDeadlineMet)) +
                                    " requiredDeadline=" + juce::String(int(requiredDeadline)) +
                                    " new=" + juce::String(callbackAllocations.load() - allocations) +
                                    " delete=" + juce::String(callbackDestructions.load() - deleted) +
                                    " sourceCallsBefore=" + juce::String(beforeCall.channels[0].renderCalls) +
                                    " sourceCallsAfter=" + juce::String(afterCall.channels[0].renderCalls) +
                                    " hostSubmittedBefore=" + juce::String(packets.submitted) +
                                    " hostSubmittedAfter=" + juce::String(now.submitted) + "\n"),
                            "Retain all refusal facts BEFORE assertions");
                    require(source.known && !source.ready && source.native.failed &&
                                !source.native.scopeOpen && source.frames == 0 && source.begins == 0 &&
                                source.ends == 0 &&
                                source.native.status ==
                                    (refusedFrames == 1
                                         ? te::SourceBurstSession::Status::observerRefused
                                         : te::SourceBurstSession::Status::physicalBurstExceeded) &&
                                (refusedFrames != 1 || source.fault == 1) &&
                                context.initialSourcePhase() ==
                                    te::EditPlaybackContext::InitialSourcePhase::failed,
                            "Genuine actual physical1/257 refuses fullQ source authority, never pending "
                            "forever/success");
                    const bool refusalSafe =
                        silence && callbackAllocations.load() == allocations &&
                        callbackDestructions.load() == deleted && range == reference &&
                        beforeCall.channels[0].renderCalls == afterCall.channels[0].renderCalls &&
                        beforeCall.channels[0].lastTime == afterCall.channels[0].lastTime &&
                        afterCall.channels[0].heldInputKeys == 0 && now.submitted == packets.submitted &&
                        host->packetFrame() == packetFrame &&
                        afterA.ingress.accepted == pendingA.ingress.accepted &&
                        afterA.ingress.delivered == pendingA.ingress.delivered &&
                        afterB.ingress.accepted == pendingB.ingress.accepted &&
                        afterB.ingress.delivered == pendingB.ingress.delivered && host->alive() &&
                        host->runtimeIdentity() == identity && host->processId() == pid &&
                        project.state().toXmlString() == model && controller.revision == revision;
                    require(
                        folder.getChildFile("refusal-safety.txt")
                            .replaceWithText("refusalSafe=" + juce::String(int(refusalSafe)) +
                                             "; includes exact source/head/input/Host/cursor/model/history "
                                             "and zero heap\n"),
                        "Retain independently evaluated refusal safety apart from timing");
                    require(refusalSafe, "Actual refusal BEFORE source/input/head/Host/inbox work, zero heap "
                                         "and exact incumbent preservation");
                    require(!requiredDeadline || literalDeadlineMet,
                            "Every supported/other physical refusal deadline remains unchanged");
                    retain();
                    return; // Terminal owned experiment; no implicit rearm/recovery or oracle retry.
                }

                for (int n = 0; n < 4; ++n)
                    drive("all_startup_samples");
                audio->midiInput.capture.begin();
                audio->typingInput.capture.begin();
                physical.on = frames;
                expectedKeys = 1;
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("physical_on_all_samples");
                drive("physical_attack_and287_join");
                typing.on = frames;
                expectedKeys = 2;
                audio->typingInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("typing_same_pitch_on");
                drive("both_owners_held");
                const auto rearm = audio->prepareFiniteEffectBaseline(controller, anchor.id, binary, hash);
                require(rearm.failed() && rearm.getErrorMessage().startsWith("finite.baseline.owner:"),
                        "A warm incumbent cannot acquire/rearm an initial finite baseline");
                drive("FIRST_incumbent_after_warm_rearm_refusal");
                auto structural = project;
                structural.channels[0].mixer = 2;
                const auto refusedRoute = audio->rebuild(structural, patternMode, pattern);
                require(refusedRoute.failed() &&
                            refusedRoute.getErrorMessage().startsWith("finite.baseline.initialOnly:") &&
                            project.state().toXmlString() == model && controller.revision == revision,
                        "No new route/membership/append authority from a finite baseline prerequisite");
                drive("FIRST_incumbent_after_structural_refusal");
                const auto make = [&]()
                {
                    auto candidate = MixerMasterGate::createDetachedGraph(edit, original->state.createCopy());
                    auto *gate = dynamic_cast<MixerMasterGate *>(candidate.get());
                    require(gate, "Actual detached Gate");
                    gate->configure(original->timelineReference(), 1, 1.f);
                    require(gate->shareCompatibleRuntimeFrom(*original).wasOk(),
                            "Exact retained Gate Runtime, no source/Host replacement");
                    auto lists = std::make_shared<std::map<const te::PluginList *, te::Plugin::Array>>();
                    const auto add = [&](const te::PluginList &list)
                    {
                        auto copy = list.getPlugins();
                        for (int n = 0; n < copy.size(); ++n)
                            if (copy[n].get() == original)
                                copy.set(n, candidate);
                        lists->emplace(&list, std::move(copy));
                    };
                    add(edit.getMasterPluginList());
                    for (auto *t : te::getAudioTracks(edit))
                    {
                        add(t->pluginList);
                        for (auto *c : t->getClips())
                            if (auto *list = c->getPluginList())
                                add(*list);
                    }
                    auto overlay = std::make_shared<te::EditPlaybackContext::GraphOverlay>();
                    overlay->plugins = [lists](const te::PluginList &list) { return lists->at(&list); };
                    auto exact = std::make_shared<te::Plugin::Array>(edit.getPluginCache().getPlugins());
                    exact->addIfNotAlreadyThere(edit.getMasterVolumePlugin().get());
                    exact->add(candidate);
                    overlay->fixedInitialisationAdmission = [exact](const te::Plugin &plugin)
                    {
                        if (!exact->contains(const_cast<te::Plugin *>(&plugin)))
                            throw std::invalid_argument("finite.fixture.foreignPlugin");
                        return true;
                    };
                    return std::pair{overlay, candidate};
                };
                const auto inject = [&](int mixer, const juce::MidiMessage &message)
                {
                    const auto before = readMidi("before_enqueue", mixer);
                    (mixer == 1 ? nativeA : nativeB)
                        ->injectLiveMidiMessage(message, te::MPESourceID(900 + mixer));
                    const auto after = readMidi("accepted_not_delivered", mixer);
                    require(after.runtime == before.runtime &&
                                after.ingress.accepted == before.ingress.accepted + 1 &&
                                after.ingress.delivered == before.ingress.delivered,
                            "Genuine pending original Track inbox");
                };
                const auto selected = context.getGraphSelection().selectedSerial;
                const auto attachments = original->runtimeAttachmentsControl();
                {
                    auto [overlay, candidate] = make();
                    auto ready = context.prepareGraphOnly(overlay);
                    require(ready && context.validatePreparedGraph(*ready) == Ready::ready,
                            "Actual private finite baseline native graph");
                    inject(1, juce::MidiMessage::noteOn(3, 67, juce::uint8(100)));
                    inject(2, juce::MidiMessage::noteOn(5, 71, juce::uint8(100)));
                    ready.reset();
                    candidate = nullptr;
                }
                require(context.getGraphSelection().selectedSerial == selected &&
                            original->runtimeAttachmentsControl() == attachments &&
                            readMidi("after_cancel_before_callback", 1).readers == baseA.readers &&
                            readMidi("after_cancel_before_callback", 2).readers == baseB.readers,
                        "Cancel preserves actual native graph/readers/history");
                drive("FIRST_incumbent_after_cancel_pending_MIDI");
                auto a = readMidi("delivered_once_A", 1), b = readMidi("delivered_once_B", 2);
                require(
                    a.ingress.accepted == a.ingress.delivered && b.ingress.accepted == b.ingress.delivered &&
                        a.terminal.on == baseA.terminal.on + 1 && b.terminal.on == baseB.terminal.on + 1 &&
                        a.terminal.active == 1 && b.terminal.active == 1,
                    "Real independent inboxes delivered to real bounded terminals");
                auto [overlay, candidate] = make();
                auto ready = context.prepareGraphOnly(overlay);
                const auto serial = ready->publicationSerial();
                inject(1, juce::MidiMessage::noteOff(3, 67));
                inject(2, juce::MidiMessage::allNotesOff(5));
                require(context.publishPreparedGraph(*ready) == Ready::ready && ready->wasPublished() &&
                            context.getGraphSelection().selectedSerial == selected,
                        "Posted native candidate is not selected before callback");
                drive("FIRST_actual_same_owner_adoption");
                a = readMidi("selected_off_A", 1);
                b = readMidi("selected_panic_B", 2);
                require(context.getGraphSelection().selectedSerial == serial && a.runtime == baseA.runtime &&
                            b.runtime == baseB.runtime && a.ingress.accepted == a.ingress.delivered &&
                            b.ingress.accepted == b.ingress.delivered &&
                            a.terminal.off == baseA.terminal.off + 1 &&
                            b.terminal.panicMessages == baseB.terminal.panicMessages + 1 &&
                            a.terminal.active == 0 && b.terminal.active == 0,
                        "Exact actual selected graph retains pending Off/panic and private held voices");
                ready.reset();
                drive("after_CONTROL_retirement");
                physical.off = frames;
                expectedKeys = 1;
                audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("physical_off_typing_still_held");
                drive("independent_physical_release");
                drive("typing_only");
                typing.off = frames;
                expectedKeys = 0;
                audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                for (int n = 0; n < 6; ++n)
                    drive("all_release_and287_tail_samples");
                initial_app_ports_acceptance::captured(audio->midiInput, channelId);
                initial_app_ports_acceptance::captured(audio->typingInput, channelId);
                Controller foreign;
                foreign.project = project;
                bool called = false, ownerRefused = false;
                try
                {
                    audio->withFiniteEffectBaselineContext(foreign,
                                                           [&](auto &, auto &, auto &) { called = true; });
                }
                catch (const std::invalid_argument &e)
                {
                    ownerRefused = juce::String(e.what()).startsWith("finite.baseline.context:");
                }
                require(ownerRefused && !called,
                        "Identical foreign Controller cannot borrow actual baseline authority");
                drive("FIRST_incumbent_after_foreign_owner_refusal");
                if (refusedFrames < 0)
                {
                    const auto beforeCall = audio->projectionDiagnostics();
                    const auto old = readSource("before_actual_seek");
                    const auto rootGeneration = ports.completedExchangeGeneration();
                    const auto stats = host->statistics();
                    const auto selected = context.getGraphSelection().selectedSerial;
                    audio->seekBeat(.25); // Genuine native cursor post, not a fabricated clock/receipt.
                    const auto allocations = callbackAllocations.load(),
                               deleted = callbackDestructions.load();
                    const auto began = juce::Time::getHighResolutionTicks();
                    {
                        const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
                        device->process(block);
                    }
                    const auto elapsed =
                        SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
                    const auto source = readSource("actual_postchild_end_clock_refusal");
                    const auto afterCall = audio->projectionDiagnostics();
                    const auto now = host->statistics();
                    bool silence = true;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < 256; ++n)
                            silence &= block.getSample(c, n) == 0;
                    raw(folder.getChildFile("end-refusal-actual.f32le"), block, 256);
                    require(folder.getChildFile("end-refusal.txt")
                                .replaceWithText(
                                    "beforeSourceFrames=" + juce::String(old.frames) +
                                    " afterSuccessfulFrames=" + juce::String(source.frames) +
                                    " beforeSourceCalls=" + juce::String(beforeCall.channels[0].renderCalls) +
                                    " afterSourceCalls=" + juce::String(afterCall.channels[0].renderCalls) +
                                    " submittedBefore=" + juce::String(stats.submitted) + " submittedAfter=" +
                                    juce::String(now.submitted) + " ms=" + juce::String(elapsed, 9) +
                                    " rootBefore=" + juce::String(rootGeneration) +
                                    " rootAfter=" + juce::String(ports.completedExchangeGeneration()) + "\n"),
                            "Retain genuine processed-prefix/end-failure facts BEFORE assertions");
                    require(source.known && !source.ready && source.fault == 2 && !source.open &&
                                source.aborts == 1 && source.begins == source.ends + 1 &&
                                source.frames == old.frames && source.native.failed &&
                                !source.native.scopeOpen &&
                                source.native.status == te::SourceBurstSession::Status::observerRefused &&
                                afterCall.channels[0].renderCalls == beforeCall.channels[0].renderCalls + 1 &&
                                now.submitted == stats.submitted + 1 && host->alive() &&
                                host->runtimeIdentity() == identity && silence &&
                                callbackAllocations.load() == allocations &&
                                callbackDestructions.load() == deleted && elapsed <= 256000. / 48000. &&
                                context.getGraphSelection().selectedSerial == selected &&
                                ports.completedExchangeGeneration() == rootGeneration &&
                                project.state().toXmlString() == model,
                            "Real end refusal AFTER a processed prefix clears physical PCM; no "
                            "rollback/completion claim");
                    retain();
                    return; // Terminal source fault, no implicit recovery/rearm.
                }

                // Genuine device setup/AsyncUpdater, not a direct test fence call.
                const auto *oldContext = &context;
                const auto oldEdit = audio->projectionDiagnostics().edit;
                const auto oldGraph = context.getGraphSelection().selectedSerial;
                const auto initialisations = audio->mixerGraphInitialisations();
                auto changed = manager.deviceManager.getAudioDeviceSetup();
                changed.sampleRate = 44100.;
                changed.bufferSize = 256;
                require(manager.deviceManager.setAudioDeviceSetup(changed, true).isEmpty(),
                        "Actual device-change request");
                dispatchMessages(40);
                const auto afterFence = audio->projectionDiagnostics();
                // Read only from the CURRENT actual owned context, never a stale
                // borrowed pre-device pointer if the refusal contract regresses.
                auto *currentContext =
                    audio->edit ? audio->edit->getTransport().getCurrentPlaybackContext() : nullptr;
                const auto retainedSerial =
                    currentContext == oldContext ? currentContext->getGraphSelection().selectedSerial : 0;
                const bool fenceRefused =
                    manager.getPreparationFenceStatus() == te::DeviceManager::PreparationFenceStatus::refused;
                require(
                    folder.getChildFile("device-refusal.txt")
                        .replaceWithText(
                            "refused=" + juce::String(int(fenceRefused)) +
                            " editBefore=" + juce::String(juce::uint64(oldEdit)) +
                            " editAfter=" + juce::String(juce::uint64(afterFence.edit)) + " contextBefore=" +
                            juce::String::toHexString(juce::pointer_sized_int(oldContext)) +
                            " contextAfter=" + juce::String(juce::uint64(afterFence.playbackContext)) + "\n"),
                    "Retain actual fence outcome before assertions");
                require(
                    !ports.finiteSourceObservation().ready && fenceRefused && afterFence.edit == oldEdit &&
                        afterFence.playbackContext == std::uintptr_t(oldContext) &&
                        retainedSerial == oldGraph && audio->mixerGraphInitialisations() == initialisations &&
                        host->alive() && host->runtimeIdentity() == identity &&
                        audio->validateFiniteEffectBaseline(project).failed(),
                    "Device fence refuses BEFORE native release/reinitialise; terminal baseline is not "
                    "false recovery");
                retain();
            });
    }
};
} // namespace vibera

__declspec(noinline) static void initialFiniteEffectBaselineTests(const juce::File &folder,
                                                                  const juce::File &originalBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned original finite proof root");
    // CTest MUST supply exactly its own source-built VibeRaMixerLatencyFixture
    // target. No arbitrary installed-provider branch or name-based finite grant.
    const auto seed = vibera::test::stopped_effects::seedAndDirectOracle(folder, originalBinary);
    std::exception_ptr failure;
    for (bool pattern : {true, false})
    {
        for (const int refusedFrames : {0, 1, 257, -1})
        {
            const auto output = folder.getChildFile(
                (pattern ? "pattern" : "song") + juce::String(refusedFrames ? "-refuse-" : "") +
                (refusedFrames ? juce::String(refusedFrames) : juce::String()));
            try
            {
                vibera::FiniteEffectBaselineFixture::scenario(output, originalBinary, seed, pattern,
                                                              refusedFrames);
            }
            catch (const std::exception &e)
            {
                output.getChildFile("failure.txt").replaceWithText(e.what());
                if (!failure)
                    failure = std::current_exception();
            }
            catch (...)
            {
                output.getChildFile("failure.txt").replaceWithText("Unknown exception");
                if (!failure)
                    failure = std::current_exception();
            }
        }
    }
    if (failure)
        std::rethrow_exception(failure);
}
