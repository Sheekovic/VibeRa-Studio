// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PatternChannelLoops.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace vibera
{
struct PianoNoteOccurrence
{
    Note note;
    double sourceBeat = 0, sourceLength = 0;
    std::uint32_t iteration = 0;
    bool dormant = false, clippedEnd = false;
    bool repeated() const
    {
        return iteration != 0;
    }
};

// CONTROL/UI-only view of a validated score. Derived appearances precede raw
// originals so painting forward leaves originals on top; hit-test in reverse.
// Source cells/IDs remain authoritative: these rows never become model notes.
inline std::vector<PianoNoteOccurrence> pianoNoteOccurrences(const Project &project, const Pattern &pattern)
{
    std::map<Id, const Note *> originals;
    std::map<Id, double> periods;
    for (const auto &note : pattern.notes)
        originals.emplace(note.id, &note);
    for (const auto &loop : pattern.channelLoops)
        periods.emplace(loop.channel, channelLoopLength(project, pattern, loop.channel));
    auto expanded = rawPatternOccurrences(project, pattern);
    std::vector<PianoNoteOccurrence> result;
    result.reserve(expanded.size() + pattern.notes.size());
    for (const auto &appearance : expanded)
        if (appearance.iteration != 0)
        {
            const auto &source = *originals.at(appearance.note.id);
            auto draw = appearance.note;
            draw.length = std::min(source.length, pattern.length - draw.beat);
            result.push_back(
                {draw, source.beat, source.length, appearance.iteration, false, draw.length < source.length});
        }
    for (const auto &note : pattern.notes)
    {
        const auto found = periods.find(note.channel);
        const bool dormant = note.beat < 0 || note.beat >= pattern.length ||
                             (found != periods.end() && note.beat >= found->second);
        // Even a dormant or crossing raw note keeps its complete source length.
        result.push_back({note, note.beat, note.length, 0, dormant, false});
    }
    return result;
}

// Half-open beat intersection (or starts), inclusive pitches. Repeated appearances select
// the same real source ID once. Empty Channel filter means all Channels.
inline std::vector<Id> pianoOccurrenceRange(const std::vector<PianoNoteOccurrence> &notes, double start,
                                            double end, int lowPitch, int highPitch,
                                            const std::set<Id> &channels = {}, bool startsOnly = false)
{
    std::set<Id> selected;
    if (!std::isfinite(start) || !std::isfinite(end) || start >= end || lowPitch > highPitch)
        return {};
    for (const auto &row : notes)
        if ((channels.empty() || channels.contains(row.note.channel)) && row.note.pitch >= lowPitch &&
            row.note.pitch <= highPitch && row.note.beat < end &&
            (startsOnly ? row.note.beat >= start : row.note.beat + row.note.length > start))
            selected.insert(row.note.id);
    return {selected.begin(), selected.end()};
}
} // namespace vibera
