// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginAutomationStream.h"
#include <cmath>
#include <tuple>

namespace vibera
{
PluginAutomationStream::Status PluginAutomationStream::process(
    PluginHost &host, const LiveDocument *document, Id plugin, juce::AudioBuffer<float> &buffer, int offset,
    int frames, std::span<const ipc::MidiEvent> events, double seconds, double rate, double tempo,
    bool playing, bool bypass, int numerator, int denominator, const juce::AudioBuffer<float> *auxiliary,
    int auxiliaryOffset, float wet, PluginPcmCapture *capture) noexcept
{
    const auto captureFailure = [](PluginPcmReceiptStatus status) noexcept
    {
        return status == PluginPcmReceiptStatus::capacityExceeded    ? Status::capacityExceeded
               : status == PluginPcmReceiptStatus::workerUnavailable ? Status::unavailableTarget
                                                                     : Status::invalidTiming;
    };
    if (capture)
    {
        // Whole-call admission BEFORE nativeActive, packet.render/hold, MIDI
        // partitioning, Host input or ramp state. Never fake this with process(0).
        const auto admission = host.preflightProcess(buffer, offset, frames, wet, capture);
        capture->identity = admission.identity;
        capture->count = 0;
        capture->required = admission.required;
        capture->status = admission.status;
        if (!admission.admitted)
        {
            const auto status = captureFailure(admission.status);
            return status; // Caller-owned receipt failure; no Host automation/delivery fault.
        }
        if (frames == 0)
        {
            capture->status = PluginPcmReceiptStatus::complete; // Actual empty call, no packet mutation.
            host.recordAutomationStatus(Status::ready);
            return Status::ready;
        }
        const auto &candidateProgram = document ? document->automation : empty;
        const bool candidateActive = document ? !candidateProgram.pluginLanes(plugin).empty() : nativeActive;
        if ((candidateActive || packet.pending()) &&
            (!std::isfinite(rate) || rate <= 0 || !std::isfinite(tempo) || tempo <= 0 ||
             !std::isfinite(seconds) || events.size() > ipc::events))
        {
            capture->status = PluginPcmReceiptStatus::invalidSpan;
            return Status::invalidTiming;
        }
    }
    if (offset < 0 || frames < 0 || offset > buffer.getNumSamples() ||
        frames > buffer.getNumSamples() - offset)
    {
        host.recordAutomationStatus(Status::invalidTiming);
        return Status::invalidTiming;
    }
    if (!host.offline() && frames > host.maximumLiveSpan())
    {
        // Do not return ready from the fast path after the Host refuses an
        // unsupported live span. No nativeActive/packet/audio state mutates.
        host.recordAutomationStatus(Status::invalidTiming);
        return Status::invalidTiming;
    }
    if (!std::isfinite(wet) || wet < 0 || wet > 1)
    {
        host.recordAutomationStatus(Status::invalidTiming);
        return Status::invalidTiming;
    }
    const auto &program = document ? document->automation : empty;
    if (document)
        nativeActive = !program.pluginLanes(plugin).empty();
    if (!nativeActive && !packet.pending())
    {
        if (document)
            host.setBlockPpq(document->tempoMap.beatAt(seconds), seconds);
        host.process(buffer, offset, frames, events.data(), int(events.size()), seconds, tempo, playing,
                     bypass, numerator, denominator, nullptr, 0, 0, true, auxiliary, auxiliaryOffset, wet,
                     capture);
        if (capture && capture->status != PluginPcmReceiptStatus::complete)
        {
            const auto status = captureFailure(capture->status);
            return status;
        }
        host.recordAutomationStatus(Status::ready);
        return Status::ready;
    }
    if (frames < 0 || !std::isfinite(rate) || rate <= 0 || !std::isfinite(tempo) || tempo <= 0 ||
        !std::isfinite(seconds) || events.size() > ipc::events)
    {
        host.recordAutomationStatus(Status::invalidTiming);
        return Status::invalidTiming;
    }
    Status status = document ? Status::ready : Status::snapshotUnavailable;
    const auto *route = document ? document->nativeRoute(plugin) : nullptr;
    const auto generation = route ? route->generation : 0;
    const bool needsAuthority = playing && !program.pluginLanes(plugin).empty();
    const bool available = !needsAuthority || (generation != 0 && route->instance == host.runtimeIdentity());
    // Validate the complete span before adding partition offsets. Forward an
    // invalid sentinel so the host records rejection and zeroes detector input;
    // a malformed caller must neither overflow nor replay a valid prefix.
    const bool auxiliarySpanValid =
        !auxiliary || (auxiliaryOffset >= 0 && auxiliaryOffset <= auxiliary->getNumSamples() &&
                       frames <= auxiliary->getNumSamples() - auxiliaryOffset);
    for (int done = 0; done < frames;)
    {
        const auto quantum = host.processingQuantum();
        const auto cursor = host.packetFrame();
        const auto count = std::min(frames - done, quantum - cursor);
        const auto time = seconds + done / rate;
        const double beat = document ? document->tempoMap.beatAt(time) : time * tempo / 60;
        host.setBlockPpq(beat, time);
        const auto result = document ? packet.render(program, plugin, cursor, quantum, beat,
                                                     document->tempoMap.bpmAt(beat) / (60 * rate), count, playing)
                                     : packet.hold(plugin, cursor, quantum, count, playing);
        const auto parameters = packet.events();
        std::size_t midiCount = 0;
        for (std::size_t i = 0; i < events.size(); ++i)
            if (events[i].frame >= done && events[i].frame < done + count)
            {
                auto local = events[i];
                local.frame -= done;
                midiItems[midiCount++] = {local, i};
            }
        std::sort(midiItems.begin(), midiItems.begin() + midiCount, [](const auto &a, const auto &b)
                  { return std::tie(a.event.frame, a.order) < std::tie(b.event.frame, b.order); });
        for (std::size_t i = 0; i < midiCount; ++i)
            midi[i] = midiItems[i].event;
        if (capture)
        {
            // Disjoint suffix of caller-prepared storage. Each slice ends at a
            // native Q boundary, so no duplicate segment has to be coalesced.
            PluginPcmCapture part(capture->storage.subspan(capture->count), capture->identity);
            host.process(buffer, offset + done, count, midi.data(), int(midiCount), time, tempo, playing,
                         bypass, numerator, denominator, parameters.data(), int(parameters.size()),
                         generation, result == PluginAutomationBlock::Result::ok && available, auxiliary,
                         auxiliarySpanValid && auxiliary ? auxiliaryOffset + done : -1, wet, &part);
            if (part.status != PluginPcmReceiptStatus::complete || part.identity != capture->identity ||
                part.count == 0 || part.count > capture->required - capture->count)
            {
                capture->status = part.status == PluginPcmReceiptStatus::complete
                                      ? PluginPcmReceiptStatus::invalidSpan
                                      : part.status;
                const auto failed = captureFailure(capture->status);
                return failed; // Honest partial failure; no rollback/completion claim.
            }
            int covered = 0;
            for (auto &segment : part.storage.first(part.count))
            {
                if (segment.offset != covered || segment.count <= 0 || segment.count > count - covered)
                {
                    capture->status = PluginPcmReceiptStatus::invalidSpan;
                    return Status::invalidTiming;
                }
                covered += segment.count;
                segment.offset += done; // Original whole-call offsets, never slice-relative readback.
            }
            if (covered != count)
            {
                capture->status = PluginPcmReceiptStatus::invalidSpan;
                return Status::invalidTiming;
            }
            capture->count += part.count;
        }
        else
            host.process(buffer, offset + done, count, midi.data(), int(midiCount), time, tempo, playing,
                         bypass, numerator, denominator, parameters.data(), int(parameters.size()),
                         generation, result == PluginAutomationBlock::Result::ok && available, auxiliary,
                         auxiliarySpanValid && auxiliary ? auxiliaryOffset + done : -1, wet);
        if (status == Status::ready || status == Status::snapshotUnavailable)
        {
            if (result == PluginAutomationBlock::Result::capacityExceeded)
                status = Status::capacityExceeded;
            else if (result != PluginAutomationBlock::Result::ok)
                status = Status::invalidTiming;
            else if (!available)
                status = Status::unavailableTarget;
        }
        done += count;
    }
    if (capture)
    {
        capture->status = capture->count == capture->required ? PluginPcmReceiptStatus::complete
                                                              : PluginPcmReceiptStatus::invalidSpan;
        if (capture->status != PluginPcmReceiptStatus::complete)
            return captureFailure(capture->status);
    }
    host.recordAutomationStatus(status);
    return status;
}
} // namespace vibera
