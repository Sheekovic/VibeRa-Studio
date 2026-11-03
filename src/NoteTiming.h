// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace vibera
{
// Quarter-note beats. Validate stored percentages at model/admission boundaries.
// Apply once after Swing on each raw occurrence, never to source extent/loop periods.
inline double noteShiftDelay(double percent) noexcept
{
    return percent * .002475;
}
} // namespace vibera
