// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RetainedInstrumentContextFixture.h"
#include "RetainedContextGraphAcceptance.h"
// Include after ParameterAcceptance/awaitProjection/SyntheticDevice/heap probes.
// SOURCE-ONLY experiment: serialized CONTROL/callback driving, not concurrency.
__declspec(noinline) static void retainedInstrumentContextTests(const juce::File &folder,
                                                                const PluginSpec &sourceSpec,
                                                                bool exactOriginal = true)
{
    require(folder.createDirectory().wasOk(), "owned same-Edit instrument graph evidence directory");
    require(sourceSpec.instrument &&
                (!exactOriginal || sourceSpec.name == "VibeRa Instrument Runtime Fixture"),
            "genuine scanned original or explicitly requested installed instrument");
    struct Storage
    {
        juce::String old = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &path)
        {
            require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                               path.getFullPathName().toWideCharPointer()) == 0,
                    "owned native settings/cache");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", old.toWideCharPointer());
        }
    } storage(folder.getChildFile("storage"));
    for (const bool patternMode : {true, false})
    {
        const auto output = folder.getChildFile(patternMode ? "pattern" : "song");
        require(!output.exists(), "fresh owned attempt must preserve any prior graph evidence");
        require(output.createDirectory().wasOk(), "owned instrument graph mode output");
        require(output.getChildFile("provider-descriptor.xml").replaceWithText(sourceSpec.descriptor),
                "retain actual scanned provider descriptor independently of fixture naming");
        Controller controller;
        auto &project = controller.project;
        project.channels.resize(1);
        auto &channel = project.channels.front();
        auto spec = sourceSpec;
        spec.id = project.allocate();
        channel.instrument = spec;
        channel.mixer = 1;
        channel.gain = 1;
        channel.pan = 0;
        for (auto &insert : project.mixer)
        {
            insert.gain = 1;
            insert.pan = 0;
            insert.effects.clear();
        }
        project.patterns.front().length = 64;
        project.patterns.front().notes.clear();
        const auto pattern = project.patterns.front().id;
        project.placements = {{project.allocate(), pattern, 0, 0, 64, 0}};
        require(project.validate().wasOk(), "genuine independent Pattern/Song external instrument project");
        // Original provider admits <=256 physical frames. Full native source
        // envelope ceil(1.1*256)=282 requires K=3 and literal768 transport.
        constexpr int workerDelay = 768;
        auto engine = std::make_unique<AudioEngine>(false, output.getChildFile("guide.json"), 256);
        auto &audio = *engine;
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
        require(device && device->getCurrentSampleRate() == 48000 &&
                    device->getCurrentBufferSizeSamples() == 256 &&
                    te::getDefaultPanLaw() == te::PanLawLinear,
                "unchanged configured48k256 synthetic device and linear pan");
        require(awaitProjection(audio, project, patternMode, pattern).wasOk(),
                "actual production external instrument projection");
        const auto host = audio.parameterHost(spec.id);
        require(host && host->alive(), "actual projected instrument worker available");
        require(!exactOriginal ||
                    (host->latencySamples() == workerDelay && host->declaredLatency().nativeSamples == 0),
                "independent native0 plus production3Q768 latency");
        if (exactOriginal)
        {
            auto metadata = parameterCatalogue(*host);
            require(parameterResult(*host, host->setParameter(77, .25f, metadata.generation)).result == 0,
                    "actual native manual gain acknowledged before graph allocation in BOTH modes");
            metadata = parameterCatalogue(*host, true);
            const auto &gain = nativeParameter(metadata, 77);
            require(gain.value == .25f,
                    "actual native manual gain readback is independent of Song automation");
            CommandArgs initial;
            initial.target = spec.id;
            initial.value = .25;
            initial.binding =
                PluginBinding{gain.id, gain.flags, gain.steps, gain.value, gain.name, gain.label};
            require(controller.execute("automation.plugin.init", initial).wasOk() &&
                        awaitProjection(audio, project, patternMode, pattern).wasOk(),
                    "canonical native gain binding/initialization and actual prepared automation authority");
        }
        CommandArgs tempo;
        tempo.value = 121;
        require(controller.execute("project.tempo", tempo).wasOk() && controller.undoEditChecked().wasOk() &&
                    controller.undo.canRedo(),
                "real canonical history cursor retained during graph-only experiment");
        juce::String log = "Serialized actual same-Edit instrument graph experiment. Device48000/256, "
                           "worker768; variable dispatched lengths127/64/65/129.\n";
        log +=
            exactOriginal
                ? "Original independent signed PCM/retrigger oracle: acknowledged native manual gain.25; one "
                  "pitch60 on, shared physical+typing ownership, worker latency768; no phase/gain fitting. "
                  "Pattern has NO Song/native automation lanes; Song has one native gain event per quantum.\n"
                : "Installed instrument compatibility branch: finite/nonzero PCM and exact raw "
                  "owner/lifecycle/MIDI readback only; no exact DSP or listening claim.\n";
        juce::String csv =
            "phase,frame,frames,cursorBefore,cursorAfter,sourceCalls,submitted,processed,"
            "missed,dropped,parametersApplied,held,markerCalls,maxError,callbackMs,newCalls,"
            "deleteCalls,headAdvance,headStart,referenceStart,referenceEnd,sameContext,samePlayhead\n";
        juce::AudioBuffer<float> captured(2, 65536);
        captured.clear();
        std::array<std::unique_ptr<juce::AudioBuffer<float>>, 5> buffers;
        const std::array<int, 5> sizes{256, 127, 64, 65, 129};
        for (std::size_t n = 0; n < sizes.size(); ++n)
            buffers[n] = std::make_unique<juce::AudioBuffer<float>>(2, sizes[n]);
        int framesWritten = 0, previousFrames = 0;
        juce::int64 onFrame = -1, offFrame = -1;
        bool passed = true;
        float providerPeak = 0;
        std::vector<te::Plugin::Ptr> shells;
        shells.reserve(16);
        try
        {
            audio.withRetainedInstrumentContextFixture(
                spec.id,
                [&](RetainedInstrumentContextFixture &fixture)
                {
                    // Freeze only native idle transaction-boundary housekeeping
                    // for this one controlled experiment. Its 350ms timer can
                    // otherwise reset the current-action counter without any
                    // graph/model mutation. Real actions, units, native state
                    // and canonical Undo/Redo remain compared in full.
                    te::Edit::UndoTransactionInhibitor nativeHistoryBatch(fixture.edit);
                    using Context = te::EditPlaybackContext;
                    using Status = Context::ReadyGraphChangeStatus;
                    require(fixture.worker == host,
                            "graph fixture uses the exact acknowledged prepared instrument worker");
                    const auto pid = fixture.worker->processId();
                    const auto instance = fixture.worker->runtimeIdentity();
                    const auto reset = fixture.worker->midiResetGeneration();
                    const auto starts = device->starts, stops = device->stops;
                    const auto *head = fixture.context.getNodePlayHead();
                    require(head != nullptr, "actual native playhead exists");
                    const auto baselineStats = fixture.worker->statistics();
                    auto history = controller.undo.prepareRedoMutation();
                    require(bool(history), "real history metadata guard");
                    auto canonical = [&]
                    {
                        return controller.project.state().toXmlString() + "/" + controller.projectIdentity +
                               "/" + juce::String(juce::int64(controller.revision)) + "/" +
                               juce::String(int(controller.dirty)) + "/" +
                               juce::String(controller.undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
                               controller.undo.getUndoDescriptions().joinIntoString("|") + "/" +
                               controller.undo.getRedoDescriptions().joinIntoString("|") + "/" +
                               juce::String(int(controller.undo.isPreparedMutationCurrent(*history)));
                    };
                    const auto capture = [&]
                    { return fixture.captureOwners() + "\ncanonical=" + canonical(); };
                    shells.push_back(fixture.original);
                    te::Plugin::Ptr active = fixture.original;
                    std::shared_ptr<vibera::test::retained_context::OutputObservation> marker;
                    juce::int64 referenceOrigin = -1;
                    const auto sourceCalls = [&]
                    {
                        std::uint64_t count = 0;
                        for (const auto &p : shells)
                            count += fixture.sourceCalls(p);
                        return count;
                    };
                    const auto drive = [&](const char *phase, int frames)
                    {
                        require(framesWritten + frames <= captured.getNumSamples(),
                                "bounded exact actual-output storage");
                        juce::AudioBuffer<float> *buffer = nullptr;
                        for (std::size_t n = 0; n < sizes.size(); ++n)
                            if (sizes[n] == frames)
                                buffer = buffers[n].get();
                        require(buffer != nullptr, "callback buffer was prepared before processing");
                        const auto beforeSource = sourceCalls();
                        const auto beforeActive = fixture.sourceCalls(active);
                        const auto beforeStats = fixture.worker->statistics();
                        const int beforeCursor = fixture.worker->packetFrame();
                        const auto beforeHead = audio.projectionDiagnostics();
                        const auto beforeReference = head->getReferenceSampleRange();
                        const auto beforeAlloc = callbackAllocations.load(),
                                   beforeDelete = callbackDestructions.load();
                        const auto began = juce::Time::getHighResolutionTicks();
                        {
                            struct Probe
                            {
                                Probe()
                                {
                                    callbackAllocationProbe = true;
                                }
                                ~Probe()
                                {
                                    callbackAllocationProbe = false;
                                }
                            } probe;
                            device->process(*buffer);
                        }
                        const auto ms = SyntheticCallbackTiming::milliseconds(
                            began, juce::Time::getHighResolutionTicks());
                        const auto allocations = callbackAllocations.load() - beforeAlloc,
                                   deletes = callbackDestructions.load() - beforeDelete;
                        const auto afterHead = audio.projectionDiagnostics();
                        const auto headAdvance =
                            std::llround((afterHead.headSeconds - beforeHead.headSeconds) * 48000.);
                        // Native PlayHead reports the START of its current
                        // reference range. For variable buffers the observed
                        // start advances by the PRECEDING span, not this span.
                        // Assert absolute known clock positions too: accepting
                        // an arbitrary zero advance could hide a restart.
                        const auto headStart = head->getPosition();
                        const auto reference = head->getReferenceSampleRange();
                        const auto selected = fixture.context.getGraphSelection();
                        const bool exactClock =
                            std::llround(beforeHead.headSeconds * 48000.) == framesWritten - previousFrames &&
                            headStart == framesWritten && referenceOrigin >= 0 &&
                            (framesWritten == 0
                                 ? beforeReference.getStart() == referenceOrigin
                                 : beforeReference.getEnd() == referenceOrigin + framesWritten) &&
                            reference.getStart() == referenceOrigin + framesWritten &&
                            reference.getEnd() == referenceOrigin + framesWritten + frames &&
                            selected.state == tracktion::graph::GraphSelectionState::selected &&
                            selected.referenceStart == reference.getStart() &&
                            selected.referenceEnd == reference.getEnd();
                        const auto sourceDelta = sourceCalls() - beforeSource,
                                   activeDelta = fixture.sourceCalls(active) - beforeActive;
                        const int afterCursor = fixture.worker->packetFrame();
                        double maximumError = 0;
                        bool finite = true;
                        for (int c = 0; c < 2; ++c)
                        {
                            captured.copyFrom(c, framesWritten, *buffer, c, 0, frames);
                            for (int n = 0; n < frames; ++n)
                            {
                                const float value = buffer->getSample(c, n);
                                finite &= std::isfinite(value);
                                const auto sourceFrame = juce::int64(framesWritten + n) - workerDelay;
                                const bool held = onFrame >= 0 && sourceFrame >= onFrame &&
                                                  (offFrame < 0 || sourceFrame < offFrame);
                                const double expected = !held    ? 0
                                                        : c == 0 ? .25 * (61. / 512 + 1. / 4096)
                                                                 : -.25 * (68. / 512 + 1. / 8192);
                                if (exactOriginal)
                                    maximumError = std::max(maximumError, std::abs(double(value) - expected));
                                if (held && std::isfinite(value))
                                    providerPeak = std::max(providerPeak, std::abs(value));
                            }
                        }
                        framesWritten += frames;
                        // Explicit serialized pacing/readback on CONTROL, not a
                        // wait, message pump or worker call inside the callback.
                        dispatchMessages(6);
                        fixture.worker->service();
                        const auto afterStats = fixture.worker->statistics();
                        const bool sameContext =
                            afterHead.edit == beforeHead.edit &&
                            afterHead.playbackContext == beforeHead.playbackContext &&
                            afterHead.playbackContext == reinterpret_cast<std::uintptr_t>(&fixture.context);
                        const bool sameHead = fixture.context.getNodePlayHead() == head;
                        const auto packets = juce::int64(framesWritten / 256);
                        csv += juce::String(phase) + "," + juce::String(framesWritten - frames) + "," +
                               juce::String(frames) + "," + juce::String(beforeCursor) + "," +
                               juce::String(afterCursor) + "," + juce::String(juce::int64(sourceDelta)) +
                               "," + juce::String(juce::int64(afterStats.submitted)) + "," +
                               juce::String(juce::int64(afterStats.processed)) + "," +
                               juce::String(juce::int64(afterStats.missed)) + "," +
                               juce::String(juce::int64(afterStats.dropped)) + "," +
                               juce::String(juce::int64(afterStats.parametersApplied)) + "," +
                               juce::String(fixture.heldKeys(active)) + "," +
                               juce::String(marker ? marker->calls.load() : 0) + "," +
                               juce::String(maximumError, 12) + "," + juce::String(ms, 6) + "," +
                               juce::String(allocations) + "," + juce::String(deletes) + "," +
                               juce::String(headAdvance) + "," + juce::String(headStart) + "," +
                               juce::String(reference.getStart()) + "," + juce::String(reference.getEnd()) +
                               "," + juce::String(int(sameContext)) + "," + juce::String(int(sameHead)) +
                               "\n";
                        const bool ok = finite && (!exactOriginal || maximumError == 0) && sourceDelta == 1 &&
                                        activeDelta == 1 && beforeCursor == (framesWritten - frames) % 256 &&
                                        afterCursor == framesWritten % 256 &&
                                        afterStats.submitted - beforeStats.submitted ==
                                            std::uint64_t((beforeCursor + frames) / 256) &&
                                        afterStats.submitted == baselineStats.submitted + packets &&
                                        afterStats.processed == afterStats.submitted &&
                                        afterStats.missed == baselineStats.missed &&
                                        afterStats.dropped == baselineStats.dropped &&
                                        afterStats.parametersRejected == baselineStats.parametersRejected &&
                                        (!exactOriginal ||
                                         afterStats.parametersApplied ==
                                             baselineStats.parametersApplied + (patternMode ? 0 : packets)) &&
                                        allocations == 0 && deletes == 0 && ms <= 1000. * frames / 48000. &&
                                        headAdvance == previousFrames && exactClock && sameContext &&
                                        sameHead && afterHead.headPlaying && afterHead.transportPlaying &&
                                        fixture.worker->alive() && !fixture.worker->deliveryFailed() &&
                                        fixture.worker->processId() == pid &&
                                        fixture.worker->runtimeIdentity() == instance &&
                                        fixture.worker->midiResetGeneration() == reset &&
                                        (!exactOriginal || fixture.worker->latencySamples() == workerDelay) &&
                                        device->starts == starts && device->stops == stops &&
                                        device->getCurrentBufferSizeSamples() == 256 &&
                                        fixture.worker->processingQuantum() == 256;
                        passed &= ok;
                        require(output.getChildFile("callbacks.csv").replaceWithText(csv),
                                "retain every actual callback before oracle assertion");
                        require(ok, "actual native instrument graph callback failed independent "
                                    "PCM/cursor/lifecycle/RT oracle; retained CSV");
                        previousFrames = frames;
                    };
                    const auto create = [&](const te::Plugin::Ptr &source)
                    {
                        auto p = fixture.createRetaining(source, source->state.createCopy());
                        shells.push_back(p);
                        return p;
                    };
                    const auto overlay =
                        [&](const te::Plugin::Ptr &candidate,
                            const std::shared_ptr<vibera::test::retained_context::OutputObservation>
                                &observation,
                            bool fail = false)
                    {
                        auto result = std::make_shared<Context::GraphOverlay>();
                        result->plugins = [original = fixture.original, candidate](const te::PluginList &list)
                        {
                            te::Plugin::Array result;
                            for (auto *plugin : list)
                                result.add(plugin == original.get() ? candidate : te::Plugin::Ptr(plugin));
                            return result;
                        };
                        result->fixedInitialisationAdmission =
                            fixture.fixedInitialisationAdmission(candidate);
                        result->lastStage = [observation, fail](te::OutputDevice &,
                                                                const te::CreateNodeParams &,
                                                                std::unique_ptr<tracktion::graph::Node> node)
                            -> std::unique_ptr<tracktion::graph::Node>
                        {
                            if (fail)
                                throw std::runtime_error("Original instrument late-factory refusal");
                            return std::make_unique<vibera::test::retained_context::OutputMarker>(
                                std::move(node), observation);
                        };
                        return result;
                    };
                    audio.setLooping(false);
                    audio.playStop();
                    dispatchMessages(20);
                    // Actual cold managerGlobal origin, independently read on
                    // CONTROL before processing; never fitted from output PCM.
                    referenceOrigin = std::llround(manager.getCurrentStreamTime() * 48000.);
                    for (int n = 0; n < 8; ++n)
                        drive("empty_warmup_all_PCM_verified", 256);
                    onFrame = framesWritten;
                    audio.midiInput.handleIncomingMidiMessage(
                        nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                    drive("physical_on_partial", 127);
                    require(fixture.heldKeys(active) == 1 && fixture.worker->packetFrame() == 127,
                            "actual held external instrument and partial native packet");
                    const auto baseline = capture();
                    require(output.getChildFile("owners-before.txt").replaceWithText(baseline),
                            "raw actual owners baseline");
                    {
                        auto candidate = create(active);
                        auto observation =
                            std::make_shared<vibera::test::retained_context::OutputObservation>();
                        auto ready = fixture.context.prepareGraphOnly(overlay(candidate, observation));
                        require(observation->constructed == 1 && observation->calls == 0 &&
                                    capture() == baseline && fixture.worker->packetFrame() == 127 &&
                                    fixture.runtimeAttachments(active) == 2,
                                "actual private native graph initializes but does not render/reset/publish");
                        audio.typingInput.handleIncomingMidiMessage(
                            nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                        drive("old_graph_during_private_prepare", 64);
                        require(observation->calls == 0 && fixture.heldKeys(active) == 2,
                                "old actual graph consumes shared physical+typing key without preview "
                                "processing");
                    }
                    require(fixture.runtimeAttachments(active) == 1 && capture() == baseline,
                            "discarded native graph releases only its shell");
                    drive("after_candidate_discard", 65);
                    drive("partial_before_late_failure", 127);
                    {
                        auto candidate = create(active);
                        auto observation =
                            std::make_shared<vibera::test::retained_context::OutputObservation>();
                        bool caught = false;
                        try
                        {
                            auto ignored =
                                fixture.context.prepareGraphOnly(overlay(candidate, observation, true));
                        }
                        catch (const std::runtime_error &e)
                        {
                            caught = juce::String(e.what()) == "Original instrument late-factory refusal";
                        }
                        require(
                            caught && capture() == baseline && fixture.runtimeAttachments(active) == 1 &&
                                fixture.worker->packetFrame() == 127,
                            "real late native factory failure preserves old stream, held owners and history");
                    }
                    drive("after_late_failure", 129);
                    drive("partial_before_publication", 127);
                    auto candidate = create(active);
                    marker = std::make_shared<vibera::test::retained_context::OutputObservation>();
                    auto ready = fixture.context.prepareGraphOnly(overlay(candidate, marker));
                    require(fixture.context.validatePreparedGraph(*ready) == Status::ready &&
                                marker->calls == 0 && capture() == baseline &&
                                fixture.worker->packetFrame() == 127,
                            "actual candidate ready with retained partial native packet");
                    const auto oldCalls = fixture.sourceCalls(active);
                    require(fixture.context.publishPreparedGraph(*ready) == Status::ready &&
                                ready->wasPublished() && marker->calls == 0 && capture() == baseline,
                            "actual publication accepted but never mislabeled adopted before callback");
                    active = candidate;
                    drive("actual_adoption_partial", 64);
                    require(
                        marker->calls > 0 && marker->firstReference >= 0 &&
                            fixture.sourceCalls(fixture.original) == oldCalls &&
                            fixture.runtimeAttachments(active) == 2,
                        "next actual callback adopts new graph only, old reader ownership retained safely");
                    drive("adopted_partial_complete", 65);
                    ready.reset(); // Release any genuinely retired predecessor owner on CONTROL after
                                   // adoption.
                    const auto constructions = marker->constructed;
                    fixture.context.reallocateGraphOnly();
                    require(marker->constructed == constructions + 1 &&
                                fixture.runtimeAttachments(active) == 1 && capture() == baseline,
                            "normal graph-only publication retains adopted overlay and retires old "
                            "initialized shell on CONTROL");
                    drive("after_old_reader_retirement", 127);
                    drive("after_ordinary_rebuild", 129);
                    {
                        auto staleShell = create(active);
                        auto staleMarker =
                            std::make_shared<vibera::test::retained_context::OutputObservation>();
                        auto stale = fixture.context.prepareGraphOnly(overlay(staleShell, staleMarker));
                        require(staleMarker->calls == 0 && fixture.worker->packetFrame() == 0,
                                "genuine unpublished stale candidate before actual context change");
                        fixture.context.reallocateGraphOnly();
                        require(fixture.context.publishPreparedGraph(*stale) != Status::ready &&
                                    !stale->wasPublished() && staleMarker->calls == 0,
                                "real ordinary graph change refuses stale publication without rendering its "
                                "instrument");
                        drive("stale_candidate_never_adopted", 127);
                        drive("active_overlay_retained", 129);
                    }
                    require(fixture.runtimeAttachments(active) == 1 && capture() == baseline,
                            "stale private graph retirement retains active native MIDI runtime");
                    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                    drive("physical_off_typing_still_holds", 127);
                    drive("typing_alone_no_native_off", 129);
                    require(fixture.heldKeys(active) == 1,
                            "physical release cannot silence another actual typing owner");
                    offFrame = framesWritten;
                    audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                    drive("typing_final_off", 127);
                    drive("final_off_complete_packet", 129);
                    for (int n = 0; n < 4; ++n)
                        drive("native_release_tail", 256);
                    const auto finalOwners = capture();
                    require(output.getChildFile("owners-after.txt").replaceWithText(finalOwners),
                            "Retain actual final owners before equality assertion, including failure");
                    log += "finalHeld=" + juce::String(fixture.heldKeys(active)) +
                           ";providerPeak=" + juce::String(providerPeak, 12) +
                           ";ownersEqual=" + juce::String(int(finalOwners == baseline)) + "\n";
                    require(fixture.heldKeys(active) == 0 && providerPeak > 1.e-8f && finalOwners == baseline,
                            "actual owners released and finite nonzero native instrument signal observed");
                    if (exactOriginal)
                    {
                        const auto counters = parameterCatalogue(*fixture.worker, true);
                        require(std::abs(nativeParameter(counters, 90).value - 1.f / 1024) < 1.e-7 &&
                                    std::abs(nativeParameter(counters, 91).value - 1.f / 1024) < 1.e-7,
                                "actual SDK native counters prove one note-on and one final note-off, no "
                                "hidden retrigger");
                    }
                    require(output.getChildFile("owners-after.txt").replaceWithText(capture()),
                            "raw actual owners after graph acceptance");
                    log += "workerPid=" + juce::String(pid) + ";instance=" + instance +
                           ";frames=" + juce::String(framesWritten) +
                           ";providerPeak=" + juce::String(providerPeak, 12) +
                           ";markerCalls=" + juce::String(marker->calls.load()) + "\n";
                    audio.stop();
                });
        }
        catch (const std::exception &e)
        {
            callbackAllocationProbe = false;
            passed = false;
            log += "FAILED " + juce::String(e.what()) + "\n";
            audio.stop();
        }
        const auto wav = output.getChildFile("actual-output.wav");
        require(!wav.exists(), "disposable original output must not overwrite existing evidence");
        juce::WavAudioFormat format;
        auto stream = wav.createOutputStream();
        require(bool(stream), "create exact actual PCM evidence");
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(captured, 0, framesWritten),
                "retain actual native graph PCM including startup/failure/partial buffers");
        writer.reset();
        log += "pass=" + juce::String(int(passed)) +
               "\nNot qualified: concurrent prepare/callback ownership, product routing transaction, "
               "arbitrary native state, "
               "installed exact-DSP equivalence, native desktop, hardware or human listening.\n";
        require(output.getChildFile("result.txt").replaceWithText(log) &&
                    output.getChildFile("callbacks.csv").replaceWithText(csv),
                "retain all graph evidence before terminal assertion");
        require(passed, "actual same-Edit native instrument graph failed; inspect preserved PCM/CSV/owners");
    }
}
