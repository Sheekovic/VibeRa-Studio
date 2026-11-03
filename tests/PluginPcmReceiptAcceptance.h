// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <limits>
#include <exception>

// Include after ParameterAcceptance.h, CallbackAllocationProbe.h and timing helpers.
// Actual original SDK workers, not a mock Host. CONTROL dispatch between spans
// qualifies functional direct processing only, not physical device scheduling.
namespace plugin_pcm_receipts
{
constexpr int quantum = 256, rate = 48000;
inline float source(int channel, int frame)
{
    return frame < 0 ? 0.f : float(((frame * (channel ? 19 : 13) + 7) % 127) - 63) / 512.f;
}
inline int nativeForPacket(int packet)
{
    constexpr std::array<int, 8> delay{31, 31, 0, 0, 257, 257, 31, 31};
    return packet < 0 ? 0 : delay[std::size_t(packet) % delay.size()];
}
inline std::uint32_t generationForPacket(int packet)
{
    std::uint32_t generation = 1;
    for (int n = 1; n <= packet; ++n)
        generation += nativeForPacket(n) != nativeForPacket(n - 1);
    return generation;
}
inline float normalizedDelay(int native)
{
    return native == 0 ? 0.f : native == 31 ? 1.f / 3 : native == 257 ? 2.f / 3 : 1.f;
}
inline void waitForSubmitted(PluginHost &host)
{
    const auto wanted = host.statistics().submitted;
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (host.statistics().processed < wanted && juce::Time::getMillisecondCounter() < deadline)
        dispatchMessages(1); // CONTROL only, never within processing/heap probes.
    require(host.statistics().processed == wanted, "Actual worker completed exact submitted prefix");
}
inline bool sameUuid(const PluginPcmIdentity &identity, const juce::String &instance)
{
    const juce::Uuid uuid(instance);
    return std::equal(identity.instance.begin(), identity.instance.end(), uuid.getRawData());
}
// All observations are committed BEFORE require so failed PCM/receipt evidence
// is retained; no retries or phase calibration.
__declspec(noinline) inline void latencyCase(const juce::File &folder, const PluginSpec &spec,
                                             PluginHost::TransportPolicy policy, bool offline)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh exact worker receipt evidence");
    PluginHost host(policy);
    require(host.start(spec, rate, quantum, offline).wasOk(), "Genuine latency fixture worker startup");
    PluginHost legacy(policy);
    require(legacy.start(spec, rate, quantum, offline).wasOk(), "Independent identical no-capture worker");
    const auto catalogue = parameterCatalogue(host);
    const auto legacyCatalogue = parameterCatalogue(legacy);
    const auto instance = host.runtimeIdentity();
    const auto pid = host.processId();
    const int packets = policy == PluginHost::TransportPolicy::oneQuantum ? 1 : 2;
    const int transport = packets == 1 ? 256 : 512; // Literal independent contracts.
    require(host.transportLatencySamples() == transport &&
                host.declaredLatency() == PluginLatencyStamp{31, 1},
            "Actual original native31/default generation, no fabricated consumed startup");
    constexpr int maximumFrames = 8192;
    juce::AudioBuffer<float> audio(2, 270), legacyAudio(2, 270), actual(2, maximumFrames),
        expected(2, maximumFrames), legacyPcm(2, maximumFrames);
    actual.clear();
    expected.clear();
    legacyPcm.clear();
    std::array<PluginPcmSegment, 2> storage{};
    PluginPcmCapture capture(storage);
    juce::String trace = "frame,frames,offset,count,packetFrame,outputPacket,kind,consumed,"
                         "native,generation,dryDelay,mix,firstBypass,lastBypass,maxError,ms,new,delete\n";
    int clock = 0;
    double bypass = 0;
    bool passed = true;
    juce::String failure;
    const auto drive = [&](int frames, bool targetBypass)
    {
        require(frames > 0 && frames <= 256 && clock + frames <= maximumFrames,
                "Bounded original partial worker call");
        // There is at most one completed input packet in an admitted <=Q call.
        // Set native mode before submission, after prior processing is acknowledged.
        // Native declaration changes are not sample automation and are never sent in DSP.
        if ((clock + frames) / quantum > clock / quantum)
        {
            waitForSubmitted(host);
            waitForSubmitted(legacy);
            const int inputPacket = (clock + frames) / quantum - 1;
            require(parameterResult(host, host.setParameter(17, normalizedDelay(nativeForPacket(inputPacket)),
                                                            catalogue.generation))
                            .result == 0,
                    "Actual native controller mode acknowledgement outside processing");
            require(
                parameterResult(legacy, legacy.setParameter(17, normalizedDelay(nativeForPacket(inputPacket)),
                                                            legacyCatalogue.generation))
                        .result == 0,
                "Separate no-capture worker receives same original native mode");
        }
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < audio.getNumSamples(); ++n)
            {
                const auto sample = n >= 7 && n < 7 + frames ? source(c, clock + n - 7) : .875f;
                audio.setSample(c, n, sample);
                legacyAudio.setSample(c, n, sample);
            }
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct HeapProbe
            {
                bool previous = callbackAllocationProbe;
                explicit HeapProbe(bool live)
                {
                    callbackAllocationProbe = live;
                }
                ~HeapProbe()
                {
                    callbackAllocationProbe = previous;
                }
            } probe(!offline);
            host.process(audio, 7, frames, nullptr, 0, clock / double(rate), 120, true, targetBypass, 4, 4,
                         nullptr, 0, 0, true, nullptr, 0, 1, &capture);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto allocations = callbackAllocations.load() - allocated;
        const auto destructions = callbackDestructions.load() - deleted;
        // Same established public process call with no optional capture. Original
        // independent worker/ring proves instrumentation preserves actual output.
        legacy.process(legacyAudio, 7, frames, nullptr, 0, clock / double(rate), 120, true, targetBypass);
        bool ok = capture.status == PluginPcmReceiptStatus::complete &&
                  capture.count == std::size_t(1 + ((clock % quantum) + frames - 1) / quantum) &&
                  capture.required == capture.count && sameUuid(capture.identity, instance) &&
                  capture.identity.host == reinterpret_cast<std::uintptr_t>(&host) &&
                  capture.identity.preparedTransport == 0 && capture.identity.quantum == 256 &&
                  capture.identity.transportSamples == transport && host.processId() == pid &&
                  host.runtimeIdentity() == instance && host.packetFrame() == (clock + frames) % 256;
        std::array<float, 256> firstBypass{};
        double maximumError = 0;
        for (int n = 0; n < frames; ++n)
        {
            bypass = targetBypass ? std::min(1., bypass + 1. / 240.) : std::max(0., bypass - 1. / 240.);
            firstBypass[std::size_t(n)] = float(bypass);
            const int packet = (clock + n) / 256 - packets;
            const int native = nativeForPacket(packet);
            const int wetSource = clock + n - transport - native;
            // Before the first valid wet packet, Host's consumed native dry epoch
            // is zero. Both independently authored sources are zero before frame0.
            // Host startup explicitly seeds its dry tap with the fixture's declared31.
            // No completed wet packet is inferred for this initial interval.
            const int dryNative = packet >= 0 ? native : 31;
            const int drySource = clock + n - transport - dryNative;
            for (int c = 0; c < 2; ++c)
            {
                const float wet = packet < 0 ? 0 : source(c, wetSource);
                const float dry = source(c, drySource);
                const float wanted = bypass == 0   ? wet
                                     : bypass == 1 ? dry
                                                   : wet + (dry - wet) * float(bypass);
                const float observed = audio.getSample(c, 7 + n);
                actual.setSample(c, clock + n, observed);
                expected.setSample(c, clock + n, wanted);
                legacyPcm.setSample(c, clock + n, legacyAudio.getSample(c, 7 + n));
                ok &= observed == legacyAudio.getSample(c, 7 + n);
                maximumError = std::isfinite(observed)
                                   ? std::max(maximumError, std::abs(double(observed) - wanted))
                                   : std::numeric_limits<double>::infinity();
            }
        }
        int done = 0;
        for (const auto &segment : capture.segments())
        {
            const int outputPacket = (clock + done) / 256, consumed = outputPacket - packets;
            const int packetFrame = (clock + done) % 256;
            const int count = std::min(frames - done, 256 - packetFrame);
            const auto kind = consumed < 0 ? PluginPcmPacketKind::startup : PluginPcmPacketKind::completed;
            ok &= segment.offset == done && segment.count == count && segment.packetFrame == packetFrame &&
                  segment.outputPacket == std::uint64_t(outputPacket) && segment.packet == kind &&
                  !segment.forcedDry &&
                  segment.dryDelaySamples == transport + (consumed >= 0 ? nativeForPacket(consumed) : 31) &&
                  segment.firstWet == 1 && segment.lastWet == 1 &&
                  segment.firstBypass == firstBypass[std::size_t(done)] &&
                  segment.lastBypass == firstBypass[std::size_t(done + count - 1)];
            if (consumed < 0)
                ok &= !segment.consumed;
            else
                ok &= segment.consumed && segment.consumed->sequence == std::uint64_t(consumed) &&
                      segment.consumed->stamp ==
                          PluginLatencyStamp{nativeForPacket(consumed), generationForPacket(consumed)};
            const auto first = firstBypass[std::size_t(done)],
                       last = firstBypass[std::size_t(done + count - 1)];
            const auto mix = first == 0 && last == 0   ? PluginPcmMixKind::wet
                             : first == 1 && last == 1 ? PluginPcmMixKind::delayedDry
                                                       : PluginPcmMixKind::mixed;
            ok &= segment.mix == mix;
            trace += juce::String(clock) + "," + juce::String(frames) + "," + juce::String(done) + "," +
                     juce::String(segment.count) + "," + juce::String(segment.packetFrame) + "," +
                     juce::String(juce::int64(segment.outputPacket)) + "," +
                     juce::String(int(segment.packet)) + "," +
                     juce::String(segment.consumed ? juce::int64(segment.consumed->sequence) : -1) + "," +
                     juce::String(segment.consumed ? segment.consumed->stamp.nativeSamples : -1) + "," +
                     juce::String(segment.consumed ? segment.consumed->stamp.generation : 0) + "," +
                     juce::String(segment.dryDelaySamples) + "," + juce::String(int(segment.mix)) + "," +
                     juce::String(segment.firstBypass, 9) + "," + juce::String(segment.lastBypass, 9) + "," +
                     juce::String(maximumError, 12) + "," + juce::String(ms, 7) + "," +
                     juce::String(allocations) + "," + juce::String(destructions) + "\n";
            done += count;
        }
        const auto statistics = host.statistics();
        ok &= done == frames && maximumError <= 2e-6 && statistics.missed == 0 && statistics.dropped == 0;
        if (!offline)
            ok &= allocations == 0 && destructions == 0 && ms <= frames * 1000. / rate;
        for (int c = 0; c < 2; ++c)
            ok &= audio.getSample(c, 0) == .875f && audio.getSample(c, 7 + frames) == .875f;
        clock += frames;
        require(folder.getChildFile("segments.csv").replaceWithText(trace), "Retain spans before assertion");
        passed &= ok;
        require(ok, "Exact native generation/partial receipt/PCM/ramp/cursor/owner/deadline contract");
        waitForSubmitted(host); // Outside callback; this is NOT physical pacing qualification.
        waitForSubmitted(legacy);
    };
    try
    {
        // LIVE default1Q's known cross-boundary causality failure is NOT qualified
        // here. Default offline and explicit2Q live/offline use all partial spans.
        const bool partial = offline || packets == 2;
        for (int n = 0; n < 20; ++n)
            drive(partial ? std::array<int, 4>{127, 64, 64, 65}[std::size_t(n) % 4] : 256, n >= 8 && n < 15);
        // Completion data remains valid after a metadata-only scope abort; abort
        // is not an audio reset or a claim of async completion/recovery.
        const auto cursor = host.packetFrame();
        const auto submitted = host.statistics().submitted;
        host.abortPhysicalBurst();
        require(host.packetFrame() == cursor && host.statistics().submitted == submitted &&
                    capture.status == PluginPcmReceiptStatus::complete,
                "Abort closes only physical metadata, no fabricated receipt or stream reset");
        PluginPcmCapture empty(std::span<PluginPcmSegment>{});
        host.process(audio, 0, 0, nullptr, 0, clock / double(rate), 120, true, false, 4, 4, nullptr, 0, 0,
                     true, nullptr, 0, 1, &empty);
        require(empty.status == PluginPcmReceiptStatus::complete && empty.required == 0 && empty.count == 0 &&
                    host.packetFrame() == cursor && host.statistics().submitted == submitted,
                "Empty call completes empty coverage, never an invented packet");
        if (!offline && packets == 2)
        {
            for (int c = 0; c < 2; ++c)
                std::fill_n(audio.getWritePointer(c), 270, .875f);
            host.process(audio, 7, 257, nullptr, 0, clock / double(rate), 120, true, false, 4, 4, nullptr, 0,
                         0, true, nullptr, 0, 1, &capture);
            require(capture.status == PluginPcmReceiptStatus::invalidSpan && capture.count == 0 &&
                        host.packetFrame() == cursor && host.statistics().submitted == submitted &&
                        audio.getSample(0, 7) == .875f && audio.getSample(1, 263) == .875f,
                    "Unsupported explicit2Q live span retains original refusal and no receipt completion");
        }
    }
    catch (const std::exception &e)
    {
        callbackAllocationProbe = false;
        passed = false;
        failure = e.what();
    }
    const auto write = [&](const char *name, const juce::AudioBuffer<float> &pcm)
    {
        auto stream = folder.getChildFile(name).createOutputStream();
        require(bool(stream), "Owned original exact PCM evidence");
        for (int c = 0; c < 2; ++c)
            require(stream->write(pcm.getReadPointer(c), std::size_t(clock) * sizeof(float)),
                    "Raw planar PCM");
    };
    write("actual-float32le.raw", actual);
    write("expected-float32le.raw", expected);
    write("legacy-no-capture-float32le.raw", legacyPcm);
    require(folder.getChildFile("result.txt")
                .replaceWithText(
                    "pass=" + juce::String(int(passed)) + ";frames=" + juce::String(clock) +
                    ";error=" + failure +
                    "\nDirect real-worker functional processing, not native device/hardware/listening.\n"),
            "Terminal receipt retained");
    require(passed, "Original real-worker emitted segment regression failed; keep raw evidence");
}

// CONTROL-only evidence from an already-returned process call. Receipt storage
// is caller-owned; this helper never runs inside a DSP/heap/timing probe.
inline void retainPendingObservation(const juce::File &folder, const char *label, PluginHost &host,
                                     const juce::AudioBuffer<float> &actual,
                                     const juce::AudioBuffer<float> &expected,
                                     const PluginPcmCapture &capture, int offset, int count,
                                     bool preparedInstrument, const juce::String &context = {})
{
    const auto write = [&](const juce::String &suffix, const juce::AudioBuffer<float> &pcm)
    {
        const auto file = folder.getChildFile(juce::String(label) + suffix);
        require(!file.exists(), "Per-call evidence must not append or overwrite an earlier attempt");
        auto stream = file.createOutputStream();
        require(bool(stream), "Create owned pending receipt PCM evidence");
        for (int c = 0; c < pcm.getNumChannels(); ++c)
            require(stream->write(pcm.getReadPointer(c), std::size_t(pcm.getNumSamples()) * sizeof(float)),
                    "Retain entire original planar float PCM including out-of-span sentinels");
    };
    write("-actual-planar-f32le.raw", actual);
    write("-expected-planar-f32le.raw", expected);
    const auto statistics = host.statistics();
    auto facts = std::make_unique<juce::DynamicObject>();
    facts->setProperty("phase", label);
    facts->setProperty("processOffset", offset);
    facts->setProperty("processCount", count);
    facts->setProperty("channels", actual.getNumChannels());
    facts->setProperty("framesPerChannel", actual.getNumSamples());
    facts->setProperty("expectedChannels", expected.getNumChannels());
    facts->setProperty("expectedFramesPerChannel", expected.getNumSamples());
    facts->setProperty("preparedInstrument", preparedInstrument); // Caller contract; not inferred from mix.
    facts->setProperty("context", context);
    facts->setProperty("runtimeInstance", host.runtimeIdentity());
    facts->setProperty("workerPid", juce::int64(host.processId()));
    facts->setProperty("hostAddress", juce::int64(reinterpret_cast<std::uintptr_t>(&host)));
    facts->setProperty("receiptHostAddress", juce::int64(capture.identity.host));
    facts->setProperty("transportAddress", juce::int64(capture.identity.preparedTransport));
    facts->setProperty("identityMatchesOwner", sameUuid(capture.identity, host.runtimeIdentity()));
    facts->setProperty("quantum", capture.identity.quantum);
    facts->setProperty("transportSamples", capture.identity.transportSamples);
    facts->setProperty("status", int(capture.status));
    facts->setProperty("segmentCount", juce::int64(capture.count));
    facts->setProperty("segmentCapacity", juce::int64(capture.storage.size()));
    facts->setProperty("requiredSegments", juce::int64(capture.required));
    facts->setProperty("packetFrame", host.packetFrame());
    facts->setProperty("submitted", juce::int64(statistics.submitted));
    facts->setProperty("processed", juce::int64(statistics.processed));
    facts->setProperty("parametersApplied", juce::int64(statistics.parametersApplied));
    facts->setProperty("parametersRejected", juce::int64(statistics.parametersRejected));
    facts->setProperty("midiOverflow", juce::int64(statistics.midiOverflow));
    facts->setProperty("midiResetGeneration", int(host.midiResetGeneration()));
    const auto bypass = host.bypassBlend(), wet = host.wetBlend();
    facts->setProperty("bypassBlend", bypass ? juce::var(*bypass) : juce::var{});
    facts->setProperty("wetBlend", wet ? juce::var(*wet) : juce::var{});
    juce::Array<juce::var> segments;
    // Retain corrupted count as a fact while bounding diagnostic reads to actual
    // storage; do not let a failed count assertion erase the rest of its evidence.
    for (std::size_t i = 0; i < std::min(capture.count, capture.storage.size()); ++i)
    {
        const auto &s = capture.storage[i];
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty("offset", s.offset);
        row->setProperty("count", s.count);
        row->setProperty("packetFrame", s.packetFrame);
        row->setProperty("outputPacket", juce::int64(s.outputPacket));
        row->setProperty("packet", int(s.packet));
        row->setProperty("consumed", bool(s.consumed));
        if (s.consumed)
        {
            row->setProperty("consumedSequence", juce::int64(s.consumed->sequence));
            row->setProperty("nativeSamples", s.consumed->stamp.nativeSamples);
            row->setProperty("nativeGeneration", int(s.consumed->stamp.generation));
        }
        row->setProperty("dryDelaySamples", s.dryDelaySamples);
        row->setProperty("forcedDry", s.forcedDry);
        row->setProperty("mix", int(s.mix));
        row->setProperty("firstWet", s.firstWet);
        row->setProperty("lastWet", s.lastWet);
        row->setProperty("firstBypass", s.firstBypass);
        row->setProperty("lastBypass", s.lastBypass);
        segments.add(juce::var(row.release()));
    }
    facts->setProperty("segments", segments);
    require(folder.getChildFile(juce::String(label) + "-facts.json")
                .replaceWithText(juce::JSON::toString(juce::var(facts.release()), true)),
            "Retain per-call receipt/owner/cursor/readback facts before their assertions");
}

__declspec(noinline) inline void pendingRefusalCase(const juce::File &folder, const PluginSpec &spec)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh pending original instrument evidence");
    PluginPcmIdentity copiedIdentity;
    PluginPcmSegment copiedSegment;
    {
        PluginHost host;
        require(host.start(spec, rate, quantum, true).wasOk(), "Actual original SDK instrument worker");
        const auto catalogue = parameterCatalogue(host);
        const auto reset = host.midiResetGeneration();
        juce::AudioBuffer<float> audio(2, 260), expected(2, 260);
        audio.clear();
        expected.clear();
        std::array<PluginPcmSegment, 2> entries{};
        PluginPcmCapture capture(entries);
        ipc::MidiEvent on{0, {0x90, 60, 127}, 3};
        const ipc::ParameterEvent gain{77, 0, .25f};
        host.process(audio, 0, 255, &on, 1, 0, 120, true, false, 4, 4, &gain, 1, catalogue.generation, true,
                     nullptr, 0, 1, &capture);
        retainPendingObservation(folder, "01_pending_prefix", host, audio, expected, capture, 0, 255, true,
                                 "Expected startup silence; one pending on/gain; cursor255; submitted0");
        require(capture.status == PluginPcmReceiptStatus::complete && capture.count == 1 &&
                    host.packetFrame() == 255 && host.statistics().submitted == 0 &&
                    host.statistics().parametersApplied == 0,
                "Actual pending MIDI/native parameter prefix remains unsubmitted");
        const auto before = host.statistics();
        const auto bypass = host.bypassBlend(), wet = host.wetBlend();
        std::array<PluginPcmSegment, 1> tooSmall{};
        PluginPcmCapture refused(tooSmall);
        ipc::MidiEvent refusedOff{0, {0x80, 60, 0}, 3};
        const ipc::ParameterEvent refusedGain{77, 0, .75f};
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), audio.getNumSamples(), .625f);
        host.process(audio, 3, 65, &refusedOff, 1, 255. / rate, 120, true, true, 4, 4, &refusedGain, 1,
                     catalogue.generation, true, nullptr, 0, .2f, &refused);
        const auto after = host.statistics();
        for (int c = 0; c < 2; ++c)
            std::fill_n(expected.getWritePointer(c), expected.getNumSamples(), .625f);
        retainPendingObservation(folder, "02_capacity_refusal", host, audio, expected, refused, 3, 65, true,
                                 "Expected capacityExceeded/count0/required2; unchanged cursor255, original "
                                 "on/gain prefix and blend");
        bool intact = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < audio.getNumSamples(); ++n)
                intact &= audio.getSample(c, n) == .625f;
        require(refused.status == PluginPcmReceiptStatus::capacityExceeded && refused.count == 0 &&
                    refused.required == 2 && intact && host.packetFrame() == 255 &&
                    before.submitted == after.submitted &&
                    before.parametersApplied == after.parametersApplied &&
                    before.parametersRejected == after.parametersRejected &&
                    before.midiOverflow == after.midiOverflow && host.bypassBlend() == bypass &&
                    host.wetBlend() == wet && host.midiResetGeneration() == reset,
                "Typed insufficient capture refuses before audio/pending events/ramps/cursor mutation");
        audio.clear();
        host.process(audio, 0, 65, nullptr, 0, 255. / rate, 120, true, false, 4, 4, nullptr, 0, 0, true,
                     nullptr, 0, 1, &capture);
        expected.clear();
        for (int c = 0; c < 2; ++c)
            for (int n = 1; n < 65; ++n)
                expected.setSample(c, n,
                                   c == 0 ? float(.25 * (61. / 512 + 1. / 4096))
                                          : float(-.25 * (68. / 512 + 1. / 8192)));
        retainPendingObservation(
            folder, "03_retry", host, audio, expected, capture, 0, 65, true,
            "Expected startup suffix1 then consumed packet0 span64; literal signed quarter-gain held PCM");
        require(capture.status == PluginPcmReceiptStatus::complete && capture.count == 2 &&
                    capture.storage[0].offset == 0 && capture.storage[0].count == 1 &&
                    capture.storage[0].packetFrame == 255 && !capture.storage[0].consumed &&
                    capture.storage[1].offset == 1 && capture.storage[1].count == 64 &&
                    capture.storage[1].consumed && capture.storage[1].consumed->sequence == 0 &&
                    capture.storage[1].consumed->stamp == PluginLatencyStamp{0, 1},
                "Retry emits exact startup suffix then genuine consumed packet0, not an accepted request");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 65; ++n)
            {
                const float wanted = n == 0   ? 0
                                     : c == 0 ? float(.25 * (61. / 512 + 1. / 4096))
                                              : float(-.25 * (68. / 512 + 1. / 8192));
                require(std::abs(audio.getSample(c, n) - wanted) <= 2e-6,
                        "Refused off/gain never changed native held note or independent signed PCM");
            }
        waitForSubmitted(host);
        const auto counters = parameterCatalogue(host, true);
        retainPendingObservation(folder, "04_native_counters", host, audio, expected, capture, 0, 65, true,
                                 "Native on=" + juce::String(nativeParameter(counters, 90).value, 12) +
                                     ";off=" + juce::String(nativeParameter(counters, 91).value, 12) +
                                     ";expected on=1/1024/off0/applied1");
        require(nativeParameter(counters, 90).value == 1.f / 1024 &&
                    nativeParameter(counters, 91).value == 0 && host.statistics().parametersApplied == 1,
                "Actual SDK exactly one on, zero off and one original pending gain event");
        copiedIdentity = capture.identity;
        copiedSegment = capture.storage[1];
        host.beginRetirement(); // Explicit failure-flag/retirement, NOT worker crash injection.
        audio.setSample(0, 0, .625f);
        audio.setSample(1, 0, -.625f);
        host.process(audio, 0, 1, nullptr, 0, 320. / rate, 120, true, false, 4, 4, nullptr, 0, 0, true,
                     nullptr, 0, 1, &capture);
        expected.setSample(0, 0, 0);
        expected.setSample(1, 0, 0);
        retainPendingObservation(
            folder, "05_retired_forced_dry", host, audio, expected, capture, 0, 1, true,
            "Expected completed cached backing packet0 but forcedDry delayedDry instrument SILENCE, bypass1");
        require(capture.status == PluginPcmReceiptStatus::complete && capture.count == 1 &&
                    capture.storage[0].packet == PluginPcmPacketKind::completed &&
                    capture.storage[0].consumed && capture.storage[0].consumed->sequence == 0 &&
                    capture.storage[0].forcedDry && capture.storage[0].mix == PluginPcmMixKind::delayedDry &&
                    capture.storage[0].firstBypass == 1 && capture.storage[0].lastBypass == 1 &&
                    audio.getSample(0, 0) == 0 && audio.getSample(1, 0) == 0,
                "Retirement forced-dry synth output distinguished from cached completed wet packet");
        // Capture admission failures do not invent a completion receipt or clear
        // prior caller-owned storage. Count/status determine the accessible result.
        host.process(audio, 0, -1, nullptr, 0, 0, 120, true, false, 4, 4, nullptr, 0, 0, true, nullptr, 0, 1,
                     &capture);
        retainPendingObservation(folder, "06_invalid_span", host, audio, expected, capture, 0, -1, true,
                                 "Expected invalidSpan/count0, previous caller PCM untouched");
        require(capture.status == PluginPcmReceiptStatus::invalidSpan && capture.count == 0,
                "Invalid span has explicit noncompletion, no stale receipt view");
    }
    // Value-only receipt has no Host/plan ownership, pointers are opaque tokens.
    // No method or dereference is permitted after original owner destruction.
    PluginHost successor;
    require(successor.start(spec, rate, quantum, true).wasOk(), "New actual worker lifetime");
    juce::AudioBuffer<float> silence(2, 1), expected(2, 1);
    silence.clear();
    expected.clear();
    std::array<PluginPcmSegment, 2> entries{};
    PluginPcmCapture fresh(entries);
    successor.process(silence, 0, 1, nullptr, 0, 0, 120, true, false, 4, 4, nullptr, 0, 0, true, nullptr, 0,
                      1, &fresh);
    retainPendingObservation(
        folder, "07_new_worker_lifetime", successor, silence, expected, fresh, 0, 1, true,
        "Copied old UUID distinct=" + juce::String(int(copiedIdentity.instance != fresh.identity.instance)) +
            ";old count=" + juce::String(copiedSegment.count) + ";old consumed=" +
            juce::String(copiedSegment.consumed ? juce::int64(copiedSegment.consumed->sequence) : -1));
    require(copiedIdentity.instance != fresh.identity.instance && copiedSegment.consumed &&
                copiedSegment.consumed->sequence == 0 && copiedSegment.count == 64,
            "Copied immutable old receipt survives destruction without stale ownership or identity alias");
    const auto cursor = successor.packetFrame();
    const auto submitted = successor.statistics().submitted;
    const auto wet = successor.wetBlend(), bypass = successor.bypassBlend();
    silence.setSample(0, 0, .625f);
    silence.setSample(1, 0, -.625f);
    successor.process(silence, 0, 1, nullptr, 0, 0, 120, true, true, 4, 4, nullptr, 0, 0, true, nullptr, 0,
                      std::numeric_limits<float>::quiet_NaN(), &fresh);
    expected.setSample(0, 0, .625f);
    expected.setSample(1, 0, -.625f);
    retainPendingObservation(
        folder, "08_invalid_mix", successor, silence, expected, fresh, 0, 1, true,
        "Expected invalidMix/count0; unchanged native cursor/submission and prior blends");
    require(fresh.status == PluginPcmReceiptStatus::invalidMix && fresh.count == 0 &&
                successor.packetFrame() == cursor && successor.statistics().submitted == submitted &&
                successor.wetBlend() == wet && successor.bypassBlend() == bypass &&
                silence.getSample(0, 0) == .625f && silence.getSample(1, 0) == -.625f,
            "Invalid capture call mix refuses atomically without stale completion/readback");
    PluginHost unstarted;
    unstarted.process(silence, 0, 1, nullptr, 0, 0, 120, true, false, 4, 4, nullptr, 0, 0, true, nullptr, 0,
                      1, &fresh);
    retainPendingObservation(
        folder, "09_unstarted", unstarted, silence, expected, fresh, 0, 1, false,
        "Expected workerUnavailable/count0/transport0, no prepared instrument on unstarted host");
    require(fresh.status == PluginPcmReceiptStatus::workerUnavailable && fresh.count == 0 &&
                fresh.identity.transportSamples == 0 && unstarted.packetFrame() == 0 &&
                unstarted.statistics().submitted == 0,
            "Unstarted Host cannot invent emitted completed wet PCM");
    require(
        folder.getChildFile("result.txt")
            .replaceWithText("pending MIDI/parameter capture refusal and copied runtime lifetime: pass\n"),
        "Original pending/failure/lifetime result");
}
} // namespace plugin_pcm_receipts

static void pluginPcmReceiptTests(const juce::File &folder, const juce::File &latencyBinary,
                                  const juce::File &instrumentBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh scoped receipt suite");
    const auto latency = parameterSpec(latencyBinary), instrument = parameterSpec(instrumentBinary);
    require(!latency.instrument && latency.name == "VibeRa Mixer Latency Fixture" && instrument.instrument &&
                instrument.name == "VibeRa Instrument Runtime Fixture",
            "Exact original genuine SDK fixtures, no arbitrary native DSP claim");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), latencyBinary, instrumentBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities), "Exact input identity");
    std::exception_ptr firstFailure;
    juce::Array<juce::var> outcomes;
    const auto run = [&](const juce::String &name, auto &&body)
    {
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty("case", name);
        try
        {
            body();
            row->setProperty("passed", true);
        }
        catch (const std::exception &error)
        {
            row->setProperty("passed", false);
            row->setProperty("error", error.what());
            if (!firstFailure)
                firstFailure = std::current_exception();
        }
        catch (...)
        {
            row->setProperty("passed", false);
            row->setProperty("error", "Unknown original receipt test exception");
            if (!firstFailure)
                firstFailure = std::current_exception();
        }
        outcomes.add(juce::var(row.release()));
    };
    for (const auto policy :
         {PluginHost::TransportPolicy::oneQuantum, PluginHost::TransportPolicy::twoQuanta})
        for (const bool offline : {false, true})
        {
            const auto name =
                juce::String(policy == PluginHost::TransportPolicy::oneQuantum ? "default1Q" : "explicit2Q") +
                (offline ? "_offline" : "_live");
            run(name, [&]
                { plugin_pcm_receipts::latencyCase(folder.getChildFile(name), latency, policy, offline); });
        }
    run("pending_refusal_lifetime",
        [&]
        {
            plugin_pcm_receipts::pendingRefusalCase(folder.getChildFile("pending_refusal_lifetime"),
                                                    instrument);
        });
    // Receipt failure remains a suite failure; preserve the first musical/provider
    // exception if one already exists. No case is retried or skipped after failure.
    try
    {
        require(folder.getChildFile("case-outcomes.json")
                    .replaceWithText(juce::JSON::toString(juce::var(outcomes), true)),
                "Retain every independent branch outcome before propagating first failure");
    }
    catch (...)
    {
        if (!firstFailure)
            firstFailure = std::current_exception();
    }
    if (firstFailure)
        std::rethrow_exception(firstFailure);
}
