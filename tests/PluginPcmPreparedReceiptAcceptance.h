// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SyntheticDevice.h"
#include <array>
#include <cmath>
#include <limits>

// Additive to frozen PluginPcmReceiptAcceptance.h. Actual original SDK instrument
// through real JUCE manager/enforcer/retained physical metadata, no native DAW graph.
// CONTROL dispatch is outside processing; no hardware/listening/paced-driver claim.
namespace plugin_pcm_prepared
{
using Manager = juce::AudioDeviceManager;
constexpr int quantum = 256, rate = 48000, recordCapacity = 128, pcmCapacity = 32768;
struct Record
{
    Manager::PhysicalCallbackSnapshot physical;
    bool known = false, refused = false, refusalIntact = true;
    int frame = 0, frames = 0, cursorBefore = 0, cursorAfter = 0;
    PluginTransportOutcome begin, end;
    PluginPcmIdentity identity;
    PluginPcmReceiptStatus status = PluginPcmReceiptStatus::empty;
    std::array<PluginPcmSegment, 2> segments{};
    std::size_t count = 0, required = 0;
    PluginPcmReceiptStatus refusedStatus = PluginPcmReceiptStatus::empty;
    std::size_t refusedCount = 0, refusedRequired = 0;
    PluginHost::Statistics beforeRefusal, afterRefusal;
    int refusedCursorBefore = 0, refusedCursorAfter = 0;
    unsigned resetBefore = 0, resetAfter = 0;
};
struct Driver final : juce::AudioIODeviceCallback
{
    PluginHost &host;
    const PreparedPluginTransport *const plan; // CONTROL retains owner outside this driver.
    unsigned catalogueGeneration;
    juce::AudioBuffer<float> audio{2, quantum}, pcm{2, pcmCapacity};
    std::array<Record, recordCapacity> records{};
    int count = 0, inputFrame = 0, outerPosition = 0, noteOff = -1;
    std::uint64_t serial = 0;
    bool refuseCapacity = false, failed = false, overflow = false;
    Driver(PluginHost &h, const PreparedPluginTransport &p, unsigned generation)
        : host(h), plan(&p), catalogueGeneration(generation)
    {
        pcm.clear();
    }
    void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *output, int channels,
                                          int frames, const juce::AudioIODeviceCallbackContext &) override
    {
        for (int c = 0; c < channels; ++c)
            std::fill_n(output[c], frames, 0.f);
        if (failed || count >= recordCapacity || inputFrame > pcmCapacity - frames || frames > quantum)
        {
            failed = overflow = true;
            return;
        }
        auto &record = records[std::size_t(count++)];
        record.frame = inputFrame;
        record.frames = frames;
        record.cursorBefore = host.packetFrame();
        record.known = plan->read(record.physical);
        if (!record.known)
        {
            failed = true;
            return;
        }
        if (serial != record.physical.callbackSerial)
        {
            serial = record.physical.callbackSerial;
            outerPosition = 0;
            // The actual whole physical total, not this first JUCE slice.
            record.begin = host.beginPhysicalBurst(record.physical.outerFrames);
            if (!record.begin.succeeded())
            {
                failed = true;
                host.abortPhysicalBurst();
                return;
            }
        }
        if (refuseCapacity)
        {
            // Executed only at cursor255 inside a genuine admitted65-frame outer.
            // Stage an off and different gain that MUST NOT reach pending/native state.
            std::array<PluginPcmSegment, 1> insufficientSegments{};
            PluginPcmCapture rejected(insufficientSegments);
            for (int c = 0; c < 2; ++c)
                std::fill_n(audio.getWritePointer(c), quantum, .625f);
            ipc::MidiEvent off{0, {0x80, 60, 0}, 3};
            const ipc::ParameterEvent gain{77, 0, .75f};
            record.beforeRefusal = host.statistics();
            record.refusedCursorBefore = host.packetFrame();
            record.resetBefore = host.midiResetGeneration();
            const auto wetBefore = host.wetBlend(), bypassBefore = host.bypassBlend();
            host.process(audio, 0, frames, &off, 1, inputFrame / double(rate), 120, true, true, 4, 4, &gain,
                         1, catalogueGeneration, true, nullptr, 0, .2f, &rejected);
            record.refused = true;
            record.refusedStatus = rejected.status;
            record.refusedCount = rejected.count;
            record.refusedRequired = rejected.required;
            record.afterRefusal = host.statistics();
            record.refusedCursorAfter = host.packetFrame();
            record.resetAfter = host.midiResetGeneration();
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < quantum; ++n)
                    record.refusalIntact &= audio.getSample(c, n) == .625f;
            record.refusalIntact &= wetBefore == host.wetBlend() && bypassBefore == host.bypassBlend() &&
                                    host.transportOutcome().succeeded();
            refuseCapacity = false;
        }
        audio.clear();
        std::array<ipc::MidiEvent, 2> events{};
        int eventsCount = 0;
        if (inputFrame == 0)
            events[std::size_t(eventsCount++)] = {0, {0x90, 60, 127}, 3};
        if (noteOff >= inputFrame && noteOff < inputFrame + frames)
            events[std::size_t(eventsCount++)] = {noteOff - inputFrame, {0x80, 60, 0}, 3};
        const ipc::ParameterEvent gain{77, 0, .25f};
        PluginPcmCapture capture(record.segments);
        host.process(audio, 0, frames, events.data(), eventsCount, inputFrame / double(rate), 120, true,
                     false, 4, 4, inputFrame == 0 ? &gain : nullptr, inputFrame == 0 ? 1 : 0,
                     inputFrame == 0 ? catalogueGeneration : 0, true, nullptr, 0, 1, &capture);
        record.identity = capture.identity;
        record.status = capture.status;
        record.count = capture.count;
        record.required = capture.required;
        record.cursorAfter = host.packetFrame();
        if (capture.status != PluginPcmReceiptStatus::complete || !host.transportOutcome().succeeded())
        {
            failed = true;
            host.abortPhysicalBurst();
            return;
        }
        for (int c = 0; c < 2; ++c)
        {
            pcm.copyFrom(c, inputFrame, audio, c, 0, frames);
            if (c < channels)
                std::copy_n(audio.getReadPointer(c), frames, output[c]);
        }
        inputFrame += frames;
        outerPosition += frames;
        if (outerPosition == record.physical.outerFrames)
        {
            record.end = host.endPhysicalBurst();
            if (!record.end.succeeded() || record.end.processedFrames != record.physical.outerFrames ||
                record.end.sourceQuota != record.physical.outerFrames)
            {
                failed = true;
                host.abortPhysicalBurst();
            }
        }
    }
};
inline bool sameUuid(const PluginPcmIdentity &identity, const juce::String &instance)
{
    const juce::Uuid uuid(instance);
    return std::equal(identity.instance.begin(), identity.instance.end(), uuid.getRawData());
}
inline void drain(PluginHost &host)
{
    const auto wanted = host.statistics().submitted;
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (host.statistics().processed < wanted && juce::Time::getMillisecondCounter() < deadline)
        dispatchMessages(1); // CONTROL only, after physical callback quiescence.
    require(host.statistics().processed == wanted, "Actual SDK completed submitted input prefix");
}
__declspec(noinline) inline void positive(const juce::File &folder, const PluginSpec &spec,
                                          int explicitPhysicalBound, int packets, int transport)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh prepared receipt attempt");
    Manager manager;
    manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", double(rate));
    setup.setAttribute("audioDeviceBufferSize", quantum);
    require(manager.initialise(0, 2, &setup, false).isEmpty(), "Actual original synthetic manager");
    auto *device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
    require(device != nullptr, "Actual pre-enforcer synthetic provider");
    PreparedPluginTransport::Ptr plan, equal;
    require(
        PreparedPluginTransport::prepare(manager, quantum, {0, 1}, 1000000, explicitPhysicalBound, plan)
                .succeeded() &&
            PreparedPluginTransport::prepare(manager, quantum, {0, 1}, 1000000, explicitPhysicalBound, equal)
                .succeeded() &&
            plan && equal && plan != equal && plan->budget().packets == packets &&
            plan->budget().transportSamples == transport &&
            plan->budget().sourceFrames == explicitPhysicalBound,
        "Real retained reader plus explicit bound, exact known K and transport");
    PluginHost host(plan);
    require(host.start(spec, rate, quantum, false).wasOk() &&
                host.transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                host.matchesPrepared(spec, rate, quantum, plan) &&
                !host.matchesPrepared(spec, rate, quantum, equal),
            "Exact actual plan owner, not equal scalars/alternate reader identity");
    const auto catalogue = parameterCatalogue(host);
    const auto instance = host.runtimeIdentity();
    const auto pid = host.processId();
    const auto reset = host.midiResetGeneration();
    auto driver = std::make_unique<Driver>(host, *plan, catalogue.generation);
    manager.addAudioCallback(driver.get());
    struct Remove
    {
        Manager &manager;
        juce::AudioIODeviceCallback *callback;
        ~Remove()
        {
            if (callback)
                manager.removeAudioCallback(callback);
        }
    } remove{manager, driver.get()};
    juce::AudioBuffer<float> physical(2, explicitPhysicalBound);
    juce::String trace = "outer,frames,frame,inner,serial,epoch,scopeEnd,processed,quota,capture,"
                         "segments,required,refusal,maxError,ms,new,delete\n";
    juce::String segments = "outer,slice,segment,frame,offset,count,packetFrame,outputPacket,kind,"
                            "consumed,native,generation,dry,forced,mix,hostToken,planToken,Q,KQ\n";
    int outer = 0;
    const auto drive = [&](int frames)
    {
        // Capacity allocated on CONTROL. Count passed to process is actual physical
        // length; manager's actual splitter supplies <=256 slices without test hooks.
        physical.setSize(2, frames, false, false, true);
        const int firstRecord = driver->count, firstFrame = driver->inputFrame;
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
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
            device->process(physical);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto allocations = callbackAllocations.load() - allocated;
        const auto destructions = callbackDestructions.load() - deleted;
        bool ok = !driver->failed && !driver->overflow && driver->inputFrame == firstFrame + frames &&
                  driver->count - firstRecord == (frames + 255) / 256 && host.alive() &&
                  host.processId() == pid && host.runtimeIdentity() == instance &&
                  host.midiResetGeneration() == reset && !host.deliveryFailed() && allocations == 0 &&
                  destructions == 0 && ms <= frames * 1000. / rate;
        double error = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < frames; ++n)
            {
                const int source = firstFrame + n - transport;
                const bool sounding = source >= 0 && (driver->noteOff < 0 || source < driver->noteOff);
                const float wanted = !sounding ? 0
                                     : c == 0  ? float(.25 * (61. / 512 + 1. / 4096))
                                               : float(-.25 * (68. / 512 + 1. / 8192));
                const float observed = physical.getSample(c, n);
                error = std::isfinite(observed) ? std::max(error, std::abs(double(observed) - wanted))
                                                : std::numeric_limits<double>::infinity();
                ok &= observed == driver->pcm.getSample(c, firstFrame + n);
            }
        ok &= error <= 2e-6;
        for (int i = firstRecord; i < driver->count; ++i)
        {
            const auto &record = driver->records[std::size_t(i)];
            ok &= record.known && record.physical.outerFrames == frames &&
                  record.physical.deviceEpoch == plan->budget().deviceEpoch &&
                  record.identity.preparedTransport == reinterpret_cast<std::uintptr_t>(plan.get()) &&
                  record.identity.preparedTransport != reinterpret_cast<std::uintptr_t>(equal.get()) &&
                  record.identity.host == reinterpret_cast<std::uintptr_t>(&host) &&
                  sameUuid(record.identity, instance) && record.identity.quantum == 256 &&
                  record.identity.transportSamples == transport &&
                  record.status == PluginPcmReceiptStatus::complete && record.count == record.required &&
                  record.count == std::size_t(1 + ((record.frame % 256) + record.frames - 1) / 256) &&
                  record.cursorBefore == record.frame % 256 &&
                  record.cursorAfter == (record.frame + record.frames) % 256;
            int done = 0;
            for (std::size_t s = 0; s < record.count; ++s)
            {
                const auto &segment = record.segments[s];
                const int packetFrame = (record.frame + done) % 256;
                const int outputPacket = (record.frame + done) / 256, consumed = outputPacket - packets;
                const int count = std::min(record.frames - done, 256 - packetFrame);
                ok &= segment.offset == done && segment.count == count &&
                      segment.packetFrame == packetFrame &&
                      segment.outputPacket == std::uint64_t(outputPacket) &&
                      segment.dryDelaySamples == transport && !segment.forcedDry &&
                      segment.mix == PluginPcmMixKind::wet && segment.firstWet == 1 && segment.lastWet == 1 &&
                      segment.firstBypass == 0 && segment.lastBypass == 0;
                if (consumed < 0)
                    ok &= segment.packet == PluginPcmPacketKind::startup && !segment.consumed;
                else
                    ok &= segment.packet == PluginPcmPacketKind::completed && segment.consumed &&
                          segment.consumed->sequence == std::uint64_t(consumed) &&
                          segment.consumed->stamp == PluginLatencyStamp{0, 1};
                segments +=
                    juce::String(outer) + "," + juce::String(i) + "," + juce::String(int(s)) + "," +
                    juce::String(record.frame) + "," + juce::String(segment.offset) + "," +
                    juce::String(segment.count) + "," + juce::String(segment.packetFrame) + "," +
                    juce::String(juce::int64(segment.outputPacket)) + "," +
                    juce::String(int(segment.packet)) + "," +
                    juce::String(segment.consumed ? juce::int64(segment.consumed->sequence) : -1) + "," +
                    juce::String(segment.consumed ? segment.consumed->stamp.nativeSamples : -1) + "," +
                    juce::String(segment.consumed ? segment.consumed->stamp.generation : 0) + "," +
                    juce::String(segment.dryDelaySamples) + "," + juce::String(int(segment.forcedDry)) + "," +
                    juce::String(int(segment.mix)) + "," + juce::String(juce::int64(record.identity.host)) +
                    "," + juce::String(juce::int64(record.identity.preparedTransport)) + "," +
                    juce::String(record.identity.quantum) + "," +
                    juce::String(record.identity.transportSamples) + "\n";
                done += count;
            }
            ok &= done == record.frames;
            if (record.refused)
                ok &= record.refusedStatus == PluginPcmReceiptStatus::capacityExceeded &&
                      record.refusedCount == 0 && record.refusedRequired == 2 && record.refusalIntact &&
                      record.refusedCursorBefore == 255 && record.refusedCursorAfter == 255 &&
                      record.resetBefore == record.resetAfter &&
                      record.beforeRefusal.submitted == record.afterRefusal.submitted &&
                      record.beforeRefusal.parametersApplied == record.afterRefusal.parametersApplied &&
                      record.beforeRefusal.parametersRejected == record.afterRefusal.parametersRejected &&
                      record.beforeRefusal.midiOverflow == record.afterRefusal.midiOverflow;
            trace += juce::String(outer) + "," + juce::String(frames) + "," + juce::String(record.frame) +
                     "," + juce::String(record.frames) + "," +
                     juce::String(juce::int64(record.physical.callbackSerial)) + "," +
                     juce::String(juce::int64(record.physical.deviceEpoch)) + "," +
                     juce::String(int(record.end.status)) + "," + juce::String(record.end.processedFrames) +
                     "," + juce::String(record.end.sourceQuota) + "," + juce::String(int(record.status)) +
                     "," + juce::String(int(record.count)) + "," + juce::String(int(record.required)) + "," +
                     juce::String(int(record.refusedStatus)) + "," + juce::String(error, 12) + "," +
                     juce::String(ms, 7) + "," + juce::String(allocations) + "," +
                     juce::String(destructions) + "\n";
        }
        require(folder.getChildFile("physical-segments.csv").replaceWithText(trace),
                "Evidence before assertions");
        require(folder.getChildFile("packet-segments.csv").replaceWithText(segments),
                "Exact interval identity facts retained");
        // Retain ALL actual PCM at every stage, including the failing callback.
        auto stream = folder.getChildFile("actual-planar-float32le.raw").createOutputStream();
        require(bool(stream) && stream->setPosition(0) && stream->truncate().wasOk(),
                "Owned raw PCM truncate");
        for (int c = 0; c < 2; ++c)
            require(
                stream->write(driver->pcm.getReadPointer(c), std::size_t(driver->inputFrame) * sizeof(float)),
                "All actual native callback PCM retained");
        stream.reset();
        require(
            ok,
            "Prepared-K exact identity/scope/PCM/partial receipts/deadline, no input or oracle exclusions");
        ++outer;
        drain(host);
    };
    drive(255); // Actual pending MIDI-on/gain prefix, unsubmitted.
    require(host.packetFrame() == 255 && host.statistics().submitted == 0,
            "Pending prefix exists before refusal");
    driver->refuseCapacity = true;
    drive(65); // Failed capacity, same admitted scope retry; processed quota MUST be65, not130.
    for (int n = 0; n < 3; ++n)
        drive(explicitPhysicalBound);
    driver->noteOff = driver->inputFrame;
    for (const int frames : {127, 64, 65})
        drive(frames);
    for (int n = 0; n < packets + 2; ++n)
        drive(256); // Literal entire release/tail.
    const auto counters = parameterCatalogue(host, true);
    require(nativeParameter(counters, 90).value == 1.f / 1024 &&
                nativeParameter(counters, 91).value == 1.f / 1024 &&
                host.statistics().parametersApplied == 1 && host.statistics().parametersRejected == 0 &&
                host.statistics().midiOverflow == 0 && host.transportOutcome().succeeded(),
            "Actual native one on/off and exactly original gain; rejected intent never applied");
    manager.removeAudioCallback(driver.get());
    remove.callback = nullptr;

    // Two independent real SDK failure cases; never damage the admitted happy Host.
    for (const bool abortBefore : {false, true})
    {
        PluginHost refused(plan);
        require(refused.start(spec, rate, quantum, false).wasOk(), "Independent actual SDK refusal worker");
        const auto schema = parameterCatalogue(refused);
        require(nativeParameter(schema, 90).value == 0 && nativeParameter(schema, 91).value == 0 &&
                    nativeParameter(schema, 77).value == .5f,
                "Actual native baseline BEFORE fault");
        struct Outside final : juce::AudioIODeviceCallback
        {
            PluginHost &host;
            unsigned generation;
            bool abortBefore;
            juce::AudioBuffer<float> audio{2, quantum};
            std::array<PluginPcmSegment, 2> entries{};
            PluginPcmIdentity identity;
            PluginPcmReceiptStatus status = PluginPcmReceiptStatus::empty;
            PluginTransportOutcome begin, fault;
            std::size_t count = 0, required = 0;
            bool intact = true;
            int cursor = -1;
            Outside(PluginHost &h, unsigned g, bool a) : host(h), generation(g), abortBefore(a) {}
            void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
            void audioDeviceStopped() override {}
            void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *out, int channels,
                                                  int frames,
                                                  const juce::AudioIODeviceCallbackContext &) override
            {
                for (int c = 0; c < channels; ++c)
                    std::fill_n(out[c], frames, 0.f);
                if (abortBefore)
                {
                    begin = host.beginPhysicalBurst(frames);
                    host.abortPhysicalBurst();
                    host.abortPhysicalBurst();
                }
                for (int c = 0; c < 2; ++c)
                    std::fill_n(audio.getWritePointer(c), quantum, .625f);
                PluginPcmCapture capture(entries);
                ipc::MidiEvent on{0, {0x90, 60, 127}, 3};
                const ipc::ParameterEvent gain{77, 0, .75f};
                host.process(audio, 0, frames, &on, 1, 0, 120, true, true, 4, 4, &gain, 1, generation, true,
                             nullptr, 0, .2f, &capture);
                status = capture.status;
                count = capture.count;
                required = capture.required;
                identity = capture.identity;
                fault = host.transportOutcome();
                cursor = host.packetFrame();
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < quantum; ++n)
                        intact &= audio.getSample(c, n) == .625f;
            }
        } outside(refused, schema.generation, abortBefore);
        manager.addAudioCallback(&outside);
        Remove negativeRemove{manager, &outside};
        juce::AudioBuffer<float> negative(2, 64);
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool prior = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = prior;
                }
            } probe;
            device->process(
                negative); // Genuine physical callback, no admitted Host scope (or explicitly aborted).
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto before = refused.statistics();
        // Sticky transport refusal makes alive false, so supported native reads
        // are explicitly unavailable. Do not reuse a cached baseline as poststate.
        PluginParameterCatalogue inaccessible;
        inaccessible.serial = 777;
        const bool catalogueRefused = refused.requestParameterCatalogue() == 0 &&
                                      !refused.readParameterCatalogue(inaccessible) &&
                                      inaccessible.serial == 777;
        const bool ok = (!abortBefore || outside.begin.succeeded()) &&
                        outside.status == PluginPcmReceiptStatus::transportRefused && outside.count == 0 &&
                        outside.required == 1 && outside.cursor == 0 && outside.intact &&
                        outside.fault.status == PluginTransportStatus::burstMissing &&
                        before.submitted == 0 && before.processed == 0 && before.parametersApplied == 0 &&
                        before.parametersRejected == 0 && !refused.bypassBlend() && !refused.wetBlend() &&
                        outside.identity.preparedTransport == reinterpret_cast<std::uintptr_t>(plan.get()) &&
                        outside.identity.transportSamples == transport && catalogueRefused &&
                        callbackAllocations.load() - allocated == 0 &&
                        callbackDestructions.load() - deleted == 0 && ms <= 64 * 1000. / rate;
        require(folder.getChildFile(abortBefore ? "aborted-scope.txt" : "missing-scope.txt")
                    .replaceWithText("pass=" + juce::String(int(ok)) +
                                     ";capture=" + juce::String(int(outside.status)) +
                                     ";fault=" + juce::String(int(outside.fault.status)) +
                                     ";count=" + juce::String(int(outside.count)) + ";catalogueRefused=" +
                                     juce::String(int(catalogueRefused)) + ";ms=" + juce::String(ms, 7) +
                                     "\nFailed-callback fixture output is not product recovery.\n"),
                "Retain actual SDK typed refusal before assertion");
        require(
            ok,
            "Out-of-scope capture noncompletion, zero native submissions; postfault catalogue unavailable");
    }
    require(folder.getChildFile("result.txt").replaceWithText("Actual prepared receipt suite passed.\n"),
            "Terminal prepared-identity/scope/native result");
}
} // namespace plugin_pcm_prepared

static void pluginPcmPreparedReceiptTests(const juce::File &folder, const juce::File &instrumentBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh additive prepared capture evidence");
    const auto instrument = parameterSpec(instrumentBinary);
    require(instrument.instrument && instrument.name == "VibeRa Instrument Runtime Fixture",
            "Exact scanned genuine original instrument, not fabricated provider outcome");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), instrumentBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact original worker inputs");
    constexpr std::array<std::array<int, 3>, 3> cases{{{256, 2, 512}, {512, 3, 768}, {1792, 8, 2048}}};
    for (const auto test : cases)
        plugin_pcm_prepared::positive(folder.getChildFile("K" + juce::String(test[1])), instrument, test[0],
                                      test[1], test[2]);
}
