// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedPluginTransport.h"
#include <array>
#include <cmath>
#include <limits>

// Include after ParameterAcceptance.h, SyntheticDevice and callback probes.
// Original actual SDK tests, SOURCE-ONLY until compiled/executed by integration.
namespace prepared_transport_coverage
{
using Manager = juce::AudioDeviceManager;
struct Device final
{
    Manager manager;
    SyntheticDevice *device = nullptr;
    Device()
    {
        manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        require(manager.initialise(0, 2, &setup, false).isEmpty(), "Actual owned coverage manager");
        device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
        require(device != nullptr, "Original in-memory provider, no physical hardware");
    }
};
struct Registration final
{
    Manager &manager;
    juce::AudioIODeviceCallback &driver;
    Registration(Manager &m, juce::AudioIODeviceCallback &d) : manager(m), driver(d)
    {
        manager.addAudioCallback(&driver);
    }
    ~Registration()
    {
        manager.removeAudioCallback(&driver);
    }
};
struct Probe final
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
};
static void saveRaw(const juce::File &file, const juce::AudioBuffer<float> &capture, int frames)
{
    auto stream = file.createOutputStream();
    require(bool(stream) && stream->setPosition(0) && stream->truncate().wasOk(),
            "Truncate owned failure PCM");
    for (int channel = 0; channel < 2; ++channel)
        require(stream->write(capture.getReadPointer(channel), std::size_t(frames) * sizeof(float)),
                "Retain every captured sample without fitted shift");
}
struct SaturationDriver final : juce::AudioIODeviceCallback
{
    PluginHost &host;
    const PreparedPluginTransport &plan; // CONTROL pins owning plan through driver exclusion.
    juce::AudioBuffer<float> work{2, 256}, capture{2, 32768};
    int clock = 0, position = 0;
    std::uint64_t serial = 0;
    bool failed = false;
    PluginTransportOutcome fault;
    SaturationDriver(PluginHost &h, const PreparedPluginTransport &p) : host(h), plan(p)
    {
        capture.clear();
    }
    static float source(int c, int frame) noexcept
    {
        return (c == 0 ? .125f : -.0625f) +
               (frame % (c == 0 ? 251 : 257) == (c == 0 ? 11 : 23) ? (c == 0 ? .25f : -.125f) : 0.f);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
    void audioDeviceStopped() override {}
    void fail(PluginTransportOutcome result) noexcept
    {
        if (!failed)
            fault = result;
        failed = true;
        host.abortPhysicalBurst();
    }
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *output, int channels,
                                          int frames, const juce::AudioIODeviceCallbackContext &) override
    {
        for (int c = 0; c < channels; ++c)
            std::fill_n(output[c], frames, 0.f);
        if (failed)
            return;
        Manager::PhysicalCallbackSnapshot physical;
        if (!plan.read(physical))
        {
            PluginTransportOutcome result;
            result.status = PluginTransportStatus::sourceRefused;
            result.source.status = vibera::transport::BurstStatus::telemetryUnavailable;
            fail(result);
            return;
        }
        if (serial != physical.callbackSerial)
        {
            serial = physical.callbackSerial;
            position = 0;
            const auto result = host.beginPhysicalBurst(physical.outerFrames);
            if (!result.succeeded())
            {
                fail(result);
                return;
            }
        }
        // Actual manager splitting remains authoritative; this guard never silently reslices oversized input.
        if (frames <= 0 || frames > 256 || clock + frames > capture.getNumSamples())
        {
            PluginTransportOutcome result;
            result.status = PluginTransportStatus::sourceSpanExceeded;
            fail(result);
            return;
        }
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < frames; ++n)
                work.setSample(c, n, source(c, clock + n));
        host.process(work, 0, frames, nullptr, 0, clock / 48000., 120, true, false);
        for (int c = 0; c < 2; ++c)
        {
            capture.copyFrom(c, clock, work, c, 0, frames);
            if (c < channels)
                std::copy_n(work.getReadPointer(c), frames, output[c]);
        }
        clock += frames;
        position += frames;
        if (auto result = host.transportOutcome(); !result.succeeded())
        {
            fail(result);
            return;
        }
        if (position == physical.outerFrames)
        {
            auto result = host.endPhysicalBurst();
            if (result.succeeded() && result.processedFrames != physical.outerFrames)
                result.status = PluginTransportStatus::sourceSpanExceeded;
            if (!result.succeeded())
                fail(result);
        }
    }
};
__declspec(noinline) static void saturation(const juce::File &folder, const PluginSpec &spec)
{
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Actual original SDK native31 effect, no installed provider claim");
    int failedCases = 0;
    for (int packets = 2; packets <= 8; ++packets)
    {
        Device device;
        const int maximumOuter = (packets - 1) * 256;
        PreparedPluginTransport::Ptr plan;
        require(PreparedPluginTransport::prepare(device.manager, 256, {31, 1}, 1000000, maximumOuter, plan)
                    .succeeded(),
                "Actual immutable maximum-burst plan prepares");
        PluginHost host(plan);
        const auto started = host.start(spec, 48000., 256, false);
        require(started.wasOk(),
                "Actual native31 saturation worker: " + started.getErrorMessage().toStdString());
        const auto pid = host.processId(), reset = host.midiResetGeneration();
        const auto instance = host.runtimeIdentity();
        require(plan->budget().packets == packets && host.transportPacketCount() == packets &&
                    host.transportLatencySamples() == packets * 256 &&
                    host.latencySamples() == packets * 256 + 31,
                "Literal prepared K2..8, fixed256 quantum and native31; no inferred PCM shift");
        auto driver = std::make_unique<SaturationDriver>(host, *plan);
        Registration registration(device.manager, *driver);
        juce::AudioBuffer<float> output(2, maximumOuter);
        juce::String csv = "outer,K,physical,source,submitted,processed,missed,dropped,consumed,maxError,new,"
                           "delete,hostStatus,sourceStatus,ok\n";
        bool passed = true;
        for (int outer = 0; outer < 12; ++outer)
        {
            const int firstFrame = driver->clock;
            const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
            {
                Probe probe;
                device.device->process(output);
            }
            const auto allocations = callbackAllocations.load() - allocated;
            const auto destructions = callbackDestructions.load() - destroyed;
            dispatchMessages(6); // CONTROL only; never between actual inner slices or in callback.
            host.service();
            const auto stats = host.statistics();
            double error = 0;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < driver->clock; ++n)
                {
                    const int sourceFrame = n - (packets * 256 + 31);
                    const float expected = sourceFrame < 0 ? 0.f : SaturationDriver::source(c, sourceFrame);
                    const auto actual = driver->capture.getSample(c, n);
                    error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                  : std::numeric_limits<double>::infinity();
                }
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < maximumOuter; ++n)
                {
                    const int sourceFrame = firstFrame + n - (packets * 256 + 31);
                    const float expected = sourceFrame < 0 ? 0.f : SaturationDriver::source(c, sourceFrame);
                    const auto actual = output.getSample(c, n);
                    error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                  : std::numeric_limits<double>::infinity();
                }
            const auto consumed = host.consumedLatency();
            const bool packetCorrect =
                driver->clock <= packets * 256
                    ? !consumed
                    : consumed && consumed->stamp.nativeSamples == 31 &&
                          consumed->sequence == std::uint64_t(driver->clock / 256 - 1 - packets);
            const bool ok = !driver->failed && host.alive() && !host.deliveryFailed() &&
                            host.transportOutcome().succeeded() && host.processId() == pid &&
                            host.runtimeIdentity() == instance && host.midiResetGeneration() == reset &&
                            driver->clock == (outer + 1) * maximumOuter && host.packetFrame() == 0 &&
                            stats.submitted == std::uint64_t((outer + 1) * (packets - 1)) &&
                            stats.processed <= stats.submitted && stats.missed == 0 && stats.dropped == 0 &&
                            stats.midiOverflow == 0 && stats.parametersRejected == 0 && error == 0 &&
                            packetCorrect && allocations == 0 && destructions == 0;
            csv += juce::String(outer) + "," + juce::String(packets) + "," + juce::String(maximumOuter) +
                   "," + juce::String(driver->clock) + "," + juce::String(juce::int64(stats.submitted)) +
                   "," + juce::String(juce::int64(stats.processed)) + "," +
                   juce::String(juce::int64(stats.missed)) + "," + juce::String(juce::int64(stats.dropped)) +
                   "," + (consumed ? juce::String(juce::int64(consumed->sequence)) : "none") + "," +
                   juce::String(error, 12) + "," + juce::String(allocations) + "," +
                   juce::String(destructions) + "," +
                   juce::String(driver->failed ? unsigned(driver->fault.status) : 0u) + "," +
                   juce::String(driver->failed ? unsigned(driver->fault.source.status) : 0u) + "," +
                   juce::String(ok ? 1 : 0) + "\n";
            require(folder.getChildFile("saturation-K" + juce::String(packets) + ".csv").replaceWithText(csv),
                    "Retain exact K case before assertion");
            if (!ok)
            {
                passed = false;
                break;
            }
        }
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        while (host.alive() && host.statistics().processed != host.statistics().submitted &&
               juce::Time::getMillisecondCounter() < deadline)
        {
            dispatchMessages(1);
            host.service();
        }
        passed = passed && host.alive() && host.statistics().processed == host.statistics().submitted;
        if (!passed)
            saveRaw(folder.getChildFile("saturation-K" + juce::String(packets) + "-failure.raw"),
                    driver->capture, driver->clock);
        const auto final = host.statistics();
        require(folder.getChildFile("saturation-K" + juce::String(packets) + "-final.txt")
                    .replaceWithText(
                        "passed=" + juce::String(passed ? 1 : 0) +
                        ";submitted=" + juce::String(juce::int64(final.submitted)) +
                        ";processed=" + juce::String(juce::int64(final.processed)) +
                        ";missed=" + juce::String(juce::int64(final.missed)) +
                        ";dropped=" + juce::String(juce::int64(final.dropped)) +
                        ";hostStatus=" + juce::String(unsigned(host.transportOutcome().status)) +
                        ";sourceStatus=" + juce::String(unsigned(host.transportOutcome().source.status)) +
                        ";error=" + host.error() + "\n"),
                "Retain final bounded drain outcome before aggregate assertion");
        failedCases += !passed;
    }
    require(failedCases == 0,
            "All7 maximal real physical K2..8 cases pass exact PCM, packet and ownership oracles");
}
struct PendingDriver final : juce::AudioIODeviceCallback
{
    PluginHost &host;
    const PreparedPluginTransport &plan;
    unsigned generation;
    juce::AudioBuffer<float> work{2, 256}, capture{2, 8192};
    std::uint64_t serial = 0;
    int clock = 0, position = 0, phase = -1, remaining = 0;
    bool failed = false, scopeCounts = true, abortPreserved = true;
    PluginTransportOutcome fault;
    std::array<int, 14> cursorAfter{}, processedAfter{};
    PendingDriver(PluginHost &h, const PreparedPluginTransport &p, unsigned g)
        : host(h), plan(p), generation(g)
    {
        capture.clear();
    }
    void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
    void audioDeviceStopped() override {}
    void fail(PluginTransportOutcome result) noexcept
    {
        if (!failed)
            fault = result;
        failed = true;
        host.abortPhysicalBurst();
    }
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *output, int channels,
                                          int frames, const juce::AudioIODeviceCallbackContext &) override
    {
        for (int c = 0; c < channels; ++c)
            std::fill_n(output[c], frames, 0.f);
        if (failed)
            return;
        Manager::PhysicalCallbackSnapshot physical;
        if (!plan.read(physical))
        {
            PluginTransportOutcome result;
            result.status = PluginTransportStatus::sourceRefused;
            result.source.status = vibera::transport::BurstStatus::telemetryUnavailable;
            fail(result);
            return;
        }
        if (serial != physical.callbackSerial)
        {
            serial = physical.callbackSerial;
            position = 0;
            ++phase;
            if (phase >= int(cursorAfter.size()))
            {
                PluginTransportOutcome result;
                result.status = PluginTransportStatus::sourceSpanExceeded;
                fail(result);
                return;
            }
            remaining = phase == 0                 ? 16
                        : phase == 1 || phase == 4 ? 0
                        : phase == 2               ? 240
                        : phase == 3               ? 13
                        : phase == 5               ? 243
                                                   : physical.outerFrames;
            const auto result = host.beginPhysicalBurst(physical.outerFrames);
            if (!result.succeeded())
            {
                fail(result);
                return;
            }
        }
        if (frames <= 0 || frames > 256 || clock + std::min(frames, remaining) > capture.getNumSamples())
        {
            PluginTransportOutcome result;
            result.status = PluginTransportStatus::sourceSpanExceeded;
            fail(result);
            return;
        }
        const int count = std::min(frames, remaining);
        if (count > 0)
        {
            work.clear();
            const bool prefix = phase == 0 || phase == 3;
            const ipc::MidiEvent midi{phase == 0 ? 7 : 5,
                                      {phase == 0 ? std::uint8_t(0x90) : std::uint8_t(0x80), 60,
                                       phase == 0 ? std::uint8_t(127) : std::uint8_t(0)},
                                      3};
            const ipc::ParameterEvent parameter{77, phase == 0 ? 11 : 3, phase == 0 ? .25f : .75f};
            host.process(work, 0, count, prefix ? &midi : nullptr, prefix ? 1 : 0, clock / 48000., 120, true,
                         false, 4, 4, prefix ? &parameter : nullptr, prefix ? 1 : 0, generation);
            for (int c = 0; c < 2; ++c)
                capture.copyFrom(c, clock, work, c, 0, count);
            clock += count;
            remaining -= count;
            if (auto result = host.transportOutcome(); !result.succeeded())
            {
                fail(result);
                return;
            }
        }
        position += frames;
        if (position == physical.outerFrames)
        {
            if (phase == 0 || phase == 3)
            {
                const auto before = host.statistics();
                const int cursor = host.packetFrame();
                const auto reset = host.midiResetGeneration();
                host.abortPhysicalBurst();
                host.abortPhysicalBurst();
                const auto after = host.statistics();
                abortPreserved = abortPreserved && host.packetFrame() == cursor &&
                                 host.midiResetGeneration() == reset && host.transportOutcome().succeeded() &&
                                 before.submitted == after.submitted && before.missed == after.missed &&
                                 before.dropped == after.dropped;
                processedAfter[std::size_t(phase)] = phase == 0 ? 16 : 13;
            }
            else
            {
                const auto result = host.endPhysicalBurst();
                if (!result.succeeded())
                {
                    fail(result);
                    return;
                }
                const int expected = phase == 1 || phase == 4 ? 0
                                     : phase == 2             ? 240
                                     : phase == 5             ? 243
                                                              : physical.outerFrames;
                scopeCounts = scopeCounts && remaining == 0 && result.processedFrames == expected &&
                              result.sourceQuota == physical.outerFrames;
                processedAfter[std::size_t(phase)] = result.processedFrames;
            }
            cursorAfter[std::size_t(phase)] = host.packetFrame();
        }
    }
};
__declspec(noinline) static void pendingAcrossAbort(const juce::File &folder, const PluginSpec &spec)
{
    require(spec.instrument && spec.name == "VibeRa Instrument Runtime Fixture",
            "Actual original SDK MIDI/gain instrument; latency fixture has no MIDI bus");
    Device device;
    PreparedPluginTransport::Ptr plan;
    require(PreparedPluginTransport::prepare(device.manager, 256, {0, 1}, 1000000, 512, plan).succeeded(),
            "Prepared K3 instrument owner");
    PluginHost host(plan);
    const auto started = host.start(spec, 48000., 256, false);
    require(started.wasOk(),
            "Actual pending MIDI/parameter worker: " + started.getErrorMessage().toStdString());
    const auto catalogue = parameterCatalogue(host);
    require(nativeParameter(catalogue, 77).value == .5f && host.latencySamples() == 768 &&
                host.declaredLatency().nativeSamples == 0,
            "Original default gain.5 and literal768/native0 before stream");
    const auto pid = host.processId(), reset = host.midiResetGeneration();
    const auto identity = host.runtimeIdentity();
    auto driver = std::make_unique<PendingDriver>(host, *plan, catalogue.generation);
    Registration registration(device.manager, *driver);
    const std::array<int, 14> physicalFrames{64,  512, 256, 64,  256, 256, 512,
                                             512, 512, 512, 512, 512, 512, 512};
    juce::String csv = "phase,outer,source,cursor,scopeProcessed,submitted,processed,applied,rejected,missed,"
                       "dropped,maxError,new,delete,hostStatus,sourceStatus,ok\n";
    bool passed = true;
    for (int phase = 0; phase < int(physicalFrames.size()); ++phase)
    {
        juce::AudioBuffer<float> output(2, physicalFrames[std::size_t(phase)]);
        const int clockBefore = driver->clock;
        const auto before = host.statistics();
        const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
        {
            Probe probe;
            device.device->process(output);
        }
        const auto allocations = callbackAllocations.load() - allocated;
        const auto destructions = callbackDestructions.load() - destroyed;
        dispatchMessages(6); // No dispatch or wait between actual inner callback slices.
        host.service();
        const auto stats = host.statistics();
        double error = 0;
        // Literal source-domain gate [7,261), exact offsets and two gain changes.
        // Pruned physical callbacks advance no HOST samples; no application transport claim.
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < driver->clock; ++n)
            {
                const int sourceFrame = n - 768;
                const double gain = sourceFrame < 11 ? .5 : sourceFrame < 259 ? .25 : .75;
                const float expected =
                    sourceFrame < 7 || sourceFrame >= 261
                        ? 0.f
                        : float(c == 0 ? gain * (61. / 512 + 1. / 4096) : -gain * (68. / 512 + 1. / 8192));
                const auto actual = driver->capture.getSample(c, n);
                error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                              : std::numeric_limits<double>::infinity();
            }
        const bool pruned = phase == 1 || phase == 4;
        const bool prunePreserved =
            !pruned || (driver->clock == clockBefore && stats.submitted == before.submitted &&
                        host.packetFrame() == (phase == 1 ? 16 : 13));
        const bool prefixUnsent =
            phase != 0 && phase != 3 || (stats.submitted == before.submitted &&
                                         stats.parametersApplied <= std::uint64_t(phase == 0 ? 0 : 1) &&
                                         host.packetFrame() == (phase == 0 ? 16 : 13));
        const bool ok = !driver->failed && driver->scopeCounts && driver->abortPreserved && prunePreserved &&
                        prefixUnsent && host.alive() && !host.deliveryFailed() &&
                        host.transportOutcome().succeeded() && host.processId() == pid &&
                        host.runtimeIdentity() == identity && host.midiResetGeneration() == reset &&
                        stats.submitted == std::uint64_t(driver->clock / 256) &&
                        stats.processed <= stats.submitted && stats.missed == 0 && stats.dropped == 0 &&
                        stats.midiOverflow == 0 && stats.parametersRejected == 0 && error == 0 &&
                        allocations == 0 && destructions == 0;
        csv += juce::String(phase) + "," + juce::String(physicalFrames[std::size_t(phase)]) + "," +
               juce::String(driver->clock) + "," + juce::String(host.packetFrame()) + "," +
               juce::String(driver->processedAfter[std::size_t(phase)]) + "," +
               juce::String(juce::int64(stats.submitted)) + "," + juce::String(juce::int64(stats.processed)) +
               "," + juce::String(juce::int64(stats.parametersApplied)) + "," +
               juce::String(juce::int64(stats.parametersRejected)) + "," +
               juce::String(juce::int64(stats.missed)) + "," + juce::String(juce::int64(stats.dropped)) +
               "," + juce::String(error, 12) + "," + juce::String(allocations) + "," +
               juce::String(destructions) + "," +
               juce::String(driver->failed ? unsigned(driver->fault.status) : 0u) + "," +
               juce::String(driver->failed ? unsigned(driver->fault.source.status) : 0u) + "," +
               juce::String(ok ? 1 : 0) + "\n";
        require(folder.getChildFile("pending-abort-prune.csv").replaceWithText(csv),
                "Persist complete native event/parameter/PCM evidence before assertion");
        if (!ok)
        {
            passed = false;
            break;
        }
    }
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (host.alive() && host.statistics().processed != host.statistics().submitted &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        dispatchMessages(1);
        host.service();
    }
    const auto final = host.statistics();
    passed = passed && host.alive() && final.processed == final.submitted && final.parametersApplied == 2 &&
             final.parametersRejected == 0;
    float ons = -1, offs = -1;
    if (passed)
    {
        try
        {
            const auto native = parameterCatalogue(host, true);
            ons = nativeParameter(native, 90).value;
            offs = nativeParameter(native, 91).value;
            passed = ons == 1.f / 1024 && offs == 1.f / 1024 && host.midiResetGeneration() == reset;
        }
        catch (...)
        {
            saveRaw(folder.getChildFile("pending-abort-prune-failure.raw"), driver->capture, driver->clock);
            throw; // Preserve native metadata failure, not a manufactured PCM pass.
        }
    }
    if (!passed)
        saveRaw(folder.getChildFile("pending-abort-prune-failure.raw"), driver->capture, driver->clock);
    require(folder.getChildFile("pending-native.txt")
                .replaceWithText("noteOns=" + juce::String(ons, 12) + ";noteOffs=" + juce::String(offs, 12) +
                                 ";applied=" + juce::String(juce::int64(final.parametersApplied)) +
                                 ";submitted=" + juce::String(juce::int64(final.submitted)) +
                                 ";processed=" + juce::String(juce::int64(final.processed)) +
                                 ";passed=" + juce::String(passed ? 1 : 0) + "\n"),
            "Actual SDK counters retained before final assertion; -1 means unqueried after earlier failure");
    require(passed, "Queued native On/Off/gain survive partial abort and zero-work scopes exactly once with "
                    "literal768 PCM");
}
} // namespace prepared_transport_coverage

__declspec(noinline) static void preparedPluginTransportCoverageTests(const juce::File &folder,
                                                                      const juce::File &latencyBinary,
                                                                      const juce::File &instrumentBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned prepared transport coverage output");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), latencyBinary, instrumentBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact app/worker/original two-provider bytes");
    // Scan each real fixture through the existing isolated catalog, never fabricate descriptors.
    const auto latency = parameterSpec(latencyBinary), instrument = parameterSpec(instrumentBinary);
    prepared_transport_coverage::saturation(folder, latency);
    prepared_transport_coverage::pendingAcrossAbort(folder, instrument);
}
