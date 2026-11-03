// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Sampler.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>

namespace vibera
{
// One control-thread producer, one device callback. Shared ownership changes only
// on the producer; retiring a slot is an atomic flag, never sample destruction.
class SamplePreview final : public juce::AudioIODeviceCallback
{
    enum State
    {
        free,
        writing,
        ready,
        playing,
        retired
    };
    struct Slot
    {
        std::atomic<int> state{free};
        std::shared_ptr<const Sample> sample;
        std::uint64_t serial = 0;
        double start = 0;
        bool toEnd = false;
    };
    std::array<Slot, 4> slots;
    std::uint64_t serial = 0;
    std::atomic<std::uint64_t> stopSerial{0}, starts{0}, callbacks{0};
    std::atomic<bool> active{false}, full{false}, deviceWasStarted{false};
    std::atomic<float> volume{.7f}, progress{0};
    std::atomic<std::uint64_t> soundingSerial{0};
    float smoothedVolume = .7f;
    int current = -1;
    double position = 0, startPosition = 0, outputRate = 48000;
    void retire() noexcept
    {
        if (current >= 0)
            slots[size_t(current)].state.store(retired, std::memory_order_release);
        current = -1;
        active.store(false, std::memory_order_relaxed);
    }

  public:
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    bool publish(std::shared_ptr<const Sample> sample, float fraction = 0, bool toEnd = false)
    {
        if (!sample || sample->data.getNumChannels() < 2 || sample->data.getNumSamples() < 2 ||
            !std::isfinite(sample->sampleRate) || sample->sampleRate <= 0 || !std::isfinite(fraction))
            return false;
        for (int pass = 0; pass < 2; ++pass)
            for (auto &slot : slots)
            {
                int state = slot.state.load(std::memory_order_acquire);
                if ((pass == 0 && (state == free || state == retired)) || (pass == 1 && state == ready))
                    if (slot.state.compare_exchange_strong(state, writing, std::memory_order_acq_rel))
                    {
                        slot.start = juce::jlimit(0.f, 1.f, fraction) * (sample->data.getNumSamples() - 1);
                        slot.toEnd = toEnd;
                        slot.sample = std::move(sample);
                        slot.serial = ++serial;
                        slot.state.store(ready, std::memory_order_release);
                        return true;
                    }
            }
        return false;
    }
    std::uint64_t publishedSerial() const noexcept
    {
        return serial;
    } // control thread only
    std::uint64_t playingSerial() const noexcept
    {
        return soundingSerial.load();
    }
    float positionFraction() const noexcept
    {
        return progress.load();
    }
    void stop() noexcept
    {
        stopSerial.store(++serial, std::memory_order_release);
    }
    void setVolume(float value) noexcept
    {
        if (std::isfinite(value))
            volume.store(juce::jlimit(0.f, 1.f, value));
    }
    float getVolume() const noexcept
    {
        return volume.load();
    }
    void setFull(bool value) noexcept
    {
        full.store(value);
    }
    bool getFull() const noexcept
    {
        return full.load();
    }
    bool isActive() const noexcept
    {
        return active.load(std::memory_order_relaxed);
    }
    std::uint64_t startCount() const noexcept
    {
        return starts.load();
    }
    std::uint64_t callbackCount() const noexcept
    {
        return callbacks.load();
    }
    // Device lifecycle witness on the existing callback registration. Never
    // reset on stop: initial offline composition differs from device loss.
    bool deviceEverStarted() const noexcept { return deviceWasStarted.load(std::memory_order_acquire); }
    void audioDeviceAboutToStart(juce::AudioIODevice *device) override
    {
        deviceWasStarted.store(true, std::memory_order_release);
        outputRate = device->getCurrentSampleRate();
        if (!std::isfinite(outputRate) || outputRate <= 0)
            outputRate = 48000;
    }
    void audioDeviceStopped() override
    {
        retire();
    }
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *outputs, int channels,
                                          int frames, const juce::AudioIODeviceCallbackContext &) override
    {
        ++callbacks;
        for (int c = 0; c < channels; ++c)
            if (outputs[c])
                std::fill_n(outputs[c], frames, 0.f);
        const auto stopped = stopSerial.load(std::memory_order_acquire);
        if (current >= 0 && slots[size_t(current)].serial <= stopped)
            retire();
        // Bounded drain: newest successfully acquired request wins. Ready slots
        // may be coalesced by the producer, but playing slots are exclusively ours.
        for (size_t i = 0; i < slots.size(); ++i)
        {
            int expected = ready;
            if (!slots[i].state.compare_exchange_strong(expected, playing, std::memory_order_acquire))
                continue;
            if (slots[i].serial <= stopped ||
                (current >= 0 && slots[i].serial < slots[size_t(current)].serial))
            {
                slots[i].state.store(retired, std::memory_order_release);
                continue;
            }
            retire();
            current = int(i);
            position = startPosition = slots[i].start;
            soundingSerial.store(slots[i].serial);
            active.store(true, std::memory_order_relaxed);
            ++starts;
        }
        if (current < 0 || channels <= 0 || frames <= 0)
            return;
        const auto &sample = *slots[size_t(current)].sample;
        const int sampleFrames = sample.data.getNumSamples();
        const double limit = (full.load() || slots[size_t(current)].toEnd)
                                 ? double(sample.data.getNumSamples() - 1)
                                 : std::min(double(sample.data.getNumSamples() - 1), sample.sampleRate * 5.);
        const float targetVolume = volume.load();
        const float smoothing = float(1. / (.005 * outputRate));
        const double delta = sample.sampleRate / outputRate;
        for (int frame = 0; frame < frames; ++frame)
        {
            if (position >= limit)
            {
                retire();
                break;
            }
            const int index = int(position);
            const float fraction = float(position - index);
            const float envelope =
                float(std::min({1., (position - startPosition) / (sample.sampleRate * .002),
                                (limit - position) / (sample.sampleRate * .003)}));
            smoothedVolume += (targetVolume - smoothedVolume) * smoothing;
            auto read = [&](int c)
            {
                const auto *data = sample.data.getReadPointer(c);
                return (data[index] + fraction * (data[index + 1] - data[index])) * smoothedVolume * envelope;
            };
            if (channels == 1)
            {
                if (outputs[0])
                    outputs[0][frame] = (read(0) + read(1)) * .5f;
            }
            else
                for (int c = 0; c < 2; ++c)
                    if (outputs[c])
                        outputs[c][frame] = read(c);
            position += delta;
        }
        progress.store(float(std::min(1., position / (sampleFrames - 1))));
    }
};
} // namespace vibera
