// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EffectsTypes.h"

#include <array>
#include <memory>
#include <vector>

namespace vibera::effects
{
// VibeRa Tune: monophonic pitch correction to a Western scale or a maqam (24-tone).
// Detection: YIN on a 2:1 decimated mono copy every 256 input samples. Correction: the
// nearest scale degree, approached over the retune time; Humanize slows it on held notes
// and Flex leaves notes far from any degree (slides, ornaments) alone. Shifting: the
// vendored Signalsmith Stretch (MIT) transposes in 128-sample blocks, keeping the
// voice formants unless Keep formants is off. The fixed delay is reported as plugin
// latency so the host compensates it. prepare() is the only allocating call.
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
    // Constant added delay in samples (reported to the host as latency).
    int latencySamples() const noexcept { return latency_; }
    PitchCorrect();
    ~PitchCorrect();
    PitchCorrect(PitchCorrect &&) noexcept;
    PitchCorrect &operator=(PitchCorrect &&) noexcept;

  private:
    void analyse() noexcept;
    double nearestDegree(double hz) const noexcept;
    double sampleRate_ = 48000;
    int key_ = 0, scale_ = 0;
    double retuneSeconds_ = .04, amount_ = 1, mix_ = 1, humanize_ = 0, flexCents_ = 0, transpose_ = 1;
    bool formants_ = true;
    double heldSeconds_ = 0, lastTarget_ = 0;
    // Analysis (decimated by two).
    std::vector<float> analysis_;
    std::vector<float> difference_, raw_; // Normalized and raw YIN difference per lag.
    int analysisWrite_ = 0, hopCounter_ = 0;
    float decimateCarry_ = 0;
    bool decimatePhase_ = false;
    double detected_ = 0, target_ = 0, ratio_ = 1, ratioTarget_ = 1;
    // Shifter: block FIFOs around the stretcher, an aligned dry path for Mix, and the
    // ratio history so each block uses the ratio detected for the audio it carries.
    struct Stretch;
    std::unique_ptr<Stretch> stretch_;
    static constexpr int block = 128;
    std::array<float, block> inL_{}, inR_{}, outL_{}, outR_{};
    int fill_ = 0, latency_ = 0, ratioWrite_ = 0;
    std::vector<float> dryL_, dryR_;
    std::vector<double> ratioHistory_;
    int dryWrite_ = 0;
    bool prepared_ = false;
};
} // namespace vibera::effects
