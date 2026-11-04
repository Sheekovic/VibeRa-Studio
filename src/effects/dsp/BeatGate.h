// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vibera::effects
{
// Beat Gate: tempo-synced rhythmic gate, stutter and half-time (original design).
// The beat position comes from the host play head when the transport plays; when it is
// stopped (or no host time exists) the effect keeps its own clock at the host/default tempo.
// Signal flow: input -> half-time -> stutter -> gate -> dry/wet mix. Buffers are allocated in
// prepare(); process() never allocates or locks.
class BeatGate
{
    using i64 = long long;

  public:
    static constexpr int patternCount = 8;
    static const char *patternName(int index) noexcept
    {
        static const char *names[patternCount]{"Open", "Pulse", "Offbeat", "Dotted",
                                               "Chop",  "Broken", "Half bar", "Build"};
        return names[std::clamp(index, 0, patternCount - 1)];
    }
    // 16 steps, 'x' open and '.' closed.
    static bool stepOpen(int pattern, int step) noexcept
    {
        static const char *steps[patternCount]{"xxxxxxxxxxxxxxxx", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
                                               "x..x..x.x..x..x.", "xxx.xxx.xxx.xxx.", "x.xxx.xxx.x.xx.x",
                                               "xxxxxxxx........", "x...x...x.x.xxxx"};
        return steps[std::clamp(pattern, 0, patternCount - 1)][step & 15] == 'x';
    }
    static double stepBeats(int choice) noexcept // 1/32, 1/16, 1/8, 1/4 note
    {
        static constexpr double beats[]{.125, .25, .5, 1.};
        return beats[std::clamp(choice, 0, 3)];
    }
    static double stutterBeats(int choice) noexcept // Off, 1/4, 1/8, 1/16, 1/32 note
    {
        static constexpr double beats[]{0., 1., .5, .25, .125};
        return beats[std::clamp(choice, 0, 4)];
    }
    static double holdBeats(int choice) noexcept // Stutter segment: 1/2, 1, 2 beats
    {
        static constexpr double beats[]{.5, 1., 2.};
        return beats[std::clamp(choice, 0, 2)];
    }
    static double cycleBeats(int choice) noexcept // Half-time cycle: 1, 2, 4 beats
    {
        static constexpr double beats[]{1., 2., 4.};
        return beats[std::clamp(choice, 0, 2)];
    }

    void prepare(double sampleRate)
    {
        rate = sampleRate;
        // Longest needs: a 4-beat half-time cycle and a 1-beat stutter slice at 20 BPM, capped.
        const auto halfFrames = std::size_t(std::min(12., 4. * 60. / 20.) * sampleRate) + 4;
        const auto stutterFrames = std::size_t(3. * sampleRate) + 4;
        for (auto *buffer : {&halfLeft, &halfRight})
            buffer->assign(halfFrames, 0.f);
        for (auto *buffer : {&stutterLeft, &stutterRight})
            buffer->assign(stutterFrames, 0.f);
        fade = std::max(1, int(.003 * sampleRate)); // 3 ms edges against clicks.
        reset();
    }
    void reset() noexcept
    {
        gain = 1.f;
        halfIndex = stutterIndex = -1;
        halfCounter = stutterCounter = 0;
        std::fill(halfLeft.begin(), halfLeft.end(), 0.f);
        std::fill(halfRight.begin(), halfRight.end(), 0.f);
        std::fill(stutterLeft.begin(), stutterLeft.end(), 0.f);
        std::fill(stutterRight.begin(), stutterRight.end(), 0.f);
    }
    void setParameters(const Values &v) noexcept
    {
        pattern = int(std::lround(v[0]));
        step = stepBeats(int(std::lround(v[1])));
        depth = std::clamp(v[2], 0.f, 1.f);
        const float smoothMs = std::max(.05f, v[3]);
        slew = 1.f - std::exp(-1.f / float(smoothMs * .001 * rate));
        stutter = stutterBeats(int(std::lround(v[4])));
        hold = holdBeats(int(std::lround(v[5])));
        halfTime = v[6] >= .5f;
        cycle = cycleBeats(int(std::lround(v[7])));
        mix = std::clamp(v[8], 0.f, 1.f);
    }
    // Once per block from the host play head. A playing transport re-anchors the beat
    // position (loops and seeks follow it); otherwise the internal clock keeps running.
    void setTransport(bool hasPosition, bool playing, double ppq, double bpm) noexcept
    {
        if (std::isfinite(bpm) && bpm >= 10 && bpm <= 999)
            tempo = bpm;
        if (hasPosition && playing && std::isfinite(ppq))
            beat = ppq;
    }
    void process(float &left, float &right) noexcept
    {
        const float dryLeft = left, dryRight = right;
        const double samplesPerBeat = rate * 60. / tempo;
        // Half-time: within each cycle, read the cycle's input at half speed (one octave down).
        if (halfTime)
        {
            const auto index = i64(std::floor(beat / cycle));
            if (index != halfIndex)
            {
                halfIndex = index;
                halfCounter = 0;
            }
            const auto capacity = i64(halfLeft.size()) - 2;
            if (halfCounter < capacity)
            {
                halfLeft[std::size_t(halfCounter)] = left;
                halfRight[std::size_t(halfCounter)] = right;
            }
            const double read = std::min(double(capacity), halfCounter * .5);
            const auto i0 = std::size_t(read);
            const auto t = float(read - double(i0));
            left = halfLeft[i0] + (halfLeft[i0 + 1] - halfLeft[i0]) * t;
            right = halfRight[i0] + (halfRight[i0 + 1] - halfRight[i0]) * t;
            const auto length = i64(cycle * samplesPerBeat);
            const float edge = edgeGain(halfCounter, length);
            left *= edge;
            right *= edge;
            ++halfCounter;
        }
        // Stutter: each segment plays its first slice live, then repeats that slice.
        if (stutter > 0)
        {
            const auto index = i64(std::floor(beat / hold));
            if (index != stutterIndex)
            {
                stutterIndex = index;
                stutterCounter = 0;
            }
            const auto slice = std::clamp(i64(stutter * samplesPerBeat), 1LL,
                                          i64(stutterLeft.size()) - 1);
            const auto k = stutterCounter % slice;
            if (stutterCounter < slice)
            {
                stutterLeft[std::size_t(k)] = left;
                stutterRight[std::size_t(k)] = right;
            }
            else
            {
                left = stutterLeft[std::size_t(k)];
                right = stutterRight[std::size_t(k)];
            }
            const float edge = stutterCounter < slice ? 1.f : edgeGain(k, slice);
            left *= edge;
            right *= edge;
            ++stutterCounter;
        }
        // Gate: the open/closed step at this beat, slewed against clicks.
        const auto stepIndex = int(i64(std::floor(beat / step)) & 15);
        const float target = stepOpen(pattern, stepIndex) ? 1.f : 1.f - depth;
        gain += (target - gain) * slew;
        left *= gain;
        right *= gain;
        left = dryLeft + (left - dryLeft) * mix;
        right = dryRight + (right - dryRight) * mix;
        beat += 1. / samplesPerBeat;
    }
    double beatPosition() const noexcept
    {
        return beat;
    }

  private:
    float edgeGain(long long position, long long length) const noexcept
    {
        if (length <= 2 * fade)
            return 1.f;
        const auto distance = std::min(position, length - 1 - position);
        return distance >= fade ? 1.f : float(std::max(0LL, distance)) / float(fade);
    }
    double rate = 48000, tempo = 120, beat = 0;
    int pattern = 1, fade = 144;
    double step = .25, stutter = 0, hold = 1, cycle = 2;
    bool halfTime = false;
    float depth = 1, slew = 1, mix = 1, gain = 1;
    long long halfIndex = -1, stutterIndex = -1, halfCounter = 0, stutterCounter = 0;
    std::vector<float> halfLeft, halfRight, stutterLeft, stutterRight;
};
} // namespace vibera::effects
