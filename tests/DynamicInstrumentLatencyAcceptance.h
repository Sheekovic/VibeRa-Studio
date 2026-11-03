// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>

// Include after ParameterAcceptance.h, CallbackAllocationProbe.h and
// CallbackTimingTests.h. Direct actual Host/SDK tests; CONTROL dispatch between
// calls is not physical-device scheduling or graph/PDC/epoch-fence qualification.
namespace dynamic_instrument_latency
{
constexpr int quantum = 256, rate = 48000, inputPackets = 24, capacity = inputPackets * quantum;
inline int delayForPacket(int packet)
{
    return packet < 4 ? 0 : packet < 8 ? 31 : packet < 14 ? 257 : 0;
}
inline std::uint32_t generationForPacket(int packet)
{
    return packet < 4 ? 1 : packet < 8 ? 2 : packet < 14 ? 3 : 4;
}
// Independent literal musical schedule, never derived from mutable fixture state,
// actual receipt phases, observed PCM, worker time or a fitted startup boundary.
inline float expectedSource(int channel, int frame, bool restored)
{
    if (frame < 0)
        return 0;
    int weight = 0;
    if (restored)
    {
        if (frame < 512)
            weight = channel == 0 ? 65 : 64; // Fresh pitch64 key, velocity127.
    }
    else
    {
        if (frame < 4096)
            weight += channel == 0 ? 61 : 68; // Held pitch60.
        if (frame >= 512 && frame < 1792)
            weight += channel == 0 ? 73 : 56; // Score pitch72.
    }
    const int numerator = channel == 0 ? (frame * 17 + 5) % 251 - 125 : -((frame * 23 + 11) % 241 - 120);
    // All values are binary rational: gain1/4, integer weights/512 and ramp/128.
    // Products stay below2^24, so float output is exactly representable here.
    return float(weight * numerator) / 262144.f;
}
inline void waitProcessed(PluginHost &host)
{
    const auto target = host.statistics().submitted;
    const auto until = juce::Time::getMillisecondCounter() + 10000;
    while (host.statistics().processed < target && juce::Time::getMillisecondCounter() < until)
        dispatchMessages(1); // CONTROL only; never inside the measured Host call.
    require(host.statistics().processed == target && host.alive(), "Real worker completes submitted prefix");
}
inline juce::String state(PluginHost &host, unsigned *capturedRevision = nullptr)
{
    const auto serial = host.requestState();
    require(serial != 0, "Actual callback-excluded native state capture admitted");
    juce::String bytes;
    const auto until = juce::Time::getMillisecondCounter() + 10000;
    unsigned revision = 0;
    while (!host.readState(serial, bytes, &revision) && juce::Time::getMillisecondCounter() < until)
        dispatchMessages(1);
    require(bytes.isNotEmpty(), "Matching actual native state capture completed");
    if (capturedRevision)
        *capturedRevision = revision;
    return bytes;
}
inline void rawStereo(const juce::File &file, const juce::AudioBuffer<float> &pcm, int frames)
{
    juce::FileOutputStream out(file);
    require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk(), "Fresh raw PCM stream");
    for (int channel = 0; channel < 2; ++channel)
        require(out.write(pcm.getReadPointer(channel), std::size_t(frames) * sizeof(float)),
                "Retain planar little-endian stereo float PCM");
    out.flush();
    require(out.getStatus().wasOk(), "Raw PCM flush/status before musical assertions");
}
inline void versionedState(const juce::String &encoded)
{
    juce::MemoryBlock bytes;
    require(bytes.fromBase64Encoding(encoded) &&
                bytes.getSize() <= std::size_t(std::numeric_limits<int>::max()),
            "Actual Host opaque-state encoding");
    const auto xml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), int(bytes.getSize()));
    require(xml && xml->hasTagName("VST3PluginState"), "Actual pinned JUCE VST3 binary-XML state envelope");
    // DYN1/revision1/mode2/reserved0/gain.25, all explicitly little-endian.
    constexpr std::array<std::uint8_t, 24> native{
        {0x44, 0x59, 0x4e, 0x31, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xd0, 0x3f}};
    // appendStateFrom encodes each SDK stream independently inside this XML.
    // Require the complete exact payload in BOTH actual combined interfaces;
    // substring search in the outer binary XML cannot inspect native bytes.
    for (const auto *identifier : {"IComponent", "IEditController"})
    {
        int matches = 0;
        for (auto *child = xml->getFirstChildElement(); child; child = child->getNextElement())
            if (child->hasTagName(identifier))
                ++matches;
        const auto *child = xml->getChildByName(identifier);
        juce::MemoryBlock nativeBytes;
        require(matches == 1 && child && nativeBytes.fromBase64Encoding(child->getAllSubText()) &&
                    nativeBytes.getSize() == native.size() &&
                    std::equal(native.begin(), native.end(),
                               static_cast<const std::uint8_t *>(nativeBytes.getData())),
                std::string("Exact original SDK DYN1/revision1/mode257/gain.25 state: ") + identifier);
    }
}
// Each call owns one actual worker; scope unwinding contains failures using the
// established Host destructor. No worker/Host escapes into other independent cases.
__declspec(noinline) inline juce::String exercise(const juce::File &folder, PluginSpec spec,
                                                  PluginHost::TransportPolicy policy, bool offline,
                                                  bool restored = false)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh case preserves all failed attempts");
    PluginHost host(policy);
    const auto start = host.start(spec, rate, quantum, offline);
    require(start.wasOk(), "Genuine SDK dynamic instrument start: " + start.getErrorMessage().toStdString());
    auto metadata = parameterCatalogue(host);
    if (!restored)
        require(parameterResult(host, host.setParameter(77, .25f, metadata.generation)).result == 0,
                "Actual supported native gain write before first musical packet");
    metadata = parameterCatalogue(host, true);
    const auto initialStamp = PluginLatencyStamp{restored ? 257 : 0, 1};
    const int transport = policy == PluginHost::TransportPolicy::oneQuantum ? 256 : 512;
    require(host.declaredLatency() == initialStamp && !host.consumedLatency() &&
                host.transportLatencySamples() == transport &&
                host.latencySamples() == transport + initialStamp.nativeSamples &&
                host.processingQuantum() == 256 && nativeParameter(metadata, 77).value == .25f &&
                nativeParameter(metadata, 17).writable() && !nativeParameter(metadata, 17).automatable() &&
                nativeParameter(metadata, 17).steps == 3,
            "True instrument layout and supported native-latency control; no invented consumed startup");
    const auto pid = host.processId();
    const auto instance = host.runtimeIdentity();
    const auto reset = host.midiResetGeneration();
    const auto initialStatistics = host.statistics();
    const juce::Uuid uuid(instance);
    juce::AudioBuffer<float> audio(2, quantum + 12), actual(2, capacity), expected(2, capacity);
    actual.clear();
    expected.clear();
    std::array<PluginPcmSegment, 2> segments{};
    PluginPcmCapture capture(segments);
    juce::String trace =
        "frame,frames,offset,count,packetFrame,outputPacket,kind,consumedSequence,native,generation,"
        "dryDelay,declaredNative,declaredGeneration,maxError,hostMs,new,delete,submitted,processed,missed,"
        "dropped\n";
    juce::String capturedState;
    if (restored)
    {
        const auto captured = state(host);
        require(folder.getChildFile("restored-before-processing.state").replaceWithText(captured),
                "Retain actual restored opaque state before equality assertion");
        require(captured == spec.state,
                "Revisioned gain/native mode survive real new-worker restore exactly");
        versionedState(captured);
    }
    int clock = 0, calls = 0, mode = restored ? 257 : 0;
    constexpr std::array<int, 4> partial{127, 64, 64, 65};
    const auto retain = [&]
    {
        rawStereo(folder.getChildFile("actual-stereo.f32"), actual, clock);
        rawStereo(folder.getChildFile("expected-stereo.f32"), expected, clock);
        require(folder.getChildFile("segments.csv").replaceWithText(trace),
                "Retain actual receipt/timing rows");
    };
    try
    {
        const int total = restored ? 2048 : capacity;
        while (clock < total)
        {
            // Known default1Q LIVE boundary causality is preserved: full-Q calls
            // only there. Offline/default and explicit2Q use actual crossing spans.
            const int frames =
                std::min(total - clock, !offline && policy == PluginHost::TransportPolicy::oneQuantum
                                            ? 256
                                            : partial[std::size_t(calls) % 4]);
            if ((clock + frames) / quantum > clock / quantum && !restored)
            {
                waitProcessed(host);
                const int packet = (clock + frames) / quantum - 1;
                const int nextMode = delayForPacket(packet);
                if (nextMode != mode)
                {
                    metadata = parameterCatalogue(host, true);
                    const float normalized = nextMode == 31 ? 1.f / 3 : nextMode == 257 ? 2.f / 3 : 0.f;
                    require(parameterResult(host, host.setParameter(17, normalized, metadata.generation))
                                    .result == 0,
                            "Real same-worker supported latency notification, CONTROL only");
                    mode = nextMode;
                }
            }
            std::array<ipc::MidiEvent, 4> midi{};
            int midiCount = 0;
            const auto event = [&](int at, int status, int pitch)
            {
                if (at >= clock && at < clock + frames)
                    midi[std::size_t(midiCount++)] = {
                        at - clock,
                        {std::uint8_t(status), std::uint8_t(pitch), std::uint8_t(status == 0x90 ? 127 : 0)},
                        3};
            };
            if (restored)
            {
                event(0, 0x90, 64);
                event(512, 0x80, 64);
            }
            else
            {
                event(0, 0x90, 60);
                event(512, 0x90, 72);
                event(1792, 0x80, 72);
                event(4096, 0x80, 60);
            }
            for (int c = 0; c < 2; ++c)
                std::fill_n(audio.getWritePointer(c), audio.getNumSamples(),
                            .875f); // Instruments must ignore audio input.
            const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
            const auto began = juce::Time::getHighResolutionTicks();
            {
                struct Probe
                {
                    bool before = callbackAllocationProbe;
                    explicit Probe(bool live)
                    {
                        callbackAllocationProbe = live;
                    }
                    ~Probe()
                    {
                        callbackAllocationProbe = before;
                    }
                } probe(!offline);
                host.process(audio, 7, frames, midi.data(), midiCount, clock / double(rate), 120, true, false,
                             4, 4, nullptr, 0, 0, true, nullptr, 0, 1, &capture);
            }
            const auto ms =
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began) *
                1000.;
            const auto allocations = callbackAllocations.load() - allocated;
            const auto destructions = callbackDestructions.load() - destroyed;
            bool ok = capture.status == PluginPcmReceiptStatus::complete &&
                      capture.count == std::size_t(1 + ((clock % quantum) + frames - 1) / quantum) &&
                      capture.required == capture.count &&
                      capture.identity.host == reinterpret_cast<std::uintptr_t>(&host) &&
                      capture.identity.preparedTransport == 0 && capture.identity.quantum == 256 &&
                      capture.identity.transportSamples == transport &&
                      std::equal(capture.identity.instance.begin(), capture.identity.instance.end(),
                                 uuid.getRawData());
            double maximumError = 0;
            for (int n = 0; n < frames; ++n)
            {
                const int outPacket = (clock + n) / 256 - transport / 256;
                const int native = restored ? 257 : delayForPacket(outPacket);
                for (int c = 0; c < 2; ++c)
                {
                    const float wanted =
                        outPacket < 0 ? 0.f : expectedSource(c, clock + n - transport - native, restored);
                    const float observed = audio.getSample(c, 7 + n);
                    actual.setSample(c, clock + n, observed);
                    expected.setSample(c, clock + n, wanted);
                    ok &= observed == wanted; // Exact rational PCM, including every startup/tail sample.
                    maximumError = std::isfinite(observed)
                                       ? std::max(maximumError, std::abs(double(observed) - wanted))
                                       : std::numeric_limits<double>::infinity();
                }
            }
            const auto observedDeclaration = host.declaredLatency();
            const auto statistics = host.statistics();
            int done = 0;
            for (const auto &segment : capture.segments())
            {
                const int outputPacket = (clock + done) / 256, inputPacket = outputPacket - transport / 256;
                const int native = restored ? 257 : delayForPacket(inputPacket);
                const auto generation = restored ? 1u : generationForPacket(inputPacket);
                const int count = std::min(frames - done, 256 - (clock + done) % 256);
                ok &= segment.offset == done && segment.count == count &&
                      segment.packetFrame == (clock + done) % 256 &&
                      segment.outputPacket == std::uint64_t(outputPacket) && !segment.forcedDry &&
                      segment.mix == PluginPcmMixKind::wet && segment.firstWet == 1 && segment.lastWet == 1 &&
                      segment.firstBypass == 0 && segment.lastBypass == 0 &&
                      segment.dryDelaySamples ==
                          transport + (inputPacket < 0 ? initialStamp.nativeSamples : native);
                if (inputPacket < 0)
                    ok &= segment.packet == PluginPcmPacketKind::startup && !segment.consumed;
                else
                    ok &= segment.packet == PluginPcmPacketKind::completed && segment.consumed &&
                          segment.consumed->sequence == std::uint64_t(inputPacket) &&
                          segment.consumed->stamp == PluginLatencyStamp{native, generation};
                trace += juce::String(clock) + "," + juce::String(frames) + "," +
                         juce::String(segment.offset) + "," + juce::String(segment.count) + "," +
                         juce::String(segment.packetFrame) + "," +
                         juce::String(juce::int64(segment.outputPacket)) + "," +
                         juce::String(int(segment.packet)) + "," +
                         juce::String(segment.consumed ? juce::int64(segment.consumed->sequence) : -1) + "," +
                         juce::String(segment.consumed ? segment.consumed->stamp.nativeSamples : -1) + "," +
                         juce::String(segment.consumed ? segment.consumed->stamp.generation : 0) + "," +
                         juce::String(segment.dryDelaySamples) + "," +
                         juce::String(observedDeclaration.nativeSamples) + "," +
                         juce::String(observedDeclaration.generation) + "," + juce::String(maximumError, 12) +
                         "," + juce::String(ms, 7) + "," + juce::String(allocations) + "," +
                         juce::String(destructions) + "," + juce::String(juce::int64(statistics.submitted)) +
                         "," + juce::String(juce::int64(statistics.processed)) + "," +
                         juce::String(juce::int64(statistics.missed)) + "," +
                         juce::String(juce::int64(statistics.dropped)) + "\n";
                done += count;
            }
            ok &= done == frames && host.packetFrame() == (clock + frames) % 256 && host.processId() == pid &&
                  host.runtimeIdentity() == instance && host.midiResetGeneration() == reset &&
                  statistics.missed == 0 && statistics.dropped == 0 && statistics.midiOverflow == 0 &&
                  statistics.parametersRejected == 0 && !host.deliveryFailed();
            for (int c = 0; c < 2; ++c)
                ok &= audio.getSample(c, 0) == .875f && audio.getSample(c, 7 + frames) == .875f;
            if (!offline)
                ok &= allocations == 0 && destructions == 0 && ms <= frames * 1000. / rate;
            clock += frames;
            ++calls;
            retain(); // Actual PCM/receipt/clock/timing before any combined assertion.
            require(ok, "Exact independent instrument PCM/packet epoch/owner/noheap/deadline contract");
            waitProcessed(host);
            const int latestInput = clock / 256 - 1;
            const auto expectedDeclaration =
                PluginLatencyStamp{restored ? 257 : delayForPacket(latestInput),
                                   restored ? 1u : generationForPacket(latestInput)};
            if (latestInput >= 0)
            {
                const auto declaration = host.declaredLatency();
                require(folder.getChildFile("latest-declaration.txt")
                            .replaceWithText("completedInputPacket=" + juce::String(latestInput) +
                                             " native=" + juce::String(declaration.nativeSamples) +
                                             " generation=" + juce::String(declaration.generation) + "\n"),
                        "Retain actual declaration before assertion");
                require(declaration == expectedDeclaration &&
                            host.latencySamples() == transport + expectedDeclaration.nativeSamples,
                        "Same actual worker declaration advances1/2/3/4 across0-to31-to257-to0");
            }
            if (!restored && capturedState.isEmpty() && latestInput == 9)
            {
                const auto before = host.statistics();
                const auto cursorBefore = host.packetFrame();
                unsigned capturedRevision = 0;
                capturedState = state(host, &capturedRevision);
                require(folder.getChildFile("held-mode257.state").replaceWithText(capturedState),
                        "Retain actual held native capture");
                require(folder.getChildFile("held-capture-revision.txt")
                            .replaceWithText("captured=" + juce::String(capturedRevision) +
                                             " current=" + juce::String(host.parameterRevision()) + "\n"),
                        "Retain actual captured parameter revision before consistency assertion");
                versionedState(capturedState);
                const auto values = parameterCatalogue(host, true);
                require(nativeParameter(values, 90).value == 2.f / 1024 &&
                            nativeParameter(values, 91).value == 1.f / 1024 &&
                            nativeParameter(values, 92).value == 1.f / 1024 &&
                            host.statistics().submitted == before.submitted &&
                            host.statistics().processed == before.processed &&
                            host.packetFrame() == cursorBefore && host.processId() == pid &&
                            host.runtimeIdentity() == instance &&
                            capturedRevision == host.parameterRevision(),
                        "Capture preserves real held ownership/cursor/PID; next literal PCM detects any "
                        "source reset");
            }
        }
        const auto counters = parameterCatalogue(host, true);
        const int noteCount = restored ? 1 : 2;
        const auto final = host.statistics();
        const auto summary = "instance=" + instance + " pid=" + juce::String(pid) +
                             " frames=" + juce::String(clock) +
                             " nativeOns=" + juce::String(nativeParameter(counters, 90).value * 1024) +
                             " nativeOffs=" + juce::String(nativeParameter(counters, 91).value * 1024) +
                             " nativeHeld=" + juce::String(nativeParameter(counters, 92).value * 1024) +
                             " submitted=" + juce::String(juce::int64(final.submitted)) +
                             " processed=" + juce::String(juce::int64(final.processed)) + "\n";
        require(folder.getChildFile("summary.txt").replaceWithText(summary),
                "Retain actual native counters before final assertion");
        require(nativeParameter(counters, 90).value == noteCount / 1024.f &&
                    nativeParameter(counters, 91).value == noteCount / 1024.f &&
                    nativeParameter(counters, 92).value == 0 &&
                    final.submitted == initialStatistics.submitted + std::uint64_t(clock / 256) &&
                    final.processed == final.submitted && (!capturedState.isEmpty() || restored),
                "Exact native on/off ownership and full tail extent, capture genuinely reached");
        return capturedState;
    }
    catch (...)
    {
        const auto first = std::current_exception();
        try
        {
            retain();
            require(folder.getChildFile("worker-error.txt").replaceWithText(host.error()),
                    "Retain worker error");
        }
        catch (...)
        {
        } // Preserve original failed musical/provider assertion.
        std::rethrow_exception(first);
    }
}
} // namespace dynamic_instrument_latency

__declspec(noinline) static void dynamicInstrumentLatencyTests(const juce::File &folder,
                                                               const juce::File &binary)
{
    require(juce::File::isAbsolutePath(binary.getFullPathName()) && binary.existsAsFile() &&
                !folder.exists() && folder.createDirectory().wasOk(),
            "Exact original binary and fresh owned suite evidence");
    std::vector<juce::PluginDescription> descriptions;
    const auto scan =
        PluginCatalog::scanOne(binary, descriptions); // One explicit file; no broad/standard-location scan.
    require(scan.wasOk() && descriptions.size() == 1 && descriptions.front().isInstrument &&
                descriptions.front().name == "VibeRa Dynamic Instrument Fixture" &&
                descriptions.front().numInputChannels == 0 && descriptions.front().numOutputChannels == 2,
            "Actual scanned distinct SDK Instrument|Synth descriptor and genuine0-in/stereo-out");
    const auto &description = descriptions.front();
    const PluginSpec spec{1, description.name, description.createXml()->toString(), {}, true, false};
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), binary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities) &&
                folder.getChildFile("descriptor.xml").replaceWithText(spec.descriptor),
            "Retain exact app/worker/SDK binary/descriptor");
    std::exception_ptr firstFailure;
    juce::Array<juce::var> outcomes;
    for (const auto policy :
         {PluginHost::TransportPolicy::oneQuantum, PluginHost::TransportPolicy::twoQuanta})
        for (const bool offline : {false, true})
        {
            const auto name =
                juce::String(policy == PluginHost::TransportPolicy::oneQuantum ? "default1Q" : "explicit2Q") +
                (offline ? "_offline" : "_live");
            auto row = std::make_unique<juce::DynamicObject>();
            row->setProperty("case", name);
            try
            {
                auto saved = spec;
                saved.state =
                    dynamic_instrument_latency::exercise(folder.getChildFile(name), spec, policy, offline);
                dynamic_instrument_latency::exercise(folder.getChildFile(name + "_restored"), saved, policy,
                                                     offline, true);
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
                row->setProperty("error", "Unknown genuine SDK scenario exception");
                if (!firstFailure)
                    firstFailure = std::current_exception();
            }
            outcomes.add(juce::var(row.release()));
        }
    require(folder.getChildFile("case-outcomes.json")
                .replaceWithText(juce::JSON::toString(juce::var(outcomes), true)),
            "Retain every independent case outcome; no retries or skipped later scenarios");
    if (firstFailure)
        std::rethrow_exception(firstFailure);
}
