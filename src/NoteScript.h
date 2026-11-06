// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <juce_javascript/juce_javascript.h>
#include <array>
#include <map>
#include <set>

// Piano roll scripts (familiar "Tools > Scripting"): JavaScript that edits one Channel's part.
// The script sees `notes` (pitch, beat, length, velocity, pan, release, muted, selected, id),
// `patternLength`, `snap` and helpers; what remains in `notes` replaces the part. Notes keeping
// their `id` keep their other properties (fine pitch, mod X/Y, slide, colour...). Runs for at
// most 2 seconds, with no file, network or project access.
namespace vibera::note_script
{
inline constexpr int maximumNotes = 20000;
inline const char *prelude()
{
    return "var anySelected = notes.some(function (n) { return n.selected; });\n"
           "function selected(n) { return !anySelected || n.selected; }\n"
           "function addNote(pitch, beat, length, velocity) {\n"
           "  notes.push({pitch: pitch, beat: beat, length: length, velocity: velocity === undefined ? 0.78 : velocity}); }\n"
           "function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }\n";
}
// Builds the replacement part for `channel` in `pattern` (allocating new IDs from `project`).
inline juce::Result run(Project &project, Pattern &pattern, Id channel, const std::vector<Id> &selectedIds,
                        double snap, const juce::String &source, std::vector<Note> &output)
{
    if (source.trim().isEmpty() || source.length() > 200000)
        return juce::Result::fail("noteScript.invalid: An empty or oversized script");
    const std::set<Id> chosen(selectedIds.begin(), selectedIds.end());
    juce::Array<juce::var> list;
    std::map<Id, Note> originals;
    for (const auto &n : pattern.notes)
        if (n.channel == channel)
        {
            auto *o = new juce::DynamicObject();
            o->setProperty("id", juce::int64(n.id));
            o->setProperty("pitch", n.pitch);
            o->setProperty("beat", n.beat);
            o->setProperty("length", n.length);
            o->setProperty("velocity", n.velocity);
            o->setProperty("pan", n.pan);
            o->setProperty("release", n.releaseVelocity);
            o->setProperty("muted", n.muted);
            o->setProperty("selected", chosen.contains(n.id));
            list.add(juce::var(o));
            originals[n.id] = n;
        }
    juce::JavascriptEngine js;
    js.maximumExecutionTime = juce::RelativeTime::seconds(2);
    const auto setup = "var notes = " + juce::JSON::toString(list, true) + ";\nvar patternLength = " +
                       juce::String(pattern.length, 6) + ";\nvar snap = " + juce::String(snap > 0 ? snap : .25, 6) +
                       ";\n" + prelude();
    if (auto ready = js.execute(setup); ready.failed())
        return juce::Result::fail("noteScript.error: " + ready.getErrorMessage());
    if (auto ran = js.execute(source); ran.failed())
        return juce::Result::fail("noteScript.error: " + ran.getErrorMessage());
    juce::Result evaluated = juce::Result::ok();
    const auto text = js.evaluate("JSON.stringify(notes)", &evaluated);
    if (evaluated.failed())
        return juce::Result::fail("noteScript.error: " + evaluated.getErrorMessage());
    const auto result = juce::JSON::parse(text.toString());
    if (!result.isArray())
        return juce::Result::fail("noteScript.invalidResult: `notes` must stay an array");
    if (result.size() > maximumNotes)
        return juce::Result::fail("noteScript.limit: At most 20000 notes");
    std::set<Id> used;
    output.clear();
    for (const auto &item : *result.getArray())
    {
        if (!item.isObject())
            return juce::Result::fail("noteScript.invalidResult: Every note must be an object");
        const auto number = [&](const char *key, double fallback) -> double
        { return item.hasProperty(key) ? double(item[key]) : fallback; };
        Note note;
        const Id id = item.hasProperty("id") ? Id(juce::int64(item["id"])) : 0;
        if (const auto found = originals.find(id); found != originals.end() && used.insert(id).second)
            note = found->second; // Keeps fine pitch, mod X/Y, slide, colour group...
        else
            note.id = project.allocate();
        note.channel = channel;
        note.pitch = int(std::lround(number("pitch", note.pitch)));
        note.beat = number("beat", note.beat);
        note.length = number("length", note.length);
        note.velocity = float(number("velocity", note.velocity));
        note.pan = number("pan", note.pan);
        note.releaseVelocity = number("release", note.releaseVelocity);
        note.muted = item.hasProperty("muted") ? bool(item["muted"]) : note.muted;
        if (note.pitch < 0 || note.pitch > 127 || !std::isfinite(note.beat) || note.beat < 0 || note.beat > 100000 ||
            !std::isfinite(note.length) || note.length <= 0 || note.length > 100000 || !std::isfinite(note.velocity) ||
            note.velocity < 0 || note.velocity > 1 || !std::isfinite(note.pan) || std::abs(note.pan) > 1 ||
            !std::isfinite(note.releaseVelocity) || note.releaseVelocity < 0 || note.releaseVelocity > 1)
            return juce::Result::fail("noteScript.invalidResult: A note is outside pitch 0..127, beat/length >= 0, "
                                      "velocity/release 0..1 or pan -1..1");
        output.push_back(note);
    }
    return juce::Result::ok();
}
// Original example scripts shown in the Piano roll menu.
struct Example
{
    const char *name, *source;
};
inline const std::array<Example, 6> &examples()
{
    static const std::array<Example, 6> list{{
        {"Humanize timing and velocity",
         "notes.forEach(function (n) { if (!selected(n)) return;\n"
         "  n.beat = Math.max(0, n.beat + (Math.random() - 0.5) * snap * 0.25);\n"
         "  n.velocity = clamp(n.velocity + (Math.random() - 0.5) * 0.15, 0.05, 1); });"},
        {"Double an octave up",
         "notes.filter(selected).forEach(function (n) { if (n.pitch + 12 <= 127) addNote(n.pitch + 12, n.beat, n.length, n.velocity * 0.85); });"},
        {"Velocity ramp (soft to loud)",
         "var s = notes.filter(selected).sort(function (a, b) { return a.beat - b.beat; });\n"
         "s.forEach(function (n, i) { n.velocity = clamp(0.3 + 0.7 * (s.length > 1 ? i / (s.length - 1) : 1), 0, 1); });"},
        {"Reverse in time",
         "var s = notes.filter(selected); if (s.length) {\n"
         "  var lo = Math.min.apply(null, s.map(function (n) { return n.beat; }));\n"
         "  var hi = Math.max.apply(null, s.map(function (n) { return n.beat + n.length; }));\n"
         "  s.forEach(function (n) { n.beat = lo + hi - (n.beat + n.length); }); }"},
        {"Every other note quieter",
         "notes.filter(selected).sort(function (a, b) { return a.beat - b.beat; })\n"
         "  .forEach(function (n, i) { if (i % 2) n.velocity = clamp(n.velocity * 0.6, 0, 1); });"},
        {"Delete short notes (under one snap step)",
         "notes = notes.filter(function (n) { return !selected(n) || n.length >= snap - 1e-9; });"},
    }};
    return list;
}
} // namespace vibera::note_script
