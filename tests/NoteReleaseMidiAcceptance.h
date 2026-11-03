// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MidiRecording.h"
#include "../src/MidiScoreApplication.h"
#include <limits>

namespace note_release_midi_acceptance
{
using namespace vibera;
struct Event
{
    unsigned tick = 0;
    int status = 0, pitch = 0, velocity = 0;
};
// Independent byte decoder for exported channel events. No readMidiScore or
// JUCE note-pairing helpers are used to assert release bytes or Repeat timing.
inline std::vector<Event> channelEvents(const juce::MemoryBlock &bytes)
{
    const auto *data = static_cast<const unsigned char *>(bytes.getData());
    const auto size = bytes.getSize();
    auto word = [&](size_t p)
    {
        require(p + 2 <= size, "Release SMF word is in bounds");
        return unsigned(data[p]) * 256 + data[p + 1];
    };
    auto dword = [&](size_t p) { return (std::uint64_t(word(p)) << 16) | word(p + 2); };
    require(size >= 22 && std::memcmp(data, "MThd", 4) == 0 && dword(4) == 6 && word(12) == 960,
            "Release export uses literal960 PPQ SMF header");
    std::vector<Event> result;
    size_t p = 14;
    for (unsigned track = 0; track < word(10); ++track)
    {
        require(p + 8 <= size && std::memcmp(data + p, "MTrk", 4) == 0,
                "Release export has bounded track chunks");
        const auto length = dword(p + 4);
        p += 8;
        require(length <= size - p, "Release export track extent is bounded");
        const auto end = p + size_t(length);
        unsigned tick = 0, running = 0;
        auto variable = [&]()
        {
            unsigned value = 0;
            for (int i = 0; i < 4; ++i)
            {
                require(p < end, "Release export variable integer is bounded");
                const auto b = data[p++];
                value = (value << 7) | (b & 127);
                if (!(b & 128))
                    return value;
            }
            require(false, "Release export variable integer terminates");
            return 0u;
        };
        while (p < end)
        {
            tick += variable();
            require(p < end, "Release export has event status");
            unsigned status = data[p] & 128 ? data[p++] : running;
            if (status == 0xff || status == 0xf0 || status == 0xf7)
            {
                running = 0;
                if (status == 0xff)
                {
                    require(p < end, "Release export meta type is bounded");
                    ++p;
                }
                const auto count = variable();
                require(count <= end - p, "Release export metadata is bounded");
                p += count;
                continue;
            }
            require(status >= 0x80 && status < 0xf0, "Release export channel status is valid");
            running = status;
            const auto count = (status & 0xe0) == 0xc0 ? 1u : 2u;
            require(count <= end - p, "Release export MIDI data is bounded");
            const int first = data[p++];
            const int second = count == 2 ? data[p++] : 0;
            if ((status & 0xf0) == 0x80 || (status & 0xf0) == 0x90)
                result.push_back({tick, int(status), first, second});
        }
    }
    require(p == size, "Release export has no trailing bytes");
    return result;
}
inline juce::MemoryBlock importFixture()
{
    // Two overlapping same-pitch notes pair FIFO; explicit releases64/127.
    // A distinct pitch uses NoteOn0 as off. All deltas below are literal ticks.
    const unsigned char bytes[]{'M',  'T',  'h', 'd', 0,    0,    0,  6,  0,    0,    0,    1,   1,
                                0xe0, 'M',  'T', 'r', 'k',  0,    0,  0,  28,   0,    0x90, 60,  100,
                                120,  0x90, 60,  80,  120,  0x80, 60, 64, 120,  0x80, 60,   127, 0,
                                0x90, 61,   90,  120, 0x90, 61,   0,  0,  0xff, 0x2f, 0};
    return {bytes, sizeof(bytes)};
}
inline void tests(const juce::File &folder)
{
    require(Note{}.releaseVelocity == 0, "Release MIDI legacy Note default is neutral zero");
    {
        MidiTake take;
        take.begin(1, 0, 0, 60, 0);
        take.add({7, 0, 1, 1, {0x90, 60, 100}});
        take.add({7, .25, 1, 1, {0x80, 60, 64}});
        take.add({7, .5, 1, 1, {0x90, 60, 80}});
        take.add({7, .75, 1, 1, {0x80, 60, 127}});
        take.add({7, 1, 1, 1, {0x90, 60, 90}});
        take.add({7, 1.25, 1, 1, {0x90, 60, 0}});
        const auto notes = take.finish(2);
        require(notes.size() == 3 && notes[0].releaseVelocity == 64. / 127 && notes[1].releaseVelocity == 1 &&
                    notes[2].releaseVelocity == 0 && notes[0].beat == 0 && notes[1].beat == .5 &&
                    notes[2].beat == 1 && notes[0].length == .25 && notes[1].length == .25 &&
                    notes[2].length == .25,
                "Captured physical release64/127 and NoteOn0 retain exact same-pitch timing");
    }
    {
        MidiTake take;
        take.begin(2, 0, 3.75, 60, 4);
        take.add({8, 0, 2, 1, {0x90, 64, 100}});
        const auto preview = take.preview(.5);
        require(preview.size() == 2 && preview[0].releaseVelocity == 0 && preview[1].releaseVelocity == 0 &&
                    take.heldCount() == 1 && take.size() == 0,
                "Synthetic preview closes are neutral and preserve the physical held owner");
        take.add({8, .5, 2, 1, {0x80, 64, 127}});
        const auto notes = take.finish(.5);
        require(notes.size() == 2 && notes[0].beat == 3.75 && notes[0].length == .25 &&
                    notes[0].releaseVelocity == 0 && notes[1].beat == 0 && notes[1].length == .25 &&
                    notes[1].releaseVelocity == 1,
                "Loop split retains actual release only on final physical-off segment");
    }
    {
        MidiTake take;
        take.begin(3, 0, 0, 60, 0);
        take.add({9, 0, 3, 1, {0x90, 67, 100}});
        take.add({9, .25, 3, 1, {0x90, 67, 80}}); // Forced retrigger close.
        take.add({9, .5, 3, 1, {0x80, 67, 64}});
        take.add({9, .75, 3, 1, {0x90, 69, 100}});
        take.interrupt(9, 1);
        take.add({9, 1.25, 3, 1, {0x90, 71, 100}});
        const auto notes = take.finish(1.5);
        require(notes.size() == 4 && notes[0].releaseVelocity == 0 && notes[1].releaseVelocity == 64. / 127 &&
                    notes[2].releaseVelocity == 0 && notes[3].releaseVelocity == 0,
                "Retrigger, interrupt and seal closes are neutral; physical off retains its value");
    }
    {
        MidiTake take;
        take.begin(4, 0, 0, 60, 0);
        take.add({10, 0, 4, 1, {0x90, 60, 100}, 0});
        take.add({10, .1, 4, 1, {0x90, 60, 80}, 1});
        take.add({10, .2, 4, 1, {0x80, 60, 64}, 0});
        take.add({10, .3, 4, 1, {0x80, 60, 127}, 1});
        const auto notes = take.finish(.3);
        require(notes.size() == 2 && notes[0].releaseVelocity == 64. / 127 && notes[1].releaseVelocity == 1 &&
                    notes[0].beat == 0 && notes[1].beat == .1,
                "Capture source owners retain independent same-pitch release values");
    }
    {
        MidiTake take;
        take.begin(5, 0, 3.75, 60, 4);
        for (size_t i = 0; i < MidiTake::maximumNotes - 1; ++i)
        {
            const double time = double(i) * .000001;
            take.add({11, time, 5, 1, {0x90, 72, 100}});
            take.add({11, time + .0000005, 5, 1, {0x80, 72, 64}});
        }
        take.add({11, .02, 5, 1, {0x90, 72, 100}});
        take.add({11, .5, 5, 1, {0x80, 72, 127}});
        require(take.failureCode() == MidiTake::Failure::noteQuota &&
                    take.finishedNotes().size() == MidiTake::maximumNotes - 1 && take.heldCount() == 1 &&
                    take.finishedNotes().back().releaseVelocity == 64. / 127 &&
                    take.releaseFinishedNotes().empty(),
                "Two-segment quota failure preserves all complete notes and unclosed physical owner");
    }
    MidiScore score;
    auto bytes = importFixture();
    require(readMidiScore(bytes, {}, score).wasOk() && score.notes.size() == 3 && score.notes[0].beat == 0 &&
                score.notes[1].beat == .25 && score.notes[0].length == .5 && score.notes[1].length == .5 &&
                score.notes[0].releaseVelocity == 64. / 127 && score.notes[1].releaseVelocity == 1 &&
                score.notes[2].releaseVelocity == 0,
            "Literal SMF import preserves FIFO release64/127 and NoteOn0 neutral release");
    Controller controller;
    controller.replace(Project::empty());
    const auto pattern = controller.project.patterns.front().id;
    const auto channel = controller.project.channels.front().id;
    const auto before = controller.project.state();
    require(applyMidiScore(controller, pattern, channel, score, false).wasOk(),
            "Release import uses canonical score application");
    const auto after = controller.project.state();
    const auto &applied = controller.project.pattern(pattern)->notes;
    require(applied.size() == 3 && applied[0].id != 0 && applied[0].id != applied[1].id &&
                applied[0].channel == channel && applied[0].releaseVelocity == 64. / 127 &&
                applied[1].releaseVelocity == 1 && applied[2].releaseVelocity == 0,
            "Release import assigns fresh IDs/owner while retaining all decoded release values");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "Release MIDI import exact Undo");
    controller.redoEdit();
    require(controller.project.state().isEquivalentTo(after), "Release MIDI import exact Redo");
    juce::MemoryBlock exported;
    require(writeMidiScore(*controller.project.pattern(pattern), channel, 60, 4, 4, exported).wasOk(),
            "Release MIDI import/export round trip encodes");
    MidiScore roundTrip;
    require(readMidiScore(exported, {}, roundTrip).wasOk() && roundTrip.notes.size() == 3 &&
                roundTrip.notes[0].releaseVelocity == 64. / 127 && roundTrip.notes[1].releaseVelocity == 1 &&
                roundTrip.notes[2].releaseVelocity == 0,
            "Release MIDI import/export round trip retains exact quantized values");
    std::vector<int> offValues;
    for (const auto &event : channelEvents(exported))
        if (event.status == 0x80)
            offValues.push_back(event.velocity);
    require(offValues == std::vector<int>{64, 127, 0},
            "Independent export bytes preserve explicit releases and legacy neutral zero");
    const auto file = folder.getChildFile("note-release-roundtrip.mid");
    require(file.replaceWithData(exported.getData(), exported.getSize()), "Release MIDI fixture file saves");
    const auto savedBytes = exported;
    juce::MemoryBlock reopened;
    require(file.loadFileAsData(reopened) && reopened == exported &&
                readMidiScore(reopened, {}, roundTrip).wasOk() && roundTrip.notes[1].releaseVelocity == 1,
            "Release MIDI actual file reopen retains release value");
    auto malformed = bytes;
    malformed.setSize(malformed.getSize() - 1);
    const auto badFile = folder.getChildFile("note-release-malformed.mid");
    juce::MemoryBlock malformedReopened;
    require(badFile.replaceWithData(malformed.getData(), malformed.getSize()) &&
                badFile.loadFileAsData(malformedReopened) && malformedReopened == malformed &&
                readMidiScore(malformedReopened, {}, roundTrip).failed() && roundTrip.notes.size() == 3 &&
                roundTrip.notes[1].releaseVelocity == 1 && controller.project.state().isEquivalentTo(after),
            "Malformed actual file decode preserves previous score and canonical source");
    auto invalidScore = score;
    invalidScore.notes.front().releaseVelocity = 1.01;
    const auto allocator = controller.project.nextId;
    require(applyMidiScore(controller, pattern, channel, invalidScore, false).failed() &&
                controller.project.state().isEquivalentTo(after) && controller.project.nextId == allocator,
            "Invalid release import refuses replacement without source deletion or ID consumption");
    for (int mode : {10, 13, 14})
    {
        auto part = *controller.project.pattern(pattern);
        part.notes.resize(1);
        auto &note = part.notes.front();
        note.beat = 0;
        note.length = .25;
        note.repeatMode = mode;
        note.releaseVelocity = .5;
        note.pan = .5;
        note.fineCents = 20;
        const auto originalId = note.id;
        const int count = mode == 10 ? 2 : mode == 13 ? 4 : 6;
        const unsigned interval = mode == 10 ? 120 : mode == 13 ? 60 : 40;
        int omitted = -1;
        require(writeMidiScore(part, channel, 60, 4, 4, exported, &omitted).wasOk() && omitted == count,
                "Release Repeat export retains existing Pan/Fine omission accounting");
        const auto events = channelEvents(exported);
        require(events.size() == size_t(count * 2), "Release Repeat has literal2/4/6 MIDI trigger counts");
        for (int i = 0; i < count; ++i)
        {
            const auto &on = events[size_t(i * 2)];
            const auto &off = events[size_t(i * 2 + 1)];
            require(on.status == 0x90 && on.tick == unsigned(i) * interval && off.status == 0x80 &&
                        off.tick == unsigned(i + 1) * interval && off.pitch == note.pitch &&
                        off.velocity == 64,
                    "Independent MIDI bytes prove rounded half release64 and equal-time off-before-on");
        }
        require(note.id == originalId && note.beat == 0 && note.length == .25 && note.repeatMode == mode,
                "Release Repeat export does not bake copies or mutate source identity/geometry");
        note.shiftPercent = 50;
        note.releaseVelocity = 1;
        require(writeMidiScore(part, channel, 60, 4, 4, exported).wasOk(), "Release Shift/Repeat export");
        const auto shifted = channelEvents(exported);
        require(shifted.front().tick == 119 && shifted.back().tick == 359 && shifted.back().velocity == 127,
                "Release export preserves literal Shift50 timing119..359 and release127");
    }
    auto invalid = *controller.project.pattern(pattern);
    const auto retainedBytes = exported;
    for (double value :
         {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        invalid.notes.front().releaseVelocity = value;
        int omitted = 43;
        require(writeMidiScore(invalid, channel, 60, 4, 4, exported, &omitted).failed() &&
                    exported == retainedBytes && omitted == 43 &&
                    controller.project.state().isEquivalentTo(after),
                "Invalid release MIDI export preserves prior output, metadata and source");
    }
    juce::MemoryBlock finalReopened;
    require(file.loadFileAsData(finalReopened) && finalReopened == savedBytes &&
                readMidiScore(finalReopened, {}, roundTrip).wasOk() &&
                roundTrip.notes[1].releaseVelocity == 1,
            "Failed encoding leaves prior actual successful fixture file intact");
}
} // namespace note_release_midi_acceptance

inline void noteReleaseMidiTests(const juce::File &folder)
{
    note_release_midi_acceptance::tests(folder);
}
