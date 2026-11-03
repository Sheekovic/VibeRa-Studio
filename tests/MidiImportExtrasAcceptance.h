// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MidiScoreApplication.h"

// MIDI import beyond notes: CC, pitch bend and channel pressure go into the Pattern for the
// target Channel, and the file tempo map/meter can replace the project's in the same Undo step.
namespace midi_import_extras_acceptance
{
using namespace vibera;

inline juce::MemoryBlock fixture()
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence conductor, part;
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(600000), 0);       // 100 BPM
    conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(3, 4), 0); // 3/4
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(428571), 480 * 4); // ~140 BPM at beat 4
    conductor.addEvent(juce::MidiMessage::endOfTrack(), 480 * 8);
    part.addEvent(juce::MidiMessage::controllerEvent(2, 7, 100), 480 * 2);
    part.addEvent(juce::MidiMessage::noteOn(2, 60, juce::uint8(100)), 480 * 2);
    part.addEvent(juce::MidiMessage::pitchWheel(2, 16383), 480 * 3);
    part.addEvent(juce::MidiMessage::channelPressureChange(2, 64), 480 * 3);
    part.addEvent(juce::MidiMessage::noteOff(2, 60), 480 * 4);
    part.addEvent(juce::MidiMessage::programChange(2, 5), 480 * 4);
    part.addEvent(juce::MidiMessage::endOfTrack(), 480 * 8);
    file.addTrack(conductor);
    file.addTrack(part);
    juce::MemoryOutputStream out;
    file.writeTo(out, 1);
    return out.getMemoryBlock();
}

inline void tests(const juce::File &)
{
    const auto bytes = fixture();
    MidiScore score;
    require(readMidiScore(bytes, {}, score).wasOk(), "Read the fixture");
    require(score.notes.size() == 1 && score.controllers.size() == 4 && score.ignoredEvents == 0,
            "Notes and four controller events (CC, bend, pressure, program) are read");
    require(score.controllers[3].type == 3 && score.controllers[3].number == 5 && score.controllers[3].beat == 4 &&
                score.controllers[3].midiChannel == 1,
            "The program change keeps its program, beat and MIDI channel");
    const auto &cc = score.controllers[0], &bend = score.controllers[1], &pressure = score.controllers[2];
    require(cc.type == 0 && cc.number == 7 && cc.midiChannel == 1 && cc.beat == 2 &&
                std::abs(cc.value - 100. / 127) < 1.e-9,
            "CC 7 keeps its number, channel, beat and value");
    require(bend.type == 1 && bend.beat == 3 && std::abs(bend.value - 1) < 1.e-9, "Pitch bend reads as -1..1");
    require(pressure.type == 2 && std::abs(pressure.value - 64. / 127) < 1.e-9, "Channel pressure reads 0..1");
    require(score.tempos.size() == 2 && std::abs(score.tempos[0].second - 100) < 1.e-6 && score.tempos[1].first == 4 &&
                std::abs(score.tempos[1].second - 140) < .01 && score.numerator == 3 && score.denominator == 4,
            "Tempo map and meter are read from the conductor track");
    MidiScoreOptions secondTrack;
    secondTrack.track = 1;
    require(readMidiScore(bytes, secondTrack, score).wasOk() && score.tempos.size() == 2 && score.numerator == 3,
            "Selecting the part track still reads the file tempo map");
    MidiScoreOptions realigned;
    realigned.realign = true;
    require(readMidiScore(bytes, realigned, score).wasOk() && score.notes[0].beat == 0 &&
                score.controllers[0].beat == 0 && score.controllers[1].beat == 1 && score.tempos.size() == 2 &&
                score.tempos[0].first == 0 && std::abs(score.tempos[0].second - 100) < 1.e-6 &&
                score.tempos[1].first == 2,
            "Realigning shifts controllers and the tempo map with the notes");

    Controller controller;
    auto project = Project::empty();
    const auto pattern = project.patterns[0].id, channel = project.channels[0].id;
    ControllerEvent old;
    old.id = project.allocate();
    old.channel = channel;
    old.number = 1;
    project.patterns[0].controllers.push_back(old);
    controller.replace(project);
    require(readMidiScore(bytes, {}, score).wasOk(), "Read again");
    const auto before = controller.project.state();
    require(applyMidiScore(controller, pattern, channel, score, false, true).wasOk(), "Import with the file tempo");
    const auto &imported = controller.project.patterns[0];
    require(imported.controllers.size() == 4 &&
                std::all_of(imported.controllers.begin(), imported.controllers.end(),
                            [&](const auto &c) { return c.channel == channel && c.id != 0; }),
            "Replace mode swaps the Channel's controllers for the imported ones");
    require(std::abs(controller.project.tempo - 100) < 1.e-6 && controller.project.tempoChanges.size() == 1 &&
                controller.project.tempoChanges[0].beat == 4 &&
                std::abs(controller.project.tempoChanges[0].bpm - 140) < .01 && controller.project.numerator == 3 &&
                controller.project.denominator == 4,
            "The project takes the file tempo map and 3/4");
    Project saved;
    require(Project::parse(controller.project.state(), saved).wasOk() && saved.patterns[0].controllers.size() == 4,
            "Imported controllers survive save/load");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "The whole import is one exact Undo step");

    require(applyMidiScore(controller, pattern, channel, score, true, false).wasOk(), "Blend without tempo");
    require(controller.project.patterns[0].controllers.size() == 5 && controller.project.tempo == 120 &&
                controller.project.tempoChanges.empty() && controller.project.numerator == 4,
            "Blend keeps existing controllers; the project tempo stays unless asked");

    // Export writes the Channel's controllers back; a re-import reads the same events.
    juce::MemoryBlock exported;
    require(writeMidiScore(controller.project.patterns[0], channel, 120, 4, 4, exported).wasOk(),
            "Export the Channel part");
    MidiScore back;
    require(readMidiScore(exported, {}, back).wasOk() && back.controllers.size() == 5, "Controllers are exported");
    require(std::any_of(back.controllers.begin(), back.controllers.end(),
                        [](const auto &c) { return c.type == 3 && c.number == 5 && c.beat == 4; }),
            "The program change is exported");
    const auto exportedBend = std::find_if(back.controllers.begin(), back.controllers.end(),
                                           [](const auto &c) { return c.type == 1; });
    const auto exportedCc = std::find_if(back.controllers.begin(), back.controllers.end(),
                                         [](const auto &c) { return c.type == 0 && c.number == 7; });
    require(exportedBend != back.controllers.end() && exportedBend->beat == 3 &&
                std::abs(exportedBend->value - 1) < 1.e-9 && exportedCc != back.controllers.end() &&
                exportedCc->beat == 2 && exportedCc->midiChannel == 1 && std::abs(exportedCc->value - 100. / 127) < 1.e-9,
            "Exported controllers keep beat, MIDI channel and value");

    // Hand editing of single controller events: add, set and delete, one Undo step each.
    {
        const auto beforeEdit = controller.project.state();
        CommandArgs add;
        add.pattern = pattern;
        add.channel = channel;
        add.beat = 6;
        add.pitch = 74;
        add.value = .25;
        add.text = "cc";
        require(controller.execute("notes.controller.add", add).wasOk(), "Add a CC 74 event");
        add.text = "pitchBend";
        add.beat = 7;
        add.value = -.5;
        require(controller.execute("notes.controller.add", add).wasOk(), "Add a pitch bend event");
        add.text = "program";
        add.value = 10. / 127;
        require(controller.execute("notes.controller.add", add).wasOk(), "Add a program change");
        // Each command replaces the project: always look the events up again.
        const auto find = [&](int type) -> const ControllerEvent *
        {
            for (const auto &c : controller.project.patterns[0].controllers)
                if (c.type == type && c.beat >= 6)
                    return &c;
            return nullptr;
        };
        require(find(0) && find(0)->number == 74 && find(0)->value == .25 && find(1) && find(1)->value == -.5 &&
                    find(3) && find(3)->number == 10,
                "Added events keep type, number and value");
        add.text = "pressure";
        add.value = -.2;
        require(controller.execute("notes.controller.add", add).failed(), "Pressure below 0 is refused");
        CommandArgs set;
        set.pattern = pattern;
        set.target = find(1)->id;
        set.value = .75;
        require(controller.execute("notes.controller.set", set).wasOk() && find(1)->value == .75, "Set a bend value");
        set.value = 2;
        require(controller.execute("notes.controller.set", set).failed(), "Out-of-range values are refused");
        CommandArgs remove;
        remove.pattern = pattern;
        remove.target = find(0)->id;
        const auto beforeDelete = controller.project.state();
        const auto deleted = controller.execute("notes.controller.delete", remove);
        require(deleted.wasOk() && !find(0), ("Delete one event: " + deleted.getErrorMessage()).toStdString().c_str());
        controller.undoEdit();
        require(controller.project.state().isEquivalentTo(beforeDelete), "Deleting is one exact Undo step");
        remove.target = 999999;
        require(controller.execute("notes.controller.delete", remove).failed(), "Unknown event IDs are refused");
        for (int i = 0; i < 4; ++i)
            controller.undoEdit();
        require(controller.project.state().isEquivalentTo(beforeEdit), "Each edit is one Undo step");
    }

    // One new Channel per MIDI track: named after the track, settings cloned from the target.
    juce::MidiFile multi;
    multi.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence conductor, bass, lead;
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 0);
    bass.addEvent(juce::MidiMessage::textMetaEvent(3, "Bass"), 0);
    bass.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 0);
    bass.addEvent(juce::MidiMessage::noteOn(1, 36, juce::uint8(100)), 0);
    bass.addEvent(juce::MidiMessage::noteOff(1, 36), 480);
    lead.addEvent(juce::MidiMessage::textMetaEvent(3, "Lead"), 0);
    lead.addEvent(juce::MidiMessage::noteOn(1, 72, juce::uint8(100)), 480);
    lead.addEvent(juce::MidiMessage::noteOff(1, 72), 960);
    lead.addEvent(juce::MidiMessage::noteOn(1, 74, juce::uint8(100)), 960);
    lead.addEvent(juce::MidiMessage::noteOff(1, 74), 1440);
    for (auto *track : {&conductor, &bass, &lead})
        multi.addTrack(*track);
    juce::MemoryOutputStream multiBytes;
    multi.writeTo(multiBytes, 1);
    MidiScore tracks;
    require(readMidiScore(multiBytes.getMemoryBlock(), {}, tracks).wasOk() && tracks.notes.size() == 3,
            "Read the multi-track file");
    const auto channelsBefore = controller.project.channels.size();
    const auto templateNotes = std::count_if(controller.project.patterns[0].notes.begin(),
                                             controller.project.patterns[0].notes.end(),
                                             [&](const auto &n) { return n.channel == channel; });
    const auto beforeSplit = controller.project.state();
    require(applyMidiScoreTracks(controller, pattern, channel, tracks).wasOk(), "Import tracks as Channels");
    const auto &channels = controller.project.channels;
    require(channels.size() == channelsBefore + 2 && channels[channelsBefore].name == "Bass" &&
                channels[channelsBefore + 1].name == "Lead",
            "Only tracks with notes become Channels, named after their tracks");
    const auto bassId = channels[channelsBefore].id, leadId = channels[channelsBefore + 1].id;
    const auto &part = controller.project.patterns[0];
    auto count = [&](Id id) { return std::count_if(part.notes.begin(), part.notes.end(), [&](const auto &n) { return n.channel == id; }); };
    require(count(bassId) == 1 && count(leadId) == 2 && count(channel) == templateNotes,
            "Each track's notes go to its own Channel; the target Channel is untouched");
    require(std::count_if(part.controllers.begin(), part.controllers.end(),
                          [&](const auto &c) { return c.channel == bassId && c.number == 1; }) == 1,
            "A track's controllers follow its notes");
    require(channels[channelsBefore].source == controller.project.channel(channel)->source &&
                channels[channelsBefore].mixer == controller.project.channel(channel)->mixer,
            "New Channels clone the target Channel's settings");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeSplit), "Track import is one exact Undo step");
}
} // namespace midi_import_extras_acceptance
