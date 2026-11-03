// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>

// Original fixture specification, not a recording of application output.
// Exact dyadic stereo amplitudes;65521-frame period exceeds every tested delay.
// Reducing before multiplication avoids overflow for an arbitrarily long run.
namespace mixer_clock_waveform
{
inline float sample(std::int64_t frame, int channel) noexcept
{
    if (frame < 0)
        return 0;
    const auto phase = std::uint64_t(frame) % 65521;
    const auto slope = channel == 0 ? 73u : 151u;
    const auto offset = channel == 0 ? 997u : 1907u;
    const auto value = int((phase * slope + offset) % 65521) - 32760;
    return float(value) / 262144.f;
}
} // namespace mixer_clock_waveform
