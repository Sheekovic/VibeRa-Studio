// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerFilterSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vibera
{
// CONTROL prepares the rate-dependent table. Callback updates only interpolate
// bounded coefficients; no transcendental functions, resource changes or waits.
struct PreparedSamplerFilter
{
    static constexpr size_t intervals = 1024;
    std::array<double, intervals + 1> frequency{};
    double smoothing = 1. / 240.;
    void prepare(double rate) noexcept
    {
        smoothing = std::min(1., 1. / (.005 * rate));
        for (size_t i = 0; i <= intervals; ++i)
            frequency[i] =
                std::tan(std::numbers::pi * samplerFilterCutoffHz(double(i) / intervals, rate) / rate);
    }
    double coefficient(double normalized) const noexcept
    {
        const auto position = std::clamp(normalized, 0., 1.) * intervals;
        const auto index = std::min(size_t(position), intervals - 1);
        return frequency[index] + (position - index) * (frequency[index + 1] - frequency[index]);
    }
};

// Original trapezoidal state-variable filter, with fixed per-voice stereo state.
// Its integrator memories retain their physical meaning during cutoff changes.
// Modes share those memories and crossfade output weights, not whole graphs.
// This is not a reconstruction of any proprietary filter transfer function.
class SamplerFilter
{
    struct Integrators
    {
        double band[2]{}, low[2]{};
        std::array<double, 3> tick(double input, int channel, double g, double k, double reciprocal) noexcept
        {
            const auto b = (band[channel] + g * (input - low[channel])) * reciprocal;
            const auto l = low[channel] + g * b;
            const auto h = input - k * b - l;
            band[channel] = 2 * b - band[channel];
            low[channel] = 2 * l - low[channel];
            return {l, h, k * b}; // Unity-peak BP; BS is LP + HP.
        }
    } first, second;
    double g = 0, targetG = 0, k = 1, targetK = 1;
    std::array<double, 5> mix{}, targetMix{};
    int settling = 0;
    bool bypassed = true;

  public:
    void configure(const PreparedSamplerFilter &prepared, int type, double cutoff, double resonance,
                   bool initialize = false) noexcept
    {
        const auto nextG = prepared.coefficient(cutoff);
        const auto nextK = 1. / samplerFilterQ(resonance);
        const bool bypass = type == 0 || ((type == 1 || type == 2) && cutoff == 1 && resonance == 0);
        std::array<double, 5> nextMix{};
        if (!bypass && type >= 1 && type <= 5)
            nextMix[size_t(type - 1)] = 1;
        if (!initialize && nextG == targetG && nextK == targetK && nextMix == targetMix)
            return;
        targetG = nextG;
        targetK = nextK;
        targetMix = nextMix;
        bypassed = bypass;
        if (initialize)
        {
            first = {};
            second = {};
            g = targetG;
            k = targetK;
            mix = targetMix;
            settling = 0;
        }
        else
        {
            // Ten time constants, then snap the remaining tiny difference so
            // dry endpoints become exact and do not retain a hidden DSP load.
            settling = int(10. / prepared.smoothing) + 1;
        }
    }
    void process(double &left, double &right, const PreparedSamplerFilter &prepared) noexcept
    {
        if (settling == 0 && bypassed)
            return; // No extra DSP work for unchanged legacy/default voices.
        if (settling > 0)
        {
            g += (targetG - g) * prepared.smoothing;
            k += (targetK - k) * prepared.smoothing;
            for (size_t i = 0; i < mix.size(); ++i)
                mix[i] += (targetMix[i] - mix[i]) * prepared.smoothing;
            if (--settling == 0)
            {
                g = targetG;
                k = targetK;
                mix = targetMix;
                if (bypassed)
                {
                    first = {};
                    second = {};
                }
            }
        }
        if (settling == 0 && bypassed)
            return; // Preserve the legacy dry multiplication/rounding exactly.
        const auto reciprocal = 1. / (1. + g * (g + k));
        auto tick = [&](double input, int channel)
        {
            const auto primary = first.tick(input, channel, g, k, reciprocal);
            const auto cascade = second.tick(primary[0], channel, g, k, reciprocal)[0];
            return input + mix[0] * (primary[0] - input) + mix[1] * (cascade - input) +
                   mix[2] * (primary[1] - input) + mix[3] * (primary[2] - input) +
                   mix[4] * (primary[0] + primary[1] - input);
        };
        left = tick(left, 0);
        right = tick(right, 1);
    }
};
} // namespace vibera
