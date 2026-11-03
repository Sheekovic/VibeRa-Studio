// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ChannelRoutingCoordinator.h"
// Include after actual RoutingLifetimeAcceptance fixture and heap probes.
// Actual Controller/Engine/native Player; synthetic device, no OS/hardware.
__declspec(noinline) static void channelRoutingCoordinatorTests(const juce::File &folder)
{
    using namespace vibera;
    using Coordinator = ChannelRoutingCoordinator;
    using Phase = Coordinator::Phase;
    using Error = Coordinator::Error;
    using namespace vibera::test::routing_lifetime;
    require(folder.createDirectory().wasOk(), "Owned coordinator evidence");
    Storage storage(folder.getChildFile("storage"));
    {
        Fixture fixture(folder.getChildFile("route-history"));
        Coordinator coordinator(fixture.controller, *fixture.audio);
        const Coordinator::Context context{true, fixture.pattern};
        const auto baseline = fixture.audio->projectionDiagnostics();
        const auto revision = fixture.controller.revision;
        const auto undoCount = fixture.controller.undo.getUndoDescriptions().size();
        bool receiptObserved = true;
        int observerCalls = 0;
        fixture.controller.changed = [&]
        {
            receiptObserved &= coordinator.projectionCovers(context);
            if (++observerCalls == 1)
                throw std::runtime_error("Original observer retry fixture");
        };
        CommandArgs assign;
        assign.channel = fixture.channel;
        assign.mixer = 2;
        const auto result = coordinator.assign(assign, context, false);
        const auto *job = coordinator.find(result.operation);
        require(result.accepted() && result.committed && !result.nativeCompletionKnown && job &&
                    job->phase == Phase::pending && job->postedSerial > 0 &&
                    fixture.controller.project.channel(fixture.channel)->mixer == 2 &&
                    fixture.controller.revision == revision + 1 &&
                    fixture.controller.undo.getUndoDescriptions().size() == undoCount + 1 &&
                    fixture.controller.hasPendingControllerNotification() && receiptObserved,
                "Actual model/history commit is pending audio; receipt precedes throwing observer");
        coordinator.poll(context);
        require(job->phase == Phase::pending && !job->nativeCompletionKnown,
                "No callback means no fabricated native completion");
        require(coordinator.cancel(result.operation).error == Error::tooLate &&
                    coordinator.assign(assign, context, false).error == Error::busy &&
                    fixture.controller.deliverPendingNotifications() && receiptObserved && observerCalls == 2,
                "Post cancellation/duplicate refuse; observer retry does not issue another route");
        fixture.silentCallback("Actual route callback after observer retry");
        coordinator.poll(context);
        require(job->phase == Phase::completed && job->nativeCompletionKnown &&
                    job->selectedSerial == job->postedSerial && !coordinator.mutationBlocked(),
                "Only exact selected publication completes the pending route");
        const auto after = fixture.audio->projectionDiagnostics();
        require(after.edit == baseline.edit && after.playbackContext == baseline.playbackContext &&
                    after.replacements == baseline.replacements,
                "Actual route uses retained Edit/context, no whole projection replacement");
        for (const auto direction : {HistoryDirection::undo, HistoryDirection::redo})
        {
            const auto history = coordinator.history(direction, context, false);
            const auto *historyJob = coordinator.find(history.operation);
            require(history.accepted() && history.committed && historyJob &&
                        historyJob->phase == Phase::pending,
                    "Real prepared history remains pending until its own callback");
            fixture.silentCallback("Actual history routing graph callback");
            coordinator.poll(context);
            require(historyJob->phase == Phase::completed &&
                        fixture.controller.project.channel(fixture.channel)->mixer ==
                            (direction == HistoryDirection::undo ? 1 : 2),
                    "One actual Undo and Redo restore exact Channel assignments");
        }
        const auto beforeInvalid = fixture.controller.project.state().toXmlString();
        const auto beforeInvalidRevision = fixture.controller.revision;
        auto invalid = assign;
        invalid.mixer = 1000000;
        require(!coordinator.assign(invalid, context, false).accepted() &&
                    fixture.controller.project.state().toXmlString() == beforeInvalid &&
                    fixture.controller.revision == beforeInvalidRevision,
                "Invalid destination does not mutate model/history/native graph");
        require(fixture.controller.beginGesture("fixture-owner"), "Actual foreign gesture fixture");
        require(coordinator.assign(assign, context, false).error == Error::activeGesture &&
                    fixture.controller.cancelGestureChecked("fixture-owner").wasOk(),
                "Coordinator never steals a semantic/local owned gesture");
    }
    {
        Fixture fixture(folder.getChildFile("aggregate"));
        Coordinator coordinator(fixture.controller, *fixture.audio);
        const Coordinator::Context context{true, fixture.pattern};
        CommandArgs first;
        first.channel = fixture.channel;
        first.mixer = 2;
        auto last = first;
        last.mixer = 3;
        const auto revision = fixture.controller.revision;
        const auto result = coordinator.sequence({first, last}, "Original route-only macro", context, false);
        require(result.accepted() && result.committed && fixture.controller.revision == revision + 1 &&
                    fixture.controller.project.channel(fixture.channel)->mixer == 3,
                "Route-only aggregate commits one final model and real history revision");
        fixture.silentCallback("Actual aggregate native graph callback");
        coordinator.poll(context);
        const auto undo = coordinator.history(HistoryDirection::undo, context, false);
        require(undo.accepted(), "Actual single Undo for the whole route aggregate");
        fixture.silentCallback("Actual aggregate Undo graph callback");
        coordinator.poll(context);
        require(fixture.controller.project.channel(fixture.channel)->mixer == 1,
                "One Undo returns aggregate original destination");
        auto invalid = last;
        invalid.channel = 0;
        const auto before = fixture.controller.project.state().toXmlString();
        const auto beforeRevision = fixture.controller.revision;
        const auto beforeHistory = fixture.controller.undo.getUndoDescriptions();
        require(
            !coordinator.sequence({first, invalid}, "Invalid later assignment", context, false).accepted() &&
                fixture.controller.project.state().toXmlString() == before &&
                fixture.controller.revision == beforeRevision &&
                fixture.controller.undo.getUndoDescriptions() == beforeHistory,
            "Invalid later route step refuses whole aggregate before first mutation");
        auto original = first;
        original.mixer = 1;
        const auto noOp = coordinator.sequence({first, original}, "Net-zero aggregate", context, false);
        require(noOp.accepted() && !noOp.committed && noOp.nativeCompletionKnown &&
                    coordinator.find(noOp.operation)->noOp && fixture.controller.revision == beforeRevision,
                "Net-zero route sequence is complete with no native or history request");
    }
    {
        Fixture fixture(folder.getChildFile("cold-stopped"));
        fixture.audio->stop();
        fixture.audio->seekBeat(3.25);
        fixture.nativeEdit().getTransport().freePlaybackContext();
        const auto position = fixture.audio->positionBeat();
        const auto starts = fixture.device->starts, stops = fixture.device->stops;
        Coordinator coordinator(fixture.controller, *fixture.audio);
        const Coordinator::Context context{true, fixture.pattern};
        CommandArgs assign;
        assign.channel = fixture.channel;
        assign.mixer = 2;
        const auto result = coordinator.assign(assign, context, false);
        require(result.accepted() && result.committed && !fixture.audio->playing() &&
                    fixture.audio->positionBeat() == position && fixture.device->starts == starts &&
                    fixture.device->stops == stops,
                "Cold stopped route allocates context without forcing Play/device restart or seek");
        fixture.silentCallback("Stopped actual native callback selects route without transport Play");
        coordinator.poll(context);
        const auto readback = fixture.audio->channelRoutingReadback();
        const auto nativeSelection =
            fixture.nativeEdit().getTransport().getCurrentPlaybackContext()->getGraphSelection();
        const auto *stoppedJob = coordinator.find(result.operation);
        auto diagnostic = std::make_unique<juce::DynamicObject>();
        diagnostic->setProperty("phase", stoppedJob ? int(stoppedJob->phase) : -1);
        diagnostic->setProperty("error", stoppedJob ? int(stoppedJob->error) : -1);
        diagnostic->setProperty("nativeKnown", stoppedJob && stoppedJob->nativeCompletionKnown);
        diagnostic->setProperty("status", int(readback.status));
        diagnostic->setProperty("posted", juce::int64(readback.postedSerial));
        diagnostic->setProperty("selected", juce::int64(readback.selectedSerial));
        diagnostic->setProperty("nativePosted", juce::int64(nativeSelection.postedSerial));
        diagnostic->setProperty("nativeSelected", juce::int64(nativeSelection.selectedSerial));
        diagnostic->setProperty("nativeState", int(nativeSelection.state));
        diagnostic->setProperty("expectedSerial", stoppedJob ? juce::int64(stoppedJob->postedSerial) : 0);
        diagnostic->setProperty("positionBefore", position);
        diagnostic->setProperty("positionAfter", fixture.audio->positionBeat());
        diagnostic->setProperty("playing", fixture.audio->playing());
        diagnostic->setProperty("startsBefore", starts);
        diagnostic->setProperty("startsAfter", fixture.device->starts);
        diagnostic->setProperty("stopsBefore", stops);
        diagnostic->setProperty("stopsAfter", fixture.device->stops);
        require(folder.getChildFile("cold-stopped-result.json")
                    .replaceWithText(juce::JSON::toString(juce::var(diagnostic.release()))),
                "Preserve actual stopped native selection/cursor/lifecycle before oracle");
        require(coordinator.find(result.operation)->phase == Phase::completed && !fixture.audio->playing() &&
                    fixture.audio->positionBeat() == position && fixture.device->starts == starts &&
                    fixture.device->stops == stops,
                "Stopped graph acknowledgement preserves nonzero cursor and device lifecycle");
        const auto witnessed = coordinator.find(result.operation)->selectedSerial;
        const auto revision = fixture.controller.revision;
        const auto source = fixture.audio->projectionDiagnostics();
        juce::AudioBuffer<float> pcm(2, 256), observed(2, 1024);
        observed.clear();
        int recorded = 0;
        juce::String csv = "callback,nativePosted,nativeSelected,sourcePeak,destinationPeak,callbackMs,"
                           "allocations,destructions\n";
        auto voiceCallback = [&]
        {
            pcm.clear();
            const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
            const auto began = juce::Time::getHighResolutionTicks();
            {
                struct Probe
                {
                    bool previous = std::exchange(callbackAllocationProbe, true);
                    ~Probe()
                    {
                        callbackAllocationProbe = previous;
                    }
                } probe;
                fixture.device->process(pcm);
            }
            const auto elapsed =
                SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
            for (int c = 0; c < 2; ++c)
                observed.copyFrom(c, recorded * 256, pcm, c, 0, 256);
            ++recorded;
            const auto first = fixture.audio->mixerMeter(1), second = fixture.audio->mixerMeter(2);
            const auto native =
                fixture.nativeEdit().getTransport().getCurrentPlaybackContext()->getGraphSelection();
            csv += juce::String(recorded) + "," + juce::String(juce::int64(native.postedSerial)) + "," +
                   juce::String(juce::int64(native.selectedSerial)) + "," + juce::String(first.peak[0], 9) +
                   "," + juce::String(second.peak[0], 9) + "," + juce::String(elapsed, 6) + "," +
                   juce::String(callbackAllocations.load() - allocated) + "," +
                   juce::String(callbackDestructions.load() - destroyed) + "\n";
            require(folder.getChildFile("cold-successor-callbacks.csv").replaceWithText(csv),
                    "Preserve actual native successor route observations before assertions");
            auto raw = folder.getChildFile("cold-successor-planar-float32le.raw").createOutputStream();
            require(bool(raw), "Owned actual successor PCM");
            raw->setPosition(0);
            raw->truncate();
            for (int c = 0; c < 2; ++c)
                require(raw->write(observed.getReadPointer(c), std::size_t(recorded * 256) * sizeof(float)),
                        "Retain every actual successor sample");
            raw.reset();
            require(first.available && second.available && first.peak[0] == 0 && first.peak[1] == 0 &&
                        second.peak[0] > 1.e-7f && second.peak[1] > 1.e-7f &&
                        callbackAllocations.load() == allocated && callbackDestructions.load() == destroyed &&
                        elapsed <= 1000. * 256 / 48000.,
                    "Actual held sampler stays on destination2 through native successor, no callback heap or "
                    "deadline failure");
            dispatchMessages(6);
            coordinator.poll(context);
            const auto read = fixture.audio->channelRoutingReadback();
            require(coordinator.find(result.operation)->phase == Phase::completed &&
                        coordinator.find(result.operation)->selectedSerial == witnessed &&
                        read.status == AudioEngine::ChannelRoutingStatus::selected &&
                        read.selectedSerial >= witnessed && fixture.controller.revision == revision &&
                        fixture.audio->positionBeat() == position && !fixture.audio->playing() &&
                        fixture.device->starts == starts && fixture.device->stops == stops,
                    "Exact witnessed completion remains historical while actual next graph retains route and "
                    "stopped cursor");
        };
        fixture.audio->typingInput.handleIncomingMidiMessage(
            nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
        for (int n = 0; n < 3; ++n)
            voiceCallback();
        fixture.audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        voiceCallback(); // Legitimate sampler release tail can remain audible.
        const auto afterVoice = fixture.audio->projectionDiagnostics();
        require(afterVoice.edit == source.edit && afterVoice.playbackContext == source.playbackContext &&
                    afterVoice.channels.front().sample == source.channels.front().sample &&
                    afterVoice.channels.front().heldInputKeys == 0 &&
                    fixture.nativeEdit()
                            .getTransport()
                            .getCurrentPlaybackContext()
                            ->getGraphSelection()
                            .selectedSerial >= nativeSelection.postedSerial,
                "Native successor actually selected, sample/context reused, final typing owner released");
    }
}
