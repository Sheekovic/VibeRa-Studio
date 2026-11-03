// SPDX-License-Identifier: AGPL-3.0-or-later
// Unbuilt original acceptance draft. No hardware, native input or listening claim.
#pragma once
#include "MixerMeter.h"
#include <array>
#include <stdexcept>
#include <thread>

namespace vibera::acceptance::overload
{
inline void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
inline void fill(juce::AudioBuffer<float>& buffer, float left, float right)
{
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
    {
        buffer.setSample(0, frame, left);
        buffer.setSample(1, frame, right);
    }
}
inline void sameReadback(const MixerMeterReading& a, const MixerMeterReading& b)
{
    check(a.available == b.available && a.blocks == b.blocks &&
              a.clearedThroughBlock == b.clearedThroughBlock,
          "Independent readers see the same non-consuming block/fence");
    for (int channel = 0; channel < 2; ++channel)
        check(a.overload[channel] == b.overload[channel] &&
                  a.lastClippedBlock[channel] == b.lastClippedBlock[channel] &&
                  a.peak[channel] == b.peak[channel] && a.rms[channel] == b.rms[channel],
              "Independent readers do not consume overload or levels");
}

inline void mixerOverloadTests()
{
    MixerMeter meter;
    juce::AudioBuffer<float> block(2, 256);
    check(!meter.read().available && meter.clearOverload() == 0,
          "Clearing an unpublished meter has fence zero and invents no level data");
    fill(block, std::nextafter(1.f, 0.f), -std::nextafter(1.f, 0.f));
    meter.publish(block, 0, 256, 48000);
    auto reading = meter.read();
    check(reading.available && reading.blocks == 1 && !reading.overload[0] && !reading.overload[1],
          "Actual peak below one does not latch");
    fill(block, 1.f, .25f);
    meter.publish(block, 0, 256, 48000);
    reading = meter.read();
    check(reading.blocks == 2 && reading.overload[0] && !reading.overload[1] &&
              reading.lastClippedBlock[0] == 2 && reading.lastClippedBlock[1] == 0,
          "Exactly 0dBFS latches left independently");
    fill(block, 0, -1.25f);
    meter.publish(block, 0, 256, 48000);
    reading = meter.read();
    check(reading.blocks == 3 && reading.overload[0] && reading.overload[1] &&
              reading.lastClippedBlock[0] == 2 && reading.lastClippedBlock[1] == 3,
          "Negative right overload latches independently without erasing left history");
    fill(block, 0, 0);
    meter.publish(block, 0, 256, 48000);
    reading = meter.read();
    check(reading.blocks == 4 && reading.peak[0] == 0 && reading.rms[1] == 0 &&
              reading.display[0] > 0 && reading.overload[0] && reading.overload[1],
          "Silence/peak decay cannot clear overload history");
    sameReadback(reading, meter.read());

    // Actual clock expiry exercises existing stale readback, independently of DSP.
    // This is a bounded test wait, not a playback/timing exclusion.
    juce::Thread::sleep(280);
    reading = meter.read();
    check(reading.available && reading.stale && reading.ageMs > 250 && reading.peak[0] == 0 &&
              reading.display[1] == 0 && reading.overload[0] && reading.overload[1] &&
              reading.lastClippedBlock[0] == 2 && reading.lastClippedBlock[1] == 3,
          "Stale/silent display retains both overload latches and their block identities");
    const auto fence = meter.clearOverload();
    check(fence == 4, "Explicit clear reports exact completed block fence");
    reading = meter.read();
    check(reading.blocks == 4 && reading.clearedThroughBlock == 4 &&
              !reading.overload[0] && !reading.overload[1] &&
              reading.lastClippedBlock[0] == 2 && reading.lastClippedBlock[1] == 3,
          "Clear uses its fence; it does not rewrite DSP clip payload or block count");
    check(meter.clearOverload() == 4, "Repeated explicit clear is deterministic");
    meter.publish(block, 0, 256, 48000);
    reading = meter.read();
    check(reading.blocks == 5 && !reading.overload[0] && !reading.overload[1],
          "Quiet newer blocks do not revive cleared history");
    fill(block, .25f, 1.f);
    meter.publish(block, 0, 256, 48000);
    reading = meter.read();
    check(reading.blocks == 6 && !reading.overload[0] && reading.overload[1] &&
              reading.lastClippedBlock[1] == 6 && reading.clearedThroughBlock == 4,
          "A genuinely newer right clip re-latches after the reset fence");
    sameReadback(reading, meter.read());
    check(meter.clearOverload() == 6, "Second clear advances fence");
    fill(block, 1.5f, 0);
    meter.publish(block, 0, 256, 48000);
    reading = meter.read();
    check(reading.overload[0] && !reading.overload[1] && reading.lastClippedBlock[0] == 7,
          "Left can re-latch without reviving old right clip");

    // Lifecycle reset is called only after the DSP writer is quiescent.
    meter.reset();
    reading = meter.read();
    check(!reading.available && !reading.overload[0] && !reading.overload[1] &&
              meter.publication().completedBlocks == 0 && meter.clearOverload() == 0,
          "True reset/device reprepare clears level lifecycle and overload state");
    fill(block, 0, 1);
    meter.publish(block, 0, 256, 44100);
    reading = meter.read();
    check(reading.blocks == 1 && !reading.overload[0] && reading.overload[1] &&
              reading.lastClippedBlock[1] == 1 && reading.clearedThroughBlock == 0,
          "New device lifecycle can latch its first block without stale clear fence");
}

// Parent provides its existing callback heap/destruction guard around the
// operation lambda. All buffers and meter state are allocated before that guard.
template <typename RunWithExistingHeapProbe>
void mixerOverloadHeapProbe(RunWithExistingHeapProbe&& run)
{
    MixerMeter meter;
    juce::AudioBuffer<float> clipped(2, 256), quiet(2, 256);
    fill(clipped, 1, -1.25f);
    fill(quiet, 0, 0);
    run([&]
        {
            for (int iteration = 0; iteration < 1000; ++iteration)
                meter.publish(iteration % 2 ? quiet : clipped, 0, 256, 48000);
        });
    const auto reading = meter.read();
    check(reading.blocks == 1000 && reading.overload[0] && reading.overload[1] &&
              reading.lastClippedBlock[0] == 999 && reading.lastClippedBlock[1] == 999,
          "Heap-probed callback publication preserves actual overload identities");
}

struct InFlightObservation
{
    bool observedWriter = false, clearExcludedInFlight = false;
    std::uint64_t clearFence = 0, completedAfterJoin = 0, clippedAfterJoin = 0;
    bool latchedAfterJoin = false;
};

// Optional actual-concurrency probe. Parent must retain an unavailable scheduling
// result distinctly; do not count an unobserved interleaving as verified.
inline InFlightObservation mixerOverloadInFlightProbe()
{
    MixerMeter meter;
    juce::AudioBuffer<float> quiet(2, 64), clipped(2, 1 << 20);
    fill(quiet, 0, 0);
    fill(clipped, 1.25f, 0);
    meter.publish(quiet, 0, 64, 48000);
    InFlightObservation result;
    for (int attempt = 0; attempt < 4 && !result.clearExcludedInFlight; ++attempt)
    {
        std::atomic<bool> release{false}, done{false};
        std::thread writer([&]
                           {
                               while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
                               meter.publish(clipped, 0, clipped.getNumSamples(), 48000);
                               done.store(true, std::memory_order_release);
                           });
        release.store(true, std::memory_order_release);
        const auto deadline = juce::Time::getMillisecondCounter() + 1000;
        bool cleared = false;
        while (!done.load(std::memory_order_acquire) && juce::Time::getMillisecondCounter() < deadline)
        {
            if (const auto state = meter.publication(); state.writerInFlight)
            {
                result.observedWriter = true;
                result.clearFence = meter.clearOverload();
                result.clearExcludedInFlight = result.clearFence == state.completedBlocks;
                cleared = true;
                break;
            }
            std::this_thread::yield();
        }
        writer.join(); // Own process-local fixture only; no audio/device thread.
        const auto reading = meter.read();
        result.completedAfterJoin = reading.blocks;
        result.clippedAfterJoin = reading.lastClippedBlock[0];
        result.latchedAfterJoin = reading.overload[0];
        if (cleared)
            check(reading.overload[0] == (reading.lastClippedBlock[0] > result.clearFence),
                  "Actual concurrent clear obeys its reported fence");
    }
    if (result.clearExcludedInFlight)
        check(result.latchedAfterJoin && result.clippedAfterJoin == result.completedAfterJoin &&
                  result.clippedAfterJoin > result.clearFence,
              "In-flight/new overload legitimately re-latches after control clear");
    return result;
}
} // namespace vibera::acceptance::overload
