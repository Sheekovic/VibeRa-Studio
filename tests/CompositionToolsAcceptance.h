// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "ScoreTools.h"
#include "Performance.h"

// Piano Roll composition tools through their canonical commands: exact notes, IDs,
// seeded determinism, refusals and one-step Undo.
namespace composition_tools_acceptance
{
using namespace vibera;

struct Fixture
{
    Controller controller;
    Id pattern = 0, channel = 0;
    Fixture()
    {
        auto project = Project::empty();
        pattern = project.patterns[0].id;
        channel = project.channels.back().id;
        controller.replace(project);
    }
    std::vector<Note> notes() const
    {
        auto list = controller.project.pattern(pattern)->notes;
        std::sort(list.begin(), list.end(),
                  [](const Note &a, const Note &b) { return a.beat != b.beat ? a.beat < b.beat : a.pitch < b.pitch; });
        return list;
    }
    std::vector<Id> add(std::initializer_list<std::tuple<double, double, int>> list)
    {
        std::vector<Id> ids;
        for (auto [beat, length, pitch] : list)
        {
            CommandArgs note;
            note.pattern = pattern;
            note.channel = channel;
            note.beat = beat;
            note.length = length;
            note.pitch = pitch;
            note.value = .8; // notes.add takes velocity from value.
            require(controller.execute("notes.add", note).wasOk(), "Fixture note added");
            ids.push_back(controller.project.pattern(pattern)->notes.back().id);
        }
        return ids;
    }
    CommandArgs args(const std::vector<Id> &ids) const
    {
        CommandArgs a;
        a.pattern = pattern;
        a.channel = channel;
        a.ids = ids;
        return a;
    }
};
inline std::vector<int> pitches(const std::vector<Note> &notes)
{
    std::vector<int> out;
    for (const auto &note : notes)
        out.push_back(note.pitch);
    return out;
}

inline void followActionTests()
{
    // Performance Follow Actions: canonical command, validation, persistence and the pad sequence.
    Controller controller;
    auto project = Project::empty();
    while (project.patterns.size() < 3)
    {
        Pattern extra = project.patterns[0];
        extra.id = project.allocate();
        extra.name = "Pattern " + juce::String(int(project.patterns.size()) + 1);
        project.patterns.push_back(extra);
    }
    for (auto &p : project.patterns)
        p.length = 4;
    controller.replace(project);
    const auto first = controller.project.patterns[0].id, second = controller.project.patterns[1].id,
               third = controller.project.patterns[2].id;
    CommandArgs a;
    a.pattern = first;
    a.text = "next";
    a.value = 2;
    require(controller.execute("pattern.followAction", a).wasOk(), "Follow Action next after 2 plays");
    a.pattern = second;
    a.value = 1;
    require(controller.execute("pattern.followAction", a).wasOk(), "Follow Action next after 1 play");
    a.pattern = third;
    a.text = "stop";
    require(controller.execute("pattern.followAction", a).wasOk(), "Follow Action stop");
    a.text = "sideways";
    require(controller.execute("pattern.followAction", a).failed(), "Unknown Follow Actions are refused");
    a.text = "next";
    a.value = 65;
    require(controller.execute("pattern.followAction", a).failed(), "Play counts above 64 are refused");
    Project reloaded;
    require(Project::parse(controller.project.state(), reloaded).wasOk() &&
                reloaded.pattern(first)->followAction == "next" && reloaded.pattern(first)->followAfter == 2,
            "Follow Actions survive save/load");
    std::map<Id, performance::Pad> pads;
    performance::Pad pad;
    pad.pattern = first;
    pad.lane = performance::firstLane;
    pads[first] = pad;
    std::vector<Id> played;
    for (double playhead = 0; playhead < 40; playhead += 1)
        for (const auto &clip : performance::due(pads, controller.project, playhead, 1.))
            played.push_back(clip.pattern);
    require(played == std::vector<Id>{first, first, second, third}, "Pad plays first twice, second once, third once, then stops");
    require(pads.begin()->second.stopping, "The stop action ends the pad");
    controller.undoEdit();
    controller.undoEdit();
    controller.undoEdit();
    require(controller.project.pattern(first)->followAction.isEmpty(), "Each Follow Action change is one Undo step");
}
inline void tests(const juce::File &)
{
    followActionTests();
    auto same = [](double a, double b) { return std::abs(a - b) < 1.e-9; };
    {
        Fixture f;
        const auto root = f.add({{0, 1, 60}});
        auto chord = f.args(root);
        chord.text = "major";
        const auto before = f.controller.project.state();
        require(f.controller.execute("notes.chordify", chord).wasOk(), "Chordify major");
        require(pitches(f.notes()) == std::vector<int>{60, 64, 67}, "C becomes C-E-G");
        require(f.notes()[0].id == root[0], "The root keeps its ID");
        f.controller.undoEdit();
        require(f.controller.project.state().isEquivalentTo(before), "Chordify is one exact Undo step");
        chord.text = "min7";
        chord.value = 1;
        require(f.controller.execute("notes.chordify", chord).wasOk(), "Chordify min7 first inversion");
        require(pitches(f.notes()) == std::vector<int>{63, 67, 70, 72}, "First inversion moves the root up");
        chord.text = "nonsense";
        require(f.controller.execute("notes.chordify", chord).failed(), "Unknown chord refused");
    }
    {
        Fixture f;
        const auto chord = f.add({{0, 1, 60}, {0, 1, 64}, {0, 1, 67}});
        auto arp = f.args(chord);
        arp.text = "up";
        arp.length = .25;
        arp.value = .5;
        require(f.controller.execute("notes.arpeggiate", arp).wasOk(), "Arpeggiate up");
        const auto up = f.notes();
        require(pitches(up) == std::vector<int>{60, 64, 67, 60}, "Up cycles through the chord over its length");
        require(same(up[1].beat, .25) && same(up[3].beat, .75) && same(up[0].length, .125),
                "Steps of 1/4 beat with half-step gate");
        f.controller.undoEdit();
        arp.text = "updown";
        arp.value = 1;
        arp.length = .125;
        require(f.controller.execute("notes.arpeggiate", arp).wasOk(), "Arpeggiate up-down");
        require(pitches(f.notes()) == std::vector<int>{60, 64, 67, 64, 60, 64, 67, 64}, "Up-down ping-pongs");
        f.controller.undoEdit();
        arp.text = "random";
        arp.seed = 7;
        require(f.controller.execute("notes.arpeggiate", arp).wasOk(), "Arpeggiate random");
        const auto first = pitches(f.notes());
        f.controller.undoEdit();
        require(f.controller.execute("notes.arpeggiate", arp).wasOk(), "Arpeggiate random again");
        require(pitches(f.notes()) == first, "The same seed replays the same random arpeggio");
        f.controller.undoEdit();
        arp.text = "sideways";
        require(f.controller.execute("notes.arpeggiate", arp).failed(), "Unknown arpeggio mode refused");
    }
    {
        Fixture f;
        const auto chord = f.add({{1, 1, 60}, {1, 1, 64}, {1, 1, 67}});
        auto strum = f.args(chord);
        strum.text = "down";
        strum.length = .0625;
        strum.value = .5;
        require(f.controller.execute("notes.strum", strum).wasOk(), "Strum down");
        const auto notes = f.notes();
        require(notes[0].pitch == 67 && same(notes[0].beat, 1) && notes[1].pitch == 64 &&
                    same(notes[1].beat, 1.0625) && notes[2].pitch == 60 && same(notes[2].beat, 1.125),
                "Down strum starts from the top note, 1/64 beat apart");
        require(same(notes[2].beat + notes[2].length, 2) && notes[2].velocity < notes[0].velocity,
                "Strummed notes keep their ends and taper");
        Fixture lone;
        auto single = lone.args(lone.add({{0, 1, 60}}));
        single.text = "up";
        single.length = .0625;
        require(lone.controller.execute("notes.strum", single).failed(), "Strum without a chord is refused");
    }
    {
        Fixture f;
        const auto ids = f.add({{1, .5, 60}, {2, .5, 62}, {3, .5, 64}});
        auto human = f.args(ids);
        human.beat = .05;
        human.value = .1;
        human.offset = .2;
        human.seed = 42;
        const auto original = f.notes();
        require(f.controller.execute("notes.humanize", human).wasOk(), "Humanize");
        const auto once = f.notes();
        for (size_t i = 0; i < once.size(); ++i)
            require(std::abs(once[i].beat - original[i].beat) <= .05 + 1.e-9 &&
                        std::abs(once[i].velocity - original[i].velocity) <= .1f + 1.e-6f &&
                        std::abs(once[i].length / original[i].length - 1) <= .2 + 1.e-9,
                    "Humanize stays inside its ranges");
        require(!same(once[0].beat, original[0].beat), "Humanize changes timing");
        f.controller.undoEdit();
        require(f.controller.execute("notes.humanize", human).wasOk(), "Humanize again");
        const auto twice = f.notes();
        for (size_t i = 0; i < once.size(); ++i)
            require(same(once[i].beat, twice[i].beat) && once[i].velocity == twice[i].velocity,
                    "The same seed replays the same humanization");
    }
    {
        Fixture f;
        const auto ids = f.add({{0, 1, 61}, {1, 1, 63}, {2, 1, 66}, {3, 1, 72}});
        auto snap = f.args(ids);
        snap.text = "major";
        snap.pitch = 0;
        require(f.controller.execute("notes.scaleSnap", snap).wasOk(), "Snap to C major");
        require(pitches(f.notes()) == std::vector<int>{60, 62, 65, 72}, "Out-of-scale notes move to the nearest, ties down");
        f.controller.undoEdit();
        snap.text = "hijaz";
        snap.pitch = 62; // D, given as any MIDI pitch.
        require(f.controller.execute("notes.scaleSnap", snap).wasOk(), "Snap to D hijaz");
        require(pitches(f.notes()) == std::vector<int>{60, 63, 66, 72}, "D hijaz: D Eb F# G A Bb C");
        snap.text = "bayati-quarter";
        require(f.controller.execute("notes.scaleSnap", snap).failed(), "Unknown scale refused");
    }
    {
        // Rast on C: C D E-half-flat F G A B-half-flat. E and B become E/B at -50 cents.
        Fixture f;
        const auto ids = f.add({{0, 1, 60}, {1, 1, 64}, {2, 1, 71}, {3, 1, 63}});
        auto maqam = f.args(ids);
        maqam.text = "rast";
        maqam.pitch = 60;
        const auto before = f.controller.project.state();
        require(f.controller.execute("notes.maqam", maqam).wasOk(), "Snap to C rast");
        const auto notes = f.notes();
        require(notes[0].pitch == 60 && same(notes[0].fineCents, 0), "C stays C");
        require(notes[1].pitch == 64 && same(notes[1].fineCents, -50), "E becomes E half-flat");
        require(notes[2].pitch == 71 && same(notes[2].fineCents, -50), "B becomes B half-flat");
        require(notes[3].pitch == 64 && same(notes[3].fineCents, -50),
                "Eb (between D and E half-flat) moves to the nearer E half-flat");
        f.controller.undoEdit();
        require(f.controller.project.state().isEquivalentTo(before), "Maqam snap is one exact Undo step");
        // Sikah on E: its tonic is E half-flat.
        maqam.text = "sikah";
        maqam.pitch = 64;
        require(f.controller.execute("notes.maqam", maqam).wasOk(), "Snap to sikah on E");
        require(f.notes()[1].pitch == 64 && same(f.notes()[1].fineCents, -50), "Sikah tonic is E half-flat");
        // Bayati on D: D E-half-flat F G A Bb C; already-tuned notes keep their cents.
        Fixture b;
        const auto bayati = b.add({{0, 1, 63}, {1, 1, 66}});
        auto bay = b.args(bayati);
        bay.text = "bayati";
        bay.pitch = 62;
        require(b.controller.execute("notes.maqam", bay).wasOk(), "Snap to D bayati");
        require(b.notes()[0].pitch == 64 && same(b.notes()[0].fineCents, -50), "Eb in D bayati is E half-flat");
        require(b.notes()[1].pitch == 65 && same(b.notes()[1].fineCents, 0), "F# in D bayati moves to F (ties down)");
        require(b.controller.execute("notes.maqam", bay).wasOk() && b.notes()[0].pitch == 64 &&
                    same(b.notes()[0].fineCents, -50),
                "Snapping again is stable");
        bay.text = "zanjaran";
        require(b.controller.execute("notes.maqam", bay).failed(), "Unknown maqam refused");
    }
    // Piano Roll guide: degrees, the tonic, and quarter tones flagged on the key above.
    {
        const auto rast = score::guideMarks("rast", 0, true);
        require(rast[0] == 2 && rast[2] == 1 && rast[4] == (1 | 4) && rast[5] == 1 && rast[7] == 1 && rast[9] == 1 &&
                    rast[11] == (1 | 4) && rast[1] == 0 && rast[3] == 0 && rast[10] == 0,
                "Rast on C: E and B half-flat, tonic C");
        const auto sikah = score::guideMarks("sikah", 4, true);
        require(sikah[4] == (2 | 4), "Sikah on E: the tonic itself is E half-flat");
        const auto major = score::guideMarks("major", 2, false);
        require(major[2] == 2 && major[6] == 1 && major[1] == 1 && major[3] == 0 &&
                    std::none_of(major.begin(), major.end(), [](int m) { return (m & 4) != 0; }),
                "D major: no quarter tones, tonic D");
        const auto unknown = score::guideMarks("nothing", 0, true);
        require(std::all_of(unknown.begin(), unknown.end(), [](int m) { return m == 0; }),
                "An unknown guide marks nothing");
    }
}
} // namespace composition_tools_acceptance
