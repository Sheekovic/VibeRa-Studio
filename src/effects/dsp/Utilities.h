// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vibera::effects
{
// Independently implemented balance, mid/side matrix and cookbook biquads.
class Utilities
{
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2]{}, z2[2]{};
        float tick(float input, int channel) noexcept
        {
            const auto result = b0 * input + z1[channel];
            z1[channel] = b1 * input - a1 * result + z2[channel];
            z2[channel] = b2 * input - a2 * result;
            return static_cast<float>(result);
        }
        void coefficients(double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
        {
            b0 = nb0 / na0;
            b1 = nb1 / na0;
            b2 = nb2 / na0;
            a1 = na1 / na0;
            a2 = na2 / na0;
        }
        void reset() noexcept
        {
            std::fill_n(z1, 2, 0.0);
            std::fill_n(z2, 2, 0.0);
        }
    };
    struct StateVariable
    {
        double g = 0, damping = 1, reciprocal = 1;
        double bandMemory[2]{}, lowMemory[2]{};
        void coefficients(double frequencyTerm, double inverseQ) noexcept
        {
            g = frequencyTerm;
            damping = inverseQ;
            reciprocal = 1 / (1 + g * (g + damping));
        }
        float tick(float input, int channel, int mode) noexcept
        {
            // Solve the two trapezoidal integrators together. Their memories
            // retain the same energy meaning when cutoff or Q changes, unlike
            // coefficient-dependent direct-form biquad delay states.
            // In exact arithmetic, zero-input memory energy changes by
            // -4*g*damping*band*band, including at coefficient transitions.
            // Background: https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
            const auto band = (bandMemory[channel] + g * (input - lowMemory[channel])) * reciprocal;
            const auto low = lowMemory[channel] + g * band;
            const auto high = input - damping * band - low;
            bandMemory[channel] = 2 * band - bandMemory[channel];
            lowMemory[channel] = 2 * low - lowMemory[channel];
            // Scale band-pass to unity at cutoff, preserving the original
            // constant-peak response rather than introducing Q-dependent gain.
            return static_cast<float>(mode == 0 ? low : mode == 1 ? high : damping * band);
        }
        void reset() noexcept
        {
            std::fill_n(bandMemory, 2, 0.0);
            std::fill_n(lowMemory, 2, 0.0);
        }
    };
    double rate = 48000, smoothing = 0;
    EffectId effect = EffectId::balance;
    Values current{}, target{};
    std::array<Biquad, 7> filters;
    StateVariable modulatedFilter;
    int filterMode = 0;
    float dcIn[2]{}, dcOut[2]{};
    unsigned coefficientClock = 0;
    bool initialized = false;
    static double gain(double db) noexcept
    {
        return std::pow(10.0, db / 20.0);
    }
    void updateFilters() noexcept
    {
        const auto pi = std::numbers::pi;
        const auto frequency = [&](double f) { return std::clamp(f, 5.0, rate * .45); };
        if (effect == EffectId::graphicEq)
        {
            constexpr double centres[]{80, 200, 500, 1250, 3150, 8000, 16000};
            for (std::size_t i = 0; i < filters.size(); ++i)
            {
                const auto w = 2 * pi * frequency(centres[i]) / rate;
                const auto alpha = std::sin(w) / (2 * 1.1);
                const auto a = std::pow(10.0, current[i] / 40.0);
                filters[i].coefficients(1 + alpha * a, -2 * std::cos(w), 1 - alpha * a, 1 + alpha / a,
                                        -2 * std::cos(w), 1 - alpha / a);
            }
        }
        else if (effect == EffectId::bassBoost)
        {
            const auto w = 2 * pi * frequency(current[0]) / rate;
            const auto a = std::pow(10.0, current[1] / 40.0);
            const auto c = std::cos(w), s = std::sin(w), beta = std::sqrt(2 * a) * s;
            filters[0].coefficients(a * ((a + 1) - (a - 1) * c + beta), 2 * a * ((a - 1) - (a + 1) * c),
                                    a * ((a + 1) - (a - 1) * c - beta), (a + 1) + (a - 1) * c + beta,
                                    -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - beta);
        }
        else if (effect == EffectId::filter)
        {
            const auto w = 2 * pi * frequency(current[1]) / rate;
            modulatedFilter.coefficients(std::tan(w * .5), 1.0 / std::max(.1f, current[2]));
        }
    }

  public:
    void prepare(double sampleRate) noexcept
    {
        rate = sampleRate;
        smoothing = 1 - std::exp(-1 / (.005 * rate));
        initialized = false;
        reset();
    }
    void reset() noexcept
    {
        for (auto &filter : filters)
            filter.reset();
        modulatedFilter.reset();
        std::fill_n(dcIn, 2, 0.0f);
        std::fill_n(dcOut, 2, 0.0f);
        coefficientClock = 0;
    }
    void setParameters(EffectId id, const Values &values) noexcept
    {
        if (!initialized || id != effect)
        {
            effect = id;
            current = values;
            reset(); // Fixed-size scalar state only; no delay buffers or callback allocation.
            initialized = true;
        }
        if (id == EffectId::filter)
        {
            const auto nextMode = static_cast<int>(std::lround(values[0]));
            if (nextMode != filterMode)
            {
                modulatedFilter.reset(); // Fixed four-scalar mode-switch invalidation.
                coefficientClock = 0;
            }
            filterMode = nextMode;
        }
        target = values;
    }
    void process(float &left, float &right) noexcept
    {
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            if ((effect == EffectId::mute && i == 0) || (effect == EffectId::polarity && i < 2))
            {
                // Binary silence/polarity must reach their exact endpoint;
                // exponential float smoothing can stall with residual signal.
                const auto step = static_cast<float>(1 / (.005 * rate));
                current[i] += std::clamp(target[i] - current[i], -step, step);
            }
            else
                current[i] += static_cast<float>(smoothing * (target[i] - current[i]));
        }
        if ((coefficientClock++ & 31u) == 0)
            updateFilters();
        switch (effect)
        {
        case EffectId::balance:
        {
            const auto g = static_cast<float>(gain(current[0]));
            left *= g * (1 - std::max(0.0f, current[1]));
            right *= g * (1 + std::min(0.0f, current[1]));
            break;
        }
        case EffectId::stereoWidth:
        {
            const auto mid = (left + right) * .5f, side = (left - right) * .5f * current[0];
            left = mid + side;
            right = mid - side;
            break;
        }
        case EffectId::polarity:
            left *= 1 - 2 * current[0];
            right *= 1 - 2 * current[1];
            break;
        case EffectId::mute:
            left *= 1 - current[0];
            right *= 1 - current[0];
            break;
        case EffectId::stereoMatrix:
        {
            const auto l = left, r = right;
            left = l * current[0] + r * current[1];
            right = l * current[2] + r * current[3];
            break;
        }
        case EffectId::dcBlock:
        {
            const auto a = static_cast<float>(std::exp(-2 * std::numbers::pi * current[0] / rate));
            auto tick = [&](float x, int c)
            {
                const auto y = x - dcIn[c] + a * dcOut[c];
                dcIn[c] = x;
                dcOut[c] = y;
                return y;
            };
            left = tick(left, 0);
            right = tick(right, 1);
            break;
        }
        case EffectId::graphicEq:
            for (auto &filter : filters)
            {
                left = filter.tick(left, 0);
                right = filter.tick(right, 1);
            }
            left *= static_cast<float>(gain(current[7]));
            right *= static_cast<float>(gain(current[7]));
            break;
        case EffectId::bassBoost:
            left = filters[0].tick(left, 0);
            right = filters[0].tick(right, 1);
            break;
        case EffectId::filter:
        {
            const auto l = modulatedFilter.tick(left, 0, filterMode),
                       r = modulatedFilter.tick(right, 1, filterMode);
            left += current[3] * (l - left);
            right += current[3] * (r - right);
            break;
        }
        default:
            break;
        }
    }
};
} // namespace vibera::effects
