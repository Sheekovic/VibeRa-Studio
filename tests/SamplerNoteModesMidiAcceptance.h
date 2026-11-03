// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveMidi.h"
#include "../src/MidiScore.h"
#include "../src/MidiNoteOwners.h"
#include <tuple>

// Include after require() and existing callback allocation/destruction probes.
// Original MIDI byte/control fixtures: no hardware/provider/audio-response claim.
namespace sampler_note_modes_midi_acceptance
{
using namespace vibera;
constexpr double rate = 48000;
inline LiveNote note(Id id, int begin, int end, int group, int pitch = 60, int release = 0)
{
    LiveNote n;
    n.note = id;
    n.start = begin / rate;
    n.end = end / rate;
    n.pitch = pitch;
    n.velocity = 1;
    n.colourGroup = group;
    n.noteOffVelocity = std::uint8_t(release);
    return n;
}
inline LiveDocument document(std::vector<LiveNote> notes, std::uint64_t generation = 1)
{
    LiveDocument doc;
    doc.generation = generation;
    LiveChannel lane;
    lane.id = 1;
    lane.sampleInstrument = false;
    lane.notes = std::move(notes);
    std::sort(lane.notes.begin(), lane.notes.end(),
              [](const auto &a, const auto &b)
              {
                  return std::tuple{a.placement, a.note, a.repeatOrdinal} <
                         std::tuple{b.placement, b.note, b.repeatOrdinal};
              });
    for (size_t i = 0; i < lane.notes.size(); ++i)
    {
        lane.events.push_back({lane.notes[i].start, std::uint32_t(i), true});
        lane.events.push_back({lane.notes[i].end, std::uint32_t(i), false});
    }
    std::sort(lane.events.begin(), lane.events.end(), [](const auto &a, const auto &b)
              { return std::tie(a.time, a.on, a.index) < std::tie(b.time, b.on, b.index); });
    doc.channels.push_back(std::move(lane));
    return doc;
}
inline int process(LiveMidi &m, const LiveDocument &d, int begin, int frames = 256, bool playing = true)
{
    const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
    const bool previous = callbackAllocationProbe;
    callbackAllocationProbe = true;
    const int count = m.process(d, 1, begin / rate, frames, rate, playing);
    callbackAllocationProbe = previous;
    require(callbackAllocations.load() == allocated && callbackDestructions.load() == destroyed,
            "Colour-group live MIDI process allocates/destroys no measured C++ objects");
    return count;
}
inline void event(const LiveMidi &m, int index, int frame, int status, int pitch, int velocity)
{
    const auto &e = m.events[size_t(index)];
    require(e.frame == frame && e.size == 3 && e.bytes[0] == status && e.bytes[1] == pitch &&
                e.bytes[2] == velocity,
            "Literal colour-group MIDI frame/status/pitch/velocity matches");
}
inline void live()
{
    {
        auto doc = document({note(1, 0, 384, 0, 60, 31), note(2, 64, 448, 15, 60, 99)});
        LiveMidi midi;
        require(process(midi, doc, 0) == 2, "Same-pitch different-channel notes both begin");
        event(midi, 0, 0, 0x90, 60, 127);
        event(midi, 1, 64, 0x9f, 60, 127);
        require(process(midi, doc, 256) == 2, "Same-pitch different-channel gates release independently");
        event(midi, 0, 128, 0x80, 60, 31);
        event(midi, 1, 192, 0x8f, 60, 99);
        require(midi.overflow == 0, "Ordinary multichannel gates have no packet overflow");
    }
    {
        auto wet = note(1, 0, 768, 2, 60, 64);
        auto first = document({wet});
        LiveMidi midi;
        require(process(midi, first, 0) == 1, "Recolour fixture starts captured channel3");
        event(midi, 0, 0, 0x92, 60, 127);
        wet.colourGroup = 10;
        auto recoloured = document({wet}, 2);
        require(process(midi, recoloured, 256) == 2,
                "Live recolour releases old gate then chases new channel");
        event(midi, 0, 0, 0x82, 60, 0);
        event(midi, 1, 0, 0x9a, 60, 127);
        require(process(midi, recoloured, 512, 256, false) == 1,
                "Stop releases the captured recoloured channel");
        event(midi, 0, 0, 0x8a, 60, 0);
        require(process(midi, recoloured, 512) == 1,
                "Restart chases the actual ongoing source on its channel");
        require(process(midi, recoloured, 2048) == 1,
                "Jump releases old channel without chasing expired source");
        event(midi, 0, 0, 0x8a, 60, 0);
    }
    {
        auto control = note(1, 0, 768, 7, 65);
        control.slide = true;
        auto ordinary = note(2, 32, 320, 3, 60, 127);
        ordinary.portamento = true;
        auto doc = document({control, ordinary});
        LiveMidi midi;
        require(process(midi, doc, 0) == 1,
                "Slide has no note-on; ordinary Porta has one unconverted note-on");
        event(midi, 0, 32, 0x93, 60, 127);
        require(process(midi, doc, 256) == 1, "Slide has no phantom note-off or Porta CC conversion");
        event(midi, 0, 64, 0x83, 60, 127);
        require(process(midi, doc, 512) == 0, "Ongoing Slide chase stays silent for unsupported hosted MIDI");
        auto normal = note(5, 0, 1536, 4);
        auto a = document({normal}, 3);
        require(process(midi, a, 768) == 1, "Ordinary source can be chased after a generation change");
        normal.slide = true;
        auto b = document({normal}, 4);
        require(process(midi, b, 1024) == 1, "Live conversion to Slide releases an existing ordinary gate");
        event(midi, 0, 0, 0x84, 60, 0);
    }
    {
        std::vector<LiveNote> notes;
        for (int i = 0; i < 129; ++i)
            notes.push_back(note(i + 1, 0, 1024, (i + 5) % 16, 48 + i % 36));
        notes.back().colourGroup = 9;
        auto doc = document(std::move(notes));
        LiveMidi midi;
        require(process(midi, doc, 0) == 130,
                "128-slot steal emits one off and129 starts within packet capacity");
        event(midi, 128, 0, 0x85, 48, 0);
        event(midi, 129, 0, 0x99, 68, 127);
        require(process(midi, doc, 256, 256, false) == 128,
                "Stop releases every retained multichannel gate once");
        require(midi.overflow == 0, "Finite gate stealing does not fabricate packet overflow");
    }
    {
        std::vector<LiveNote> notes;
        for (int i = 0; i < 600; ++i)
            notes.push_back(note(i + 1, 0, 1024, i % 16, 48 + i % 48));
        auto doc = document(std::move(notes));
        LiveMidi midi;
        require(process(midi, doc, 0) == ipc::events && midi.overflow > 0,
                "Extreme notes truthfully exhaust fixed512 events");
        unsigned channels = 0;
        for (int i = 0; i < 16; ++i)
        {
            const auto &e = midi.events[size_t(ipc::events - 16 + i)];
            require(e.frame == 255 && e.size == 3 && (e.bytes[0] & 0xf0) == 0xb0 && e.bytes[1] == 123 &&
                        e.bytes[2] == 0,
                    "Fixed panic tail contains16 real channel all-notes-off messages");
            channels |= 1u << (e.bytes[0] & 15);
        }
        require(channels == 65535 && midi.events.back().bytes[0] == 0xb0,
                "Panic covers all16 channels and preserves original final channel1 panic oracle");
        MidiNoteOwners owners;
        const ipc::MidiEvent held{0, {0x96, 72, 100}, 3};
        owners.process(1, held, [](const ipc::MidiEvent &) noexcept {});
        bool unrelatedOff = false, unrelatedPanic = false;
        for (const auto &e : midi.events)
            owners.process(0, e,
                           [&](const ipc::MidiEvent &r) noexcept
                           {
                               unrelatedOff = unrelatedOff || ((r.bytes[0] & 0xf0) == 0x80 &&
                                                               (r.bytes[0] & 15) == 6 && r.bytes[1] == 72);
                               unrelatedPanic = unrelatedPanic || r.bytes[0] == 0xb6;
                           });
        require(!unrelatedOff && !unrelatedPanic,
                "Score-lane panic retains unrelated input ownership on channel7");
        int released = 0;
        owners.process(1, ipc::MidiEvent{0, {0x86, 72, 23}, 3},
                       [&](const ipc::MidiEvent &e) noexcept
                       {
                           if (e.bytes[0] == 0x86 && e.bytes[1] == 72 && e.bytes[2] == 23)
                               ++released;
                       });
        require(released == 1,
                "Unrelated held input retains its own exact final release after score overflow");
        const auto overflow = midi.overflow;
        auto next = document({note(900, 256, 768, 5)}, 2);
        require(process(midi, next, 256) == 1 && midi.overflow == overflow,
                "Valid next block recovers after panic with no latch or repeated phase origin");
        event(midi, 0, 0, 0x95, 60, 127);
    }
}
inline void score()
{
    auto project = Project::empty();
    auto &part = project.patterns.front();
    part.autoLength = false;
    part.length = 8;
    part.notes.clear();
    const auto channel = project.channels.front().id;
    Note first{project.allocate(), channel, .5, .25, 60, 1, false};
    first.colourGroup = 0;
    first.shiftPercent = 50;
    first.repeatMode = 10;
    first.releaseVelocity = .5;
    first.pan = .25;
    first.fineCents = 20;
    first.modX = .75;
    Note second{project.allocate(), channel, .65, .5, 60, 1, false};
    second.colourGroup = 15;
    second.releaseVelocity = 1;
    second.portamento = true;
    Note slide{project.allocate(), channel, 2, .5, 65, 0, false};
    slide.colourGroup = 7;
    slide.slide = true;
    slide.portamento = true;
    part.notes = {first, second, slide};
    const auto original = project.state();
    juce::MemoryBlock bytes;
    int omitted = -1;
    MidiScoreOmissions detail;
    require(writeMidiScore(part, channel, 120, 4, 4, bytes, &omitted, &detail).wasOk() && omitted == 4 &&
                detail == MidiScoreOmissions{2, 1, 2},
            "MIDI omission union and overlapping expression/Slide/Porta categories are truthful");
    juce::MemoryInputStream stream(bytes, false);
    juce::MidiFile file;
    require(file.readFrom(stream, false) && file.getTimeFormat() == 960 && file.getNumTracks() == 1,
            "Colour score writes a literal960PPQ single-track MIDI file");
    int ons = 0, offs = 0, controls = 0;
    bool group1 = false, group16 = false, shift599 = false, release64 = false, release127 = false;
    for (int i = 0; i < file.getTrack(0)->getNumEvents(); ++i)
    {
        const auto &m = file.getTrack(0)->getEventPointer(i)->message;
        if (m.isNoteOn())
        {
            ++ons;
            group1 |= m.getChannel() == 1;
            group16 |= m.getChannel() == 16;
            shift599 |= m.getChannel() == 1 && m.getTimeStamp() == 599;
            require(m.getNoteNumber() == 60, "Slide emits no phantom MIDI pitch65");
        }
        if (m.isNoteOff())
        {
            ++offs;
            release64 |= m.getChannel() == 1 && m.getVelocity() == 64;
            release127 |= m.getChannel() == 16 && m.getVelocity() == 127;
        }
        controls += m.isController() ? 1 : 0;
    }
    require(ons == 3 && offs == 3 && controls == 0 && group1 && group16 && shift599 && release64 &&
                release127,
            "MIDI channel mapping, literal Shift+Repeat timing and release bytes preserve ordinary notes "
            "without fake Porta CC");
    MidiScore imported;
    require(readMidiScore(bytes, {}, imported).wasOk() && imported.notes.size() == 3,
            "Different-channel same-pitch overlaps decode as independent notes");
    int channel1 = 0, channel16 = 0;
    for (const auto &n : imported.notes)
    {
        channel1 += n.colourGroup == 0;
        channel16 += n.colourGroup == 15;
        require(!n.slide && !n.portamento && n.pan == 0 && n.fineCents == 0 && n.modX == .5 && n.modY == .5 &&
                    n.shiftPercent == 0 && n.repeatMode == 0,
                "MIDI import does not invent omitted flags/expression or double-apply baked timing");
    }
    require(
        channel1 == 2 && channel16 == 1 && project.state().isEquivalentTo(original),
        "MIDI source channels1/16 roundtrip as groups0/15 without mutating any original expression/identity");
    MidiScore filtered;
    require(readMidiScore(bytes, {-1, 16, false}, filtered).wasOk() && filtered.notes.size() == 1 &&
                filtered.notes.front().colourGroup == 15 && filtered.notes.front().releaseVelocity == 1,
            "Selected MIDI channel16 retains group15 and literal release127");
    juce::MemoryBlock again;
    Pattern decoded = part;
    decoded.notes = imported.notes;
    for (auto &n : decoded.notes)
        n.channel = channel;
    require(writeMidiScore(decoded, channel, 120, 4, 4, again).wasOk() &&
                readMidiScore(again, {}, filtered).wasOk() && filtered.notes.size() == 3,
            "Re-export/import retains mapped channels without duplicating source repeats");
    const auto retained = bytes;
    const MidiScoreOmissions sentinel{17, 18, 19};
    for (int colour : {-1, 16})
    {
        auto bad = part;
        bad.notes.front().colourGroup = colour;
        omitted = 43;
        detail = sentinel;
        require(writeMidiScore(bad, channel, 120, 4, 4, bytes, &omitted, &detail).failed() &&
                    bytes == retained && omitted == 43 && detail == sentinel,
                "Invalid MIDI colour refuses atomically without replacing bytes or omission receipts");
    }
    auto bad = part;
    bad.notes.back().repeatMode = 10;
    detail = sentinel;
    omitted = 43;
    require(writeMidiScore(bad, channel, 120, 4, 4, bytes, &omitted, &detail).failed() && bytes == retained &&
                detail == sentinel && omitted == 43,
            "Unsupported Slide+Repeat refuses rather than emitting phantom repeats");
    auto silent = part;
    silent.notes = {slide};
    omitted = -1;
    detail = {};
    require(writeMidiScore(silent, channel, 120, 4, 4, bytes, &omitted, &detail).wasOk() && omitted == 1 &&
                detail.slideNotes == 1 && detail.portamentoNotes == 1,
            "Slide-only source export truthfully completes an empty score with explicit omissions");
    const auto retainedImport = imported.notes.size();
    require(readMidiScore(bytes, {}, imported).failed() && imported.notes.size() == retainedImport,
            "Silent Slide export cannot import phantom notes or overwrite prior decoded score on failure");
    auto compiled = compileLiveDocument(project, true, part.id, 77);
    require(compiled && compiled->find(channel) && compiled->find(channel)->notes.size() == 4,
            "Actual score preparation retains two Repeat gates plus ordinary and Slide occurrences");
    bool sawSlide = false, sawPorta = false, saw16 = false;
    for (const auto &n : compiled->find(channel)->notes)
    {
        sawSlide |= n.slide;
        sawPorta |= n.portamento;
        saw16 |= n.colourGroup == 15;
    }
    require(sawSlide && sawPorta && saw16,
            "Prepared LiveNote carries actual Slide/Porta/colour metadata without source mutation");
}
inline void tests()
{
    live();
    score();
}
} // namespace sampler_note_modes_midi_acceptance
