// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginDynamicLatencyAcceptance.h"
#include <tracktion_graph/tracktion_graph.h>
// EditPlaybackContext is already defined by the Tracktion module umbrella.
// Its internal header is intentionally not safe to include a second time.
#include <limits>

// Original default AudioEngine acceptance. No configureCompensationHistory,
// phase-pair binding, private initial source option or foreign graph injection.
// Include after existing actual parameter/state/readWav/awaitProjection helpers.
static void pluginDefaultDynamicLatencyTests(const juce::File &folder, const juce::File &binary)
{
    using namespace mixer_dynamic_detail;
    require(folder.createDirectory().wasOk() && binary.existsAsFile(), "Owned default-latency SDK fixture");
    const auto original = parameterSpec(binary);
    require(original.name == "VibeRa Mixer Latency Fixture" && !original.instrument,
            "Exact original native latency SDK effect");
    const auto staircase = folder.getChildFile("original-dyadic-staircase.wav");
    const auto constant = folder.getChildFile("original-held-constant.wav");
    for (const bool steps : {true, false})
    {
        // Longer than Capture's entire physical bound; no loop seam or fitted
        // phase can masquerade as a PDC/alignment error in this closed case.
        constexpr int length = 4096 * 256;
        juce::AudioBuffer<float> pcm(2, length);
        constexpr std::array<int, 4> levels{1, 2, 3, 2};
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < length; ++n)
                pcm.setSample(c, n, steps ? levels[std::size_t((n / 1024) % 4)] / 16.f : .125f);
        auto stream = (steps ? staircase : constant).createOutputStream();
        require(bool(stream), "Original dyadic source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, length), "Write original exact PCM");
    }
    juce::String report =
        "Actual default managerGlobal AudioEngine;48k/256 original SDK. No history/phase-pair experiment.\n"
        "C: declaration restart may interrupt/reset compensation queues. Full prefix PCM/heap/wall deadlines "
        "retained.\n"
        "After actual selection allow only declared D+Q pipeline priming; then independent per-sample "
        "alignment.\n";
    bool allPassed = true;
    for (const bool patternMode : {true, false})
    {
        const auto name = juce::String(patternMode ? "pattern" : "song");
        const auto output = folder.getChildFile(name);
        require(output.createDirectory().wasOk(), "Owned mode evidence");
        AudioEngine audio(false, {}, block);
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", double(rate));
        setup.setAttribute("audioDeviceBufferSize", block);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == rate &&
                    device->getCurrentBufferSizeSamples() == block,
                "Actual owned48k256 physical callback contract");
        require(te::getDefaultPanLaw() == te::PanLawLinear, "Existing literal linear pan law");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = staircase.getFullPathName();
        project.channels.front().gain = 1;
        project.channels.front().pan = -1;
        project.channels.front().mixer = 1;
        project.channels.front().sampler.root = 60;
        project.channels.front().sampler.loop = false;
        auto right = project.channels.front();
        right.id = project.allocate();
        right.pan = 1;
        right.mixer = 2;
        auto held = right;
        held.id = project.allocate();
        held.source = constant.getFullPathName();
        held.sampler.loop = true; // Imported one-shots deliberately ignore NoteOff otherwise.
        project.channels.push_back(right);
        project.channels.push_back(held);
        for (auto &row : project.mixer)
            row.gain = 1;
        auto &pattern = project.patterns.front();
        pattern.length = 64;
        const auto patternId = pattern.id;
        pattern.notes = {{project.allocate(), project.channels.front().id, 0, 64, 60, 1},
                         {project.allocate(), right.id, 0, 64, 60, 1}};
        project.placements = {{project.allocate(), patternId, 0, 0, 64, 0}};
        require(setMixerRoute(project, 1, 3, true).wasOk(), "Actual parallel dry+wet path");
        auto changed = original, unchanged = original;
        changed.id = project.allocate();
        unchanged.id = project.allocate();
        project.mixer[3].effects = {changed};
        project.mixer[2].effects = {unchanged};
        require(project.validate().wasOk() && awaitProjection(audio, project, patternMode, patternId).wasOk(),
                "Ordinary default Pattern/Song source projection");
        auto changedHost = audio.parameterHost(changed.id), unchangedHost = audio.parameterHost(unchanged.id);
        require(changedHost && unchangedHost && !changedHost->offline() && !unchangedHost->offline() &&
                    changedHost->transportLatencySamples() == preparedTransportSamples &&
                    unchangedHost->transportLatencySamples() == preparedTransportSamples,
                "Real live production3Q workers");
        const auto changedCatalogue = parameterCatalogue(*changedHost);
        const auto unchangedCatalogue = parameterCatalogue(*unchangedHost);
        require(parameterResult(*changedHost, changedHost->setParameter(17, 0, changedCatalogue.generation))
                            .result == 0 &&
                    parameterResult(*unchangedHost,
                                    unchangedHost->setParameter(17, 0, unchangedCatalogue.generation))
                            .result == 0,
                "Actual native0 parameters acknowledged before play");
        auto edits = audio.engine.getActiveEdits().getEdits();
        const auto initial = audio.projectionDiagnostics();
        require(edits.size() == 1 && reinterpret_cast<std::uintptr_t>(edits[0]) == initial.edit,
                "Public actual sole Edit inventory binds native graph readback");
        auto *const edit = edits[0];
        auto *const context = edit->getTransport().getCurrentPlaybackContext();
        require(context && reinterpret_cast<std::uintptr_t>(context) == initial.playbackContext,
                "Actual ordinary playback context");
        const auto changedInstance = changedHost->runtimeIdentity(),
                   unchangedInstance = unchangedHost->runtimeIdentity();
        audio.setMidiTarget(held.id);
        audio.setLooping(false);
        audio.playStop();
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        std::vector<te::EditPlaybackContext::GraphSelection> selections(capacity);
        int callbackOrdinal = 0; // Sole driver writer, published by Capture::complete.
        Capture capture(
            [&](juce::AudioBuffer<float> &buffer)
            {
                device->process(buffer);
                selections[std::size_t(callbackOrdinal++)] = context->getGraphSelection();
            },
            changedHost);
        struct Interval
        {
            int first, end;
            float held;
            PluginLatencyStamp native;
            juce::String phase;
        };
        std::vector<Interval> intervals;
        juce::String events =
            "phase,targetNative,firstCallback,selectedSerial,rootLatency,steadyFirst,steadyEnd,elapsedMs\n";
        const auto service = [&]
        {
            audio.reclaimPluginHosts();
            dispatchMessages(1);
        };
        const auto waitCount = [&](int wanted)
        {
            const auto start = juce::Time::getMillisecondCounter();
            while (capture.complete.load(std::memory_order_acquire) < wanted && !capture.exhausted.load() &&
                   juce::Time::getMillisecondCounter() - start < 10000)
                service();
            require(capture.complete.load(std::memory_order_acquire) >= wanted && !capture.exhausted.load(),
                    "Bounded real callback progress without callback wait");
        };
        const auto heldCount = [&]
        {
            const auto state = audio.projectionDiagnostics();
            const auto found = std::find_if(state.channels.begin(), state.channels.end(),
                                            [&](const auto &c) { return c.channel == held.id; });
            return found == state.channels.end() ? -1 : found->heldInputKeys;
        };
        constexpr float heldValue = .25f * (100.f / 127.f);
        const auto recover = [&](const juce::String &phase, int native, std::uint64_t priorSerial, int first)
        {
            const auto began = juce::Time::getMillisecondCounter();
            te::EditPlaybackContext::GraphSelection selection;
            bool ready = false;
            do
            {
                service();
                selection = context->getGraphSelection();
                ready = selection.state == tracktion::graph::GraphSelectionState::selected &&
                        selection.selectedSerial == selection.postedSerial &&
                        (!priorSerial || selection.selectedSerial != priorSerial) &&
                        selection.latencySamples == preparedTransportSamples + native &&
                        changedHost->declaredLatency().nativeSamples == native &&
                        audio.projectionDiagnostics().pluginSourceState == "active";
            } while (!ready && !capture.exhausted.load() &&
                     juce::Time::getMillisecondCounter() - began < 10000);
            require(ready,
                    "Default reclaim/timer restart actually selects expected route-wide latency graph");
            // Cold queues are a declared structural interruption, not a fitted
            // phase. This is exactly ceil((rootD+Q)/Q)+one observation quantum.
            const int prime = (preparedTransportSamples + native + block + block - 1) / block + 1;
            // Native selection publishes inside process; Capture publishes its
            // corresponding immutable record only after the whole call returns.
            waitCount(capture.complete.load(std::memory_order_acquire) + 1);
            const int complete = capture.complete.load(std::memory_order_acquire);
            int actualFirst = first;
            if (!priorSerial)
                actualFirst = 0;
            while (actualFirst < complete &&
                   selections[std::size_t(actualFirst)].selectedSerial != selection.selectedSerial)
                ++actualFirst;
            require(actualFirst < complete,
                    "Actual callback recorded the selected graph before pipeline bound");
            const int steady = actualFirst + prime;
            const auto recoveryHead = audio.projectionDiagnostics().headSeconds;
            waitCount(std::max(steady + 32, complete + 1));
            intervals.push_back({steady, steady + 32, heldValue, changedHost->declaredLatency(), phase});
            events += phase + "," + juce::String(native) + "," + juce::String(first) + "," +
                      juce::String(juce::int64(selection.selectedSerial)) + "," +
                      juce::String(selection.latencySamples) + "," + juce::String(steady) + "," +
                      juce::String(steady + 32) + "," +
                      juce::String(juce::Time::getMillisecondCounter() - began) + "\n";
            const auto now = audio.projectionDiagnostics();
            require(audio.playing() && now.headPlaying && now.transportPlaying && heldCount() == 1 &&
                        audio.midiInput.capture.active && now.headSeconds >= initial.headSeconds &&
                        now.headSeconds > recoveryHead && now.playbackContext == initial.playbackContext &&
                        changedHost->runtimeIdentity() == changedInstance &&
                        unchangedHost->runtimeIdentity() == unchangedInstance && changedHost->alive() &&
                        unchangedHost->alive(),
                    "Actual automatic playback/native state and held ownership survive default restart");
        };
        capture.start();
        juce::String failure;
        Project saved;
        try
        {
            waitCount(100);
            recover("initial-native0", 0, 0, 0);
            for (const auto target :
                 {std::pair{1.f, 1024}, std::pair{1.f / 3, 31}, std::pair{2.f / 3, 257}, std::pair{0.f, 0}})
            {
                const auto previous = context->getGraphSelection().selectedSerial;
                const auto previousNative = changedHost->declaredLatency();
                const int requestFirst = capture.complete.load(std::memory_order_acquire);
                const auto request = changedHost->setParameter(17, target.first, changedCatalogue.generation);
                PluginParameterResult reply;
                bool acknowledged = false;
                const auto began = juce::Time::getMillisecondCounter();
                while (!(acknowledged = changedHost->readParameterResult(request, reply)) &&
                       juce::Time::getMillisecondCounter() - began < 10000)
                    service();
                require(request && acknowledged && reply.result == 0 && reply.value == target.first,
                        "Actual sparse parameter17 native control acknowledgement");
                recover("native" + juce::String(target.second), target.second, previous, requestFirst);
                require(changedHost->declaredLatency() == previousNative.next(target.second),
                        "Actual declaration generation advances exactly once per changed native mode");
            }
            for (const bool bypass : {true, false})
            {
                const auto before = context->getGraphSelection().selectedSerial;
                const int first = capture.complete.load(std::memory_order_acquire);
                project.plugin(changed.id)->bypass = bypass;
                require(audio.rebuild(project, patternMode, patternId).wasOk(),
                        "Ordinary live effect bypass publication");
                waitCount(first + 32);
                intervals.push_back({first, first + 32, heldValue, changedHost->declaredLatency(),
                                     bypass ? "bypass-on" : "bypass-off"});
                require(context->getGraphSelection().selectedSerial == before && audio.playing(),
                        "Ordinary bypass is smooth control, no structural restart/priming allowance");
            }
            require(audio.requestPluginState(project).wasOk(), "Real current native state capture admitted");
            juce::Result captured = juce::Result::fail("Pending");
            bool done = false;
            const auto began = juce::Time::getMillisecondCounter();
            while (!(done = audio.pollPluginState(saved, captured)) &&
                   juce::Time::getMillisecondCounter() - began < 10000)
                service();
            require(done && captured.wasOk() && saved.plugin(changed.id)->state.isNotEmpty(),
                    "Actual native final mode captured with current project");
            require(changedHost->declaredLatency().nativeSamples == 0,
                    "Final saved native mode0 remains actual current mode");
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
            const int released = capture.complete.load(std::memory_order_acquire) + 40;
            waitCount(released + 32);
            require(heldCount() == 0, "Original held NoteOff releases after every declaration/restart");
            intervals.push_back({released, released + 32, 0.f, changedHost->declaredLatency(), "released"});
        }
        catch (const std::exception &error)
        {
            failure = error.what();
        }
        capture.stop();
        capture.write(output, "default");
        require(output.getChildFile("default-controls.csv").replaceWithText(events),
                "Retain default control/selection timing");
        juce::String graphTrace = "callback,posted,selected,latency,referenceStart,referenceEnd\n";
        int overruns = 0, pacingMisses = 0, nonfinite = 0, packetErrors = 0;
        unsigned allocations = 0, destructions = 0;
        double maxError = 0, maxMs = 0;
        const int count = capture.complete.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const auto &f = capture.frames[std::size_t(i)];
            const auto &g = selections[std::size_t(i)];
            graphTrace += juce::String(i) + "," + juce::String(juce::int64(g.postedSerial)) + "," +
                          juce::String(juce::int64(g.selectedSerial)) + "," + juce::String(g.latencySamples) +
                          "," + juce::String(g.referenceStart) + "," + juce::String(g.referenceEnd) + "\n";
            maxMs = std::max(maxMs, f.callbackMs);
            overruns += f.callbackMs > block * 1000. / rate;
            pacingMisses += f.releaseLatenessMs > block * 1000. / rate;
            allocations += f.allocations;
            destructions += f.destructions;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < block; ++n)
                    nonfinite += !std::isfinite(capture.pcm[std::size_t(c)][std::size_t(i * block + n)]);
        }
        for (const auto &interval : intervals)
        {
            for (int i = interval.first; i < interval.end; ++i)
            {
                const auto &f = capture.frames[std::size_t(i)];
                packetErrors +=
                    f.consumedNativeLatency != interval.native.nativeSamples ||
                    f.consumedGeneration != interval.native.generation ||
                    (i > interval.first &&
                     f.consumedSequence != capture.frames[std::size_t(i - 1)].consumedSequence + 1);
            }
            for (int n = interval.first * block; n < interval.end * block; ++n)
            {
                const auto left = capture.pcm[0][std::size_t(n)], rightValue = capture.pcm[1][std::size_t(n)];
                const double levelError = std::min({std::abs(double(left) - .25), std::abs(double(left) - .5),
                                                    std::abs(double(left) - .75)});
                maxError = std::max({maxError, levelError,
                                     std::abs(double(left) - 2. * (double(rightValue) - interval.held))});
            }
        }
        require(output.getChildFile("default-selected-graphs.csv").replaceWithText(graphTrace),
                "Actual per-callback graph selection/latency/reference retained");
        audio.midiInput.stopCapture();
        audio.stop();
        if (failure.isEmpty())
        {
            const auto file = output.getChildFile("actual-native-state.vibra");
            Project restored;
            const auto save = saveProject(saved, file);
            const auto load = loadProject(file, restored);
            auto collectedExpected = saved;
            bool samplesMatch = load.wasOk();
            for (auto &channel : collectedExpected.channels)
                if (!channel.instrument && !channel.source.startsWith("builtin:"))
                {
                    const auto *loaded = restored.channel(channel.id);
                    const auto collected = loaded ? juce::File(loaded->source) : juce::File{};
                    samplesMatch = samplesMatch && loaded && collected.isAChildOf(output) &&
                                   collected.existsAsFile() &&
                                   juce::SHA256(collected) == juce::SHA256(juce::File(channel.source));
                    if (loaded)
                        channel.source = loaded->source;
                }
            if (save.failed() || load.failed() || !samplesMatch ||
                !restored.state().isEquivalentTo(collectedExpected.state()))
                failure = "Exact native project and independently verified portable sample roundtrip";
            else
            {
                {
                    PluginHost reopened;
                    const auto native = reopened.start(*restored.plugin(changed.id), rate, block, true);
                    require(native.wasOk(),
                            "Actual independent worker reopens exact saved opaque native state");
                    const auto restoredCatalogue = parameterCatalogue(reopened);
                    require(nativeParameter(restoredCatalogue, 17).value == 0.f &&
                                reopened.declaredLatency().nativeSamples == 0,
                            "Saved state restores actual mode0 in a fresh worker, not merely nonempty bytes");
                }
                // No live held lane is invented in the document. Its two real
                // score paths remain, so exported L must equal2R on EVERY frame.
                restored.placements.front().length = 4;
                const auto wav = output.getChildFile("actual-saved-native-song.wav");
                const auto rendered = audio.renderSync(restored, wav, rate, 24, block);
                if (rendered.failed())
                    failure = rendered.getErrorMessage();
                else
                {
                    double actualRate = 0;
                    const auto pcm = readWav(wav, actualRate);
                    double offlineError = 0;
                    if (pcm.getNumChannels() == 2)
                        for (int n = 0; n < pcm.getNumSamples(); ++n)
                        {
                            const auto l = pcm.getSample(0, n), r = pcm.getSample(1, n);
                            if (!std::isfinite(l) || !std::isfinite(r))
                                offlineError = std::numeric_limits<double>::infinity();
                            else
                                offlineError = std::max(offlineError, std::abs(double(l) - 2. * r));
                        }
                    if (actualRate != rate || pcm.getNumChannels() != 2 || pcm.getNumSamples() == 0 ||
                        !std::isfinite(offlineError) || offlineError > 2.e-6 ||
                        pcm.getMagnitude(0, pcm.getNumSamples()) < .1f)
                        failure = "Actual saved-state offline musical alignment/nonzero PCM";
                }
            }
        }
        const auto changedStats = changedHost->statistics(), otherStats = unchangedHost->statistics();
        const bool clean = failure.isEmpty() && !capture.exhausted.load() && !capture.pacingFailed.load() &&
                           !nonfinite && !overruns && !pacingMisses && !packetErrors && !allocations &&
                           !destructions && maxError < 2.e-6 && !changedStats.missed &&
                           !changedStats.dropped && !otherStats.missed && !otherStats.dropped &&
                           intervals.size() == 8;
        report += name + " pass=" + juce::String(int(clean)) + " failure=" + failure +
                  " callbackCount=" + juce::String(count) + " maxPCMError=" + juce::String(maxError, 10) +
                  " nonfinite=" + juce::String(nonfinite) + " overruns=" + juce::String(overruns) +
                  " pacingMisses=" + juce::String(pacingMisses) +
                  " packetErrors=" + juce::String(packetErrors) + " maxCallbackMs=" + juce::String(maxMs, 6) +
                  " allocations=" + juce::String(allocations) +
                  " destructions=" + juce::String(destructions) + "\n";
        allPassed = allPassed && clean;
    }
    require(folder.getChildFile("default-dynamic-latency.txt").replaceWithText(report),
            "Retain independent mode outcomes");
    require(allPassed, "Default live dynamic native latency recovers with exact parallel/held PCM, ordinary "
                       "bypass and saved offline equivalence");
}
