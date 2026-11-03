// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Include after ParameterAcceptance.h and existing callback allocation/timing probes.
// Original real-worker experiment. All live outer spans are explicitly <=256.
// CONTROL dispatch between spans is not physical device/deadline qualification.
__declspec(noinline) static void pluginTurnaroundEffect512Tests(const juce::File &folder,
                                                                const juce::File &parameterBinary,
                                                                const juce::File &latencyBinary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned effect experiment directory preserves previous failures");
    const auto parameter = parameterSpec(parameterBinary), delayed = parameterSpec(latencyBinary);
    require(!parameter.instrument && parameter.name == "VibeRa Parameter Fixture" && !delayed.instrument &&
                delayed.name == "VibeRa Mixer Latency Fixture",
            "Genuine independently authored original SDK effect descriptors");
    juce::String identities;
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             pluginWorkerExecutable(), parameterBinary, latencyBinary})
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact executable, worker and genuine fixture input hashes");
    constexpr int quantum = 256, capacity = 8192, offset = 7, auxiliaryOffset = 13;
    constexpr double rate = 48000.;
    struct Scenario
    {
        const char *name;
        int auxiliaryChannels;
        bool initiallyBypassed;
    };
    constexpr std::array<Scenario, 4> scenarios{{{"affine_wet", 0, false},
                                                 {"affine_initial_bypass", 0, true},
                                                 {"native31_stereo", 2, false},
                                                 {"native31_mono_initial_bypass", 1, true}}};
    for (const auto &scenario : scenarios)
    {
        const bool nativeDelay = scenario.auxiliaryChannels != 0;
        // Literal independent fixture declarations, not measured shifts or Host getters.
        const int totalLatency = nativeDelay ? 543 : 512;
        juce::AudioBuffer<float> live(2, capacity), rendered(2, capacity);
        live.clear();
        rendered.clear();
        int liveFrames = 0, offlineFrames = 0;
        for (const bool offline : {false, true})
        {
            const auto output =
                folder.getChildFile(juce::String(scenario.name) + (offline ? "_offline" : "_live"));
            require(!output.exists() && output.createDirectory().wasOk(), "New exact attempt evidence");
            PluginHost host(PluginHost::TransportPolicy::twoQuanta);
            const auto &spec = nativeDelay ? delayed : parameter;
            const PluginBusRequest buses =
                nativeDelay ? PluginBusRequest{1, scenario.auxiliaryChannels} : PluginBusRequest{};
            const auto started = host.start(spec, rate, quantum, offline, {}, buses);
            require(started.wasOk(),
                    "Real fixed512 effect worker startup: " + started.getErrorMessage().toStdString());
            const auto catalogue = parameterCatalogue(host);
            const auto set = [&](std::uint32_t id, float value)
            {
                require(parameterResult(host, host.setParameter(id, value, catalogue.generation)).result == 0,
                        "Actual acknowledged fixture parameter");
            };
            if (nativeDelay)
                set(71, .5f);
            else
            {
                set(0, .5f);
                set(0xfedcba98u, .125f);
            }
            const auto state = [&]
            {
                const auto serial = host.requestState();
                require(serial != 0, "State request actually admitted outside processing");
                const auto deadline = juce::Time::getMillisecondCounter() + 10000;
                juce::String data;
                while (!host.readState(serial, data) && juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep(2);
                require(data.isNotEmpty(), "Native opaque state completed, not merely accepted");
                return data;
            };
            const auto baseline = state();
            const auto revision = host.parameterRevision();
            const auto pid = host.processId();
            const auto instance = host.runtimeIdentity();
            const auto reset = host.midiResetGeneration();
            const auto initial = host.statistics();
            const auto declaration = host.declaredLatency();
            require(host.transportPolicy() == PluginHost::TransportPolicy::twoQuanta &&
                        host.transportLatencySamples() == 512 && host.processingQuantum() == 256 &&
                        host.maximumLiveSpan() == 256 && host.latencySamples() == totalLatency &&
                        declaration.nativeSamples == (nativeDelay ? 31 : 0) && declaration.generation != 0 &&
                        host.busLayout() == buses && !host.consumedLatency().has_value(),
                    "Exact prepared policy/native layout and no invented consumed packet");

            juce::AudioBuffer<float> audio(2, offset + quantum + 5),
                detector(std::max(1, scenario.auxiliaryChannels), auxiliaryOffset + quantum + 5);
            juce::AudioBuffer<float> mainHistory(2, capacity), auxiliaryHistory(2, capacity);
            mainHistory.clear();
            auxiliaryHistory.clear();
            auto &capture = offline ? rendered : live;
            constexpr int rampSamples = 240; // ceil(48000*.005), independent fixed musical contract.
            constexpr double step = 1. / 240.;
            double blend = scenario.initiallyBypassed ? 1. : 0.;
            int clock = 0;
            bool passed = true;
            juce::String failure;
            juce::String csv =
                "phase,frame,frames,bypass,auxPresent,cursor,submitted,processed,missed,dropped,"
                "maxError,endpointExact,spanMs,new,delete,consumedSequence\n";
            const auto mainInput = [](int channel, int frame)
            {
                const auto steady = channel == 0 ? .125f : -.0625f;
                const auto impulse = (frame % (channel == 0 ? 257 : 263)) == (channel == 0 ? 0 : 17);
                return steady + (impulse ? (channel == 0 ? .25f : -.125f) : 0.f);
            };
            const auto auxiliaryInput = [](int channel, int frame)
            {
                const auto steady = channel == 0 ? .25f : -.375f;
                const auto impulse = (frame % (channel == 0 ? 251 : 241)) == (channel == 0 ? 11 : 23);
                return steady + (impulse ? (channel == 0 ? -.125f : .0625f) : 0.f);
            };
            const auto drive = [&](const char *phase, int frames, bool bypass, bool auxiliaryPresent = true)
            {
                require(frames > 0 && frames <= 256 && clock + frames <= capacity,
                        "Explicit admitted <=Q outer span, bounded preallocated history");
                audio.clear();
                detector.clear();
                for (int channel = 0; channel < 2; ++channel)
                    for (int n = 0; n < frames; ++n)
                    {
                        const auto main = mainInput(channel, clock + n);
                        const auto aux =
                            nativeDelay && auxiliaryPresent
                                ? auxiliaryInput(scenario.auxiliaryChannels == 1 ? 0 : channel, clock + n)
                                : 0.f;
                        audio.setSample(channel, offset + n, main);
                        mainHistory.setSample(channel, clock + n, main);
                        auxiliaryHistory.setSample(channel, clock + n, aux);
                        if (channel < detector.getNumChannels())
                            detector.setSample(channel, auxiliaryOffset + n, aux);
                    }
                const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
                const auto begin = juce::Time::getHighResolutionTicks();
                const auto process = [&]
                {
                    host.process(audio, offset, frames, nullptr, 0, clock / rate, 120, true, bypass, 4, 4,
                                 nullptr, 0, 0, true, nativeDelay && auxiliaryPresent ? &detector : nullptr,
                                 auxiliaryOffset);
                };
                if (offline)
                    process();
                else
                {
                    struct HeapProbe
                    {
                        bool previous = callbackAllocationProbe;
                        HeapProbe()
                        {
                            callbackAllocationProbe = true;
                        }
                        ~HeapProbe()
                        {
                            callbackAllocationProbe = previous;
                        }
                    } probe;
                    process();
                }
                const auto ms =
                    SyntheticCallbackTiming::milliseconds(begin, juce::Time::getHighResolutionTicks());
                const auto allocations = callbackAllocations.load() - allocated;
                const auto destructions = callbackDestructions.load() - deleted;
                double error = 0;
                bool exactEndpoints = true;
                for (int n = 0; n < frames; ++n)
                {
                    blend = bypass ? std::min(1., blend + step) : std::max(0., blend - step);
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        // Pure source-history oracle with exact fixture constants; no output calibration.
                        const int source = clock + n - (nativeDelay ? 543 : 512);
                        const float dry = source >= 0 ? mainHistory.getSample(channel, source) : 0.f;
                        const float wet = source < 0 ? 0.f
                                          : nativeDelay
                                              ? dry + .5f * auxiliaryHistory.getSample(channel, source)
                                              : .5f * dry + .125f;
                        const double expected = (1. - blend) * double(wet) + blend * double(dry);
                        const auto actual = audio.getSample(channel, offset + n);
                        capture.setSample(channel, clock + n, actual);
                        error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                      : std::numeric_limits<double>::infinity();
                        if (blend == 0 || blend == 1)
                            exactEndpoints &= actual == (blend == 0 ? wet : dry);
                    }
                }
                clock += frames;
                // Deliberate CONTROL scheduling between separate bounded spans, never a live DSP wait.
                dispatchMessages(6);
                host.service();
                const auto stats = host.statistics();
                const auto consumed = host.consumedLatency();
                const bool consumedCorrect =
                    clock <= 512 ? !consumed.has_value()
                                 : consumed.has_value() && consumed->stamp == declaration &&
                                       consumed->sequence == std::uint64_t((clock - 1) / 256 - 2);
                const bool ok =
                    error < 2.e-7 && exactEndpoints && consumedCorrect && host.packetFrame() == clock % 256 &&
                    stats.submitted - initial.submitted == std::uint64_t(clock / 256) &&
                    stats.processed == stats.submitted && stats.missed == initial.missed &&
                    stats.dropped == initial.dropped && !host.deliveryFailed() && host.alive() &&
                    host.processId() == pid && host.runtimeIdentity() == instance &&
                    host.midiResetGeneration() == reset && host.parameterRevision() == revision &&
                    host.latencySamples() == totalLatency && host.bypassTransitionSamples() == rampSamples &&
                    host.bypassBlend().has_value() && std::abs(*host.bypassBlend() - blend) < 1.e-6 &&
                    (offline || (allocations == 0 && destructions == 0));
                csv += juce::String(phase) + "," + juce::String(clock - frames) + "," + juce::String(frames) +
                       "," + juce::String(int(bypass)) + "," + juce::String(int(auxiliaryPresent)) + "," +
                       juce::String(host.packetFrame()) + "," + juce::String(juce::int64(stats.submitted)) +
                       "," + juce::String(juce::int64(stats.processed)) + "," +
                       juce::String(juce::int64(stats.missed)) + "," +
                       juce::String(juce::int64(stats.dropped)) + "," + juce::String(error, 12) + "," +
                       juce::String(int(exactEndpoints)) + "," + juce::String(ms, 6) + "," +
                       juce::String(allocations) + "," + juce::String(destructions) + "," +
                       (consumed ? juce::String(juce::int64(consumed->sequence)) : "null") + "\n";
                require(output.getChildFile("spans.csv").replaceWithText(csv),
                        "Keep actual observations before oracle");
                passed &= ok;
                require(ok,
                        "Independent512/543 wet+aligned dry5ms oracle, native state/ownership/heap/packets");
            };
            try
            {
                for (int n = 0; n < 8; ++n)
                    drive("startup_every_sample_checked", 256, scenario.initiallyBypassed);
                drive("partial127", 127, scenario.initiallyBypassed);
                drive("partial191", 64, scenario.initiallyBypassed);
                drive("partial255", 64, scenario.initiallyBypassed);
                drive("cross255_to64", 65, scenario.initiallyBypassed);
                drive("begin5ms", 31, !scenario.initiallyBypassed);
                drive("reverse_unfinished", 17, scenario.initiallyBypassed);
                drive("one_frame_reverse", 1, !scenario.initiallyBypassed);
                drive("two_frame_reverse", 2, scenario.initiallyBypassed);
                drive("settle_opposite", 256, !scenario.initiallyBypassed);
                drive("settled_endpoint", 127, !scenario.initiallyBypassed);
                for (int n = 0; n < 12; ++n)
                    drive("rapid_reversal", std::array<int, 6>{1, 3, 7, 13, 29, 67}[std::size_t(n % 6)],
                          (n & 1) != 0);
                drive("settle_dry", 256, true);
                drive("dry_exact", 193, true);
                drive("settle_wet", 256, false);
                drive("wet_exact", 193, false);
                if (nativeDelay)
                {
                    drive("missing_aux_partial", 127, false, false);
                    drive("missing_aux_to255", 128, false, false);
                    drive("missing_aux_cross255", 65, false, false);
                    for (int n = 0; n < 3; ++n)
                        drive("missing_aux_no_stale", 256, false, false);
                    for (int n = 0; n < 4; ++n)
                        drive("auxiliary_resumes_exact", 256, false);
                }
                if (clock % 256)
                    drive("complete_state_boundary", 256 - clock % 256, false);
                require(state() == baseline && host.parameterRevision() == revision &&
                            host.processId() == pid && host.runtimeIdentity() == instance,
                        "Bypass and missing detector preserve exact original native state and same worker");
            }
            catch (const std::exception &e)
            {
                passed = false;
                failure = e.what();
            }
            const auto raw = output.getChildFile("actual-planar-float32le.raw");
            require(!raw.exists(), "Preserve previous exact failed PCM");
            auto stream = raw.createOutputStream();
            require(bool(stream), "Owned raw effect PCM file");
            for (int channel = 0; channel < 2; ++channel)
                require(stream->write(capture.getReadPointer(channel), std::size_t(clock) * sizeof(float)),
                        "Actual original effect output bits");
            stream.reset();
            require(output.getChildFile("result.txt")
                        .replaceWithText("pass=" + juce::String(int(passed)) +
                                         ";frames=" + juce::String(clock) + ";error=" + failure +
                                         "\nOriginal SDK worker; explicit2Q+native0/31. Not graph, "
                                         "callback-deadline, installed-provider, "
                                         "hardware or listening qualification.\n"),
                    "Actual terminal effect experiment result");
            require(passed, "Explicit2Q original effect experiment failed; all observed evidence retained");
            (offline ? offlineFrames : liveFrames) = clock;
        }
        require(liveFrames == offlineFrames, "Original effect source schedules identical live/offline");
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < liveFrames; ++n)
                require(live.getSample(channel, n) == rendered.getSample(channel, n),
                        "Exact original sample-local effect live/offline bits at independently specified "
                        "latency");
    }
}
