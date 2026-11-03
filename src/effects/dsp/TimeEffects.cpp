// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TimeEffects.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibera::effects
{
namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;
constexpr std::array<double, 8> combMilliseconds{29.7, 37.1, 41.3, 43.7, 53.1, 59.3, 61.7, 67.7};
constexpr std::array<double, 2> diffuserMilliseconds{4.3, 1.7};

float finiteAudio(float value) noexcept
{
    // Only pathological magnitudes are bounded; ordinary audio is not clipped.
    if (!std::isfinite(value) || std::abs(value) < 1.0e-20f)
        return 0.0f;
    return std::clamp(value, -1.0e6f, 1.0e6f);
}

float parameter(float value, float low, float high, float initial) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : initial;
}

float blend(float dry, float wet, float mix) noexcept
{
    return finiteAudio(dry + mix * (wet - dry));
}

double combTime(std::size_t channel, std::size_t index, float size) noexcept
{
    // Independent stereo tuning, with no claim of another reverb's topology.
    const double offset = channel == 0 ? 0.0 : 0.71 + 0.13 * static_cast<double>(index);
    return (combMilliseconds[index] + offset) * (0.65 + 0.7 * size) * 0.001;
}
} // namespace

void TimeEffects::DelayLine::prepare(std::size_t length)
{
    samples.assign(std::max<std::size_t>(length, 4), 0.0f);
    reset();
}

void TimeEffects::DelayLine::reset() noexcept
{
    // Logical invalidation keeps reset/type automation independent of buffer size.
    position = 0;
    validSamples = 0;
}

void TimeEffects::DelayLine::write(float sample) noexcept
{
    if (samples.empty())
        return;
    samples[position] = finiteAudio(sample);
    if (++position == samples.size())
        position = 0;
    validSamples = std::min(validSamples + 1, samples.size());
}

float TimeEffects::DelayLine::read(double delaySamples) const noexcept
{
    if (samples.size() < 4 || validSamples == 0)
        return 0.0f;
    const double delay = std::clamp(delaySamples, 1.0, static_cast<double>(samples.size() - 2));
    const auto whole = static_cast<std::size_t>(delay);
    const float fraction = static_cast<float>(delay - static_cast<double>(whole));
    const auto newerIndex = position >= whole ? position - whole : samples.size() - (whole - position);
    const auto olderIndex = newerIndex == 0 ? samples.size() - 1 : newerIndex - 1;
    const float newer = validSamples >= whole ? samples[newerIndex] : 0.0f;
    const float older = validSamples > whole ? samples[olderIndex] : 0.0f;
    return newer + fraction * (older - newer);
}

void TimeEffects::prepare(double sampleRate)
{
    prepared_ = false;
    parametersSet_ = false;
    sampleRate_ =
        std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0 ? sampleRate : 48000.0;
    smoothing_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.025 * sampleRate_)));
    for (auto &line : delays_)
        line.prepare(static_cast<std::size_t>(std::ceil(sampleRate_ * 2.05)) + 4);
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        predelays_[channel].prepare(static_cast<std::size_t>(std::ceil(sampleRate_ * 0.205)) + 4);
        for (std::size_t index = 0; index < combCount; ++index)
            combs_[channel][index].line.prepare(
                static_cast<std::size_t>(std::ceil(sampleRate_ * combTime(channel, index, 1.0f))) + 4);
        for (std::size_t index = 0; index < diffuserCount; ++index)
            diffusers_[channel][index].prepare(
                static_cast<std::size_t>(
                    std::ceil(sampleRate_ * (diffuserMilliseconds[index] + 0.37 * channel) * 0.001)) +
                4);
    }
    updateCoefficients();
    reset();
    prepared_ = true;
}

void TimeEffects::reset() noexcept
{
    current_ = targets_;
    phase_ = 0.0;
    delayFiltered_.fill(0.0f);
    toneCoefficient_ = targetToneCoefficient_;
    dampingCoefficient_ = targetDampingCoefficient_;
    phaserFeedback_.fill(0.0f);
    for (auto &channel : phaserMemory_)
        channel.fill(0.0f);
    for (auto &line : delays_)
        line.reset();
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        predelays_[channel].reset();
        for (auto &comb : combs_[channel])
        {
            comb.line.reset();
            comb.damped = 0.0f;
            comb.feedback = comb.targetFeedback;
        }
        for (auto &line : diffusers_[channel])
            line.reset();
    }
}

void TimeEffects::setParameters(EffectId effect, const Values &values) noexcept
{
    Values next{};
    std::size_t count = 0;
    switch (effect)
    {
    case EffectId::delay:
        next = {parameter(values[0], 1.0f, 2000.0f, 250.0f), parameter(values[1], 0.0f, 0.95f, 0.35f),
                parameter(values[2], 0.0f, 1.0f, 0.3f), parameter(values[3], 0.0f, 1.0f, 0.0f),
                parameter(values[4], 200.0f, 20000.0f, 8000.0f)};
        count = 5;
        break;
    case EffectId::chorus:
        next = {parameter(values[0], 0.05f, 8.0f, 0.8f), parameter(values[1], 0.0f, 12.0f, 4.0f),
                parameter(values[2], 0.0f, 1.0f, 0.35f), parameter(values[3], 0.0f, 1.0f, 1.0f)};
        count = 4;
        break;
    case EffectId::flanger:
        next = {parameter(values[0], 0.02f, 5.0f, 0.2f), parameter(values[1], 0.1f, 10.0f, 2.0f),
                parameter(values[2], -0.9f, 0.9f, 0.35f), parameter(values[3], 0.0f, 1.0f, 0.5f)};
        count = 4;
        break;
    case EffectId::phaser:
        next = {parameter(values[0], 0.02f, 8.0f, 0.4f), parameter(values[1], 0.0f, 1.0f, 0.7f),
                parameter(values[2], -0.9f, 0.9f, 0.2f), parameter(values[3], 0.0f, 1.0f, 0.5f)};
        count = 4;
        break;
    case EffectId::reverb:
        next = {parameter(values[0], 0.0f, 1.0f, 0.6f), parameter(values[1], 0.1f, 12.0f, 2.0f),
                parameter(values[2], 0.0f, 1.0f, 0.5f), parameter(values[3], 0.0f, 200.0f, 20.0f),
                parameter(values[4], 0.0f, 1.0f, 0.25f)};
        count = 5;
        break;
    case EffectId::tremolo:
        next = {parameter(values[0], 0.05f, 20.0f, 4.0f), parameter(values[1], 0.0f, 1.0f, 0.5f),
                parameter(values[2], 0.0f, 1.0f, 0.0f)};
        count = 3;
        break;
    case EffectId::ringMod:
        next = {parameter(values[0], 0.1f, 2000.0f, 30.0f), parameter(values[1], 0.0f, 1.0f, 0.5f)};
        count = 2;
        break;
    case EffectId::autoPan:
        next = {parameter(values[0], 0.02f, 10.0f, 0.5f), parameter(values[1], 0.0f, 1.0f, 1.0f)};
        count = 2;
        break;
    default:
        break;
    }
    const bool changedEffect = effect_ != effect;
    const bool firstParameters = !parametersSet_;
    parametersSet_ = true;
    if (!changedEffect && !firstParameters && targets_ == next)
        return;
    targets_ = next;
    parameterCount_ = count;
    effect_ = effect;
    updateCoefficients();
    // Ordinary edits retain all tails. Selecting another effect invalidates its
    // predecessor's history in constant time and starts at the requested values.
    if (changedEffect || firstParameters)
        reset();
}

void TimeEffects::updateCoefficients() noexcept
{
    if (effect_ == EffectId::delay)
    {
        const double cutoff = std::min<double>(targets_[4], sampleRate_ * 0.45);
        targetToneCoefficient_ = static_cast<float>(std::exp(-twoPi * cutoff / sampleRate_));
    }
    if (effect_ == EffectId::reverb)
    {
        const double brightness = 1.0 - targets_[2];
        const double cutoff = std::min(1200.0 + 19000.0 * brightness * brightness, sampleRate_ * 0.45);
        targetDampingCoefficient_ = static_cast<float>(std::exp(-twoPi * cutoff / sampleRate_));
        for (std::size_t channel = 0; channel < 2; ++channel)
            for (std::size_t index = 0; index < combCount; ++index)
                combs_[channel][index].targetFeedback = static_cast<float>(std::min(
                    0.995,
                    std::exp(-std::log(1000.0) * combTime(channel, index, targets_[0]) / targets_[1])));
    }
}

void TimeEffects::advancePhase(double frequency) noexcept
{
    phase_ += frequency / sampleRate_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;
}

float TimeEffects::processPhaser(float input, std::size_t channel, double wave) noexcept
{
    // Six first-order allpasses sweep three octaves either side of 700 Hz at
    // full depth. The right LFO leads by a quarter-cycle for stereo movement.
    const double frequency = std::min(700.0 * std::exp2(3.0 * current_[1] * wave), sampleRate_ * 0.4);
    const double tangent = std::tan(std::numbers::pi * frequency / sampleRate_);
    const float coefficient = static_cast<float>((1.0 - tangent) / (1.0 + tangent));
    float value = finiteAudio(input + current_[2] * phaserFeedback_[channel]);
    for (auto &memory : phaserMemory_[channel])
    {
        const float output = finiteAudio(memory - coefficient * value);
        memory = finiteAudio(value + coefficient * output);
        value = output;
    }
    phaserFeedback_[channel] = value;
    return value;
}

float TimeEffects::processReverb(float input, std::size_t channel) noexcept
{
    const double predelay = current_[3] * 0.001 * sampleRate_;
    const float delayedInput = predelay < 1.0 ? input : predelays_[channel].read(predelay);
    predelays_[channel].write(input);
    float sum = 0.0f;
    for (std::size_t index = 0; index < combCount; ++index)
    {
        auto &comb = combs_[channel][index];
        const float delayed = comb.line.read(combTime(channel, index, current_[0]) * sampleRate_);
        comb.feedback += smoothing_ * (comb.targetFeedback - comb.feedback);
        comb.damped = finiteAudio(delayed + dampingCoefficient_ * (comb.damped - delayed));
        comb.line.write(delayedInput * 0.25f + comb.feedback * comb.damped);
        sum += delayed * 0.125f;
    }
    // Two original delay allpasses diffuse eight parallel damped combs.
    for (std::size_t index = 0; index < diffuserCount; ++index)
    {
        auto &line = diffusers_[channel][index];
        const float delayed = line.read(sampleRate_ * (diffuserMilliseconds[index] + 0.37 * channel) * 0.001);
        const float output = finiteAudio(delayed - 0.5f * sum);
        line.write(sum + 0.5f * output);
        sum = output;
    }
    return finiteAudio(sum);
}

void TimeEffects::process(float &left, float &right) noexcept
{
    left = finiteAudio(left);
    right = finiteAudio(right);
    if (!prepared_)
        return;
    for (std::size_t index = 0; index < parameterCount_; ++index)
        current_[index] += smoothing_ * (targets_[index] - current_[index]);

    const float dryLeft = left;
    const float dryRight = right;
    switch (effect_)
    {
    case EffectId::delay:
    {
        toneCoefficient_ += smoothing_ * (targetToneCoefficient_ - toneCoefficient_);
        const double samples = current_[0] * 0.001 * sampleRate_;
        const std::array<float, 2> wet{delays_[0].read(samples), delays_[1].read(samples)};
        for (std::size_t channel = 0; channel < 2; ++channel)
            delayFiltered_[channel] =
                finiteAudio(wet[channel] + toneCoefficient_ * (delayFiltered_[channel] - wet[channel]));
        const float feedbackLeft = delayFiltered_[0] + current_[3] * (delayFiltered_[1] - delayFiltered_[0]);
        const float feedbackRight = delayFiltered_[1] + current_[3] * (delayFiltered_[0] - delayFiltered_[1]);
        delays_[0].write(dryLeft + current_[1] * feedbackLeft);
        delays_[1].write(dryRight + current_[1] * feedbackRight);
        left = blend(dryLeft, wet[0], current_[2]);
        right = blend(dryRight, wet[1], current_[2]);
        break;
    }
    case EffectId::chorus:
    {
        // A 3+depth ms centre delay, +/-depth ms sine excursion; width sets
        // the right-channel LFO offset from zero to one quarter-cycle.
        const double excursion = current_[1];
        const double leftTime = 3.0 + excursion * (1.0 + std::sin(twoPi * phase_));
        const double rightTime = 3.0 + excursion * (1.0 + std::sin(twoPi * (phase_ + 0.25 * current_[3])));
        left = blend(dryLeft, delays_[0].read(leftTime * 0.001 * sampleRate_), current_[2]);
        right = blend(dryRight, delays_[1].read(rightTime * 0.001 * sampleRate_), current_[2]);
        delays_[0].write(dryLeft);
        delays_[1].write(dryRight);
        advancePhase(current_[0]);
        break;
    }
    case EffectId::flanger:
    {
        // Both taps remain above 0.2 ms; stereo sine LFOs are a quarter-cycle
        // apart. Feedback is independent per channel and bounded below unity.
        const double excursion = current_[1];
        const float wetLeft =
            delays_[0].read((0.2 + excursion * (1.0 + std::sin(twoPi * phase_))) * 0.001 * sampleRate_);
        const float wetRight = delays_[1].read((0.2 + excursion * (1.0 + std::sin(twoPi * (phase_ + 0.25)))) *
                                               0.001 * sampleRate_);
        delays_[0].write(dryLeft + current_[2] * wetLeft);
        delays_[1].write(dryRight + current_[2] * wetRight);
        left = blend(dryLeft, wetLeft, current_[3]);
        right = blend(dryRight, wetRight, current_[3]);
        advancePhase(current_[0]);
        break;
    }
    case EffectId::phaser:
        left = blend(dryLeft, processPhaser(dryLeft, 0, std::sin(twoPi * phase_)), current_[3]);
        right = blend(dryRight, processPhaser(dryRight, 1, std::sin(twoPi * (phase_ + 0.25))), current_[3]);
        advancePhase(current_[0]);
        break;
    case EffectId::reverb:
        dampingCoefficient_ += smoothing_ * (targetDampingCoefficient_ - dampingCoefficient_);
        // A small, symmetric cross-send creates stereo reflections from a
        // one-sided source while retaining the source's wet image.
        left = blend(dryLeft, processReverb(0.8f * dryLeft + 0.2f * dryRight, 0), current_[4]);
        right = blend(dryRight, processReverb(0.8f * dryRight + 0.2f * dryLeft, 1), current_[4]);
        break;
    case EffectId::tremolo:
    {
        // Shape interpolates a sine with a rounded square; depth=0 is unity,
        // depth=1 modulates between silence and unity without gain overshoot.
        const float sine = static_cast<float>(std::sin(twoPi * phase_));
        const float square = std::tanh(8.0f * sine) / 0.9999997749296758f;
        const float wave = sine + current_[2] * (square - sine);
        const float gain = 1.0f - current_[1] * (0.5f - 0.5f * wave);
        left = finiteAudio(dryLeft * gain);
        right = finiteAudio(dryRight * gain);
        advancePhase(current_[0]);
        break;
    }
    case EffectId::ringMod:
    {
        const float carrier = static_cast<float>(std::sin(twoPi * phase_));
        left = blend(dryLeft, dryLeft * carrier, current_[1]);
        right = blend(dryRight, dryRight * carrier, current_[1]);
        advancePhase(current_[0]);
        break;
    }
    case EffectId::autoPan:
    {
        // Unity gain at centre; equal-power movement has a maximum +3 dB
        // single-channel gain and preserves stereo power for equal inputs.
        const float pan = current_[1] * static_cast<float>(std::sin(twoPi * phase_));
        left = finiteAudio(dryLeft * std::sqrt(std::max(0.0f, 1.0f - pan)));
        right = finiteAudio(dryRight * std::sqrt(std::max(0.0f, 1.0f + pan)));
        advancePhase(current_[0]);
        break;
    }
    default:
        break;
    }
}
} // namespace vibera::effects
