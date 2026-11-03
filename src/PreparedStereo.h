// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <limits>

namespace vibera::dsp
{
// Original linear cross-mix. Centre is bit-exact identity; positive separation
// merges to mono, negative separation subtracts the opposite channel. The
// latter can remove common/centre content: it does not manufacture stereo.
class PreparedStereo
{
  public:
    struct Target
    {
        float separation = 0;
        bool invert = false, swap = false;
    };
    static bool valid(Target target) noexcept
    {
        return std::isfinite(target.separation) && target.separation >= -1 && target.separation <= 1;
    }
    static std::array<float, 4> matrix(Target target) noexcept
    {
        const float own = target.separation > 0 ? 1 - .5f * target.separation : 1;
        const float cross = target.separation > 0 ? .5f * target.separation : target.separation;
        const float sign = target.invert ? -1.f : 1.f;
        return target.swap ? std::array<float, 4>{sign * cross, sign * own, sign * own, sign * cross}
                           : std::array<float, 4>{sign * own, sign * cross, sign * cross, sign * own};
    }
    bool prepare(double sampleRate, Target target) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 ||
            sampleRate * .005 > double(std::numeric_limits<int>::max()) || !valid(target))
            return false;
        const auto values = matrix(target);
        for (size_t i = 0; i < values.size(); ++i)
        {
            coefficients[i].setCurrentAndTargetValue(values[i]);
            coefficients[i].reset(sampleRate, .005);
        }
        const auto flags = matrix({0, target.invert, target.swap});
        for (size_t i = 0; i < flags.size(); ++i)
        {
            orientation[i].setCurrentAndTargetValue(flags[i]);
            orientation[i].reset(sampleRate, .005);
        }
        ready = true;
        return true;
    }
    bool setTarget(Target target) noexcept
    {
        if (!ready || !valid(target))
            return false;
        const auto values = matrix(target);
        for (size_t i = 0; i < values.size(); ++i)
            coefficients[i].setTargetValue(values[i]);
        const auto flags = matrix({0, target.invert, target.swap});
        for (size_t i = 0; i < flags.size(); ++i)
            orientation[i].setTargetValue(flags[i]);
        return true;
    }
    void process(float &left, float &right) noexcept
    {
        if (!ready)
            return;
        advanceOrientation(); // Keep automated flag transition clock current without changing manual PCM.
        const auto a = coefficients[0].getNextValue(), b = coefficients[1].getNextValue();
        const auto c = coefficients[2].getNextValue(), d = coefficients[3].getNextValue();
        if (a == 1 && b == 0 && c == 0 && d == 1)
            return;
        const float oldLeft = left, oldRight = right;
        left = a * oldLeft + b * oldRight;
        right = c * oldLeft + d * oldRight;
    }

    // Sample-accurate separation, without the MANUAL separation ramp. Manual
    // matrix and discrete orientation clocks keep advancing underneath. During
    // automation polarity/swap interpolate their orientation matrix over5ms;
    // the instantaneous separation matrix is then composed with that matrix.
    // This explicit automated contract leaves all legacy manual output intact.
    bool processAutomated(float &left, float &right, float separation) noexcept
    {
        if (!ready || !std::isfinite(separation) || separation < -1 || separation > 1) return false;
        for (auto &value : coefficients) (void)value.getNextValue();
        const auto flags = advanceOrientation();
        if (separation == 0 && flags[0] == 1 && flags[1] == 0 && flags[2] == 0 && flags[3] == 1)
            return true; // Bit-exact identity after advancing the prepared clocks.
        const float own = separation > 0 ? 1 - .5f * separation : 1;
        const float cross = separation > 0 ? .5f * separation : separation;
        const float sourceLeft = left, sourceRight = right;
        const float separatedLeft = own * sourceLeft + cross * sourceRight;
        const float separatedRight = cross * sourceLeft + own * sourceRight;
        left = flags[0] * separatedLeft + flags[1] * separatedRight;
        right = flags[2] * separatedLeft + flags[3] * separatedRight;
        return true;
    }

  private:
    std::array<float, 4> advanceOrientation() noexcept
    {
        return {orientation[0].getNextValue(), orientation[1].getNextValue(),
                orientation[2].getNextValue(), orientation[3].getNextValue()};
    }
    std::array<juce::SmoothedValue<float>, 4> orientation;
    std::array<juce::SmoothedValue<float>, 4> coefficients;
    bool ready = false;
};
} // namespace vibera::dsp
