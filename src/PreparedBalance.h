// SPDX-License-Identifier: AGPL-3.0-or-later
// Prepared immutable targets; no parameters, ValueTrees or mutable global settings in DSP.
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <limits>

namespace vibera::dsp
{
class PreparedBalance
{
  public:
    enum class Mode
    {
        panOnly,
        channelGainAndPan
    };
    enum class Result
    {
        ok,
        notPrepared,
        invalidRate,
        invalidLaw,
        invalidTarget,
        invalidShape
    };
    struct Coefficients
    {
        float left = 1, right = 1, extra = 1;
    };

    // The caller resolves PanLawDefault from native settings BEFORE publication.
    // Mixer/Master retain their separate immutable gain stage and use panOnly.
    Result prepare(double sampleRate, Mode mode, tracktion::PanLaw resolvedLaw, float initialGain,
                   float initialPan) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 ||
            sampleRate * .015 > double(std::numeric_limits<int>::max()))
            return Result::invalidRate;
        if (!validLaw(resolvedLaw))
            return Result::invalidLaw;
        if (!validTarget(mode, initialGain, initialPan))
            return Result::invalidTarget;
        const auto coefficients = calculate(mode, resolvedLaw, initialGain, snapManualPan(initialPan));
        mode_ = mode;
        law_ = resolvedLaw;
        left.setCurrentAndTargetValue(coefficients.left);
        right.setCurrentAndTargetValue(coefficients.right);
        extra.setCurrentAndTargetValue(coefficients.extra);
        // Safe value-only JUCE primitive; preserves floor(sampleRate * .015) and endpoint semantics.
        left.reset(sampleRate, .015);
        right.reset(sampleRate, .015);
        extra.reset(sampleRate, .015);
        prepared = true;
        return Result::ok;
    }

    Result setManualTarget(float gain, float pan, int activeChannels) noexcept
    {
        if (!prepared)
            return Result::notPrepared;
        if (activeChannels <= 0)
            return Result::invalidShape;
        if (!validTarget(mode_, gain, pan))
            return Result::invalidTarget;
        const auto coefficients = calculate(mode_, law_, gain, snapManualPan(pan));
        left.setTargetValue(coefficients.left);
        right.setTargetValue(coefficients.right);
        // Native mono/stereo processing does not retarget or advance its extra-channel gain.
        if (activeChannels > 2)
            extra.setTargetValue(coefficients.extra);
        return Result::ok;
    }

    // Call once PER FRAME, not once per channel. Missing-channel ramp state remains untouched.
    Result next(int activeChannels, Coefficients &result) noexcept
    {
        if (!prepared)
            return Result::notPrepared;
        if (activeChannels <= 0)
            return Result::invalidShape;
        result.left = left.getNextValue();
        result.right = activeChannels > 1 ? right.getNextValue() : right.getCurrentValue();
        result.extra = activeChannels > 2 ? extra.getNextValue() : extra.getCurrentValue();
        return Result::ok;
    }

    // Sample-accurate automation bypasses the MANUAL ramp and its centre snap.
    // This computes coefficients without modifying the manual state. The adapter
    // keeps advancing the manual ramp underneath automation, as AutomationGain does.
    Result automated(float gain, float pan, Coefficients &result) const noexcept
    {
        if (!prepared)
            return Result::notPrepared;
        if (!validTarget(mode_, gain, pan))
            return Result::invalidTarget;
        result = calculate(mode_, law_, gain, pan);
        return Result::ok;
    }

    // Convenience for channel adapters. Invalid shape/target is atomic: no PCM or ramp mutation.
    // Zero frames is a no-op. Inputs are not clipped, sanitized, or silently muted.
    Result process(juce::AudioBuffer<float> &buffer, int start, int frames, float gain, float pan) noexcept
    {
        if (!prepared)
            return Result::notPrepared;
        if (start < 0 || frames < 0 || start > buffer.getNumSamples() ||
            frames > buffer.getNumSamples() - start || buffer.getNumChannels() <= 0)
            return Result::invalidShape;
        if (!validTarget(mode_, gain, pan))
            return Result::invalidTarget;
        if (frames == 0)
            return Result::ok;
        const auto channels = buffer.getNumChannels();
        const auto target = setManualTarget(gain, pan, channels);
        if (target != Result::ok)
            return target;
        for (int frame = 0; frame < frames; ++frame)
        {
            Coefficients c;
            next(channels, c);
            buffer.getWritePointer(0)[start + frame] *= c.left;
            if (channels > 1)
                buffer.getWritePointer(1)[start + frame] *= c.right;
            for (int channel = 2; channel < channels; ++channel)
                buffer.getWritePointer(channel)[start + frame] *= c.extra;
        }
        return Result::ok;
    }

  private:
    bool prepared = false;
    Mode mode_ = Mode::panOnly;
    tracktion::PanLaw law_ = tracktion::PanLawLinear;
    juce::SmoothedValue<float> left{1}, right{1}, extra{1};

    static bool validLaw(tracktion::PanLaw law) noexcept
    {
        switch (law)
        {
        case tracktion::PanLawLinear:
        case tracktion::PanLaw2point5dBCenter:
        case tracktion::PanLaw3dBCenter:
        case tracktion::PanLaw4point5dBCenter:
        case tracktion::PanLaw6dBCenter:
            return true;
        default:
            return false;
        }
    }
    static bool validTarget(Mode mode, float gain, float pan) noexcept
    {
        return (mode == Mode::panOnly || mode == Mode::channelGainAndPan) && std::isfinite(gain) &&
               gain >= 0 && gain <= 2 && std::isfinite(pan) && pan >= -1 && pan <= 1 &&
               (mode != Mode::panOnly || gain == 1);
    }
    static float snapManualPan(float pan) noexcept
    {
        return pan >= -.005f && pan <= .005f ? 0.f : pan;
    }
    static Coefficients calculate(Mode mode, tracktion::PanLaw law, float gain, float pan) noexcept
    {
        // Preserve the existing native CHANNEL control conversion, including
        // -100 dB silence and the +6 dB fader ceiling. These are pure helpers.
        const auto db = mode == Mode::panOnly ? 0.f : juce::Decibels::gainToDecibels(gain, -100.f);
        const auto fader = juce::jlimit(0.f, 1.f, tracktion::decibelsToVolumeFaderPosition(db));
        Coefficients result;
        tracktion::getGainsFromVolumeFaderPositionAndPan(fader, pan, law, result.left, result.right);
        result.extra = tracktion::volumeFaderPositionToGain(fader);
        return result;
    }
};
} // namespace vibera::dsp
