// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RetainedContextFixture.h"
#include "RetainedContextGraphAcceptance.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>
#include <thread>
#include <exception>

// Include after RetainedContextExperimentAcceptance.h: reuses the exact original
// prime251 source, supported fixture helpers and strict musical oracle.
namespace concurrent_retained_detail
{
constexpr int rate = 48000, block = 256, capacity = 512, noteBlock = 8;
struct Frame
{
    double milliseconds = 0, releaseLatenessMs = 0;
    std::uint64_t sourceDelta = 0, submittedDelta = 0, processed = 0, missed = 0, dropped = 0;
    std::uint64_t consumedSequence = 0;
    unsigned allocations = 0, destructions = 0, markerCalls = 0;
    juce::int64 headBefore = 0, headAfter = 0, referenceAfter = 0;
    int held = 0, consumedNative = -1;
    bool playing = false, consumedValid = false, cadenceReset = false;
};
class Capture
{
    // Immutable bindings established on CONTROL before the owned thread starts.
    SyntheticDevice &device;
    PluginHost &host;
    tracktion::graph::PlayHead &head;
    std::function<std::uint64_t()> sourceCalls;
    std::function<int()> heldKeys;
    std::function<void()> originalNoteOn;
    std::shared_ptr<vibera::test::retained_context::OutputObservation> marker;
    juce::AudioBuffer<float> buffer{2, block};
    std::jthread thread;

  public:
    std::vector<Frame> frames{capacity};
    std::array<std::vector<float>, 2> pcm{std::vector<float>(capacity * block),
                                          std::vector<float>(capacity *block)};
    std::atomic<int> complete{0}, originalNoteOns{0};
    std::atomic<bool> exhausted{false};
    Capture(SyntheticDevice &d, PluginHost &h, tracktion::graph::PlayHead &p,
            std::function<std::uint64_t()> calls, std::function<int()> keys, std::function<void()> note,
            std::shared_ptr<vibera::test::retained_context::OutputObservation> observation)
        : device(d), host(h), head(p), sourceCalls(std::move(calls)), heldKeys(std::move(keys)),
          originalNoteOn(std::move(note)), marker(std::move(observation))
    {
    }
    ~Capture()
    {
        stop();
    }
    void start()
    {
        thread = std::jthread(
            [this](std::stop_token stop)
            {
                using Clock = std::chrono::steady_clock;
                const auto period = std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(double(block) / rate));
                auto due = Clock::now();
                for (int index = 0; !stop.stop_requested(); ++index)
                {
                    if (index >= capacity)
                    {
                        exhausted = true;
                        break;
                    }
                    std::this_thread::sleep_until(due); // Pacing outside the audio callback.
                    auto &f = frames[size_t(index)];
                    f.releaseLatenessMs =
                        std::chrono::duration<double, std::milli>(Clock::now() - due).count();
                    // One genuine MIDI producer message at a fixed input sample
                    // boundary. No replay/voice copying and no control-thread race
                    // choosing a note origin from an in-flight callback.
                    if (index == noteBlock)
                    {
                        originalNoteOn();
                        ++originalNoteOns;
                    }
                    const auto source = sourceCalls();
                    const auto before = host.statistics();
                    f.headBefore = head.getPosition();
                    const bool wasPlaying = head.isPlaying();
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    const auto began = juce::Time::getHighResolutionTicks();
                    callbackAllocationProbe = true;
                    device.process(buffer); // ONLY actual adopted graph; no worker wait.
                    callbackAllocationProbe = false;
                    f.milliseconds =
                        SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
                    f.allocations = callbackAllocations.load() - allocations;
                    f.destructions = callbackDestructions.load() - destructions;
                    f.headAfter = head.getPosition();
                    f.referenceAfter = head.getReferenceSampleRange().getStart();
                    f.playing = wasPlaying && head.isPlaying();
                    f.sourceDelta = sourceCalls() - source;
                    const auto after = host.statistics();
                    f.submittedDelta = after.submitted - before.submitted;
                    f.processed = after.processed; // Async observation; delta need NOT be1.
                    f.missed = after.missed;
                    f.dropped = after.dropped;
                    f.held = heldKeys();
                    if (const auto consumed = host.consumedLatency())
                    {
                        f.consumedValid = true;
                        f.consumedSequence = consumed->sequence;
                        f.consumedNative = consumed->stamp.nativeSamples;
                    }
                    f.markerCalls = marker->calls.load(std::memory_order_acquire);
                    for (int channel = 0; channel < 2; ++channel)
                        std::copy_n(buffer.getReadPointer(channel), block,
                                    pcm[size_t(channel)].data() + index * block);
                    due += period;
                    if (Clock::now() > due + period)
                    {
                        f.cadenceReset = true; // Actual driver gap remains a failed cadence gate.
                        due = Clock::now();    // Never burst callbacks to catch up.
                    }
                    complete.store(index + 1, std::memory_order_release);
                }
                callbackAllocationProbe = false;
            });
    }
    void stop()
    {
        if (thread.joinable())
        {
            thread.request_stop();
            thread.join();
        }
    }
    bool wait(int target, bool pump)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.;
        while (complete.load(std::memory_order_acquire) < target && !exhausted.load() &&
               juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            if (pump)
                dispatchMessages(1);
            else
                juce::Thread::sleep(1); // CONTROL barrier, no message reentry during preparation.
        }
        return complete.load(std::memory_order_acquire) >= target;
    }
    bool writeAndVerify(const juce::File &folder, const PluginHost::Statistics &initial,
                        juce::String &summary) const
    {
        // Thread joined before reading any frame/PCM. complete alone never grants
        // CONTROL permission to read the still-being-written next frame.
        const int count = complete.load(std::memory_order_acquire);
        bool pass = count > 24 && !exhausted.load() && originalNoteOns.load() == 1 && initial.submitted == 1;
        juce::String csv =
            "callback,absoluteSample,sourceDelta,submittedDelta,processed,consumedSequence,"
            "consumedNative,missed,dropped,held,headBefore,headAfter,referenceAfter,playing,"
            "markerCalls,maxError,callbackMs,releaseLatenessMs,cadenceReset,newCalls,deleteCalls\n";
        float maximumError = 0;
        int firstBad = -1, overruns = 0, pacingMisses = 0, firstMarker = -1, cadenceResets = 0;
        for (int index = 0; index < count; ++index)
        {
            const auto &f = frames[size_t(index)];
            float error = 0;
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < block; ++sample)
                {
                    const auto inputFrame = (index - noteBlock) * block + sample - block;
                    const auto expected =
                        inputFrame >= 0 ? .75f * retainedContextOriginalSample(inputFrame) : 0.f;
                    const auto observed = pcm[size_t(channel)][size_t(index * block + sample)];
                    error = !std::isfinite(observed) ? std::numeric_limits<float>::infinity()
                                                     : std::max(error, std::abs(observed - expected));
                }
            if (error != 0 && firstBad < 0)
                firstBad = index;
            maximumError = std::max(maximumError, error);
            overruns += f.milliseconds > 1000. * block / rate;
            pacingMisses += f.releaseLatenessMs > 1000. * block / rate;
            cadenceResets += f.cadenceReset;
            if (f.markerCalls && firstMarker < 0)
                firstMarker = index;
            pass = pass && error == 0 && f.sourceDelta == 1 && f.submittedDelta == 1 &&
                   f.missed == initial.missed && f.dropped == initial.dropped && f.allocations == 0 &&
                   f.destructions == 0 && f.consumedValid && f.consumedNative == 0 &&
                   f.consumedSequence == initial.submitted - 1 + std::uint64_t(index);
            // First transport activation may establish its reference origin.
            // Every held-note and structural-operation callback must advance256.
            if (index >= noteBlock)
                pass = pass && f.held == 1 && f.playing && f.headAfter - f.headBefore == block;
            csv += juce::String(index) + "," + juce::String(index * block) + "," +
                   juce::String(juce::int64(f.sourceDelta)) + "," +
                   juce::String(juce::int64(f.submittedDelta)) + "," +
                   juce::String(juce::int64(f.processed)) + "," +
                   juce::String(juce::int64(f.consumedSequence)) + "," + juce::String(f.consumedNative) +
                   "," + juce::String(juce::int64(f.missed)) + "," + juce::String(juce::int64(f.dropped)) +
                   "," + juce::String(f.held) + "," + juce::String(f.headBefore) + "," +
                   juce::String(f.headAfter) + "," + juce::String(f.referenceAfter) + "," +
                   juce::String(int(f.playing)) + "," + juce::String(f.markerCalls) + "," +
                   juce::String(error, 9) + "," + juce::String(f.milliseconds, 7) + "," +
                   juce::String(f.releaseLatenessMs, 7) + "," + juce::String(int(f.cadenceReset)) + "," +
                   juce::String(f.allocations) + "," + juce::String(f.destructions) + "\n";
        }
        pass = pass && overruns == 0 && pacingMisses == 0 && cadenceResets == 0 && firstMarker >= 0 &&
               marker->firstReference.load() == frames[size_t(firstMarker)].referenceAfter;
        require(folder.getChildFile("concurrent-context-callbacks.csv").replaceWithText(csv),
                "Retain EVERY callback and exact PCM verdict before assertion");
        juce::WavAudioFormat format;
        auto stream = folder.getChildFile("concurrent-context-actual-pcm.wav").createOutputStream();
        require(bool(stream), "Create owned full callback PCM evidence");
        stream->setPosition(0);
        stream->truncate();
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 32, {}, 0));
        const float *pointers[]{pcm[0].data(), pcm[1].data()};
        require(writer && writer->writeFromFloatArrays(pointers, 2, count * block),
                "Retain ALL actual PCM without selected phase exclusions");
        summary +=
            "callbacks=" + juce::String(count) + " originalNoteOns=" + juce::String(originalNoteOns.load()) +
            " maxPCMError=" + juce::String(maximumError, 9) + " firstBadCallback=" + juce::String(firstBad) +
            " overruns=" + juce::String(overruns) + " pacingMisses=" + juce::String(pacingMisses) +
            " cadenceResets=" + juce::String(cadenceResets) +
            " firstMarkerCallback=" + juce::String(firstMarker) +
            " callbackChecks=" + juce::String(int(pass)) + "\n";
        return pass;
    }
};
// Persistent overlays must not retain references to the stack driver or log.
// Only CONTROL invokes a native graph factory hook; DSP sees OutputMarker only.
struct PreparationObservation
{
    Capture *capture = nullptr;
    juce::String events = "operation,hookBeginCallback,hookEndCallback,returnedCallback\n";
    unsigned overlapHooks = 0;
};
struct DisconnectPreparation
{
    std::shared_ptr<PreparationObservation> observation;
    ~DisconnectPreparation()
    {
        observation->capture = nullptr;
    }
};
} // namespace concurrent_retained_detail

__declspec(noinline) static void concurrentRetainedContextTests(const juce::File &folder,
                                                                const juce::File &binary)
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
    // Explicit original unity-sustain source. The sampler's ordinary disabled-
    // envelope mode has a 2ms note-on ramp; it is not an instant-unity fixture.
    // Configure the musical input BEFORE projection rather than excluding onset
    // samples or fitting a phase/gain to observed output. No product DSP changes.
    channel.sampler.envelope = true;
    channel.sampler.attack = 0;
    channel.sampler.decay = 0;
    channel.sampler.sustain = 1;
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
    juce::String log = "Original CONCURRENT same-Edit retained graph experiment. Closed "
                       "sample/SDK/unchanged-document scope.\n"
                       "Original one MIDI NoteOn at callback8; ALL PCM expected "
                       ".75*prime251(inputFrame-256), no phase fitting.\n"
                       "Paced48k256, no callback worker waits; control native-factory barrier proves "
                       "callbacks advance during preparation.\n"
                       "Production retained history OFF. No native-input/installed-plugin/hardware/listening "
                       "qualification.\n";
    const auto report = folder.getChildFile("concurrent-context.txt");
    require(report.replaceWithText(log), "Preserve concurrent experiment start before native preparation");
    bool pass = true;
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
                        "Supported actual SDK native0/detector.25 control acknowledgements");
                juce::AudioBuffer<float> preparation(2, 256);
                preparation.clear();
                const auto processed = host.statistics().processed;
                host.process(preparation, 0, 256, nullptr, 0, 0, 120, false, false);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.;
                while ((host.statistics().processed == processed || host.latencySamples() != 256) &&
                       host.alive() && juce::Time::getMillisecondCounterHiRes() < deadline)
                    dispatchMessages(2);
                require(host.statistics().processed == processed + 1 && host.latencySamples() == 256,
                        "One real zero provider-preparation packet, no preview graph processing");
                host.service();
            },
            [&](RetainedContextFixture &fixture)
            {
                using namespace concurrent_retained_detail;
                using Context = te::EditPlaybackContext;
                using Observation = vibera::test::retained_context::OutputObservation;
                using Marker = vibera::test::retained_context::OutputMarker;
                // Every pointer/binding the driver reads is installed before it
                // starts. No concurrently modified shared_ptr/array/configuration.
                const RetainedContextFixture::Replacements replacements{
                    fixture.createRetainingSampler(fixture.original[0]->state.createCopy()),
                    fixture.create(1, fixture.original[1]->state.createCopy()),
                    fixture.create(2, fixture.original[2]->state.createCopy())};
                const auto runtime = fixture.samplerRuntimeIdentity(fixture.original[0]);
                require(fixture.samplerRuntimeIdentity(replacements[0]) == runtime &&
                            fixture.samplerRuntimeAttachments(fixture.original[0]) == 1,
                        "Candidate pins exact compatible Runtime without a new attachment/DSP copy");
                auto marker = std::make_shared<Observation>();
                auto *head = fixture.context.getNodePlayHead();
                require(head != nullptr, "Actual context has stable native PlayHead");
                const auto starts = device->starts, stops = device->stops;
                const auto pid = fixture.worker->processId();
                const auto instance = fixture.worker->runtimeIdentity();
                const auto initial = fixture.worker->statistics();
                auto historyGuard = controller.undo.prepareRedoMutation();
                require(bool(historyGuard), "Pin genuine canonical redo metadata before thread starts");
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
                           juce::String(int(controller.undo.isPreparedMutationCurrent(*historyGuard)));
                };
                auto captureOwners = [&] { return fixture.captureOwners() + "\ncanonical=" + canonical(); };
                audio.setLooping(false);
                audio.playStop();
                dispatchMessages(20);
                Capture capture(
                    *device, *fixture.worker, *head,
                    [&]
                    {
                        return fixture.samplerCalls(fixture.original[0]) +
                               fixture.samplerCalls(replacements[0]);
                    },
                    [&] { return fixture.samplerHeldKeys(fixture.original[0]); },
                    [&]
                    {
                        audio.midiInput.handleIncomingMidiMessage(
                            nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                    },
                    marker);
                auto preparationObservation = std::make_shared<PreparationObservation>();
                preparationObservation->capture = &capture;
                DisconnectPreparation disconnect{preparationObservation};
                bool discarded = false, lateFailure = false, staleRefused = false, adopted = false;
                juce::String controlFailure;
                auto resolver = [originals = fixture.original, replacements](const te::PluginList &list)
                {
                    te::Plugin::Array result;
                    for (auto *plugin : list)
                    {
                        te::Plugin::Ptr chosen = plugin;
                        for (unsigned kind = 0; kind < 3; ++kind)
                            if (plugin == originals[kind].get())
                                chosen = replacements[kind];
                        result.add(chosen);
                    }
                    return result;
                };
                auto overlay =
                    [&](const char *operation, std::shared_ptr<Observation> observation, bool throwLate)
                {
                    auto value = std::make_shared<Context::GraphOverlay>();
                    value->plugins = resolver;
                    value->fixedInitialisationAdmission = fixture.fixedInitialisationAdmission(replacements);
                    value->lastStage = [preparationObservation, operation = juce::String(operation),
                                        observation,
                                        throwLate](te::OutputDevice &, const te::CreateNodeParams &,
                                                   std::unique_ptr<tracktion::graph::Node> node)
                        -> std::unique_ptr<tracktion::graph::Node>
                    {
                        // Executed INSIDE native createNodeForEdit on CONTROL.
                        // No message pump/reentry. Only the control side waits;
                        // actual callbacks never wait on this test barrier.
                        if (auto *driver = preparationObservation->capture)
                        {
                            const auto began = driver->complete.load(std::memory_order_acquire);
                            const bool advanced = driver->wait(began + 3, false);
                            const auto ended = driver->complete.load(std::memory_order_acquire);
                            preparationObservation->events += operation + "," + juce::String(began) + "," +
                                                              juce::String(ended) + ",pending\n";
                            if (!advanced || ended - began < 3)
                                throw std::runtime_error(
                                    "Concurrent native factory did not overlap three actual "
                                    "callbacks within bounded control wait");
                            ++preparationObservation->overlapHooks;
                            if (throwLate)
                                throw std::runtime_error(
                                    "Original intentional concurrent late native-factory failure");
                        }
                        return std::make_unique<Marker>(std::move(node), observation);
                    };
                    return value;
                };
                capture.start();
                try
                {
                    require(capture.wait(24, true), "Actual paced warmup/one original held note completes");
                    const auto before = captureOwners(); // CONTROL only; never XML/history in DSP.
                    require(folder.getChildFile("owners-before.txt").replaceWithText(before),
                            "Retain immutable canonical/native owner baseline");
                    require(fixture.samplerHeldKeys(replacements[0]) == 1,
                            "One original held key visible through exact shared runtime");
                    {
                        auto unused = std::make_shared<Observation>();
                        auto candidate =
                            fixture.context.prepareGraphOnly(overlay("prepare_discard", unused, false));
                        require(!candidate->wasPublished() && unused->calls.load() == 0 &&
                                    captureOwners() == before,
                                "Prepared unpublished graph preserves actual live owners and never runs");
                        require(fixture.samplerRuntimeAttachments(fixture.original[0]) == 2,
                                "Distinct prepared shell attaches while incumbent continues rendering");
                        candidate.reset(); // Actual native graph retirement on CONTROL with live DSP.
                        discarded = unused->calls.load() == 0 &&
                                    fixture.samplerRuntimeAttachments(fixture.original[0]) == 1;
                        require(discarded && capture.wait(capture.complete.load() + 4, true),
                                "Discard leaves shared held voice running");
                        const auto afterDiscard = captureOwners();
                        if (afterDiscard != before)
                            require(folder.getChildFile("owners-after-discard-failure.txt")
                                        .replaceWithText(afterDiscard),
                                    "Retain exact failed discard ownership comparison");
                        require(afterDiscard == before, "Discard preserves genuine registrations/history");
                    }
                    try
                    {
                        (void)fixture.context.prepareGraphOnly(
                            overlay("late_factory_failure", std::make_shared<Observation>(), true));
                    }
                    catch (const std::runtime_error &error)
                    {
                        lateFailure = std::string(error.what()) ==
                                      "Original intentional concurrent late native-factory failure";
                        if (!lateFailure)
                            throw;
                    }
                    require(lateFailure && captureOwners() == before &&
                                fixture.samplerRuntimeAttachments(fixture.original[0]) == 1,
                            "Late real factory unwind preserves one live sample attachment and history");
                    require(capture.wait(capture.complete.load() + 4, true),
                            "Live callbacks continue after actual factory unwind");
                    auto nextOverlay = overlay("prepare_publish", marker, false);
                    auto candidate = fixture.context.prepareGraphOnly(nextOverlay);
                    auto staleMarker = std::make_shared<Observation>();
                    auto stale =
                        fixture.context.prepareGraphOnly(overlay("prepare_future_stale", staleMarker, false));
                    require(marker->calls.load() == 0 && staleMarker->calls.load() == 0 &&
                                captureOwners() == before,
                            "Neither actual prepared graph runs before publication");
                    const auto publishedAt = capture.complete.load();
                    require(fixture.context.validatePreparedGraph(*candidate) ==
                                    Context::ReadyGraphChangeStatus::ready &&
                                fixture.context.publishPreparedGraph(*candidate) ==
                                    Context::ReadyGraphChangeStatus::ready &&
                                candidate->wasPublished(),
                            "Exact actual ready graph publication acknowledged separately from adoption");
                    staleRefused = fixture.context.publishPreparedGraph(*stale) !=
                                       Context::ReadyGraphChangeStatus::ready &&
                                   !stale->wasPublished();
                    stale.reset(); // Old candidate shares the SAME shell, not copied mutable DSP.
                    require(staleRefused && staleMarker->calls.load() == 0,
                            "Stale holder refused and retired without ever processing");
                    require(capture.wait(publishedAt + 8, true),
                            "Actual callback path can adopt published graph");
                    adopted = marker->calls.load() > 0 && marker->firstReference.load() >= 0;
                    const auto afterAdoption = captureOwners();
                    require(folder.getChildFile("owners-after-adoption.txt").replaceWithText(afterAdoption),
                            "Retain exact CONTROL owner observation before adoption equality assertion");
                    require(adopted && afterAdoption == before,
                            "Actual output marker proves adoption while history/owners remain exact");
                    candidate.reset(); // Retired owner destruction/quiesce stays CONTROL.
                    require(capture.wait(capture.complete.load() + 8, true),
                            "Verify continued PCM after actual retired owner release");
                    log += "runtimeIdentity=" + juce::String::toHexString(juce::int64(runtime)) +
                           " remainingInitializedShells=" +
                           juce::String(fixture.samplerRuntimeAttachments(fixture.original[0])) + "\n";
                    require(folder.getChildFile("owners-after.txt").replaceWithText(captureOwners()) &&
                                captureOwners() == before,
                            "Preserve final CONTROL owner/history equality before stopping stream");
                    preparationObservation->events += "published," + juce::String(publishedAt) + "," +
                                                      juce::String(capture.complete.load()) + "," +
                                                      juce::String(capture.complete.load()) + "\n";
                }
                catch (const std::exception &error)
                {
                    controlFailure = error.what();
                }
                preparationObservation->capture =
                    nullptr;    // Persistent overlay no longer borrows stack state.
                capture.stop(); // Join BEFORE reading mutable PCM/frame arrays or teardown.
                const auto expectedProcessed = initial.processed + std::uint64_t(capture.complete.load());
                const auto drainDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.;
                while (fixture.worker->statistics().processed < expectedProcessed &&
                       fixture.worker->alive() && juce::Time::getMillisecondCounterHiRes() < drainDeadline)
                {
                    fixture.worker->service();
                    dispatchMessages(1);
                }
                // Drain only the already submitted final packet. No new processing
                // call, callback wait or requirement for immediate processed==1.
                const auto final = fixture.worker->statistics();
                const bool callbackChecks = capture.writeAndVerify(folder, initial, log);
                const bool totals =
                    final.submitted == initial.submitted + std::uint64_t(capture.complete.load()) &&
                    final.processed == expectedProcessed && final.missed == initial.missed &&
                    final.dropped == initial.dropped;
                const bool identities = fixture.worker->alive() && fixture.worker->processId() == pid &&
                                        fixture.worker->runtimeIdentity() == instance &&
                                        device->starts == starts && device->stops == stops &&
                                        audio.projectionDiagnostics().playbackContext ==
                                            reinterpret_cast<std::uintptr_t>(&fixture.context) &&
                                        fixture.samplerHeldKeys(replacements[0]) == 1 &&
                                        fixture.samplerRuntimeIdentity(replacements[0]) == runtime;
                pass = pass && callbackChecks && totals && identities && controlFailure.isEmpty() &&
                       preparationObservation->overlapHooks == 4 && discarded && lateFailure &&
                       staleRefused && adopted;
                log += "workerPid=" + juce::String(pid) + " instance=" + instance +
                       " overlapHooks=" + juce::String(preparationObservation->overlapHooks) +
                       " discarded=" + juce::String(int(discarded)) +
                       " lateFailure=" + juce::String(int(lateFailure)) +
                       " staleRefused=" + juce::String(int(staleRefused)) +
                       " adopted=" + juce::String(int(adopted)) +
                       " totalPacketChecks=" + juce::String(int(totals)) +
                       " identities=" + juce::String(int(identities)) + " controlFailure=" + controlFailure +
                       "\n";
                require(folder.getChildFile("concurrent-context-control.csv")
                            .replaceWithText(preparationObservation->events),
                        "Retain actual callback spans inside native preparation and all control outcomes");
                audio.stop();
            });
    }
    catch (const std::exception &error)
    {
        pass = false;
        log += "fixture/driver FAILED " + juce::String(error.what()) + "\n";
    }
    log += "pass=" + juce::String(int(pass)) +
           "\nUnqualified: generic changed-document structural commit, instruments/automation stream "
           "carryover, multichannel/route-wide lineage, installed-provider/hardware/listening.\n";
    require(report.replaceWithText(log),
            "Preserve actual concurrent failure evidence before aggregate assertion");
    require(pass, "Concurrent retained context failed; inspect full PCM/callback/control evidence");
}
