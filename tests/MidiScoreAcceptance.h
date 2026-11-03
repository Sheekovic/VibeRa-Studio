// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiScoreApplication.h"
static void midiScoreTests(const juce::File &folder)
{
    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    const auto channel = project.channels.front().id;
    pattern.notes = {{1, channel, 2, .5, 60, .7f, false},
                     {2, channel, 2.25, 1.25, 64, 1, false},
                     {3, project.channels.back().id, 0, 1, 70, .5f, false}};
    for (auto &note : pattern.notes)
        note.id = project.allocate();
    pattern.length = 8;
    juce::MemoryBlock data;
    require(writeMidiScore(pattern, channel, 93, 6, 8, data).wasOk(), "SMF score encode");
    MidiScore score;
    require(readMidiScore(data, {}, score).wasOk(), "SMF score decode");
    require(score.notes.size() == 2 && score.tracks == 1 && score.tempoMetadata && score.meterMetadata,
            "SMF channel-only export with tempo/meter metadata");
    require(score.notes[0].beat == 2 && score.notes[0].length == .5 && score.notes[1].pitch == 64 &&
                score.notes[1].beat == 2.25 && score.notes[1].length == 1.25 &&
                std::abs(score.notes[0].velocity - .7f) <= 1.f / 127,
            "SMF musical round trip");
    require(readMidiScore(data, {-1, 1, true}, score).wasOk() && score.notes.front().beat == 0 &&
                score.notes.back().beat == .25,
            "SMF explicit realignment");
    auto retained = score.notes;
    require(readMidiScore(data, {-1, 2, false}, score).failed() && score.notes.size() == retained.size() &&
                score.notes[0].beat == retained[0].beat,
            "SMF empty filter preserves previous output");
    require(readMidiScore(data, {2, 0, false}, score).failed(), "SMF missing track rejected");
    auto truncated = data;
    truncated.setSize(truncated.getSize() - 1);
    require(readMidiScore(truncated, {}, score).failed(), "SMF truncated payload rejected");
    auto smpte = data;
    static_cast<unsigned char *>(smpte.getData())[12] = 0xe7;
    require(readMidiScore(smpte, {}, score).failed(), "SMF unsupported SMPTE explicit rejection");
    auto type2 = data;
    static_cast<unsigned char *>(type2.getData())[9] = 2;
    require(readMidiScore(type2, {}, score).failed(), "SMF asynchronous type 2 explicit rejection");
    auto trailing = data;
    const char byte = 0;
    trailing.append(&byte, 1);
    require(readMidiScore(trailing, {}, score).failed(), "SMF trailing bytes rejected");
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::textMetaEvent(3, "Lead\nline"));
    auto on = juce::MidiMessage::noteOn(3, 72, juce::uint8(96));
    on.setTimeStamp(240);
    auto off = juce::MidiMessage::noteOn(3, 72, juce::uint8(0));
    off.setTimeStamp(720);
    sequence.addEvent(on);
    sequence.addEvent(juce::MidiMessage::controllerEvent(3, 64, 127));
    sequence.addEvent(off);
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(sequence);
    juce::MemoryOutputStream stream;
    require(file.writeTo(stream, 1), "SMF external fixture encode");
    require(readMidiScore(stream.getMemoryBlock(), {-1, 3, false}, score).wasOk() &&
                score.notes.size() == 1 && score.notes[0].beat == .5 && score.notes[0].length == 1 &&
                score.ignoredEvents == 0 && score.controllers.size() == 1 && score.controllers[0].number == 64,
            "SMF alternate PPQ, velocity-zero off and the sustain CC imported as a controller");
    require(score.trackNames.size() == 1 && score.trackNames[0] == "Lead line",
            "SMF track name sanitized for UI");
    require(readMidiScore(stream.getMemoryBlock(), {-1, 0, false, 1u << 2}, score).wasOk() &&
                score.notes.size() == 1,
            "SMF selected subset includes channel 3");
    const auto subsetRetained = score.notes.size();
    require(readMidiScore(stream.getMemoryBlock(), {-1, 0, false, 1u << 9}, score).failed() &&
                score.notes.size() == subsetRetained,
            "SMF excluded subset preserves output on no matching notes");
    require(readMidiScore(stream.getMemoryBlock(), {-1, 0, false, 0}, score).failed(),
            "SMF empty channel subset rejected");
    require(readMidiScore(stream.getMemoryBlock(), {-1, 0, false, 65536}, score).failed(),
            "SMF out-of-range subset rejected");
    juce::MidiMessageSequence mixed;
    for (int channelNumber : {1, 3, 10})
    {
        auto start = juce::MidiMessage::noteOn(channelNumber, 50 + channelNumber, juce::uint8(100));
        auto end = juce::MidiMessage::noteOff(channelNumber, 50 + channelNumber);
        start.setTimeStamp(channelNumber * 480);
        end.setTimeStamp(channelNumber * 480 + 240);
        mixed.addEvent(start);
        mixed.addEvent(end);
    }
    juce::MidiFile multi;
    multi.setTicksPerQuarterNote(480);
    multi.addTrack(mixed);
    juce::MemoryOutputStream multiStream;
    require(multi.writeTo(multiStream, 0), "Mixed-channel SMF fixture");
    require(
        readMidiScore(multiStream.getMemoryBlock(), {-1, 0, true, (1u << 2) | (1u << 9)}, score).wasOk() &&
            score.notes.size() == 2 && score.notes[0].pitch == 53 && score.notes[1].pitch == 60 &&
            score.notes[0].beat == 0 && score.notes[1].beat == 7 && score.notes[0].length == .5,
        "Arbitrary MIDI subset includes 3/10, excludes 1 and realigns after filtering");
    require(
        readMidiScore(multiStream.getMemoryBlock(), {-1, 3, false, (1u << 2) | (1u << 9)}, score).wasOk() &&
            score.notes.size() == 1 && score.notes[0].pitch == 53 && score.notes[0].beat == 3,
        "Legacy MIDI channel selector intersects subset");
    sequence.clear();
    sequence.addEvent(on);
    file.clear();
    file.addTrack(sequence);
    juce::MemoryOutputStream dangling;
    require(file.writeTo(dangling, 0), "SMF dangling fixture encode");
    require(readMidiScore(dangling.getMemoryBlock(), {}, score).failed(),
            "SMF unmatched note fails without invented duration");
    auto invalid = pattern;
    invalid.notes.front().length = -1;
    auto before = data;
    require(writeMidiScore(invalid, channel, 120, 4, 4, data).failed() && data == before,
            "SMF failed export preserves output");
    Controller controller;
    controller.replace(project);
    auto original = controller.project.state();
    MidiScore imported;
    imported.notes = {{0, 0, 1, .75, 75, .6f, false}};
    int notifications = 0;
    controller.changed = [&] { ++notifications; };
    require(applyMidiScore(controller, pattern.id, channel, imported, false).wasOk(),
            "SMF canonical replace");
    require(notifications == 1, "SMF replacement publishes one atomic update");
    const auto *replaced = controller.project.pattern(pattern.id);
    require(replaced->notes.size() == 2 && replaced->notes.back().pitch == 75 &&
                replaced->notes.front().channel == project.channels.back().id,
            "SMF replace preserves other Channels and remaps imported owner");
    auto changed = controller.project.state();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(original), "SMF replace exact one undo");
    controller.redoEdit();
    require(controller.project.state().isEquivalentTo(changed), "SMF replace exact redo");
    require(applyMidiScore(controller, pattern.id, channel, imported, true).wasOk() &&
                controller.project.pattern(pattern.id)->notes.size() == 3,
            "SMF blend preserves notes");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(changed), "SMF blend exact undo");
    imported.notes.front().length = -1;
    require(applyMidiScore(controller, pattern.id, channel, imported, false).failed() &&
                controller.project.state().isEquivalentTo(changed),
            "SMF invalid replacement rolls back deletion");
    controller.beginGesture();
    require(applyMidiScore(controller, pattern.id, channel, imported, true).failed(),
            "SMF import excludes active gestures");
    controller.cancelGesture();
    folder.getChildFile("midi-score.mid").replaceWithData(data.getData(), data.getSize());
    folder.getChildFile("midi-score-verification.txt")
        .replaceWithText("PASS: actual SMF codec round trip, PPQ, filters, realignment, velocity-zero off, "
                         "metadata/loss flags, malformed/unsupported timing and failure preservation. Codec "
                         "only; no import UI, physical MIDI, listening or FL runtime observation.\n");
}
