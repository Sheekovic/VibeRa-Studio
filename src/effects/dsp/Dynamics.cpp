// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Dynamics.h"

#include <algorithm>
#include <cmath>

namespace vibera::effects
{
namespace
{
float bounded(float value, float minimum, float maximum, float initial) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : initial;
}

float decibelsToGain(float decibels) noexcept
{
    return std::pow(10.0f, decibels * 0.05f);
}

float finiteSample(float sample) noexcept
{
    // Silence non-finite input and inaudible denormals. The generous guard also
    // keeps intermediate ratios finite for malformed upstream signals.
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-30f)
        return 0.0f;
    return std::clamp(sample, -1.0e6f, 1.0e6f);
}

bool supported(EffectId effect) noexcept
{
    switch (effect)
    {
    case EffectId::softClip:
    case EffectId::distortion:
    case EffectId::bitCrusher:
    case EffectId::compressor:
    case EffectId::limiter:
    case EffectId::gate:
        return true;
    default:
        return false;
    }
}

float quantize(float sample, int bits) noexcept
{
    // Signed PCM-style quantization: 2^bits equally spaced values, including
    // -1 and excluding +1. Saturation prevents integer-domain wraparound.
    const float scale = std::ldexp(1.0f, bits - 1);
    const float limited = std::clamp(sample, -1.0f, 1.0f - 1.0f / scale);
    return std::round(limited * scale) / scale;
}
} // namespace

void Dynamics::prepare(double sampleRate)
{
    effect_ = EffectId::balance;
    target_ = {};
    current_ = {};
    sampleRate_ =
        std::isfinite(sampleRate) && sampleRate > 0.0 ? std::clamp(sampleRate, 1000.0, 768000.0) : 44100.0;
    parameterCoefficient_ = timeCoefficient(5.0f);
    reset();
}

void Dynamics::reset() noexcept
{
    current_ = target_;
    gain_ = effect_ == EffectId::gate ? decibelsToGain(target_[3]) : 1.0f;
    heldLeft_ = heldRight_ = 0.0f;
    holdCounter_ = 0;
}

void Dynamics::setParameters(EffectId effect, const Values &values) noexcept
{
    Values next{};
    const auto parameter = [&](std::size_t index, float minimum, float maximum, float initial)
    { next[index] = bounded(values[index], minimum, maximum, initial); };
    switch (effect)
    {
    case EffectId::softClip:
        parameter(0, -36.0f, 0.0f, -6.0f);
        parameter(1, -24.0f, 12.0f, 0.0f);
        parameter(2, 0.0f, 1.0f, 1.0f);
        break;
    case EffectId::distortion:
        parameter(0, 0.0f, 36.0f, 6.0f);
        parameter(1, -36.0f, 12.0f, -6.0f);
        parameter(2, 0.0f, 1.0f, 1.0f);
        break;
    case EffectId::bitCrusher:
        parameter(0, 2.0f, 24.0f, 12.0f);
        parameter(1, 1.0f, 64.0f, 1.0f);
        parameter(2, 0.0f, 1.0f, 1.0f);
        next[0] = std::round(next[0]);
        next[1] = std::round(next[1]);
        break;
    case EffectId::compressor:
        parameter(0, -60.0f, 0.0f, -18.0f);
        parameter(1, 1.0f, 20.0f, 4.0f);
        parameter(2, 0.1f, 200.0f, 10.0f);
        parameter(3, 5.0f, 2000.0f, 120.0f);
        parameter(4, -12.0f, 24.0f, 0.0f);
        parameter(5, 0.0f, 1.0f, 1.0f);
        break;
    case EffectId::limiter:
        parameter(0, -24.0f, 0.0f, -1.0f);
        parameter(1, 5.0f, 1000.0f, 80.0f);
        parameter(2, -12.0f, 24.0f, 0.0f);
        break;
    case EffectId::gate:
        parameter(0, -90.0f, 0.0f, -48.0f);
        parameter(1, 0.1f, 100.0f, 2.0f);
        parameter(2, 5.0f, 2000.0f, 150.0f);
        parameter(3, -96.0f, 0.0f, -96.0f);
        break;
    default:
        break;
    }

    const bool changedEffect = effect != effect_;
    effect_ = effect;
    target_ = next;
    if (changedEffect)
        reset();
}

float Dynamics::timeCoefficient(float milliseconds) const noexcept
{
    return static_cast<float>(std::exp(-1000.0 / (sampleRate_ * milliseconds)));
}

void Dynamics::smoothParameters() noexcept
{
    const float amount = 1.0f - parameterCoefficient_;
    for (std::size_t index = 0; index < maximumParameters; ++index)
    {
        if (effect_ == EffectId::bitCrusher && index < 2)
            current_[index] = target_[index];
        else
        {
            current_[index] += amount * (target_[index] - current_[index]);
            if (std::abs(current_[index]) < 1.0e-30f)
                current_[index] = 0.0f;
        }
    }
}

void Dynamics::process(float &left, float &right) noexcept
{
    if (!supported(effect_))
        return;

    smoothParameters();
    left = finiteSample(left);
    right = finiteSample(right);
    const float dryLeft = left;
    const float dryRight = right;
    float mix = 1.0f;

    switch (effect_)
    {
    case EffectId::softClip:
    {
        // This threshold-scaled tanh curve is continuous, has unity slope at
        // zero, and approaches +/-threshold before the output control.
        const float threshold = decibelsToGain(current_[0]);
        const float output = decibelsToGain(current_[1]);
        left = threshold * std::tanh(left / threshold) * output;
        right = threshold * std::tanh(right / threshold) * output;
        mix = current_[2];
        break;
    }
    case EffectId::distortion:
    {
        const float drive = decibelsToGain(current_[0]);
        const float output = decibelsToGain(current_[1]);
        left = std::tanh(left * drive) * output;
        right = std::tanh(right * drive) * output;
        mix = current_[2];
        break;
    }
    case EffectId::bitCrusher:
    {
        const auto bits = static_cast<int>(current_[0]);
        const auto factor = static_cast<unsigned int>(current_[1]);
        if (holdCounter_ == 0 || holdCounter_ >= factor)
        {
            heldLeft_ = quantize(left, bits);
            heldRight_ = quantize(right, bits);
            holdCounter_ = 0;
        }
        ++holdCounter_;
        left = heldLeft_;
        right = heldRight_;
        mix = current_[2];
        break;
    }
    case EffectId::compressor:
    {
        const float peak = std::max(std::abs(left), std::abs(right));
        const float threshold = decibelsToGain(current_[0]);
        float desiredGain = 1.0f;
        if (peak > threshold)
        {
            const float aboveThresholdDb = 20.0f * std::log10(peak / threshold);
            desiredGain = decibelsToGain(-aboveThresholdDb * (1.0f - 1.0f / current_[1]));
        }
        const float coefficient = timeCoefficient(desiredGain < gain_ ? current_[2] : current_[3]);
        gain_ = desiredGain + coefficient * (gain_ - desiredGain);
        const float output = gain_ * decibelsToGain(current_[4]);
        left *= output;
        right *= output;
        mix = current_[5];
        break;
    }
    case EffectId::limiter:
    {
        // Lowering a ceiling must protect the very next sample; raising it is
        // smoothed. This is a zero-latency sample-peak limiter, not true peak.
        const float ceiling = decibelsToGain(std::min(current_[0], target_[0]));
        const float input = decibelsToGain(current_[2]);
        left *= input;
        right *= input;
        const float peak = std::max(std::abs(left), std::abs(right));
        const float desiredGain = peak > ceiling ? ceiling / peak : 1.0f;
        if (desiredGain < gain_)
            gain_ = desiredGain;
        else
            gain_ = desiredGain + timeCoefficient(current_[1]) * (gain_ - desiredGain);
        left = std::clamp(left * gain_, -ceiling, ceiling);
        right = std::clamp(right * gain_, -ceiling, ceiling);
        break;
    }
    case EffectId::gate:
    {
        const float peak = std::max(std::abs(left), std::abs(right));
        const float floor = decibelsToGain(current_[3]);
        const float desiredGain = peak >= decibelsToGain(current_[0]) ? 1.0f : floor;
        const float coefficient = timeCoefficient(desiredGain > gain_ ? current_[1] : current_[2]);
        gain_ = desiredGain + coefficient * (gain_ - desiredGain);
        left *= gain_;
        right *= gain_;
        break;
    }
    default:
        break;
    }

    // Exact dry endpoints avoid cancellation error and make dry mix a true
    // identity for ordinary finite input, even while wet state keeps advancing.
    if (mix <= 0.0f)
    {
        left = dryLeft;
        right = dryRight;
    }
    else if (mix < 1.0f)
    {
        left = dryLeft + mix * (left - dryLeft);
        right = dryRight + mix * (right - dryRight);
    }
}
} // namespace vibera::effects
