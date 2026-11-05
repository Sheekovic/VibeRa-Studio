// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsTypes.h"
#include <algorithm>
#include <array>
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

// Formant filter: the input through three resonant band-passes placed on the formants of a
// sung vowel (A E I O U, morphing continuously), optionally shifted, like a "talking" filter.
// Coefficients change only in setParameters; processing is allocation-free.
class Formant
{
  public:
    // Formant frequencies (Hz) and relative levels of the five vowels (adult voice, rounded
    // textbook values), interpolated for a fractional vowel and shifted by `shift` semitones.
    static void formants(float vowel, float shift, double (&hz)[3], double (&level)[3]) noexcept
    {
        static constexpr double table[5][3]{{800, 1150, 2900}, {400, 1700, 2600}, {300, 2300, 3000},
                                            {450, 800, 2830}, {325, 700, 2530}};
        static constexpr double levels[5][3]{{1, .5, .12}, {1, .35, .18}, {1, .2, .16}, {1, .3, .05}, {1, .16, .04}};
        const float v = std::clamp(vowel, 0.f, 4.f);
        const int a = std::min(3, int(v));
        const double t = v - a, ratio = std::pow(2., double(shift) / 12.);
        for (int i = 0; i < 3; ++i)
        {
            hz[i] = (table[a][i] + (table[a + 1][i] - table[a][i]) * t) * ratio;
            level[i] = levels[a][i] + (levels[a + 1][i] - levels[a][i]) * t;
        }
    }
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        reset();
    }
    void reset() noexcept
    {
        for (auto &band : state)
            for (auto &channel : band)
                channel = {0, 0, 0, 0};
    }
    void setParameters(const Values &v) noexcept
    {
        double hz[3], level[3];
        formants(v[0], v[1], hz, level);
        const double q = std::clamp(double(v[2]), 1., 20.);
        for (int i = 0; i < 3; ++i)
        {
            // RBJ band-pass (0 dB peak), scaled by the formant level.
            const double w = 2 * std::numbers::pi * std::clamp(hz[i], 40., rate * .45) / rate;
            const double alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
            gains[i] = float(level[i]);
            b0[i] = float(alpha / a0);
            a1[i] = float(-2 * std::cos(w) / a0);
            a2[i] = float((1 - alpha) / a0);
        }
        mix = std::clamp(v[3], 0.f, 1.f);
        output = std::pow(10.f, std::clamp(v[4], -12.f, 18.f) / 20.f);
    }
    void process(float &left, float &right) noexcept
    {
        float wet[2]{0, 0};
        const float dry[2]{left, right};
        for (int i = 0; i < 3; ++i)
            for (int c = 0; c < 2; ++c)
            {
                // Direct form I: y = b0 x - b0 x[n-2] - a1 y[n-1] - a2 y[n-2].
                auto &s = state[size_t(i)][size_t(c)];
                const float y = b0[i] * (dry[c] - s[1]) - a1[i] * s[2] - a2[i] * s[3];
                s[1] = s[0];
                s[0] = dry[c];
                s[3] = s[2];
                s[2] = std::isfinite(y) ? y : 0.f;
                wet[c] += s[2] * gains[i];
            }
        left = (dry[0] + (wet[0] * output - dry[0]) * mix);
        right = (dry[1] + (wet[1] * output - dry[1]) * mix);
    }

  private:
    double rate = 48000;
    float b0[3]{}, a1[3]{}, a2[3]{}, gains[3]{};
    float mix = 1, output = 1;
    std::array<std::array<std::array<float, 4>, 2>, 3> state{};
};

// Waveshaper: drive into a transfer curve that morphs between five shapes (soft tanh, hard
// clip, sine fold, asymmetric tube, full-wave rectify), with a bias for asymmetry. A 10 Hz
// DC blocker removes the offset the bias and rectification add. Each sample averages the
// curve at the sample and at the midpoint to the previous one: a light alias reduction,
// not true oversampling.
class Waveshaper
{
  public:
    static float curve(float shape, float x) noexcept
    {
        const auto one = [](int k, float v) noexcept
        {
            switch (k)
            {
            case 0: return std::tanh(v);
            case 1: return std::clamp(v, -1.f, 1.f);
            case 2: return std::sin(v * 1.5707963f);
            case 3: return v >= 0 ? std::tanh(v) : std::tanh(1.8f * v) / 1.8f + .2f * std::tanh(v);
            default: return std::min(1.f, std::abs(v)) * 2.f - 1.f;
            }
        };
        const float s = std::clamp(shape, 0.f, 4.f);
        const int a = std::min(3, int(s));
        const float t = s - float(a);
        return one(a, x) + (one(a + 1, x) - one(a, x)) * t;
    }
    void prepare(double sampleRate)
    {
        dcCoefficient = float(std::exp(-2 * std::numbers::pi * 10. / sampleRate));
        reset();
    }
    void reset() noexcept
    {
        previous[0] = previous[1] = 0;
        dcIn[0] = dcIn[1] = dcOut[0] = dcOut[1] = 0;
    }
    void setParameters(const Values &v) noexcept
    {
        shape = std::clamp(v[0], 0.f, 4.f);
        drive = std::pow(10.f, std::clamp(v[1], 0.f, 36.f) / 20.f);
        bias = std::clamp(v[2], -1.f, 1.f);
        output = std::pow(10.f, std::clamp(v[3], -36.f, 12.f) / 20.f);
        mix = std::clamp(v[4], 0.f, 1.f);
        rest = curve(shape, bias); // Output at silence: removed so silence stays silent.
    }
    void process(float &left, float &right) noexcept
    {
        float *io[2]{&left, &right};
        for (int c = 0; c < 2; ++c)
        {
            const float dry = *io[c];
            // 2x: the midpoint with the previous sample, then the sample itself.
            const float mid = .5f * (previous[c] + dry);
            previous[c] = dry;
            const float shaped = .5f * (curve(shape, mid * drive + bias) + curve(shape, dry * drive + bias)) - rest;
            // DC blocker: y = x - x[n-1] + R y[n-1].
            const float y = shaped - dcIn[c] + dcCoefficient * dcOut[c];
            dcIn[c] = shaped;
            dcOut[c] = std::isfinite(y) ? y : 0.f;
            *io[c] = dry + (dcOut[c] * output - dry) * mix;
        }
    }

  private:
    float shape = 0, drive = 4, bias = 0, output = .5f, mix = 1, dcCoefficient = .999f, rest = 0;
    float previous[2]{}, dcIn[2]{}, dcOut[2]{};
};

// Frequency shifter: every frequency moves by the same number of Hz (single sideband), so
// harmonic sounds turn metallic or detuned. An analytic signal from two all-pass chains
// (90-degree pair over about 20 Hz..20 kHz at 44.1-48 kHz) is mixed with a quadrature
// oscillator. Optional feedback re-shifts the output; the right channel can be offset.
class FrequencyShifter
{
  public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        reset();
    }
    void reset() noexcept
    {
        for (auto &channel : channels)
            channel = {};
        phase[0] = phase[1] = 0;
    }
    void setParameters(const Values &v) noexcept
    {
        shift[0] = std::clamp(double(v[0]), -2000., 2000.);
        shift[1] = std::clamp(shift[0] + double(v[1]), -2200., 2200.);
        feedback = std::clamp(v[2], 0.f, .9f);
        mix = std::clamp(v[3], 0.f, 1.f);
    }
    void process(float &left, float &right) noexcept
    {
        float *io[2]{&left, &right};
        for (int c = 0; c < 2; ++c)
        {
            auto &ch = channels[size_t(c)];
            const float dry = *io[c];
            const float in = dry + feedback * ch.last;
            // Two all-pass chains whose outputs stay 90 degrees apart (A delayed one sample).
            float a = in, b = in;
            for (int i = 0; i < 4; ++i)
            {
                a = allpass(ch.a[size_t(i)], coefficientsA[i] * coefficientsA[i], a);
                b = allpass(ch.b[size_t(i)], coefficientsB[i] * coefficientsB[i], b);
            }
            // Chain A one sample late is the real part, chain B the imaginary part.
            const float real = ch.delayedA;
            ch.delayedA = a;
            const float out = float(real * std::cos(phase[c]) + b * std::sin(phase[c])); // Upper sideband.
            ch.last = std::isfinite(out) ? out : 0.f;
            phase[c] += 2 * std::numbers::pi * shift[c] / rate;
            if (phase[c] > std::numbers::pi)
                phase[c] -= 2 * std::numbers::pi;
            else if (phase[c] < -std::numbers::pi)
                phase[c] += 2 * std::numbers::pi;
            *io[c] = dry + (ch.last - dry) * mix;
        }
    }

  private:
    struct Section
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };
    struct Channel
    {
        std::array<Section, 4> a{}, b{};
        float delayedA = 0, last = 0;
    };
    // y[n] = c (x[n] + y[n-2]) - x[n-2]: a second-order all-pass in z^2.
    static float allpass(Section &s, float c, float x) noexcept
    {
        const float y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1;
        s.x1 = x;
        s.y2 = s.y1;
        s.y1 = std::isfinite(y) ? y : 0.f;
        return s.y1;
    }
    // Published wideband 90-degree all-pass pair coefficients (used squared).
    static constexpr float coefficientsA[4]{.6923878f, .9360654322959f, .9882295226860f, .9987488452737f};
    static constexpr float coefficientsB[4]{.4021921162426f, .8561710882420f, .9722909545651f, .9952884791278f};
    double rate = 48000, shift[2]{100, 100}, phase[2]{0, 0};
    float feedback = 0, mix = 1;
    std::array<Channel, 2> channels{};
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
