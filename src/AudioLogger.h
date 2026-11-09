// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <thread>

namespace vibera
{
// Audio logger (FL Tools > Audio logger): a rolling memory of the master output so the last
// minutes can be saved without having pressed Record. The ring buffer is allocated and freed
// only on the control thread; the audio thread (master gain stage) only copies samples into
// the published buffer. Offline renders are not logged.
class AudioLogger
{
  public:
    static AudioLogger &instance() noexcept
    {
        static AudioLogger logger;
        return logger;
    }
    static constexpr double maximumSeconds = 300; // 5 minutes: ~115 MB stereo float at 48 kHz.

    // Control thread. Replaces any previous buffer (its contents are dropped).
    juce::Result enable(double seconds, double sampleRate)
    {
        if (!(seconds >= 10 && seconds <= maximumSeconds) || !(sampleRate >= 8000 && sampleRate <= 384000))
            return juce::Result::fail("audioLog.invalid: 10..300 seconds at a valid sample rate");
        std::unique_ptr<Storage> next;
        try
        {
            next = std::make_unique<Storage>(int(seconds * sampleRate), sampleRate);
        }
        catch (...)
        {
            return juce::Result::fail("audioLog.memory: Not enough memory for that length");
        }
        publish(std::move(next));
        return juce::Result::ok();
    }
    void disable()
    {
        publish({});
    }
    bool enabled() const noexcept
    {
        return current.load() != nullptr;
    }
    double capacitySeconds() const noexcept
    {
        const auto *s = current.load();
        return s ? s->frames / s->rate : 0.;
    }
    double recordedSeconds() const noexcept
    {
        const auto *s = current.load();
        return s ? double(std::min<std::uint64_t>(s->written.load(), std::uint64_t(s->frames))) / s->rate : 0.;
    }

    // Audio thread: append one block (stereo; mono is duplicated). Lock-free, no allocation.
    void write(const juce::AudioBuffer<float> &buffer, int start, int count, double sampleRate) noexcept
    {
        writers.fetch_add(1, std::memory_order_acquire);
        if (auto *s = current.load(std::memory_order_acquire); s && count > 0 && sampleRate == s->rate)
        {
            const auto base = s->written.load(std::memory_order_relaxed);
            const int channels = buffer.getNumChannels();
            for (int i = 0; i < count; ++i)
            {
                const auto at = size_t((base + std::uint64_t(i)) % std::uint64_t(s->frames));
                const float left = channels > 0 ? buffer.getSample(0, start + i) : 0.f;
                s->left[at] = left;
                s->right[at] = channels > 1 ? buffer.getSample(1, start + i) : left;
            }
            s->written.store(base + std::uint64_t(count), std::memory_order_release);
        }
        writers.fetch_sub(1, std::memory_order_release);
    }

    // Control thread: write the last `seconds` (0 = everything held) to a 24-bit stereo WAV.
    juce::Result save(const juce::File &file, double seconds, double &savedSeconds) const
    {
        const auto *s = current.load(std::memory_order_acquire);
        if (!s)
            return juce::Result::fail("audioLog.disabled: Turn the audio logger on first");
        const auto written = s->written.load(std::memory_order_acquire);
        auto available = std::min<std::uint64_t>(written, std::uint64_t(s->frames));
        if (seconds > 0)
            available = std::min<std::uint64_t>(available, std::uint64_t(seconds * s->rate));
        if (available == 0)
            return juce::Result::fail("audioLog.empty: Nothing has played since the logger started");
        juce::AudioBuffer<float> copy(2, int(available));
        for (std::uint64_t i = 0; i < available; ++i)
        {
            const auto at = size_t((written - available + i) % std::uint64_t(s->frames));
            copy.setSample(0, int(i), s->left[at]);
            copy.setSample(1, int(i), s->right[at]);
        }
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream().release());
        if (!stream)
            return juce::Result::fail("audioLog.write: Cannot write " + file.getFullPathName());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), s->rate, 2, 24, {}, 0));
        if (!writer)
            return juce::Result::fail("audioLog.write: Cannot create the WAV writer");
        stream.release(); // The writer owns the stream now.
        if (!writer->writeFromAudioSampleBuffer(copy, 0, copy.getNumSamples()))
            return juce::Result::fail("audioLog.write: Writing the WAV failed");
        savedSeconds = double(available) / s->rate;
        return juce::Result::ok();
    }

  private:
    struct Storage
    {
        Storage(int count, double sampleRate)
            : frames(std::max(1, count)), rate(sampleRate), left(size_t(frames)), right(size_t(frames))
        {
        }
        const int frames;
        const double rate;
        std::vector<float> left, right;
        std::atomic<std::uint64_t> written{0};
    };
    std::atomic<Storage *> current{nullptr};
    std::unique_ptr<Storage> owned;
    std::atomic<int> writers{0};

    void publish(std::unique_ptr<Storage> next)
    {
        current.store(next.get(), std::memory_order_release);
        // Free the old buffer only after the audio thread has left it.
        while (writers.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        owned = std::move(next);
    }
};
} // namespace vibera
