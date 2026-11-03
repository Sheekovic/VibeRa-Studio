// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
namespace vibera
{
struct MixerMeterReading
{
    float peak[2]{}, rms[2]{}, display[2]{};
    std::uint64_t blocks = 0;
    bool overload[2]{};
    std::uint64_t lastClippedBlock[2]{}, clearedThroughBlock = 0;
    unsigned ageMs = 0;
    bool available = false, stale = true;
};
// Single DSP writer; bounded, non-consuming UI/MCP readers. Every payload field
// is atomic: a seqlock around ordinary float fields would be a C++ data race.
struct MixerMeterPublication
{
    std::uint64_t completedBlocks = 0;
    bool writerInFlight = false;
};
class MixerMeter
{
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<unsigned> timeMs{0};
    std::atomic<std::uint64_t> lastClippedBlock[2]{}; // DSP writer, part of payload snapshot.
    std::atomic<std::uint64_t> clearThroughBlock{0}; // Sole CONTROL writer; not payload seqlock.
    std::atomic<float> peak[2]{}, rms[2]{}, display[2]{};
    float envelope[2]{}; // Writer only; reset at graph initialisation.
  public:
    void reset() noexcept
    {
        sequence = 0;
        timeMs = 0;
        clearThroughBlock = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            peak[ch] = 0;
            rms[ch] = 0;
            display[ch] = 0;
            envelope[ch] = 0;
            lastClippedBlock[ch] = 0;
        }
    }
    // True reset above requires quiesced DSP, just like the original meter lifecycle.
    // A live overload clear never modifies DSP payload or its sequence.
    MixerMeterPublication publication() const noexcept
    {
        const auto stamp = sequence.load(std::memory_order_acquire);
        return {stamp / 2, (stamp & 1) != 0};
    }
    std::uint64_t clearOverload() noexcept
    {
        // An odd stamp names an in-flight block; floor/2 excludes it.
        // New/in-flight clipping can legitimately re-latch after this fence.
        const auto fence = publication().completedBlocks;
        clearThroughBlock.store(fence, std::memory_order_release);
        return fence;
    }
    void publish(const juce::AudioBuffer<float> &buffer, int start, int frames, double rate) noexcept
    {
        if (frames <= 0 || rate <= 0)
            return;
        const auto prior = sequence.fetch_add(1, std::memory_order_seq_cst);
        const auto block = prior / 2 + 1;
        const float release = float(std::exp(-double(frames) / (.3 * rate)));
        for (int ch = 0; ch < 2; ++ch)
        {
            float p = 0;
            double sum = 0;
            if (ch < buffer.getNumChannels())
            {
                const auto *data = buffer.getReadPointer(ch, start);
                for (int i = 0; i < frames; ++i)
                {
                    const auto v = std::isfinite(data[i]) ? data[i] : 0.f;
                    p = std::max(p, std::abs(v));
                    sum += double(v) * v;
                }
            }
            envelope[ch] = std::max(p, envelope[ch] * release);
            if (p >= 1.f)
                lastClippedBlock[ch].store(block);
            peak[ch].store(p);
            rms[ch].store(float(std::sqrt(sum / frames)));
            display[ch].store(envelope[ch]);
        }
        timeMs.store(juce::Time::getMillisecondCounter());
        sequence.fetch_add(1, std::memory_order_seq_cst);
    }
    MixerMeterReading read() const noexcept
    {
        MixerMeterReading r;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            auto before = sequence.load();
            if (before == 0 || (before & 1))
                continue;
            for (int ch = 0; ch < 2; ++ch)
            {
                r.peak[ch] = peak[ch].load();
                r.rms[ch] = rms[ch].load();
                r.display[ch] = display[ch].load();
                r.lastClippedBlock[ch] = lastClippedBlock[ch].load();
            }
            const auto time = timeMs.load();
            r.clearedThroughBlock = clearThroughBlock.load(std::memory_order_acquire);
            if (sequence.load() != before)
                continue;
            r.blocks = before / 2;
            r.available = true;
            for (int ch = 0; ch < 2; ++ch)
                r.overload[ch] = r.lastClippedBlock[ch] > r.clearedThroughBlock;
            r.ageMs = juce::Time::getMillisecondCounter() - time;
            r.stale = r.ageMs > 250;
            if (r.stale)
                for (int ch = 0; ch < 2; ++ch)
                    r.peak[ch] = r.rms[ch] = r.display[ch] = 0;
            return r;
        }
        return {};
    }
};
} // namespace vibera
