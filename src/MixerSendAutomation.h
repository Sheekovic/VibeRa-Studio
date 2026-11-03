// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include <tracktion_graph/tracktion_graph/tracktion_SendGainBlock.h>
#include <cmath>
#include <stdexcept>

namespace vibera
{
// CONTROL prepares the graph shell. Each DSP call pins its immutable document
// only while evaluating that block. Inactive/retired shells must not pin stale
// generations: the shared timeline intentionally has only three bounded slots.
class MixerSendAutomation
{
    std::shared_ptr<LiveTimeline> timeline;
    int source = -1, destination = -1;
    double rate = 0;
    mutable std::atomic<bool> fault{false};

  public:
    struct Block
    {
        LiveTimeline::Lease document; // Counter-only release, never owning DSP destruction.
        bool valid = false;
        float manual = 0;
        const AutomationProgram::Lane *curve = nullptr;
        double beat = 0, step = 0;
    };
    void prepare(std::shared_ptr<LiveTimeline> scope, int from, int to, double sampleRate)
    {
        if (!scope || from <= 0 || to < 0 || from == to || !std::isfinite(sampleRate) || sampleRate <= 0)
            throw std::invalid_argument("sendAutomation.invalidBinding: Identified edge and rate required");
        auto pinned = scope->acquire();
        if (!pinned.document)
            throw std::invalid_argument("sendAutomation.unprepared: Published graph document required");
        timeline = std::move(scope);
        source = from;
        destination = to;
        rate = sampleRate;
        fault.store(false, std::memory_order_relaxed);
    }
    bool healthy() const noexcept
    {
        return !fault.load(std::memory_order_relaxed);
    }
    Block read(double seconds, double secondsPerFrame, int frames, bool playing) const noexcept
    {
        const auto failed = [&]() noexcept
        {
            fault.store(true, std::memory_order_relaxed);
            return Block{};
        };
        if (!timeline || rate <= 0 || frames <= 0 || !std::isfinite(seconds) ||
            !std::isfinite(secondsPerFrame) || secondsPerFrame < 0)
            return failed();
        auto pinned = timeline->acquire();
        if (!pinned.document)
            return failed();
        const auto &doc = *pinned.document;
        if (!std::isfinite(doc.tempo) || doc.tempo <= 0)
            return failed();
        const auto pair = std::pair{source, destination};
        const auto edge = std::lower_bound(doc.mixerSends.begin(), doc.mixerSends.end(), pair,
                                           [](const auto &item, auto key)
                                           { return std::pair{item.source, item.target} < key; });
        // A disabled Master edge still owns a graph root. Its correct gain is0.
        if (edge == doc.mixerSends.end() || edge->source != source || edge->target != destination)
            return {std::move(pinned), true, 0, nullptr, 0, 0};
        if (!std::isfinite(edge->gain) || edge->gain < 0 || edge->gain > 2)
            return failed();
        const auto *curve =
            playing
                ? doc.automation.find({AutomationParameter::mixerSend, source, std::uint32_t(destination)})
                : nullptr;
        return {std::move(pinned),
                true,
                edge->gain,
                curve,
                doc.tempoMap.beatAt(seconds),
                secondsPerFrame * doc.tempoMap.bpmAt(doc.tempoMap.beatAt(seconds)) / 60.};
    }
    tracktion::graph::SendGainBlockResult fill(const tracktion::graph::SendGainBlockContext &clock,
                                               std::span<float> output) const noexcept
    {
        using Mode = tracktion::graph::SendGainBlockMode;
        if (clock.sampleRate != rate || clock.frames != int(output.size()) ||
            clock.referenceRange.getLength() != clock.frames)
        {
            fault.store(true, std::memory_order_relaxed);
            return {};
        }
        const auto value = read(clock.editStartSeconds, clock.secondsPerFrame, clock.frames,
                                clock.playing || clock.rendering);
        if (!value.valid)
            return {};
        if (!value.curve)
            return {Mode::manual, value.manual};
        for (int frame = 0; frame < clock.frames; ++frame)
        {
            const auto gain = value.curve->valueAt(value.beat + frame * value.step);
            if (!std::isfinite(gain) || gain < 0 || gain > 2)
            {
                fault.store(true, std::memory_order_relaxed);
                return {};
            }
            output[size_t(frame)] = gain;
        }
        return {Mode::automated, value.manual};
    }
};
} // namespace vibera
