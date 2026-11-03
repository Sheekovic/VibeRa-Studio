// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EffectsTypes.h"

namespace vibera::effects
{
// Stereo-linked dynamics and deliberately original nonlinear effects. All
// working storage is fixed; parameter changes preserve running detector state.
class Dynamics
{
  public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(EffectId effect, const Values &values) noexcept;
    void process(float &left, float &right) noexcept;

  private:
    [[nodiscard]] float timeCoefficient(float milliseconds) const noexcept;
    void smoothParameters() noexcept;

    EffectId effect_ = EffectId::balance;
    Values target_{};
    Values current_{};
    double sampleRate_ = 44100.0;
    float parameterCoefficient_ = 0.9954751f;
    float gain_ = 1.0f;
    float heldLeft_ = 0.0f;
    float heldRight_ = 0.0f;
    unsigned int holdCounter_ = 0;
};
} // namespace vibera::effects
