// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationStream.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <exception>
#include <memory>
#include <vector>

// Include after ParameterAcceptance.h, CallbackAllocationProbe and timing helpers.
// Genuine original ParameterFixture + InstrumentFixture workers, not mock hosts.
// Live calls have CONTROL completion dispatch BETWEEN calls; no paced/native
// graph/hardware/installed-provider/dynamic-epoch qualification is inferred.
namespace plugin_stream_pcm
{
constexpr int rate = 48000, quantum = 256;
inline float source(int channel, int frame) noexcept
{
    return float(((frame * (channel ? 19 : 13) + 7) % 127) - 63) / 512.f;
}
inline void completed(PluginHost &host)
{
    const auto wanted = host.statistics().submitted;
    const auto end = juce::Time::getMillisecondCounter() + 10000;
    while (host.statistics().processed < wanted && juce::Time::getMillisecondCounter() < end)
        dispatchMessages(1); // CONTROL only, never within a processing probe.
    require(host.statistics().processed == wanted, "Actual SDK exact submitted prefix completed");
}
inline LiveDocument document(PluginHost &host, Id id, unsigned catalogue, bool instrument, bool automate,
                             float gain = .25f)
{
    LiveDocument result;
    if (automate)
    {
        AutomationDocument values;
        values.initialValues = {{{AutomationParameter::pluginNormalized, id, instrument ? 77u : 0u}, gain}};
        if (!instrument)
            values.initialValues.push_back({{AutomationParameter::pluginNormalized, id, 0xfedcba98u}, .125f});
        std::string error;
        require(AutomationProgram::compile(values, true, result.automation, error),
                "Original exact constant native program");
        result.nativeAutomation = {{id, catalogue, host.runtimeIdentity()}};
    }
    return result;
}
inline void raw(const juce::File &file, const std::vector<float> &values)
{
    require(!file.exists(), "Fresh owned exact PCM output");
    auto stream = file.createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk() &&
                stream->write(values.data(), values.size() * sizeof(float)),
            "Exact full-span raw PCM evidence");
    stream->flush();
    require(stream->getStatus().wasOk(), "Flush exact raw PCM evidence");
}
inline bool unchanged(const PluginHost::Statistics &a, const PluginHost::Statistics &b) noexcept
{
    return a.submitted == b.submitted && a.processed == b.processed && a.missed == b.missed &&
           a.dropped == b.dropped && a.midiOverflow == b.midiOverflow &&
           a.parametersApplied == b.parametersApplied && a.parametersRejected == b.parametersRejected &&
           a.auxiliaryRejected == b.auxiliaryRejected;
}
inline bool unchanged(const PluginHost::AutomationStatistics &a,
                      const PluginHost::AutomationStatistics &b) noexcept
{
    return a.last == b.last && a.invalidTiming == b.invalidTiming &&
           a.capacityExceeded == b.capacityExceeded && a.unavailableTarget == b.unavailableTarget &&
           a.snapshotUnavailable == b.snapshotUnavailable;
}
__declspec(noinline) inline void equivalence(const juce::File &folder, const PluginSpec &spec, bool automated,
                                             bool offline)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh original Stream PCM case");
    const auto policy =
        offline ? PluginHost::TransportPolicy::oneQuantum : PluginHost::TransportPolicy::twoQuanta;
    PluginHost measured(policy), legacy(policy);
    require(measured.start(spec, rate, quantum, offline).wasOk() &&
                legacy.start(spec, rate, quantum, offline).wasOk(),
            "Separate real original SDK workers, unchanged null comparison");
    const auto catalogue = parameterCatalogue(measured), otherCatalogue = parameterCatalogue(legacy);
    auto first = document(measured, spec.id, catalogue.generation, false, automated);
    auto second = document(legacy, spec.id, otherCatalogue.generation, false, automated);
    auto captured = std::make_unique<PluginAutomationStream>(),
         plain = std::make_unique<PluginAutomationStream>();
    juce::AudioBuffer<float> audio(2, 1040), nullAudio(2, 1040);
    std::array<PluginPcmSegment, 8> entries{};
    PluginPcmCapture receipt(entries, measured.pcmIdentity());
    const auto identity = measured.pcmIdentity();
    const auto pid = measured.processId();
    const int packets = offline ? 1 : 2, transport = offline ? 256 : 512;
    require(measured.declaredLatency() == PluginLatencyStamp{0, 1} && identity.quantum == 256 &&
                identity.transportSamples == transport,
            "Original ParameterFixture native0 and literal KQ identity");
    std::vector<float> actual, expected, nullPcm;
    actual.reserve(20000);
    expected.reserve(20000);
    nullPcm.reserve(20000);
    juce::String records = "clock,frames,offset,count,packetFrame,packet,kind,consumed,sequence,native,"
                           "generation,error,ms,new,delete\n";
    juce::String calls = "clock,frames,streamStatus,nullStatus,captureStatus,count,required,identity,cursor,"
                         "error,ms,new,delete\n";
    int clock = 0;
    bool passed = true;
    const auto drive = [&](int count)
    {
        completed(measured);
        completed(legacy);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < audio.getNumSamples(); ++n)
            {
                const auto value = n >= 7 && n < 7 + count ? source(c, clock + n - 7) : .875f;
                audio.setSample(c, n, value);
                nullAudio.setSample(c, n, value);
            }
        const auto before = callbackAllocations.load(), destroyed = callbackDestructions.load();
        const auto started = juce::Time::getHighResolutionTicks();
        PluginAutomationStream::Status status;
        {
            struct Probe
            {
                bool old = callbackAllocationProbe;
                explicit Probe(bool live)
                {
                    callbackAllocationProbe = live;
                }
                ~Probe()
                {
                    callbackAllocationProbe = old;
                }
            } probe(!offline);
            status = captured->process(measured, &first, spec.id, audio, 7, count, {}, clock / double(rate),
                                       rate, 120, true, false, 4, 4, nullptr, 0, 1, &receipt);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(started, juce::Time::getHighResolutionTicks());
        const auto allocations = callbackAllocations.load() - before,
                   deletions = callbackDestructions.load() - destroyed;
        const auto nullStatus = plain->process(legacy, &second, spec.id, nullAudio, 7, count, {},
                                               clock / double(rate), rate, 120, true, false);
        bool ok = status == PluginAutomationStream::Status::ready && nullStatus == status &&
                  receipt.status == PluginPcmReceiptStatus::complete && receipt.identity == identity &&
                  receipt.count == std::size_t(1 + (std::int64_t(clock % 256) + count - 1) / 256) &&
                  receipt.required == receipt.count && measured.packetFrame() == (clock + count) % 256 &&
                  measured.processId() == pid;
        double maximum = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < count; ++n)
            {
                const int sourceFrame = clock + n - transport;
                const float wanted = sourceFrame < 0 ? 0.f
                                                     : source(c, sourceFrame) * (automated ? .25f : .5f) +
                                                           (automated ? .125f : 0.f);
                const auto value = audio.getSample(c, 7 + n);
                actual.push_back(value);
                expected.push_back(wanted);
                nullPcm.push_back(nullAudio.getSample(c, 7 + n));
                ok &= value == nullAudio.getSample(c, 7 + n);
                maximum =
                    std::isfinite(value) ? std::max(maximum, std::abs(double(value) - wanted)) : INFINITY;
            }
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < audio.getNumSamples(); ++n)
                if (n < 7 || n >= 7 + count)
                    ok &= audio.getSample(c, n) == .875f && nullAudio.getSample(c, n) == .875f;
        int done = 0;
        for (const auto &segment : receipt.storage.first(std::min(receipt.count, receipt.storage.size())))
        {
            const int absolute = clock + done, packet = absolute / 256, consumed = packet - packets;
            const int extent = std::min(count - done, 256 - absolute % 256);
            ok &= segment.offset == done && segment.count == extent &&
                  segment.packetFrame == absolute % 256 && segment.outputPacket == std::uint64_t(packet) &&
                  segment.dryDelaySamples == transport &&
                  segment.packet ==
                      (consumed < 0 ? PluginPcmPacketKind::startup : PluginPcmPacketKind::completed) &&
                  !segment.forcedDry && segment.mix == PluginPcmMixKind::wet && segment.firstWet == 1 &&
                  segment.lastWet == 1 && segment.firstBypass == 0 && segment.lastBypass == 0;
            if (consumed < 0)
                ok &= !segment.consumed;
            else
                ok &= segment.consumed && segment.consumed->sequence == std::uint64_t(consumed) &&
                      segment.consumed->stamp == PluginLatencyStamp{0, 1};
            records += juce::String(clock) + "," + juce::String(count) + "," + juce::String(segment.offset) +
                       "," + juce::String(segment.count) + "," + juce::String(segment.packetFrame) + "," +
                       juce::String(juce::int64(segment.outputPacket)) + "," +
                       juce::String(int(segment.packet)) + "," + juce::String(int(bool(segment.consumed))) +
                       "," + juce::String(segment.consumed ? juce::int64(segment.consumed->sequence) : -1) +
                       "," + juce::String(segment.consumed ? segment.consumed->stamp.nativeSamples : -1) +
                       "," + juce::String(segment.consumed ? int(segment.consumed->stamp.generation) : -1) +
                       "," + juce::String(maximum, 12) + "," + juce::String(ms, 9) + "," +
                       juce::String(allocations) + "," + juce::String(deletions) + "\n";
            done += extent;
        }
        ok &= done == count && maximum == 0 && !measured.deliveryFailed() && !legacy.deliveryFailed();
        if (!offline)
            ok &= allocations == 0 && deletions == 0 && ms <= 1000. * count / rate;
        calls += juce::String(clock) + "," + juce::String(count) + "," + juce::String(int(status)) + "," +
                 juce::String(int(nullStatus)) + "," + juce::String(int(receipt.status)) + "," +
                 juce::String(juce::int64(receipt.count)) + "," +
                 juce::String(juce::int64(receipt.required)) + "," +
                 juce::String(int(receipt.identity == identity)) + "," +
                 juce::String(measured.packetFrame()) + "," + juce::String(maximum, 12) + "," +
                 juce::String(ms, 9) + "," + juce::String(allocations) + "," + juce::String(deletions) + "\n";
        passed &= ok;
        clock += count;
    };
    // Every call crosses a different phase, including a cursor255->256 boundary.
    for (int n = 0; n < 3; ++n)
        for (int count : {127, 64, 64, 65, 256, 1, 255})
            drive(count);
    if (offline)
        drive(777); // Full-span4-or5 receipt capacity, never assume two.
    require(folder.getChildFile("segments.csv").replaceWithText(records),
            "Retain every actual emitted segment before assertions");
    require(folder.getChildFile("calls.csv").replaceWithText(calls),
            "Retain noncompletion and empty/corrupt-count observations too");
    raw(folder.getChildFile("actual-call-planar-f32.raw"), actual);
    raw(folder.getChildFile("expected-call-planar-f32.raw"), expected);
    raw(folder.getChildFile("null-call-planar-f32.raw"), nullPcm);
    require(passed, "Every Stream segment/whole PCM/null-equivalence/heap/deadline oracle passed");
}
__declspec(noinline) inline void pending(const juce::File &folder, const PluginSpec &spec)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh real pending Stream receipt case");
    PluginHost host, control;
    require(host.start(spec, rate, quantum, true).wasOk() && control.start(spec, rate, quantum, true).wasOk(),
            "Separate actual original SDK instruments");
    const auto catalog = parameterCatalogue(host), other = parameterCatalogue(control);
    auto good = document(host, spec.id, catalog.generation, true, true),
         bad = document(host, spec.id, catalog.generation, true, true, .75f);
    auto twin = document(control, spec.id, other.generation, true, true);
    auto stream = std::make_unique<PluginAutomationStream>(),
         plain = std::make_unique<PluginAutomationStream>();
    juce::AudioBuffer<float> audio(2, 270), nullAudio(2, 270);
    audio.clear();
    nullAudio.clear();
    std::array<PluginPcmSegment, 2> entries{};
    PluginPcmCapture receipt(entries, host.pcmIdentity());
    const std::array<ipc::MidiEvent, 1> on{{{0, {0x90, 60, 127}, 3}}}, off{{{0, {0x80, 60, 0}, 3}}};
    require(stream->process(host, &good, spec.id, audio, 0, 255, on, 0, rate, 120, true, false, 4, 4, nullptr,
                            0, 1, &receipt) == PluginAutomationStream::Status::ready &&
                plain->process(control, &twin, spec.id, nullAudio, 0, 255, on, 0, rate, 120, true, false) ==
                    PluginAutomationStream::Status::ready,
            "Actual pending native On +gain across255 frames");
    const auto before = host.statistics();
    const auto counters = host.automationStatistics();
    const auto reset = host.midiResetGeneration(), pid = host.processId();
    const auto wet = host.wetBlend(), bypass = host.bypassBlend();
    require(before.submitted == 0 && before.parametersApplied == 0 && host.packetFrame() == 255,
            "Real pending prefix not simulated take data");
    juce::String facts = "reason,status,required,count,cursor,submitted,applied,rejected,healthy\n";
    bool passed = true;
    const auto refuse = [&](const char *reason, PluginPcmCapture &capture, int offset, int count, float mix,
                            PluginPcmReceiptStatus expected)
    {
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), audio.getNumSamples(), .625f);
        const auto status = stream->process(host, &bad, spec.id, audio, offset, count, off, 255. / rate, rate,
                                            120, true, true, 4, 4, nullptr, 0, mix, &capture);
        bool intact = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < audio.getNumSamples(); ++n)
                intact &= audio.getSample(c, n) == .625f;
        passed &= status != PluginAutomationStream::Status::ready && capture.status == expected &&
                  capture.count == 0 && intact && host.packetFrame() == 255 &&
                  unchanged(before, host.statistics()) && unchanged(counters, host.automationStatistics()) &&
                  host.midiResetGeneration() == reset && host.processId() == pid && host.wetBlend() == wet &&
                  host.bypassBlend() == bypass && !host.deliveryFailed() &&
                  host.transportOutcome().succeeded();
        facts += juce::String(reason) + "," + juce::String(int(capture.status)) + "," +
                 juce::String(juce::int64(capture.required)) + "," +
                 juce::String(juce::int64(capture.count)) + "," + juce::String(host.packetFrame()) + "," +
                 juce::String(juce::int64(host.statistics().submitted)) + "," +
                 juce::String(juce::int64(host.statistics().parametersApplied)) + "," +
                 juce::String(juce::int64(host.statistics().parametersRejected)) + "," +
                 juce::String(int(!host.deliveryFailed())) + "\n";
    };
    std::array<PluginPcmSegment, 1> insufficientEntries{};
    PluginPcmCapture insufficient(insufficientEntries);
    refuse("capacity", insufficient, 3, 65, .2f, PluginPcmReceiptStatus::capacityExceeded);
    passed &= insufficient.required == 2;
    PluginPcmCapture wrong(entries, control.pcmIdentity());
    refuse("wrong_actual_worker", wrong, 3, 65, .2f, PluginPcmReceiptStatus::identityMismatch);
    refuse("negative_offset", receipt, -1, 65, 1, PluginPcmReceiptStatus::invalidSpan);
    refuse("negative_frames", receipt, 0, -1, 1, PluginPcmReceiptStatus::invalidSpan);
    refuse("outside_span", receipt, 269, 65, 1, PluginPcmReceiptStatus::invalidSpan);
    refuse("invalid_mix", receipt, 3, 65, std::numeric_limits<float>::quiet_NaN(),
           PluginPcmReceiptStatus::invalidMix);
    // Metadata-only overlap test: genuine Segment storage, external audio view,
    // no float access to that storage. Admission must refuse before any DSP access.
    std::array<PluginPcmSegment, 2> aliased{};
    float *channels[]{reinterpret_cast<float *>(aliased.data())};
    juce::AudioBuffer<float> overlapping(channels, 1, 1);
    PluginPcmCapture invalidStorage(aliased);
    const auto aliasStatus = stream->process(host, &bad, spec.id, overlapping, 0, 1, off, 255. / rate, rate,
                                             120, true, false, 4, 4, nullptr, 0, 1, &invalidStorage);
    passed &= aliasStatus != PluginAutomationStream::Status::ready &&
              invalidStorage.status == PluginPcmReceiptStatus::invalidStorage && invalidStorage.count == 0 &&
              unchanged(before, host.statistics()) && unchanged(counters, host.automationStatistics()) &&
              host.packetFrame() == 255;
    facts += "storage_overlap," + juce::String(int(invalidStorage.status)) + "," +
             juce::String(juce::int64(invalidStorage.required)) + "," +
             juce::String(juce::int64(invalidStorage.count)) + "," + juce::String(host.packetFrame()) + "\n";
    std::span<PluginPcmSegment> noStorage;
    PluginPcmCapture zero(noStorage, host.pcmIdentity());
    passed &= stream->process(host, &bad, spec.id, audio, 0, 0, off, 255. / rate, rate, 120, true, false, 4,
                              4, nullptr, 0, 1, &zero) == PluginAutomationStream::Status::ready &&
              zero.status == PluginPcmReceiptStatus::complete && zero.count == 0 && zero.required == 0 &&
              host.packetFrame() == 255 && unchanged(before, host.statistics()) &&
              unchanged(counters, host.automationStatistics());
    facts += "zero_count," + juce::String(int(zero.status)) + "," + juce::String(juce::int64(zero.required)) +
             "," + juce::String(juce::int64(zero.count)) + "," + juce::String(host.packetFrame()) + "\n";
    audio.clear();
    nullAudio.clear();
    const auto resumed = stream->process(host, &good, spec.id, audio, 0, 65, {}, 255. / rate, rate, 120, true,
                                         false, 4, 4, nullptr, 0, 1, &receipt);
    const auto otherResumed =
        plain->process(control, &twin, spec.id, nullAudio, 0, 65, {}, 255. / rate, rate, 120, true, false);
    std::vector<float> actual, expected, twinPcm;
    actual.reserve(130);
    expected.reserve(130);
    twinPcm.reserve(130);
    double error = 0;
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 65; ++n)
        {
            const float wanted = n == 0   ? 0.f
                                 : c == 0 ? float(.25 * (61. / 512 + 1. / 4096))
                                          : float(-.25 * (68. / 512 + 1. / 8192));
            const auto value = audio.getSample(c, n);
            actual.push_back(value);
            expected.push_back(wanted);
            twinPcm.push_back(nullAudio.getSample(c, n));
            error = std::isfinite(value) ? std::max(error, std::abs(double(value) - wanted)) : INFINITY;
            passed &= value == nullAudio.getSample(c, n);
        }
    completed(host);
    const auto native = parameterCatalogue(host, true);
    passed &= resumed == PluginAutomationStream::Status::ready && otherResumed == resumed && error == 0 &&
              receipt.status == PluginPcmReceiptStatus::complete && receipt.count == 2 &&
              receipt.storage[0].offset == 0 && receipt.storage[0].count == 1 &&
              receipt.storage[1].offset == 1 && receipt.storage[1].count == 64 &&
              receipt.storage[1].consumed && receipt.storage[1].consumed->sequence == 0 &&
              receipt.storage[1].consumed->stamp == PluginLatencyStamp{0, 1} &&
              nativeParameter(native, 90).value == 1.f / 1024 && nativeParameter(native, 91).value == 0 &&
              host.statistics().parametersApplied == 1 && !host.deliveryFailed();
    facts += "resumed," + juce::String(int(receipt.status)) + ",error=" + juce::String(error, 12) +
             ",on=" + juce::String(nativeParameter(native, 90).value, 12) +
             ",off=" + juce::String(nativeParameter(native, 91).value, 12) + "\n";
    require(folder.getChildFile("refusals.csv").replaceWithText(facts),
            "All refusal/retry facts retained before assertion");
    raw(folder.getChildFile("actual-planar-f32.raw"), actual);
    raw(folder.getChildFile("expected-planar-f32.raw"), expected);
    raw(folder.getChildFile("null-planar-f32.raw"), twinPcm);
    require(passed,
            "Caller refusal preserves both native queues/automation history and actual next held-note PCM");
}
__declspec(noinline) inline void tests(const juce::File &folder, const juce::File &parameterBinary,
                                       const juce::File &instrumentBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned aggregate Stream capture evidence");
    require(parameterBinary.existsAsFile() && instrumentBinary.existsAsFile() &&
                pluginWorkerExecutable().existsAsFile(),
            "Actual original CMake MODULE fixture/worker files present");
    const auto parameter = parameterSpec(parameterBinary), instrument = parameterSpec(instrumentBinary);
    require(!parameter.instrument && instrument.instrument,
            "Genuine distinct original SDK effect and instrument");
    require(
        folder.getChildFile("identities.txt")
            .replaceWithText("test=" +
                             juce::SHA256(juce::File::getSpecialLocation(juce::File::currentExecutableFile))
                                 .toHexString() +
                             "\nworker=" + juce::SHA256(pluginWorkerExecutable()).toHexString() +
                             "\nparameter=" + juce::SHA256(parameterBinary).toHexString() +
                             "\ninstrument=" + juce::SHA256(instrumentBinary).toHexString() + "\n"),
        "Exact original binary identities");
    std::exception_ptr first;
    juce::String outcomes;
    const auto run = [&](const juce::String &name, auto &&body)
    {
        try
        {
            body();
            outcomes += name + "=completed\n";
        }
        catch (const std::exception &e)
        {
            if (!first)
                first = std::current_exception();
            outcomes += name + "=failed:" + juce::String(e.what()) + "\n";
        }
        catch (...)
        {
            if (!first)
                first = std::current_exception();
            outcomes += name + "=failed:unknown\n";
        }
        require(folder.getChildFile("cases.txt").replaceWithText(outcomes),
                "Independent case outcomes retained");
    };
    for (bool offline : {true, false})
        for (bool automate : {false, true})
        {
            const juce::String name =
                juce::String(offline ? "offline1Q_" : "live2Q_") + (automate ? "automation" : "fast");
            run(name, [&] { equivalence(folder.getChildFile(name), parameter, automate, offline); });
        }
    run("pending_instrument", [&] { pending(folder.getChildFile("pending_instrument"), instrument); });
    if (first)
        std::rethrow_exception(first);
}
} // namespace plugin_stream_pcm
