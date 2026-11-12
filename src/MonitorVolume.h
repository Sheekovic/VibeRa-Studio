// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
namespace vibera
{
// Monitor volume: a listening-only gain on the live Master output. It is applied after the Master
// meter, the envelope follower source and the audio logger, and never while rendering, so exports,
// meters and logged audio are unchanged. The control thread sets the target; the audio thread ramps
// to it over one block (no allocation, no locks).
class MonitorVolume final
{
    std::atomic<float> target{1.f};
    float applied = 1.f; // Audio thread only.

  public:
    static MonitorVolume &instance() noexcept
    {
        static MonitorVolume monitor;
        return monitor;
    }
    void set(float gain) noexcept
    {
        target.store(juce::jlimit(0.f, 1.f, gain), std::memory_order_relaxed);
    }
    float get() const noexcept
    {
        return target.load(std::memory_order_relaxed);
    }
    void apply(juce::AudioBuffer<float> &buffer, int start, int count) noexcept
    {
        const float wanted = target.load(std::memory_order_relaxed);
        if (count <= 0 || (wanted == 1.f && applied == 1.f))
            return;
        buffer.applyGainRamp(start, count, applied, wanted);
        applied = wanted;
    }
};
} // namespace vibera
