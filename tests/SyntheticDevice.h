// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <stdexcept>
#include "CallbackStallProbe.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Test-only driver. Output is captured in memory; never opens a physical device.
struct SyntheticCallbackTiming
{
    juce::int64 processBegan = 0, dispatchBegan = 0, dispatchEnded = 0, processEnded = 0;
    static double milliseconds(juce::int64 first, juce::int64 last)
    {
        return juce::Time::highResolutionTicksToSeconds(last - first) * 1000;
    }
    bool containedBy(juce::int64 outerBegan, juce::int64 outerEnded) const
    {
        return outerBegan <= processBegan && processBegan <= dispatchBegan &&
               dispatchBegan <= dispatchEnded && dispatchEnded <= processEnded && processEnded <= outerEnded;
    }
    static constexpr auto csvHeader = "wrapperBeforeMs,setupMs,dispatchMs,teardownMs,wrapperAfterMs,"
                                      "outerBeganQpc,processBeganQpc,dispatchBeganQpc,dispatchEndedQpc,"
                                      "processEndedQpc,outerEndedQpc";
    juce::String csv(juce::int64 outerBegan, juce::int64 outerEnded) const
    {
        return juce::String(milliseconds(outerBegan, processBegan), 6) + "," +
               juce::String(milliseconds(processBegan, dispatchBegan), 6) + "," +
               juce::String(milliseconds(dispatchBegan, dispatchEnded), 6) + "," +
               juce::String(milliseconds(dispatchEnded, processEnded), 6) + "," +
               juce::String(milliseconds(processEnded, outerEnded), 6) + "," + juce::String(outerBegan) +
               "," + juce::String(processBegan) + "," + juce::String(dispatchBegan) + "," +
               juce::String(dispatchEnded) + "," + juce::String(processEnded) + "," +
               juce::String(outerEnded);
    }
    juce::String describe(juce::int64 outerBegan, juce::int64 outerEnded) const
    {
        // Called by the test AFTER processing, never by the callback.
        return " wrapperBeforeMs=" + juce::String(milliseconds(outerBegan, processBegan), 6) +
               " setupMs=" + juce::String(milliseconds(processBegan, dispatchBegan), 6) +
               " dispatchMs=" + juce::String(milliseconds(dispatchBegan, dispatchEnded), 6) +
               " teardownMs=" + juce::String(milliseconds(dispatchEnded, processEnded), 6) +
               " wrapperAfterMs=" + juce::String(milliseconds(processEnded, outerEnded), 6) +
               " outerBeganQpc=" + juce::String(outerBegan) +
               " dispatchBeganQpc=" + juce::String(dispatchBegan) +
               " dispatchEndedQpc=" + juce::String(dispatchEnded) +
               " outerEndedQpc=" + juce::String(outerEnded);
    }
};
struct SyntheticCallbackSpan
{
    SyntheticCallbackTiming timing;
    juce::int64 began = 0, ended = 0;
};

class SyntheticDevice final : public juce::AudioIODevice
{
    juce::AudioIODeviceCallback *callback = nullptr;
    bool opened = false;
    double rate = 48000;
    int block = 256;
    juce::BigInteger outputs;
    vibera::test::CallbackStallProbe stallProbe;
    SyntheticCallbackTiming timing;

  public:
    int starts = 0, stops = 0;
    SyntheticDevice() : AudioIODevice("In-memory output", "VibeRa synthetic") {}
    ~SyntheticDevice() override
    {
        close();
    }
    juce::StringArray getOutputChannelNames() override
    {
        return {"Left", "Right"};
    }
    juce::StringArray getInputChannelNames() override
    {
        return {};
    }
    juce::Array<double> getAvailableSampleRates() override
    {
        return {44100., 48000.};
    }
    juce::Array<int> getAvailableBufferSizes() override
    {
        return {256, 512};
    }
    int getDefaultBufferSize() override
    {
        return 256;
    }
    juce::String open(const juce::BigInteger &, const juce::BigInteger &o, double r, int b) override
    {
        outputs = o;
        rate = r > 0 ? r : 48000;
        block = b > 0 ? b : 256;
        opened = true;
        return {};
    }
    void close() override
    {
        stop();
        opened = false;
    }
    bool isOpen() override
    {
        return opened;
    }
    void start(juce::AudioIODeviceCallback *c) override
    {
        if (callback == c)
            return;
        stop();
        callback = c;
        if (callback)
        {
            ++starts;
            callback->audioDeviceAboutToStart(this);
        }
    }
    void stop() override
    {
        auto *old = callback;
        callback = nullptr;
        if (old)
        {
            ++stops;
            old->audioDeviceStopped();
        }
    }
    bool isPlaying() override
    {
        return callback != nullptr;
    }
    juce::String getLastError() override
    {
        return {};
    }
    int getCurrentBufferSizeSamples() override
    {
        return block;
    }
    double getCurrentSampleRate() override
    {
        return rate;
    }
    int getCurrentBitDepth() override
    {
        return 32;
    }
    juce::BigInteger getActiveOutputChannels() const override
    {
        return outputs;
    }
    juce::BigInteger getActiveInputChannels() const override
    {
        return {};
    }
    int getOutputLatencyInSamples() override
    {
        return 0;
    }
    int getInputLatencyInSamples() override
    {
        return 0;
    }
    void process(juce::AudioBuffer<float> &output)
    {
        timing.processBegan = juce::Time::getHighResolutionTicks();
        output.clear();
        if (callback)
        {
            stallProbe.enter();
            timing.dispatchBegan = juce::Time::getHighResolutionTicks();
            callback->audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2,
                                                       output.getNumSamples(), {});
            timing.dispatchEnded = juce::Time::getHighResolutionTicks();
            stallProbe.leave();
        }
        else
            timing.dispatchBegan = timing.dispatchEnded = juce::Time::getHighResolutionTicks();
        timing.processEnded = juce::Time::getHighResolutionTicks();
    }
    const SyntheticCallbackTiming &lastTiming() const noexcept
    {
        // Serialized synthetic driver; inspect on its owner after process returns.
        return timing;
    }
};
class SyntheticDeviceType final : public juce::AudioIODeviceType
{
  public:
    SyntheticDeviceType() : AudioIODeviceType("VibeRa synthetic") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input = false) const override
    {
        return input ? juce::StringArray{} : juce::StringArray{"In-memory output"};
    }
    int getDefaultDeviceIndex(bool) const override
    {
        return 0;
    }
    int getIndexOfDevice(juce::AudioIODevice *, bool) const override
    {
        return 0;
    }
    bool hasSeparateInputsAndOutputs() const override
    {
        return true;
    }
    juce::AudioIODevice *createDevice(const juce::String &output, const juce::String &) override
    {
        return output.isNotEmpty() ? new SyntheticDevice : nullptr;
    }
};
struct TestDispatchStatistics
{
    std::uint64_t calls = 0, waits = 0, messages = 0, longWaits = 0;
    double requestedMs = 0, totalMs = 0, waitMs = 0, maxWaitMs = 0, messageMs = 0, maxCallMs = 0;
};
inline TestDispatchStatistics &testDispatchStatistics()
{
    static thread_local TestDispatchStatistics statistics;
    return statistics;
}
inline void dispatchMessages(int milliseconds)
{
    // Test/control thread only. Sleep(1) and timeGetTime deadlines can expand to
    // a Windows scheduling tick in hidden/occluded test processes. Use a private
    // high-resolution wait, without changing system timer/power settings or DSP.
#if JUCE_WINDOWS
    struct Delay
    {
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_MODIFY_STATE | SYNCHRONIZE);
        ~Delay()
        {
            if (timer)
                CloseHandle(timer);
        }
        void wait()
        {
            const auto began = juce::Time::getMillisecondCounterHiRes();
            LARGE_INTEGER due;
            due.QuadPart = -10000; // Relative one millisecond, in100-nanosecond units.
            if (!timer || !SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ||
                WaitForSingleObject(timer, 2000) != WAIT_OBJECT_0)
                throw std::runtime_error("Owned test-dispatch timer unavailable");
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - began;
            auto &statistics = testDispatchStatistics();
            ++statistics.waits;
            statistics.waitMs += elapsed;
            statistics.maxWaitMs = std::max(statistics.maxWaitMs, elapsed);
            if (elapsed >= 10)
                ++statistics.longWaits;
        }
    };
    static thread_local Delay delay;
#endif
    const auto began = juce::Time::getMillisecondCounterHiRes();
    const auto end = began + std::max(0, milliseconds);
    auto &statistics = testDispatchStatistics();
    ++statistics.calls;
    statistics.requestedMs += std::max(0, milliseconds);
    do
    {
#if JUCE_WINDOWS
        MSG message;
        const auto messageBegan = juce::Time::getMillisecondCounterHiRes();
        for (int count = 0; count < 256 && PeekMessage(&message, nullptr, 0, 0, PM_REMOVE); ++count)
        {
            ++statistics.messages;
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        statistics.messageMs += juce::Time::getMillisecondCounterHiRes() - messageBegan;
        delay.wait();
#else
        juce::Thread::sleep(1);
#endif
    } while (juce::Time::getMillisecondCounterHiRes() < end);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - began;
    statistics.totalMs += elapsed;
    statistics.maxCallMs = std::max(statistics.maxCallMs, elapsed);
}
