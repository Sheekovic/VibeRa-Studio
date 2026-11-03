// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <cmath>
#include <set>

namespace vibera
{
// Quarter-note beats. A continuous, monotonic sixteenth-pair warp keeps free
// timing ordered: even sixteenths stay fixed; odd sixteenths move up to half a
// step. This is our independent off-grid policy, not recovered vendor code.
inline double swingDelay(double beat, double global, const Channel &channel) noexcept
{
    if (channel.audioClip || global == 0 || channel.swingMix == 0)
        return 0;
    const auto phase = std::fmod(beat, .5);
    return std::min(phase, .5 - phase) * .5 * global * channel.swingMix;
}

} // namespace vibera
