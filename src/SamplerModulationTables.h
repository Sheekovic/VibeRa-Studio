// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <numbers>

namespace vibera
{
// Rate-independent original tables. First preparation MUST run on CONTROL.
// Voices/banks borrow immutable pointers; no callback invokes this accessor.
struct SamplerModulationTables
{
    static constexpr size_t sineIntervals = 2048, pitchIntervals = 4096;
    std::array<double, sineIntervals + 1> sine{};
    std::array<double, pitchIntervals + 1> pitch{};

    static const SamplerModulationTables &prepareOnControl() noexcept
    {
        static const SamplerModulationTables tables;
        return tables;
    }

  private:
    SamplerModulationTables() noexcept
    {
        for (size_t i = 0; i <= sineIntervals; ++i)
            sine[i] = std::sin(2. * std::numbers::pi * double(i) / sineIntervals);
        sine[0] = sine[sineIntervals] = 0.;
        sine[sineIntervals / 4] = 1.;
        sine[sineIntervals / 2] = 0.;
        sine[3 * sineIntervals / 4] = -1.;
        for (size_t i = 0; i <= pitchIntervals; ++i)
            pitch[i] = std::exp2(2. * (double(i) / 1024. - 2.));
    }
};
} // namespace vibera
