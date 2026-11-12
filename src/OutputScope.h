// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
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
    static constexpr int rawSize = 1024, bars = 48; // Spectrum: the newest 1024 frames, 48 log-spaced bars.

  private:
    std::array<std::atomic<float>, size> ring{};
    std::atomic<unsigned> written{0};
    std::array<std::atomic<float>, rawSize> raw{};
    std::atomic<unsigned> rawWritten{0};
    std::atomic<bool> spectrumMode{false};
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
        auto rawAt = rawWritten.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
            raw[rawAt++ % rawSize].store((left[i] + right[i]) * .5f, std::memory_order_relaxed);
        rawWritten.store(rawAt, std::memory_order_release);
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
    void readRaw(std::array<float, rawSize> &out) const noexcept
    {
        const auto at = rawWritten.load(std::memory_order_acquire);
        for (int i = 0; i < rawSize; ++i)
            out[size_t(i)] = raw[(at + unsigned(i)) % rawSize].load(std::memory_order_relaxed);
    }
    // INTERFACE: Hann-windowed magnitude spectrum as 0..1 bars (-72..0 dB), log-spaced from the first bin up.
    static void spectrum(const std::array<float, rawSize> &frames, std::array<float, bars> &out)
    {
        static juce::dsp::FFT fft(10);
        std::array<float, rawSize * 2> work{};
        for (int i = 0; i < rawSize; ++i)
            work[size_t(i)] = frames[size_t(i)] *
                              (.5f - .5f * std::cos(juce::MathConstants<float>::twoPi * float(i) / float(rawSize - 1)));
        fft.performFrequencyOnlyForwardTransform(work.data());
        const double top = rawSize / 2 - 1;
        for (int bar = 0; bar < bars; ++bar)
        {
            const int from = juce::jlimit(1, int(top), int(std::floor(std::pow(top, double(bar) / bars))));
            const int to = juce::jlimit(from, int(top), int(std::floor(std::pow(top, double(bar + 1) / bars))));
            float most = 0;
            for (int bin = from; bin <= to; ++bin)
                most = std::max(most, work[size_t(bin)]);
            const float level = most / float(rawSize / 4); // A full-scale sine reads about 0 dB under a Hann window.
            out[size_t(bar)] = juce::jlimit(0.f, 1.f, 1.f + juce::Decibels::gainToDecibels(level, -72.f) / 72.f);
        }
    }
    bool spectrumShown() const noexcept
    {
        return spectrumMode.load(std::memory_order_relaxed);
    }
    void showSpectrum(bool on) noexcept
    {
        spectrumMode.store(on, std::memory_order_relaxed);
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
        for (auto &value : raw)
            value.store(0.f, std::memory_order_relaxed);
    }
};
// The small waveform beside the transport: the Master output as it plays.
class OutputScopeView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
    std::array<float, OutputScope::size> samples{};
    std::array<float, OutputScope::rawSize> frames{};
    std::array<float, OutputScope::bars> levels{};
    bool quiet = true, spectrumDrawn = false;
    void timerCallback() override
    {
        const bool spectrum = OutputScope::instance().spectrumShown();
        if (spectrum != spectrumDrawn)
        {
            spectrumDrawn = spectrum;
            quiet = false;
        }
        if (spectrum)
        {
            OutputScope::instance().readRaw(frames);
            OutputScope::spectrum(frames, levels);
        }
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
        setTooltip("Output oscilloscope: the Master output as it plays. Click to switch between waveform and spectrum");
        setInterceptsMouseClicks(true, false);
        startTimerHz(30);
    }
    std::function<void()> onToggle; // The shared view.outputScopeMode action.
    void mouseUp(const juce::MouseEvent &event) override
    {
        if (event.mouseWasClicked() && onToggle)
            onToggle();
    }
    void paint(juce::Graphics &g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(.5f);
        g.setColour(panel);
        g.fillRoundedRectangle(area, 5.f);
        g.setColour(grid);
        g.drawRoundedRectangle(area, 5.f, 1.f);
        if (spectrumDrawn)
        {
            // Spectrum: 48 bars, low frequencies on the left.
            const float width = (area.getWidth() - 8.f) / float(OutputScope::bars), floor = area.getBottom() - 3.f;
            g.setColour(accent);
            for (int bar = 0; bar < OutputScope::bars; ++bar)
            {
                const float height = std::max(1.f, levels[size_t(bar)] * (area.getHeight() - 6.f));
                g.fillRect(area.getX() + 4.f + width * float(bar), floor - height, std::max(1.f, width - 1.f), height);
            }
            return;
        }
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
