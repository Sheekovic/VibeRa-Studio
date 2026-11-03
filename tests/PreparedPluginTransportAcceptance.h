// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedPluginTransport.h"
#include <array>
#include <cmath>
#include <limits>

// Include after ParameterAcceptance.h, SyntheticDevice and callback probes.
// Original SOURCE-ONLY acceptance draft. Actual SDK worker and real JUCE enforcer,
// not installed-provider, production graph, physical-device or deadline proof.
// Each case owns an actual manager and SDK worker. No synthetic IPC reply,
// sleeps inside callbacks, hardware, native input or recovery claim.
__declspec(noinline) static void preparedPluginTransportScopeTests(const juce::File &folder,
                                                                   const PluginSpec &spec)
{
    using Manager = juce::AudioDeviceManager;
    using Status = PluginTransportStatus;
    using SourceStatus = vibera::transport::BurstStatus;
    enum class Case
    {
        unopenedAbort,
        partialAbortContinue,
        abortThenEnd,
        duplicateBegin,
        missingBegin,
        quotaOverflow,
        zeroQuota,
        changedSerialAtEnd,
        reusedSerialAtBegin,
        readerDeath,
        beforeFirstPhysical
    };
    struct Driver final : juce::AudioIODeviceCallback
    {
        PluginHost &host;
        Case scenario;
        juce::AudioBuffer<float> work{2, 256};
        std::array<PluginTransportOutcome, 4> results;
        int calls = 0;
        bool abortPreserved = true, refusedPcmPreserved = true;
        explicit Driver(PluginHost &h, Case c) : host(h), scenario(c) {}
        // Read only documented lock-free/scalar APIs on the sole processing owner.
        struct State
        {
            PluginHost::Statistics stats;
            PluginTransportOutcome fault;
            int cursor, transportLatency;
            unsigned reset;
            std::optional<float> wet, bypass;
            bool equals(const State &b) const noexcept
            {
                return cursor == b.cursor && transportLatency == b.transportLatency && reset == b.reset &&
                       wet == b.wet && bypass == b.bypass && fault.status == b.fault.status &&
                       fault.source.status == b.fault.source.status && stats.submitted == b.stats.submitted &&
                       stats.missed == b.stats.missed && stats.dropped == b.stats.dropped &&
                       stats.midiOverflow == b.stats.midiOverflow &&
                       stats.parametersApplied == b.stats.parametersApplied &&
                       stats.parametersRejected == b.stats.parametersRejected &&
                       stats.auxiliaryRejected == b.stats.auxiliaryRejected;
                // Worker heartbeat/processed may legitimately advance asynchronously.
            }
        };
        State state() const noexcept
        {
            return {host.statistics(),          host.transportOutcome(),
                    host.packetFrame(),         host.transportLatencySamples(),
                    host.midiResetGeneration(), host.wetBlend(),
                    host.bypassBlend()};
        }
        void abortTwice() noexcept
        {
            const auto before = state();
            host.abortPhysicalBurst();
            host.abortPhysicalBurst();
            abortPreserved = abortPreserved && before.equals(state());
        }
        void process(int frames) noexcept
        {
            for (int c = 0; c < 2; ++c)
                std::fill_n(work.getWritePointer(c), frames, c == 0 ? .375f : -.125f);
            host.process(work, 0, frames, nullptr, 0, 0, 120, true, false);
        }
        void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
        void audioDeviceStopped() override {}
        void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *out, int channels,
                                              int frames, const juce::AudioIODeviceCallbackContext &) override
        {
            for (int c = 0; c < channels; ++c)
                std::fill_n(out[c], frames, 0.f); // This scope test does NOT claim PCM rendering.
            ++calls;
            switch (scenario)
            {
            case Case::unopenedAbort:
                abortTwice();
                results[0] = host.beginPhysicalBurst(64);
                results[1] = host.endPhysicalBurst();
                abortTwice();
                break;
            case Case::partialAbortContinue:
                results[0] = host.beginPhysicalBurst(64);
                process(16);
                if (calls == 1)
                    abortTwice();
                else
                {
                    results[1] = host.endPhysicalBurst();
                    abortTwice();
                }
                break;
            case Case::abortThenEnd:
                results[0] = host.beginPhysicalBurst(64);
                process(16);
                abortTwice();
                results[1] = host.endPhysicalBurst();
                abortTwice();
                break;
            case Case::duplicateBegin:
                results[0] = host.beginPhysicalBurst(64);
                results[1] = host.beginPhysicalBurst(64);
                results[2] = host.endPhysicalBurst();
                abortTwice();
                break;
            case Case::missingBegin:
                process(16);
                results[0] = host.transportOutcome();
                results[1] = host.endPhysicalBurst();
                abortTwice();
                break;
            case Case::quotaOverflow:
                results[0] = host.beginPhysicalBurst(16);
                process(16);
                process(1); // Refused BEFORE cursor/audio/packet mutation.
                refusedPcmPreserved = work.getSample(0, 0) == .375f && work.getSample(1, 0) == -.125f;
                results[1] = host.transportOutcome();
                results[2] = host.endPhysicalBurst();
                abortTwice();
                break;
            case Case::zeroQuota:
                results[0] = host.beginPhysicalBurst(0);
                results[1] = host.endPhysicalBurst();
                abortTwice();
                break;
            case Case::changedSerialAtEnd:
                if (calls == 1)
                {
                    results[0] = host.beginPhysicalBurst(64);
                    process(16); // Deliberately omit close at this actual outer boundary.
                }
                else
                {
                    results[1] = host.endPhysicalBurst();
                    abortTwice();
                }
                break;
            case Case::reusedSerialAtBegin:
                results[0] = host.beginPhysicalBurst(64);
                results[1] = host.endPhysicalBurst();
                abortTwice(); // Closing/aborting cannot readmit this same actual outer.
                results[2] = host.beginPhysicalBurst(64);
                abortTwice();
                break;
            case Case::readerDeath:
                results[0] = host.beginPhysicalBurst(64);
                process(16);
                break; // The sole owner closes after CONTROL excludes/destroys manager.
            case Case::beforeFirstPhysical:
                break; // Exercised before registering this callback.
            }
        }
    };
    juce::String csv =
        "case,status0,source0,status1,source1,status2,cursor,abortPreserved,pcmPreserved,new,delete,ok\n";
    int failures = 0;
    for (int index = 0; index < 11; ++index)
    {
        const auto scenario = Case(index);
        auto manager = std::make_unique<Manager>();
        manager->addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        require(manager->initialise(0, 2, &setup, false).isEmpty(), "Owned scope manager prepares");
        auto *device = dynamic_cast<SyntheticDevice *>(manager->getCurrentAudioDevice());
        require(device != nullptr, "Actual original in-memory device");
        PreparedPluginTransport::Ptr plan;
        require(PreparedPluginTransport::prepare(*manager, 256, {31, 1}, 1000000, 512, plan).succeeded(),
                "Fresh retained reader512/K3 for each independent fault scope");
        PluginHost host(plan);
        const auto started = host.start(spec, 48000., 256, false);
        require(started.wasOk(), "Actual SDK scope worker: " + started.getErrorMessage().toStdString());
        auto driver = std::make_unique<Driver>(host, scenario);
        juce::AudioBuffer<float> output(2, 64);
        const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
        if (scenario == Case::beforeFirstPhysical)
        {
            driver->results[0] = host.beginPhysicalBurst(64);
            driver->abortTwice(); // Same sole owner before callback attachment.
        }
        else
        {
            manager->addAudioCallback(driver.get());
            // RAII unregisters before Host/Driver destruction even if a later CONTROL assertion throws.
            struct Remove final
            {
                Manager *manager;
                Driver *driver;
                ~Remove()
                {
                    if (manager)
                        manager->removeAudioCallback(driver);
                }
            } remove{manager.get(), driver.get()};
            const int count =
                scenario == Case::partialAbortContinue || scenario == Case::changedSerialAtEnd ? 2 : 1;
            for (int callback = 0; callback < count; ++callback)
            {
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
                } probe;
                device->process(output);
            }
            if (scenario == Case::readerDeath)
            {
                manager->removeAudioCallback(driver.get());
                remove.manager = nullptr;
                manager.reset(); // Retained reader survives. No callback/lifecycle owner overlap.
                driver->results[1] = host.endPhysicalBurst();
                driver->abortTwice();
            }
        }
        const auto allocations = callbackAllocations.load() - allocated;
        const auto destructions = callbackDestructions.load() - destroyed;
        const auto &r = driver->results;
        bool ok = driver->abortPreserved && driver->refusedPcmPreserved && allocations == 0 &&
                  destructions == 0 && host.statistics().submitted == 0 && host.statistics().missed == 0 &&
                  host.statistics().dropped == 0;
        switch (scenario)
        {
        case Case::unopenedAbort:
            ok = ok && r[0].succeeded() && r[1].succeeded() && r[1].processedFrames == 0 &&
                 host.packetFrame() == 0;
            break;
        case Case::partialAbortContinue:
            ok = ok && r[0].succeeded() && r[1].succeeded() && r[1].sourceQuota == 64 &&
                 r[1].processedFrames == 16 && host.packetFrame() == 32;
            break;
        case Case::abortThenEnd:
            ok = ok && r[0].succeeded() && r[1].status == Status::burstMissing && host.packetFrame() == 16;
            break;
        case Case::duplicateBegin:
            ok = ok && r[0].succeeded() && r[1].status == Status::burstAlreadyActive &&
                 r[2].status == r[1].status;
            break;
        case Case::missingBegin:
            ok = ok && r[0].status == Status::burstMissing && r[1].status == r[0].status &&
                 host.packetFrame() == 0;
            break;
        case Case::quotaOverflow:
            ok = ok && r[0].succeeded() && r[1].status == Status::sourceSpanExceeded &&
                 r[2].status == r[1].status && host.packetFrame() == 16;
            break;
        case Case::zeroQuota:
            ok = ok && r[0].status == Status::sourceRefused &&
                 r[0].source.status == SourceStatus::invalidPreparation && r[1].status == r[0].status &&
                 host.packetFrame() == 0;
            break;
        case Case::changedSerialAtEnd:
            ok = ok && r[0].succeeded() && r[1].status == Status::serialReused && host.packetFrame() == 16;
            break;
        case Case::reusedSerialAtBegin:
            ok = ok && r[0].succeeded() && r[1].succeeded() && r[2].status == Status::serialReused;
            break;
        case Case::readerDeath:
            ok = ok && r[0].succeeded() && r[1].status == Status::sourceRefused &&
                 r[1].source.status == SourceStatus::managerReleased && host.packetFrame() == 16;
            break;
        case Case::beforeFirstPhysical:
            ok = ok && r[0].status == Status::sourceRefused &&
                 r[0].source.status == SourceStatus::noPhysicalCallback;
            break;
        }
        failures += !ok;
        csv += juce::String(index) + "," + juce::String(unsigned(r[0].status)) + "," +
               juce::String(unsigned(r[0].source.status)) + "," + juce::String(unsigned(r[1].status)) + "," +
               juce::String(unsigned(r[1].source.status)) + "," + juce::String(unsigned(r[2].status)) + "," +
               juce::String(host.packetFrame()) + "," + juce::String(driver->abortPreserved ? 1 : 0) + "," +
               juce::String(driver->refusedPcmPreserved ? 1 : 0) + "," + juce::String(allocations) + "," +
               juce::String(destructions) + "," + juce::String(ok ? 1 : 0) + "\n";
        require(folder.getChildFile("physical-scopes.csv").replaceWithText(csv),
                "Retain every original scope case before aggregate assertion");
        host.service(); // Existing owned shutdown only on CONTROL; never abort recovery.
    }
    require(failures == 0,
            "All11 actual retained-reader/SDK-worker scope cases execute and preserve first faults/state");
}

__declspec(noinline) static void preparedPluginTransportTests(const juce::File &folder,
                                                              const juce::File &latencyBinary)
{
    using Manager = juce::AudioDeviceManager;
    using Status = PluginTransportStatus;
    using SourceStatus = vibera::transport::BurstStatus;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned prepared-K fixture output");
    Manager manager;
    manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    require(manager.initialise(0, 2, &setup, false).isEmpty(), "Actual standalone synthetic manager");
    auto *device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
    require(device != nullptr, "Original in-memory provider, no hardware");
    const auto spec = parameterSpec(latencyBinary);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Genuine original SDK native31 effect, not a fabricated plugin descriptor");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), latencyBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact application/worker/original provider bytes");
    PreparedPluginTransport::Ptr plan;
    auto prepared = PreparedPluginTransport::prepare(manager, 256, {31, 1}, 1000000, 512, plan);
    require(prepared.succeeded() && plan && plan->budget().packets == 3 &&
                plan->budget().transportSamples == 768 && plan->budget().sourceFrames == 512,
            "Actual retained-reader512 preparation is literal K3/768");
    const auto retained = plan;
    auto refused = PreparedPluginTransport::prepare(manager, 256, {31, 1}, 1000000, 0, plan);
    require(refused.status == Status::sourceRefused &&
                refused.source.status == SourceStatus::unknownProviderBound && plan == retained,
            "Unknown custom-provider budget refuses CONTROL preparation without changing caller owner");
    refused = PreparedPluginTransport::prepare(manager, 256, {31, 1}, 1000000, 1793, plan);
    require(refused.status == Status::sourceRefused &&
                refused.source.status == SourceStatus::packetCapacityExceeded &&
                refused.source.requiredPackets == 9 && plan == retained,
            "Required K9 refuses instead of casting/clamping to an eight-slot transport");
    refused = PreparedPluginTransport::prepare(manager, 256, {130304, 1}, 1000000, 512, plan);
    require(refused.status == Status::sourceRefused &&
                refused.source.status == SourceStatus::delayCapacityExceeded && plan == retained,
            "Native plus literal768 at fixed dry capacity refuses before worker startup");
    refused = PreparedPluginTransport::prepare(manager, 15, {0, 1}, 1000000, 512, plan);
    require(refused.status == Status::sourceRefused &&
                refused.source.status == SourceStatus::invalidPreparation && plan == retained,
            "Invalid Q is a typed CONTROL refusal preserving the original plan");
    PreparedPluginTransport::Ptr expansion;
    require(PreparedPluginTransport::prepare(manager, 256, {31, 1}, 1100000, 256, expansion).succeeded() &&
                expansion->budget().sourceFrames == 282 && expansion->budget().packets == 3 &&
                expansion->budget().transportSamples == 768,
            "Actual256 device plus bounded1.1 source expansion admits282 with literal K3");
    // Probe typed startup identity before any worker is launched.
    PluginHost mismatch(plan);
    require(mismatch.start(spec, 44100., 256, false).failed() && mismatch.processId() == 0 &&
                mismatch.transportOutcome().status == Status::invalidPlan,
            "Prepared rate mismatch refuses before worker launch");

    struct Driver final : juce::AudioIODeviceCallback
    {
        PluginHost &host;
        PreparedPluginTransport::Ptr plan; // Acquired/destroyed CONTROL only.
        juce::AudioBuffer<float> work{2, 256}, capture{2, 16384};
        int sourceClock = 0, physicalPosition = 0, remainingSource = 0;
        std::uint64_t serial = 0;
        bool failed = false;
        PluginTransportOutcome firstFailure;
        bool expanded = false, bypass = false, pruned = false;
        Driver(PluginHost &h, const PreparedPluginTransport::Ptr &p, bool e, bool b, bool mute)
            : host(h), plan(p), expanded(e), bypass(b), pruned(mute)
        {
            capture.clear();
        }
        static float source(int channel, int frame) noexcept
        {
            return (channel == 0 ? .125f : -.0625f) +
                   (frame % (channel == 0 ? 251 : 257) == (channel == 0 ? 11 : 23)
                        ? (channel == 0 ? .25f : -.125f)
                        : 0.f);
        }
        void fail(PluginTransportOutcome result) noexcept
        {
            if (!failed)
                firstFailure = result;
            failed = true;
        }
        void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
        void audioDeviceStopped() override {}
        void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *output, int channels,
                                              int frames, const juce::AudioIODeviceCallbackContext &) override
        {
            if (failed)
            {
                for (int c = 0; c < channels; ++c)
                    std::fill_n(output[c], frames, 0.f);
                return;
            }
            Manager::PhysicalCallbackSnapshot physical;
            if (!plan->read(physical))
            {
                PluginTransportOutcome result;
                result.status = Status::sourceRefused;
                result.source.status = SourceStatus::telemetryUnavailable;
                fail(result);
                return;
            }
            if (physical.callbackSerial != serial)
            {
                serial = physical.callbackSerial;
                physicalPosition = 0;
                // Explicit EXPERIMENTAL bounded source generator, not Tracktion speed/resampler proof.
                remainingSource =
                    expanded ? (physical.outerFrames * 1100000 + 999999) / 1000000 : physical.outerFrames;
                const auto result = host.beginPhysicalBurst(remainingSource);
                if (!result.succeeded())
                {
                    fail(result);
                    return;
                }
            }
            const bool last = physicalPosition + frames == physical.outerFrames;
            const int requested = expanded ? (last ? remainingSource : frames) : frames;
            const int sourceFrames = pruned ? 0 : requested;
            if (sourceClock + sourceFrames > capture.getNumSamples())
            {
                PluginTransportOutcome result;
                result.status = Status::sourceSpanExceeded;
                fail(result);
                return;
            }
            for (int done = 0; done < sourceFrames;)
            {
                const int count = std::min(256, sourceFrames - done);
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < count; ++n)
                        work.setSample(c, n, source(c, sourceClock + n));
                host.process(work, 0, count, nullptr, 0, sourceClock / 48000., 120, true, bypass);
                for (int c = 0; c < 2; ++c)
                    capture.copyFrom(c, sourceClock, work, c, 0, count);
                if (auto result = host.transportOutcome(); !result.succeeded())
                {
                    fail(result);
                    return;
                }
                sourceClock += count;
                done += count;
            }
            remainingSource -= requested;
            // Ordinary source cadence goes to the ACTUAL manager output. Expanded
            // source is deliberately not masqueraded as a device-rate resampler.
            for (int c = 0; c < channels; ++c)
                if (!expanded && !pruned && c < 2)
                    std::copy_n(capture.getReadPointer(c, sourceClock - frames), frames, output[c]);
                else
                    std::fill_n(output[c], frames, 0.f);
            physicalPosition += frames;
            if (last)
            {
                auto result = host.endPhysicalBurst();
                const bool wrongCount = (!pruned && result.processedFrames != result.sourceQuota) ||
                                        (pruned && result.processedFrames != 0);
                if (wrongCount && result.succeeded())
                    result.status = Status::sourceSpanExceeded;
                if (!result.succeeded())
                    fail(result);
            }
        }
    };
    // Three real SDK hosts, each independently owned; no policy mutation or DSP cloning.
    for (const int scenario : {2, 0, 1})
    {
        const bool expanded = scenario == 2, bypass = scenario == 1;
        const auto &session = expanded ? expansion : plan;
        PluginHost host(session);
        const auto started = host.start(spec, 48000., 256, false);
        require(started.wasOk(),
                "Actual prepared K3 native worker: " + started.getErrorMessage().toStdString());
        PreparedPluginTransport::Ptr equalConfig;
        require(PreparedPluginTransport::prepare(manager, 256, {31, 1}, expanded ? 1100000 : 1000000,
                                                 expanded ? 256 : 512, equalConfig)
                    .succeeded(),
                "Independent CONTROL reader plan with equal scalar configuration");
        require(host.transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                    host.transportPacketCount() == 3 && host.transportLatencySamples() == 768 &&
                    host.latencySamples() == 799 && host.matchesPrepared(spec, 48000., 256, session) &&
                    !host.matchesPrepared(spec, 48000., 256, equalConfig) && !host.matches(spec, 48000., 256),
                "Literal768/native31=799 and exact prepared-owner reuse, never legacy or equal new reader");
        auto driver = std::make_unique<Driver>(host, session, expanded, bypass, false);
        manager.addAudioCallback(driver.get());
        struct Remove final
        {
            Manager &manager;
            Driver *driver;
            ~Remove()
            {
                manager.removeAudioCallback(driver);
            }
        } remove{manager, driver.get()};
        juce::String csv = "outer,sourceFrames,submitted,processed,missed,dropped,error,new,delete\n";
        for (int outer = 0; outer < 22; ++outer)
        {
            const int frames =
                expanded ? 256 : std::array<int, 6>{512, 127, 128, 1, 64, 512}[std::size_t(outer % 6)];
            juce::AudioBuffer<float> physical(2, frames);
            const int physicalSourceStart = driver->sourceClock;
            const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
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
                } probe;
                device->process(physical); // REAL JUCE outer enforcer; no CONTROL between inner chunks.
            }
            const auto allocations = callbackAllocations.load() - allocated;
            const auto destructions = callbackDestructions.load() - destroyed;
            // CONTROL scheduling only after the actual physical callback returns.
            dispatchMessages(6);
            host.service();
            const auto stats = host.statistics();
            double error = 0;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < driver->sourceClock; ++n)
                {
                    const float expected = n < 799 ? 0.f : Driver::source(c, n - 799);
                    const auto actual = driver->capture.getSample(c, n);
                    error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                  : std::numeric_limits<double>::infinity();
                }
            if (!expanded)
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < frames; ++n)
                    {
                        const int source = physicalSourceStart + n - 799;
                        const float expected = source < 0 ? 0.f : Driver::source(c, source);
                        const auto actual = physical.getSample(c, n);
                        error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                      : std::numeric_limits<double>::infinity();
                    }
            csv += juce::String(outer) + "," + juce::String(driver->sourceClock) + "," +
                   juce::String(juce::int64(stats.submitted)) + "," +
                   juce::String(juce::int64(stats.processed)) + "," +
                   juce::String(juce::int64(stats.missed)) + "," + juce::String(juce::int64(stats.dropped)) +
                   "," + juce::String(error, 12) + "," + juce::String(allocations) + "," +
                   juce::String(destructions) + "\n";
            require(folder.getChildFile("scenario-" + juce::String(scenario) + ".csv").replaceWithText(csv),
                    "Preserve actual native source-cadence packet/PCM/fault evidence before assertion");
            const auto consumed = host.consumedLatency();
            const bool phaseCorrect =
                driver->sourceClock <= 768
                    ? !consumed.has_value()
                    : consumed && consumed->stamp.nativeSamples == 31 &&
                          consumed->sequence == std::uint64_t((driver->sourceClock - 1) / 256 - 3);
            const bool ok = !driver->failed && host.transportOutcome().succeeded() && host.alive() &&
                            !host.deliveryFailed() && error == 0 && allocations == 0 && destructions == 0 &&
                            phaseCorrect && stats.submitted == std::uint64_t(driver->sourceClock / 256);
            if (!ok)
            {
                const auto raw = folder.getChildFile("scenario-" + juce::String(scenario) + "-failure.raw");
                auto stream = raw.createOutputStream();
                require(bool(stream) && stream->setPosition(0) && stream->truncate().wasOk(),
                        "Truncate owned failed source-cadence PCM");
                for (int c = 0; c < 2; ++c)
                    require(stream->write(driver->capture.getReadPointer(c),
                                          std::size_t(driver->sourceClock) * sizeof(float)),
                            "Retain complete actual source-cadence PCM, no fitted shift");
            }
            require(ok, "Exact independent literal799 PCM, once-only submission, no callback C++ heap or "
                        "packet loss");
        }
        const auto drainDeadline = juce::Time::getMillisecondCounter() + 2000;
        while (host.statistics().processed != host.statistics().submitted && host.alive() &&
               juce::Time::getMillisecondCounter() < drainDeadline)
        {
            dispatchMessages(1); // CONTROL after callback driver quiescence only.
            host.service();
        }
        require(
            host.alive() && host.statistics().processed == host.statistics().submitted,
            "Every submitted packet actually processed after bounded CONTROL drain, not immediate snapshots");
        // An admitted Host may legally receive NONE of the prepared source quota.
        driver->pruned = true;
        juce::AudioBuffer<float> muted(2, expanded ? 256 : 512);
        const auto before = host.statistics();
        device->process(muted);
        const auto after = host.statistics();
        require(!driver->failed && host.transportOutcome().succeeded() &&
                    after.submitted == before.submitted && after.missed == before.missed &&
                    after.dropped == before.dropped,
                "Zero-frame pruned node closes a positive upper quota without fabricated packet obligation");
        if (scenario == 1)
        {
            driver->pruned = false;
            const auto sourceClock = driver->sourceClock, cursor = host.packetFrame();
            juce::AudioBuffer<float> oversized(2, 768);
            device->process(oversized);
            const auto fault = host.transportOutcome();
            require(driver->failed && fault.status == Status::sourceRefused &&
                        fault.source.status == SourceStatus::physicalBurstExceeded &&
                        driver->sourceClock == sourceClock && host.packetFrame() == cursor &&
                        host.statistics().submitted == after.submitted && host.latencySamples() == -1 &&
                        !host.alive(),
                    "Actual768 physical burst refuses BEFORE first host slice; no hidden reslicing or PCM "
                    "mutation");
            host.service(); // Existing exact-owned worker graceful containment; no callback shutdown.
        }
    }
    manager.closeAudioDevice();
    PluginHost stopped(plan);
    require(stopped.start(spec, 48000., 256, false).failed() && stopped.processId() == 0 &&
                stopped.transportOutcome().source.status == SourceStatus::deviceStopped,
            "Retained reader after device stop refuses startup without launching another worker");
    preparedPluginTransportScopeTests(folder, spec);
}
