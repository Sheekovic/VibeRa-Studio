// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace vibera
{
// Source fields are strictly validated before use. Defensive pure conversion;
// zero preserves legacy note-off bytes. No allocation, ownership or DSP policy.
inline std::uint8_t noteReleaseMidiVelocity(double normalized) noexcept
{
    return std::isfinite(normalized) ? std::uint8_t(std::lround(std::clamp(normalized, 0., 1.) * 127.))
                                     : std::uint8_t(0);
}
} // namespace vibera
