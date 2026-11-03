// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsCatalogue.h"
#include "Utilities.h"
#include "Dynamics.h"
#include "TimeEffects.h"
#include "PitchCorrect.h"
#include "Vocoder.h"
#include <cmath>
#include <stdexcept>

namespace vibera::effects
{
class Engine
{
    Utilities utilities;
    Dynamics dynamics;
    TimeEffects time;
    PitchCorrect tune;
    Vocoder vocoder;
    EffectId selected = EffectId::balance;
    bool prepared = false;
    bool configured = false;

  public:
    void prepare(double sampleRate)
    {
        if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000)
            throw std::invalid_argument("VibeRa Effects requires a sample rate from 8 to 384 kHz");
        utilities.prepare(sampleRate);
        dynamics.prepare(sampleRate);
        time.prepare(sampleRate);
        tune.prepare(sampleRate);
        vocoder.prepare(sampleRate);
        prepared = true;
        selected = EffectId::balance;
        configured = false;
    }
    void reset() noexcept
    {
        utilities.reset();
        dynamics.reset();
        time.reset();
        tune.reset();
        vocoder.reset();
    }
    // Bad automation values are refused before changing the active DSP settings.
    bool setParameters(EffectId effect, const Values &values) noexcept
    {
        const auto index = static_cast<std::size_t>(effect);
        if (!prepared || index >= catalogue::effects.size())
            return false;
        const auto &spec = catalogue::effects[index];
        for (std::size_t i = 0; i < spec.parameterCount; ++i)
        {
            const auto &p = spec.parameters[i];
            if (!std::isfinite(values[i]) || values[i] < p.minimum || values[i] > p.maximum ||
                (p.interval > 0 && std::abs((values[i] - p.minimum) / p.interval -
                                            std::round((values[i] - p.minimum) / p.interval)) > .0001f))
                return false;
        }
        if (effect != selected)
            reset(); // Fixed scalar state and O(1) logical delay invalidation only.
        selected = effect;
        configured = true;
        if (effect <= EffectId::filter)
            utilities.setParameters(effect, values);
        else if (effect <= EffectId::gate)
            dynamics.setParameters(effect, values);
        else if (effect <= EffectId::autoPan)
            time.setParameters(effect, values);
        else if (effect == EffectId::pitchCorrect)
            tune.setParameters(values);
        else
            vocoder.setParameters(values);
        return true;
    }
    void process(float *left, float *right, int frames) noexcept
    {
        if (!prepared || !configured || !left || !right || frames <= 0)
            return;
        for (int i = 0; i < frames; ++i)
        {
            auto l = std::isfinite(left[i]) ? std::clamp(left[i], -1.0e6f, 1.0e6f) : 0.0f;
            auto r = std::isfinite(right[i]) ? std::clamp(right[i], -1.0e6f, 1.0e6f) : 0.0f;
            if (selected <= EffectId::filter)
                utilities.process(l, r);
            else if (selected <= EffectId::gate)
                dynamics.process(l, r);
            else if (selected <= EffectId::autoPan)
                time.process(l, r);
            else if (selected == EffectId::pitchCorrect)
                tune.process(l, r);
            else
                vocoder.process(l, r);
            left[i] = std::isfinite(l) ? l : 0.0f;
            right[i] = std::isfinite(r) ? r : 0.0f;
        }
    }
};
} // namespace vibera::effects
