// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TuneCorrection.h"
#include "TuneAllocationTrace.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

static thread_local bool counting = false;
static thread_local size_t allocations = 0;
static thread_local size_t deallocations = 0, resourceAllocations = 0, resourceFrees = 0;
extern "C" void vibera_tune_resource_event(int operation, size_t bytes) noexcept
{
    if (!counting)
        return;
    if (operation == 1)
        ++resourceAllocations;
    else
        ++resourceFrees;
    vibera::tune::allocationTrace::record(bytes,
                                          operation == 1 ? "RubberBand allocate" : "RubberBand deallocate");
}
void *operator new(size_t size)
{
    if (counting)
    {
        ++allocations;
        vibera::tune::allocationTrace::record(size);
    }
    if (auto *value = std::malloc(size ? size : 1))
        return value;
    throw std::bad_alloc();
}
void *operator new[](size_t size)
{
    return ::operator new(size);
}
void operator delete(void *value) noexcept
{
    if (counting && value)
        ++deallocations;
    std::free(value);
}
void operator delete[](void *value) noexcept
{
    ::operator delete(value);
}
void operator delete(void *value, size_t) noexcept
{
    ::operator delete(value);
}
void operator delete[](void *value, size_t) noexcept
{
    ::operator delete(value);
}

static int checks = 0;
static void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
}
static double frequency(const std::vector<float> &pcm, double rate)
{
    constexpr int n = 32768;
    signalsmith::linear::RealFFT<float> fft(n);
    std::vector<float> window(n);
    std::vector<std::complex<float>> spectrum(n / 2);
    const size_t offset = pcm.size() - n - 256;
    for (int i = 0; i < n; ++i)
        window[size_t(i)] =
            pcm[offset + size_t(i)] * float(.5 - .5 * std::cos(2 * std::acos(-1.) * i / (n - 1)));
    fft.fft(window.data(), spectrum.data());
    int best = 1;
    for (int i = 2; i < n / 2 - 1; ++i)
        if (std::norm(spectrum[size_t(i)]) > std::norm(spectrum[size_t(best)]))
            best = i;
    const double l = std::log(std::abs(spectrum[size_t(best - 1)]) + 1e-20);
    const double c = std::log(std::abs(spectrum[size_t(best)]) + 1e-20);
    const double r = std::log(std::abs(spectrum[size_t(best + 1)]) + 1e-20);
    return (best + .5 * (l - r) / (l - 2 * c + r)) * rate / n;
}
static void wav(const std::filesystem::path &path, const std::array<std::vector<float>, 2> &pcm, int rate)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto number = [&](uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            out.put(char((value >> (8 * i)) & 255));
    };
    const uint32_t size = uint32_t(pcm[0].size() * 4);
    out.write("RIFF", 4);
    number(size + 36, 4);
    out.write("WAVEfmt ", 8);
    number(16, 4);
    number(1, 2);
    number(2, 2);
    number(uint32_t(rate), 4);
    number(uint32_t(rate * 4), 4);
    number(4, 2);
    number(16, 2);
    out.write("data", 4);
    number(size, 4);
    for (size_t i = 0; i < pcm[0].size(); ++i)
        for (int channel = 0; channel < 2; ++channel)
            number(uint16_t(int16_t(std::lrint(std::clamp(pcm[size_t(channel)][i], -1.f, 1.f) * 32767))), 2);
    require(bool(out), "WAV write failed");
}
#include "TuneStreamingAcceptance.h"

int main(int argc, char **argv)
{
    std::ofstream report;
    try
    {
        require(argc == 2 || (argc == 3 && std::string(argv[2]) == "--known-pitch"),
                "Supply evidence directory and optional --known-pitch");
        const bool knownPitch = argc == 3;
        vibera::tune::allocationTrace::configure();
        const std::filesystem::path folder(argv[1]);
        std::filesystem::create_directories(folder);
        report.open(folder / "verification.txt", std::ios::trunc);
        require(bool(report), "Report unavailable");
        double maxCents = 0, maxProcessMs = 0, maxAnalysisMs = 0;
        int overruns = 0, cases = 0, pitchFailures = 0, stereoFailures = 0, allocatingBlocks = 0;
        int latencyFailures = 0, resetFailures = 0, liveLatencyFailures = 0;
        std::uint64_t providerMessages = 0;
        for (int rate : {44100, 48000, 96000})
            for (int kind = 0; kind < 2; ++kind)
                for (int scenario = 0; scenario < 6; ++scenario)
                {
                    const double target = scenario == 4 ? 293.664767917
                                          : scenario == 5
                                              ? 442.
                                              : std::array<double, 4>{110, 220, 440, 880}[size_t(scenario)];
                    const double sourceHz = scenario == 4
                                                ? 277.182630977
                                                : target * std::exp2((scenario % 2 ? -36. : 32.) / 1200.);
                    vibera::tune::Detector detector;
                    vibera::tune::Correction effect;
                    require(detector.prepare(rate) && effect.prepare(rate), "Preparation failed");
                    vibera::tune::CorrectionParameters settings;
                    settings.speedMs = 0;
                    if (scenario == 4)
                        settings.targets.mask = 0xab5;
                    if (scenario == 5)
                        settings.targets.referenceA = 442;
                    require(effect.setParameters(settings), "Valid parameters rejected");
                    const int frames = rate * 3, observation = rate == 96000 ? 4096 : 2048;
                    std::array<std::vector<float>, 2> input, output;
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        input[size_t(channel)].resize(size_t(frames));
                        output[size_t(channel)].resize(size_t(frames));
                    }
                    for (int i = 0; i < frames; ++i)
                    {
                        const double phase = 2 * std::acos(-1.) * sourceHz * i / rate;
                        const float value =
                            float(.3 * std::sin(phase) +
                                  (kind ? .12 * std::sin(phase * 2) + .06 * std::sin(phase * 3) : 0));
                        input[0][size_t(i)] = value;
                        input[1][size_t(i)] = -.5f * value;
                    }
                    double caseProcessMs = 0, stereoError = 0, steadyStereoError = 0;
                    size_t caseAllocations = 0;
                    for (int offset = 0; offset < frames; offset += 256)
                    {
                        const int count = std::min(256, frames - offset);
                        if (offset >= observation && offset % 1024 == 0)
                        {
                            const auto start = std::chrono::steady_clock::now();
                            const auto pitch = detector.analyze(std::span<const float>(
                                input[0].data() + offset - observation, size_t(observation)));
                            maxAnalysisMs =
                                std::max(maxAnalysisMs, std::chrono::duration<double, std::milli>(
                                                            std::chrono::steady_clock::now() - start)
                                                            .count());
                            effect.setPitch(knownPitch ? vibera::tune::Pitch{sourceHz, 1} : pitch);
                        }
                        const float *in[]{input[0].data() + offset, input[1].data() + offset};
                        float *out[]{output[0].data() + offset, output[1].data() + offset};
                        allocations = 0;
                        vibera::tune::allocationTrace::context(rate, kind, scenario, offset);
                        counting = true;
                        const auto start = std::chrono::steady_clock::now();
                        const bool processed = effect.process(in, out, count);
                        const double ms = std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - start)
                                              .count();
                        counting = false;
                        require(processed, "Prepared shifter rejected block");
                        caseAllocations += allocations;
                        if (allocations)
                            ++allocatingBlocks;
                        caseProcessMs = std::max(caseProcessMs, ms);
                        if (ms >= count * 1000. / rate)
                            ++overruns;
                    }
                    bool finite = true;
                    for (int i = 0; i < frames; ++i)
                    {
                        finite = finite && std::isfinite(output[0][size_t(i)]) &&
                                 std::isfinite(output[1][size_t(i)]);
                        stereoError = std::max(
                            stereoError, std::abs(double(output[1][size_t(i)]) + .5 * output[0][size_t(i)]));
                        if (i >= rate)
                            steadyStereoError =
                                std::max(steadyStereoError,
                                         std::abs(double(output[1][size_t(i)]) + .5 * output[0][size_t(i)]));
                    }
                    require(finite, "Non-finite correction");
                    const double measured = frequency(output[0], rate);
                    const double cents = std::abs(1200 * std::log2(measured / target));
                    maxCents = std::max(maxCents, cents);
                    maxProcessMs = std::max(maxProcessMs, caseProcessMs);
                    ++cases;
                    report << "knownPitch=" << knownPitch << " rate=" << rate << " kind=" << kind
                           << " source=" << sourceHz << " target=" << target << " measured=" << measured
                           << " cents=" << cents << " stereoError=" << stereoError
                           << " steadyStereoError=" << steadyStereoError
                           << " latencyFrames=" << effect.latency()
                           << " latencyMs=" << effect.latency() * 1000. / rate
                           << " maxProcessMs=" << caseProcessMs
                           << " appliedSemitones=" << effect.correctionSemitones()
                           << " allocations=" << caseAllocations
                           << " providerMessages=" << effect.diagnosticCount() << '\n';
                    providerMessages += effect.diagnosticCount();
                    if (!(cents < 5))
                        ++pitchFailures;
                    if (!(stereoError < 1e-4))
                        ++stereoFailures;
                    if (rate == 48000 && kind == 1 && scenario == 2)
                    {
                        wav(folder / "original-detuned-harmonic.wav", input, rate);
                        wav(folder / "corrected-harmonic.wav", output, rate);
                    }
                }
#if VIBERA_TUNE_RUBBERBAND
        // Isolate prepared phase capacity during later ratio changes. Initial
        // neutral processing is prepared before counting; startup remains a
        // separate gate. This is not a whole-allocator or vocal-quality audit.
        for (int stressRate : {44100, 48000, 96000})
        {
            vibera::tune::RubberCandidate bridge;
            bridge.presetDefault(2, float(stressRate), true);
            std::array<float, 512> input{}, left{}, right{};
            const float *source[]{input.data(), input.data()};
            float *destination[]{left.data(), right.data()};
            bridge.process(source, 512, destination, 512);
            size_t stressAllocations = 0;
            int stressOverruns = 0, stressAllocatingBlocks = 0;
            double stressMaximumMs = 0;
            uint32_t random = 0x56494245;
            std::ofstream stressTrace(folder / ("ratio-stress-" + std::to_string(stressRate) + ".csv"),
                                      std::ios::trunc);
            stressTrace << "block,ratio,wallMs,threadCycles,allocations,overrun,providerMessages\n";
            for (int i = 0; i < 512; ++i)
            {
                random = random * 1664525u + 1013904223u;
                const float ratio = i % 8 == 0   ? .5f
                                    : i % 8 == 1 ? 2.f
                                    : i % 8 == 2 ? 1.f
                                                 : .5f + 1.5f * float(random >> 8) / 16777215.f;
                vibera::tune::allocationTrace::context(stressRate, 2, i, i * 512);
#if defined(_WIN32)
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore);
#endif
                allocations = 0;
                const auto previousMessages = bridge.diagnostics().total;
                counting = true;
                const auto start = std::chrono::steady_clock::now();
                bridge.setTransposeFactor(ratio);
                bridge.process(source, 512, destination, 512);
                const auto ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
                counting = false;
                unsigned long long cycles = 0;
#if defined(_WIN32)
                if (QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) && cyclesAfter >= cyclesBefore)
                    cycles = cyclesAfter - cyclesBefore;
#endif
                stressAllocations += allocations;
                stressAllocatingBlocks += allocations != 0;
                stressOverruns += ms >= 512000. / stressRate;
                stressMaximumMs = std::max(stressMaximumMs, ms);
                stressTrace << i << ',' << ratio << ',' << ms << ',' << cycles << ',' << allocations << ','
                            << (ms >= 512000. / stressRate) << ','
                            << bridge.diagnostics().total - previousMessages << '\n';
                bool finite = true;
                for (size_t n = 0; n < left.size(); ++n)
                    finite = finite && std::isfinite(left[n]) && std::isfinite(right[n]);
                require(finite, "Prepared ratio stress produced non-finite output");
            }
            report << "preparedRatioStress rate=" << stressRate << " blocks=512 range=0.5..2"
                   << " allocations=" << stressAllocations << " overruns=" << stressOverruns
                   << " maxProcessMs=" << stressMaximumMs
                   << " providerMessages=" << bridge.diagnostics().total << '\n';
            const auto &diagnostics = bridge.diagnostics();
            providerMessages += diagnostics.total;
            std::ofstream providerTrace(
                folder / ("ratio-stress-" + std::to_string(stressRate) + "-provider.txt"), std::ios::trunc);
            providerTrace << "total=" << diagnostics.total << " retained=" << diagnostics.retained
                          << " dropped=" << diagnostics.dropped << '\n';
            for (size_t i = 0; i < diagnostics.retained; ++i)
            {
                const auto &entry = diagnostics.entries[i];
                providerTrace << entry.message.data() << " values=" << entry.values << ' ' << entry.first
                              << ' ' << entry.second << '\n';
            }
            require(bool(providerTrace), "Prepared provider diagnostic write failed");
            require(bool(stressTrace), "Ratio stress trace write failed");
            allocatingBlocks += stressAllocatingBlocks;
            overruns += stressOverruns;
            maxProcessMs = std::max(maxProcessMs, stressMaximumMs);
        }
#endif
        // Broader streaming gates are independent of detection/target selection.
#if VIBERA_TUNE_RUBBERBAND
        const auto streaming = tuneStreamingTests(folder, report);
        cases += streaming.cases;
        pitchFailures += streaming.pitchFailures;
        stereoFailures += streaming.stereoFailures;
        allocatingBlocks += streaming.allocatingBlocks;
        overruns += streaming.overruns;
        providerMessages += streaming.messages;
        latencyFailures += streaming.latencyFailures;
        resetFailures += streaming.resetFailures;
        liveLatencyFailures += streaming.liveLatencyFailures;
        maxCents = std::max(maxCents, streaming.maxCents);
        maxProcessMs = std::max(maxProcessMs, streaming.maxProcessMs);
#endif
        {
            vibera::tune::Correction effect;
            require(effect.prepare(48000), "Bypass preparation");
            vibera::tune::CorrectionParameters parameters;
            parameters.bypass = true;
            require(effect.setParameters(parameters), "Bypass parameters");
            const int frames = effect.latency() + 512;
            std::array<std::vector<float>, 2> in, out;
            for (int channel = 0; channel < 2; ++channel)
            {
                in[size_t(channel)].resize(size_t(frames));
                out[size_t(channel)].resize(size_t(frames));
            }
            in[0][0] = .7f;
            in[1][0] = -.2f;
            for (int offset = 0; offset < frames; offset += 127)
            {
                const float *source[]{in[0].data() + offset, in[1].data() + offset};
                float *destination[]{out[0].data() + offset, out[1].data() + offset};
                require(effect.process(source, destination, std::min(127, frames - offset)),
                        "Variable block bypass");
            }
            for (int channel = 0; channel < 2; ++channel)
            {
                bool exact = true;
                for (int i = 0; i < frames; ++i)
                    exact = exact && out[size_t(channel)][size_t(i)] ==
                                         (i == effect.latency() ? in[size_t(channel)][0] : 0);
                require(exact, "Bypass impulse delay/channel separation");
            }
            require(effect.prepare(48000), "Wet impulse reprepare");
            parameters.bypass = false;
            require(effect.setParameters(parameters), "Wet impulse parameters");
            for (auto &channel : out)
                std::fill(channel.begin(), channel.end(), 0);
            for (int offset = 0; offset < frames; offset += 127)
            {
                const float *source[]{in[0].data() + offset, in[1].data() + offset};
                float *destination[]{out[0].data() + offset, out[1].data() + offset};
                require(effect.process(source, destination, std::min(127, frames - offset)),
                        "Variable block wet impulse");
            }
            const auto peak = size_t(std::max_element(out[0].begin(), out[0].end(), [](float a, float b)
                                                      { return std::abs(a) < std::abs(b); }) -
                                     out[0].begin());
            report << "unityWetImpulsePeakFrame=" << peak << " reportedLatency=" << effect.latency()
                   << " amplitude=" << out[0][peak] << '\n';
            require(int(peak) == effect.latency() && std::abs(out[0][peak]) > .3f,
                    "Wet/dry unity impulse alignment failed");
            parameters.amount = -1;
            require(!effect.setParameters(parameters), "Invalid amount accepted");
            parameters.amount = 1;
            parameters.targets.mask = 0;
            require(!effect.setParameters(parameters), "Empty scale accepted");
            parameters.targets.mask = 0xfff;
            parameters.bypass = false;
            parameters.speedMs = 100;
            require(effect.setParameters(parameters), "Smoothing parameters");
            effect.setPitch({440 * std::exp2(.3 / 12), 1});
            std::array<float, 256> zero{}, a{}, b{};
            const float *source[]{zero.data(), zero.data()};
            float *destination[]{a.data(), b.data()};
            require(effect.process(source, destination, 256) && effect.correctionSemitones() < 0 &&
                        effect.correctionSemitones() > -.3,
                    "Correction speed not gradual");
            const double old = effect.correctionSemitones();
            parameters.amount = 0;
            require(effect.setParameters(parameters), "Zero correction amount");
            require(effect.process(source, destination, 256) && effect.correctionSemitones() > old &&
                        effect.correctionSemitones() <= 0,
                    "Live amount did not preserve and release smoother");
            parameters.amount = 1;
            parameters.speedMs = 0;
            require(effect.setParameters(parameters), "Immediate correction parameters");
            effect.setPitch({440 * std::exp2(.3 / 12), .1});
            require(effect.process(source, destination, 256) && effect.correctionSemitones() == 0,
                    "Unvoiced confidence must not tune");
            require(!effect.process(source, destination, -1), "Negative block accepted");
            require(!effect.prepare(0), "Invalid rate accepted");
            report << "Aligned bypass impulse, variable127-frame chunks, invalid parameters, live "
                      "amount/speed and unvoiced release passed.\n";
        }
        report << "RESULT checks=" << checks << " cases=" << cases << " maxCents=" << maxCents
               << " maxProcessMs=" << maxProcessMs << " maxAnalysisMs=" << maxAnalysisMs
               << " shifterOverruns=" << overruns << " pitchFailures=" << pitchFailures
               << " stereoFailures=" << stereoFailures << " allocatingBlocks=" << allocatingBlocks
               << " providerMessages=" << providerMessages << " latencyFailures=" << latencyFailures
               << " resetFailures=" << resetFailures << " liveLatencyFailures=" << liveLatencyFailures
               << " cppDeleteCalls=" << deallocations << " resourceAllocateCalls=" << resourceAllocations
               << " resourceDeallocateCalls=" << resourceFrees
#if VIBERA_TUNE_RESOURCE_AUDIT
               << " resourceAuditAvailable=1"
#else
               << " resourceAuditAvailable=0"
#endif
#if VIBERA_TUNE_RUBBERBAND
               << " staticStreamingCases=30 impulseLatencyCases=15 liveImpulseLatencyCases=15 "
                  "providerDiagnosticsAvailable=1"
#else
               << " staticStreamingCases=0 impulseLatencyCases=0 liveImpulseLatencyCases=0 "
                  "providerDiagnosticsAvailable=0"
#endif
               << "\nOffline synthetic correction; analysis separate from processing. "
#if VIBERA_TUNE_RESOURCE_AUDIT
               << "Ordinary C++ new/delete and instrumented Rubber Band allocator entrypoints; "
                  "not a whole CRT allocator or realtime audit; no vocal listening or app "
#else
               << "Ordinary C++ new/delete hooks only; no full realtime audit, vocal listening or app "
#endif
                  "integration.\n";
        vibera::tune::allocationTrace::write(folder);
        require(pitchFailures == 0 && stereoFailures == 0 && allocatingBlocks == 0 && overruns == 0 &&
                    providerMessages == 0 && latencyFailures == 0 && resetFailures == 0 &&
                    deallocations == 0 && resourceAllocations == 0 && resourceFrees == 0 &&
                    liveLatencyFailures == 0,
                "Correction candidate failed pitch/stereo gates; full matrix retained");
        report << "PASS all defined correction gates\n";
        std::cout << "PASS " << cases << " correction cases, max " << maxCents << " cents\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        counting = false;
        report << "FAIL checks=" << checks << " reason=" << error.what() << '\n';
        std::cerr << error.what() << '\n';
        return 1;
    }
}
