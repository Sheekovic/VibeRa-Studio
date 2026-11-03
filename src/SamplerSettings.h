// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerFilterSettings.h"
#include "SamplerModulationSettings.h"
#include <cmath>
#include <algorithm>

namespace vibera
{
// Non-destructive per-Channel playback settings; PCM remains immutable/shared.
struct SamplerSettings
{
    int root = -1; // -1 uses the prepared sample's original root.
    double cents = 0, start = 0, end = 1;
    double sourceTempo = 0; // Unknown until set; never modifies the source file.
    bool fitTempo = false;
    // With fitTempo, keep pitch: playback uses a prepared time-stretched copy of
    // the source instead of resampling. Ignored while fitTempo is off.
    bool stretch = false;
    bool reverse = false, envelope = false;
    bool normalize = false; // Whole loaded stereo source peak; file and PCM stay immutable.
    bool loop = false, pingPong = false;
    double loopStart = 0, loopEnd = 1;
    double attack = .002, decay = .1, sustain = 1, release = .012;
    // Source-time fades (seconds,0..10) at the playback start/end of the trimmed
    // region, like precomputed fades: looped audio passing those points dips too.
    double fadeIn = 0, fadeOut = 0;
    // Loop crossfade seconds,0..10. Blends the audio before the loop start into the
    // loop end; limited to half the loop and the available pre-loop source.
    double loopCrossfade = 0;
    bool hqInterpolation = false; // 4-point Hermite instead of linear source interpolation.
    int cut = 0, cutBy = 0, maxPoly = 0; // 0: no group / no user voice limit; groups and limits1..99.
    int filterType = samplerFilterDefaultType;
    double filterCutoff = samplerFilterDefaultCutoff, filterResonance = samplerFilterDefaultResonance;
    std::array<SamplerModulation, 5> modulation{};
    // Original sampler voice controls. Off/defaults preserve the legacy path.
    bool mono = false, portamento = false, fullPortamento = false;
    double slideTime = .06; // Seconds,0..10. No exact FL glide curve/default claim.
    double gateBeats = 0;   // Quarter-note beats,0..64;0 disables the automatic gate.
    bool operator==(const SamplerSettings &) const = default;
    bool valid() const noexcept
    {
        for (size_t target = 0; target < modulation.size(); ++target)
            if (!validSamplerModulation(modulation[target], target))
                return false;
        return std::isfinite(loopCrossfade) && loopCrossfade >= 0 && loopCrossfade <= 10 &&
               std::isfinite(fadeIn) && fadeIn >= 0 && fadeIn <= 10 && std::isfinite(fadeOut) &&
               fadeOut >= 0 && fadeOut <= 10 && std::isfinite(slideTime) && slideTime >= 0 && slideTime <= 10 && std::isfinite(gateBeats) &&
               gateBeats >= 0 && gateBeats <= 64 && filterType >= 0 && filterType < samplerFilterTypeCount &&
               std::isfinite(filterCutoff) && filterCutoff >= 0 && filterCutoff <= 1 &&
               std::isfinite(filterResonance) && filterResonance >= 0 && filterResonance <= 1 && cut >= 0 &&
               cut <= 99 && cutBy >= 0 && cutBy <= 99 && maxPoly >= 0 && maxPoly <= 99 &&
               std::isfinite(sourceTempo) &&
               (sourceTempo == 0 || (sourceTempo >= 20 && sourceTempo <= 400)) &&
               (!fitTempo || sourceTempo > 0) && root >= -1 && root <= 127 && std::isfinite(cents) &&
               std::abs(cents) <= 4800 && std::isfinite(start) && std::isfinite(end) && start >= 0 &&
               end <= 1 && end > start && std::isfinite(attack) && attack >= 0 && attack <= 10 &&
               std::isfinite(decay) && decay >= 0 && decay <= 10 && std::isfinite(sustain) && sustain >= 0 &&
               sustain <= 1 && std::isfinite(release) && release >= 0 && release <= 10 &&
               std::isfinite(loopStart) && std::isfinite(loopEnd) && loopStart >= 0 && loopEnd <= 1 &&
               loopStart < loopEnd && (!loop || std::max(start, loopStart) < std::min(end, loopEnd));
    }
};
struct PreparedSamplerSettings
{
    SamplerSettings values;
    double tuning = 1;
    double tempo = 120;
    explicit PreparedSamplerSettings(const SamplerSettings &s = {}, double projectTempo = 120)
        : values(s), tuning(std::pow(2., s.cents / 1200.) *
                            (s.fitTempo && !s.stretch ? projectTempo / s.sourceTempo : 1.)),
          tempo(projectTempo)
    {
    }
};
} // namespace vibera
