// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/PluginDryHistory.h"
#include <juce_core/juce_core.h>
#include <array>
#include <stdexcept>

// Include after the existing callback allocation and SyntheticCallbackTiming helpers.
// COMPONENT proof only: no SDK/Host IPC, native graph, source Session or retirement claim.
namespace vibera
{
struct PluginDryHistoryFixture
{
    static void feed(PluginDryHistory &h, float *left, float *right, int count, int delay,
                     PluginLatencyStamp stamp, bool completed, bool dry)
    {
        for (int n = 0; n < count; ++n)
        {
            h.write(0, left[n]);
            h.write(1, right[n]);
            left[n] = h.tap(0, delay);
            right[n] = h.tap(1, delay);
            h.advance();
        }
        h.record(count, delay, stamp, completed, dry);
    }
    static void exhaustedCounter(PluginDryHistory &h)
    {
        h.last.frames = std::numeric_limits<std::uint64_t>::max();
    }
};
namespace test::plugin_dry_history
{
inline void check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
inline float source(int channel, std::int64_t t)
{
    if (t < 0)
        return 0;
    // Independent signed, sparse, nonconstant dyadic PCM; exact float oracle.
    const auto pulse = t % 37;
    if (pulse != 0 && pulse != 1 && pulse != 17)
        return 0;
    const float amplitude = pulse == 0 ? .25f : pulse == 1 ? -.125f : .0625f;
    return channel == 0 ? amplitude : -amplitude * .5f;
}
struct Driver
{
    const juce::File evidence;
    const int hostOwner = 1, nodeOwner = 2;
    PluginDryHistory::Ptr history = std::make_shared<PluginDryHistory>(&hostOwner);
    std::array<float, 256> left{}, right{};
    std::int64_t reference = 0; // Independent fixture input origin, never rebased from history.
    std::uint64_t calls = 0;
    juce::String csv = "call,start,frames,delay,owner,spans,logicalFrames,ms,new,delete\n";
    explicit Driver(juce::File path) : evidence(std::move(path)) {}
    void drive(int count, int delay, bool detached, bool completed = true, bool dry = true)
    {
        for (int n = 0; n < count; ++n)
        {
            left[std::size_t(n)] = source(0, reference + n);
            right[std::size_t(n)] = source(1, reference + n);
        }
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto start = juce::Time::getHighResolutionTicks();
        PluginDryHistory::Status result = PluginDryHistory::Status::ready;
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            if (detached)
                result = history->render(&nodeOwner, left.data(), right.data(), count);
            else
                PluginDryHistoryFixture::feed(*history, left.data(), right.data(), count, delay,
                                              {delay == 287 || delay == 799 ? 31 : 0, 1}, completed, dry);
        }
        const auto elapsed =
            SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
        const auto newCalls = callbackAllocations.load() - allocations;
        const auto deleteCalls = callbackDestructions.load() - destructions;
        const auto witness = history->witness(); // Serialized COMPONENT DSP owner, not live CONTROL readback.
        csv += juce::String(juce::int64(++calls)) + "," + juce::String(reference) + "," +
               juce::String(count) + "," + juce::String(delay) + "," + (detached ? "continuation" : "host") +
               "," + juce::String(juce::int64(witness.spans)) + "," +
               juce::String(juce::int64(witness.frames)) + "," + juce::String(elapsed, 9) + "," +
               juce::String(juce::int64(newCalls)) + "," + juce::String(juce::int64(deleteCalls)) + "\n";
        try
        {
            check(result == PluginDryHistory::Status::ready && newCalls == 0 && deleteCalls == 0 &&
                      elapsed <= double(count) * 1000. / 48000.,
                  "Every component span has its original physical-length deadline and zero C++ heap calls");
            for (int n = 0; n < count; ++n)
                check(left[std::size_t(n)] == source(0, reference + n - delay) &&
                          right[std::size_t(n)] == source(1, reference + n - delay),
                      "Complete independent signed sparse stereo PCM, startup, uneven spans and ring wrap");
        }
        catch (...)
        {
            evidence.replaceWithText(csv); // CONTROL; retain the failing span before unwinding.
            throw;
        }
        reference += count;
    }
    PluginDryHistory::Status claim(int delay, PluginLatencyStamp stamp)
    {
        return history->claim(&hostOwner, &nodeOwner, history->witness(), delay, stamp, 256);
    }
};
inline void tests(const juce::File &output)
{
    check(!output.exists() && output.createDirectory().wasOk(), "Fresh owned dry history component output");
    for (const int delay : {0, 256, 287, 799})
    {
        Driver f(output.getChildFile("delay-" + juce::String(delay) + ".csv"));
        // Independent fixed source extent covers D799. No worker or DSP warm-up
        // exclusion: EVERY zero/nonzero sample and callback is checked above.
        for (const int count : {256, 1, 17, 223, 31, 256, 256, 60})
            f.drive(count, delay, false);
        const PluginLatencyStamp native{delay == 287 || delay == 799 ? 31 : 0, 1};
        check(f.claim(delay, native) == PluginDryHistory::Status::ready &&
                  f.history->phase() == PluginDryHistory::Phase::continuation,
              "One-way claim of the actual populated ring, never a reset or copied queue");
        const auto zero = f.history->witness();
        check(f.history->render(&f.nodeOwner, nullptr, nullptr, 0) == PluginDryHistory::Status::ready &&
                  f.history->witness() == zero,
              "Valid zero span changes no audio/cursor/witness");
        for (int n = 0; n < 650; ++n)
            f.drive(n % 5 == 0 ? 1 : 256, delay, true);
        check(output.getChildFile("delay-" + juce::String(delay) + ".csv").replaceWithText(f.csv),
              "Retain every component callback, not provider/graph qualification");
    }
    Driver f(output.getChildFile("negative-spans.csv"));
    f.drive(256, 799, false);
    auto before = f.history->witness();
    check(f.claim(799, {31, 1}) == PluginDryHistory::Status::incompleteHistory &&
              f.history->witness() == before && f.history->phase() == PluginDryHistory::Phase::host,
          "Actual short populated history cannot become a full dry-history witness");
    for (int n = 0; n < 3; ++n)
        f.drive(256, 799, false);
    const auto original = f.history->witness();
    const int foreign = 3;
    check(f.history->claim(&foreign, &f.nodeOwner, original, 799, {31, 1}, 256) ==
              PluginDryHistory::Status::wrongOwner,
          "Foreign actual Host-owner token cannot acquire history");
    check(f.claim(799, {31, 2}) == PluginDryHistory::Status::nativeChanged &&
              f.claim(768, {31, 1}) == PluginDryHistory::Status::delayChanged,
          "Full native generation and actual dry tap must match; delay alone is insufficient");
    f.drive(17, 799, false);
    before = f.history->witness();
    check(f.history->claim(&f.hostOwner, &f.nodeOwner, original, 799, {31, 1}, 256) ==
                  PluginDryHistory::Status::staleWitness &&
              f.history->witness() == before,
          "A later Host span invalidates a previously recorded immediate handoff witness");
    f.drive(31, 799, false, true, false);
    before = f.history->witness();
    check(f.claim(799, {31, 1}) == PluginDryHistory::Status::notDry && f.history->witness() == before,
          "Handoff cannot replace an unfinished bypass crossfade");
    f.drive(1, 799, false, false, true);
    check(f.claim(799, {31, 1}) == PluginDryHistory::Status::unavailable,
          "Startup/missing/invalid wet packet cannot be a healthy handoff declaration");
    f.drive(17, 799, false);
    check(f.claim(799, {31, 1}) == PluginDryHistory::Status::ready, "Genuine later dry endpoint can claim");
    before = f.history->witness();
    f.left.fill(.25f);
    const auto audio = f.left;
    check(f.history->render(&foreign, f.left.data(), f.right.data(), 1) ==
                  PluginDryHistory::Status::wrongOwner &&
              f.history->render(&f.nodeOwner, f.left.data(), f.left.data() + 1, 17) ==
                  PluginDryHistory::Status::overlappingChannels &&
              f.history->render(&f.nodeOwner, f.left.data(), f.right.data(), 257) ==
                  PluginDryHistory::Status::invalidSpan &&
              f.history->witness() == before && f.left == audio,
          "Owner/capacity/overlap refusal precedes all audio and cursor mutation");
    check(f.claim(799, {31, 1}) == PluginDryHistory::Status::alreadyClaimed,
          "No rearm/second claim/ABA restoration of the old Host writer");
    PluginDryHistoryFixture::exhaustedCounter(*f.history);
    before = f.history->witness();
    check(f.history->render(&f.nodeOwner, f.left.data(), f.right.data(), 1) ==
                  PluginDryHistory::Status::counterExhausted &&
              f.history->witness() == before && f.left == audio,
          "Checked uint64 overflow refuses before any continuation mutation");
}
} // namespace test::plugin_dry_history
} // namespace vibera
