// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace vibera
{
// Independent musical contract. Times are seconds, or quarter-note beats when
// the corresponding Tempo flag is set. This is not proprietary DSP recovery.
inline constexpr std::array<std::string_view, 5> samplerModulationTargets{"volume", "pan", "cutoff",
                                                                          "resonance", "pitch"};
struct SamplerModulation
{
    bool enabled = false, tempo = false;
    double delay = 0, attack = .01, hold = 0, decay = .1, sustain = 1, release = .1;
    double attackTension = 0, decayTension = 0, releaseTension = 0, amount = 0;
    bool lfoEnabled = false, lfoTempo = false, lfoGlobal = false;
    double lfoDelay = 0, lfoAttack = 0, lfoPeriod = 1, lfoAmount = 0;
    int lfoShape = 0; // Sine, triangle, square; start at the rising zero crossing.
    bool operator==(const SamplerModulation &) const = default;
};
struct SamplerModulationParameter
{
    std::string_view id, label;
    double minimum, maximum, initial;
    bool integral = false;
    bool boolean = false;
    double SamplerModulation::*number = nullptr;
    bool SamplerModulation::*flag = nullptr;
};
inline constexpr std::array<SamplerModulationParameter, 20> samplerModulationParameters{
    {{"enabled", "Envelope enabled", 0, 1, 0, true, true, nullptr, &SamplerModulation::enabled},
     {"tempo", "Envelope tempo", 0, 1, 0, true, true, nullptr, &SamplerModulation::tempo},
     {"delay", "Delay", 0, 64, 0, false, false, &SamplerModulation::delay},
     {"attack", "Attack", 0, 64, .01, false, false, &SamplerModulation::attack},
     {"hold", "Hold", 0, 64, 0, false, false, &SamplerModulation::hold},
     {"decay", "Decay", 0, 64, .1, false, false, &SamplerModulation::decay},
     {"sustain", "Sustain", 0, 1, 1, false, false, &SamplerModulation::sustain},
     {"release", "Release", 0, 64, .1, false, false, &SamplerModulation::release},
     {"attackTension", "Attack tension", -1, 1, 0, false, false, &SamplerModulation::attackTension},
     {"decayTension", "Decay tension", -1, 1, 0, false, false, &SamplerModulation::decayTension},
     {"releaseTension", "Release tension", -1, 1, 0, false, false, &SamplerModulation::releaseTension},
     {"amount", "Envelope amount", -1, 1, 0, false, false, &SamplerModulation::amount},
     {"lfoEnabled", "LFO enabled", 0, 1, 0, true, true, nullptr, &SamplerModulation::lfoEnabled},
     {"lfoTempo", "LFO tempo", 0, 1, 0, true, true, nullptr, &SamplerModulation::lfoTempo},
     {"lfoGlobal", "Global phase", 0, 1, 0, true, true, nullptr, &SamplerModulation::lfoGlobal},
     {"lfoDelay", "LFO delay", 0, 64, 0, false, false, &SamplerModulation::lfoDelay},
     {"lfoAttack", "LFO attack", 0, 64, 0, false, false, &SamplerModulation::lfoAttack},
     {"lfoPeriod", "LFO period", .01, 64, 1, false, false, &SamplerModulation::lfoPeriod},
     {"lfoAmount", "LFO amount", -1, 1, 0, false, false, &SamplerModulation::lfoAmount},
     {"lfoShape", "LFO shape", 0, 2, 0, true}}};
inline int samplerModulationTarget(std::string_view id) noexcept
{
    for (size_t i = 0; i < samplerModulationTargets.size(); ++i)
        if (samplerModulationTargets[i] == id)
            return int(i);
    return -1;
}
inline const SamplerModulationParameter *samplerModulationParameter(std::string_view id) noexcept
{
    for (const auto &p : samplerModulationParameters)
        if (p.id == id)
            return &p;
    return nullptr;
}
inline double samplerModulationValue(const SamplerModulation &s, const SamplerModulationParameter &p) noexcept
{
    if (p.number)
        return s.*p.number;
    if (p.flag)
        return s.*p.flag ? 1. : 0.;
    return s.lfoShape;
}
inline bool setSamplerModulationValue(SamplerModulation &s, const SamplerModulationParameter &p,
                                      double value) noexcept
{
    if (!std::isfinite(value) || value < p.minimum || value > p.maximum ||
        (p.integral && std::floor(value) != value))
        return false;
    if (p.number)
        s.*p.number = value;
    else if (p.flag)
        s.*p.flag = value != 0;
    else
        s.lfoShape = int(value);
    return true;
}
inline bool validSamplerModulation(const SamplerModulation &s, size_t target) noexcept
{
    if (target >= samplerModulationTargets.size() || (target == 1 && s.enabled))
        return false; // Familiar Pan page provides LFO only.
    for (const auto &p : samplerModulationParameters)
    {
        const auto value = samplerModulationValue(s, p);
        if (!std::isfinite(value) || value < p.minimum || value > p.maximum ||
            (p.integral && std::floor(value) != value))
            return false;
    }
    return true;
}
} // namespace vibera
