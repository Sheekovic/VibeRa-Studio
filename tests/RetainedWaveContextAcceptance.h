// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RetainedWaveContextFixture.h"
// Include after require/awaitProjection/SyntheticDevice/CallbackAllocationProbe.
// Actual native live decoder + ready-graph test; serialized, not concurrent.
__declspec(noinline) static void retainedWaveContextTests(const juce::File &folder)
{
    const auto output = folder.getChildFile("wave-context");
    require(!output.exists() && output.createDirectory().wasOk(),
            "Fresh owned native Wave evidence; preserve failures");
    struct Storage
    {
        juce::String previous =
            juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &path)
        {
            require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                               path.getFullPathName().toWideCharPointer()) == 0,
                    "Owned Wave cache/settings");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer());
        }
    } storage(output.getChildFile("storage"));
    // Original signed independent stereo beds plus sparse prime-period markers.
    // The nonzero beds make a silence/fade seam observable at EVERY boundary.
    // No leading silence, measured phase fitting, startup exclusion or baseline
    // recording is used as the musical oracle.
    const auto sample = [](int channel, int frame) -> float
    {
        if (frame < 0 || frame >= 16384)
            return 0;
        return channel == 0
                   ? .0625f + (frame % 257 == 0 ? .125f : 0.f) + (frame % 64 < 3 ? .015625f : 0.f)
                   : -.03125f - (frame % 263 == 0 ? .0625f : 0.f) - (frame % 64 < 5 ? .0078125f : 0.f);
    };
    const auto writePcm = [](const juce::File &file, const juce::AudioBuffer<float> &pcm, int frames)
    {
        require(!file.exists(), "Never overwrite retained original Wave evidence");
        auto stream = file.createOutputStream();
        require(bool(stream), "Owned exact Wave file stream");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, frames),
                "Write exact original Wave PCM");
    };
    const auto source = output.getChildFile("original-sparse-stereo.wav");
    {
        juce::AudioBuffer<float> original(2, 16384);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 16384; ++n)
                original.setSample(c, n, sample(c, n));
        writePcm(source, original, 16384);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(
            format.createReaderFor(source.createInputStream().release(), true));
        require(reader && reader->sampleRate == 48000 && reader->numChannels == 2 &&
                    reader->lengthInSamples == 16384,
                "Genuine independently authored same-rate stereo source, no resampling/transposition");
        juce::AudioBuffer<float> decoded(2, 16384);
        require(reader->read(&decoded, 0, 16384, 0, true, true), "Read original PCM fixture for integrity");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 16384; ++n)
                require(decoded.getSample(c, n) == sample(c, n),
                        "Source file roundtrip matches analytical code at every frame");
    }
    Controller controller;
    controller.project.channels.clear();
    CommandArgs add;
    add.text = source.getFullPathName();
    add.length = 16384. / 48000.;
    add.beat = 0;
    add.mixer = 1;
    require(controller.execute("audio.add", add).wasOk(), "Actual canonical AudioClip import/placement");
    auto &project = controller.project;
    const auto channel = project.channels.front().id, pattern = project.patterns.front().id;
    project.channels.front().gain = 1;
    project.channels.front().pan = 0;
    for (auto &insert : project.mixer)
    {
        insert.gain = insert.index == 1 ? .25f : insert.index == 2 ? .5f : 1.f;
        insert.pan = 0;
        insert.effects.clear();
    }
    require(project.validate().wasOk() && project.channels.front().audioClip &&
                project.audioPlacements.size() == 1,
            "Actual musical model has one placed AudioClip and distinct .25/.5 Mixer buses");
    CommandArgs tempo;
    tempo.value = 121;
    require(controller.execute("project.tempo", tempo).wasOk() && controller.undoEditChecked().wasOk() &&
                controller.undo.canRedo(),
            "Real canonical history remains available during routing-only graph experiment");
    auto audioOwner = std::make_unique<AudioEngine>(false, output.getChildFile("guide.json"));
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
    require(device && device->getCurrentSampleRate() == 48000 &&
                device->getCurrentBufferSizeSamples() == 256 && te::getDefaultPanLaw() == te::PanLawLinear,
            "Actual native Wave48k256 virtual device and linear pan");
    require(awaitProjection(audio, project, false, pattern).wasOk(),
            "Actual production Song native AudioClip projection");
    juce::AudioBuffer<float> actual(2, 16384), expected(2, 16384);
    actual.clear();
    expected.clear();
    const std::array<int, 5> sizes{256, 127, 64, 65, 129};
    std::array<std::unique_ptr<juce::AudioBuffer<float>>, 5> buffers;
    for (std::size_t n = 0; n < sizes.size(); ++n)
        buffers[n] = std::make_unique<juce::AudioBuffer<float>>(2, sizes[n]);
    int framesWritten = 0, previousFrames = 0, target = 1;
    bool passed = true;
    juce::String csv = "phase,frame,frames,target,maximumError,callbackMs,newCalls,deleteCalls,headStart,"
                       "referenceStart,referenceEnd,posted,selected,markerCalls,mappedFirstBefore,"
                       "mappedLastBefore,cacheMissBeforeReset,cacheMissInCallback\n";
    juce::String report =
        "Original native Wave graph execution receipt. Song AudioClip; synthetic callback, no "
        "hardware/input/listening.\n"
        "Analytical source starts nonzero at frame0; all startup/transition frames checked, no "
        "fit/exclusion. Config48k256; actual callback lengths256/127/64/65/129.\n";
    try
    {
        audio.withRetainedWaveContextFixture(
            channel,
            [&](RetainedWaveContextFixture &fixture)
            {
                // The native350ms idle timer opens an empty transaction; keep
                // this bounded graph experiment in one native control batch.
                // Canonical history and all native actions/units remain checked.
                te::Edit::UndoTransactionInhibitor nativeHistory(fixture.edit);
                using Context = te::EditPlaybackContext;
                using Status = Context::ReadyGraphChangeStatus;
                const auto *head = fixture.context.getNodePlayHead();
                require(head != nullptr, "Actual native Wave playhead");
                const auto starts = device->starts, stops = device->stops;
                auto history = controller.undo.prepareRedoMutation();
                require(bool(history), "Actual canonical history guard without cursor mutation");
                const auto canonical = [&]
                {
                    return controller.project.state().toXmlString() + "/" + controller.projectIdentity + "/" +
                           juce::String(juce::int64(controller.revision)) + "/" +
                           juce::String(int(controller.dirty)) + "/" +
                           juce::String(int(controller.undo.isPreparedMutationCurrent(*history))) + "/" +
                           controller.undo.getUndoDescriptions().joinIntoString("|") + "/" +
                           controller.undo.getRedoDescriptions().joinIntoString("|");
                };
                const auto capture = [&] { return fixture.captureOwners() + "\ncanonical=" + canonical(); };
                // These live-cache diagnostics run only on CONTROL around the
                // sole manually invoked callback. No callback allocations/I/O.
                auto& cache = audio.engine.getAudioFileManager().cache;
                const te::AudioFile nativeFile(audio.engine, source);
                std::shared_ptr<RetainedWaveObservation> marker;
                juce::int64 referenceOrigin = -1;
                const auto drive = [&](const char *phase, int frames)
                {
                    require(framesWritten + frames <= 16384, "Bounded original Wave output storage");
                    juce::AudioBuffer<float> *buffer = nullptr;
                    for (std::size_t n = 0; n < sizes.size(); ++n)
                        if (sizes[n] == frames)
                            buffer = buffers[n].get();
                    require(buffer != nullptr, "Callback buffer prepared before actual processing");
                    const bool mappedFirst = cache.hasMappedReader(nativeFile, 0);
                    const bool mappedLast = cache.hasMappedReader(nativeFile, 16383);
                    // Retain prior diagnostic before starting a per-callback
                    // miss epoch. This never hides or waives a PCM failure.
                    const bool priorCacheMiss = cache.hasCacheMissed(true);
                    const auto before = audio.projectionDiagnostics();
                    const auto beforeReference = head->getReferenceSampleRange();
                    const auto allocated = callbackAllocations.load(),
                               destroyed = callbackDestructions.load();
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
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
                    const auto allocations = callbackAllocations.load() - allocated,
                               deletions = callbackDestructions.load() - destroyed;
                    const bool callbackCacheMiss = cache.hasCacheMissed(false);
                    const auto after = audio.projectionDiagnostics();
                    const auto range = head->getReferenceSampleRange();
                    const auto selection = fixture.context.getGraphSelection();
                    const float gain = target == 1 ? .25f : .5f;
                    double maximum = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                        {
                            const auto value = buffer->getSample(c, n),
                                       oracle = sample(c, framesWritten + n) * gain;
                            actual.setSample(c, framesWritten + n, value);
                            expected.setSample(c, framesWritten + n, oracle);
                            maximum = std::isfinite(value)
                                          ? std::max(maximum, std::abs(double(value) - oracle))
                                          : std::numeric_limits<double>::infinity();
                        }
                    const bool ok =
                        maximum == 0 && !callbackCacheMiss && allocations == 0 && deletions == 0 &&
                        ms <= 1000. * frames / 48000. &&
                        std::llround(before.headSeconds * 48000.) == framesWritten - previousFrames &&
                        head->getPosition() == framesWritten && referenceOrigin >= 0 &&
                        beforeReference.getEnd() == referenceOrigin + framesWritten &&
                        range.getStart() == referenceOrigin + framesWritten &&
                        range.getEnd() == referenceOrigin + framesWritten + frames &&
                        selection.state == tracktion::graph::GraphSelectionState::selected &&
                        selection.referenceStart == range.getStart() &&
                        selection.referenceEnd == range.getEnd() && before.edit == after.edit &&
                        before.playbackContext == after.playbackContext &&
                        after.playbackContext == reinterpret_cast<std::uintptr_t>(&fixture.context) &&
                        fixture.context.getNodePlayHead() == head && after.headPlaying &&
                        after.transportPlaying && device->starts == starts && device->stops == stops &&
                        device->getCurrentBufferSizeSamples() == 256 && audio.pluginDiagnostics().empty();
                    csv += juce::String(phase) + "," + juce::String(framesWritten) + "," +
                           juce::String(frames) + "," + juce::String(target) + "," +
                           juce::String(maximum, 12) + "," + juce::String(ms, 6) + "," +
                           juce::String(allocations) + "," + juce::String(deletions) + "," +
                           juce::String(head->getPosition()) + "," + juce::String(range.getStart()) + "," +
                           juce::String(range.getEnd()) + "," +
                           juce::String(juce::int64(selection.postedSerial)) + "," +
                           juce::String(juce::int64(selection.selectedSerial)) + "," +
                           juce::String(marker ? marker->calls.load() : 0) + "," +
                           juce::String(int(mappedFirst)) + "," + juce::String(int(mappedLast)) + "," +
                           juce::String(int(priorCacheMiss)) + "," +
                           juce::String(int(callbackCacheMiss)) + "\n";
                    framesWritten += frames;
                    previousFrames = frames;
                    passed &= ok;
                    require(output.getChildFile("callbacks.csv").replaceWithText(csv),
                            "Retain all actual Wave callback failures before assertion");
                    require(ok, "Native Wave callback failed exact independent PCM/clock/lifecycle/RT "
                                "oracle; retain failure");
                    dispatchMessages(6); // CONTROL pacing only, never callback I/O/wait.
                };
                audio.setLooping(false);
                audio.playStop();
                dispatchMessages(20);
                referenceOrigin = head->getReferenceSampleRange().getEnd();
                for (int n = 0; n < 4; ++n)
                    drive("startup_every_sample_checked", 256);
                const auto baseline = capture();
                require(output.getChildFile("owners-before.txt").replaceWithText(baseline),
                        "Raw native Wave owners before route");
                const auto verifyWave = [](const std::shared_ptr<RetainedWaveObservation> &value)
                {
                    require(value->constructed == 1 && value->waveNodes + value->realTimeWaveNodes == 1 &&
                                value->calls == 0,
                            ("Actual private native graph must contain one original unprocessed decoder: " +
                             juce::String("constructed=") + juce::String(value->constructed) +
                             ";wave=" + juce::String(value->waveNodes) +
                             ";realtime=" + juce::String(value->realTimeWaveNodes) +
                             ";processed=" + juce::String(value->calls.load()))
                                .toStdString());
                };
                {
                    auto privateMarker = std::make_shared<RetainedWaveObservation>();
                    auto ready =
                        fixture.context.prepareGraphOnly(fixture.overlayFor(2, privateMarker, false));
                    verifyWave(privateMarker);
                    require(capture() == baseline,
                            "Private native Wave preparation preserves actual cache/clip/output/history");
                    drive("old_decoder_while_private_route_prepared", 127);
                    drive("old_decoder_still_selected", 64);
                    require(privateMarker->calls == 0,
                            "Private route/decoder is not processed before publication");
                    report += "decoderTypes=" + juce::String(privateMarker->waveNodes) + "/" +
                              juce::String(privateMarker->realTimeWaveNodes) + "\n";
                }
                require(capture() == baseline,
                        "Discarded native decoder route retains old source and history");
                drive("after_private_discard", 65);
                {
                    auto refused = std::make_shared<RetainedWaveObservation>();
                    bool caught = false;
                    try
                    {
                        auto ignored = fixture.context.prepareGraphOnly(fixture.overlayFor(2, refused, true));
                    }
                    catch (const std::runtime_error &e)
                    {
                        caught = juce::String(e.what()) == "Original Wave late factory refusal";
                    }
                    require(caught && capture() == baseline,
                            "Genuine late native Wave factory refusal is atomic");
                }
                drive("after_late_factory_refusal", 127);
                drive("partial_before_route", 129);
                marker = std::make_shared<RetainedWaveObservation>();
                auto ready = fixture.context.prepareGraphOnly(fixture.overlayFor(2, marker, false));
                verifyWave(marker);
                const auto requestSerial = ready->publicationSerial();
                require(requestSerial != 0, "Actual prepared EPC owns exact immutable publication token");
                const auto previous = fixture.context.getGraphSelection();
                require(fixture.context.validatePreparedGraph(*ready) == Status::ready &&
                            capture() == baseline,
                        "Actual native Wave route candidate ready before publication");
                require(fixture.context.publishPreparedGraph(*ready) == Status::ready &&
                            ready->wasPublished() && ready->publicationSerial() == requestSerial &&
                            marker->calls == 0 && capture() == baseline,
                        "Graph-only route post is not falsely reported as callback adoption");
                const auto posted = fixture.context.getGraphSelection();
                require(posted.postedSerial == requestSerial &&
                            posted.selectedSerial == previous.selectedSerial &&
                            posted.postedSerial != previous.postedSerial,
                        "Actual decoder graph remains old until callback selects posted holder");
                target = 2;
                drive("actual_native_decoder_route_adoption", 127);
                require(marker->calls > 0 &&
                            marker->firstReference == referenceOrigin + framesWritten - 127 &&
                            fixture.context.getGraphSelection().selectedSerial == posted.postedSerial,
                        "First actual callback selects new route and continues source at exact current "
                        "reference");
                drive("adopted_decoder_partial", 129);
                ready.reset();
                const auto constructions = marker->constructed;
                fixture.context.reallocateGraphOnly();
                require(marker->constructed == constructions + 1 && capture() == baseline,
                        "Ordinary rebuild retains adopted private output route");
                drive("after_predecessor_control_retirement", 64);
                drive("retained_native_decoder_again", 65);
                {
                    auto staleMarker = std::make_shared<RetainedWaveObservation>();
                    auto stale = fixture.context.prepareGraphOnly(fixture.overlayFor(1, staleMarker, false));
                    verifyWave(staleMarker);
                    fixture.context.reallocateGraphOnly();
                    require(fixture.context.publishPreparedGraph(*stale) != Status::ready &&
                                !stale->wasPublished() && staleMarker->calls == 0 && capture() == baseline,
                            "Actual stale Wave route candidate refuses without changing selected route or "
                            "source");
                    drive("stale_route_never_processed", 127);
                    drive("retained_route_after_stale", 129);
                }
                marker = std::make_shared<RetainedWaveObservation>();
                auto back = fixture.context.prepareGraphOnly(fixture.overlayFor(1, marker, false));
                verifyWave(marker);
                require(fixture.context.publishPreparedGraph(*back) == Status::ready,
                        "Actual native Wave route back to original bus posts");
                target = 1;
                drive("actual_route_back_adoption", 256);
                require(marker->calls > 0 && capture() == baseline,
                        "Routed-back native decoder retains original clip/output/list/history identities");
                for (int n = 0; n < 4; ++n)
                    drive("after_route_back", 256);
                require(output.getChildFile("owners-after.txt").replaceWithText(capture()),
                        "Raw native Wave owners after actual routing");
                // Real application preparation and canonical Controller history,
                // after the independent native-only routing baseline above.
                // This does not expose an unfinished product command to UI/MCP.
                using RoutingStatus = AudioEngine::ChannelRoutingStatus;
                const auto nativeState = [&]
                {
                    auto state = fixture.edit.state.createCopy();
                    state.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
                    return state.toXmlString();
                };
                const auto originalNative = nativeState();
                const auto transact = [&](int destination, int historyDirection)
                {
                    std::unique_ptr<PreparedControllerChange> mutation;
                    CommandArgs args;
                    args.channel = channel;
                    args.mixer = destination;
                    const auto revision = controller.revision;
                    const auto before = controller.project.state().toXmlString();
                    const auto prepared =
                        historyDirection < 0
                            ? controller.prepareHistoryChange(HistoryDirection::undo, mutation)
                        : historyDirection > 0
                            ? controller.prepareHistoryChange(HistoryDirection::redo, mutation)
                            : controller.prepareStructuralCommand("channel.mixer", args, mutation);
                    require(prepared.wasOk() && mutation && !mutation->isNoOp(),
                            "Genuine prepared route command or history destination");
                    require(mutation->destinationBeforeCommit()->channel(channel)->mixer == destination,
                            "Canonical destination, not a hand-edited parallel document");
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> audioPlan;
                    const auto audioReady =
                        audio.prepareChannelRouting(*mutation->destinationBeforeCommit(), audioPlan);
                    require(audioReady.status == RoutingStatus::ready && bool(audioPlan),
                            ("Actual application routing prepare: " + audioReady.message).toStdString());
                    require(controller.project.state().toXmlString() == before &&
                                controller.revision == revision && nativeState() == originalNative,
                            "Private application preparation leaves canonical model/history/native state "
                            "unchanged");
                    require(controller.validatePreparedChange(*mutation).wasOk(),
                            "Final real Controller admission before publication");
                    const auto audioValid = audio.validateChannelRouting(*audioPlan);
                    require(audioValid.status == RoutingStatus::ready,
                            ("Final application audio admission: " + audioValid.message).toStdString());
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    const bool previous = callbackAllocationProbe;
                    callbackAllocationProbe = true; // CONTROL commit instrumentation, not DSP.
                    const auto posted = audio.postChannelRouting(*audioPlan);
                    const bool committed =
                        posted == RoutingStatus::posted && controller.commitPreparedChange(*mutation);
                    callbackAllocationProbe = previous;
                    require(posted == RoutingStatus::posted && committed && audioPlan->wasPosted() &&
                                callbackAllocations.load() == allocations &&
                                callbackDestructions.load() == destructions,
                            "Exact post then canonical commit has no observed C++ allocation/destruction");
                    require(controller.project.channel(channel)->mixer == destination &&
                                controller.revision == revision + 1 && nativeState() == originalNative,
                            "Canonical routing and real history commit exactly once without native output "
                            "writes");
                    controller.deliverPendingNotifications();
                    require(audio.channelRoutingReadback().status == RoutingStatus::posted,
                            "Posted application transaction does not claim completion before DSP");
                    target = destination;
                    drive(historyDirection < 0   ? "application_actual_undo_adoption"
                          : historyDirection > 0 ? "application_actual_redo_adoption"
                                                 : "application_actual_route_adoption",
                          127);
                    audio.reclaimPluginHosts(); // Real CONTROL selection/retirement service.
                    const auto selected = audio.channelRoutingReadback();
                    require(selected.status == RoutingStatus::selected && selected.overlayActive &&
                                selected.postedSerial != 0 &&
                                selected.selectedSerial == selected.postedSerial,
                            "Application completion matches the exact actual selected native request");
                    drive("application_retained_decoder_after_commit", 129);
                };
                transact(2, 0);
                transact(1, -1);
                transact(2, 1);
                transact(1, 0);
                {
                    const auto state = controller.project.state().toXmlString();
                    const auto revision = controller.revision;
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> noOp;
                    const auto unchanged = audio.prepareChannelRouting(controller.project, noOp);
                    auto invalid = controller.project;
                    invalid.channel(channel)->mixer = 999;
                    const auto refused = audio.prepareChannelRouting(invalid, noOp);
                    require(unchanged.status == RoutingStatus::noOp && !noOp &&
                                refused.status == RoutingStatus::invalidProject && !noOp &&
                                controller.project.state().toXmlString() == state &&
                                controller.revision == revision && nativeState() == originalNative,
                            "Actual no-op and invalid target preserve source, history, native owners and "
                            "output ticket");
                }
                audio.stop();
            });
    }
    catch (const std::exception &error)
    {
        callbackAllocationProbe = false;
        passed = false;
        report += "FAILED " + juce::String(error.what()) + "\n";
        audio.stop();
    }
    // Planar little-endian float32 preserves exact observed bits, including a
    // non-finite failure that an integer WAV writer cannot faithfully retain.
    const auto raw = output.getChildFile("actual-output-planar-float32le.raw");
    require(!raw.exists(), "Do not overwrite raw Wave failure evidence");
    auto rawStream = raw.createOutputStream();
    require(bool(rawStream), "Exact raw Wave PCM output");
    for (int c = 0; c < 2; ++c)
        require(rawStream->write(actual.getReadPointer(c), std::size_t(framesWritten) * sizeof(float)),
                "Retain original observed float bits before any WAV conversion");
    rawStream.reset();
    writePcm(output.getChildFile("actual-output.wav"), actual, framesWritten);
    writePcm(output.getChildFile("analytical-expected.wav"), expected, framesWritten);
    report +=
        "pass=" + juce::String(int(passed)) + ";frames=" + juce::String(framesWritten) +
        "\nActual canonical private-document route/Undo/Redo/back included. Not qualified: concurrent "
        "preparation, Pattern AudioClip "
        "playback (currently Song only), resampling/time-stretch, physical device, desktop, listening.\n";
    require(output.getChildFile("result.txt").replaceWithText(report) &&
                output.getChildFile("callbacks.csv").replaceWithText(csv),
            "Retain exact Wave PCM and terminal result");
    require(passed, "Actual native Wave routing context experiment failed; inspect retained "
                    "analytical/actual WAV and CSV");
}
