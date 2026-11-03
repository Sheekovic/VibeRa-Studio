// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "LiveMidi.h"
#include "LiveTimeline.h"
#include "MidiRecording.h"

// MIDI controllers in takes: sustain extends note lengths, CC/pitch bend/pressure are kept
// as Pattern events, persisted, cleared, and played to plugin instruments with chase.
namespace controller_recording_acceptance
{
using namespace vibera;

inline MidiCapture::Event event(Id channel, double seconds, std::array<unsigned char, 3> bytes)
{
    MidiCapture::Event e;
    e.channel = channel;
    e.seconds = seconds;
    e.session = 1;
    e.bytes = bytes;
    return e;
}

inline void tests(const juce::File &)
{
    auto same = [](double a, double b) { return std::abs(a - b) < 1.e-9; };
    const Id channel = 7;
    {
        MidiTake take;
        take.begin(1, 0, 0, 120, 0); // 2 beats per second.
        auto apply = [&](double seconds, std::array<unsigned char, 3> bytes)
        { require(take.acceptCapturedEvent(event(channel, seconds, bytes)) == MidiTake::EventResult::applied, "Event applied"); };
        apply(0, {0x90, 60, 100});
        apply(.1, {0xb0, 1, 64});     // Mod wheel half way.
        apply(.25, {0xb0, 64, 127});  // Pedal down.
        apply(.5, {0x80, 60, 0});     // Key up under the pedal.
        apply(.6, {0xe0, 0, 64});     // Pitch bend centre.
        apply(.7, {0xe0, 127, 127});  // Pitch bend fully up.
        apply(.8, {0xd0, 100, 0});    // Channel pressure.
        apply(1, {0xb0, 64, 0});      // Pedal up: the note ends here.
        apply(1.5, {0x90, 62, 90});
        apply(1.75, {0x80, 62, 0});   // No pedal: ends at its key up.
        apply(2, {0x91, 64, 80});     // MIDI channel 2.
        apply(2.1, {0xb1, 64, 127});
        apply(2.2, {0x81, 64, 0});    // Still under the pedal when the take stops.
        require(take.seal(3) == MidiTake::SealResult::ready, "Take seals");
        auto notes = take.releaseFinishedNotes();
        std::sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) { return a.beat < b.beat; });
        require(notes.size() == 3 && same(notes[0].length, 2) && same(notes[1].length, .5) && same(notes[2].length, 2),
                "Sustain holds notes until pedal up (or the end of the take); unpedalled notes keep their length");
        const auto controls = take.releaseFinishedControllers();
        require(controls.size() == 4, "Mod wheel, two pitch bends and pressure are kept; sustain is not");
        require(controls[0].type == 0 && controls[0].number == 1 && same(controls[0].beat, .2) &&
                    same(controls[0].value, 64. / 127.),
                "CC keeps its number, beat and normalized value");
        require(controls[1].type == 1 && std::abs(controls[1].value) < 1.e-3 && controls[2].type == 1 &&
                    same(controls[2].value, 1),
                "Pitch bend maps centre to 0 and the top to +1");
        require(controls[3].type == 2 && same(controls[3].value, 100. / 127.), "Channel pressure is kept");
    }
    // Model: a controllers-only take commits, persists (version 46), and clears per Channel.
    auto project = Project::empty();
    project.tempo = 120;
    const auto pattern = project.patterns[0].id;
    juce::PluginDescription description;
    description.name = "Controller fixture instrument";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "controller-fixture.vst3";
    description.numInputChannels = description.numOutputChannels = 2;
    description.isInstrument = true;
    auto &synth = project.channels[0];
    synth.instrument = PluginSpec{project.allocate(), description.name, description.createXml()->toString(),
                                  juce::MemoryBlock("state", 5).toBase64Encoding(), true, false};
    const Id instrument = synth.id;
    Controller controller;
    controller.replace(project);
    CommandArgs record;
    record.pattern = pattern;
    record.recordedControllers = {{0, instrument, 1, .5, 0, 1, 0}, {0, instrument, 2, -1, 1, 0, 0}};
    const auto committed = controller.execute("notes.record", record);
    require(committed.wasOk(), ("A controllers-only take commits: " + committed.getErrorMessage()).toStdString());
    const auto &stored = controller.project.pattern(pattern)->controllers;
    require(stored.size() == 2 && stored[0].id != 0 && stored[0].beat == 1, "Controllers are stored with IDs");
    {
        Project reopened;
        require(Project::parse(controller.project.state(), reopened).wasOk() &&
                    reopened.pattern(pattern)->controllers.size() == 2 &&
                    reopened.pattern(pattern)->controllers[1].type == 1,
                "Controllers survive save and reopen");
        auto old = controller.project.state().createCopy();
        old.setProperty("version", 45, nullptr);
        require(Project::parse(old, reopened).failed(), "A version-45 file cannot carry controllers");
    }
    {
        // Played to the instrument: CC1 at beat 1 (0.5 s) and full-down bend at beat 2 (1 s).
        auto song = controller.project;
        song.patterns[0].length = 4;
        song.placements = {{song.allocate(), pattern, 0, 0, 4, 0}};
        auto document = compileLiveDocument(song, false, pattern, 1);
        require(document != nullptr, "Live document compiles");
        LiveMidi midi;
        int cc = -1, bend = -1;
        for (int position = 0; position < 60000; position += 512)
        {
            const int count = midi.process(*document, instrument, position / 48000., 512, 48000, true);
            for (int i = 0; i < count; ++i)
            {
                const auto &e = midi.events[size_t(i)];
                if ((e.bytes[0] & 0xf0) == 0xb0 && e.bytes[1] == 1)
                    cc = position + e.frame, require(e.bytes[2] == 64, "CC value is played");
                if ((e.bytes[0] & 0xf0) == 0xe0)
                    bend = position + e.frame, require(e.bytes[1] == 1 && e.bytes[2] == 0, "Bend value is played");
            }
        }
        require(cc == 24000 && bend == 48000, "Controllers play at their exact sample positions");
        LiveMidi jumped;
        jumped.process(*document, instrument, 0, 512, 48000, true);
        const int count = jumped.process(*document, instrument, 1.25, 512, 48000, true);
        bool chasedCc = false, chasedBend = false;
        for (int i = 0; i < count; ++i)
        {
            const auto &e = jumped.events[size_t(i)];
            chasedCc |= (e.bytes[0] & 0xf0) == 0xb0 && e.bytes[1] == 1 && e.frame == 0;
            chasedBend |= (e.bytes[0] & 0xf0) == 0xe0 && e.frame == 0;
        }
        require(chasedCc && chasedBend, "A jump past controllers re-sends their latest values");
    }
    CommandArgs clear;
    clear.pattern = pattern;
    clear.channel = instrument;
    require(controller.execute("notes.clearControllers", clear).wasOk() &&
                controller.project.pattern(pattern)->controllers.empty(),
            "Recorded controllers clear per Channel");
    require(controller.execute("notes.clearControllers", clear).failed(), "Clearing nothing is refused");
}
} // namespace controller_recording_acceptance
