// SPDX-License-Identifier: AGPL-3.0-or-later
// Original independent bounded-bank checks; include after require().
#pragma once
#include "MidiCompensationHistory.h"

static void midiCompensationHistoryTests()
{
    using namespace vibera::dsp;
    auto configuration = [](unsigned events = 16, unsigned sources = 4)
    {
        return MidiHistoryConfiguration{
            {101, 202, 0}, 48000., 512, 64, {events, sources, events, 1, 1024 * 1024}};
    };
    const std::array<std::uint8_t, 3> on{0x90, 60, 100}, off{0x80, 60, 0}, cc{0xb0, 7, 73};
    struct Capture
    {
        std::array<RetainedShortMidi, 64> events{};
        std::array<unsigned, 64> frames{};
        std::size_t size = 0;
        void consume(MidiHistoryBatch batch)
        {
            require(size + batch.events.size() <= events.size(),
                    "Independent fixed capture has sufficient capacity");
            for (const auto &event : batch.events)
            {
                events[size] = event;
                frames[size++] = unsigned(event.due - batch.reference);
            }
        }
    };
    {
        MidiCompensationHistory bank(configuration());
        Capture capture;
        require(bank.beginBlock(0, 64, false) && bank.enqueue(16, 128, 101, on), "Queue old-delay note");
        capture.consume(bank.drain());
        bank.beginBlock(64, 64, false);
        require(bank.enqueue(16, 128, 101, off), "Queue old-delay release");
        capture.consume(bank.drain());
        bank.beginBlock(128, 64, false);
        require(bank.enqueue(16, 0, 202, on), "New zero delay shares pending bank");
        capture.consume(bank.drain());
        bank.beginBlock(192, 64, false);
        capture.consume(bank.drain());
        require(capture.size == 3 && capture.events[0].due == 144 && capture.events[0].source == 101 &&
                    capture.events[1].due == 144 && capture.events[1].source == 202 &&
                    capture.events[2].due == 208 && capture.events[2].bytes == off,
                "Old deadlines/source/stable ties survive delay becoming zero");
    }
    {
        MidiCompensationHistory bank(configuration());
        Capture capture;
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 64, 101, on);
        capture.consume(bank.drain());
        require(capture.size == 0, "Due at block end is not delivered early");
        bank.beginBlock(64, 64, false);
        capture.consume(bank.drain());
        require(capture.size == 1 && capture.events[0].due == 64 && capture.frames[0] == 0,
                "Half-open boundary delivers at next callback frame zero");
    }
    {
        MidiCompensationHistory bank(configuration());
        Capture capture;
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 128, 101, on);
        bank.enqueue(1, 128, 101, off);
        bank.enqueue(4, 128, 202, cc);
        capture.consume(bank.drain());
        bank.beginBlock(64, 64, true);
        bank.enqueue(5, 0, 303, on);
        capture.consume(bank.drain());
        require(bank.allNotesOff() && capture.size == 1 && capture.events[0].due == 69,
                "Immediate boolean panic cancels old notes and admits new current-block note");
        bank.beginBlock(128, 64, false);
        capture.consume(bank.drain());
        require(!bank.allNotesOff() && capture.size == 2 && capture.events[1].due == 132 &&
                    capture.events[1].bytes == cc && capture.events[1].source == 202,
                "Boolean panic delivered once and preserves unrelated queued controls");
    }
    {
        MidiCompensationHistory bank(configuration(2));
        Capture capture;
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 256, 101, on);
        bank.enqueue(1, 256, 202, cc);
        require(!bank.enqueue(2, 256, 101, off) && bank.allNotesOff() &&
                    (bank.observation().failures & std::uint32_t(MidiHistoryFailure::eventCapacity)),
                "Queue exhaustion is a typed safety failure instead of lost-release success");
        capture.consume(bank.drain());
        for (int start = 64; start <= 256; start += 64)
        {
            bank.beginBlock(start, 64, false);
            capture.consume(bank.drain());
        }
        require(capture.size == 1 && capture.events[0].bytes == cc && capture.events[0].due == 257,
                "Overflow panic quarantines notes while unrelated admitted control retains deadline");
    }
    {
        MidiCompensationHistory bank(configuration(16, 1));
        Capture capture;
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 0, 101, on);
        bank.enqueue(1, 0, 202, on);
        capture.consume(bank.drain());
        require(capture.size == 0 && bank.allNotesOff() &&
                    (bank.observation().failures & std::uint32_t(MidiHistoryFailure::sourceCapacity)),
                "Source-capacity failure retracts staged note before untimed safety panic");
    }
    {
        MidiCompensationHistory bank(configuration());
        bank.beginBlock(0, 64, false);
        const std::array<std::uint8_t, 4> sysex{0xf0, 1, 2, 0xf7};
        require(!bank.enqueue(0, 0, 101, sysex) && bank.allNotesOff() &&
                    (bank.observation().failures & std::uint32_t(MidiHistoryFailure::unsupportedPayload)),
                "SysEx is explicitly unsupported, never truncated or silently delivered");
    }
    {
        auto config = configuration();
        config.limits.outputEvents = 1;
        MidiCompensationHistory bank(config);
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 0, 101, on);
        bank.enqueue(1, 0, 101, off);
        const auto batch = bank.drain();
        require(batch.events.empty() && batch.allNotesOff &&
                    (bank.observation().failures & std::uint32_t(MidiHistoryFailure::outputCapacity)),
                "Output overflow retracts earlier staged note before publishing panic");
    }
    {
        MidiCompensationHistory bank(configuration());
        Capture capture;
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 256, 101, cc);
        capture.consume(bank.drain());
        bank.beginBlock(1000, 64, false);
        capture.consume(bank.drain());
        require(bank.allNotesOff() && capture.size == 0 &&
                    (bank.observation().failures & std::uint32_t(MidiHistoryFailure::referenceDiscontinuity)),
                "Reference discontinuity releases gates and prevents old-stream control replay");
    }
    {
        MidiCompensationHistory bank(configuration());
        Capture capture;
        bank.beginBlock(0, 64, false);
        bank.enqueue(0, 256, 101, on);
        bank.enqueue(1, 0, 101, std::array<std::uint8_t, 3>{0xb0, 123, 0});
        capture.consume(bank.drain());
        for (int start = 64; start <= 256; start += 64)
        {
            bank.beginBlock(start, 64, false);
            capture.consume(bank.drain());
        }
        require(capture.size == 1 && capture.events[0].bytes[1] == 123,
                "Explicit channel panic prevents older queued note from reopening gate");
    }
    // Independent crossing oracle: input On208 with old delay256 is due464.
    // After replacement, Off272 with delay0 must not succeed then reopen at464.
    // Equal deadlines are conservatively unsupported too; do not infer pairing.
    for (const auto release : {off, std::array<std::uint8_t, 3>{0x90, 60, 0}})
        for (const unsigned releaseDelay : {0u, 192u})
        {
            MidiCompensationHistory bank(configuration());
            Capture capture;
            for (int start = 0; start < 192; start += 64)
            {
                require(bank.beginBlock(start, 64, false), "Prepare continuous crossing reference");
                capture.consume(bank.drain());
            }
            require(bank.beginBlock(192, 64, false) && bank.enqueue(16, 256, 101, on) &&
                        bank.enqueue(20, 256, 202, cc),
                    "Queue exact On208+256 and unrelated CC212+256");
            capture.consume(bank.drain());
            require(capture.size == 0 && bank.beginBlock(256, 64, false),
                    "Old NoteOn464 remains pending at delay replacement");
            require(!bank.enqueue(16, releaseDelay, 101, release),
                    "Off272 at or before pending NoteOn464 is explicitly refused");
            const auto batch = bank.drain();
            const auto observation = bank.observation();
            require(batch.allNotesOff && batch.events.empty() &&
                        observation.failures == std::uint32_t(MidiHistoryFailure::deadlineInversion) &&
                        observation.panics == 1 && observation.rejected == 1,
                    "Crossing latches only typed inversion failure and emits immediate safety panic");
            for (int start = 320; start <= 512; start += 64)
            {
                require(bank.beginBlock(start, 64, false), "Continue reference after inversion failure");
                const auto later = bank.drain();
                require(!later.allNotesOff, "Inversion panic is not replayed in later blocks");
                capture.consume(later);
            }
            require(capture.size == 1 && capture.events[0].due == 468 && capture.events[0].bytes == cc &&
                        capture.events[0].source == 202,
                    "No delayed NoteOn464 reopens a gate; unrelated CC retains exact deadline468");
            require(bank.beginBlock(576, 64, false) && bank.enqueue(1, 0, 101, on) &&
                        bank.enqueue(2, 0, 101, off),
                    "Explicit safety failure still permits subsequent well-ordered notes");
            const auto recovered = bank.drain();
            require(!recovered.allNotesOff && recovered.events.size() == 2 &&
                        recovered.events[0].due == 577 && recovered.events[0].bytes == on &&
                        recovered.events[1].due == 578 && recovered.events[1].bytes == off,
                    "Recovery preserves exact new attack/release deadlines without stuck gate");
        }
    for (int difference = 0; difference < 3; ++difference)
    {
        MidiCompensationHistory bank(configuration());
        Capture capture;
        for (int start = 0; start < 192; start += 64)
        {
            bank.beginBlock(start, 64, false);
            capture.consume(bank.drain());
        }
        bank.beginBlock(192, 64, false);
        require(bank.enqueue(16, 256, 101, on), "Queue pending NoteOn464 for identity isolation");
        capture.consume(bank.drain());
        bank.beginBlock(256, 64, false);
        auto independentOff = off;
        if (difference == 1)
            independentOff[1] = 61;
        if (difference == 2)
            independentOff[0] = 0x81;
        const unsigned source = difference == 0 ? 202 : 101;
        require(bank.enqueue(16, 0, source, independentOff),
                "Other source, key or channel release is not a false inversion");
        capture.consume(bank.drain());
        for (int start = 320; start <= 512; start += 64)
        {
            bank.beginBlock(start, 64, false);
            if (start == 448)
                require(bank.enqueue(17, 0, 101, off), "Queue matching release465 after attack464");
            const auto batch = bank.drain();
            require(!batch.allNotesOff, "Independent MIDI identity never triggers crossing panic");
            capture.consume(batch);
        }
        require(bank.observation().failures == 0 && capture.size == 3 && capture.events[0].due == 272 &&
                    capture.events[0].source == source && capture.events[0].bytes == independentOff &&
                    capture.events[1].due == 464 && capture.events[1].bytes == on &&
                    capture.events[2].due == 465 && capture.events[2].bytes == off,
                "Independent releases and original exact attack/release retain identity and deadlines");
    }
    {
        MidiCompensationHistory bank(configuration(4, 1));
        MidiHistoryBatch last;
        for (int block = 0; block < 16384; ++block)
        {
            bank.beginBlock(std::int64_t(block) * 64, 64, false);
            for (unsigned frame = 0; frame < 4; ++frame)
                bank.enqueue(frame, 0, 101, on);
            last = bank.drain();
        }
        require(last.events.empty() && last.allNotesOff &&
                    (bank.observation().failures & std::uint32_t(MidiHistoryFailure::gateCapacity)),
                "Actual fixed gate-counter exhaustion triggers panic before output instead of wrapping");
    }
}
