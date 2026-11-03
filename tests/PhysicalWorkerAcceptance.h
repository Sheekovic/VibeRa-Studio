// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PhysicalBurstContract.h"
#include "SyntheticDevice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <chrono>
#include <thread>

// Original bounded experiment, NOT a production transport-policy selection.
// Include after ParameterAcceptance.h and existing callback allocation probes.
// Actual custom device -> JUCE pre-enforcer -> manager callback -> live real SDK
// worker. There is no AudioEngine/Tracktion graph, physical hardware or listening.
namespace physical_worker_detail
{
constexpr int quantum = 256, maximumPlans = 96, maximumSlices = maximumPlans * 3;
constexpr double rate = 48000.;
struct Plan
{
    int frames = 256;
    bool bypass = false, refusedIntent = false;
};
struct Slice
{
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    vibera::transport::BurstOutcome admission;
    PluginHost::Statistics before, after;
    int frames = 0, inputFrame = 0, cursorBefore = 0, cursorAfter = 0;
    unsigned resetBefore = 0, resetAfter = 0;
    bool known = false, hostCalled = false;
};
struct Outer
{
    double callbackMs = 0, releaseLateMs = 0, intervalMs = 0;
    unsigned allocations = 0, destructions = 0;
    int firstSlice = 0, slices = 0;
    SyntheticCallbackTiming timing;
    juce::int64 began = 0, ended = 0;
};
inline float mainValue(int channel, int frame) noexcept
{
    const bool pulse = frame % (channel == 0 ? 257 : 263) == (channel == 0 ? 0 : 17);
    return (channel == 0 ? .125f : -.0625f) + (pulse ? (channel == 0 ? .25f : -.125f) : 0.f);
}
inline float detectorValue(int channel, int frame) noexcept
{
    const bool pulse = frame % (channel == 0 ? 251 : 241) == (channel == 0 ? 11 : 23);
    return (channel == 0 ? .25f : -.375f) + (pulse ? (channel == 0 ? -.125f : .0625f) : 0.f);
}
struct Reader final : juce::AudioIODeviceCallback
{
    juce::AudioDeviceManager &manager;
    PluginHost &host;
    const vibera::transport::PhysicalBurstBudget &budget;
    bool instrument;
    int auxiliaryChannels;
    unsigned generation;
    int noteOn, noteOff;
    juce::AudioBuffer<float> audio{2, quantum}, detector{2, quantum};
    std::array<Slice, maximumSlices> records{};
    int count = 0, inputFrame = 0;
    std::uint64_t lastSerial = 0;
    int outerOffset = 0;
    bool overflow = false;
    Plan current;
    Reader(juce::AudioDeviceManager &m, PluginHost &h, const vibera::transport::PhysicalBurstBudget &b,
           bool isInstrument, int aux, unsigned schema, int on, int off)
        : manager(m), host(h), budget(b), instrument(isInstrument), auxiliaryChannels(aux),
          generation(schema), noteOn(on), noteOff(off)
    {
    }
    void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *outputs, int channels,
                                          int frames, const juce::AudioIODeviceCallbackContext &) override
    {
        // No text/errors, native/control APIs, heap, waits or evidence I/O here.
        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::clear(outputs[c], frames);
        if (count >= maximumSlices)
        {
            overflow = true;
            return;
        }
        auto &record = records[std::size_t(count++)];
        record.known = manager.readPhysicalCallbackSnapshot(record.physical);
        record.frames = frames;
        record.inputFrame = inputFrame;
        record.cursorBefore = host.packetFrame();
        record.resetBefore = host.midiResetGeneration();
        record.before = host.statistics();
        // Admit the REAL TOTAL physical burst before the first host slice or
        // musical input mutation. Later JUCE chunks re-read the same outer ID.
        record.admission = vibera::transport::admitSourceBurst(record.known, record.physical, budget,
                                                               record.physical.outerFrames);
        if (record.physical.callbackSerial != lastSerial)
        {
            lastSerial = record.physical.callbackSerial;
            outerOffset = 0;
        }
        if (record.admission.status == vibera::transport::BurstStatus::ready && frames <= quantum)
        {
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < frames; ++n)
                {
                    audio.setSample(
                        c, n, instrument || inputFrame + n >= noteOff ? 0.f : mainValue(c, inputFrame + n));
                    detector.setSample(c, n,
                                       inputFrame + n >= noteOff
                                           ? 0.f
                                           : detectorValue(auxiliaryChannels == 1 ? 0 : c, inputFrame + n));
                }
            std::array<ipc::MidiEvent, 2> midi{};
            int notes = 0;
            if (instrument)
            {
                for (int which = 0; which < 2; ++which)
                {
                    const int at = which == 0 ? noteOn : noteOff;
                    if (at >= inputFrame && at < inputFrame + frames)
                        midi[std::size_t(notes++)] = {at - inputFrame,
                                                      {static_cast<std::uint8_t>(which == 0 ? 0x90 : 0x80),
                                                       60, static_cast<std::uint8_t>(which == 0 ? 127 : 0)},
                                                      3};
                }
            }
            if (instrument && current.refusedIntent)
                midi[std::size_t(notes++)] = {0, {0x90, 60, 127}, 3};
            // Real native automation accompanies every admitted input slice.
            // Whole-Q batching/validation remains Host-owned and unchanged.
            const ipc::ParameterEvent parameter{instrument ? 77u : 71u, 0,
                                                current.refusedIntent ? .75f
                                                : instrument          ? .25f
                                                                      : .5f};
            host.process(audio, 0, frames, midi.data(), notes, inputFrame / rate, 120., true, current.bypass,
                         4, 4, &parameter, 1, generation, true, instrument ? nullptr : &detector, 0);
            for (int c = 0; c < std::min(channels, 2); ++c)
                juce::FloatVectorOperations::copy(outputs[c], audio.getReadPointer(c), frames);
            inputFrame += frames;
            record.hostCalled = true;
        }
        // The >Q physical experiments deliberately stage a note retrigger and
        // parameter .75 intent. Refusal does NOT construct/deliver those events,
        // advance Host/MIDI/time or pretend that silent rejection is recovery.
        record.cursorAfter = host.packetFrame();
        record.resetAfter = host.midiResetGeneration();
        record.after = host.statistics();
        outerOffset += frames;
    }
};
} // namespace physical_worker_detail

__declspec(noinline) static void physicalWorkerTurnaroundTests(const juce::File &folder,
                                                               const juce::File &instrumentBinary,
                                                               const juce::File &latencyBinary)
{
    using namespace physical_worker_detail;
    using namespace vibera::transport;
    using Manager = juce::AudioDeviceManager;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh physical-worker evidence attempt");
    const auto instrument = parameterSpec(instrumentBinary), effect = parameterSpec(latencyBinary);
    require(instrument.instrument && instrument.name == "VibeRa Instrument Runtime Fixture" &&
                !effect.instrument && effect.name == "VibeRa Mixer Latency Fixture",
            "Actual scanned independently authored original SDK fixtures only");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), instrumentBinary, latencyBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact input identities");
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        const bool isInstrument = scenario == 0;
        const int aux = scenario == 2 ? 1 : 2;
        const auto output = folder.getChildFile(isInstrument ? "instrument"
                                                : aux == 1   ? "effect31_mono"
                                                             : "effect31_stereo");
        require(!output.exists() && output.createDirectory().wasOk(), "Preserve each failed worker attempt");
        PluginHost host(PluginHost::TransportPolicy::twoQuanta);
        require(host.start(isInstrument ? instrument : effect, rate, quantum, false, {},
                           isInstrument ? PluginBusRequest{} : PluginBusRequest{1, aux})
                    .wasOk(),
                "Actual live-worker preparation; no offline wait mode");
        auto catalogue = parameterCatalogue(host);
        require(parameterResult(host, host.setParameter(isInstrument ? 77u : 71u, isInstrument ? .25f : .5f,
                                                        catalogue.generation))
                        .result == 0,
                "Acknowledged original native parameter before callback ownership");
        catalogue = parameterCatalogue(host, true);
        const auto pid = host.processId();
        const auto identity = host.runtimeIdentity();
        const auto revision = host.parameterRevision();
        const auto reset = host.midiResetGeneration();
        const auto initialStatistics = host.statistics();
        const auto declaration = host.declaredLatency();
        require(host.transportLatencySamples() == 512 && host.processingQuantum() == 256 &&
                    host.maximumLiveSpan() == 256 && declaration.nativeSamples == (isInstrument ? 0 : 31) &&
                    host.latencySamples() == (isInstrument ? 512 : 543),
                "Literal prepared2Q plus genuine0/31 native declaration");

        Manager manager;
        manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", rate);
        setup.setAttribute("audioDeviceBufferSize", quantum);
        require(manager.initialise(0, 2, &setup, false).isEmpty(),
                "Actual manager with original synthetic device");
        auto *device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
        Manager::PhysicalCallbackSnapshot prepared;
        const bool known = manager.readPhysicalCallbackSnapshot(prepared);
        PhysicalBurstBudget budget;
        require(
            device && known && prepared.active && prepared.callbackSerial == 0 &&
                prepared.configuredFrames == 256 &&
                manager.getCurrentPhysicalCallbackBound() == Manager::PhysicalCallbackBound::unknown &&
                preparePhysicalBurstBudget(known, prepared, manager.getCurrentPhysicalCallbackBound(),
                                           quantum, declaration, 1000000, 256, budget)
                        .status == BurstStatus::ready &&
                budget.proof == BurstProof::explicitProviderBudget && budget.packets == 2 &&
                budget.sourceFrames == 256 && budget.transportSamples == 512,
            "CONTROL explicit custom-provider256 budget before first processing; not pinned hardware proof");
        std::array<Plan, maximumPlans> plans{};
        int planCount = 0, positiveFrames = 0, positiveCalls = 0;
        const auto add = [&](int frames, bool bypass = false, bool negative = false)
        {
            require(planCount < maximumPlans && frames > 0 && frames <= 768,
                    "Bounded prepared physical spans");
            plans[std::size_t(planCount++)] = {frames, bypass, negative};
            if (!negative)
            {
                positiveFrames += frames;
                ++positiveCalls;
            }
        };
        for (int n = 0; n < 8; ++n)
            add(256);
        const int noteOn = positiveFrames;
        for (const int n : {127, 64, 64, 65})
            add(n);
        for (int n = 0; n < 8; ++n)
            add(256);
        if (!isInstrument)
        {
            add(127, true);
            add(64, false);
            add(64, true);
            add(65, true);
            for (int n = 0; n < 3; ++n)
                add(256, true);
            for (const int n : {127, 64, 64, 65})
                add(n, false);
        }
        const int noteOff = positiveFrames;
        for (const int n : {127, 64, 64, 65})
            add(n);
        if (positiveFrames % 256)
            add(256 - positiveFrames % 256);
        for (int n = 0; n < 8; ++n)
            add(256); // Check all fixed transport/native tail samples, not just audible plateaus.
        add(512, false, true);
        add(768, false, true);
        std::array<juce::AudioBuffer<float>, maximumPlans> pcm;
        std::array<Outer, maximumPlans> observations{};
        for (int i = 0; i < planCount; ++i)
        {
            pcm[std::size_t(i)].setSize(2, plans[std::size_t(i)].frames);
            pcm[std::size_t(i)].clear();
        }
        Reader reader(manager, host, budget, isInstrument, aux, catalogue.generation, noteOn, noteOff);
        manager.addAudioCallback(&reader);
        struct Remove
        {
            Manager &manager;
            Reader &reader;
            ~Remove()
            {
                manager.removeAudioCallback(&reader);
            }
        } remove{manager, reader};
        const auto starts = device->starts, stops = device->stops;
        std::atomic<bool> complete{false};
        std::jthread driver(
            [&](std::stop_token stop)
            {
                using Clock = std::chrono::steady_clock;
                auto due = Clock::now(), previousStart = due;
                for (int i = 0; i < planCount && !stop.stop_requested(); ++i)
                {
                    std::this_thread::sleep_until(due); // DRIVER only, never inside callback.
                    const auto beganWall = Clock::now();
                    auto &record = observations[std::size_t(i)];
                    record.releaseLateMs = std::chrono::duration<double, std::milli>(beganWall - due).count();
                    record.intervalMs =
                        i ? std::chrono::duration<double, std::milli>(beganWall - previousStart).count() : 0;
                    previousStart = beganWall;
                    reader.current = plans[std::size_t(i)];
                    record.firstSlice = reader.count;
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    record.began = juce::Time::getHighResolutionTicks();
                    const bool priorProbe = callbackAllocationProbe;
                    callbackAllocationProbe = true;
                    device->process(pcm[std::size_t(i)]); // SOLE physical callback owner.
                    callbackAllocationProbe = priorProbe;
                    record.ended = juce::Time::getHighResolutionTicks();
                    record.timing = device->lastTiming();
                    record.callbackMs = SyntheticCallbackTiming::milliseconds(record.began, record.ended);
                    record.allocations = callbackAllocations.load() - allocations;
                    record.destructions = callbackDestructions.load() - destructions;
                    record.slices = reader.count - record.firstSlice;
                    // Never catch up by bursting after scheduling jitter. Minimum
                    // period is the literal physical frames/rate from actual release;
                    // actual lateness is retained, never interpreted as hardware proof.
                    due = beganWall + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(plans[std::size_t(i)].frames / rate));
                }
                complete.store(true, std::memory_order_release);
            });
        const auto timeout = juce::Time::getMillisecondCounterHiRes() + 10000.;
        while (!complete.load(std::memory_order_acquire) &&
               juce::Time::getMillisecondCounterHiRes() < timeout)
        {
            dispatchMessages(1); // CONTROL message service; not callback sleep or packet wait.
            host.service();
        }
        const bool completed = complete.load(std::memory_order_acquire);
        if (!completed)
            driver.request_stop();
        driver.join(); // CONTROL joins outside callbacks; no reader survives Engine/device owner.

        bool passed = completed && !reader.overflow && reader.inputFrame == positiveFrames;
        int sourceClock = 0;
        double blend = 0, maximumError = 0, maximumCallbackMs = 0, maximumReleaseLateMs = 0;
        int deadlineOverruns = 0;
        juce::String csv = "outer,frames,negative,bypass,slices,sourceFrame,maxError,callbackMs,deadlineMs,"
                           "releaseLateMs,intervalMs,new,delete\n";
        juce::String slices = "outer,slice,frames,inputFrame,known,epoch,serial,outerFrames,maximumOuter,"
                              "status,hostCalled,cursorBefore,cursorAfter,submittedBefore,submittedAfter,"
                              "missedBefore,missedAfter,droppedBefore,droppedAfter\n";
        const auto raw = output.getChildFile("actual-planar-float32le.raw");
        require(!raw.exists(), "Never overwrite exact failed callback PCM");
        auto stream = raw.createOutputStream();
        require(bool(stream), "Owned raw callback PCM evidence");
        for (int i = 0; i < planCount; ++i)
        {
            const auto &plan = plans[std::size_t(i)];
            const auto &outer = observations[std::size_t(i)];
            const double deadlineMs = 1000. * plan.frames / rate;
            const bool timely = outer.callbackMs < deadlineMs;
            deadlineOverruns += !timely;
            maximumCallbackMs = std::max(maximumCallbackMs, outer.callbackMs);
            maximumReleaseLateMs = std::max(maximumReleaseLateMs, outer.releaseLateMs);
            double error = 0;
            bool exactEndpoints = true;
            for (int n = 0; n < plan.frames; ++n)
            {
                if (!plan.refusedIntent)
                    blend = plan.bypass ? std::min(1., blend + 1. / 240.) : std::max(0., blend - 1. / 240.);
                for (int c = 0; c < 2; ++c)
                {
                    const int source = sourceClock + n - (isInstrument ? 512 : 543);
                    const float dry =
                        source >= 0 && source < noteOff && !isInstrument ? mainValue(c, source) : 0.f;
                    float wet = 0;
                    if (source >= 0)
                        wet = isInstrument
                                  ? source >= noteOn && source < noteOff
                                        ? float(.25 *
                                                (c == 0 ? 61. / 512 + 1. / 4096 : -(68. / 512 + 1. / 8192)))
                                        : 0.f
                                  : dry + (source < noteOff ? .5f * detectorValue(aux == 1 ? 0 : c, source)
                                                            : 0.f);
                    const double expected =
                        plan.refusedIntent ? 0. : (1. - blend) * double(wet) + blend * double(dry);
                    const float actual = pcm[std::size_t(i)].getSample(c, n);
                    error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                  : std::numeric_limits<double>::infinity();
                    if (!plan.refusedIntent && (blend == 0 || blend == 1))
                        exactEndpoints &= actual == (blend == 0 ? wet : dry);
                }
            }
            maximumError = std::max(maximumError, error);
            bool correct = timely && outer.allocations == 0 && outer.destructions == 0 &&
                           outer.slices == (plan.frames + 255) / 256 && error < 2.e-7 && exactEndpoints &&
                           outer.timing.containedBy(outer.began, outer.ended);
            for (int j = 0; j < outer.slices; ++j)
            {
                const auto &s = reader.records[std::size_t(outer.firstSlice + j)];
                const auto status =
                    plan.refusedIntent ? BurstStatus::physicalBurstExceeded : BurstStatus::ready;
                correct &= s.known && s.physical.deviceEpoch == prepared.deviceEpoch &&
                           s.physical.callbackSerial == std::uint64_t(i + 1) &&
                           s.physical.outerFrames == plan.frames &&
                           s.frames == std::min(256, plan.frames - j * 256) && s.admission.status == status &&
                           s.hostCalled == !plan.refusedIntent && s.resetBefore == reset &&
                           s.resetAfter == reset;
                if (plan.refusedIntent)
                    correct &= s.inputFrame == positiveFrames && s.cursorBefore == s.cursorAfter &&
                               s.before.submitted == s.after.submitted && s.before.missed == s.after.missed &&
                               s.before.dropped == s.after.dropped &&
                               s.before.parametersRejected == s.after.parametersRejected;
                else
                    correct &= s.cursorBefore == s.inputFrame % 256 &&
                               s.cursorAfter == (s.inputFrame + s.frames) % 256 &&
                               s.after.missed == initialStatistics.missed &&
                               s.after.dropped == initialStatistics.dropped;
                slices += juce::String(i) + "," + juce::String(j) + "," + juce::String(s.frames) + "," +
                          juce::String(s.inputFrame) + "," + juce::String(int(s.known)) + "," +
                          juce::String(juce::int64(s.physical.deviceEpoch)) + "," +
                          juce::String(juce::int64(s.physical.callbackSerial)) + "," +
                          juce::String(s.physical.outerFrames) + "," +
                          juce::String(s.physical.maximumOuterFrames) + "," +
                          juce::String(int(s.admission.status)) + "," + juce::String(int(s.hostCalled)) +
                          "," + juce::String(s.cursorBefore) + "," + juce::String(s.cursorAfter) + "," +
                          juce::String(juce::int64(s.before.submitted)) + "," +
                          juce::String(juce::int64(s.after.submitted)) + "," +
                          juce::String(juce::int64(s.before.missed)) + "," +
                          juce::String(juce::int64(s.after.missed)) + "," +
                          juce::String(juce::int64(s.before.dropped)) + "," +
                          juce::String(juce::int64(s.after.dropped)) + "\n";
            }
            passed &= correct;
            csv += juce::String(i) + "," + juce::String(plan.frames) + "," +
                   juce::String(int(plan.refusedIntent)) + "," + juce::String(int(plan.bypass)) + "," +
                   juce::String(outer.slices) + "," + juce::String(sourceClock) + "," +
                   juce::String(error, 12) + "," + juce::String(outer.callbackMs, 6) + "," +
                   juce::String(deadlineMs, 6) + "," + juce::String(outer.releaseLateMs, 6) + "," +
                   juce::String(outer.intervalMs, 6) + "," + juce::String(outer.allocations) + "," +
                   juce::String(outer.destructions) + "\n";
            for (int c = 0; c < 2; ++c)
                require(stream->write(pcm[std::size_t(i)].getReadPointer(c),
                                      std::size_t(plan.frames) * sizeof(float)),
                        "Retain every actual physical outer PCM block before assertions");
            if (!plan.refusedIntent)
                sourceClock += plan.frames;
        }
        stream.reset();
        require(output.getChildFile("outer-callbacks.csv").replaceWithText(csv) &&
                    output.getChildFile("host-slices.csv").replaceWithText(slices),
                "Actual complete failure evidence");
        const auto native = parameterCatalogue(host, true);
        const auto stats = host.statistics();
        const auto consumed = host.consumedLatency();
        passed &=
            host.alive() && host.processId() == pid && host.runtimeIdentity() == identity &&
            host.parameterRevision() == revision + unsigned(positiveFrames / 256) &&
            host.midiResetGeneration() == reset && !host.deliveryFailed() &&
            !host.automationStatistics().failed() && device->starts == starts && device->stops == stops &&
            stats.missed == initialStatistics.missed && stats.dropped == initialStatistics.dropped &&
            stats.submitted - initialStatistics.submitted == std::uint64_t(positiveFrames / 256) &&
            stats.processed == stats.submitted &&
            stats.parametersApplied - initialStatistics.parametersApplied == std::uint64_t(positiveCalls) &&
            stats.parametersRejected == initialStatistics.parametersRejected &&
            stats.auxiliaryRejected == initialStatistics.auxiliaryRejected && consumed.has_value() &&
            consumed->stamp == declaration &&
            consumed->sequence == std::uint64_t((positiveFrames - 1) / 256 - 2) &&
            nativeParameter(native, isInstrument ? 77u : 71u).value == (isInstrument ? .25f : .5f);
        if (isInstrument)
            passed &= nativeParameter(native, 90).value == 1.f / 1024 &&
                      nativeParameter(native, 91).value == 1.f / 1024;
        require(output.getChildFile("result.txt")
                    .replaceWithText(
                        "pass=" + juce::String(int(passed)) + ";positiveFrames=" +
                        juce::String(positiveFrames) + ";maxError=" + juce::String(maximumError, 12) +
                        ";maxCallbackMs=" + juce::String(maximumCallbackMs, 6) +
                        ";deadlineOverruns=" + juce::String(deadlineOverruns) +
                        ";maxReleaseLateMs=" + juce::String(maximumReleaseLateMs, 6) +
                        ";submitted=" + juce::String(juce::int64(stats.submitted)) +
                        ";processed=" + juce::String(juce::int64(stats.processed)) +
                        ";missed=" + juce::String(juce::int64(stats.missed)) +
                        ";dropped=" + juce::String(juce::int64(stats.dropped)) +
                        ";parametersApplied=" + juce::String(juce::int64(stats.parametersApplied)) +
                        ";expectedParameters=" + juce::String(positiveCalls) +
                        ";rejected=" + juce::String(juce::int64(stats.parametersRejected)) +
                        ";revision=" + juce::String(host.parameterRevision()) +
                        ";expectedRevision=" + juce::String(revision + unsigned(positiveFrames / 256)) +
                        ";cursor=" + juce::String(host.packetFrame()) + ";heldOn=" +
                        (isInstrument ? juce::String(nativeParameter(native, 90).value, 9) : "n/a") +
                        ";heldOff=" +
                        (isInstrument ? juce::String(nativeParameter(native, 91).value, 9) : "n/a") +
                        "\nActual custom JUCE physical callback admission+live SDK2Q worker. Negative "
                        "oversize silence is explicit test refusal, not product recovery.\n"
                        "No AudioEngine/PDC, speed expansion, generic3Q, installed plugin, physical "
                        "hardware, desktop input or listening qualification.\n"),
                "Truthful final experiment outcome");
        require(passed,
                "Actual JUCE outer admission and literal512/543 complete PCM/ownership/deadline oracle");
    }
}
