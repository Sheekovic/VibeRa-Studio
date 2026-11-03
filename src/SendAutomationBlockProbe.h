// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal isolated experiment, NOT a registered/persisted send automation target.
#pragma once
#include "LiveTimeline.h"
#include <tracktion_graph/tracktion_graph/tracktion_SendGainBlock.h>
#include <stdexcept>

namespace vibera::experimental
{
class SendAutomationBlockProbe
{
    const std::shared_ptr<LiveTimeline> timeline;
    const int source, destination;
    const AutomationKey key;
    const double preparedRate;
    mutable LiveTimeline::Lease document; // One selected DSP owner, graph-local shell.

  public:
    SendAutomationBlockProbe(std::shared_ptr<LiveTimeline> scope, int from, int to,
                             AutomationKey experimentalKey, double rate)
        : timeline(std::move(scope)), source(from), destination(to), key(experimentalKey), preparedRate(rate)
    {
        if (!timeline || source <= 0 || destination < 0 || source == destination ||
            key != AutomationKey{AutomationParameter::mixerGain, source, 0} || !std::isfinite(rate) ||
            rate <= 0)
            throw std::invalid_argument("sendAutomation.invalidBinding: Fixed identified edge/rate required");
        document = timeline->acquire();
        if (!document.document || !route(*document.document))
            throw std::invalid_argument(
                "sendAutomation.unprepared: Pin valid graph-local route document before DSP");
    }
    SendAutomationBlockProbe(const SendAutomationBlockProbe &) = delete;
    SendAutomationBlockProbe &operator=(const SendAutomationBlockProbe &) = delete;
    // One bounded lease acquisition covers BOTH manual target and curve.
    // Lease replacement only changes reader counters; no owner/destructor/heap work.
    tracktion::graph::SendGainBlockResult fill(const tracktion::graph::SendGainBlockContext &clock,
                                               std::span<float> output) const noexcept
    {
        using Mode = tracktion::graph::SendGainBlockMode;
        if (clock.sampleRate != preparedRate || clock.frames <= 0 || clock.frames != int(output.size()) ||
            clock.referenceRange.getLength() != clock.frames || !std::isfinite(clock.editStartSeconds) ||
            !std::isfinite(clock.secondsPerFrame) || clock.secondsPerFrame < 0)
            return {};
        auto next = timeline->acquire();
        if (next.document)
            document = std::move(next);
        if (!document.document)
            return {};
        const auto &doc = *document.document;
        if (!std::isfinite(doc.tempo) || doc.tempo <= 0)
            return {};
        const auto *edge = route(doc);
        if (!edge || !std::isfinite(edge->gain) || edge->gain < 0 || edge->gain > 2)
            return {};
        const auto *curve = clock.playing || clock.rendering ? doc.automation.find(key) : nullptr;
        if (!curve)
            return {Mode::manual, edge->gain};
        const double start = doc.tempoMap.beatAt(clock.editStartSeconds);
        const double step = clock.secondsPerFrame * doc.tempoMap.bpmAt(start) / 60.;
        for (int frame = 0; frame < clock.frames; ++frame)
        {
            const auto gain = curve->valueAt(start + frame * step);
            if (!std::isfinite(gain) || gain < 0 || gain > 2)
                return {};
            output[size_t(frame)] = gain;
        }
        return {Mode::automated, edge->gain};
    }

  private:
    const LiveDocument::MixerSendControl *route(const LiveDocument &doc) const noexcept
    {
        const auto pair = std::pair{source, destination};
        const auto found = std::lower_bound(doc.mixerSends.begin(), doc.mixerSends.end(), pair,
                                            [](const auto &edge, const auto &target)
                                            { return std::pair{edge.source, edge.target} < target; });
        return found != doc.mixerSends.end() && found->source == source && found->target == destination
                   ? &*found
                   : nullptr;
    }
};
} // namespace vibera::experimental
