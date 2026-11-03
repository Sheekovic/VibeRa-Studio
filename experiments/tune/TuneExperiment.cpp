// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TunePitch.h"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>
#include <stdexcept>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    std::ofstream report(argv[1], std::ios::trunc);
    int checks = 0, cases = 0;
    double maximumMs = 0, maximumShortMs = 0, maximumCents = 0;
    auto require = [&](bool condition, const char *message)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    };
    try
    {
        vibera::tune::Detector detector;
        std::vector<float> frame(8192);
        for (double rate : {44100., 48000., 96000.})
            for (int frameCount : {rate == 96000 ? 4096 : 2048, 8192})
                for (double hz : {65., 80., 110., 220., 440., 880., 1200.})
                    for (int kind : {0, 1, 2})
                    {
                        frame.resize(size_t(frameCount));
                        require(detector.prepare(rate), "detector preparation");
                        for (size_t i = 0; i < frame.size(); ++i)
                        {
                            const double phase = 2 * std::numbers::pi * hz * i / rate;
                            frame[i] =
                                float((kind == 2 ? 0 : .35 * std::sin(phase)) +
                                      (kind ? .5 * std::sin(2 * phase) + .2 * std::sin(3 * phase) : 0));
                        }
                        const auto begin = std::chrono::steady_clock::now();
                        const auto pitch = detector.analyze(frame);
                        const double ms = std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - begin)
                                              .count();
                        maximumMs = std::max(maximumMs, ms);
                        if (frameCount < 8192)
                            maximumShortMs = std::max(maximumShortMs, ms);
                        ++cases;
                        const double cents = pitch.hz > 0 ? std::abs(1200 * std::log2(pitch.hz / hz)) : 1e9;
                        maximumCents = std::max(maximumCents, cents);
                        report << "rate=" << rate << " frames=" << frameCount << " targetHz=" << hz
                               << " kind=" << kind << " measuredHz=" << pitch.hz << " cents=" << cents
                               << " confidence=" << pitch.confidence << " ms=" << ms << '\n';
                        require(cents < 5 && pitch.confidence > .8, "synthetic pitch error/confidence gate");
                    }
        std::fill(frame.begin(), frame.end(), 0);
        require(detector.analyze(frame).hz == 0, "silence must not produce pitch");
        std::fill(frame.begin(), frame.end(), .25f);
        require(detector.analyze(frame).hz == 0, "DC must not produce pitch");
        frame[42] = std::numeric_limits<float>::quiet_NaN();
        require(detector.analyze(frame).hz == 0, "invalid PCM must not produce pitch");
        require(!detector.prepare(48000, 0), "invalid preparation rejected");
        require(detector.prepare(48000), "prepare unvoiced checks");
        std::fill(frame.begin(), frame.end(), 0);
        frame[500] = 1;
        require(detector.analyze(frame).hz == 0, "isolated transient must not produce stable pitch");
        std::uint32_t random = 0x13579bdf;
        for (auto &value : frame)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            value = float(double(random) / 2147483648. - 1.);
        }
        require(detector.analyze(frame).hz == 0, "unvoiced white noise must not produce stable pitch");
        require(detector.analyze(std::span<const float>(frame.data(), 32)).hz == 0,
                "insufficient observation must not produce pitch");
        vibera::tune::Targets targets;
        require(targets.nearest(69.3) == 69, "chromatic nearest note");
        targets.mask = 0xab5; // Major scale degrees 0,2,4,5,7,9,11.
        require(targets.nearest(61.2) == 62 && targets.nearest(60.2) == 60, "major-scale mapping");
        targets.root = 2;
        require(targets.nearest(61.2) == 61 && targets.nearest(60.2) == 61, "transposed scale mapping");
        targets.mask = 0;
        require(!targets.valid(), "empty note mask rejected");
        report << "PASS checks=" << checks << " cases=" << cases << " maxCents=" << maximumCents
               << " maxAnalyzeMs=" << maximumMs << " maxShortAnalyzeMs=" << maximumShortMs
               << "\nOffline synthetic frames/target mapping only; no audio correction, real vocals, "
                  "callback deadlines, GUI, hardware or listening qualification.\n";
        std::cout << "PASS " << checks << " checks; " << maximumCents << " max cents; " << maximumMs
                  << " max analysis ms\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        report << "FAIL checks=" << checks << " reason=" << error.what() << '\n';
        std::cerr << error.what() << '\n';
        return 1;
    }
}
