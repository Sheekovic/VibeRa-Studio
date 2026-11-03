// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MidiScore.h"
#include "Commands.h"
namespace vibera
{
// Uses the existing canonical gesture pipeline; notifications keep hot playback edits incremental.
// useTempo also replaces the project tempo map and meter with the file's (same Undo step).
inline juce::Result applyMidiScore(Controller &controller, Id patternId, Id channelId, const MidiScore &score,
                                   bool blend, bool useTempo = false)
{
    if (controller.hasGesture())
        return juce::Result::fail("Finish the current edit before importing MIDI");
    const auto *pattern = controller.project.pattern(patternId);
    if (!pattern || !controller.project.channel(channelId) || score.notes.empty())
        return juce::Result::fail("MIDI import target or score is missing");
    CommandArgs args;
    args.pattern = patternId;
    args.channel = channelId;
    args.enabled = blend;
    args.recordedNotes = score.notes;
    args.recordedControllers = score.controllers;
    for (auto &note : args.recordedNotes)
        note.channel = 0; // Source track numbers are not Channels; notes.import targets channelId.
    for (auto &control : args.recordedControllers)
        control.channel = 0;
    if (useTempo && (!score.tempos.empty() || score.numerator != 0))
        args.importTempo = CommandArgs::ImportedTempo{score.tempos, score.numerator, score.denominator};
    auto result = controller.execute("notes.import", args);
    return result;
}
// One new Channel per MIDI track that has notes, each a settings clone of channelId named
// after its track, all in one Undo step (notes.importTracks).
inline juce::Result applyMidiScoreTracks(Controller &controller, Id patternId, Id channelId, const MidiScore &score,
                                         bool useTempo = false)
{
    if (controller.hasGesture())
        return juce::Result::fail("Finish the current edit before importing MIDI");
    if (!controller.project.pattern(patternId) || !controller.project.channel(channelId) || score.notes.empty())
        return juce::Result::fail("MIDI import target or score is missing");
    CommandArgs args;
    args.pattern = patternId;
    args.channel = channelId;
    args.recordedNotes = score.notes;
    args.recordedControllers = score.controllers;
    for (const auto &name : score.trackNames)
        args.importTrackNames.push_back(name);
    if (useTempo && (!score.tempos.empty() || score.numerator != 0))
        args.importTempo = CommandArgs::ImportedTempo{score.tempos, score.numerator, score.denominator};
    return controller.execute("notes.importTracks", args);
}
} // namespace vibera
