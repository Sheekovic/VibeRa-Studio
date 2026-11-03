// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
namespace vibera
{
// Control-thread planning only. The source remains unchanged until canonical commit.
struct NoteStretchPlan
{
    double anchor = 0, end = 0, minimumRatio = 0, maximumRatio = 0;
    std::vector<Note> notes;
};
juce::Result validateStretchRatio(double);
juce::Result planNoteStretch(const Project &, Id pattern, Id channel, const std::vector<Id> &, double anchor,
                             double ratio, NoteStretchPlan &);
bool pianoRightShiftDown(); // UI input adapter; never called from DSP.
} // namespace vibera
