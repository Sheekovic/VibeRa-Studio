// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SampleCache.h"
#include "PreviewWaveform.h"
#include <juce_core/juce_core.h>
#include <functional>
namespace vibera
{
// One background decoder/cache owner. UI publishes only the latest completion.
// This mutex is never touched by the audio callback.
class PreviewLoader final : private juce::Thread
{
    juce::CriticalSection mutex;
    SampleCache cache;
    std::function<juce::Result(const juce::String &, std::shared_ptr<const Sample> &)> decoder;
    juce::String pending;
    std::uint64_t generation = 0;
    bool queued = false, complete = false;
    std::shared_ptr<const Sample> sample;
    juce::Result result = juce::Result::ok();
    PreviewWaveform waveform;
    void run() override
    {
        while (!threadShouldExit())
        {
            juce::String source;
            std::uint64_t request = 0;
            {
                const juce::ScopedLock guard(mutex);
                if (queued)
                {
                    source = pending;
                    request = generation;
                    queued = false;
                }
            }
            if (!request)
            {
                wait(100);
                continue;
            }
            std::shared_ptr<const Sample> prepared;
            auto decoded = juce::Result::ok();
            PreviewWaveform peaks;
            try
            {
                decoded = decoder ? decoder(source, prepared) : cache.prepare(source, prepared);
                if (decoded.wasOk() && prepared)
                    peaks = PreviewWaveform::prepare(*prepared, source);
            }
            catch (const std::exception &error)
            {
                decoded = juce::Result::fail(error.what());
            }
            catch (...)
            {
                decoded = juce::Result::fail("Sample preview decoding failed");
            }
            const juce::ScopedLock guard(mutex);
            if (request == generation && !threadShouldExit())
            {
                sample = std::move(prepared);
                waveform = std::move(peaks);
                result = decoded;
                complete = true;
            }
        }
    }

  public:
    explicit PreviewLoader(
        std::function<juce::Result(const juce::String &, std::shared_ptr<const Sample> &)> prepare = {})
        : Thread("VibeRa sample preview decoder"), decoder(std::move(prepare))
    {
    }
    ~PreviewLoader() override
    {
        signalThreadShouldExit();
        notify();
        // Control-thread shutdown only. Never forcibly terminate a decoder that
        // owns resource cleanup. Slow/disconnected filesystem shutdown is a gap.
        waitForThreadToExit(-1);
    }
    void request(const juce::String &source)
    {
        const juce::ScopedLock guard(mutex);
        ++generation;
        pending = source;
        queued = true;
        complete = false;
        sample.reset();
        if (!isThreadRunning() && !startThread())
        {
            queued = false;
            complete = true;
            result = juce::Result::fail("Unable to start sample preview decoder");
        }
        notify();
    }
    void cancel()
    {
        const juce::ScopedLock guard(mutex);
        ++generation;
        queued = complete = false;
        pending.clear();
        sample.reset();
    }
    bool poll(std::shared_ptr<const Sample> &prepared, juce::Result &outcome,
              PreviewWaveform *peaks = nullptr)
    {
        const juce::ScopedLock guard(mutex);
        if (!complete)
            return false;
        complete = false;
        prepared = std::move(sample);
        outcome = result;
        if (peaks)
            *peaks = std::move(waveform);
        return true;
    }
};
} // namespace vibera
