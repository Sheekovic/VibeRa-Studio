// SPDX-License-Identifier: AGPL-3.0-or-later
// Independent analytical oracles; no native device or listening qualification.
#pragma once
#include "PreparedBalance.h"
#include <array>
#include <stdexcept>

namespace vibera::dsp::acceptance
{
inline void check(bool passed, const char *message)
{
    if (!passed)
        throw std::runtime_error(message);
}
inline void expectNear(float actual, double expected, double tolerance, const char *message)
{
    check(std::isfinite(actual) && std::abs(double(actual) - expected) <= tolerance, message);
}

// Independent musical oracle: input linear gain has the old channel fader's
// -100 dB silence boundary and +6 dB ceiling. No production conversion helper.
inline double channelGain(double gain)
{
    return gain <= 1e-5 ? 0. : std::min(gain, std::pow(10., 6. / 20.));
}
inline std::array<double, 3> liexpectNear(double gain, double pan)
{
    return {gain * (1 - pan), gain * (1 + pan), gain};
}

inline void preparedBalanceAcceptance()
{
    using Balance = PreparedBalance;
    using Mode = Balance::Mode;
    using Result = Balance::Result;
    constexpr double fixedTolerance = 2e-6;
    // JUCE's retained float step accumulates roundoff during the ramp. This is
    // an explicit numerical tolerance, not a timing or audio-silence exclusion.
    constexpr double rampTolerance = 5e-5;
    Balance::Coefficients c;
    for (const auto rate : {48000., 44100.})
    {
        const int length = int(std::floor(rate * .015)); // 720 and 661, independently derived.
        check(length == (rate == 48000. ? 720 : 661), "Independent 15ms frame count");
        for (const auto gain : {0.f, 1e-6f, .25f, 1.f, 2.f})
            for (const auto pan : {-1.f, -.25f, -.005f, 0.f, .005f, .25f, 1.f})
            {
                Balance b;
                check(b.prepare(rate, Mode::channelGainAndPan, tracktion::PanLawLinear, gain, pan) ==
                          Result::ok,
                      "Prepare valid channel balance");
                check(b.next(4, c) == Result::ok, "Prepared coefficient read");
                const auto expected = liexpectNear(channelGain(gain), std::abs(pan) <= .005f ? 0. : pan);
                expectNear(c.left, expected[0], fixedTolerance, "Initial left coefficient / floor / ceiling");
                expectNear(c.right, expected[1], fixedTolerance, "Initial right coefficient / floor / ceiling");
                expectNear(c.extra, expected[2], fixedTolerance, "Initial extra-channel coefficient");
            }

        // Pan-only ramp must retain gain stage independence and hard-pan x2.
        Balance b;
        check(b.prepare(rate, Mode::panOnly, tracktion::PanLawLinear, 1, 0) == Result::ok,
              "Prepare pan-only");
        for (int frame = 1; frame <= length + 9; ++frame)
        {
            if (frame == 1 || frame % 47 == 0)
                check(b.setManualTarget(1, -1, 2) == Result::ok, "Set/repeat identical pan target");
            b.next(2, c);
            const double position = std::min(1., double(frame) / length);
            expectNear(c.left, 1 + position, rampTolerance, "Every left ramp frame");
            expectNear(c.right, 1 - position, rampTolerance, "Every right ramp frame");
        }
        expectNear(c.left, 2, fixedTolerance, "Hard-left exact target after endpoint");
        expectNear(c.right, 0, fixedTolerance, "Hard-left right silence endpoint");

        // Channel gain+pan ramps interpolate endpoint COEFFICIENTS jointly.
        // Independent gain/pan ramps would produce .625/.625 at halfway and fail.
        Balance joint;
        joint.prepare(rate, Mode::channelGainAndPan, tracktion::PanLawLinear, .25f, -.5f);
        joint.setManualTarget(1, .5f, 2);
        for (int frame = 1; frame <= length; ++frame)
        {
            joint.next(2, c);
            const double t = double(frame) / length;
            expectNear(c.left, .375 + (.5 - .375) * t, rampTolerance, "Joint left gain-pan interpolation");
            expectNear(c.right, .125 + (1.5 - .125) * t, rampTolerance, "Joint right gain-pan interpolation");
        }

        // Retarget/cancel returns toward a new target from the ACTUAL current
        // coefficient and spends the full original ramp duration, without jumps.
        Balance retarget;
        retarget.prepare(rate, Mode::panOnly, tracktion::PanLawLinear, 1, 0);
        retarget.setManualTarget(1, -1, 2);
        for (int frame = 0; frame < length / 3; ++frame)
            retarget.next(2, c);
        const auto previous = c;
        retarget.setManualTarget(1, 0, 2);
        for (int frame = 1; frame <= length; ++frame)
        {
            retarget.next(2, c);
            expectNear(c.left, previous.left + (1 - previous.left) * double(frame) / length, rampTolerance,
                 "Cancel/retarget left starts at current value");
            expectNear(c.right, previous.right + (1 - previous.right) * double(frame) / length, rampTolerance,
                 "Cancel/retarget right starts at current value");
        }

        // Native mono advances L only; its absent R ramp starts on later stereo.
        Balance mono;
        mono.prepare(rate, Mode::panOnly, tracktion::PanLawLinear, 1, 0);
        mono.setManualTarget(1, -1, 1);
        for (int frame = 0; frame < length; ++frame)
            mono.next(1, c);
        expectNear(c.left, 2, fixedTolerance, "Mono uses left coefficient");
        expectNear(c.right, 1, fixedTolerance, "Absent mono right ramp does not advance");
        mono.setManualTarget(1, -1, 2);
        mono.next(2, c);
        expectNear(c.right, 1 - 1. / length, fixedTolerance, "New stereo right begins its retained ramp");

        // Extra channels receive gain only and share one frame's coefficient.
        Balance multi;
        multi.prepare(rate, Mode::channelGainAndPan, tracktion::PanLawLinear, .25f, 0);
        multi.setManualTarget(1, 1, 2);
        for (int frame = 0; frame < length; ++frame)
            multi.next(2, c);
        expectNear(c.extra, .25, fixedTolerance, "Absent extra channels do not retarget gain");
        juce::AudioBuffer<float> surround(4, length + 2);
        for (int channel = 0; channel < 4; ++channel)
            for (int frame = 0; frame < surround.getNumSamples(); ++frame)
                surround.setSample(channel, frame, float(channel + 1));
        multi.process(surround, 1, length, 1, 1);
        expectNear(surround.getSample(2, 1), 3 * (.25 + .75 / length), fixedTolerance,
             "First extra channel begins gain ramp");
        expectNear(surround.getSample(3, 1), 4 * (.25 + .75 / length), fixedTolerance,
             "Second extra channel uses SAME frame gain");
        expectNear(surround.getSample(2, length), 3, fixedTolerance, "Extra gain endpoint");
        expectNear(surround.getSample(0, 0), 1, 0, "Processing preserves prefix");
        expectNear(surround.getSample(0, length + 1), 1, 0, "Processing preserves suffix");

        // Irregular partitions must give the same independent per-sample ramp.
        Balance partitioned;
        partitioned.prepare(rate, Mode::panOnly, tracktion::PanLawLinear, 1, 0);
        juce::AudioBuffer<float> pcm(2, length + 37);
        for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
        {
            pcm.setSample(0, frame, 1);
            pcm.setSample(1, frame, -.25f);
        }
        constexpr std::array<int, 5> partitions{1, 37, 128, 9, 256};
        int done = 0, part = 0;
        while (done < pcm.getNumSamples())
        {
            const int count =
                std::min(partitions[size_t(part++ % partitions.size())], pcm.getNumSamples() - done);
            check(partitioned.process(pcm, done, count, 1, .5f) == Result::ok, "Partition processing");
            done += count;
        }
        for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
        {
            const double t = std::min(1., double(frame + 1) / length);
            expectNear(pcm.getSample(0, frame), 1 - .5 * t, rampTolerance, "Partition left PCM oracle");
            expectNear(pcm.getSample(1, frame), -.25 * (1 + .5 * t), rampTolerance, "Partition right PCM oracle");
        }
    }

    // Nondefault laws remain explicit and use independent trigonometric oracles.
    for (const auto law : {tracktion::PanLaw2point5dBCenter, tracktion::PanLaw3dBCenter,
                           tracktion::PanLaw4point5dBCenter, tracktion::PanLaw6dBCenter})
        for (const auto pan : {-1.f, -.5f, 0.f, .5f, 1.f})
        {
            Balance b;
            check(b.prepare(48000, Mode::panOnly, law, 1, pan) == Result::ok, "Explicit resolved law");
            b.next(2, c);
            const double exponent = law == tracktion::PanLaw2point5dBCenter   ? 2.5 / 3.
                                    : law == tracktion::PanLaw4point5dBCenter ? 1.5
                                    : law == tracktion::PanLaw6dBCenter       ? 2.
                                                                              : 1.;
            const double halfPi = std::acos(-1.) / 2;
            const double p = (pan + 1.) / 2;
            expectNear(c.left, std::pow(std::sin((1 - p) * halfPi), exponent), fixedTolerance,
                 "Independent resolved-law left oracle");
            expectNear(c.right, std::pow(std::sin(p * halfPi), exponent), fixedTolerance,
                 "Independent resolved-law right oracle");
        }

    // Invalid controls and ranges are refused atomically, without changing PCM or ramp state.
    Balance b, reference;
    check(b.next(2, c) == Result::notPrepared, "Unprepared refusal");
    b.prepare(48000, Mode::panOnly, tracktion::PanLawLinear, 1, 0);
    reference.prepare(48000, Mode::panOnly, tracktion::PanLawLinear, 1, 0);
    b.setManualTarget(1, -1, 2);
    reference.setManualTarget(1, -1, 2);
    check(b.prepare(0, Mode::panOnly, tracktion::PanLawLinear, 1, 0) == Result::invalidRate,
          "Invalid preparation retains prior valid state");
    check(b.prepare(48000, Mode::panOnly, tracktion::PanLawDefault, 1, 0) == Result::invalidLaw,
          "Unresolved global law refused");
    check(b.setManualTarget(.5f, 0, 2) == Result::invalidTarget, "Pan-only cannot duplicate gain stage");
    check(b.setManualTarget(1, std::numeric_limits<float>::quiet_NaN(), 2) == Result::invalidTarget,
          "Nonfinite target refused");
    check(b.setManualTarget(1, 2, 2) == Result::invalidTarget, "Pan range refused");
    juce::AudioBuffer<float> invalidPcm(2, 8);
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < 8; ++frame)
            invalidPcm.setSample(channel, frame, .25f);
    check(b.process(invalidPcm, std::numeric_limits<int>::max(), 1, 1, 0) == Result::invalidShape,
          "Overflowing offset refused without addition");
    check(b.process(invalidPcm, 0, 9, 1, 0) == Result::invalidShape, "Oversized span refused");
    check(b.process(invalidPcm, 0, 0, 1, 0) == Result::ok, "Zero-frame no-op");
    Balance::Coefficients expected;
    b.next(2, c);
    reference.next(2, expected);
    expectNear(c.left, expected.left, 0, "Invalid calls/zero span preserve ramp left exactly");
    expectNear(c.right, expected.right, 0, "Invalid calls/zero span preserve ramp right exactly");
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < 8; ++frame)
            expectNear(invalidPcm.getSample(channel, frame), .25, 0, "Invalid calls preserve PCM exactly");

    // Automation is sample-exact, including tiny centre values, and cannot mutate manual smoothing.
    b.automated(1, .003f, c);
    expectNear(c.left, .997, fixedTolerance, "Automation is not manually centre-snapped");
    expectNear(c.right, 1.003, fixedTolerance, "Automation continuous right value");
    b.next(2, c);
    reference.next(2, expected);
    expectNear(c.left, expected.left, 0, "Automation read does not disturb manual ramp");
}
} // namespace vibera::dsp::acceptance
