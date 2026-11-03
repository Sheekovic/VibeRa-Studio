// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <limits>
namespace vibera
{
// UI-owned envelope pyramid. Build and query outside the real-time callback.
// 64-frame leaves keep storage small; partial leaves use the immutable PCM.
class SamplerWaveform
{
  public:
    struct Extent
    {
        float low = 0, high = 0;
    };

  private:
    std::vector<std::vector<Extent>> levels;
    static void merge(Extent &a, Extent b) noexcept
    {
        a.low = std::min(a.low, b.low);
        a.high = std::max(a.high, b.high);
    }
    static Extent empty() noexcept
    {
        return {std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    }
    static Extent scan(const juce::AudioBuffer<float> &pcm, int begin, int end) noexcept
    {
        auto value = empty();
        for (int c = 0; c < pcm.getNumChannels(); ++c)
            for (int i = begin; i < end; ++i)
                merge(value, {pcm.getSample(c, i), pcm.getSample(c, i)});
        return value;
    }

  public:
    void prepare(const juce::AudioBuffer<float> &pcm)
    {
        levels.clear();
        if (pcm.getNumSamples() == 0 || pcm.getNumChannels() == 0)
            return;
        const int count = (pcm.getNumSamples() + 63) / 64;
        levels.emplace_back(size_t(count));
        for (int i = 0; i < count; ++i)
            levels[0][size_t(i)] = scan(pcm, i * 64, std::min((i + 1) * 64, pcm.getNumSamples()));
        while (levels.back().size() > 1)
        {
            auto &previous = levels.back();
            std::vector<Extent> next((previous.size() + 1) / 2);
            for (size_t i = 0; i < next.size(); ++i)
            {
                next[i] = previous[i * 2];
                if (i * 2 + 1 < previous.size())
                    merge(next[i], previous[i * 2 + 1]);
            }
            levels.push_back(std::move(next));
        }
    }
    Extent query(const juce::AudioBuffer<float> &pcm, int begin, int end) const noexcept
    {
        begin = juce::jlimit(0, pcm.getNumSamples(), begin);
        end = juce::jlimit(begin, pcm.getNumSamples(), end);
        if (end == begin || pcm.getNumChannels() == 0)
            return {};
        if (levels.empty())
            return scan(pcm, begin, end);
        auto result = empty();
        const int aligned = std::min(end, ((begin + 63) / 64) * 64);
        if (aligned > begin)
            merge(result, scan(pcm, begin, aligned));
        int first = aligned / 64, last = end / 64;
        while (first < last)
        {
            size_t level = 0;
            int span = 1;
            while (level + 1 < levels.size() && first % (span * 2) == 0 && first + span * 2 <= last)
            {
                ++level;
                span *= 2;
            }
            merge(result, levels[level][size_t(first / span)]);
            first += span;
        }
        const int tail = std::max(aligned, last * 64);
        if (tail < end)
            merge(result, scan(pcm, tail, end));
        return result;
    }
};
} // namespace vibera
