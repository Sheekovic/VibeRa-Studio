// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace vibera::tune
{
struct Pitch
{
    double hz = 0, confidence = 0;
};
// Original mathematical implementation of a normalized difference estimator.
// Experimental ordered-frame input; no streaming integration or Antares code.
class Detector
{
    std::array<double, 4097> difference{};
    double rate = 48000, low = 60, high = 1400;
    int minimum = 34, maximum = 800;

  public:
    bool prepare(double sampleRate, double minimumHz = 60, double maximumHz = 1400) noexcept
    {
        if (!std::isfinite(sampleRate) || !std::isfinite(minimumHz) || !std::isfinite(maximumHz) ||
            sampleRate < 8000 || sampleRate > 192000 || minimumHz < 50 || maximumHz <= minimumHz ||
            maximumHz >= sampleRate * .5 || std::ceil(sampleRate / minimumHz) > 4096)
            return false;
        rate = sampleRate;
        low = minimumHz;
        high = maximumHz;
        minimum = std::max(2, int(std::floor(rate / high)));
        maximum = int(std::ceil(rate / low));
        return true;
    }
    Pitch analyze(std::span<const float> frame, double threshold = .15) noexcept
    {
        if (frame.size() < size_t(maximum * 2) || !std::isfinite(threshold) || threshold <= 0 ||
            threshold >= 1)
            return {};
        double mean = 0, power = 0;
        for (float value : frame)
        {
            if (!std::isfinite(value))
                return {};
            mean += value;
            power += double(value) * value;
        }
        mean /= frame.size();
        if (power / frame.size() - mean * mean < 1e-10)
            return {};
        const size_t width = frame.size() - size_t(maximum);
        double accumulated = 0;
        difference[0] = 1;
        for (int lag = 1; lag <= maximum; ++lag)
        {
            double squared = 0;
            for (size_t i = 0; i < width; ++i)
            {
                const double distance = double(frame[i]) - frame[i + size_t(lag)];
                squared += distance * distance;
            }
            accumulated += squared;
            difference[size_t(lag)] = accumulated > 0 ? squared * lag / accumulated : 1;
        }
        for (int lag = minimum; lag <= maximum; ++lag)
        {
            if (difference[size_t(lag)] >= threshold)
                continue;
            while (lag < maximum && difference[size_t(lag + 1)] < difference[size_t(lag)])
                ++lag;
            double period = lag;
            if (lag < maximum)
            {
                const double left = difference[size_t(lag - 1)], centre = difference[size_t(lag)],
                             right = difference[size_t(lag + 1)];
                const double denominator = left - 2 * centre + right;
                if (denominator > 1e-12)
                    period += std::clamp(.5 * (left - right) / denominator, -.5, .5);
            }
            const double hz = rate / period;
            return hz >= low && hz <= high ? Pitch{hz, std::clamp(1 - difference[size_t(lag)], 0., 1.)}
                                           : Pitch{};
        }
        return {};
    }
};
struct Targets
{
    unsigned mask = 0xfff; // Pitch classes relative to root, chromatic by default.
    int root = 0;
    double referenceA = 440;
    bool valid() const noexcept
    {
        return mask != 0 && mask <= 0xfff && root >= 0 && root <= 11 && std::isfinite(referenceA) &&
               referenceA >= 400 && referenceA <= 480;
    }
    double nearest(double midi) const noexcept
    {
        if (!valid() || !std::isfinite(midi) || midi < -128 || midi > 256)
            return midi;
        double selected = midi, distance = 100;
        const int centre = int(std::floor(midi));
        for (int note = centre - 12; note <= centre + 12; ++note)
        {
            const int pitchClass = ((note - root) % 12 + 12) % 12;
            if ((mask & (1u << pitchClass)) && std::abs(note - midi) < distance)
            {
                distance = std::abs(note - midi);
                selected = note;
            }
        }
        return selected;
    }
};
} // namespace vibera::tune
