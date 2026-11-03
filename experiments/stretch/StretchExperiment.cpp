// SPDX-License-Identifier: AGPL-3.0-or-later
#include <signalsmith-stretch.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <memory>
#include <new>

// Counts ordinary C++ allocations only; separate upstream/source audit still required.
static thread_local bool countAllocations = false;
static thread_local size_t allocations = 0;
void *operator new(size_t bytes) {
    if (countAllocations) ++allocations;
    if (auto *p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
static const char *presetName(int preset) {
    return preset == 0 ? "default" : preset == 1 ? "cheaper" : preset == 2 ? "window1024" : preset == 3 ? "window2048" : "window4096";
}
static void configure(signalsmith::stretch::SignalsmithStretch<float> &stretch, int channels, int preset, bool split) {
    if (preset == 0) stretch.presetDefault(channels, 48000.f, split);
    if (preset == 1) stretch.presetCheaper(channels, 48000.f, split);
    if (preset == 2) stretch.configure(channels, 1024, 256, split);
    if (preset == 3) stretch.configure(channels, 2048, 512, split);
    if (preset == 4) stretch.configure(channels, 4096, 1024, split);
}
static double spectralFrequency(const std::vector<float> &pcm) {
    constexpr int n = 32768;
    signalsmith::linear::RealFFT<float> fft(n);
    std::vector<float> window(n);
    std::vector<std::complex<float>> spectrum(n / 2);
    const int offset = int(pcm.size()) / 2 - n / 2;
    for (int i = 0; i < n; ++i)
        window[i] = pcm[size_t(offset+i)] * float(.5 - .5*std::cos(2*std::acos(-1.)*i/(n-1)));
    fft.fft(window.data(),spectrum.data());
    int best = 1;
    for (int i = 2; i < n/2-1; ++i)
        if (std::norm(spectrum[i]) > std::norm(spectrum[best])) best = i;
    const double left = std::log(std::abs(spectrum[best-1])+1e-20);
    const double centre = std::log(std::abs(spectrum[best])+1e-20);
    const double right = std::log(std::abs(spectrum[best+1])+1e-20);
    const double adjustment = .5*(left-right)/(left-2*centre+right);
    return (best+adjustment)*48000./n;
}
int main(int argc, char **argv)
{
    try {
        require(argc == 2, "Specify the evidence text path");
        std::ofstream report(argv[1]);
        require(bool(report), "Evidence output unavailable");
        int pitchFailures = 0, markerFailures = 0;
        constexpr int rate = 48000, inputFrames = rate * 4;
        const double pi = std::acos(-1.);
        std::array<std::vector<float>, 2> input;
        for (int channel = 0; channel < 2; ++channel) {
            input[channel].resize(inputFrames);
            for (int i = 0; i < inputFrames; ++i)
                input[channel][i] = float(.3 * std::sin(2 * pi * (channel ? 330 : 220) * i / rate));
        }
        const float *in[] = {input[0].data(), input[1].data()};
        for (int preset : {0,1,2,3,4}) {
        for (double tempoRatio : {.5, .8333333333, 1., 1.2, 2.}) {
            for (double pitch : {1., 2.}) {
                const auto began = std::chrono::steady_clock::now();
                signalsmith::stretch::SignalsmithStretch<float> stretch;
                configure(stretch, 2, preset, false);
                stretch.setTransposeFactor(float(pitch));
                const int frames = int(std::round(inputFrames / tempoRatio));
                std::array<std::vector<float>, 2> output;
                for (auto &channel : output) channel.resize(frames);
                float *out[] = {output[0].data(), output[1].data()};
                require(stretch.exact(in, inputFrames, out, frames), "Long source exact stretch failed");
                const auto milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - began).count();
                report << "preset=" << presetName(preset) << " tempoRatio=" << tempoRatio << " pitchRatio=" << pitch << " frames=" << frames
                       << " inputLatency=" << stretch.inputLatency() << " outputLatency=" << stretch.outputLatency()
                       << " preparationMs=" << milliseconds;
                const auto failuresBefore = pitchFailures;
                for (int c = 0; c < 2; ++c) {
                    int crossings = 0;
                    float peak = 0;
                    for (int i = 0; i < frames; ++i) {
                        require(std::isfinite(output[c][i]), "Non-finite output");
                        peak = std::max(peak, std::abs(output[c][i]));
                    }
                    // Exclude analysis/flush boundaries; independently count each stereo channel.
                    const int begin = frames / 4, end = frames * 3 / 4;
                    for (int i = begin + 1; i < end; ++i)
                        crossings += output[c][i-1] <= 0 && output[c][i] > 0;
                    const double frequency = crossings * double(rate) / (end - begin - 1);
                    const double expected = (c ? 330 : 220) * pitch;
                    require(peak > .1f && peak < 1.f, "Missing or unbounded output");
                    const double spectral = spectralFrequency(output[c]);
                    const double cents = 1200 * std::log2(spectral / expected);
                    const bool validPitch = std::abs(cents) < 5.;
                    pitchFailures += !validPitch;
                    report << " channel" << c << "ZeroCrossHz=" << frequency << " spectralHz=" << spectral
                           << " peak=" << peak << " centsError=" << cents << " pitchPass=" << validPitch;
                }
                report << (failuresBefore == pitchFailures ? " PASS\n" : " FAIL\n");
            }
        }
        }
        for (int preset : {0,1,2,3,4}) {
            signalsmith::stretch::SignalsmithStretch<float> mono;
            configure(mono,1,preset,false);
            mono.setTransposeFactor(2.f);
            std::vector<float> output(inputFrames*2);
            const float *monoIn[] = {input[0].data()};
            float *monoOut[] = {output.data()};
            require(mono.exact(monoIn,inputFrames,monoOut,int(output.size())), "Mono comparison preparation failed");
            const double hz = spectralFrequency(output);
            report << "IndependentMono preset=" << presetName(preset) << " expectedHz=440 spectralHz="
                   << hz << " centsError=" << 1200*std::log2(hz/440.) << "\n";
        }
        for (int preset : {0,1,2,3,4}) {
            for (double tempoRatio : {.8333333333, 1.2}) {
                signalsmith::stretch::SignalsmithStretch<float> stretch;
                configure(stretch, 2, preset, false);
                std::array<std::vector<float>,2> markers;
                for (auto &channel : markers) channel.resize(inputFrames);
                for (int beat = 0; beat < 8; ++beat) {
                    const int onset = rate/4 + beat * rate/2;
                    markers[0][onset] = markers[1][onset] = .5f;
                }
                const int frames = int(std::round(inputFrames / tempoRatio));
                std::array<std::vector<float>,2> output;
                for (auto &channel : output) channel.resize(frames);
                const float *markerIn[] = {markers[0].data(), markers[1].data()};
                float *markerOut[] = {output[0].data(), output[1].data()};
                require(stretch.exact(markerIn,inputFrames,markerOut,frames), "Marker preparation failed");
                double maximumError = 0;
                for (int beat = 0; beat < 8; ++beat) {
                    const int expected = int(std::round((rate/4 + beat * rate/2) / tempoRatio));
                    const int first = std::max(0,expected-rate/10), last = std::min(frames,expected+rate/10);
                    float peak = 0; int peakFrame = first;
                    for (int frame = first; frame < last; ++frame) {
                        require(std::isfinite(output[0][frame]), "Non-finite marker output");
                        if (std::abs(output[0][frame]) > peak) {peak=std::abs(output[0][frame]);peakFrame=frame;}
                    }
                    markerFailures += peak <= .005f;
                    maximumError=std::max(maximumError,std::abs(peakFrame-expected)*1000./rate);
                }
                report << "Markers preset=" << presetName(preset) << " tempoRatio=" << tempoRatio
                       << " events=8 maxPeakErrorMs=" << maximumError << "\n";
            }
        }
        for (int channels : {1,2}) {
        for (int preset : {0,1,2,3,4}) {
        for (int polyphony : {1, 16, 64}) {
            std::vector<std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float>>> voices;
            for (int i = 0; i < polyphony; ++i) {
                auto voice = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
                configure(*voice,channels,preset,true);
                voices.push_back(std::move(voice));
            }
            std::array<std::array<float, 256>, 2> block{};
            float *out[] = {block[0].data(), block[1].data()};
            double maximum = 0, total = 0;
            int overruns = 0;
            allocations = 0;
            int sourcePosition = 0;
            for (int tick = 0; tick < 512; ++tick) {
                const int frames = tick % 2 ? 256 : 320;
                const float *source[] = {input[0].data() + sourcePosition,
                                        input[1].data() + sourcePosition};
                const auto start = std::chrono::steady_clock::now();
                countAllocations = true;
                for (auto &voice : voices) {
                    voice->setTransposeFactor(tick % 64 < 32 ? 1.f : 2.f);
                    voice->process(source, frames, out, 256);
                }
                countAllocations = false;
                sourcePosition = (sourcePosition + frames) % (inputFrames - 320);
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                maximum = std::max(maximum, ms); total += ms;
                overruns += ms > 256. * 1000 / rate;
                for (int c = 0; c < channels; ++c)
                    for (auto value : block[c]) require(std::isfinite(value), "Non-finite streaming output");
            }
            report << "Streaming preset=" << presetName(preset) << " channels=" << channels << " voices=" << polyphony << " blocks=512 maxMs=" << maximum
                   << " meanMs=" << total / 512 << " overruns=" << overruns
                   << " ordinaryCppAllocations=" << allocations << "\n";
            require(allocations == 0, "Streaming processor allocated after preparation");
        }
        }}
        signalsmith::stretch::SignalsmithStretch<float> shortStretch;
        shortStretch.presetDefault(2, float(rate));
        std::array<std::array<float, 128>, 2> shortOutput{};
        float *shortOut[] = {shortOutput[0].data(), shortOutput[1].data()};
        require(!shortStretch.exact(in, 128, shortOut, 128), "Short source limitation changed");
        report << "pitchFailures=" << pitchFailures << " missingMarkers=" << markerFailures << "\n";
        require(pitchFailures == 0 && markerFailures == 0, "Candidate matrix contains failed pitch or marker checks; inspect full report");
        report << "PASS: fifty stereo pitch/time cases; ten marker cases; mono/stereo preset streaming benchmarks; short-source failure exposed.\n"
                  "Offline preparation experiment only. Ordinary C++ allocation hooks cover prepared streaming calls only; no full real-time/malloc/lock audit, integration, listening, hardware or musical quality qualification.\n";
        std::cout << "PASS: isolated Signalsmith offline pitch/time experiment\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
