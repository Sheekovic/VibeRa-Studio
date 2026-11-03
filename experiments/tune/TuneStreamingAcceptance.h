// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#if VIBERA_TUNE_RUBBERBAND
struct TuneStreamingResult
{
    int cases = 0, pitchFailures = 0, stereoFailures = 0, allocatingBlocks = 0, overruns = 0;
    int latencyFailures = 0, resetFailures = 0, liveLatencyFailures = 0;
    std::uint64_t messages = 0;
    double maxCents = 0, maxProcessMs = 0;
};

static TuneStreamingResult tuneStreamingTests(const std::filesystem::path &folder, std::ofstream &report)
{
    TuneStreamingResult result;
    std::ofstream matrix(folder / "streaming-static.csv", std::ios::trunc);
    matrix << "rate,kind,ratio,expectedHz,measuredHz,cents,stereoError,latencyFrames,providerMessages\n";
    int scenario = 0;
    for (int rate : {44100, 48000, 96000})
        for (int kind : {0, 1})
            for (float ratio : {.5f, .75f, 1.f, 1.5f, 2.f})
            {
                vibera::tune::RubberCandidate bridge;
                // A known initial ratio is prepared outside processing. Live
                // mutation during the first partial block is tested separately.
                bridge.presetDefault(2, float(rate), true, ratio);
                const int frames = ((3 * rate + 511) / 512) * 512;
                std::array<std::vector<float>, 2> input, output;
                for (int channel = 0; channel < 2; ++channel)
                {
                    input[size_t(channel)].resize(size_t(frames));
                    output[size_t(channel)].resize(size_t(frames));
                }
                for (int i = 0; i < frames; ++i)
                {
                    const double phase = 2 * std::acos(-1.) * 440 * i / rate;
                    input[0][size_t(i)] =
                        float(.3 * std::sin(phase) +
                              (kind ? .12 * std::sin(2 * phase) + .06 * std::sin(3 * phase) : 0));
                    input[1][size_t(i)] = -.5f * input[0][size_t(i)];
                }
                for (int offset = 0; offset < frames; offset += 512)
                {
                    const float *source[]{input[0].data() + offset, input[1].data() + offset};
                    float *destination[]{output[0].data() + offset, output[1].data() + offset};
                    vibera::tune::allocationTrace::context(rate, 3, scenario, offset);
                    allocations = 0;
                    counting = true;
                    const auto start = std::chrono::steady_clock::now();
                    bridge.process(source, 512, destination, 512);
                    const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                            .count();
                    counting = false;
                    result.allocatingBlocks += allocations != 0;
                    result.overruns += ms >= 512000. / rate;
                    result.maxProcessMs = std::max(result.maxProcessMs, ms);
                }
                bool finite = true;
                double stereo = 0;
                for (int i = 0; i < frames; ++i)
                {
                    finite =
                        finite && std::isfinite(output[0][size_t(i)]) && std::isfinite(output[1][size_t(i)]);
                    stereo =
                        std::max(stereo, std::abs(double(output[1][size_t(i)]) + .5 * output[0][size_t(i)]));
                }
                require(finite, "Direct static transpose produced non-finite PCM");
                const double measured = frequency(output[0], rate);
                const double cents = std::abs(1200 * std::log2(measured / (440 * ratio)));
                result.maxCents = std::max(result.maxCents, cents);
                result.pitchFailures += !(cents < 5);
                result.stereoFailures += !(stereo < 1e-4);
                result.messages += bridge.diagnostics().total;
                ++result.cases;
                matrix << rate << ',' << kind << ',' << ratio << ',' << 440 * ratio << ',' << measured << ','
                       << cents << ',' << stereo << ',' << bridge.inputLatency() + bridge.outputLatency()
                       << ',' << bridge.diagnostics().total << '\n';
                if (rate == 48000 && kind == 1 && ratio == .5f)
                {
                    wav(folder / "octave-down-reference.wav", input, rate);
                    wav(folder / "octave-down-corrected.wav", output, rate);
                }
                ++scenario;
            }
    require(bool(matrix), "Direct transpose matrix write failed");

    // Non-silent dynamic inputs retain a known stereo image. Unlike the previous
    // silence stress, this checks actual processing output throughout transitions.
    for (int rate : {44100, 48000, 96000})
    {
        vibera::tune::RubberCandidate bridge;
        bridge.presetDefault(2, float(rate), true);
        const auto preparedDelay = bridge.inputLatency() + bridge.outputLatency();
        std::array<float, 512> input{}, inputRight{}, left{}, right{};
        const float *source[]{input.data(), inputRight.data()};
        float *destination[]{left.data(), right.data()};
        bridge.process(source, 512, destination, 512); // Neutral preparation only.
        double stereo = 0, maximum = 0, energy = 0;
        int allocating = 0, overruns = 0;
        uint32_t random = 0x56494245;
        std::ofstream trace(folder / ("streaming-transition-" + std::to_string(rate) + ".csv"),
                            std::ios::trunc);
        trace << "block,ratio,wallMs,allocations,providerMessages,stereoError\n";
        for (int block = 0; block < 512; ++block)
        {
            for (size_t i = 0; i < input.size(); ++i)
            {
                const double phase = 2 * std::acos(-1.) * 220 * (block * 512 + int(i)) / rate;
                input[i] = float(.3 * std::sin(phase) + .1 * std::sin(phase * 2));
                inputRight[i] = -.5f * input[i];
            }
            random = random * 1664525u + 1013904223u;
            const float ratio = block % 8 == 0   ? .5f
                                : block % 8 == 1 ? 2.f
                                : block % 8 == 2 ? 1.f
                                                 : .5f + 1.5f * float(random >> 8) / 16777215.f;
            const auto before = bridge.diagnostics().total;
            vibera::tune::allocationTrace::context(rate, 4, block, block * 512);
            allocations = 0;
            counting = true;
            const auto start = std::chrono::steady_clock::now();
            bridge.setTransposeFactor(ratio);
            bridge.process(source, 512, destination, 512);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            counting = false;
            require(bridge.inputLatency() + bridge.outputLatency() == preparedDelay,
                    "Live ratios must not change the prepared latency declaration");
            allocating += allocations != 0;
            overruns += ms >= 512000. / rate;
            maximum = std::max(maximum, ms);
            double blockStereo = 0;
            bool finite = true;
            for (size_t i = 0; i < left.size(); ++i)
            {
                finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
                blockStereo = std::max(blockStereo, std::abs(double(right[i]) + .5 * left[i]));
                energy += double(left[i]) * left[i];
            }
            require(finite, "Dynamic transpose produced non-finite PCM");
            stereo = std::max(stereo, blockStereo);
            trace << block << ',' << ratio << ',' << ms << ',' << allocations << ','
                  << bridge.diagnostics().total - before << ',' << blockStereo << '\n';
        }
        require(bool(trace) && energy > 1, "Dynamic transpose must produce actual non-silent output");
        result.allocatingBlocks += allocating;
        result.overruns += overruns;
        result.maxProcessMs = std::max(result.maxProcessMs, maximum);
        result.stereoFailures += !(stereo < 1e-4);
        result.messages += bridge.diagnostics().total;
        report << "harmonicTransition rate=" << rate << " blocks=512 allocations=" << allocating
               << " overruns=" << overruns << " maxProcessMs=" << maximum << " stereoError=" << stereo
               << " providerMessages=" << bridge.diagnostics().total << " energy=" << energy << '\n';
    }
    std::ofstream impulses(folder / "streaming-impulses.csv", std::ios::trunc);
    impulses << "rate,ratio,declaredDelay,peakFrame,peakAmplitude,frameError,resetEqual,messages\n";
    for (int rate : {44100, 48000, 96000})
        for (float ratio : {.5f, .75f, 1.f, 1.5f, 2.f})
        {
            vibera::tune::RubberCandidate bridge;
            bridge.presetDefault(2, float(rate), true, ratio);
            const int delay = bridge.inputLatency() + bridge.outputLatency();
            const int frames = ((delay + 4096 + 511) / 512) * 512;
            std::array<std::vector<float>, 2> input, output;
            for (int c = 0; c < 2; ++c)
            {
                input[size_t(c)].resize(size_t(frames));
                output[size_t(c)].resize(size_t(frames));
            }
            input[0][0] = .7f;
            input[1][0] = -.2f;
            const auto render = [&]
            {
                constexpr std::array<int, 5> sizes{127, 256, 17, 512, 511};
                int index = 0;
                for (int offset = 0; offset < frames;)
                {
                    const int count = std::min(sizes[size_t(index++ % sizes.size())], frames - offset);
                    const float *source[]{input[0].data() + offset, input[1].data() + offset};
                    float *destination[]{output[0].data() + offset, output[1].data() + offset};
                    allocations = 0;
                    counting = true;
                    bridge.process(source, count, destination, count);
                    counting = false;
                    result.allocatingBlocks += allocations != 0;
                    require(bridge.inputLatency() + bridge.outputLatency() == delay,
                            "Variable blocks preserve prepared latency");
                    offset += count;
                }
            };
            render();
            const auto first = output;
            const auto peak = size_t(std::max_element(output[0].begin(), output[0].end(), [](float a, float b)
                                                      { return std::abs(a) < std::abs(b); }) -
                                     output[0].begin());
            const int error = std::abs(int(peak) - delay);
            result.latencyFailures += error > (ratio == 1 ? 0 : 1) || std::abs(output[0][peak]) < 1e-5;
            const auto peakValue = output[0][peak];
            bridge.reset(); // Control-thread operation, restores the prepared starting ratio.
            for (auto &channel : output)
                std::fill(channel.begin(), channel.end(), 0);
            render();
            const bool resetEqual = output == first;
            result.resetFailures += !resetEqual;
            result.messages += bridge.diagnostics().total;
            impulses << rate << ',' << ratio << ',' << delay << ',' << peak << ',' << peakValue << ','
                     << error << ',' << resetEqual << ',' << bridge.diagnostics().total << '\n';
            wav(folder / ("impulse-" + std::to_string(rate) + "-" + std::to_string(ratio) + ".wav"), first,
                rate);
        }
    require(bool(impulses), "Impulse latency/reset receipt write failed");
    report << "impulseLatency cases=15 exactUnity=required nonUnityRoundingToleranceFrames=1 failures="
           << result.latencyFailures << " resetFailures=" << result.resetFailures << '\n';

    std::ofstream liveImpulses(folder / "streaming-live-impulses.csv", std::ios::trunc);
    liveImpulses << "rate,ratio,onset,preparedDelay,peakFrame,measuredDelay,frameError,messages\n";
    for (int rate : {44100, 48000, 96000})
        for (float ratio : {.5f, .75f, 1.f, 1.5f, 2.f})
        {
            vibera::tune::RubberCandidate bridge;
            bridge.presetDefault(2, float(rate), true);
            const int delay = bridge.inputLatency() + bridge.outputLatency();
            const int onset = ((rate / 2 + 511) / 512) * 512;
            const int frames = onset + ((delay + 8192 + 511) / 512) * 512;
            std::array<std::vector<float>, 2> input, output;
            for (int c = 0; c < 2; ++c)
            {
                input[size_t(c)].resize(size_t(frames));
                output[size_t(c)].resize(size_t(frames));
            }
            input[0][size_t(onset)] = .7f;
            input[1][size_t(onset)] = -.2f;
            for (int offset = 0; offset < frames; offset += 512)
            {
                const float *source[]{input[0].data() + offset, input[1].data() + offset};
                float *destination[]{output[0].data() + offset, output[1].data() + offset};
                allocations = 0;
                counting = true;
                if (offset == 512)
                    bridge.setTransposeFactor(ratio);
                bridge.process(source, 512, destination, 512);
                counting = false;
                result.allocatingBlocks += allocations != 0;
                require(bridge.inputLatency() + bridge.outputLatency() == delay,
                        "Live impulse retains prepared delay declaration");
            }
            const auto peak = size_t(std::max_element(output[0].begin(), output[0].end(), [](float a, float b)
                                                      { return std::abs(a) < std::abs(b); }) -
                                     output[0].begin());
            const int measured = int(peak) - onset;
            const int error = std::abs(measured - delay);
            result.liveLatencyFailures += error > (ratio == 1 ? 0 : 1) || std::abs(output[0][peak]) < 1e-5;
            result.messages += bridge.diagnostics().total;
            liveImpulses << rate << ',' << ratio << ',' << onset << ',' << delay << ',' << peak << ','
                         << measured << ',' << error << ',' << bridge.diagnostics().total << '\n';
        }
    require(bool(liveImpulses), "Live impulse alignment receipt write failed");
    report << "liveImpulseLatency cases=15 failures=" << result.liveLatencyFailures << '\n';

    for (int rate : {44100, 48000, 96000})
    {
        vibera::tune::RubberCandidate bridge;
        bridge.presetDefault(2, float(rate), true);
        const int delay = bridge.inputLatency() + bridge.outputLatency();
        std::array<float, 127> input{}, inputRight{}, left{}, right{};
        const float *source[]{input.data(), inputRight.data()};
        float *destination[]{left.data(), right.data()};
        int allocating = 0, overruns = 0;
        double maximum = 0, energy = 0;
        const int chunks = (delay + 8192 + 126) / 127;
        for (int block = 0; block < chunks; ++block)
        {
            for (size_t i = 0; i < input.size(); ++i)
            {
                input[i] = float(.3 * std::sin(2 * std::acos(-1.) * 440 * (block * 127 + int(i)) / rate));
                inputRight[i] = -.5f * input[i];
            }
            allocations = 0;
            counting = true;
            const auto start = std::chrono::steady_clock::now();
            bridge.setTransposeFactor(block % 2 ? .5f : 2.f);
            bridge.process(source, 127, destination, 127);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            counting = false;
            allocating += allocations != 0;
            overruns += ms >= 127000. / rate;
            maximum = std::max(maximum, ms);
            require(bridge.inputLatency() + bridge.outputLatency() == delay,
                    "Early partial-block target updates retain prepared delay");
            for (float sample : left)
                energy += double(sample) * sample;
        }
        require(energy > .01, "Startup target edits must retain non-silent output at every sample rate");
        result.allocatingBlocks += allocating;
        result.overruns += overruns;
        result.maxProcessMs = std::max(result.maxProcessMs, maximum);
        result.messages += bridge.diagnostics().total;
        report << "earlyTargetChanges rate=" << rate << " chunks=" << chunks
               << " size=127 allocations=" << allocating << " overruns=" << overruns
               << " maxProcessMs=" << maximum << " providerMessages=" << bridge.diagnostics().total
               << " energy=" << energy << '\n';
    }

    report << "streamingStatic cases=" << result.cases << " maxCents=" << result.maxCents
           << " pitchFailures=" << result.pitchFailures << " stereoFailures=" << result.stereoFailures
           << " allocatingBlocks=" << result.allocatingBlocks << " overruns=" << result.overruns
           << " providerMessages=" << result.messages << " maxProcessMs=" << result.maxProcessMs << '\n';
    return result;
}
#endif
