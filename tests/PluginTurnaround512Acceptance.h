// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Include after require / parameterCatalogue / parameterResult / callback probes.
// Independent ORIGINAL real-worker2Q experiment. Does not edit old256 oracles.
__declspec(noinline) static void pluginTurnaround512Tests(const juce::File &folder, const PluginSpec &spec)
{
    require(spec.instrument && spec.name == "VibeRa Instrument Runtime Fixture",
            "Explicit genuine scanned original SDK instrument; no installed-DSP compatibility claim");
    require(folder.createDirectory().wasOk(), "Owned fixed512 worker experiment evidence");
    constexpr int quantum = 256, transport = 512;
    constexpr double rate = 48000;
    juce::AudioBuffer<float> live(2, 8192), rendered(2, 8192);
    live.clear();
    rendered.clear();
    int liveFrames = 0, renderFrames = 0;
    for (bool offline : {false, true})
    {
        const auto output = folder.getChildFile(offline ? "offline" : "live");
        require(!output.exists() && output.createDirectory().wasOk(), "Fresh attempt keeps failed receipts");
        PluginHost host(PluginHost::TransportPolicy::twoQuanta);
        require(host.start(spec, rate, quantum, offline).wasOk(),
                "Actual original SDK worker with unchanged256 quantum");
        require(host.transportPolicy() == PluginHost::TransportPolicy::twoQuanta &&
                    host.processingQuantum() == quantum && host.maximumLiveSpan() == quantum &&
                    host.transportLatencySamples() == transport && host.latencySamples() == transport &&
                    host.declaredLatency().nativeSamples == 0,
                "Explicit independently specified512 transport declaration, not a fitted output shift");
        auto metadata = parameterCatalogue(host);
        require(parameterResult(host, host.setParameter(77, .25f, metadata.generation)).result == 0,
                "Actual acknowledged original native gain before processing");
        metadata = parameterCatalogue(host, true);
        require(nativeParameter(metadata, 77).value == .25f, "Native manual gain readback");
        const auto pid = host.processId();
        const auto instance = host.runtimeIdentity();
        const auto initial = host.statistics();
        const auto reset = host.midiResetGeneration();
        juce::AudioBuffer<float> buffer(2, quantum);
        auto &capture = offline ? rendered : live;
        std::array<unsigned, 32> parametersPerQuantum{};
        int clock = 0, on = -1, off = -1;
        bool passed = true;
        juce::String csv = "phase,inputFrame,frames,cursorBefore,cursorAfter,submitted,processed,missed,"
                           "dropped,applied,rejected,maxError,hostSpanMs,new,delete\n";
        juce::String failure;
        const auto drive =
            [&](const char *phase, int frames, const ipc::MidiEvent *midi = nullptr, int kind = 0)
        {
            require(frames > 0 && frames <= quantum && clock + frames <= capture.getNumSamples(),
                    "Prepared bounded spans");
            buffer.clear();
            std::array<ipc::ParameterEvent, 1> events{};
            int count = 0;
            const int cursor = clock % quantum;
            if (kind == 0)
            {
                const int boundary = cursor == 0 ? 0 : quantum - cursor;
                if (boundary < frames)
                {
                    events[0] = {77, boundary, .25f};
                    count = 1;
                    ++parametersPerQuantum[std::size_t((clock + boundary) / quantum)];
                }
            }
            else if (kind == 1)
            {
                require(cursor == 0 && frames == 1, "Exact original atomic prefix setup");
                events[0] = {77, 0, .5f};
                count = 1;
                ++parametersPerQuantum[std::size_t(clock / quantum)];
            }
            else if (kind == 2)
            {
                require(cursor == 1 && frames == 255, "Exact invalid suffix within SAME logical quantum");
                events[0] = {77, frames, .75f};
                count = 1; // Unreachable: whole quantum must reject prefix too.
                parametersPerQuantum[std::size_t(clock / quantum)] = 0;
            }
            const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
            const auto began = juce::Time::getHighResolutionTicks();
            if (offline)
                host.process(buffer, 0, frames, midi, midi ? 1 : 0, clock / rate, 120, true, false, 4, 4,
                             events.data(), count, metadata.generation);
            else
            {
                struct Probe
                {
                    bool before = callbackAllocationProbe;
                    Probe()
                    {
                        callbackAllocationProbe = true;
                    }
                    ~Probe()
                    {
                        callbackAllocationProbe = before;
                    }
                } probe;
                host.process(buffer, 0, frames, midi, midi ? 1 : 0, clock / rate, 120, true, false, 4, 4,
                             events.data(), count, metadata.generation);
            }
            const auto ms =
                SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
            const auto allocations = callbackAllocations.load() - allocated,
                       destructions = callbackDestructions.load() - deleted;
            double error = 0;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < frames; ++n)
                {
                    const int source =
                        clock + n - 512; // Independent literal contract, NEVER getter/calibration.
                    const bool sounding = on >= 0 && source >= on && (off < 0 || source < off);
                    const double expected = !sounding ? 0
                                            : c == 0  ? .25 * (61. / 512 + 1. / 4096)
                                                      : -.25 * (68. / 512 + 1. / 8192);
                    const auto actual = buffer.getSample(c, n);
                    capture.setSample(c, clock + n, actual);
                    error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                                  : std::numeric_limits<double>::infinity();
                }
            clock += frames;
            dispatchMessages(6);
            host.service(); // CONTROL-only scheduling, never worker wait inside live process.
            const auto stats = host.statistics();
            unsigned expectedParameters = 0;
            for (int q = 0; q < clock / quantum; ++q)
                expectedParameters += parametersPerQuantum[std::size_t(q)];
            const bool ok = error == 0 && host.packetFrame() == clock % quantum &&
                            stats.submitted - initial.submitted == std::uint64_t(clock / quantum) &&
                            stats.processed == stats.submitted && stats.missed == initial.missed &&
                            stats.dropped == initial.dropped &&
                            stats.parametersApplied - initial.parametersApplied == expectedParameters &&
                            (offline || (allocations == 0 && destructions == 0)) && host.alive() &&
                            host.processId() == pid && host.runtimeIdentity() == instance &&
                            host.midiResetGeneration() == reset;
            csv += juce::String(phase) + "," + juce::String(clock - frames) + "," + juce::String(frames) +
                   "," + juce::String(cursor) + "," + juce::String(host.packetFrame()) + "," +
                   juce::String(juce::int64(stats.submitted)) + "," +
                   juce::String(juce::int64(stats.processed)) + "," +
                   juce::String(juce::int64(stats.missed)) + "," + juce::String(juce::int64(stats.dropped)) +
                   "," + juce::String(juce::int64(stats.parametersApplied)) + "," +
                   juce::String(juce::int64(stats.parametersRejected)) + "," + juce::String(error, 12) + "," +
                   juce::String(ms, 6) + "," + juce::String(allocations) + "," + juce::String(destructions) +
                   "\n";
            require(output.getChildFile("spans.csv").replaceWithText(csv),
                    "Keep every real worker span before assertion");
            passed &= ok;
            require(
                ok,
                "Exact independent512/native packets/parameter/owner/heap oracle; failed evidence retained");
        };
        try
        {
            for (int n = 0; n < 8; ++n)
                drive("startup_all_PCM_checked", 256);
            ipc::MidiEvent noteOn{0, {0x90, 60, 127}, 3}, noteOff{0, {0x80, 60, 0}, 3};
            on = clock;
            drive("one_native_on_partial", 127, &noteOn);
            drive("cursor127_to191", 64);
            drive("cursor191_to255", 64);
            drive("old_failure_boundary255_to64",
                  65); // Previously forced immediate just-submitted consumption.
            drive("independent512_onset", 129);
            drive("held_native_continuity", 127);
            if (clock % 256)
                drive("align_for_atomic_negative", 256 - clock % 256);
            const auto rejected = host.statistics().parametersRejected;
            drive("valid_prefix_not_yet_native", 1, nullptr, 1);
            drive("malformed_suffix_rejects_whole_Q", 255, nullptr, 2);
            for (int n = 0; n < 3; ++n)
                drive("atomic_prefix_never_changes_native_PCM", 256);
            require(host.statistics().parametersRejected == rejected + 2,
                    "Unchanged exact whole-Q rejection: earlier valid prefix plus malformed suffix");
            off = clock;
            drive("one_native_off_partial", 127, &noteOff);
            drive("off_partial_crossing", 64);
            drive("off_cursor255_crossing", 65);
            for (int n = 0; n < 4; ++n)
                drive("release_and_fixed512_tail", 256);
            const auto counters = parameterCatalogue(host, true);
            require(nativeParameter(counters, 90).value == 1.f / 1024 &&
                        nativeParameter(counters, 91).value == 1.f / 1024 &&
                        nativeParameter(counters, 77).value == .25f,
                    "Actual original SDK one on/off and no early invalid-prefix execution");
            if (!offline)
            {
                juce::AudioBuffer<float> oversized(2, 257);
                for (int c = 0; c < 2; ++c)
                    std::fill_n(oversized.getWritePointer(c), 257, .125f);
                const auto before = host.statistics();
                const auto cursor = host.packetFrame();
                host.process(oversized, 0, 257, nullptr, 0, clock / rate, 120, true, false);
                require(host.packetFrame() == cursor && host.statistics().submitted == before.submitted &&
                            host.automationStatistics().last == PluginHost::AutomationStatus::invalidTiming &&
                            oversized.getSample(0, 0) == .125f && oversized.getSample(1, 256) == .125f,
                        "Unsupported live>Q span explicitly refuses before packet mutation");
            }
        }
        catch (const std::exception &e)
        {
            callbackAllocationProbe = false;
            passed = false;
            failure = e.what();
        }
        const auto raw = output.getChildFile("actual-planar-float32le.raw");
        require(!raw.exists(), "Never overwrite failed exact PCM");
        auto stream = raw.createOutputStream();
        require(bool(stream), "Owned raw PCM");
        for (int c = 0; c < 2; ++c)
            require(stream->write(capture.getReadPointer(c), std::size_t(clock) * sizeof(float)),
                    "Exact observed PCM bits");
        stream.reset();
        require(output.getChildFile("result.txt")
                    .replaceWithText("pass=" + juce::String(int(passed)) + ";frames=" + juce::String(clock) +
                                     ";error=" + failure +
                                     "\nDirect original-worker spans; not native engine callback/deadline, "
                                     "hardware or listening qualification.\n"),
                "Actual terminal experiment evidence");
        require(passed, "New independent2Q transport experiment failed; old256 tests remain unchanged");
        (offline ? renderFrames : liveFrames) = clock;
    }
    require(liveFrames == renderFrames, "Identical independent live/offline source spans");
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < liveFrames; ++n)
            require(live.getSample(c, n) == rendered.getSample(c, n),
                    "Original sample-local SDK exact offline/live equivalence at independent512 latency");
}
