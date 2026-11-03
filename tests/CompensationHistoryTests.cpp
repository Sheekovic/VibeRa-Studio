// SPDX-License-Identifier: AGPL-3.0-or-later
// Independent sample-time/lifetime oracles; no device or plugin dependencies.
#include "CompensationHistory.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <utility>

static thread_local bool monitor = false;
static std::atomic<unsigned> allocations{0}, destructions{0};
void *operator new(std::size_t bytes)
{
    if (monitor)
        ++allocations;
    if (auto *p = std::malloc(bytes ? bytes : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void *p) noexcept
{
    if (p && monitor)
        ++destructions;
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    ::operator delete(p);
}

using namespace vibera::dsp;
static constexpr CompensationConfiguration config{{11, 22, 3}, 48000., 2, 7};
static std::array<float, 2> signal(int frame)
{
    return {1.f + frame * .125f, -2.f - frame * .25f};
}
static void check(bool condition)
{
    if (!condition)
        throw std::runtime_error("Compensation history independent oracle failed");
}
int main()
{
    static_assert(noexcept(std::declval<CompensationHistory &>().processFrame(std::span<const float>{},
                                                                              std::span<float>{}, 0, 0)));
    auto history = std::make_shared<CompensationHistory>(config, 4096);
    std::array<float, 2> output{};
    // Independent absolute-time oracle over many ring wraps and changing taps.
    // Expected samples come from source(frame-delay), never from another ring.
    for (int frame = 0; frame < 90; ++frame)
    {
        const auto delay = std::uint32_t(frame < 20 ? 0 : frame < 40 ? 7 : frame % 8);
        const auto input = signal(frame);
        const auto result = history->processFrame(input, output, delay, frame - 16);
        check(result.result == CompensationResult::ready && !result.discontinuity);
        check(output == signal(frame - int(delay)));
        check(result.sourceReference == frame - 16 - int(delay));
    }
    // Preparation can reuse a warmed zero-delay tap with a new node-local delay.
    const std::array previous{history};
    const std::array newEdges{config.edge};
    auto selected = selectCompensationHistory(config, previous, newEdges);
    check(selected.result == CompensationReuse::matched && selected.bank.get() == history.get());
    check(selected.bank->processFrame(signal(90), output, 7, 74).result == CompensationResult::ready);
    check(output == signal(83));
    const std::array duplicateOld{history, history};
    const std::array duplicateNew{config.edge, config.edge};
    check(selectCompensationHistory(config, duplicateOld, newEdges).result ==
          CompensationReuse::ambiguousEdge);
    check(selectCompensationHistory(config, previous, duplicateNew).result ==
          CompensationReuse::ambiguousEdge);
    auto changed = config;
    changed.sampleRate = 44100;
    check(selectCompensationHistory(changed, previous, newEdges).result ==
          CompensationReuse::incompatibleConfiguration);
    changed = config;
    changed.maximumDelay = 8;
    check(selectCompensationHistory(changed, previous, newEdges).result ==
          CompensationReuse::incompatibleConfiguration);
    changed = config;
    changed.channels = 1;
    check(selectCompensationHistory(changed, previous, newEdges).result ==
          CompensationReuse::incompatibleConfiguration);
    changed = config;
    changed.edge.consumer = 23;
    const std::array otherEdge{changed.edge};
    check(selectCompensationHistory(changed, previous, otherEdge).result ==
          CompensationReuse::noPreviousEdge);

    std::shared_ptr<CompensationHistory> carried;
    {
        auto predecessor = std::make_shared<CompensationHistory>(config, 4096);
        for (int frame = 0; frame < 20; ++frame)
            predecessor->processFrame(signal(frame), output, 0, frame);
        const std::array oldGraph{predecessor};
        carried = selectCompensationHistory(config, oldGraph, newEdges).bank;
    } // Old graph handles retire on control thread; new graph still owns bank.
    check(carried && carried->processFrame(signal(20), output, 7, 20).result == CompensationResult::ready);
    check(output == signal(13));

    CompensationHistory cold(config, 4096);
    for (int frame = 0; frame < 8; ++frame)
    {
        const auto result = cold.processFrame(signal(frame), output, 7, frame);
        check(result.result ==
              (frame < 7 ? CompensationResult::insufficientHistory : CompensationResult::ready));
        check(output == (frame < 7 ? std::array<float, 2>{0, 0} : signal(0)));
    }
    // A seek/gap invalidates old history in O(1), even if the ring still contains
    // prior matching values. Continue safely; no stale old-stream note/audio.
    auto result = cold.processFrame(signal(200), output, 1, 200);
    check(result.discontinuity && result.result == CompensationResult::insufficientHistory);
    check(output == std::array<float, 2>{0, 0});
    check(cold.processFrame(signal(201), output, 1, 201).result == CompensationResult::ready);
    check(output == signal(200));
    check(cold.processFrame(signal(0), output, 0, 0).discontinuity && output == signal(0));
    output = {17, 19};
    const std::array<float, 1> badShape{1};
    check(cold.processFrame(badShape, output, 0, 1).result == CompensationResult::invalidShape);
    check(output == std::array<float, 2>{17, 19});
    check(cold.processFrame(signal(1), output, 8, 1).result == CompensationResult::capacityExceeded);
    check(output == std::array<float, 2>{0, 0});

    CompensationHistory extreme(config, 4096);
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    check(extreme.processFrame(signal(0), output, 1, minimum).result ==
          CompensationResult::insufficientHistory);
    check(extreme.processFrame(signal(1), output, 1, minimum + 1).result == CompensationResult::ready);
    check(output == signal(0));
    // An in-place frame is legal because all input channels are written first.
    auto inPlace = signal(2);
    check(extreme.processFrame(inPlace, inPlace, 0, minimum + 2).result == CompensationResult::ready);
    check(inPlace == signal(2));
    bool refusedBudget = false;
    try
    {
        CompensationHistory impossible(config, 1);
    }
    catch (const std::length_error &)
    {
        refusedBudget = true;
    }
    check(refusedBudget);

    // Full warm history followed by 10k variable-delay frames: no allocation or
    // destruction is permitted inside processing. Shared handles stay retained.
    CompensationHistory bounded(config, 4096);
    for (int frame = 0; frame < 8; ++frame)
        bounded.processFrame(signal(frame), output, 0, frame);
    bool pcmCorrect = true;
    monitor = true;
    for (int frame = 8; frame < 10008; ++frame)
    {
        const auto delay = std::uint32_t(frame % 8);
        const auto status = bounded.processFrame(signal(frame), output, delay, frame);
        pcmCorrect =
            pcmCorrect && status.result == CompensationResult::ready && output == signal(frame - int(delay));
    }
    monitor = false;
    check(pcmCorrect && allocations.load() == 0 && destructions.load() == 0);
    std::cout << "PASS original history: absolute sample oracle, zero-to-positive reuse, wrapped taps, "
                 "cold/gap/capacity errors, edge ambiguity, incompatible preparation, in-place frames "
                 "and 10000 allocation/destruction-free frames\n";
}
