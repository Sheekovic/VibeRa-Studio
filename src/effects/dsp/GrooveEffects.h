// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsTypes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace vibera::effects
{
// Pump: a tempo-synced volume shape (the "sidechain pumping" sound without a sidechain).
// Each cycle drops to (1 - depth), holds, then recovers over the release fraction with an
// adjustable curve. Follows the host beat while playing; keeps its own clock when stopped.
class Pump
{
  public:
    static double cycleBeats(int choice) noexcept // 1 bar, 1/2 bar, 1/4, 1/8, 1/16
    {
        static constexpr double beats[]{4., 2., 1., .5, .25};
        return beats[std::clamp(choice, 0, 4)];
    }
    static const char *cycleName(int choice) noexcept
    {
        static const char *names[]{"1 bar", "1/2 bar", "1/4", "1/8", "1/16"};
        return names[std::clamp(choice, 0, 4)];
    }
    // Gain at a cycle phase 0..1 (also used by the editor curve).
    static float shape(double phase, float depth, float release, float curve, float hold) noexcept
    {
        phase -= std::floor(phase);
        const float floor = 1.f - depth;
        if (phase < hold)
            return floor;
        const double t = (phase - hold) / std::max(1e-4f, release);
        if (t >= 1)
            return 1.f;
        const double shaped = std::pow(t, std::exp2(-2. * curve));
        return floor + depth * float(shaped);
    }
    void prepare(double sampleRate) noexcept
    {
        rate = sampleRate;
        reset();
    }
    void reset() noexcept { gain = 1.f; }
    void setParameters(const Values &v) noexcept
    {
        cycle = cycleBeats(int(std::lround(v[0])));
        depth = std::clamp(v[1], 0.f, 1.f);
        release = std::clamp(v[2], .05f, 1.f);
        curve = std::clamp(v[3], -1.f, 1.f);
        hold = std::clamp(v[4], 0.f, .5f);
        offset = std::clamp(v[5], 0.f, 1.f);
        slew = 1.f - std::exp(-1.f / float(std::max(.05f, v[6]) * .001 * rate));
        mix = std::clamp(v[7], 0.f, 1.f);
    }
    void setTransport(bool hasPosition, bool playing, double ppq, double bpm) noexcept
    {
        if (std::isfinite(bpm) && bpm >= 10 && bpm <= 999)
            tempo = bpm;
        if (hasPosition && playing && std::isfinite(ppq))
            beat = ppq;
    }
    void process(float &left, float &right) noexcept
    {
        const float target = shape(beat / cycle - offset, depth, release, curve, hold);
        gain += (target - gain) * slew;
        const float g = 1.f + (gain - 1.f) * mix;
        left *= g;
        right *= g;
        beat += tempo / 60. / rate;
    }
    double beatPosition() const noexcept { return beat; }

  private:
    double rate = 48000, tempo = 120, beat = 0, cycle = 1;
    float depth = .8f, release = .6f, curve = .4f, hold = .05f, offset = 0, slew = 1, mix = 1, gain = 1;
};

// Lo-fi: tape wow (slow) and flutter (fast) pitch wobble through a modulated delay, hiss and
// vinyl crackle, a darker tone, gentle saturation ("age") and a narrower stereo image. The
// wobble needs a fixed base delay, reported to the host as latency (dry path included).
// Hiss and crackle follow the input level (1 s release), so a silent channel stays silent.
class LoFi
{
  public:
    static constexpr double wowHz = .55, flutterHz = 6.5;
    static constexpr double wowRatio = .0087, flutterRatio = .0017; // About 15 and 3 cents.
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        wowAmplitude = wowRatio * rate / (2 * std::numbers::pi * wowHz);
        flutterAmplitude = flutterRatio * rate / (2 * std::numbers::pi * flutterHz);
        base = int(std::ceil(wowAmplitude + flutterAmplitude)) + 4;
        releaseCoefficient = float(std::exp(-1. / rate)); // About 1 s.
        const auto size = std::size_t(2 * base + 8);
        delayLeft.assign(size, 0.f);
        delayRight.assign(size, 0.f);
        reset();
    }
    int latencySamples() const noexcept { return base; }
    void reset() noexcept
    {
        std::fill(delayLeft.begin(), delayLeft.end(), 0.f);
        std::fill(delayRight.begin(), delayRight.end(), 0.f);
        write = 0;
        wowPhase = flutterPhase = 0;
        toneLeft[0] = toneLeft[1] = toneRight[0] = toneRight[1] = 0;
        hiss = crackle = follower = 0;
        seed = 0x9e3779b9u;
    }
    void setParameters(const Values &v) noexcept
    {
        wow = std::clamp(v[0], 0.f, 1.f);
        flutter = std::clamp(v[1], 0.f, 1.f);
        noise = std::clamp(v[2], 0.f, 1.f);
        crackleAmount = std::clamp(v[3], 0.f, 1.f);
        const double tone = std::clamp(double(v[4]), 200., rate * .45);
        toneCoefficient = float(1 - std::exp(-2 * std::numbers::pi * tone / rate));
        age = std::clamp(v[5], 0.f, 1.f);
        width = std::clamp(v[6], 0.f, 1.f);
        mix = std::clamp(v[7], 0.f, 1.f);
    }
    void process(float &left, float &right) noexcept
    {
        const auto size = int(delayLeft.size());
        const float level = std::max(std::abs(left), std::abs(right));
        follower = level > follower ? level : follower * releaseCoefficient;
        delayLeft[std::size_t(write)] = left;
        delayRight[std::size_t(write)] = right;
        // Dry, aligned with the wet path's base delay.
        const int dryAt = (write - base + size) % size;
        const float dryLeft = delayLeft[std::size_t(dryAt)], dryRight = delayRight[std::size_t(dryAt)];
        const double offset = wow * wowAmplitude * std::sin(wowPhase) + flutter * flutterAmplitude * std::sin(flutterPhase);
        const double read = double(write) - base + offset + size;
        const int i0 = int(std::floor(read)) % size, i1 = (i0 + 1) % size;
        const float t = float(read - std::floor(read));
        float l = delayLeft[std::size_t(i0)] + (delayLeft[std::size_t(i1)] - delayLeft[std::size_t(i0)]) * t;
        float r = delayRight[std::size_t(i0)] + (delayRight[std::size_t(i1)] - delayRight[std::size_t(i0)]) * t;
        wowPhase += 2 * std::numbers::pi * wowHz / rate;
        flutterPhase += 2 * std::numbers::pi * flutterHz / rate;
        if (wowPhase > 2 * std::numbers::pi)
            wowPhase -= 2 * std::numbers::pi;
        if (flutterPhase > 2 * std::numbers::pi)
            flutterPhase -= 2 * std::numbers::pi;
        write = (write + 1) % size;
        // Age: normalised soft saturation.
        if (age > 0)
        {
            const float drive = 1.f + 3.f * age, norm = 1.f / std::tanh(drive);
            l = std::tanh(l * drive) * norm;
            r = std::tanh(r * drive) * norm;
        }
        // Tone: two cascaded one-pole low-passes.
        toneLeft[0] += (l - toneLeft[0]) * toneCoefficient;
        toneLeft[1] += (toneLeft[0] - toneLeft[1]) * toneCoefficient;
        toneRight[0] += (r - toneRight[0]) * toneCoefficient;
        toneRight[1] += (toneRight[0] - toneRight[1]) * toneCoefficient;
        l = toneLeft[1];
        r = toneRight[1];
        // Width: scale the side signal.
        const float mid = .5f * (l + r), side = .5f * (l - r) * width;
        l = mid + side;
        r = mid - side;
        // Hiss (filtered noise) and crackle (sparse decaying clicks), both mono like a record.
        hiss += (random() - hiss) * .35f;
        const float presence = std::min(1.f, follower * 8.f); // Full noise from about -18 dBFS.
        float added = hiss * noise * .02f * presence;
        if (crackleAmount > 0 && presence > 0 && (random() * .5f + .5f) < crackleAmount * .0006f)
            crackle = random() * .35f * presence;
        added += crackle;
        crackle *= .82f;
        l += added;
        r += added;
        left = dryLeft + (l - dryLeft) * mix;
        right = dryRight + (r - dryRight) * mix;
    }
    double wobblePhase() const noexcept { return wowPhase / (2 * std::numbers::pi); }

  private:
    float random() noexcept // xorshift32 in -1..1, no allocation or locks.
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return float(seed) / 2147483648.f - 1.f;
    }
    double rate = 48000, wowAmplitude = 0, flutterAmplitude = 0, wowPhase = 0, flutterPhase = 0;
    int base = 8, write = 0;
    std::vector<float> delayLeft, delayRight;
    float wow = .3f, flutter = .2f, noise = .15f, crackleAmount = .2f, toneCoefficient = 1, age = .3f, width = .8f,
          mix = 1;
    float toneLeft[2]{}, toneRight[2]{}, hiss = 0, crackle = 0, follower = 0, releaseCoefficient = .99998f;
    std::uint32_t seed = 0x9e3779b9u;
};
} // namespace vibera::effects
