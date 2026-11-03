// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// Include after actual parameter helpers, readWav/awaitProjection and SyntheticDevice.
// Original native SDK, real AudioEngine Pattern/Song projections, serialized CONTROL.
// Actual fixed256-frame provider; production prepared3Q transport is asserted independently.
// Standalone legacy1Q/partial-span tests retain their original policy and oracles.
__declspec(noinline) static void mixerChainLiveTests(const juce::File &folder,
                                                     const juce::File &parameterBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned live-chain attempt preserves earlier failures");
    const auto originalSpec = parameterSpec(parameterBinary);
    require(!originalSpec.instrument && originalSpec.name == "VibeRa Parameter Fixture",
            "Actual isolated scan of exact original SDK effect, not installed-provider qualification");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), parameterBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact executable/worker/original-fixture hashes");
    constexpr int quantum = 256, sampleFrames = 16384, noteFrames = 12000;
    constexpr int transportPackets = 3, transportFrames = 768;
    constexpr double rate = 48000.;
    constexpr std::array<float, 2> bed{.125f, -.0625f};
    const auto source = folder.getChildFile("original-signed-unity-bed.wav");
    {
        juce::AudioBuffer<float> pcm(2, sampleFrames);
        for (int c = 0; c < 2; ++c)
            std::fill_n(pcm.getWritePointer(c), sampleFrames, bed[std::size_t(c)]);
        auto stream = source.createOutputStream();
        require(bool(stream), "Owned original source file");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 32, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, sampleFrames),
                "Write independently authored signed stereo sample bed");
    }
    {
        double sourceRate = 0;
        const auto pcm = readWav(source, sourceRate);
        require(sourceRate == rate && pcm.getNumChannels() == 2 && pcm.getNumSamples() == sampleFrames,
                "Genuine same-rate stereo fixture, no transposition or normalization");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < sampleFrames; ++n)
                require(pcm.getSample(c, n) == bed[std::size_t(c)],
                        "Source roundtrip exact binary fractions");
    }
    require(folder.getChildFile("source-identity.txt")
                .replaceWithText(source.getFullPathName() + " sha256=" + juce::SHA256(source).toHexString() +
                                 "\n"),
            "Exact original sample identity for separate offline input preflight");
    struct Storage
    {
        juce::String previous =
            juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &path)
        {
            require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                               path.getFullPathName().toWideCharPointer()) == 0,
                    "Owned native settings/cache");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer());
        }
    } storage(folder.getChildFile("storage"));
    struct RenderCase
    {
        const char *name;
        Project project;
        float coefficient;
        std::array<float, 2> livePlateau;
    };
    std::vector<RenderCase> renders;
    for (const bool patternMode : {true, false})
    {
        const auto output = folder.getChildFile(patternMode ? "pattern" : "song");
        require(!output.exists() && output.createDirectory().wasOk(), "Fresh native mode output");
        Controller controller;
        auto &project = controller.project;
        project.tempo = 120;
        project.channels.resize(1);
        auto &channel = project.channels.front();
        channel.source = source.getFullPathName();
        channel.instrument.reset();
        channel.gain = 1;
        channel.pan = 0;
        channel.mixer = 1;
        channel.sampler = {};
        channel.sampler.loop = true;
        channel.sampler.envelope = true;
        channel.sampler.attack = channel.sampler.decay = channel.sampler.release = 0;
        channel.sampler.sustain = 1;
        for (auto &insert : project.mixer)
        {
            insert.gain = 1;
            insert.pan = 0;
            insert.muted = false;
            insert.effects.clear();
            insert.effectsBypassed = false;
        }
        auto spec = originalSpec;
        spec.id = project.allocate();
        project.mixer[1].effects.push_back(spec);
        auto &pattern = project.patterns.front();
        pattern.length = 1;
        pattern.autoLength = false; // Literal one-beat PCM/export and loop horizon.
        pattern.notes = {{project.allocate(), channel.id, 0, .5, 60, 1.f, false}};
        project.placements = {{project.allocate(), pattern.id, 0, 0, 1, 0}};
        const auto patternId = pattern.id;
        require(project.validate().wasOk(), "Original unity-envelope signed score and actual effect slot");
        AudioEngine audio(false, output.getChildFile("guide.json"), quantum);
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", rate);
        setup.setAttribute("audioDeviceBufferSize", quantum);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == rate &&
                    device->getCurrentBufferSizeSamples() == quantum &&
                    te::getDefaultPanLaw() == te::PanLawLinear,
                "Exact original native48k256 device and linear-pan prerequisite");
        require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                "Actual initial effect projection");
        const auto host = audio.parameterHost(spec.id);
        require(host && host->alive() && host->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                    host->transportPacketCount() == transportPackets &&
                    host->transportLatencySamples() == transportFrames &&
                    host->latencySamples() == transportFrames && host->declaredLatency().nativeSamples == 0,
                "Actual production app3Q/768 plus native0, independent of measured PCM");
        const auto metadata = parameterCatalogue(*host);
        require(nativeParameter(metadata, 0).value == .5f &&
                    nativeParameter(metadata, 0xfedcba98u).value == 0,
                "Genuine native default half-gain/zero-bias before processing");
        const auto captureState = [&]
        {
            const auto request = host->requestState();
            require(request != 0, "Native state admitted outside callback");
            const auto deadline = juce::Time::getMillisecondCounter() + 10000;
            juce::String state;
            while (!host->readState(request, state) && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep(2);
            require(state.isNotEmpty(), "Actual native state completed");
            return state;
        };
        const auto nativeState = captureState();
        const auto revision = host->parameterRevision();
        const auto pid = host->processId();
        const auto instance = host->runtimeIdentity();
        audio.setLooping(false);
        audio.seekBeat(0);
        audio.playStop();
        dispatchMessages(20);
        const auto baseline = audio.projectionDiagnostics();
        const auto graphCounts = audio.mixerGraphInitialisations();
        const auto packets = host->statistics();
        const auto reset = host->midiResetGeneration();
        const int starts = device->starts, stops = device->stops;
        require(baseline.edit != 0 && baseline.playbackContext != 0 && baseline.channels.size() == 1 &&
                    baseline.headSeconds == 0 && audio.playing() && packets.submitted == 0,
                "Prepared native graph at exact musical origin; no fitted output phase");
        juce::AudioBuffer<float> block(2, quantum), actual(2, sampleFrames), expected(2, sampleFrames);
        actual.clear();
        expected.clear();
        int clock = 0, callbacks = 0;
        double wet = 1, goal = 1, increment = 0, bypass = 0;
        int remaining = 0;
        bool passed = true;
        juce::String failure;
        juce::String csv = "phase,frame,maxError,callbackMs,new,delete,submitted,processed,missed,dropped,"
                           "wet,bypass,headSeconds\n";
        const auto retained = [&]
        {
            const auto now = audio.projectionDiagnostics();
            return now.edit == baseline.edit && now.playbackContext == baseline.playbackContext &&
                   now.replacements == baseline.replacements &&
                   now.samplerCreations == baseline.samplerCreations &&
                   now.midiUpdates == baseline.midiUpdates &&
                   now.cache.preparations == baseline.cache.preparations && now.channels.size() == 1 &&
                   now.channels[0].sampler == baseline.channels[0].sampler &&
                   now.channels[0].sample == baseline.channels[0].sample &&
                   now.channels[0].track == baseline.channels[0].track &&
                   now.channels[0].timelineMisses == baseline.channels[0].timelineMisses &&
                   now.channels[0].graphInitialisations == baseline.channels[0].graphInitialisations &&
                   audio.mixerGraphInitialisations() == graphCounts && device->starts == starts &&
                   device->stops == stops && audio.parameterHost(spec.id) == host &&
                   host->processId() == pid && host->runtimeIdentity() == instance;
        };
        const auto tick = [&](const char *phase)
        {
            require(clock + quantum <= sampleFrames, "Bounded all-startup/tail PCM storage");
            const auto &slot = project.mixer[1].effects.front();
            const bool bypassTarget = slot.bypass || project.mixer[1].effectsBypassed;
            if (goal != double(slot.wet))
            {
                goal = slot.wet;
                increment = (goal - wet) / 240.;
                remaining = 240;
            }
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            const auto began = juce::Time::getHighResolutionTicks();
            {
                struct Probe
                {
                    bool before = callbackAllocationProbe;
                    Probe()
                    {
                        callbackAllocationProbe = true;
                    }
                    ~Probe()
                    {
                        callbackAllocationProbe = before;
                    }
                } probe;
                device->process(block);
            }
            const auto callbackMs =
                SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
            const auto allocated = callbackAllocations.load() - allocations;
            const auto deleted = callbackDestructions.load() - destructions;
            double error = 0;
            for (int n = 0; n < quantum; ++n)
            {
                if (remaining > 0)
                    wet = --remaining == 0 ? goal : wet + increment;
                bypass = bypassTarget ? std::min(1., bypass + 1. / 240.) : std::max(0., bypass - 1. / 240.);
                // Same original note: release0 at exact source frame12000, plus production3Q/768.
                const bool sounding =
                    clock + n >= transportFrames && clock + n < noteFrames + transportFrames;
                for (int c = 0; c < 2; ++c)
                {
                    const double dry = sounding ? bed[std::size_t(c)] : 0;
                    const double reference =
                        ((.5 * dry) * wet + dry * (1 - wet)) * (1 - bypass) + dry * bypass;
                    const auto sample = block.getSample(c, n);
                    actual.setSample(c, clock + n, sample);
                    expected.setSample(c, clock + n, float(reference));
                    error = std::isfinite(sample) ? std::max(error, std::abs(double(sample) - reference))
                                                  : std::numeric_limits<double>::infinity();
                }
            }
            clock += quantum;
            ++callbacks;
            dispatchMessages(6); // CONTROL scheduling outside DSP and its allocation/timing region.
            host->service();
            const auto stats = host->statistics();
            const auto now = audio.projectionDiagnostics();
            const bool ok = error < 2.e-7 && callbackMs <= 256000. / 48000. && allocated == 0 &&
                            deleted == 0 && retained() && audio.playing() && !audio.looping() &&
                            host->alive() && !host->deliveryFailed() &&
                            host->parameterRevision() == revision && host->midiResetGeneration() == reset &&
                            stats.missed == packets.missed && stats.dropped == packets.dropped &&
                            stats.submitted == std::uint64_t(callbacks) &&
                            stats.processed == stats.submitted && host->wetBlend() &&
                            std::abs(double(*host->wetBlend()) - wet) < 1.e-6 && host->bypassBlend() &&
                            std::abs(double(*host->bypassBlend()) - bypass) < 1.e-6 &&
                            std::abs(now.headSeconds - double((callbacks - 1) * 256) / rate) < 1.e-9;
            csv += juce::String(phase) + "," + juce::String(clock - quantum) + "," + juce::String(error, 12) +
                   "," + juce::String(callbackMs, 6) + "," + juce::String(allocated) + "," +
                   juce::String(deleted) + "," + juce::String(juce::int64(stats.submitted)) + "," +
                   juce::String(juce::int64(stats.processed)) + "," +
                   juce::String(juce::int64(stats.missed)) + "," + juce::String(juce::int64(stats.dropped)) +
                   "," + juce::String(wet, 9) + "," + juce::String(bypass, 9) + "," +
                   juce::String(now.headSeconds, 12) + "\n";
            require(output.getChildFile("callbacks.csv").replaceWithText(csv),
                    "Retain callback observations before assertion");
            passed &= ok;
            require(ok, "Actual canonical live-chain musical/retention/deadline/observed callback-thread "
                        "heap oracle");
        };
        const auto publish = [&]
        {
            const auto state = project.state();
            const auto modelRevision = controller.revision;
            const auto undo = controller.undo.getUndoDescription(),
                       redo = controller.undo.getRedoDescription();
            const auto position = audio.projectionDiagnostics().headSeconds;
            const auto result = audio.rebuild(project, patternMode, patternId);
            require(result.wasOk() && retained() && audio.playing() &&
                        audio.projectionDiagnostics().headSeconds == position &&
                        controller.revision == modelRevision &&
                        controller.undo.getUndoDescription() == undo &&
                        controller.undo.getRedoDescription() == redo && project.state().isEquivalentTo(state),
                    "Same-topology fast publication: no waiting, graph replacement, transport reset or extra "
                    "history");
        };
        const auto mix = [&](float value)
        {
            CommandArgs args;
            args.target = spec.id;
            args.value = value;
            require(controller.execute("plugin.effect.mix", args).wasOk() &&
                        project.plugin(spec.id)->wet == value,
                    "Actual canonical wet command and model readback");
            publish();
            tick("wet_240_sample_transition");
            tick("wet_exact_plateau");
        };
        const auto chain = [&](bool value)
        {
            CommandArgs args;
            args.mixer = 1;
            args.enabled = value;
            require(controller.execute("mixer.effects.bypass", args).wasOk() &&
                        project.mixer[1].effectsBypassed == value && !project.plugin(spec.id)->bypass,
                    "Canonical chain override preserves individual slot flag");
            publish();
            tick("chain_240_sample_transition");
            tick("chain_exact_plateau");
        };
        const auto history = [&](bool redo, float target, bool chainBypassed)
        {
            require((redo ? controller.redoEditChecked() : controller.undoEditChecked()).wasOk() &&
                        project.plugin(spec.id)->wet == target &&
                        project.mixer[1].effectsBypassed == chainBypassed,
                    "Actual canonical Undo/Redo restores exact musical targets");
            publish();
            tick(redo ? "real_Redo_transition" : "real_Undo_transition");
            tick("history_exact_plateau");
        };
        const auto snapshot = [&](const char *name, float coefficient)
        {
            if (patternMode)
                renders.push_back(
                    {name, project, coefficient, {block.getSample(0, 255), block.getSample(1, 255)}});
            for (int c = 0; c < 2; ++c)
                require(block.getSample(c, 255) == bed[std::size_t(c)] * coefficient,
                        "Exact stable live plateau for independent offline comparison");
        };
        try
        {
            for (int n = 0; n < 4; ++n)
                tick("startup_ALL_samples_checked");
            mix(.25f);
            snapshot("wet25", .875f);
            history(false, 1, false);
            history(true, .25f, false);
            chain(true);
            snapshot("chain_override", 1);
            history(false, .25f, false);
            history(true, .25f, true);
            mix(.75f); // Keep processing the native effect while chain bypassed.
            history(false, .25f, true);
            history(true, .75f, true);
            chain(false);
            snapshot("wet75", .625f);
            mix(0);
            mix(1);
            // Shift only declared transport by512; retain the original800-frame post-release extent.
            while (clock < 13568)
                tick("exact_noteoff_12000_and_delayed_tail_12768");
            require(captureState() == nativeState && host->parameterRevision() == revision,
                    "Host controls preserve opaque native state; capture occurs outside callbacks");
        }
        catch (const std::exception &e)
        {
            passed = false;
            failure = e.what();
        }
        audio.stop();
        const auto raw = output.getChildFile("actual-planar-float32le.raw");
        require(!raw.exists(), "Never overwrite previous live PCM failure");
        auto stream = raw.createOutputStream();
        require(bool(stream), "Owned actual raw PCM");
        for (int c = 0; c < 2; ++c)
            require(stream->write(actual.getReadPointer(c), std::size_t(clock) * sizeof(float)),
                    "Retain actual PCM bits");
        stream.reset();
        const auto referenceFile = output.getChildFile("analytical-expected-planar-float32le.raw");
        require(!referenceFile.exists(), "Never overwrite expected musical evidence");
        auto referenceStream = referenceFile.createOutputStream();
        require(bool(referenceStream), "Owned analytical reference");
        for (int c = 0; c < 2; ++c)
            require(referenceStream->write(expected.getReadPointer(c), std::size_t(clock) * sizeof(float)),
                    "Independent reference, explicitly distinct from actual observed PCM");
        referenceStream.reset();
        require(output.getChildFile("result.txt")
                    .replaceWithText("pass=" + juce::String(int(passed)) +
                                     ";callbacks=" + juce::String(callbacks) +
                                     ";frames=" + juce::String(clock) + ";error=" + failure +
                                     "\nActual production3Q/768 engine aligned256 synthetic callbacks; not continuous "
                                     "pacing/concurrent CONTROL, "
                                     "physical device, installed-provider or listening qualification. "
                                     "Thread-local C++ heap probe "
                                     "does not instrument native graph worker threads or plugin process.\n"),
                "Terminal truthful live result");
        require(passed, "Live Mixer chain experiment failed; actual startup/tail PCM and callbacks retained");
    }
    const auto renderInputs = folder.getChildFile("render-inputs");
    require(!renderInputs.exists() && renderInputs.createDirectory().wasOk() && renders.size() == 3,
            "Three genuine canonical musical-state snapshots for separate offline acceptance");
    for (const auto &item : renders)
    {
        const auto projectFile = renderInputs.getChildFile(juce::String(item.name) + ".vibra");
        const auto plateauFile = renderInputs.getChildFile(juce::String(item.name) + "-live-plateau.bin");
        require(!projectFile.exists() && !plateauFile.exists(), "Explicit no-overwrite snapshot files");
        Project reopened;
        require(saveProject(item.project, projectFile).wasOk() &&
                    loadProject(projectFile, reopened).wasOk() && reopened.channels.size() == 1,
                "Actual persisted host mix/chain snapshot loads");
        // Saving deliberately collects media. Verify those exact bytes and the
        // owned destination, then compare every musical/model field after only
        // normalising this documented relocation (never a numeric/score fit).
        const juce::File collected(reopened.channels[0].source);
        const juce::File original(item.project.channels[0].source);
        require(collected.existsAsFile() && collected.isAChildOf(renderInputs.getChildFile("media")) &&
                    juce::SHA256(collected).toHexString() == juce::SHA256(original).toHexString(),
                "Saved project contains byte-identical collected original sample");
        auto expectedProject = item.project;
        expectedProject.channels[0].source = collected.getFullPathName();
        require(reopened.state().isEquivalentTo(expectedProject.state()),
                "Actual persisted host mix/chain/score state, with explicit media relocation");
        auto stream = plateauFile.createOutputStream();
        require(bool(stream) && stream->writeFloat(item.livePlateau[0]) &&
                    stream->writeFloat(item.livePlateau[1]),
                "Actual stable live PCM captured as exact binary floats for separate WAV equivalence");
    }
}

// Run separately AFTER successful live acceptance; output must be another fresh owned directory.
// Strict full score duration is intentional. Existing native trim concern remains a possible failure.
__declspec(noinline) static void mixerChainOfflineTests(const juce::File &folder,
                                                        const juce::File &liveAttempt,
                                                        const juce::File &parameterBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh separate WAV attempt keeps failures");
    const auto spec = parameterSpec(parameterBinary);
    require(!spec.instrument && spec.name == "VibeRa Parameter Fixture",
            "Actual original offline SDK provider");
    const auto fixtureIdentity =
        parameterBinary.getFullPathName() + " sha256=" + juce::SHA256(parameterBinary).toHexString() + "\n";
    require(liveAttempt.getChildFile("binary-identities.txt").loadFileAsString().replace("\r\n", "\n").contains(fixtureIdentity),
            "Same exact original native fixture binary used by passed live input");
    require(
        folder.getChildFile("binary-identities.txt")
            .replaceWithText("Executable=" +
                             juce::SHA256(juce::File::getSpecialLocation(juce::File::currentExecutableFile))
                                 .toHexString() +
                             "\nWorker=" + juce::SHA256(pluginWorkerExecutable()).toHexString() + "\n" +
                             fixtureIdentity),
        "Exact separate offline executable, worker and unchanged fixture identity");
    const auto inputs = liveAttempt.getChildFile("render-inputs");
    require(inputs.isDirectory(),
            "Actual prior canonical live snapshots, not invented offline target states");
    for (const auto *mode : {"pattern", "song"})
        require(liveAttempt.getChildFile(mode)
                    .getChildFile("result.txt")
                    .loadFileAsString()
                    .startsWith("pass=1;"),
                "Both actual live mode receipts passed before independent WAV branch");
    constexpr std::array<float, 2> bed{.125f, -.0625f};
    constexpr int noteFrames = 12000, renderFrames = 24000;
    constexpr double rate = 48000.;
    struct Case
    {
        const char *name;
        float wet, coefficient;
        bool bypass;
    };
    constexpr std::array<Case, 3> cases{
        {{"wet25", .25f, .875f, false}, {"chain_override", .25f, 1.f, true}, {"wet75", .75f, .625f, false}}};
    AudioEngine renderer(false, folder.getChildFile("render-guide.json"));
    for (const auto &item : cases)
    {
        const auto projectFile = inputs.getChildFile(juce::String(item.name) + ".vibra");
        const auto plateauFile = inputs.getChildFile(juce::String(item.name) + "-live-plateau.bin");
        const auto wav = folder.getChildFile(juce::String(item.name) + ".wav");
        require(projectFile.existsAsFile() && plateauFile.getSize() == 8 && !wav.exists(),
                "Required actual snapshot and two live PCM values, explicit disposable new WAV");
        Project reopened;
        require(loadProject(projectFile, reopened).wasOk() && reopened.channels.size() == 1 &&
                    reopened.mixer.size() > 1 && reopened.mixer[1].effects.size() == 1,
                "Actual saved live snapshot has one effect for independent offline verification");
        auto persistedDescriptor = juce::XmlDocument::parse(reopened.mixer[1].effects[0].descriptor);
        auto rescannedDescriptor = juce::XmlDocument::parse(spec.descriptor);
        require(persistedDescriptor && rescannedDescriptor,
                "Saved and fresh original-provider descriptors are genuine XML");
        // Each isolated scan stamps infoUpdateTime. Binary SHA and every other
        // descriptor field remain exact; the persisted descriptor was already
        // round-trip checked against its actual live canonical snapshot above.
        persistedDescriptor->removeAttribute("infoUpdateTime");
        rescannedDescriptor->removeAttribute("infoUpdateTime");
        require(juce::ValueTree::fromXml(*persistedDescriptor).isEquivalentTo(
                    juce::ValueTree::fromXml(*rescannedDescriptor)),
                "Exact provider identity excluding only its fresh scan timestamp");
        require(reopened.channels.size() == 1 &&
                    reopened.mixer.size() > 1 && !reopened.patterns.empty() &&
                    reopened.mixer[1].effects.size() == 1 &&
                    reopened.mixer[1].effects[0].wet == item.wet && !reopened.mixer[1].effects[0].bypass &&
                    reopened.mixer[1].effectsBypassed == item.bypass && reopened.tempo == 120 &&
                    reopened.endBeat() == 1 && reopened.patterns[0].notes.size() == 1 &&
                    reopened.patterns[0].notes[0].beat == 0 && reopened.patterns[0].notes[0].length == .5,
                "Actual persisted canonical mix/chain/score contract, not a hand-mutated render project");
        const juce::File source(reopened.channels[0].source);
        require(source.isAChildOf(inputs.getChildFile("media")) && source.existsAsFile() &&
                    liveAttempt.getChildFile("source-identity.txt").loadFileAsString().replace("\r\n", "\n").endsWith(
                        " sha256=" + juce::SHA256(source).toHexString() + "\n"),
                "Original signed source remains exactly the verified live input");
        auto plateauStream = plateauFile.createInputStream();
        require(bool(plateauStream), "Actual exact live plateau input");
        const std::array<float, 2> plateau{plateauStream->readFloat(), plateauStream->readFloat()};
        for (int c = 0; c < 2; ++c)
            require(plateau[std::size_t(c)] == bed[std::size_t(c)] * item.coefficient,
                    "Actual captured live plateau equals independent coefficient before rendering");
        const auto began = juce::Time::getMillisecondCounterHiRes();
        const auto result = renderer.renderSync(reopened, wav, rate, 32, 256);
        require(folder.getChildFile(juce::String(item.name) + "-render-result.txt")
                    .replaceWithText("completed=" + juce::String(int(result.wasOk())) + ";elapsedMs=" +
                                     juce::String(juce::Time::getMillisecondCounterHiRes() - began, 6) +
                                     ";error=" + result.getErrorMessage()),
                "Keep actual render failure before PCM assertions");
        require(result.wasOk(), "Actual offline original-plugin WAV completed, not merely accepted");
        double wavRate = 0;
        const auto pcm = readWav(wav, wavRate);
        bool correct = wavRate == rate && pcm.getNumChannels() == 2 && pcm.getNumSamples() == renderFrames;
        double error = 0;
        // Even a wrong-length file retains a useful independent PCM diagnostic.
        if (pcm.getNumChannels() == 2)
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < pcm.getNumSamples(); ++n)
                {
                    // Native renderer drops declared latency (NodeRenderContext64).
                    // No startup fitting/trim adjustment: file frame0 is score frame0.
                    const float reference = n < noteFrames ? bed[std::size_t(c)] * item.coefficient : 0.f;
                    const auto actual = pcm.getSample(c, n);
                    error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - reference))
                                                  : std::numeric_limits<double>::infinity();
                    correct &= actual == reference && (n >= noteFrames || actual == plateau[std::size_t(c)]);
                }
        require(folder.getChildFile(juce::String(item.name) + "-render-PCM.txt")
                    .replaceWithText("pass=" + juce::String(int(correct)) +
                                     ";frames=" + juce::String(pcm.getNumSamples()) +
                                     ";expectedFrames=24000;rate=" + juce::String(wavRate) +
                                     ";maximumError=" + juce::String(error, 12)),
                "Whole-file export evidence before independent musical oracle");
        require(correct,
                "Exact original-plugin WAV versus independent score, tail and actual stable live plateau");
    }
}
