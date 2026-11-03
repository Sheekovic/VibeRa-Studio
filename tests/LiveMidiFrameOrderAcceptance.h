// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveMidi.h"
#include "../src/MidiNoteOwners.h"
#include <tuple>

// Include after require and the existing callback allocation/destruction probes.
namespace live_midi_frame_order_acceptance
{
using namespace vibera;
struct Event
{
    std::int64_t frame = 0;
    int status = 0, pitch = 0, velocity = 0;
    bool operator==(const Event &) const = default;
};
struct Capture
{
    std::array<Event, 128> raw{}, routed{};
    size_t rawCount = 0, routedCount = 0;
    bool overflow = false;
};
inline LiveNote note(Id id, double first, double last, int pitch, int release, double rate, double origin = 0)
{
    LiveNote n;
    n.note = id;
    n.start = origin + first / rate;
    n.end = origin + last / rate;
    n.pitch = pitch;
    n.velocity = 1;
    n.noteOffVelocity = std::uint8_t(release);
    return n;
}
inline LiveDocument document(std::vector<LiveNote> notes, std::uint64_t generation = 1)
{
    LiveDocument doc;
    doc.generation = generation;
    LiveChannel lane;
    lane.id = 1;
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
inline void drive(LiveMidi &midi, MidiNoteOwners &owners, Capture &capture, const LiveDocument &doc,
                  Id channel, std::int64_t begin, int frames, double rate, double origin = 0,
                  bool playing = true)
{
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    const bool previousProbe = callbackAllocationProbe;
    callbackAllocationProbe = true;
    const int count = midi.process(doc, channel, origin + double(begin) / rate, frames, rate, playing);
    for (int i = 0; i < count; ++i)
    {
        const auto &e = midi.events[size_t(i)];
        const Event observed{begin + e.frame, e.bytes[0], e.bytes[1], e.bytes[2]};
        if (capture.rawCount == capture.raw.size())
            capture.overflow = true;
        else
            capture.raw[capture.rawCount++] = observed;
        owners.process(0, e,
                       [&](const ipc::MidiEvent &routed) noexcept
                       {
                           if (capture.routedCount == capture.routed.size())
                               capture.overflow = true;
                           else
                               capture.routed[capture.routedCount++] = {begin + routed.frame, routed.bytes[0],
                                                                        routed.bytes[1], routed.bytes[2]};
                       });
    }
    callbackAllocationProbe = previousProbe;
    require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions &&
                !capture.overflow && midi.overflow == 0,
            "Realized MIDI grouping and ownership allocate/destroy no measured C++ objects or overflow");
}
inline void exact(const Capture &capture, std::initializer_list<Event> raw,
                  std::initializer_list<Event> routed)
{
    require(capture.rawCount == raw.size() && std::equal(raw.begin(), raw.end(), capture.raw.begin()),
            "Exact literal raw MIDI sample/status/pitch/velocity order");
    require(capture.routedCount == routed.size() &&
                std::equal(routed.begin(), routed.end(), capture.routed.begin()),
            "Exact literal owned MIDI sample/status/pitch/velocity order");
}
inline void tests()
{
    // Actual prepared Repeat source reproduces the proven7096 boundary:
    // source doubles sort the next on before the previous off by tiny fractions.
    for (bool oddPartitions : {false, true})
    {
        auto project = Project::empty();
        auto &part = project.patterns.front();
        part.autoLength = false;
        part.length = 1;
        const auto channel = project.channels.front().id;
        Note n;
        n.id = project.allocate();
        n.channel = channel;
        n.beat = 4096. / 24000.;
        n.length = .25;
        n.pitch = 60;
        n.velocity = 1;
        n.repeatMode = 14;
        n.releaseVelocity = 64. / 127;
        part.notes = {n};
        const auto before = project.state();
        const auto doc = compileLiveDocument(project, true, part.id, 1);
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        constexpr std::array<int, 4> odd{127, 1, 255, 1};
        int begin = 0, calls = 0;
        while (begin < 10496)
        {
            const int span = std::min(10496 - begin, oddPartitions ? odd[size_t(calls++ % 4)] : 256);
            drive(midi, owners, capture, *doc, channel, begin, span, 48000);
            begin += span;
        }
        require(capture.rawCount == 12 && capture.routedCount == 12,
                "Repeat14 retains all six natural offs through ownership, including7096");
        for (int ordinal = 0; ordinal < 6; ++ordinal)
        {
            const Event on{4096 + ordinal * 1000, 0x90, 60, 127};
            const Event off{5096 + ordinal * 1000, 0x80, 60, 64};
            require(capture.raw[size_t(ordinal * 2)] == on && capture.raw[size_t(ordinal * 2 + 1)] == off &&
                        capture.routed[size_t(ordinal * 2)] == on &&
                        capture.routed[size_t(ordinal * 2 + 1)] == off,
                    "Literal Repeat off-before-next-on is independent of callback subdivision");
        }
        const auto count = capture.rawCount;
        drive(midi, owners, capture, *doc, channel, begin, 1, 48000, 0, false);
        require(capture.rawCount == count && project.state().isEquivalentTo(before),
                "All Repeat gates retire; no forced stop off or source identity/timing mutation");
    }
    for (double rate : {44100., 48000., 96000.})
    {
        // Late fractional off/start rounds into next callback, not out of it.
        // The short gate61 starts AND ends at256 and must not remain sounding.
        const auto doc = document({note(1, 0, 255.75, 60, 64, rate), note(2, 255.75, 256.25, 61, 32, rate),
                                   note(3, 256, 512.25, 62, 127, rate)});
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        drive(midi, owners, capture, doc, 1, 0, 256, rate);
        drive(midi, owners, capture, doc, 1, 256, 1, rate);
        drive(midi, owners, capture, doc, 1, 257, 255, rate);
        drive(midi, owners, capture, doc, 1, 512, 1, rate);
        drive(midi, owners, capture, doc, 1, 513, 1, rate, 0, false);
        const std::initializer_list<Event> expected{{0, 0x90, 60, 127},   {256, 0x80, 60, 64},
                                                    {256, 0x90, 61, 127}, {256, 0x90, 62, 127},
                                                    {256, 0x80, 61, 32},  {512, 0x80, 62, 127}};
        exact(capture, expected, expected);
    }
    {
        // Existing genuine overlaps retain score ownership. The contained
        // collapsed gate is balanced without releasing either longer source.
        const auto doc = document({note(1, 0, 100, 60, 64, 48000), note(2, 50, 150, 60, 127, 48000),
                                   note(3, 75.1, 75.2, 60, 32, 48000)});
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        drive(midi, owners, capture, doc, 1, 0, 256, 48000);
        drive(midi, owners, capture, doc, 1, 256, 1, 48000, 0, false);
        exact(capture,
              {{0, 0x90, 60, 127},
               {50, 0x90, 60, 127},
               {75, 0x90, 60, 127},
               {75, 0x80, 60, 32},
               {100, 0x80, 60, 64},
               {150, 0x80, 60, 127}},
              {{0, 0x90, 60, 127}, {50, 0x90, 60, 127}, {75, 0x90, 60, 127}, {150, 0x80, 60, 127}});
    }
    {
        // Exact half samples use nearest-even on a stable origin, including
        // one-frame/odd callbacks. No boundary may replay a complete short gate.
        const auto doc = document({note(1, .49, .5, 60, 32, 48000), note(2, .5, 1.5, 61, 64, 48000),
                                   note(3, 1.5, 1.6, 62, 127, 48000)});
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        for (int begin = 0; begin < 5; ++begin)
            drive(midi, owners, capture, doc, 1, begin, 1, 48000);
        drive(midi, owners, capture, doc, 1, 5, 1, 48000, 0, false);
        const std::initializer_list<Event> expected{{0, 0x90, 60, 127}, {0, 0x90, 61, 127},
                                                    {0, 0x80, 60, 32},  {2, 0x80, 61, 64},
                                                    {2, 0x90, 62, 127}, {2, 0x80, 62, 127}};
        exact(capture, expected, expected);
    }
    {
        // Publication must not force-close an off whose raw time is just before
        // the new callback but whose realized wire frame belongs to that call.
        auto doc = document({note(1, 0, 255.75, 60, 64, 48000)});
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        drive(midi, owners, capture, doc, 1, 0, 256, 48000);
        doc = document({note(1, 0, 255.75, 60, 127, 48000), note(2, 255.75, 300, 60, 32, 48000)}, 2);
        drive(midi, owners, capture, doc, 1, 256, 128, 48000);
        drive(midi, owners, capture, doc, 1, 384, 1, 48000, 0, false);
        const std::initializer_list<Event> expected{
            {0, 0x90, 60, 127}, {256, 0x80, 60, 127}, {256, 0x90, 60, 127}, {300, 0x80, 60, 32}};
        exact(capture, expected, expected);
    }
    {
        constexpr double origin = 12345.123456;
        const auto doc =
            document({note(1, -.25, .25, 60, 64, 48000, origin), note(2, .25, .75, 61, 127, 48000, origin)});
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        drive(midi, owners, capture, doc, 1, 0, 1, 48000, origin);
        drive(midi, owners, capture, doc, 1, 1, 1, 48000, origin);
        drive(midi, owners, capture, doc, 1, 2, 1, 48000, origin, false);
        const std::initializer_list<Event> expected{
            {0, 0x90, 60, 127}, {0, 0x90, 61, 127}, {0, 0x80, 60, 64}, {1, 0x80, 61, 127}};
        exact(capture, expected, expected);
    }
    {
        // Sparse CONTROL model call advances beyond int32; no physical device
        // or per-sample loop is involved in this wide-coordinate test.
        constexpr std::int64_t first = 2147483658LL;
        const auto doc = document({note(1, double(first), double(first + 2), 60, 64, 48000)});
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture capture;
        drive(midi, owners, capture, doc, 1, 0, 2147483000, 48000);
        drive(midi, owners, capture, doc, 1, 2147483000LL, 1024, 48000);
        const std::initializer_list<Event> expected{{first, 0x90, 60, 127}, {first + 2, 0x80, 60, 64}};
        exact(capture, expected, expected);
    }
    {
        LiveMidi midi;
        MidiNoteOwners owners;
        Capture first;
        const auto held = document({note(1, 0, 1000, 60, 64, 48000)});
        drive(midi, owners, first, held, 1, 0, 1, 48000);
        const double origin = 1. / 48000;
        const auto changed = document({note(2, .25, .75, 61, 127, 96000, origin)}, 2);
        Capture second;
        drive(midi, owners, second, changed, 1, 0, 2, 96000, origin);
        const std::initializer_list<Event> expected{{0, 0x80, 60, 0}, {0, 0x90, 61, 127}, {1, 0x80, 61, 127}};
        exact(second, expected, expected);
    }
}
} // namespace live_midi_frame_order_acceptance
inline void liveMidiFrameOrderTests()
{
    live_midi_frame_order_acceptance::tests();
}
