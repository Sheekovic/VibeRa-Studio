// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace vibera::synth
{
// Original wavetable banks generated from harmonic spectra (no recorded or copied material).
// Each bank has 16 frames that morph across its position; every frame is stored at ten
// band-limited levels (512, 256 ... 1 harmonics) so high notes never alias.
class Wavetables
{
  public:
    static constexpr int banks = 5, frames = 16, size = 2048, levels = 10, maximumHarmonics = 512;
    static constexpr std::array<const char *, banks> names{"Analog Morph", "Harmonic Sweep", "Formant",
                                                          "Pulse Sweep", "Digital"};

    // Built once on first use (prepare time, never on the audio thread).
    static const Wavetables &instance()
    {
        static const Wavetables tables;
        return tables;
    }
    // Phase 0..1, position 0..1 across the frames, step = cycles per sample.
    float sample(int bank, double position, double phase, double step) const noexcept
    {
        if (!(step > 0) || step >= .49)
            return 0;
        const int allowed = int(.45 / step);
        if (allowed < 1)
            return 0;
        int level = 0;
        while (level < levels - 1 && (maximumHarmonics >> level) > allowed)
            ++level;
        const double frame = std::clamp(position, 0.0, 1.0) * (frames - 1);
        const int f0 = std::min(frames - 2, int(frame));
        const double blend = frame - f0;
        const double index = phase * size;
        const int i0 = int(index) & (size - 1), i1 = (i0 + 1) & (size - 1);
        const double fraction = index - std::floor(index);
        const auto read = [&](int f)
        {
            const float *table = data_.data() + offset(bank, f, level);
            return table[i0] + (table[i1] - table[i0]) * fraction;
        };
        return float(read(f0) + (read(f0 + 1) - read(f0)) * blend);
    }

  private:
    std::vector<float> data_;
    static std::size_t offset(int bank, int frame, int level) noexcept
    {
        return ((std::size_t(bank) * frames + std::size_t(frame)) * levels + std::size_t(level)) * size;
    }
    // Sine and cosine amplitude of harmonic k (1-based) for a bank frame at morph t (0..1).
    static void spectrum(int bank, double t, int k, double &sine, double &cosine)
    {
        constexpr double pi = std::numbers::pi;
        sine = cosine = 0;
        const double saw = 2 / (pi * k) * ((k % 2) ? 1 : -1);
        const double square = (k % 2) ? 4 / (pi * k) : 0;
        const double triangle = (k % 2) ? 8 / (pi * pi * k * k) * (((k - 1) / 2) % 2 ? -1 : 1) : 0;
        const double sineWave = k == 1 ? 1 : 0;
        switch (bank)
        {
        case 0: // Sine -> triangle -> saw -> square.
        {
            const double x = t * 3;
            const int segment = std::min(2, int(x));
            const double w = x - segment;
            const double from[3]{sineWave, triangle, saw}, to[3]{triangle, saw, square};
            sine = from[segment] + (to[segment] - from[segment]) * w;
            break;
        }
        case 1: // Brighter and brighter: harmonics fade in up to 64.
        {
            const double edge = 1 + t * 63;
            sine = k <= edge ? 1.0 / k : k < edge + 4 ? (edge + 4 - k) / 4 / k : 0;
            break;
        }
        case 2: // A resonant formant peak sweeping up the harmonic series.
        {
            const double centre = 2 + t * 28, width = 1.5 + t * 2;
            sine = std::exp(-std::pow((k - centre) / width, 2)) + (k == 1 ? .35 : 0);
            break;
        }
        case 3: // Pulse width 50% -> 5%.
        {
            const double widthRatio = .5 - t * .45;
            cosine = 2 / (pi * k) * std::sin(pi * k * widthRatio);
            break;
        }
        default: // Metallic digital partials whose pattern shifts with the position.
        {
            const double pattern = std::abs(std::sin(k * (1 + t * 7) * .37));
            sine = pattern / std::sqrt(double(k)) * (k <= 96 ? 1 : 0);
            break;
        }
        }
    }
    Wavetables() : data_(std::size_t(banks) * frames * levels * size)
    {
        std::vector<double> sine(maximumHarmonics + 1), cosine(maximumHarmonics + 1);
        std::array<double, levels> sums{};
        for (int bank = 0; bank < banks; ++bank)
            for (int frame = 0; frame < frames; ++frame)
            {
                const double t = double(frame) / (frames - 1);
                for (int k = 1; k <= maximumHarmonics; ++k)
                    spectrum(bank, t, k, sine[std::size_t(k)], cosine[std::size_t(k)]);
                double peak = 0;
                for (int i = 0; i < size; ++i)
                {
                    // Chebyshev recurrence for sin/cos(k*theta); partial sums captured at
                    // each level's harmonic limit, so one pass fills all ten levels.
                    const double theta = 2 * std::numbers::pi * i / size, c = 2 * std::cos(theta);
                    double s0 = 0, s1 = std::sin(theta), c0 = 1, c1 = std::cos(theta), sum = 0;
                    int level = levels - 1;
                    for (int k = 1; k <= maximumHarmonics; ++k)
                    {
                        sum += sine[std::size_t(k)] * s1 + cosine[std::size_t(k)] * c1;
                        while (level >= 0 && k == (maximumHarmonics >> level))
                            sums[std::size_t(level--)] = sum;
                        const double s2 = c * s1 - s0, c2 = c * c1 - c0;
                        s0 = s1;
                        s1 = s2;
                        c0 = c1;
                        c1 = c2;
                    }
                    for (int l = 0; l < levels; ++l)
                        data_[offset(bank, frame, l) + std::size_t(i)] = float(sums[std::size_t(l)]);
                    peak = std::max(peak, std::abs(sums[0]));
                }
                // Same gain on every level of a frame keeps loudness steady across the keyboard.
                const float gain = peak > 1e-9 ? float(.9 / peak) : 0.f;
                for (int l = 0; l < levels; ++l)
                {
                    float *table = data_.data() + offset(bank, frame, l);
                    double mean = 0;
                    for (int i = 0; i < size; ++i)
                        mean += table[i];
                    mean /= size;
                    for (int i = 0; i < size; ++i)
                        table[i] = float((table[i] - mean) * gain);
                }
            }
    }
};
} // namespace vibera::synth
