// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationStream.h"
#include "PluginAutomationPreparation.h"

static void providerAutomationStream(PluginHost &host, const PluginSpec &spec,
                                     const PluginParameterCatalogue &catalogue, std::uint32_t id,
                                     const juce::File &folder, double rate, int quantum)
{
    const auto &native = nativeParameter(catalogue, id);
    auto project = Project::empty();
    auto declared = spec;
    declared.id = project.allocate();
    declared.automationBindings = {{id, native.flags, native.steps, native.value, native.name, native.label}};
    project.channels.front().instrument = declared;
    LiveDocument document;
    AutomationDocument constant;
    constant.initialValues = {{{AutomationParameter::pluginNormalized, declared.id, id}, native.value}};
    std::string error;
    require(AutomationProgram::compile(constant, true, document.automation, error),
            "actual provider stream constant prepared");
    // Bounded borrowed alias only for this test call; the caller owns the stack
    // host and its verified worker for the entire preparation and DSP sequence.
    const std::map<Id, std::shared_ptr<PluginHost>> hosts{
        {declared.id, std::shared_ptr<PluginHost>(&host, [](PluginHost *) {})}};
    require(prepareNativeAutomationRoutes(project, document, hosts, rate).wasOk(),
            "actual installed provider metadata and workload admitted through production route preparation");
    auto stream = std::make_unique<PluginAutomationStream>();
    juce::AudioBuffer<float> audio(2, quantum);
    audio.clear();
    const auto before = host.statistics();
    const auto pid = host.processId();
    int begin = 0;
    for (const int end : {7, 23, quantum})
    {
        require(stream->process(host, &document, declared.id, audio, begin, end - begin, {}, begin / rate,
                                rate, 120, true, false) == PluginAutomationStream::Status::ready,
                "actual provider partial callback prepared through shared adapter stream");
        begin = end;
    }
    audio.clear();
    require(stream->process(host, &document, declared.id, audio, 0, quantum, {}, quantum / rate, rate, 120,
                            false, false) == PluginAutomationStream::Status::ready,
            "actual provider stopped tail passes through stream without new automated points");
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (host.statistics().processed < host.statistics().submitted &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(2);
    const auto after = host.statistics();
    require(after.parametersApplied == before.parametersApplied + 1 &&
                after.parametersRejected == before.parametersRejected && host.processId() == pid &&
                host.alive(),
            "actual provider constant target sent once across three callback slices with retained worker");
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < quantum; ++n)
            require(std::isfinite(audio.getSample(c, n)), "actual provider stream finite PCM");
    require(folder.getChildFile("native-provider-stream.txt")
                .replaceWithText("PASS actual installed provider -> production metadata/workload/identity "
                                 "route preparation -> "
                                 "three partial callbacks -> one same-value native event; stopped tail and "
                                 "worker retained. No changing-response/listening/hardware claim.\n"),
            "actual provider stream evidence saved");
}

static void nativeAutomationStreamTests(const juce::File &folder, const juce::File &binary)
{
    using Status = PluginAutomationStream::Status;
    const auto spec = parameterSpec(binary);
    const AutomationKey gain{AutomationParameter::pluginNormalized, spec.id, 0};
    const AutomationKey bias{AutomationParameter::pluginNormalized, spec.id, 0xfedcba98u};
    unsigned cases = 0;
    double maximumError = 0;
    for (const double rate : {44100., 48000., 96000.})
        for (const int quantum : {64, 256, 512, 4096})
        {
            PluginHost host;
            require(host.start(spec, rate, quantum, true).wasOk(), "native adapter stream worker prepared");
            const auto pid = host.processId();
            require(host.start(spec, rate, quantum, true).failed() && host.processId() == pid && host.alive(),
                    "single-use startup cannot replace a retained worker behind admitted route identity");
            const auto catalogue = parameterCatalogue(host);
            LiveDocument document;
            AutomationDocument curves;
            curves.initialValues = {{gain, .5f}, {bias, .2f}};
            curves.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{gain, 0, 1}, {bias, 1, 0}}}};
            curves.placements = {{4, 1, 0, 4, 0}};
            std::string error;
            require(AutomationProgram::compile(curves, true, document.automation, error),
                    "native adapter stream program prepared");
            document.nativeAutomation = {{spec.id, catalogue.generation, host.runtimeIdentity()}};
            auto stream = std::make_unique<PluginAutomationStream>();
            juce::AudioBuffer<float> audio(2, quantum + 10);
            for (int c = 0; c < 2; ++c)
                std::fill_n(audio.getWritePointer(c), quantum + 10, -7.f);
            std::vector<double> positions;
            int begin = 0;
            for (const int end : {7, 23, 36, quantum})
            {
                const double beat = begin == 0 ? 1. : begin == 7 ? 3.999 : begin == 23 ? -.001 : 0.;
                for (int c = 0; c < 2; ++c)
                    std::fill_n(audio.getWritePointer(c, 5 + begin), end - begin, c ? -.25f : .75f);
                require(stream->process(host, &document, spec.id, audio, 5 + begin, end - begin, {}, beat / 2,
                                        rate, 120, true, false) == Status::ready,
                        "native adapter accepts partial callbacks across loop/pre-roll/seek discontinuities");
                for (int n = 0; n < end - begin; ++n)
                    positions.push_back(beat + n * 2 / rate);
                begin = end;
            }
            audio.clear(5, quantum);
            require(stream->process(host, &document, spec.id, audio, 5, quantum, {}, 0, rate, 120, false,
                                    false) == Status::ready,
                    "stopped stream flushes preceding native output without automation reassertion");
            for (int n = 0; n < quantum; ++n)
            {
                const auto position = positions[std::size_t(n)];
                const double delta = (double(.9f) - .1f) * std::clamp(position / 4, 0., 1.);
                const float g = position < 0 ? .5f : float(double(.1f) + delta);
                const float b = position < 0 ? .2f : float(1 - double(.1f) - delta);
                for (int c = 0; c < 2; ++c)
                {
                    const auto difference =
                        std::abs(double(audio.getSample(c, 5 + n) - ((c ? -.25f : .75f) * g + b)));
                    maximumError = std::max(maximumError, difference);
                    require(difference < .000001,
                            "native shared adapter PCM matches independent piecewise oracle");
                }
            }
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 5; ++n)
                    require(audio.getSample(c, n) == -7.f && audio.getSample(c, quantum + 5 + n) == -7.f,
                            "native adapter preserves samples outside callback destination range");
            require(host.statistics().parametersRejected == 0 && host.processId() == pid,
                    "native partial adapter stream preserves all admitted points and worker");
            ++cases;
        }

    {
        PluginHost host;
        require(host.start(spec, 48000, 64, true).wasOk(), "multi-quantum MIDI stream worker prepared");
        const auto metadata = parameterCatalogue(host);
        LiveDocument doc;
        AutomationDocument constant;
        constant.initialValues = {{bias, .1f}};
        std::string error;
        require(AutomationProgram::compile(constant, true, doc.automation, error), "MIDI stream prepared");
        doc.nativeAutomation = {{spec.id, metadata.generation, host.runtimeIdentity()}};
        auto stream = std::make_unique<PluginAutomationStream>();
        juce::AudioBuffer<float> audio(2, 128);
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), 128, c ? .3f : .6f);
        const std::array<ipc::MidiEvent, 4> midi{
            {{80, {0xb0, 7, 0}, 3}, {9, {0xb0, 7, 127}, 3}, {40, {0xb0, 7, 0}, 3}, {70, {0xb0, 7, 127}, 3}}};
        require(stream->process(host, &doc, spec.id, audio, 0, 128, midi, 0, 48000, 120, true, false) ==
                    Status::ready,
                "shared stream partitions multiple native quanta and stably orders MIDI offsets");
        const auto check = [&](int begin, int outputOffset)
        {
            for (int n = 0; n < 64; ++n)
            {
                const auto frame = begin + n;
                const float value = frame < 9    ? .5f
                                    : frame < 40 ? 1.f
                                    : frame < 70 ? 0.f
                                    : frame < 80 ? 1.f
                                                 : 0.f;
                for (int c = 0; c < 2; ++c)
                    require(std::abs(audio.getSample(c, outputOffset + n) - ((c ? .3f : .6f) * value + .1f)) <
                                .000001f,
                            "native mapped MIDI/constant automation PCM matches independent timing oracle");
            }
        };
        check(0, 64); // One native-quantum latency; the first result is already returned.
        audio.clear();
        require(stream->process(host, &doc, spec.id, audio, 0, 64, {}, 128. / 48000, 48000, 120, false,
                                false) == Status::ready,
                "multi-quantum stream tail flush");
        check(64, 0);
        require(host.statistics().parametersApplied == 2 && host.statistics().parametersRejected == 0,
                "constant target reasserts once per native quantum while MIDI keeps its exact offsets");
    }
    {
        PluginHost host;
        require(host.start(spec, 48000, 8192, true).wasOk(), "accumulated overflow stream worker prepared");
        const auto metadata = parameterCatalogue(host);
        LiveDocument doc;
        AutomationDocument curves;
        curves.initialValues = {{gain, .5f}, {bias, 0.f}};
        curves.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{gain, 0, 1}, {bias, 1, 0}}}};
        curves.placements = {{4, 1, 0, 4, 0}};
        std::string error;
        require(AutomationProgram::compile(curves, true, doc.automation, error), "overflow stream prepared");
        doc.nativeAutomation = {{spec.id, metadata.generation, host.runtimeIdentity()}};
        auto stream = std::make_unique<PluginAutomationStream>();
        juce::AudioBuffer<float> audio(2, 8192);
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), 8192, 1.f);
        require(stream->process(host, &doc, spec.id, audio, 0, 4096, {}, .5, 48000, 120, true, false) ==
                    Status::ready,
                "full native event prefix is retained until quantum submission");
        require(stream->process(host, &doc, spec.id, audio, 4096, 4096, {}, .5 + 4096. / 48000, 48000, 120,
                                true, false) == Status::capacityExceeded,
                "accumulated generated overflow invalidates the complete assembled native prefix");
        audio.clear();
        require(stream->process(host, &doc, spec.id, audio, 0, 8192, {}, 0, 48000, 120, false, false) ==
                    Status::ready,
                "failed stream quantum recovers for stopped tail without worker restart");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 8192; ++n)
                require(std::abs(audio.getSample(c, n) - .5f) < .000001f,
                        "stream overflow applies no partial native prefix and preserves audio");
        require(host.statistics().parametersApplied == 0 &&
                    host.statistics().parametersRejected >= ipc::parameterEvents && host.alive(),
                "stream overflow rejection remains explicit and audio processing survives");
        const auto delivery = host.automationStatistics();
        require(delivery.last == Status::ready && delivery.capacityExceeded == 1 && host.deliveryFailed(),
                "a recovered callback cannot erase the overflow history used to reject offline export");
    }
    {
        PluginHost host;
        require(host.start(spec, 48000, 256, true).wasOk(), "snapshot retirement stream worker prepared");
        const auto metadata = parameterCatalogue(host);
        auto stream = std::make_unique<PluginAutomationStream>();
        const auto makeDocument = [&]
        {
            auto doc = std::make_unique<LiveDocument>();
            AutomationDocument constant;
            constant.initialValues = {{gain, .6f}};
            std::string error;
            require(AutomationProgram::compile(constant, true, doc->automation, error),
                    "retirement program prepared");
            doc->nativeAutomation = {{spec.id, metadata.generation, host.runtimeIdentity()}};
            return doc;
        };
        LiveTimeline timeline;
        auto document = makeDocument();
        require(timeline.publish(document), "retirement snapshot published");
        juce::AudioBuffer<float> audio(2, 256);
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), 256, 1.f);
        {
            auto lease = timeline.acquire();
            require(stream->process(host, lease.document, spec.id, audio, 0, 7, {}, 0, 48000, 120, true,
                                    false) == Status::ready,
                    "native stream consumes a leased snapshot synchronously");
        }
        for (int i = 0; i < 10; ++i)
        {
            auto replacement = makeDocument();
            require(timeline.publish(replacement),
                    "idle stream retains no document lease that can stall three-slot publication");
        }
        require(stream->process(host, nullptr, spec.id, audio, 7, 16, {}, 7. / 48000, 48000, 120, true,
                                false) == Status::snapshotUnavailable,
                "missing snapshot holds scalar controls while continuing native audio processing");
        require(host.automationStatistics().snapshotUnavailable == 1 && host.deliveryFailed(),
                "snapshot fallback is observable and invalidates lossless offline export");
        {
            auto lease = timeline.acquire();
            require(stream->process(host, lease.document, spec.id, audio, 23, 233, {}, 23. / 48000, 48000,
                                    120, true, false) == Status::ready,
                    "new snapshot resumes the same packet after prior document reclamation");
            audio.clear();
            require(stream->process(host, lease.document, spec.id, audio, 0, 256, {}, 0, 48000, 120, false,
                                    false) == Status::ready,
                    "snapshot recovery packet flushed without native worker reset");
        }
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                require(std::abs(audio.getSample(c, n) - .6f) < .000001f,
                        "snapshot retirement/miss retains continuous native PCM");
        require(host.statistics().parametersApplied == 1 && host.statistics().parametersRejected == 0,
                "snapshot recovery does not re-send unchanged native target values");
    }
    // Two workers intentionally have the same parameter generation; identity still differs.
    PluginHost first, second;
    require(first.start(spec, 48000, 256, true).wasOk() && second.start(spec, 48000, 256, true).wasOk(),
            "identity/authority safety workers prepared");
    const auto metadata = parameterCatalogue(first);
    const auto other = parameterCatalogue(second);
    require(metadata.generation == other.generation && first.runtimeIdentity() != second.runtimeIdentity(),
            "same schema generation does not establish the same native worker instance");
    for (const int scenario : {0, 1})
    {
        LiveDocument doc;
        AutomationDocument constant;
        constant.initialValues = {{gain, .9f}};
        std::string error;
        require(AutomationProgram::compile(constant, true, doc.automation, error), "authority test prepared");
        if (scenario == 0)
            doc.nativeAutomation = {{spec.id, metadata.generation, second.runtimeIdentity()}};
        auto stream = std::make_unique<PluginAutomationStream>();
        juce::AudioBuffer<float> audio(2, 256);
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), 256, 1.f);
        const auto before = first.statistics();
        require(stream->process(first, &doc, spec.id, audio, 0, 256, {}, 0, 48000, 120, true, false) ==
                    Status::unavailableTarget,
                "missing authority or different worker identity cannot activate project targets");
        audio.clear();
        first.process(audio, 0, 256, nullptr, 0, 256. / 48000, 120, true, false);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                require(std::abs(audio.getSample(c, n) - .5f) < .000001f,
                        "unavailable authority preserves original native PCM");
        require(first.statistics().parametersApplied == before.parametersApplied &&
                    first.statistics().parametersRejected > before.parametersRejected,
                "unavailable route is explicitly refused without native writes");
        require(first.automationStatistics().unavailableTarget == std::uint64_t(scenario + 1) &&
                    first.deliveryFailed(),
                "wrong/missing worker authority remains visible to control and renderer diagnostics");
    }
    PluginHost live;
    require(live.start(spec, 48000, 256, false).wasOk(), "live shared adapter worker prepared");
    const auto liveMetadata = parameterCatalogue(live);
    const auto livePid = live.processId();
    auto liveStream = std::make_unique<PluginAutomationStream>();
    LiveTimeline timeline;
    juce::AudioBuffer<float> buffer(2, 64);
    double maximumCallbackMs = 0;
    int overruns = 0;
    juce::String trace = "callback,wallMs,submitted,applied,rejected\n";
    callbackAllocations = 0;
    callbackDestructions = 0;
    for (int i = 0; i < 64; ++i)
    {
        auto document = std::make_unique<LiveDocument>();
        AutomationDocument revision;
        revision.initialValues = {{gain, .5f}, {bias, .2f}};
        revision.sources = {
            {1, 4, {{2, 0, i % 3 == 1 ? .3f : .1f}, {3, 4, .9f}}, {{gain, 0, 1}, {bias, 1, 0}}}};
        revision.placements = {{4, 1, 0, 4, 0}};
        std::string error;
        require(AutomationProgram::compile(revision, true, document->automation, error),
                "rapid edit/undo-like projection compiled outside live callback");
        document->generation = std::uint64_t(i + 1);
        document->nativeAutomation = {{spec.id, liveMetadata.generation, live.runtimeIdentity()}};
        require(timeline.publish(document),
                "rapid parameter revision published without retaining raw pointers");
        buffer.clear();
        const auto started = juce::Time::getHighResolutionTicks();
        callbackAllocationProbe = true;
        auto lease = timeline.acquire();
        const auto result =
            liveStream->process(live, lease.document, spec.id, buffer, 0, 64, {},
                                i % 4 == 2 ? 0. : .5 + i * 64. / 48000, 48000, 120, true, false);
        lease = {};
        callbackAllocationProbe = false;
        const auto elapsed =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - started) * 1000;
        maximumCallbackMs = std::max(maximumCallbackMs, elapsed);
        overruns += elapsed > 64000. / 48000 ? 1 : 0;
        require(result == Status::ready, "rapid revision/seek retained live native packet admission");
        const auto statistics = live.statistics();
        trace += juce::String(i) + "," + juce::String(elapsed, 6) + "," +
                 juce::String(juce::int64(statistics.submitted)) + "," +
                 juce::String(juce::int64(statistics.parametersApplied)) + "," +
                 juce::String(juce::int64(statistics.parametersRejected)) + "\n";
        juce::Thread::sleep(3); // Virtual pacing outside the callback; not hardware qualification.
    }
    const auto drain = juce::Time::getMillisecondCounter() + 3000;
    while (live.statistics().processed < live.statistics().submitted &&
           juce::Time::getMillisecondCounter() < drain)
        juce::Thread::sleep(2);
    require(folder.getChildFile("native-stream-callbacks.csv").replaceWithText(trace),
            "native stream callback trace retained before deadline assertion");
    require(
        overruns == 0 && callbackAllocations.load() == 0 && callbackDestructions.load() == 0,
        "live lease/identity/carry/processing path has no observed C++ allocations/destructions/overruns");
    require(live.statistics().parametersRejected == 0 && live.statistics().parametersApplied > 0 &&
                live.processId() == livePid && live.alive(),
            "rapid native stream edits retain the same live worker with no refused parameter packets");
    require(
        folder.getChildFile("native-automation-stream.txt")
            .replaceWithText(
                "PASS shared instrument/effect adapter stream SDK-fixture PCM cases=" + juce::String(cases) +
                "; rates44100/48000/96000; quanta64/256/512/4096; "
                "partial destination ranges/loop/pre-roll/seek, multi-quantum mapped MIDI, "
                "whole-prefix overflow, snapshot retirement/hold/recovery, identity/missing authority. "
                "Max PCM error=" +
                juce::String(maximumError, 10) +
                "; live64callbacks maximumMs=" + juce::String(maximumCallbackMs, 6) +
                ", zero observed C++ allocation/destruction/overrun. "
                "Project target creation/complete DAW graph/PDC remain pending.\n"),
        "native stream evidence saved");
}
