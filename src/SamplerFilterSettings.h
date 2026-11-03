// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace vibera
{
inline constexpr int samplerFilterTypeCount = 6;
inline constexpr int samplerFilterDefaultType = 1;
inline constexpr double samplerFilterDefaultCutoff = 1, samplerFilterDefaultResonance = 0;
inline constexpr double samplerFilterNormalizedMinimum = 0, samplerFilterNormalizedMaximum = 1;
inline constexpr double samplerFilterMinimumHz = 20, samplerFilterMaximumHz = 20000;
inline constexpr double samplerFilterMinimumQ = .7071067811865476, samplerFilterMaximumQ = 12;
inline const char *samplerFilterTypeName(int type) noexcept
{
    static constexpr const char *names[]{"Off", "LP12", "LP24", "HP12", "BP12", "BS12"};
    return type >= 0 && type < samplerFilterTypeCount ? names[type] : "Unknown";
}
// Original VibeRa note/global policy; not a recovered FL combination rule.
// Validated note controls prepare signed offsets; neutral.5 prepares exact zero.
inline double samplerFilterModulatedValue(double global, double offset) noexcept
{
    return std::clamp(global + offset, 0., 1.);
}
// CONTROL preparation/readback only. Never evaluate pow in a sample callback.
// Settings validation rejects invalid values; defensive metadata calls clamp
// finite normalized values and return0 Hz when normalization/rate is unavailable.
inline double samplerFilterCutoffHz(double normalized, double rate = 48000)
{
    if (!std::isfinite(normalized) || !std::isfinite(rate) || rate <= 0)
        return 0;
    return std::min(samplerFilterMinimumHz * std::pow(1000., std::clamp(normalized, 0., 1.)), .45 * rate);
}
inline double samplerFilterQ(double normalized) noexcept
{
    return samplerFilterMinimumQ + (samplerFilterMaximumQ - samplerFilterMinimumQ) *
                                       (std::isfinite(normalized) ? std::clamp(normalized, 0., 1.) : 0.);
}
} // namespace vibera
