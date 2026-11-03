// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoteSlice.h"
#include "PianoLoopView.h"
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>
namespace vibera
{
juce::var sliceLineSchema()
{
    return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{
      "startBeat":{"type":"number","minimum":0,"maximum":1000000,"description":"Start quarter-note beat in the source Pattern, not Song time."},
      "endBeat":{"type":"number","minimum":0,"maximum":1000000,"description":"End quarter-note beat; may precede startBeat for reverse strokes."},
      "startPitch":{"type":"number","minimum":-0.5,"maximum":127.5,"description":"Continuous MIDI semitones; an integer is the row centre, edges are pitch +/-0.5."},
      "endPitch":{"type":"number","minimum":-0.5,"maximum":127.5,"description":"End continuous pitch; may precede startPitch."},
      "grid":{"type":"number","minimum":0,"maximum":64,"default":0.25,"description":"Snap each intersection to this beat grid. Zero bypasses; nonzero minimum1/960 beat."},
      "allChannels":{"type":"boolean","default":false,"description":"With empty IDs, include all Pattern parts (editable ghosts). False limits to the specified active Channel. Explicit IDs always bound targets."},
      "loopPreview":{"type":"boolean","default":false,"description":"Intersect displayed Channel-loop occurrences and edit their original source notes. Conflicting source cut positions are refused atomically."}
    },"required":["startBeat","endBeat","startPitch","endPitch"]})");
}
juce::Result validateSliceLine(const NoteSliceLine &line)
{
    for (auto beat : {line.startBeat, line.endBeat})
        if (!std::isfinite(beat) || beat < 0 || beat > 1000000)
            return juce::Result::fail("Slice beat endpoints must be 0..1000000 quarter-note beats");
    for (auto pitch : {line.startPitch, line.endPitch})
        if (!std::isfinite(pitch) || pitch < -.5 || pitch > 127.5)
            return juce::Result::fail("Slice pitch endpoints must be -0.5..127.5 MIDI semitones");
    if (!std::isfinite(line.grid) || (line.grid != 0 && (line.grid < minimumSliceLength || line.grid > 64)))
        return juce::Result::fail("Slice grid must be zero (unsnapped), or 1/960..64 beats");
    return juce::Result::ok();
}
juce::var sliceLineState(const NoteSliceLine &line)
{
    auto *o = new juce::DynamicObject;
    o->setProperty("startBeat", line.startBeat);
    o->setProperty("endBeat", line.endBeat);
    o->setProperty("startPitch", line.startPitch);
    o->setProperty("endPitch", line.endPitch);
    o->setProperty("grid", line.grid);
    o->setProperty("allChannels", line.allChannels);
    if (line.loopPreview)
        o->setProperty("loopPreview", true);
    return juce::var(o);
}
juce::Result parseSliceLine(const juce::var &value, NoteSliceLine &result)
{
    if (!value.isObject())
        return juce::Result::fail("Slice line must be an object");
    auto *o = value.getDynamicObject();
    for (const auto &p : o->getProperties())
        if (p.name.toString() != "startBeat" && p.name.toString() != "endBeat" &&
            p.name.toString() != "startPitch" && p.name.toString() != "endPitch" &&
            p.name.toString() != "grid" && p.name.toString() != "allChannels" &&
            p.name.toString() != "loopPreview")
            return juce::Result::fail("Unknown Slice line property");
    NoteSliceLine line;
    for (auto field : {"startBeat", "endBeat", "startPitch", "endPitch", "grid"})
    {
        const auto v = value[field];
        if (!value.hasProperty(field) && juce::String(field) == "grid")
            continue;
        if (!v.isInt() && !v.isInt64() && !v.isDouble())
            return juce::Result::fail("Slice endpoints/grid must be finite numbers");
        const double x = double(v);
        if (juce::String(field) == "startBeat")
            line.startBeat = x;
        else if (juce::String(field) == "endBeat")
            line.endBeat = x;
        else if (juce::String(field) == "startPitch")
            line.startPitch = x;
        else if (juce::String(field) == "endPitch")
            line.endPitch = x;
        else
            line.grid = x;
    }
    if (value.hasProperty("allChannels") && !value["allChannels"].isBool())
        return juce::Result::fail("Slice allChannels must be Boolean");
    line.allChannels = bool(value.getProperty("allChannels", false));
    if (value.hasProperty("loopPreview") && !value["loopPreview"].isBool())
        return juce::Result::fail("Slice loopPreview must be Boolean");
    line.loopPreview = bool(value.getProperty("loopPreview", false));
    if (auto r = validateSliceLine(line); r.failed())
        return r;
    result = line;
    return juce::Result::ok();
}
namespace
{
double sliceEpsilon(double beat, const Note &note)
{
    return 16 * std::numeric_limits<double>::epsilon() *
           std::max({1., std::abs(beat), std::abs(note.beat + note.length)});
}
bool interiorSliceBeat(double beat, const Note &note)
{
    const auto epsilon = sliceEpsilon(beat, note);
    return beat - note.beat + epsilon >= minimumSliceLength &&
           note.beat + note.length - beat + epsilon >= minimumSliceLength;
}
std::optional<double> sliceIntersection(const Note &note, const NoteSliceLine &line)
{
    const auto dy = line.endPitch - line.startPitch;
    double fraction = 0;
    if (std::abs(dy) < 1.e-12)
    {
        // Horizontal/point cuts use their first beat within the half-open row.
        if (line.startPitch < note.pitch - .5 || line.startPitch >= note.pitch + .5)
            return {};
    }
    else
    {
        auto a = (note.pitch - .5 - line.startPitch) / dy, b = (note.pitch + .5 - line.startPitch) / dy;
        if (a > b)
            std::swap(a, b);
        const auto low = std::max(0., a), high = std::min(1., b);
        if (high <= low)
            return {}; // Touching only a row edge is not a body intersection.
        fraction = juce::jlimit(low, high, (note.pitch - line.startPitch) / dy);
    }
    auto beat = line.startBeat + fraction * (line.endBeat - line.startBeat);
    if (line.grid > 0)
        beat = std::round(beat / line.grid) * line.grid;
    return interiorSliceBeat(beat, note) ? std::optional<double>(beat) : std::nullopt;
}
} // namespace
juce::Result planNoteSlices(const Project &project, Id patternId, Id channelId, const std::vector<Id> &ids,
                            const NoteSliceLine &line, std::vector<NoteSliceCut> &result)
{
    if (auto r = validateSliceLine(line); r.failed())
        return r;
    const auto *pattern = project.pattern(patternId);
    const auto *channel = project.channel(channelId);
    if (!pattern || !channel || channel->audioClip)
        return juce::Result::fail("Select an existing Pattern and playable Channel for slicing");
    std::unordered_set<Id> targets(ids.begin(), ids.end());
    if (targets.size() != ids.size())
        return juce::Result::fail("Slice target IDs must be distinct");
    if (ids.size() > 100000)
        return juce::Result::fail("Too many Slice targets");
    auto unmatched = targets;
    std::vector<NoteSliceCut> cuts;
    std::map<Id, size_t> sourceIndices;
    for (size_t index = 0; index < pattern->notes.size(); ++index)
    {
        const auto &note = pattern->notes[index];
        unmatched.erase(note.id);
        if (ids.empty() ? (!line.allChannels && note.channel != channelId) : !targets.contains(note.id))
            continue;
        if (line.loopPreview)
            sourceIndices.emplace(note.id, index);
        else if (const auto beat = sliceIntersection(note, line))
            cuts.push_back({note.id, 0, index, *beat});
    }
    if (!unmatched.empty())
        return juce::Result::fail("Slice target does not belong to the Pattern");
    if (line.loopPreview)
    {
        if (auto r = validateChannelLoops(project, *pattern); r.failed())
            return r;
        std::map<size_t, double> sourceCuts;
        for (const auto &row : pianoNoteOccurrences(project, *pattern))
        {
            const auto found = sourceIndices.find(row.note.id);
            if (found == sourceIndices.end())
                continue;
            const auto displayBeat = sliceIntersection(row.note, line);
            if (!displayBeat)
                continue;
            const auto sourceBeat = *displayBeat - (row.note.beat - row.sourceBeat);
            const auto &source = pattern->notes[found->second];
            if (!interiorSliceBeat(sourceBeat, source))
                continue;
            const auto [cut, inserted] = sourceCuts.emplace(found->second, sourceBeat);
            if (!inserted && std::abs(cut->second - sourceBeat) > sliceEpsilon(sourceBeat, source))
                return juce::Result::fail("loopPreview.ambiguousSlice: Overlapping occurrences imply "
                                          "different source cuts for Note " +
                                          juce::String(source.id));
        }
        for (const auto &[index, beat] : sourceCuts)
            cuts.push_back({pattern->notes[index].id, 0, index, beat});
    }
    if (pattern->notes.size() + cuts.size() > 100000)
        return juce::Result::fail("Slice exceeds the Pattern note limit");
    result = std::move(cuts);
    return juce::Result::ok();
}
} // namespace vibera
