// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace vibera
{
// CONTROL preparation only. These coefficients are copied into retained voices;
// no transcendental calculation, owner or allocation crosses the audio boundary.
// A neutral balance preserves the original stereo image/level exactly. At a
// hard endpoint the opposite source side is silent; there is no stereo fold-down.
struct PreparedNoteExpression
{
    float left = 1, right = 1;
    double tuning = 1;
    double filterCutoffOffset = 0, filterResonanceOffset = 0;

    bool operator==(const PreparedNoteExpression &) const = default;

    static bool valid(double pan, double cents, double modX = .5, double modY = .5) noexcept
    {
        return std::isfinite(pan) && pan >= -1 && pan <= 1 && std::isfinite(cents) && cents >= -100 &&
               cents <= 100 && std::isfinite(modX) && modX >= 0 && modX <= 1 && std::isfinite(modY) &&
               modY >= 0 && modY <= 1;
    }

    static PreparedNoteExpression prepare(double pan, double cents, double modX = .5,
                                          double modY = .5) noexcept
    {
        // Callers validate persisted/application inputs before preparation. Fail
        // neutral defensively rather than publish NaNs into an audio voice.
        if (!valid(pan, cents, modX, modY))
            return {};
        return {float(1 - std::max(0., pan)), float(1 + std::min(0., pan)),
                cents == 0 ? 1. : std::pow(2., cents / 1200.), 2 * (modX - .5), 2 * (modY - .5)};
    }
};
} // namespace vibera
