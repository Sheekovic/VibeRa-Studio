// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Per-insert 3-band EQ (post-effects, pre-stereo/pan/fader): a low shelf, a peaking mid and a
// high shelf. Original RBJ-cookbook biquads; real-time safe (no allocation or locks).
namespace vibera
{
struct MixerEqSettings
{
    std::array<float, 3> gain{0, 0, 0};          // dB, -18..18.
    std::array<float, 3> freq{90, 1500, 8000};   // Hz, 20..20000.
    static constexpr std::array<float, 3> defaultFreq{90, 1500, 8000};
    bool active() const noexcept
    {
        return gain[0] != 0 || gain[1] != 0 || gain[2] != 0;
    }
    bool operator==(const MixerEqSettings &) const = default;
    static bool valid(float gainDb, float hz) noexcept
    {
        return std::isfinite(gainDb) && std::isfinite(hz) && gainDb >= -18 && gainDb <= 18 && hz >= 20 &&
               hz <= 20000;
    }
};

class MixerEqProcessor
{
    struct Coefficients
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    std::array<Coefficients, 3> coefficients{};
    std::array<std::array<std::array<float, 2>, 8>, 3> state{}; // [band][channel][z1,z2]
    MixerEqSettings current{};
    double sampleRate = 0;
    bool active = false;

    static Coefficients design(int band, float gainDb, float hz, double rate)
    {
        constexpr double pi = 3.14159265358979323846;
        const double frequency = std::min(double(hz), rate * .45);
        const double a = std::pow(10., gainDb / 40.), w = 2. * pi * frequency / rate;
        const double cs = std::cos(w), sn = std::sin(w);
        double b0, b1, b2, a0, a1, a2;
        if (band == 1)
        {
            const double alpha = sn / (2. * .9); // Peaking mid, Q 0.9.
            b0 = 1 + alpha * a, b1 = -2 * cs, b2 = 1 - alpha * a;
            a0 = 1 + alpha / a, a1 = -2 * cs, a2 = 1 - alpha / a;
        }
        else
        {
            const double alpha = sn / 2. * std::sqrt(2.), sq = 2. * std::sqrt(a) * alpha; // Shelf slope 1.
            if (band == 0)
            {
                b0 = a * ((a + 1) - (a - 1) * cs + sq), b1 = 2 * a * ((a - 1) - (a + 1) * cs);
                b2 = a * ((a + 1) - (a - 1) * cs - sq), a0 = (a + 1) + (a - 1) * cs + sq;
                a1 = -2 * ((a - 1) + (a + 1) * cs), a2 = (a + 1) + (a - 1) * cs - sq;
            }
            else
            {
                b0 = a * ((a + 1) + (a - 1) * cs + sq), b1 = -2 * a * ((a - 1) + (a + 1) * cs);
                b2 = a * ((a + 1) + (a - 1) * cs - sq), a0 = (a + 1) - (a - 1) * cs + sq;
                a1 = 2 * ((a - 1) - (a + 1) * cs), a2 = (a + 1) - (a - 1) * cs - sq;
            }
        }
        return {float(b0 / a0), float(b1 / a0), float(b2 / a0), float(a1 / a0), float(a2 / a0)};
    }

  public:
    void prepare(double rate, const MixerEqSettings &settings) noexcept
    {
        sampleRate = rate;
        for (auto &band : state)
            for (auto &channel : band)
                channel = {0, 0};
        current = {};
        current.gain = {1000, 1000, 1000}; // Force a design on the first setTarget.
        setTarget(settings);
    }
    void setTarget(const MixerEqSettings &settings) noexcept
    {
        if (settings == current || sampleRate <= 0)
            return;
        for (int band = 0; band < 3; ++band)
            coefficients[size_t(band)] = design(band, settings.gain[size_t(band)], settings.freq[size_t(band)], sampleRate);
        if (!settings.active())
            for (auto &band : state)
                for (auto &channel : band)
                    channel = {0, 0};
        current = settings;
        active = settings.active();
    }
    // Filters `samples` frames of up to 8 channels in place (transposed direct form II).
    void process(float *const *channels, int channelCount, int samples) noexcept
    {
        if (!active)
            return;
        for (int band = 0; band < 3; ++band)
        {
            if (current.gain[size_t(band)] == 0)
                continue;
            const auto c = coefficients[size_t(band)];
            for (int ch = 0; ch < std::min(channelCount, 8); ++ch)
            {
                auto &z = state[size_t(band)][size_t(ch)];
                float *x = channels[ch];
                for (int i = 0; i < samples; ++i)
                {
                    const float in = x[i];
                    const float out = c.b0 * in + z[0];
                    z[0] = c.b1 * in - c.a1 * out + z[1];
                    z[1] = c.b2 * in - c.a2 * out;
                    x[i] = out;
                }
            }
        }
    }
    // Magnitude response in dB at `hz` (for the editor curve; control thread).
    static float responseDb(const MixerEqSettings &settings, double hz, double rate = 48000)
    {
        constexpr double pi = 3.14159265358979323846;
        double total = 0;
        for (int band = 0; band < 3; ++band)
        {
            if (settings.gain[size_t(band)] == 0)
                continue;
            const auto c = design(band, settings.gain[size_t(band)], settings.freq[size_t(band)], rate);
            const double w = 2. * pi * hz / rate;
            const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
            const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
            const double dr = 1 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
            total += 10. * std::log10((nr * nr + ni * ni) / std::max(1.e-30, dr * dr + di * di));
        }
        return float(total);
    }
};
} // namespace vibera
