// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EffectsTypes.h"

#include <array>
#include <cstddef>
#include <vector>

namespace vibera::effects
{
// Original time-domain effects. prepare() is the only allocating operation.
// All remaining methods are bounded and intended for the audio processing thread;
// parameter changes must be delivered on that thread rather than concurrently.
class TimeEffects
{
  public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(EffectId effect, const Values &values) noexcept;
    void process(float &left, float &right) noexcept;

  private:
    struct DelayLine
    {
        std::vector<float> samples;
        std::size_t position = 0;
        std::size_t validSamples = 0;

        void prepare(std::size_t length);
        void reset() noexcept;
        void write(float sample) noexcept;
        float read(double delaySamples) const noexcept;
    };

    struct Comb
    {
        DelayLine line;
        float damped = 0.0f;
        float feedback = 0.8f;
        float targetFeedback = 0.8f;
    };

    static constexpr std::size_t combCount = 8;
    static constexpr std::size_t diffuserCount = 2;
    static constexpr std::size_t phaserStageCount = 6;

    void updateCoefficients() noexcept;
    void advancePhase(double frequency) noexcept;
    float processReverb(float input, std::size_t channel) noexcept;
    float processPhaser(float input, std::size_t channel, double wave) noexcept;

    double sampleRate_ = 48000.0;
    float smoothing_ = 0.001f;
    EffectId effect_ = EffectId::delay;
    Values targets_{250.0f, 0.35f, 0.3f, 0.0f, 8000.0f};
    Values current_ = targets_;
    std::size_t parameterCount_ = 5;
    bool prepared_ = false;
    bool parametersSet_ = false;

    std::array<DelayLine, 2> delays_;
    std::array<float, 2> delayFiltered_{};
    float toneCoefficient_ = 0.0f;
    float targetToneCoefficient_ = 0.0f;
    double phase_ = 0.0;

    std::array<std::array<float, phaserStageCount>, 2> phaserMemory_{};
    std::array<float, 2> phaserFeedback_{};

    std::array<DelayLine, 2> predelays_;
    std::array<std::array<Comb, combCount>, 2> combs_;
    std::array<std::array<DelayLine, diffuserCount>, 2> diffusers_;
    float dampingCoefficient_ = 0.0f;
    float targetDampingCoefficient_ = 0.0f;
};
} // namespace vibera::effects
