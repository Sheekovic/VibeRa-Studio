// SPDX-License-Identifier: AGPL-3.0-or-later
// ORIGINAL unbuilt/unrun draft; include after require + CallbackAllocationProbe.
#pragma once
#include "../src/MidiRecording.h"
#include <limits>

namespace midi_take_recovery_acceptance
{
using namespace vibera;
inline void note(const Note &n, Id channel, int pitch, double beat, double length, float velocity)
{
    require(n.id == 0 && n.channel == channel && n.pitch == pitch && std::abs(n.beat - beat) < 1.e-10 &&
                std::abs(n.length - length) < 1.e-10 && std::abs(n.velocity - velocity) < 1.e-6 && !n.step &&
                !n.muted,
            "Retained take follows independent exact musical note oracle");
}
inline void tests(const juce::File &folder)
{
    {
        MidiTake take;
        take.begin(7, 10, 3.9, 120, 4);
        take.add({1, 10, 7, 1, {0x90, 60, 100}});
        require(take.heldCount() == 1, "Held note is owned before sealing");
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        const auto seal = take.seal(10.15);
        preparationAllocationFailureAfter = -1;
        require(seal == MidiTake::SealResult::ready && !preparationAllocationFailureHit &&
                    take.heldCount() == 0 && take.finishedNotes().size() == 2,
                "Preallocated seal closes a held loop-crossing note without any new allocation");
        note(take.finishedNotes()[0], 1, 60, 3.9, .1, 100.f / 127);
        note(take.finishedNotes()[1], 1, 60, 0, .2, 100.f / 127);
        require(take.seal(30) == MidiTake::SealResult::ready && take.finishedNotes().size() == 2,
                "Sealed stop time is immutable; retry cannot grow the held note");
        auto result = take.releaseFinishedNotes();
        require(result.size() == 2 && take.releaseFinishedNotes().empty(),
                "Valid sealed note ownership is transferred at most once");
    }
    {
        MidiTake take;
        take.begin(8, 0, 0, 120, 0);
        const MidiCapture::Event first{2, 0, 8, 3, {0x90, 64, 100}, 0};
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        const auto result = take.acceptCapturedEvent(first);
        preparationAllocationFailureAfter = -1;
        require(result == MidiTake::EventResult::retryPending && preparationAllocationFailureHit &&
                    take.hasPendingEvent() && take.pendingEvent()->bytes == first.bytes &&
                    take.pendingEvent()->seconds == first.seconds && take.heldCount() == 0 &&
                    take.size() == 0,
                "Popped note-on remains explicitly owned when map preparation allocation fails");
        require(take.seal(.2) == MidiTake::SealResult::pendingInput && take.releaseFinishedNotes().empty(),
                "Pending popped event prevents false successful seal/release");
        require(take.acceptCapturedEvent({2, .1, 8, 3, {0x80, 64, 0}, 0}) == MidiTake::EventResult::busy &&
                    take.pendingEvent()->bytes == first.bytes,
                "Additional event never overwrites pending input; consumer must not pop while busy");
        require(take.retryPendingEvent() == MidiTake::EventResult::applied && !take.hasPendingEvent(),
                "Same popped event is applied after memory recovery");
        require(take.acceptCapturedEvent({2, .125, 8, 3, {0x80, 64, 0}, 0}) ==
                        MidiTake::EventResult::applied &&
                    take.seal(.125) == MidiTake::SealResult::ready && take.finishedNotes().size() == 1,
                "Recovered event retains original musical time, channel and velocity");
        note(take.finishedNotes()[0], 2, 64, 0, .25, 100.f / 127);
    }
    {
        MidiTake take;
        take.begin(9, 0, 0, 120, 0);
        take.add({3, 0, 9, 2, {0x90, 67, 100}});
        const MidiCapture::Event retrigger{3, .125, 9, 2, {0x90, 67, 80}, 0};
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        const auto result = take.acceptCapturedEvent(retrigger);
        preparationAllocationFailureAfter = -1;
        require(result == MidiTake::EventResult::retryPending && preparationAllocationFailureHit &&
                    take.heldCount() == 1 && take.size() == 0,
                "Failed retrigger cannot erase/close old held note before new owner is prepared");
        require(take.retryPendingEvent() == MidiTake::EventResult::applied && take.size() == 1 &&
                    take.heldCount() == 1,
                "Retried retrigger closes old note exactly once");
        take.add({3, .3, 9, 2, {0x80, 67, 0}});
        const auto notes = take.finish(.3);
        require(notes.size() == 2, "Retrigger recovery has no duplicate or missing notes");
        note(notes[0], 3, 67, 0, .25, 100.f / 127);
        note(notes[1], 3, 67, .25, .35, 80.f / 127);
    }
    {
        MidiTake take;
        take.begin(10, 0, 0, 120, 0);
        take.add({4, 0, 10, 1, {0x90, 60, 90}});
        take.add({4, .1, 10, 5, {0x90, 60, 70}, 1});
        take.add({4, .2, 10, 1, {0x80, 60, 0}});
        take.add({4, .3, 10, 5, {0x80, 60, 0}, 1});
        const auto notes = take.finish(.4);
        require(notes.size() == 2, "Physical and typing ownership remain independent");
        note(notes[0], 4, 60, 0, .4, 90.f / 127);
        note(notes[1], 4, 60, .2, .4, 70.f / 127);
    }
    {
        MidiTake take;
        take.begin(11, 0, 0, 120, 0);
        // Quota is the preexisting public limit. Explicit invalid capture retains
        // all complete notes and the unclosed overflow owner for diagnostics.
        for (size_t i = 0; i < MidiTake::maximumNotes; ++i)
        {
            const double time = double(i) / 1000.;
            take.add({5, time, 11, 1, {0x90, 72, 100}});
            take.add({5, time + .00025, 11, 1, {0x80, 72, 0}});
        }
        take.add({5, 20, 11, 1, {0x90, 72, 100}});
        require(take.seal(20.1) == MidiTake::SealResult::invalid &&
                    take.failureCode() == MidiTake::Failure::noteQuota && take.overflowed() &&
                    take.finishedNotes().size() == MidiTake::maximumNotes && take.heldCount() == 1 &&
                    take.releaseFinishedNotes().empty(),
                "Quota cannot silently release truncated take or erase held owner");
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        take.cancel();
        preparationAllocationFailureAfter = -1;
        require(!preparationAllocationFailureHit && take.finishedNotes().empty() && take.heldCount() == 0 &&
                    !take.hasPendingEvent() && !take.overflowed(),
                "Explicit cancel remains usable with next allocation fault armed");
    }
    {
        MidiTake original;
        original.begin(22, 0, 0, 120, 0);
        original.add({8, 0, 22, 1, {0x90, 61, 100}});
        original.add({8, .1, 22, 1, {0x80, 61, 0}});
        original.add({8, .2, 22, 1, {0x90, 63, 80}});
        auto copy = original; // std::vector copy need not retain reserved capacity.
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        const auto result = copy.seal(.4);
        preparationAllocationFailureAfter = -1;
        require(result == MidiTake::SealResult::retryRequired && preparationAllocationFailureHit &&
                    copy.size() == 1 && copy.heldCount() == 1 && !copy.isSealed(),
                "Seal preparation failure cannot erase held note or existing complete notes");
        require(copy.seal(.4) == MidiTake::SealResult::ready && copy.finishedNotes().size() == 2,
                "Seal retry after capacity recovery preserves exact complete take");
        note(copy.finishedNotes()[0], 8, 61, 0, .2, 100.f / 127);
        note(copy.finishedNotes()[1], 8, 63, .4, .4, 80.f / 127);
        require(original.size() == 1 && original.heldCount() == 1,
                "Preview/copy failures cannot mutate original recording builder");
        original.markInputFailure();
        require(
            original.seal(.4) == MidiTake::SealResult::invalid &&
                original.failureCode() == MidiTake::Failure::captureInput && original.size() == 1 &&
                original.heldCount() == 1 && original.releaseFinishedNotes().empty(),
            "Queue/clock failure preserves diagnostic data but never releases partial capture as complete");
    }
    {
        MidiTake take;
        take.begin(12, 0, 0, 120, 0);
        take.add({6, 0, 12, 1, {0x90, 60, 100}});
        require(take.seal(std::numeric_limits<double>::quiet_NaN()) == MidiTake::SealResult::invalid &&
                    take.failureCode() == MidiTake::Failure::invalidTime && take.heldCount() == 1,
                "Invalid seal clock retains held musical state explicitly");
    }
    {
        // Deterministic boundary fixture, NOT a concurrent hardware test: an
        // already in-flight producer publishes after the obsolete early cutoff.
        MidiTake take;
        take.begin(30, 0, 0, 120, 0);
        MidiCapture capture;
        capture.queue[0] = {9, .126, 30, 1, {0x90, 66, 100}, 0};
        capture.write = 1;
        capture.active = false; // In-flight producer published and then quiesced.
        MidiClock clock;
        clock.publish(.128, 1, .005, true);
        MidiClock::Position cutoff;
        require(clock.read(cutoff), "Post-quiescence cutoff fixture available");
        MidiCapture::Event event;
        require(capture.pop(event) && take.acceptCapturedEvent(event) == MidiTake::EventResult::applied &&
                    take.seal(cutoff.seconds) == MidiTake::SealResult::ready &&
                    take.finishedNotes().size() == 1,
                "Valid in-flight event drains before finalization at post-quiescence cutoff");
        note(take.finishedNotes()[0], 9, 66, .252, .004, 100.f / 127);
        require(take.finishedNotes()[0].beat + take.finishedNotes()[0].length <= cutoff.seconds * 2 + 1.e-12,
                "No minimum-length note may extend beyond this valid stop boundary");
    }
    require(folder.getChildFile("midi-take-recovery-primitive.txt")
                .replaceWithText("PASS original retained MIDI take map-allocation/retrigger/held-loop "
                                 "seal/quota/cancel/invalid-clock "
                                 "oracles; original capture musical/source semantics preserved. Control "
                                 "primitive only: no Studio "
                                 "history/file/MCP integration, CRT OOM, audio, native input, hardware or "
                                 "listening qualification.\n"),
            "Retained MIDI primitive evidence");
}
} // namespace midi_take_recovery_acceptance
