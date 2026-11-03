// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/PluginDryHistory.h"
#include "../src/PluginHost.h"
#include <array>
#include <cmath>
#include <memory>
#include <limits>

namespace vibera
{
// CONTROL preparation only. This friend exposes the actual immutable preparation
// receipt and shared pure counter policy; it cannot inject Host counters or a
// ring/writer, inspect Impl or change DSP ownership.
struct PluginDryHistoryHostFixture
{
    struct Preparation
    {
        PluginDryHistory::Ptr history;
        PluginPcmIdentity identity;
        const void *writer = nullptr;
    };
    static std::optional<Preparation> retain(const PluginHost &host)
    {
        const auto owned = host.prepareDryHistoryRetention();
        if (!owned)
            return {};
        return Preparation{owned->history, owned->identity, owned->writer};
    }
    static bool countersPermit(const PluginHost::Statistics &stats,
                               const PluginHost::AutomationStatistics &automation = {},
                               std::uint64_t rejectedLatency = 0, std::uint64_t sourceFault = 0) noexcept
    {
        return PluginHost::dryHistoryCountersPermit(stats, automation, rejectedLatency, sourceFault);
    }
};
} // namespace vibera

// Include after ParameterAcceptance/SyntheticDevice/probe/timing helpers.
namespace vibera::test::dry_history_host
{
constexpr int quantum = 256, rate = 48000, capacity = 32768;
inline void counterAdmissionTests()
{
    using Stats = PluginHost::Statistics;
    using Automation = PluginHost::AutomationStatistics;
    require(PluginDryHistoryHostFixture::countersPermit(Stats{}),
            "Pure CONTROL eligibility accepts clean counters");
    // Synthetic snapshots exercise the exact production predicate. These are
    // NOT injected Host failures, provider recovery or graph continuity proof.
    Stats recovered;
    recovered.submitted = recovered.processed = 96;
    recovered.parametersApplied = 12;
    recovered.workerHeartbeat = 96;
    recovered.missed = 3;
    recovered.dropped = 2;
    require(PluginDryHistoryHostFixture::countersPermit(recovered) && recovered.missed == 3 &&
                recovered.dropped == 2 && recovered.submitted == 96 && recovered.processed == 96,
            "Historical PCM loss permits CONTROL retention without clearing cumulative diagnostics");
    auto maximum = recovered;
    maximum.missed = maximum.dropped = std::numeric_limits<std::uint64_t>::max();
    require(PluginDryHistoryHostFixture::countersPermit(maximum) &&
                maximum.missed == std::numeric_limits<std::uint64_t>::max() &&
                maximum.dropped == std::numeric_limits<std::uint64_t>::max(),
            "Historical PCM counters are neither summed, overflowed nor normalized");
    for (auto field : {&Stats::midiOverflow, &Stats::parametersRejected, &Stats::auxiliaryRejected})
    {
        auto fatal = recovered;
        fatal.*field = 1;
        require(!PluginDryHistoryHostFixture::countersPermit(fatal) && fatal.*field == 1 &&
                    fatal.missed == 3 && fatal.dropped == 2,
                "Each fatal MIDI/parameter/auxiliary counter refuses independently and remains intact");
    }
    require(!PluginDryHistoryHostFixture::countersPermit(recovered, {}, 1, 0) &&
                !PluginDryHistoryHostFixture::countersPermit(recovered, {}, 0, 1),
            "Latency rejection and transport fault still independently refuse CONTROL retention");
    for (auto field : {&Automation::invalidTiming, &Automation::capacityExceeded,
                       &Automation::unavailableTarget, &Automation::snapshotUnavailable})
    {
        Automation fatal;
        fatal.*field = 1;
        require(!PluginDryHistoryHostFixture::countersPermit(recovered, fatal) && fatal.*field == 1,
                "Every sticky automation failure still refuses CONTROL retention");
    }
}
inline float source(int c, int frame) noexcept
{
    if (frame < 0)
        return 0;
    return c == 0 ? float((17 * frame + 13) % 251 - 125) / 1024.f
                  : float((29 * frame + 7) % 257 - 128) / 2048.f;
}
struct Device
{
    juce::AudioDeviceManager manager;
    SyntheticDevice *device = nullptr;
    Device()
    {
        manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", rate);
        setup.setAttribute("audioDeviceBufferSize", quantum);
        require(manager.initialise(0, 2, &setup, false).isEmpty(), "Owned actual prepared3Q manager");
        device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == rate, "Actual original synthetic device");
    }
};
struct Driver final : juce::AudioIODeviceCallback
{
    PluginHost *host; // CONTROL excludes processing before destruction; never read after claim.
    PluginDryHistory *const history;
    const PreparedPluginTransport *const plan;
    const PluginPcmIdentity identity;
    const void *const writer;
    const PluginLatencyStamp native;
    const int packets, delay;
    juce::AudioBuffer<float> work{2, quantum};
    std::array<PluginPcmSegment, 2> entries{};
    PluginPcmCapture capture;
    PluginDryHistory::Witness claimedWitness, previous;
    std::array<PluginDryHistory::Status, 6> negatives{};
    PluginDryHistory::Status claimStatus = PluginDryHistory::Status::unavailable;
    int clock = 0, wrongOwner = 0, continuationOwner = 0;
    bool bypass = false, requestClaim = false, claimed = false;
    bool healthy = true, earlyNotDry = false, wrongRenderUntouched = false;
    float blend = 0;
    std::uint64_t serial = 0;
    Driver(PluginHost &h, const PluginDryHistoryHostFixture::Preparation &p,
           const PreparedPluginTransport *prepared)
        : host(&h), history(p.history.get()), plan(prepared), identity(p.identity), writer(p.writer),
          native(h.declaredLatency()), packets(h.transportPacketCount()), delay(packets * quantum + 31),
          capture(entries, identity)
    {
    }
    void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
    void audioDeviceStopped() override {}
    void span(int count) noexcept
    {
        if (count <= 0 || count > quantum || clock + count > capacity)
        {
            healthy = false;
            return;
        }
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < count; ++n)
                work.setSample(c, n, source(c, clock + n));
        if (claimed)
        {
            const auto before = history->witness();
            const auto first = work.getSample(0, 0), second = work.getSample(1, 0);
            const auto refused =
                history->render(&wrongOwner, work.getWritePointer(0), work.getWritePointer(1), count);
            wrongRenderUntouched = refused == PluginDryHistory::Status::wrongOwner &&
                                   history->witness() == before && work.getSample(0, 0) == first &&
                                   work.getSample(1, 0) == second;
            healthy &= wrongRenderUntouched &&
                       history->render(&continuationOwner, work.getWritePointer(0), work.getWritePointer(1),
                                       count) == PluginDryHistory::Status::ready;
            clock += count;
            return; // No Host/shared_ptr/worker/plan access after transfer.
        }
        previous = history->witness(); // Serialized processing owner, never CONTROL witness inspection.
        bool scope = false;
        if (plan)
        {
            juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
            if (!plan->read(physical) || physical.outerFrames != count || physical.callbackSerial <= serial)
            {
                healthy = false;
                return;
            }
            serial = physical.callbackSerial;
            const auto opened = host->beginPhysicalBurst(count);
            scope = opened.succeeded();
            healthy &= scope;
            if (!scope)
            {
                host->abortPhysicalBurst();
                return;
            }
        }
        host->process(work, 0, count, nullptr, 0, clock / double(rate), 120, true, bypass, 4, 4, nullptr, 0,
                      0, true, nullptr, 0, 1, &capture);
        blend = host->bypassBlend().value_or(-1);
        healthy &= capture.status == PluginPcmReceiptStatus::complete && capture.identity == identity &&
                   capture.count == 1 && capture.storage.size() >= capture.count && count == quantum;
        if (capture.count == 1)
        {
            const auto &s = entries[0];
            const int consumed = clock / quantum - packets;
            healthy &=
                s.offset == 0 && s.count == count && s.packetFrame == 0 &&
                s.outputPacket == std::uint64_t(clock / quantum) && !s.forcedDry &&
                s.dryDelaySamples == delay && s.firstWet == 1 && s.lastWet == 1 &&
                s.packet == (consumed < 0 ? PluginPcmPacketKind::startup : PluginPcmPacketKind::completed);
            if (consumed < 0)
                healthy &= !s.consumed;
            else
                healthy &= s.consumed && s.consumed->sequence == std::uint64_t(consumed) &&
                           s.consumed->stamp == native;
            if (!bypass)
                healthy &=
                    s.mix == PluginPcmMixKind::wet && s.firstBypass == 0 && s.lastBypass == 0 && blend == 0;
            else if (clock == 8 * quantum)
                healthy &= s.mix == PluginPcmMixKind::mixed &&
                           std::abs(double(s.firstBypass) - 1. / 240.) < 1.e-8 && s.lastBypass == 1 &&
                           blend == 1;
            else
                healthy &= s.mix == PluginPcmMixKind::delayedDry && s.firstBypass == 1 && s.lastBypass == 1 &&
                           blend == 1;
        }
        if (scope)
        {
            const auto closed = host->endPhysicalBurst();
            healthy &= closed.succeeded() && closed.processedFrames == count;
            if (!closed.succeeded())
                host->abortPhysicalBurst();
        }
        const auto actual = history->witness();
        // Actual warm completed wet history, before requesting bypass, must refuse
        // the handoff without changing witness, ownership or subsequent PCM.
        if (clock == 7 * quantum)
        {
            earlyNotDry = history->claim(writer, &continuationOwner, actual, delay, native, quantum) ==
                              PluginDryHistory::Status::notDry &&
                          history->witness() == actual && history->phase() == PluginDryHistory::Phase::host;
            healthy &= earlyNotDry;
        }
        if (requestClaim)
        {
            auto wrongNative = native;
            ++wrongNative.generation;
            negatives[0] = history->claim(&wrongOwner, &continuationOwner, actual, delay, native, quantum);
            negatives[1] = history->claim(writer, &continuationOwner, previous, delay, native, quantum);
            negatives[2] = history->claim(writer, &continuationOwner, actual, delay, wrongNative, quantum);
            negatives[3] = history->claim(writer, &continuationOwner, actual, delay + 1, native, quantum);
            negatives[4] = history->claim(writer, &continuationOwner, actual, delay, native, 0);
            healthy &= negatives[0] == PluginDryHistory::Status::wrongOwner &&
                       negatives[1] == PluginDryHistory::Status::staleWitness &&
                       negatives[2] == PluginDryHistory::Status::nativeChanged &&
                       negatives[3] == PluginDryHistory::Status::delayChanged &&
                       negatives[4] == PluginDryHistory::Status::invalidSpan &&
                       history->witness() == actual && history->phase() == PluginDryHistory::Phase::host;
            claimedWitness = actual;
            claimStatus = history->claim(writer, &continuationOwner, actual, delay, native, quantum);
            claimed = claimStatus == PluginDryHistory::Status::ready;
            healthy &= claimed && actual.completedWet && actual.dryEndpoint && actual.delay == delay &&
                       actual.native == native && actual.knownFrames >= delay &&
                       actual.frames == std::uint64_t(clock + count) &&
                       history->phase() == PluginDryHistory::Phase::continuation;
            negatives[5] = history->claim(writer, &continuationOwner, actual, delay, native, quantum);
            healthy &=
                negatives[5] == PluginDryHistory::Status::alreadyClaimed && history->witness() == actual;
        }
        clock += count;
    }
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *out, int channels,
                                          int count, const juce::AudioIODeviceCallbackContext &) override
    {
        if (count <= 0 || count > quantum)
        {
            healthy = false;
            for (int c = 0; c < channels; ++c)
                std::fill_n(out[c], std::max(0, count), 0.f);
            return;
        }
        span(count);
        for (int c = 0; c < channels; ++c)
            if (c < 2)
                std::copy_n(work.getReadPointer(c), count, out[c]);
            else
                std::fill_n(out[c], count, 0.f);
    }
};
inline void raw(const juce::File &file, const juce::AudioBuffer<float> &data, int frames)
{
    auto stream = file.createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk(), "Owned exact PCM evidence reset");
    for (int c = 0; c < 2; ++c)
        require(stream->write(data.getReadPointer(c), std::size_t(frames) * sizeof(float)), "Retain all PCM");
    stream->flush();
    require(stream->getStatus().wasOk(), "Complete evidence flush");
}
__declspec(noinline) inline void scenario(const juce::File &folder, const PluginSpec &spec, bool live)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned SDK history case");
    auto device = live ? std::make_unique<Device>() : nullptr;
    PreparedPluginTransport::Ptr plan;
    if (live)
        require(PreparedPluginTransport::prepare(device->manager, quantum, {31, 1}, 1000000, 512, plan)
                        .succeeded() &&
                    plan->budget().packets == 3 && plan->budget().transportSamples == 768,
                "Real retained physical reader, independently literal preparedK3/768");
    auto host = live ? std::make_unique<PluginHost>(plan) : std::make_unique<PluginHost>();
    require(host->start(spec, rate, quantum, !live).wasOk() && host->declaredLatency().nativeSamples == 31 &&
                host->latencySamples() == (live ? 799 : 287) && host->bypassTransitionSamples() == 240,
            "Actual SDK native31, legacy1Q/offline or prepared3Q/live, original5ms bypass");
    const auto retained = PluginDryHistoryHostFixture::retain(*host);
    require(retained && retained->history && retained->writer && retained->identity == host->pcmIdentity(),
            "Actual CONTROL pre-owned Host ring/immutable identity/writer, no replacement history");
    auto driver = std::make_unique<Driver>(*host, *retained, plan.get());
    if (live)
        device->manager.addAudioCallback(driver.get());
    struct Exclude
    {
        Device *device;
        Driver *driver;
        ~Exclude()
        {
            if (device)
                device->manager.removeAudioCallback(driver);
        }
    } exclusion{device.get(), driver.get()};
    juce::AudioBuffer<float> output(2, quantum), actual(2, capacity), expected(2, capacity);
    juce::String csv =
        "phase,frame,count,error,ms,newCalls,deleteCalls,claimed,healthy,packet,consumed,native,blend\n";
    const auto drive = [&](int count, const char *phase)
    {
        const auto clock = driver->clock;
        const bool continuation = driver->claimed;
        // setSize is CONTROL-only; physical readback still records the genuine count.
        output.setSize(2, count, false, false, true);
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            if (live)
                device->device->process(output);
            else
                driver->span(count); // Render owner: existing offline waits never run in a live callback.
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto newCalls = callbackAllocations.load() - allocated,
                   deleteCalls = callbackDestructions.load() - deleted;
        double error = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < count; ++n)
            {
                const auto value = live ? output.getSample(c, n) : driver->work.getSample(c, n);
                const auto want = source(c, clock + n - (live ? 799 : 287));
                actual.setSample(c, clock + n, value);
                expected.setSample(c, clock + n, want);
                error = std::isfinite(value) ? std::max(error, std::abs(double(value) - want)) : INFINITY;
            }
        const auto &segment = driver->entries[0];
        csv +=
            juce::String(phase) + "," + juce::String(clock) + "," + juce::String(count) + "," +
            juce::String(error, 12) + "," + juce::String(ms, 6) + "," + juce::String(newCalls) + "," +
            juce::String(deleteCalls) + "," + juce::String(int(driver->claimed)) + "," +
            juce::String(int(driver->healthy)) + "," + juce::String(continuation ? -1 : int(segment.packet)) +
            "," +
            juce::String(!continuation && segment.consumed ? juce::int64(segment.consumed->sequence) : -1) +
            "," +
            juce::String(!continuation && segment.consumed ? segment.consumed->stamp.nativeSamples : -1) +
            "," + juce::String(driver->blend, 9) + "\n";
        require(folder.getChildFile("spans.csv").replaceWithText(csv),
                "Retain actual conditions before assertion");
        raw(folder.getChildFile("actual-planar-f32.raw"), actual, driver->clock);
        raw(folder.getChildFile("expected-planar-f32.raw"), expected, driver->clock);
        require(driver->clock == clock + count && driver->healthy && error == 0 && newCalls == 0 &&
                    deleteCalls == 0 && ms <= 1000. * count / rate,
                "Literal signed source(t-D) every sample, real packet/claim invariants, zero C++ heap and "
                "strict span deadline");
        if (host)
        {
            const auto stats = host->statistics();
            require(host->alive() && stats.missed == 0 && stats.dropped == 0,
                    "Actual SDK native worker has no missing/dropped packet");
        }
        dispatchMessages(6); // CONTROL pacing only, not a callback wait or native graph qualification.
    };
    for (int n = 0; n < 8; ++n)
        drive(quantum, "real_host_wet_all_startup_samples");
    const auto afterProcessing = PluginDryHistoryHostFixture::retain(*host);
    require(afterProcessing && afterProcessing->history == retained->history &&
                afterProcessing->identity == retained->identity &&
                afterProcessing->writer == retained->writer && !host->deliveryFailed(),
            "Actual healthy SDK processing retains the SAME prepared ring/identity/writer on CONTROL");
    driver->bypass = true;
    drive(quantum, "real_240frame_bypass_ramp_all_samples");
    driver->requestClaim = true;
    drive(quantum, "actual_delayed_dry_endpoint_and_claim");
    require(driver->claimed && driver->earlyNotDry &&
                retained->history->phase() == PluginDryHistory::Phase::continuation,
            "Actual populated ring transferred only at healthy native31 dry endpoint");
    const auto beforeRetire = host->statistics();
    require(beforeRetire.processed > 0 && host->consumedLatency() &&
                host->consumedLatency()->stamp == host->declaredLatency(),
            "Actual completed native PCM, not submission alone");
    // This existing public typed producer-fault path is a REAL Host ledger
    // failure, separate from the synthetic counter-policy cases above. No more
    // Host processing follows the completed one-way dry transfer.
    host->recordAutomationStatus(PluginHost::AutomationStatus::invalidTiming);
    require(host->alive() && host->automationStatistics().invalidTiming == 1 &&
                host->deliveryFailed() && !PluginDryHistoryHostFixture::retain(*host) &&
                host->statistics().missed == beforeRetire.missed &&
                host->statistics().dropped == beforeRetire.dropped,
            "Actual Host automation failure refuses CONTROL retention without clearing loss diagnostics");
    host->beginRetirement(); // No processing reader may touch Host again.
    const auto began = juce::Time::getMillisecondCounter();
    while (!host->retirementComplete() && juce::Time::getMillisecondCounter() - began < 10000)
    {
        host->service();
        dispatchMessages(2);
    }
    require(host->retirementComplete(), "Real owned worker exited before Host CONTROL destruction");
    driver->host = nullptr; // CONTROL after excluded callback; continuation never dereferences it.
    host.reset();
    require(folder.getChildFile("worker-retirement.txt")
                .replaceWithText(
                    "Actual worker retirementComplete; Host destroyed CONTROL before remaining spans.\n"),
            "Retain actual worker/Host lifetime milestone");
    constexpr std::array<int, 8> spans{1, 17, 31, 223, 256, 127, 64, 65};
    for (int n = 0; n < 32; ++n)
        drive(spans[std::size_t(n) % spans.size()], "same_ring_after_host_worker_retired");
}
__declspec(noinline) inline void tests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk(), "Owned dry-history actual SDK attempt");
    counterAdmissionTests(); // Pure synthetic metadata policy; NOT provider recovery evidence.
    const auto spec = parameterSpec(binary);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Actual original native31 SDK fixture");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), binary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("identities.txt").replaceWithText(identities),
            "Retain actual binary identities");
    int failures = 0;
    juce::String result;
    for (const bool live : {false, true})
    {
        const auto name = juce::String(live ? "prepared3q-live" : "legacy1q-offline");
        try
        {
            scenario(folder.getChildFile(name), spec, live);
            result += name + " PASS real Host populated ring, exact PCM, refusal invariants, actual worker "
                             "exit, continued partial spans\n";
        }
        catch (const std::exception &error)
        {
            ++failures;
            result += name + " FAIL " + juce::String(error.what()) + "\n";
        }
        require(folder.getChildFile("cases.txt").replaceWithText(result),
                "Independent actual provider branch outcome");
    }
    require(failures == 0, result.toStdString());
}
} // namespace vibera::test::dry_history_host
