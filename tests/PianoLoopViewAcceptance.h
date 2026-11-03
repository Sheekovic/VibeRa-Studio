// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PianoLoopView.h"
#include <limits>

// Original literal model/view tests. No UI, callback, worker or audio evidence.
inline void pianoLoopViewTests()
{
    using namespace vibera;
    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    pattern.autoLength = false;
    pattern.length = 2.5;
    const auto channel = project.channels[0].id, other = project.channels[1].id;
    const auto sustained = project.allocate(), muted = project.allocate(), zero = project.allocate();
    const auto dormant = project.allocate(), outside = project.allocate(), foreign = project.allocate();
    pattern.notes = {
        {sustained, channel, 0, 1.5, 60, .7f, true}, {muted, channel, .75, 1, 64, .4f, true, true},
        {zero, channel, .25, .25, 67, 0, true},      {dormant, channel, 1, .5, 69, .6f, true},
        {outside, channel, 3, .5, 72, .8f, false},   {foreign, other, .5, .5, 55, .3f, false}};
    pattern.channelLoops = {{channel, ChannelLoopMode::fixed, 1}};
    const auto before = project.state();
    const auto nextId = project.nextId;
    auto rows = pianoNoteOccurrences(project, pattern);
    const auto find = [](const std::vector<PianoNoteOccurrence> &view, Id id,
                         std::uint32_t iteration) -> const PianoNoteOccurrence &
    {
        const auto found = std::find_if(view.begin(), view.end(), [&](const auto &row)
                                        { return row.note.id == id && row.iteration == iteration; });
        require(found != view.end(), "Find literal source/iteration in Piano loop view");
        return *found;
    };
    const auto repeatedBeats = [](const std::vector<PianoNoteOccurrence> &view, Id id)
    {
        std::vector<double> result;
        for (const auto &row : view)
            if (row.note.id == id && row.repeated())
                result.push_back(row.note.beat);
        return result;
    };
    require(rows.size() == 11 && repeatedBeats(rows, sustained) == std::vector<double>{1, 2} &&
                repeatedBeats(rows, muted) == std::vector<double>{1.75} &&
                repeatedBeats(rows, zero) == std::vector<double>{1.25, 2.25},
            "Fixed view has literal repeated onsets, including muted/zero sources and crossing notes");
    require(find(rows, sustained, 1).note.length == 1.5 && !find(rows, sustained, 1).clippedEnd &&
                find(rows, sustained, 2).note.length == .5 && find(rows, sustained, 2).clippedEnd &&
                find(rows, sustained, 2).sourceBeat == 0 && find(rows, sustained, 2).sourceLength == 1.5 &&
                find(rows, muted, 1).note.length == .75 && find(rows, muted, 1).clippedEnd &&
                find(rows, muted, 1).note.muted && find(rows, zero, 2).note.velocity == 0 &&
                !find(rows, zero, 2).clippedEnd,
            "Only repeated draw length clips at horizon; crossing loop-boundary duration and original "
            "metadata survive");
    require(find(rows, dormant, 0).dormant && find(rows, outside, 0).dormant &&
                !find(rows, sustained, 0).dormant && !find(rows, foreign, 0).dormant &&
                repeatedBeats(rows, dormant).empty() && repeatedBeats(rows, outside).empty() &&
                repeatedBeats(rows, foreign).empty(),
            "Dormant source on cycle boundary/outside Pattern remains visible; unrelated unlooped Channel "
            "never repeats");
    const auto rawStart = rows.size() - pattern.notes.size();
    for (size_t index = 0; index < rows.size(); ++index)
        require(rows[index].repeated() == (index < rawStart),
                "Every derived row precedes every raw row for original paint/hit priority");
    for (size_t index = 0; index < pattern.notes.size(); ++index)
    {
        const auto &row = rows[rawStart + index];
        const auto &source = pattern.notes[index];
        require(row.note.id == source.id && row.note.channel == source.channel &&
                    row.note.beat == source.beat && row.note.length == source.length &&
                    row.note.pitch == source.pitch && row.note.velocity == source.velocity &&
                    row.note.step == source.step && row.note.muted == source.muted &&
                    row.sourceBeat == source.beat && row.sourceLength == source.length && !row.clippedEnd,
                "Raw source rows preserve full original note/order/properties without synthetic IDs");
    }
    require(pianoOccurrenceRange(rows, 2, 2.25, 60, 60) == std::vector<Id>{sustained} &&
                pianoOccurrenceRange(rows, 1.75, 2, 64, 64) == std::vector<Id>{muted} &&
                pianoOccurrenceRange(rows, 2.25, 2.5, 67, 67) == std::vector<Id>{zero},
            "Intersection selects repeated tails and deduplicates overlapping occurrences by real source ID");
    require(pianoOccurrenceRange(rows, 2.5, 2.75, 64, 64).empty() &&
                pianoOccurrenceRange(rows, .5, .75, 64, 64).empty() &&
                pianoOccurrenceRange(rows, .75, 1, 55, 55) == std::vector<Id>{foreign},
            "Beat endpoints are half-open: touching start/end alone never selects, crossing tails do");
    require(pianoOccurrenceRange(rows, 1.1, 1.25, 60, 60, {}, true).empty() &&
                pianoOccurrenceRange(rows, 1.1, 1.25, 60, 60) == std::vector<Id>{sustained} &&
                pianoOccurrenceRange(rows, 1, 2, 60, 60, {}, true) == std::vector<Id>{sustained} &&
                pianoOccurrenceRange(rows, 1.25, 1.5, 67, 67, {channel}, true) == std::vector<Id>{zero},
            "Ruler starts and marquee intersections use distinct half-open criteria on the same source IDs");
    require(pianoOccurrenceRange(rows, 1, 1.5, 69, 69) == std::vector<Id>{dormant} &&
                pianoOccurrenceRange(rows, 3, 3.25, 72, 72) == std::vector<Id>{outside} &&
                pianoOccurrenceRange(rows, 0, 4, 0, 127, {other}) == std::vector<Id>{foreign} &&
                pianoOccurrenceRange(rows, 0, 4, 0, 127, {999999999}).empty(),
            "Range can select dormant originals and filters exact Channels without selection retargeting");
    require(pianoOccurrenceRange(rows, 0, 4, 60, 69) == std::vector<Id>{sustained, muted, zero, dormant} &&
                pianoOccurrenceRange(rows, 1, 1, 0, 127).empty() &&
                pianoOccurrenceRange(rows, 2, 1, 0, 127).empty() &&
                pianoOccurrenceRange(rows, 0, 4, 70, 60).empty() &&
                pianoOccurrenceRange(rows, 0, std::numeric_limits<double>::infinity(), 0, 127).empty() &&
                pianoOccurrenceRange(rows, std::numeric_limits<double>::quiet_NaN(), 4, 0, 127).empty(),
            "Inclusive pitch limits, sorted unique IDs and invalid/empty queries are deterministic");
    std::reverse(rows.begin(), rows.end());
    require(pianoOccurrenceRange(rows, 0, 4, 60, 69) == std::vector<Id>{sustained, muted, zero, dormant} &&
                project.nextId == nextId && project.state().isEquivalentTo(before),
            "Selection is paint-order independent and neither view operation changes model or allocator");

    // Original crossing note is never clipped, even without a loop.
    auto crossing = project;
    auto &crossPart = crossing.patterns.front();
    crossPart.channelLoops.clear();
    crossPart.notes = {{sustained, channel, 2, 3, 60, .5f, false}};
    const auto crossingBefore = crossing.state();
    auto crossingRows = pianoNoteOccurrences(crossing, crossPart);
    require(crossingRows.size() == 1 && crossingRows[0].note.length == 3 && !crossingRows[0].clippedEnd &&
                !crossingRows[0].dormant &&
                pianoOccurrenceRange(crossingRows, 4, 5, 60, 60) == std::vector<Id>{sustained} &&
                crossing.state().isEquivalentTo(crossingBefore),
            "Raw crossing tail retains its full editable extent beyond the Pattern draw horizon");

    auto automatic = Project::empty();
    automatic.numerator = 5;
    automatic.denominator = 4;
    auto &autoPart = automatic.patterns.front();
    autoPart.autoLength = false;
    autoPart.length = 4;
    autoPart.numerator = 3;
    autoPart.denominator = 8;
    const auto autoChannel = automatic.channels[0].id;
    const auto sounding = automatic.allocate(), ignored = automatic.allocate(), silent = automatic.allocate();
    autoPart.notes = {{sounding, autoChannel, 0, .625, 60, .8f, true},
                      {ignored, autoChannel, 3.5, 20, 72, .8f, true, true},
                      {silent, autoChannel, 3.75, 30, 74, 0, true}};
    autoPart.channelLoops = {{autoChannel, ChannelLoopMode::step, 1}};
    for (auto mode : {ChannelLoopMode::step, ChannelLoopMode::beat, ChannelLoopMode::bar})
    {
        autoPart.channelLoops.front().mode = mode;
        const auto opening = automatic.state();
        const auto view = pianoNoteOccurrences(automatic, autoPart);
        const std::vector<double> expected = mode == ChannelLoopMode::step
                                                 ? std::vector<double>{.75, 1.5, 2.25, 3, 3.75}
                                             : mode == ChannelLoopMode::beat ? std::vector<double>{1, 2, 3}
                                                                             : std::vector<double>{1.5, 3};
        require(repeatedBeats(view, sounding) == expected && find(view, ignored, 0).dormant &&
                    find(view, silent, 0).dormant && repeatedBeats(view, ignored).empty() &&
                    repeatedBeats(view, silent).empty() && automatic.state().isEquivalentTo(opening),
                "Automatic Step/Beat/Bar uses sounding ends and effective3/8 meter; muted/zero dormant data "
                "stays raw");
        if (mode == ChannelLoopMode::step)
            require(find(view, sounding, 5).note.length == .25 && find(view, sounding, 5).clippedEnd,
                    "Automatic final appearance clips only its displayed tail");
    }
    autoPart.numerator = autoPart.denominator = 0;
    autoPart.channelLoops.front().mode = ChannelLoopMode::bar;
    require(repeatedBeats(pianoNoteOccurrences(automatic, autoPart), sounding).empty(),
            "Inherited5/4 bar exceeds four-beat horizon without inventing repetitions");
    autoPart.channelLoops.front().mode = ChannelLoopMode::beat;
    autoPart.notes.front().length = .375;
    require(repeatedBeats(pianoNoteOccurrences(automatic, autoPart), sounding) ==
                std::vector<double>{1, 2, 3},
            "Inherited5/4 beat rounds the short source to one quarter-note unit");
    autoPart.numerator = 3;
    autoPart.denominator = 8;
    require(repeatedBeats(pianoNoteOccurrences(automatic, autoPart), sounding) ==
                std::vector<double>{.5, 1, 1.5, 2, 2.5, 3, 3.5},
            "The same short source uses a half-quarter beat unit under Pattern3/8 override");
    autoPart.numerator = autoPart.denominator = 0;
    autoPart.channelLoops.front().mode = ChannelLoopMode::step;
    autoPart.notes.resize(1);
    autoPart.notes.front().length = .5;
    require(repeatedBeats(pianoNoteOccurrences(automatic, autoPart), sounding) ==
                std::vector<double>{.5, 1, 1.5, 2, 2.5, 3, 3.5},
            "Exact automatic boundary does not add an extra empty step unit");
    autoPart.notes.front().muted = true;
    require(
        repeatedBeats(pianoNoteOccurrences(automatic, autoPart), sounding).size() == 15,
        "All-muted automatic Step retains its minimum unit and raw expansion includes inactive appearances");
    autoPart.notes.front().muted = false;
    autoPart.notes.front().velocity = 0;
    require(repeatedBeats(pianoNoteOccurrences(automatic, autoPart), sounding).size() == 15,
            "Zero-velocity-only source also uses minimum Step unit without losing the raw note");
    autoPart.notes.front().velocity = .8f;
    autoPart.notes.front().length = 10;
    const auto longRows = pianoNoteOccurrences(automatic, autoPart);
    require(longRows.size() == 1 && longRows.front().note.length == 10 && !longRows.front().dormant,
            "Automatic sounding-end analysis clips to Pattern horizon but never clips the raw source length");
}
