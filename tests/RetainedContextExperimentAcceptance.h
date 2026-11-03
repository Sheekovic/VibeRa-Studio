// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RetainedContextFixture.h"
#include "DetachedGraphPluginAcceptance.h"
#include "RetainedContextGraphAcceptance.h"

// Include after ParameterAcceptance.h and awaitProjection declaration. Actual
// original SDK worker + actual same-Edit native graph, not installed-provider or
// human listening qualification. This experiment must remain red on real gaps.
// Prime period251 deliberately differs from256-frame callback/worker quanta.
// Binary fractions make exact equality meaningful without phase/gain fitting.
static float retainedContextOriginalSample(juce::int64 frame) noexcept
{
    return .125f + float(frame % 251 - 125) / 8192.f;
}
static void retainedContextExperimentTests(const juce::File &folder, const juce::File &binary)
{
    struct Storage
    {
        juce::String old = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &root)
        {
            require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                               root.getFullPathName().toWideCharPointer()) == 0,
                    "Use disposable fixture-owned native preferences/cache");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", old.toWideCharPointer());
        }
    } storage(folder.getChildFile("storage"));
    auto spec = parameterSpec(binary);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Actual original SDK aux/latency provider is mandatory");
    const auto sample = folder.getChildFile("original-eighth-waveform.wav");
    require(!sample.exists(), "Original sample fixture must not overwrite a valuable input");
    {
        juce::AudioBuffer<float> pcm(2, 48000 * 8);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
                pcm.setSample(channel, frame, retainedContextOriginalSample(frame));
        juce::WavAudioFormat format;
        auto stream = sample.createOutputStream();
        require(bool(stream), "Create original power-of-two sample");
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "Write independent exact binary-fraction/prime-period stereo source");
    }
    Controller controller;
    controller.project.channels.resize(1);
    auto &channel = controller.project.channels.front();
    channel.source = sample.getFullPathName();
    channel.gain = 1;
    channel.pan = 0;
    channel.mixer = 1;
    channel.sampler.loop = true;
    auto &pattern = controller.project.patterns.front();
    pattern.length = 64;
    pattern.notes.clear();
    controller.project.placements.clear();
    for (auto &insert : controller.project.mixer)
    {
        insert.gain = 1;
        insert.pan = 0;
        insert.effects.clear();
    }
    controller.project.mixer[1].routes = {
        {2, .5f}}; // Independent main level, no direct source leak to Master.
    controller.project.mixer[2].routes = {{0, 1}};
    const auto patternId = pattern.id;
    spec.id = controller.project.allocate(); // Explicit reserved fixture ID, not a saved plugin operation.
    CommandArgs tempo;
    tempo.value = 121;
    require(controller.execute("project.tempo", tempo).wasOk() && controller.undoEditChecked().wasOk() &&
                controller.undo.canRedo(),
            "Seed genuine Controller Undo/Redo state before graph experiment");
    require(controller.project.validate().wasOk(), "Original held-source project is a valid actual Project");
    AudioEngine audio(false, folder.getChildFile("guide.json"));
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
            "Exact unchanged48k256 synthetic-only output and linear pan prerequisites");
    require(awaitProjection(audio, controller.project, true, patternId).wasOk(),
            "Prepare genuine production sample Edit");
    juce::String log =
        "Original actual same-Edit retained graph experiment. Production history OFF.\n"
        "Expected stereo=.75*source(inputFrame-256), source=.125+(frame%251-125)/8192; no phase/gain "
        "fitting.\n"
        "No physical device/input, installed-provider, human listening or production transaction claim.\n";
    juce::String csv = "phase,callback,sourceCalls,submitted,processed,missed,dropped,heldOld,heldCandidate,"
                       "maxError,callbackMs,newCalls,deleteCalls,headAdvance,playing,sameContext\n";
    bool pass = true;
    const auto report = folder.getChildFile("retained-context.txt");
    const auto traces = folder.getChildFile("retained-context.csv");
    require(report.replaceWithText(log), "Retain original experiment start before any risky operation");
    try
    {
        audio.withRetainedContextFixture(
            spec,
            [&](PluginHost &host)
            {
                const auto catalogue = parameterCatalogue(host);
                require(parameterResult(host, host.setParameter(17, 0, catalogue.generation)).result == 0 &&
                            parameterResult(host, host.setParameter(71, .25f, catalogue.generation)).result ==
                                0,
                        "Actual native0 delay/stereo detector amount acknowledged before graph allocation");
                // Native control acknowledgement precedes the DSP-side parameter
                // application. Prime that original provider ONCE before a graph
                // exists; do not infer declared latency from the control reply.
                juce::AudioBuffer<float> preparation(2, 256);
                preparation.clear();
                const auto processed = host.statistics().processed;
                host.process(preparation, 0, 256, nullptr, 0, 0, 120, false, false);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.;
                while ((host.statistics().processed == processed || host.latencySamples() != 256) &&
                       host.alive() && juce::Time::getMillisecondCounterHiRes() < deadline)
                    dispatchMessages(2); // Test preparation only, never inside a callback.
                require(host.statistics().processed == processed + 1 && host.latencySamples() == 256,
                        "Original provider must actually process/declare native0 before graph preparation");
                host.service();
            },
            [&](RetainedContextFixture &fixture)
            {
                // Pin the native transaction boundary, as the related retained
                // Wave/Instrument fixtures do. Edit's350ms idle timer otherwise
                // closes it during CONTROL message dispatch, invalidating the
                // exact history guard despite unchanged graph/project owners.
                // Direct history mutations remain visible to every assertion.
                te::Edit::UndoTransactionInhibitor nativeHistoryBatch(fixture.edit);
                constexpr int block = 256;
                juce::AudioBuffer<float> output(2, block), captured(2, 256 * block);
                captured.clear();
                int calls = 0, noteStart = -1;
                std::array<te::Plugin::Ptr, 3> published;
                const auto pid = fixture.worker->processId();
                const auto instance = fixture.worker->runtimeIdentity();
                const auto starts = device->starts, stops = device->stops;
                auto canonicalHistoryGuard = controller.undo.prepareRedoMutation();
                require(bool(canonicalHistoryGuard),
                        "Pin actual canonical manager metadata guard, without executing history");
                auto canonical = [&]
                {
                    return controller.project.state().toXmlString() + "/" + controller.projectIdentity + "/" +
                           juce::String(juce::int64(controller.revision)) + "/" +
                           juce::String(int(controller.dirty)) + "/" +
                           juce::String(controller.undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
                           juce::String(controller.undo.getNumActionsInCurrentTransaction()) + "/" +
                           controller.undo.getUndoDescriptions().joinIntoString("|") + "/" +
                           controller.undo.getRedoDescriptions().joinIntoString("|") + "/" +
                           controller.undo.getCurrentTransactionName() + "/" +
                           juce::String(
                               int(controller.undo.isPreparedMutationCurrent(*canonicalHistoryGuard)));
                };
                auto capture = [&] { return fixture.captureOwners() + "\ncanonical=" + canonical(); };
                log += "workerPid=" + juce::String(pid) + " instance=" + instance + "\n";
                auto sourceCalls = [&]
                {
                    return fixture.samplerCalls(fixture.original[0]) +
                           (published[0] ? fixture.samplerCalls(published[0]) : 0);
                };
                auto drive = [&](const char *phase, int count, bool verify)
                {
                    for (int frame = 0; frame < count; ++frame)
                    {
                        require(calls < 256, "Bounded original PCM capture capacity");
                        const auto beforeSource = sourceCalls();
                        const auto before = fixture.worker->statistics();
                        const auto beforeHead = audio.projectionDiagnostics();
                        const auto allocBefore = callbackAllocations.load(),
                                   deleteBefore = callbackDestructions.load();
                        const auto began = juce::Time::getHighResolutionTicks();
                        callbackAllocationProbe = true;
                        device->process(output); // ONLY the actual current engine graph is processed.
                        callbackAllocationProbe = false;
                        const auto ms = SyntheticCallbackTiming::milliseconds(
                            began, juce::Time::getHighResolutionTicks());
                        const auto allocations = callbackAllocations.load() - allocBefore;
                        const auto deletes = callbackDestructions.load() - deleteBefore;
                        const auto afterHead = audio.projectionDiagnostics();
                        const auto headAdvance = std::llround(afterHead.headSeconds * 48000.) -
                                                 std::llround(beforeHead.headSeconds * 48000.);
                        const bool playing = beforeHead.headPlaying && beforeHead.transportPlaying &&
                                             afterHead.headPlaying && afterHead.transportPlaying;
                        const bool sameContext =
                            beforeHead.playbackContext == afterHead.playbackContext &&
                            beforeHead.playbackContext == reinterpret_cast<std::uintptr_t>(&fixture.context);
                        const auto sourceDelta = sourceCalls() - beforeSource;
                        float maximumError = 0;
                        for (int channel = 0; channel < 2; ++channel)
                        {
                            captured.copyFrom(channel, calls * block, output, channel, 0, block);
                            for (int sample = 0; sample < block; ++sample)
                            {
                                const auto value = output.getSample(channel, sample);
                                maximumError =
                                    !std::isfinite(value)
                                        ? std::numeric_limits<float>::infinity()
                                        : std::max(maximumError,
                                                   std::abs(value -
                                                            (noteStart >= 0 && (calls - noteStart) * block +
                                                                                       sample - block >=
                                                                                   0
                                                                 ? .75f * retainedContextOriginalSample(
                                                                              (calls - noteStart) * block +
                                                                              sample - block)
                                                                 : 0.f)));
                            }
                        }
                        ++calls;
                        // Explicit serialized original-provider pacing outside DSP;
                        // no GUI/native input and no worker wait in the callback.
                        dispatchMessages(6);
                        fixture.worker->service();
                        const auto after = fixture.worker->statistics();
                        const auto oldHeld = fixture.samplerHeldKeys(fixture.original[0]);
                        const auto newHeld = published[0] ? fixture.samplerHeldKeys(published[0]) : -1;
                        csv += juce::String(phase) + "," + juce::String(calls) + "," +
                               juce::String(juce::int64(sourceDelta)) + "," +
                               juce::String(juce::int64(after.submitted - before.submitted)) + "," +
                               juce::String(juce::int64(after.processed - before.processed)) + "," +
                               juce::String(juce::int64(after.missed)) + "," +
                               juce::String(juce::int64(after.dropped)) + "," + juce::String(oldHeld) + "," +
                               juce::String(newHeld) + "," + juce::String(maximumError, 9) + "," +
                               juce::String(ms, 6) + "," + juce::String(allocations) + "," +
                               juce::String(deletes) + "," + juce::String(headAdvance) + "," +
                               juce::String(int(playing)) + "," + juce::String(int(sameContext)) + "\n";
                        if (verify)
                            pass = pass && maximumError == 0 && sourceDelta == 1 &&
                                   after.submitted - before.submitted == 1 &&
                                   after.processed - before.processed == 1 && after.missed == before.missed &&
                                   after.dropped == before.dropped && allocations == 0 && deletes == 0 &&
                                   ms <= 1000. * block / 48000. && fixture.worker->alive() &&
                                   fixture.worker->processId() == pid &&
                                   fixture.worker->runtimeIdentity() == instance &&
                                   device->starts == starts && device->stops == stops &&
                                   headAdvance == block && playing && sameContext;
                    }
                    require(traces.replaceWithText(csv),
                            "Preserve all actual callback/provider evidence before aggregate assertion");
                };
                audio.setLooping(false);
                audio.playStop();
                dispatchMessages(20);
                drive("initial_empty_graph_warmup", 8, false);
                noteStart = calls; // Exact first input-voice callback; never fitted from output.
                audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                          juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("original_one_held_note_warmup", 16, false);
                drive("independent_steady_oracle", 4, true);
                require(fixture.samplerHeldKeys(fixture.original[0]) == 1,
                        "Genuine source monitor holds one input key before any candidate");
                require(folder.getChildFile("owners-before.txt").replaceWithText(capture()),
                        "Retain exact native/app/canonical owner baseline");
                auto resolver = [&](std::array<te::Plugin::Ptr, 3> replacements)
                {
                    return [originals = fixture.original, replacements](const te::PluginList &list)
                    {
                        te::Plugin::Array result;
                        for (auto *plugin : list)
                        {
                            te::Plugin::Ptr chosen = plugin;
                            for (unsigned kind = 0; kind < 3; ++kind)
                                if (replacements[kind] && plugin == originals[kind].get())
                                    chosen = replacements[kind];
                            result.add(chosen);
                        }
                        return result;
                    };
                };
                for (unsigned kind = 0; kind < 3; ++kind)
                {
                    try
                    {
                        vibera::draft::detachedPluginConstructionTests(
                            fixture.edit, fixture.original[kind],
                            [&](const juce::ValueTree &state) { return fixture.create(kind, state); },
                            [&](te::Plugin::Ptr candidate)
                            {
                                auto overlay = std::make_shared<te::EditPlaybackContext::GraphOverlay>();
                                std::array<te::Plugin::Ptr, 3> replacement;
                                replacement[kind] = candidate;
                                overlay->plugins = resolver(replacement);
                                overlay->fixedInitialisationAdmission =
                                    fixture.fixedInitialisationAdmission(replacement);
                                return fixture.context.prepareGraphOnly(overlay);
                            });
                        log += "constructionKind=" + juce::String(kind) +
                               " passed actual native preparation/discard/refusal\n";
                    }
                    catch (const std::exception &error)
                    {
                        pass = false;
                        log += "constructionKind=" + juce::String(kind) + " FAILED " + error.what() + "\n";
                    }
                    drive("after_original_adapter_construction_checks", 4, true);
                    require(report.replaceWithText(log),
                            "Retain each original adapter result, including failures");
                }
                for (unsigned kind = 0; kind < 3; ++kind)
                    published[kind] =
                        kind == 0 ? fixture.createRetainingSampler(fixture.original[0]->state.createCopy())
                                  : fixture.create(kind, fixture.original[kind]->state.createCopy());
                require(fixture.samplerRuntimeIdentity(published[0]) ==
                                fixture.samplerRuntimeIdentity(fixture.original[0]) &&
                            fixture.samplerRuntimeAttachments(fixture.original[0]) == 1,
                        "Private successor pins exact DSP owner without attaching or copying mutable voices");
                const auto before = capture();
                try
                {
                    vibera::test::retained_context::run(
                        fixture.context, resolver(published), fixture.fixedInitialisationAdmission(published),
                        capture, [&](const char *phase) { drive(phase, 8, true); }, require,
                        [&](const char* phase, const juce::String& baseline, const juce::String& current)
                        {
                            const auto retainSnapshot = [&](const char* suffix, const juce::String& snapshot)
                            {
                                const auto file = folder.getChildFile(juce::String(phase) + suffix);
                                require(!file.exists(), "Fresh exact native registration snapshot path");
                                juce::FileOutputStream stream(file);
                                require(stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk() &&
                                            stream.write(snapshot.toRawUTF8(), std::size_t(snapshot.getNumBytesAsUTF8())),
                                        "Retain exact native/app/history snapshot before comparison");
                                stream.flush();
                                require(stream.getStatus().wasOk(), "Flush exact registration snapshot");
                            };
                            // CONTROL-only evidence; no normalization, extra capture or message pump.
                            retainSnapshot("-before.txt", baseline);
                            retainSnapshot("-after.txt", current);
                        });
                    require(capture() == before,
                            "All retained native and canonical registrations/history remain exact");
                    // No new NoteOn is sent; shared DSP must actually retain the
                    // original monitor and voices through real native adoption.
                    pass = pass && fixture.samplerHeldKeys(published[0]) == 1;
                    log += "actualContextCases=completed; heldCandidate=" +
                           juce::String(fixture.samplerHeldKeys(published[0])) + "\n";
                }
                catch (const std::exception &error)
                {
                    pass = false;
                    log += "actualContextCases FAILED " + juce::String(error.what()) + "\n";
                }
                // Always retain audio before asserting; a real carryover/stream gap
                // is a red product result, never skipped or turned into WILL_FAIL.
                const auto pcmFile = folder.getChildFile("retained-context-output.wav");
                juce::WavAudioFormat format;
                auto stream = pcmFile.createOutputStream();
                require(bool(stream), "Create original actual-output evidence");
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
                require(writer && writer->writeFromAudioSampleBuffer(captured, 0, calls * block),
                        "Preserve exact captured actual output");
                require(folder.getChildFile("owners-after.txt").replaceWithText(capture()),
                        "Retain exact owner/history state after actual graph cases");
                audio.stop();
            });
    }
    catch (const std::exception &error)
    {
        pass = false;
        log += "fixture/driver FAILED " + juce::String(error.what()) + "\n";
    }
    log +=
        "pass=" + juce::String(int(pass)) +
        "\nUnqualified: generic structural transactions, external instrument/native automation stream carryover, "
        "native ID allocator peek, queued native mirror timers, installed providers, hardware/listening.\n";
    require(report.replaceWithText(log) && traces.replaceWithText(csv),
            "Retain failed experiment evidence before terminal assertion");
    require(pass, "Actual retained context original-adapter musical experiment failed; inspect preserved "
                  "PCM/CSV/report");
}
