// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ChordProgression.h"
#include "Commands.h"

// Chord progression generator: shape, harmony and the one-step write into a Pattern.
namespace chord_progression_acceptance
{
using namespace vibera;

inline void tests()
{
    int seventhsConventional = 0, seventhsAdventurous = 0;
    for (std::uint32_t seed = 1; seed <= 60; ++seed)
    {
        chords::Settings st;
        st.key = int(seed % 12);
        st.scale = int(seed % 5);
        st.count = 2 + int(seed % 7);
        st.adventure = 0;
        st.seed = seed;
        const auto plain = chords::Generator(st).generate();
        require(int(plain.size()) == st.count, "The progression has the requested number of chords");
        const auto steps = chords::scaleSteps(st.scale);
        double movement = 0;
        for (size_t i = 0; i < plain.size(); ++i)
        {
            const auto &c = plain[i];
            require(c.name.isNotEmpty() && c.pitches.size() >= 3 && c.pitches.front() >= 36 && c.pitches.back() <= 96,
                    "Every chord is named and voiced in a playable range");
            if (!c.borrowed)
                require(c.root == (st.key + steps[size_t(c.degree)]) % 12, "Diatonic chords sit on their scale degree");
            for (size_t k = 1; k < c.pitches.size(); ++k)
                require(c.pitches[k] > c.pitches[k - 1], "Voicings rise from low to high");
            if (i > 0)
                movement += std::abs(c.pitches.front() - plain[i - 1].pitches.front());
            seventhsConventional += c.seventh;
        }
        require(plain.size() < 2 || movement / double(plain.size() - 1) <= 7,
                "Voice leading keeps the lowest voice moving by a few semitones");
        st.adventure = 1;
        for (const auto &c : chords::Generator(st).generate())
            seventhsAdventurous += c.seventh;
    }
    require(seventhsAdventurous > seventhsConventional * 2, "Adventurous progressions use more seventh chords");
    {
        chords::Settings st;
        st.seed = 7;
        chords::Generator generator(st);
        auto list = generator.generate();
        const auto before = list[1];
        const auto rolled = generator.reroll(list, 1);
        require(rolled.degree != before.degree || rolled.seventh != before.seventh,
                "Rolling one chord changes that chord");
        require(chords::chordName(0, 4, 7, -1) == "C" && chords::chordName(2, 3, 7, 10) == "Dm7" &&
                    chords::chordName(11, 3, 6, -1) == "Bdim" && chords::chordName(7, 4, 7, 10) == "G7",
                "Chord names follow their intervals");
    }
    // The window writes chords + bass through notes.import as one Undo step.
    Controller controller;
    const auto channel = controller.project.channels.back().id;
    CommandArgs a;
    a.pattern = controller.project.patterns.front().id;
    a.channel = channel;
    a.enabled = false;
    chords::Settings st;
    st.seed = 3;
    const auto progression = chords::Generator(st).generate();
    for (size_t i = 0; i < progression.size(); ++i)
        for (int pitch : progression[i].pitches)
            a.recordedNotes.push_back({0, channel, double(i) * 4, 4, pitch, .78f, false});
    const auto before = controller.project.state();
    require(controller.execute("notes.import", a).wasOk(), "Chord notes are written");
    size_t written = 0;
    for (const auto &n : controller.project.patterns.front().notes)
        written += n.channel == channel;
    require(written == a.recordedNotes.size(), "Every chord tone lands on the Channel");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "Accepting a progression is one Undo step");

    // MIDI take modes: overdub keeps earlier notes; replace clears the recorded span only.
    Controller takes;
    const auto lead = takes.project.channels.back().id;
    auto &part = takes.project.patterns.front();
    for (double beat : {0., 1., 2., 3.})
        part.notes.push_back({takes.project.allocate(), lead, beat, .5, 60, .8f, false});
    CommandArgs take;
    take.pattern = part.id;
    take.recordedNotes = {{0, lead, 1.2, .5, 67, .9f, false}, {0, lead, 2.1, .4, 69, .9f, false}};
    require(takes.execute("notes.record", take).wasOk(), "An overdub take records");
    size_t kept = 0;
    for (const auto &n : takes.project.patterns.front().notes)
        kept += n.channel == lead && n.pitch == 60;
    require(kept == 4, "Overdub keeps every earlier note");
    takes.undoEdit();
    take.replaceRecorded = true;
    require(takes.execute("notes.record", take).wasOk(), "A replace take records");
    std::vector<double> remaining;
    for (const auto &n : takes.project.patterns.front().notes)
        if (n.channel == lead && n.pitch == 60)
            remaining.push_back(n.beat);
    std::sort(remaining.begin(), remaining.end());
    require(remaining == std::vector<double>{0., 3.}, "Replace clears only the notes inside the recorded span");
}
} // namespace chord_progression_acceptance
