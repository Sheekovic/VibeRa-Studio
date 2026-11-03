// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ScoreLog.h"
#include "Commands.h"

// Original CONTROL score-history fixtures; no MIDI hardware/provider claim.
static void noteReleaseScoreLogTests(const juce::File &folder)
{
    using namespace vibera;
    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    pattern.autoLength = false;
    pattern.length = 4;
    pattern.notes.clear();
    const auto channel = project.channels.front().id, patternId = pattern.id;
    const auto before = project.state();
    const auto input = [&](ScoreLog &log, double seconds, int pitch, int status, int velocity,
                           unsigned source = 0, unsigned epoch = 1)
    {
        log.add({channel,
                 seconds,
                 1,
                 epoch,
                 {static_cast<unsigned char>(status), static_cast<unsigned char>(pitch),
                  static_cast<unsigned char>(velocity)},
                 source});
    };
    ScoreLog real;
    input(real, 10, 60, 0x90, 100);
    input(real, 10.25, 61, 0x90, 80);
    input(real, 10.5, 60, 0x80, 64);
    input(real, 10.75, 61, 0x80, 127);
    input(real, 11, 62, 0x90, 90);
    input(real, 11.25, 62, 0x90, 0);
    input(real, 11.5, 63, 0x90, 70);
    input(real, 11.75, 63, 0x80, 0);
    const auto notes = real.dump(12, 3, 120, project);
    require(notes.size() == 4 && notes[0].releaseVelocity == 64. / 127. && notes[1].releaseVelocity == 1 &&
                notes[2].releaseVelocity == 0 && notes[3].releaseVelocity == 0 && notes[0].beat == 0 &&
                notes[0].length == 1 && notes[1].beat == .5 && notes[1].length == 1 && notes[2].beat == 2 &&
                notes[2].length == .5 && notes[3].beat == 3 && notes[3].length == .5,
            "Score history preserves physical64/127 offs and neutral velocity-zero NoteOn/explicit0 without "
            "changing timing");
    for (const auto &note : notes)
        require(note.id == 0 && note.channel == channel && !note.step && !note.muted && note.pan == 0 &&
                    note.fineCents == 0 && note.shiftPercent == 0 && note.repeatMode == 0,
                "Score history changes no existing source identity or default expression metadata");
    require(project.state().isEquivalentTo(before), "Dump never changes source Project or its allocator");
    const auto cropped = real.dump(10.625, 2, 120, project);
    require(cropped.size() == 2 && cropped[0].length == 1 && cropped[0].releaseVelocity == 64. / 127. &&
                cropped[1].length == .75 && cropped[1].releaseVelocity == 0,
            "Historical right-edge truncation is synthetic0 while an earlier real physical end retains64");
    const auto leftCropped = real.dump(11.25, 1, 120, project);
    require(leftCropped.size() == 3 && leftCropped[0].beat == 0 && leftCropped[0].length == .5 &&
                leftCropped[0].releaseVelocity == 64. / 127. && leftCropped[1].releaseVelocity == 1,
            "Clipping only the historical start retains the actual physical end's release");

    ScoreLog retrigger;
    input(retrigger, 20, 60, 0x90, 100);
    input(retrigger, 21, 60, 0x90, 90);
    input(retrigger, 22, 60, 0x80, 127);
    const auto repeated = retrigger.dump(23, 10, 120, project);
    require(repeated.size() == 2 && repeated[0].length == 2 && repeated[0].releaseVelocity == 0 &&
                repeated[1].beat == 2 && repeated[1].length == 2 && repeated[1].releaseVelocity == 1,
            "Repeated note-on closes old score ownership with0; only the new physical off carries127");
    ScoreLog epoch;
    input(epoch, 20, 62, 0x90, 100, 0, 1);
    input(epoch, 21, 63, 0x90, 100, 0, 2);
    input(epoch, 22, 63, 0x80, 64, 0, 2);
    const auto epochNotes = epoch.dump(23, 10, 120, project);
    require(epochNotes.size() == 2 && epochNotes[0].pitch == 62 && epochNotes[0].releaseVelocity == 0 &&
                epochNotes[1].pitch == 63 && epochNotes[1].releaseVelocity == 64. / 127.,
            "Epoch replacement never fabricates a physical release for the old owner");
    ScoreLog interrupted;
    input(interrupted, 20, 60, 0x90, 100, 0);
    input(interrupted, 20, 61, 0x90, 100, 1);
    interrupted.interrupt(channel, 0, 21);
    input(interrupted, 21.5, 61, 0x80, 64, 1);
    const auto interruptedNotes = interrupted.dump(22, 10, 120, project);
    require(interruptedNotes.size() == 2 && interruptedNotes[0].pitch == 60 &&
                interruptedNotes[0].releaseVelocity == 0 && interruptedNotes[1].pitch == 61 &&
                interruptedNotes[1].releaseVelocity == 64. / 127.,
            "Source interruption uses0 without stealing the other input's real physical release");
    ScoreLog held;
    input(held, 20, 64, 0x90, 100);
    const auto preview = held.dump(21, 10, 120, project);
    require(preview.size() == 1 && preview[0].length == 2 && preview[0].releaseVelocity == 0,
            "Dump-held closure uses0 without claiming a physical key-up");
    input(held, 22, 64, 0x80, 127);
    const auto finished = held.dump(23, 10, 120, project);
    require(finished.size() == 1 && finished[0].length == 4 && finished[0].releaseVelocity == 1,
            "Dump's copied held closure never retires real ownership or duplicates the later release");
    held.prune(4000);
    require(held.dump(4000, 1800, 120, project).empty(),
            "Release metadata does not extend expired score history");

    Controller controller;
    controller.replace(project);
    CommandArgs args;
    args.pattern = patternId;
    args.recordedNotes = notes;
    args.enabled = false;
    require(controller.execute("notes.record", args).wasOk(),
            "Actual score Dump commit uses canonical recorded-note history");
    const auto committed = controller.project.state();
    const auto &stored = controller.project.pattern(patternId)->notes;
    require(stored.size() == 4 && stored[0].releaseVelocity == 64. / 127. && stored[1].releaseVelocity == 1 &&
                stored[2].releaseVelocity == 0 && stored[3].releaseVelocity == 0 &&
                stored[0].id == project.nextId && stored[1].id == project.nextId + 1,
            "Canonical Dump allocates fresh runtime IDs and preserves exact physical release data");
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(committed),
            "Score Release Dump one exact UndoRedo");
    const auto file = folder.getChildFile("release-score-dump.vibra");
    Project reopened;
    require(saveProject(controller.project, file).wasOk() && loadProject(file, reopened).wasOk() &&
                reopened.state().isEquivalentTo(committed),
            "Physical score Release survives actual project file roundtrip");
}
