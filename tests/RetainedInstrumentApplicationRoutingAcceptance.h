// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RetainedInstrumentContextFixture.h"
#include "RetainedContextGraphAcceptance.h"
// Include after ParameterAcceptance/awaitProjection/SyntheticDevice/heap probes.
// Original actual production routing/history acceptance; serialized CONTROL/DSP.
// Separate fresh Engine per mode; original native-only 28-callback test unchanged.
__declspec(noinline) static void retainedInstrumentApplicationRoutingTests(const juce::File &folder,
                                                                           const PluginSpec &sourceSpec,
                                                                           bool exactOriginal = true,
                                                                           bool publishRoutes = true)
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
        const auto output = folder.getChildFile(
            publishRoutes ? (patternMode ? "application-pattern" : "application-song")
                          : (patternMode ? "packet-baseline-pattern" : "packet-baseline-song"));
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
            insert.gain = insert.index == 1 ? .25f : insert.index == 2 ? .5f : 1.f;
            insert.pan = 0;
            insert.effects.clear();
        }
        project.patterns.front().length = 64;
        project.patterns.front().notes.clear();
        const auto pattern = project.patterns.front().id;
        const auto channelId = channel.id; // Stable scalar; Project commit swaps vector storage.
        project.placements = {{project.allocate(), pattern, 0, 0, 64, 0}};
        require(project.validate().wasOk(), "genuine independent Pattern/Song external instrument project");
        // Original provider contract: every dispatched physical call below is
        // <=256 frames. Native source-rate envelope is +/-10%, so production
        // K=1+ceil(ceil(256*1.1)/256)=3. This is an independent numerical
        // transport oracle, not a fitted offset or an enlarged device buffer.
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
        // The first ordinary context must bind an ACTUAL nonzero managerGlobal
        // clock. Do not derive its origin from output PCM or callback serials.
        juce::AudioBuffer<float> beforeContext(2, 127);
        for (int n = 0; n < 17; ++n)
            device->process(beforeContext);
        require(std::llround(manager.getCurrentStreamTime() * 48000.) == 2159,
                "seventeen genuine empty physical calls establish independent global frame2159");
        require(awaitProjection(audio, project, patternMode, pattern).wasOk(),
                "actual production external instrument projection");
        const auto preparedSource = audio.projectionDiagnostics();
        require(preparedSource.pluginSourceState == "pending" && preparedSource.pluginTransportPackets == 3 &&
                    preparedSource.maximumPhysicalFrames == 256 &&
                    preparedSource.maximumSourceFrames == 282 && preparedSource.pluginDeviceEpoch > 0,
                "shared readback reports3Q physical256/source282 and pending, not fabricated DSP completion");
        const auto host = audio.parameterHost(spec.id);
        require(host && host->alive(), "actual projected instrument worker available");
        require(!exactOriginal ||
                    (host->latencySamples() == workerDelay && host->declaredLatency().nativeSamples == 0),
                "independent native0 plus declared production3Q768 latency");
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
        juce::String log =
            "Serialized actual canonical Channel routing + Undo/Redo instrument experiment. Device48000/256, "
            "worker768; variable dispatched lengths127/64/65/129. Mixer1=.25, Mixer2=.5.\n";
        if (!publishRoutes)
            log += "Independent unchanged-graph packet-boundary reproduction: NO routing/model/history "
                   "publication; insert1=.25 remains constant. Does not qualify route transactions.\n";
        log +=
            exactOriginal
                ? "Original independent signed PCM/retrigger oracle: acknowledged native manual gain.25; one "
                  "pitch60 on, shared physical+typing ownership, worker latency768; no phase/gain fitting. "
                  "Pattern has NO Song/native automation lanes; Song has one native gain event per quantum.\n"
                : "Installed instrument compatibility branch: finite/nonzero PCM and exact raw "
                  "owner/lifecycle/MIDI readback only; no exact DSP or listening claim.\n";
        juce::String csv = "phase,frame,frames,cursorBefore,cursorAfter,sourceCalls,submitted,processed,"
                           "missed,dropped,parametersApplied,held,markerCalls,maxError,callbackMs,newCalls,"
                           "deleteCalls,headAdvance,headStart,referenceStart,referenceEnd,sameContext,"
                           "samePlayhead,routingTarget\n";
        juce::AudioBuffer<float> captured(2, 65536), analytical(2, 65536);
        captured.clear();
        analytical.clear();
        std::array<std::unique_ptr<juce::AudioBuffer<float>>, 5> buffers;
        const std::array<int, 5> sizes{256, 127, 64, 65, 129};
        for (std::size_t n = 0; n < sizes.size(); ++n)
            buffers[n] = std::make_unique<juce::AudioBuffer<float>>(2, sizes[n]);
        int framesWritten = 0, previousFrames = 0, routingTarget = 1;
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
                    require(fixture.worker == host,
                            "graph fixture uses the exact acknowledged prepared instrument worker");
                    const auto pid = fixture.worker->processId();
                    const auto instance = fixture.worker->runtimeIdentity();
                    const auto reset = fixture.worker->midiResetGeneration();
                    const auto starts = device->starts, stops = device->stops;
                    const auto *head = fixture.context.getNodePlayHead();
                    require(head != nullptr, "actual native playhead exists");
                    const auto baselineStats = fixture.worker->statistics();

                    auto canonical = [&]
                    {
                        return controller.project.state().toXmlString() + "/" + controller.projectIdentity +
                               "/" + juce::String(juce::int64(controller.revision)) + "/" +
                               juce::String(int(controller.dirty)) + "/" +
                               juce::String(controller.undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
                               controller.undo.getUndoDescriptions().joinIntoString("|") + "/" +
                               controller.undo.getRedoDescriptions().joinIntoString("|");
                    };
                    const auto capture = [&]
                    { return fixture.captureOwners() + "\ncanonical=" + canonical(); };
                    require(fixture.currentSampler && fixture.currentSampler() == fixture.original,
                            "Fresh actual baseline, before ANY test overlay or retired registry source");
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
                                const double mixerGain = routingTarget == 1 ? .25 : .5;
                                const double expected =
                                    !held ? 0
                                          : mixerGain * (c == 0 ? .25 * (61. / 512 + 1. / 4096)
                                                                : -.25 * (68. / 512 + 1. / 8192));
                                analytical.setSample(c, framesWritten + n, float(expected));
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
                               "," + juce::String(routingTarget) + "\n";
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
                    audio.setLooping(false);
                    audio.playStop();
                    dispatchMessages(20);
                    // The cold ordinary gate binds the ACTUAL manager clock,
                    // not the constructor's anticipatory one-quantum range END.
                    // Establish it independently before any output processing.
                    referenceOrigin = std::llround(manager.getCurrentStreamTime() * 48000.);
                    for (int n = 0; n < 8; ++n)
                        drive("empty_warmup_all_PCM_verified", 256);
                    onFrame = framesWritten;
                    audio.midiInput.handleIncomingMidiMessage(
                        nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                    drive("physical_on_partial", 127);
                    require(fixture.heldKeys(active) == 1 && fixture.worker->packetFrame() == 127,
                            "actual held external instrument and partial native packet");
                    audio.typingInput.handleIncomingMidiMessage(
                        nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                    drive("typing_same_pitch_partial", 64);
                    require(fixture.heldKeys(active) == 2 && fixture.worker->packetFrame() == 191,
                            "Two actual MIDI input owners and partial native packet BEFORE route");
                    const auto nativeState = [&]
                    {
                        auto state = fixture.edit.state.createCopy();
                        state.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
                        return state.toXmlString();
                    };
                    const auto originalNative = nativeState();
                    const auto runtime = fixture.runtimeIdentity(active);
                    require(output.getChildFile("owners-before.txt").replaceWithText(capture()),
                            "Retain actual baseline before canonical route/history");
                    using Routing = AudioEngine::ChannelRoutingStatus;
                    bool retainedJointPlanRefused = false;
                    const auto transact = [&](int destination, int direction)
                    {
                        if (!publishRoutes)
                        {
                            // Independent retained failure reproduction: identical
                            // MIDI/worker callback boundary WITHOUT any graph/model
                            // transaction. Never substitutes for the route scenario.
                            drive("unchanged_graph_partial_before_crossing", 64);
                            drive("unchanged_graph_packet_boundary_crossing", 65);
                            return;
                        }
                        const auto before = canonical();
                        const auto revision = controller.revision;
                        const auto sourceCursor = fixture.worker->packetFrame();
                        require(sourceCursor != 0,
                                "Every route/history candidate prepared mid native packet");
                        const auto held = fixture.heldKeys(active);
                        const auto oldShell = active;
                        const auto oldCalls = fixture.sourceCalls(oldShell);
                        std::unique_ptr<PreparedControllerChange> mutation;
                        CommandArgs args;
                        args.channel = channelId;
                        args.mixer = destination;
                        const auto prepared =
                            direction < 0 ? controller.prepareHistoryChange(HistoryDirection::undo, mutation)
                            : direction > 0
                                ? controller.prepareHistoryChange(HistoryDirection::redo, mutation)
                                : controller.prepareStructuralCommand("channel.mixer", args, mutation);
                        require(prepared.wasOk() && mutation && !mutation->isNoOp() &&
                                    mutation->destinationBeforeCommit()->channel(channelId)->mixer ==
                                        destination,
                                "Canonical prepared destination/history, not duplicate test mutation");
                        std::unique_ptr<AudioEngine::PreparedChannelRouting> audioPlan;
                        const auto ready =
                            audio.prepareChannelRouting(*mutation->destinationBeforeCommit(), audioPlan);
                        if (ready.status == Routing::preparationFailed &&
                            ready.message.contains("routing.pdcStageConfiguration"))
                        {
                            // User policy2026-10-18: this private mid-packet
                            // retained-context experiment is an optimization.
                            // Refusal must preserve the exact live incumbent;
                            // the application's bounded reconfigure route is a
                            // separate required workflow, still to be qualified.
                            require(!audioPlan && !mutation->wasCommitted() && canonical() == before &&
                                        nativeState() == originalNative &&
                                        fixture.currentSampler() == oldShell &&
                                        fixture.worker->packetFrame() == sourceCursor &&
                                        fixture.heldKeys(oldShell) == held &&
                                        fixture.runtimeIdentity(oldShell) == runtime &&
                                        fixture.worker->processId() == pid &&
                                        fixture.worker->runtimeIdentity() == instance,
                                    "Unqualified joint-plan refusal retains model/history/MIDI/worker/cursor "
                                    "exactly");
                            retainedJointPlanRefused = true;
                            log += "acceptedStructuralLimitation=retained partial-packet joint PDC plan "
                                   "unavailable; "
                                   "NOT technically fixed or application route completion\n";
                            drive("refused_joint_plan_retains_partial_native_packet", 64);
                            drive("refused_joint_plan_retains_native_boundary", 65);
                            return;
                        }
                        require(ready.status == Routing::ready && audioPlan,
                                ("Actual native instrument routing prepare: " + ready.message).toStdString());
                        require(
                            canonical() == before && nativeState() == originalNative &&
                                fixture.currentSampler() == oldShell &&
                                fixture.worker->packetFrame() == sourceCursor &&
                                fixture.heldKeys(oldShell) == held &&
                                fixture.runtimeIdentity(oldShell) == runtime &&
                                fixture.worker->processId() == pid &&
                                fixture.worker->runtimeIdentity() == instance,
                            "Private native preparation preserves exact canonical history/input/Host/cursor");
                        require(controller.validatePreparedChange(*mutation).wasOk(),
                                "Final canonical admission");
                        const auto valid = audio.validateChannelRouting(*audioPlan);
                        require(
                            valid.status == Routing::ready,
                            ("Final actual instrument routing admission: " + valid.message).toStdString());
                        const auto allocations = callbackAllocations.load(),
                                   deletes = callbackDestructions.load();
                        bool committed = false;
                        Routing posted;
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
                            } probe; // CONTROL commit probe, distinct from DSP callback instrumentation.
                            posted = audio.postChannelRouting(*audioPlan);
                            committed =
                                posted == Routing::posted && controller.commitPreparedChange(*mutation);
                        }
                        require(posted == Routing::posted && committed && audioPlan->wasPosted() &&
                                    callbackAllocations.load() == allocations &&
                                    callbackDestructions.load() == deletes,
                                "Native post plus actual canonical commit observes no C++ heap/destruction");
                        active = fixture.currentSampler(); // Exact committed map, not original cache.
                        require(active != oldShell && fixture.runtimeIdentity(active) == runtime &&
                                    fixture.heldKeys(active) == held &&
                                    fixture.worker->packetFrame() == sourceCursor &&
                                    controller.project.channel(channelId)->mixer == destination &&
                                    controller.revision == revision + 1 && nativeState() == originalNative,
                                "One committed successor retains real instrument/MIDI Runtime and native "
                                "routing state");
                        if (std::none_of(shells.begin(), shells.end(),
                                         [&](const auto &p) { return p == active; }))
                            shells.push_back(active);
                        controller.deliverPendingNotifications();
                        const auto pending = audio.channelRoutingReadback();
                        require(pending.status == Routing::posted && pending.postedSerial != 0 &&
                                    pending.selectedSerial != pending.postedSerial,
                                "Actual pending request is not falsely completed before DSP callback");
                        auto another = controller.project;
                        another.channel(channelId)->mixer = destination == 1 ? 2 : 1;
                        std::unique_ptr<AudioEngine::PreparedChannelRouting> refused;
                        const auto pendingCanonical = canonical();
                        const auto busy = audio.prepareChannelRouting(another, refused);
                        require(busy.status == Routing::busy && !refused && canonical() == pendingCanonical &&
                                    fixture.worker->packetFrame() == sourceCursor,
                                "Second real route waits for actual adoption without model/history work");
                        routingTarget = destination;
                        drive(direction < 0   ? "canonical_undo_held_native_adoption"
                              : direction > 0 ? "canonical_redo_held_native_adoption"
                                              : "canonical_route_held_native_adoption",
                              64);
                        require(fixture.sourceCalls(oldShell) == oldCalls,
                                "Retired registered/effective predecessor never processes after adoption");
                        audio.reclaimPluginHosts();
                        const auto selected = audio.channelRoutingReadback();
                        require(selected.status == Routing::selected &&
                                    selected.selectedSerial == pending.postedSerial &&
                                    selected.postedSerial == pending.postedSerial &&
                                    fixture.currentSampler() == active && fixture.heldKeys(active) == held,
                                "Actual exact native holder selection plus current effective sampler/input "
                                "readback");
                        drive("committed_native_route_packet_crossing", 65);
                    };
                    transact(2, 0);
                    if (!retainedJointPlanRefused)
                    {
                        transact(1, -1);
                        transact(2, 1);
                        transact(1, 0);
                    }
                    require(
                        controller.project.channel(channelId)->mixer == 1 && nativeState() == originalNative,
                        "Canonical repeated successor returns original route without registered-list writes");
                    {
                        const auto before = canonical();
                        std::unique_ptr<AudioEngine::PreparedChannelRouting> outputTicket;
                        const auto noOp = audio.prepareChannelRouting(controller.project, outputTicket);
                        auto invalid = controller.project;
                        invalid.channel(channelId)->mixer = 999;
                        const auto bad = audio.prepareChannelRouting(invalid, outputTicket);
                        require(
                            noOp.status == Routing::noOp && bad.status == Routing::invalidProject &&
                                !outputTicket && canonical() == before && nativeState() == originalNative,
                            "Actual no-op/invalid target refuse atomically without losing MIDI ownership");
                    }
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
                           ";ownersEqual=" + juce::String(int(nativeState() == originalNative)) + "\n";
                    require(fixture.heldKeys(active) == 0 && providerPeak > 1.e-8f &&
                                nativeState() == originalNative,
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
                           ";markerCalls=" + juce::String(marker ? marker->calls.load() : 0) + "\n";
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
        const auto raw = output.getChildFile("actual-output-planar-float32le.raw");
        require(!raw.exists(), "Preserve failed exact native PCM receipts");
        auto rawStream = raw.createOutputStream();
        require(bool(rawStream), "Exact raw native PCM evidence stream");
        for (int c = 0; c < 2; ++c)
            require(rawStream->write(captured.getReadPointer(c), std::size_t(framesWritten) * sizeof(float)),
                    "Retain every observed float bit before WAV conversion");
        rawStream.reset();
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
        if (exactOriginal)
        {
            const auto expected = output.getChildFile("analytical-expected.wav");
            require(!expected.exists(), "Preserve original analytical evidence");
            auto expectedStream = expected.createOutputStream();
            require(bool(expectedStream), "Original analytical PCM evidence stream");
            std::unique_ptr<juce::AudioFormatWriter> expectedWriter(
                format.createWriterFor(expectedStream.release(), 48000, 2, 32, {}, 0));
            require(expectedWriter &&
                        expectedWriter->writeFromAudioSampleBuffer(analytical, 0, framesWritten),
                    "Retain independent per-frame signed PCM and route-gain oracle");
        }
        log += "pass=" + juce::String(int(passed)) +
               "\nNot qualified: concurrent prepare/callback ownership, UI/MCP coordinator, "
               "arbitrary native state, "
               "installed exact-DSP equivalence, native desktop, hardware or human listening.\n";
        require(output.getChildFile("result.txt").replaceWithText(log) &&
                    output.getChildFile("callbacks.csv").replaceWithText(csv),
                "retain all graph evidence before terminal assertion");
        require(passed,
                "actual canonical instrument routing/history failed; inspect preserved PCM/CSV/owners");
    }
}
