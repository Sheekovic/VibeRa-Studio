// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EffectsTypes.h"

#include <array>
#include <vector>

namespace vibera::effects
{
// VibeRa Tune: monophonic pitch correction to a Western scale or a maqam (24-tone).
// Detection: YIN on a 2:1 decimated mono copy every 256 input samples. Correction: the
// nearest scale degree, approached over the retune time. Shifting: two read heads on a
// delay line, crossfaded over a 20 ms window (about 10 ms of added delay, not reported
// as plugin latency). prepare() is the only allocating call.
class PitchCorrect
{
  public:
    static constexpr int scaleCount = 15;
    static const char *scaleName(int index) noexcept;
    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(const Values &values) noexcept;
    void process(float &left, float &right) noexcept;
    // Last analysis, for tests and display: detected Hz (0 = unvoiced) and target Hz.
    double detectedHz() const noexcept { return detected_; }
    double targetHz() const noexcept { return target_; }

  private:
    void analyse() noexcept;
    double nearestDegree(double hz) const noexcept;
    double sampleRate_ = 48000;
    int key_ = 0, scale_ = 0;
    double retuneSeconds_ = .04, amount_ = 1, mix_ = 1;
    // Analysis (decimated by two).
    std::vector<float> analysis_;
    std::vector<float> difference_;
    int analysisWrite_ = 0, hopCounter_ = 0;
    float decimateCarry_ = 0;
    bool decimatePhase_ = false;
    double detected_ = 0, target_ = 0, ratio_ = 1, ratioTarget_ = 1;
    // Shifter.
    std::vector<float> lineL_, lineR_;
    int write_ = 0, window_ = 960;
    double phase_ = 0;
    bool prepared_ = false;
};
} // namespace vibera::effects
