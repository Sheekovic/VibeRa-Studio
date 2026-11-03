// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EffectsTypes.h"

#include <array>

namespace vibera::effects
{
// Sixteen-band channel vocoder with an internal carrier: the input (modulator) envelope
// in each band shapes the same band of a band-limited sawtooth at `note` plus noise.
// Bands are log-spaced 120 Hz..8 kHz, two-pole band-passes; no allocation at all.
class Vocoder
{
  public:
    static constexpr int bands = 16;
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const Values &values) noexcept;
    void process(float &left, float &right) noexcept;
    // Current band envelopes of the modulator, for tests and display.
    const std::array<float, bands> &envelopes() const noexcept { return envelope_; }

  private:
    struct BandPass
    {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void design(double frequency, double q, double rate) noexcept;
        double run(double x) noexcept;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
    };
    double sampleRate_ = 48000, phase_ = 0, increment_ = 0;
    double attack_ = 0, release_ = 0, noise_ = .1, mix_ = 1, gain_ = 1;
    std::uint32_t random_ = 22222;
    std::array<BandPass, bands> analysis_, synthesis_;
    std::array<float, bands> envelope_{};
};
} // namespace vibera::effects
