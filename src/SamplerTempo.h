// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Sampler.h"
#include <atomic>
#include <vector>
namespace vibera
{
// Bounded offline onset-envelope autocorrelation; no calls from the audio thread.
// Returns 0 for insufficient rhythmic evidence. Half/double tempo remains ambiguous.
inline double estimateSampleTempo(const Sample &sample, const std::atomic<bool> *cancel = nullptr)
{
    if (sample.sampleRate <= 0 || sample.data.getNumChannels() == 0)
        return 0;
    const int hop = std::max(1, int(sample.sampleRate / 200));
    const int count = std::min(sample.data.getNumSamples() / hop, 24000);
    if (count < 800)
        return 0;
    std::vector<double> onset(size_t(count), 0.);
    double previous = 0, energy = 0;
    for (int i = 0; i < count; ++i)
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return 0;
        double value = 0;
        for (int n = i * hop; n < (i + 1) * hop; ++n)
            for (int c = 0; c < sample.data.getNumChannels(); ++c)
                value += std::abs(sample.data.getSample(c, n));
        onset[size_t(i)] = std::max(0., value - previous);
        energy += onset[size_t(i)] * onset[size_t(i)];
        previous = value;
    }
    if (energy < 1e-10)
        return 0;
    double best = 0;
    int bestLag = 0;
    const double rate = sample.sampleRate / hop;
    for (int lag = int(rate * 60 / 200); lag <= int(rate * 60 / 60); ++lag)
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return 0;
        double score = 0, left = 0, right = 0;
        for (int i = lag; i < count; ++i)
        {
            const auto a = onset[size_t(i)], b = onset[size_t(i - lag)];
            score += a * b;
            left += a * a;
            right += b * b;
        }
        score /= std::sqrt(left * right) + 1e-12;
        if (score > best + .001)
        {
            best = score;
            bestLag = lag;
        }
    }
    return best > .15 && bestLag > 0 ? rate * 60 / bestLag : 0;
}
} // namespace vibera
