// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <array>
#include <atomic>
namespace vibera
{
// Output oscilloscope: the newest slice of the live Master output, decimated to a small ring the
// interface reads. The audio thread only stores atomics (no allocation, no locks); a torn frame is
// a drawing glitch at worst and is redrawn on the next timer tick.
class OutputScope final
{
  public:
    static constexpr int size = 512, step = 8; // 512 points, every 8th frame: about 85 ms at 48 kHz.

  private:
    std::array<std::atomic<float>, size> ring{};
    std::atomic<unsigned> written{0};
    int phase = 0; // Audio thread only.

  public:
    static OutputScope &instance() noexcept
    {
        static OutputScope scope;
        return scope;
    }
    // AUDIO, live Master output.
    void push(const juce::AudioBuffer<float> &buffer, int start, int count) noexcept
    {
        const int channels = buffer.getNumChannels();
        if (channels <= 0 || count <= 0)
            return;
        const float *left = buffer.getReadPointer(0, start);
        const float *right = buffer.getReadPointer(channels > 1 ? 1 : 0, start);
        auto at = written.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
            if (++phase >= step)
            {
                phase = 0;
                ring[at++ % size].store((left[i] + right[i]) * .5f, std::memory_order_relaxed);
            }
        written.store(at, std::memory_order_release);
    }
    // INTERFACE: oldest to newest.
    void read(std::array<float, size> &out) const noexcept
    {
        const auto at = written.load(std::memory_order_acquire);
        for (int i = 0; i < size; ++i)
            out[size_t(i)] = ring[(at + unsigned(i)) % size].load(std::memory_order_relaxed);
    }
    float peak() const noexcept
    {
        float most = 0;
        for (const auto &value : ring)
            most = std::max(most, std::abs(value.load(std::memory_order_relaxed)));
        return most;
    }
    void clear() noexcept
    {
        for (auto &value : ring)
            value.store(0.f, std::memory_order_relaxed);
    }
};
// The small waveform beside the transport: the Master output as it plays.
class OutputScopeView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
    std::array<float, OutputScope::size> samples{};
    bool quiet = true;
    void timerCallback() override
    {
        OutputScope::instance().read(samples);
        bool silent = true;
        for (const float value : samples)
            silent = silent && std::abs(value) < 1.e-4f;
        if (!silent || !quiet)
            repaint();
        quiet = silent;
    }

  public:
    OutputScopeView()
    {
        setTitle("Output oscilloscope");
        setTooltip("Output oscilloscope: the Master output as it plays");
        setInterceptsMouseClicks(true, false);
        startTimerHz(30);
    }
    void paint(juce::Graphics &g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(.5f);
        g.setColour(panel);
        g.fillRoundedRectangle(area, 5.f);
        g.setColour(grid);
        g.drawRoundedRectangle(area, 5.f, 1.f);
        const float middle = area.getCentreY(), half = area.getHeight() * .5f - 3.f;
        g.drawHorizontalLine(juce::roundToInt(middle), area.getX() + 4.f, area.getRight() - 4.f);
        juce::Path wave;
        const float width = area.getWidth() - 8.f;
        for (int i = 0; i < OutputScope::size; ++i)
        {
            const float x = area.getX() + 4.f + width * float(i) / float(OutputScope::size - 1);
            const float y = middle - juce::jlimit(-1.f, 1.f, samples[size_t(i)]) * half;
            i == 0 ? wave.startNewSubPath(x, y) : wave.lineTo(x, y);
        }
        g.setColour(accent);
        g.strokePath(wave, juce::PathStrokeType(1.2f));
    }
};
} // namespace vibera
