// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <optional>
namespace vibera
{
// Musical coordinates. A MIDI pitch denotes the centre of its piano row.
struct NoteSliceLine
{
    double startBeat = 1, endBeat = 1, startPitch = -.5, endPitch = 127.5, grid = .25;
    bool allChannels = false;
    bool loopPreview = false;
};
struct NoteSliceCut
{
    Id id = 0, right = 0;
    size_t index = 0;
    double beat = 0;
};
inline constexpr double minimumSliceLength = 1. / 960.;
juce::var sliceLineSchema();
juce::Result validateSliceLine(const NoteSliceLine &);
juce::var sliceLineState(const NoteSliceLine &);
juce::Result parseSliceLine(const juce::var &, NoteSliceLine &);
juce::Result planNoteSlices(const Project &, Id pattern, Id channel, const std::vector<Id> &,
                            const NoteSliceLine &, std::vector<NoteSliceCut> &);
} // namespace vibera
