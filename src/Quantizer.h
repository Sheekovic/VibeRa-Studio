// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace vibera
{
inline juce::Result quantizerEditAdmission(bool armed, bool taking, bool projectIO, bool rendering,
                                           bool midiIO)
{
    return armed || taking || projectIO || rendering || midiIO
               ? juce::Result::fail("Finish recording or file operation before quantizing")
               : juce::Result::ok();
}
struct GrooveAnchor
{
    double phase, length, velocity;
};
struct GrooveMatch
{
    GrooveAnchor anchor;
    double beat;
    bool eligible;
};
inline GrooveMatch matchGroove(const std::vector<GrooveAnchor> &anchors, double cycle, double beat,
                               double sensitivity)
{
    const double repetition = std::floor(beat / cycle), phase = beat - repetition * cycle;
    const auto next = std::lower_bound(anchors.begin(), anchors.end(), phase,
                                       [](const auto &point, double value) { return point.phase < value; });
    const size_t right = next == anchors.end() ? 0 : size_t(next - anchors.begin());
    const size_t left = right == 0 ? anchors.size() - 1 : right - 1;
    double r = repetition * cycle + anchors[right].phase;
    if (next == anchors.end())
        r += cycle;
    double l = repetition * cycle + anchors[left].phase;
    if (right == 0 && next != anchors.end())
        l -= cycle;
    const bool chooseRight = r - beat <= beat - l;
    const double chosen = chooseRight ? r : l;
    const auto index = chooseRight ? right : left;
    const double distance = std::abs(beat - chosen), radius = (r - l) * .5;
    return {anchors[index], chosen, distance <= radius * sensitivity + 1.e-12};
}
// Original control-thread score transform. No DSP/callback access or template assets.
inline juce::Result quantizeNotes(Project &next, const Project &baseline, const CommandArgs &a,
                                  const juce::String &command)
{
    const auto *source = baseline.pattern(a.pattern);
    auto *target = next.pattern(a.pattern);
    const auto *channel = baseline.channel(a.channel);
    if (!source || !target || (a.ids.empty() && (!channel || channel->audioClip)))
        return juce::Result::fail("Select a playable Channel or valid note selection");
    if (!std::isfinite(a.length) || a.length < .03125 || a.length > 64)
        return juce::Result::fail("Quantizer grid must be 0.03125..64 quarter-note beats");
    for (auto value : {a.strength, a.sensitivity, a.durationStrength, a.velocityStrength})
        if (!std::isfinite(value) || value < 0 || value > 1)
            return juce::Result::fail("Quantizer strengths must be finite normalized values from 0 to 1");
    auto mode = command == "notes.quickQuantize"                        ? juce::String("duration")
                : command == "notes.quantizeStarts" || a.text.isEmpty() ? juce::String("leaveDuration")
                                                                        : a.text;
    if (mode != "duration" && mode != "end" && mode != "leaveDuration" && mode != "leaveEnd")
        return juce::Result::fail("Quantizer mode: duration, end, leaveDuration or leaveEnd");
    if (a.groove && command != "notes.quantize")
        return juce::Result::fail("Groove templates require the advanced notes.quantize command");
    std::vector<GrooveAnchor> starts, ends;
    if (a.groove)
    {
        if (auto valid = validateGroove(*a.groove); valid.failed())
            return valid;
        for (const auto &point : a.groove->points)
        {
            starts.push_back({point.beat, point.length, point.velocity});
            ends.push_back(
                {std::fmod(point.beat + point.length, a.groove->cycle), point.length, point.velocity});
        }
        std::stable_sort(ends.begin(), ends.end(),
                         [](const auto &a, const auto &b) { return a.phase < b.phase; });
        ends.erase(std::unique(ends.begin(), ends.end(),
                               [](const auto &a, const auto &b) { return a.phase == b.phase; }),
                   ends.end());
    }
    const std::set<Id> ids(a.ids.begin(), a.ids.end());
    if (ids.size() != a.ids.size())
        return juce::Result::fail("Quantizer selection contains duplicate identities");
    std::unordered_map<Id, const Note *> originals;
    originals.reserve(source->notes.size());
    for (const auto &note : source->notes)
        originals.emplace(note.id, &note);
    for (auto id : ids)
        if (!originals.contains(id))
            return juce::Result::fail("Quantizer note target is stale");
    const auto snap = [&](double value)
    {
        const double nearest = std::round(value / a.length) * a.length;
        return std::abs(nearest - value) <= a.length * .5 * a.sensitivity + 1.e-12 ? nearest : value;
    };
    // Restore preview targets from the original score, including notes removed by
    // an earlier rest preview. Retain other parts and original vector/identity order.
    std::unordered_map<Id, const Note *> current;
    for (const auto &note : target->notes)
        current.emplace(note.id, &note);
    std::vector<Note> result;
    result.reserve(std::max(target->notes.size(), source->notes.size()));
    for (const auto &original : source->notes)
    {
        const bool selected = ids.empty() ? original.channel == a.channel : ids.contains(original.id);
        if (!selected)
        {
            if (auto found = current.find(original.id); found != current.end())
                result.push_back(*found->second);
            continue;
        }
        auto note = original;
        std::optional<GrooveMatch> onset;
        if (a.groove)
            onset = matchGroove(starts, a.groove->cycle, original.beat, a.sensitivity);
        if (onset && onset->eligible && a.zeroVelocityDeletes && onset->anchor.velocity == 0)
            continue;
        const double snappedStart =
            onset ? (onset->eligible ? onset->beat : original.beat) : snap(original.beat);
        note.beat = std::max(0., original.beat + (snappedStart - original.beat) * a.strength);
        if (onset && onset->eligible)
            note.velocity =
                float(original.velocity + (onset->anchor.velocity - original.velocity) * a.velocityStrength);
        constexpr double minimum = .000001;
        if (mode == "duration")
        {
            const double length = onset ? (onset->eligible ? onset->anchor.length : original.length)
                                        : (snap(original.length) == 0 ? a.length : snap(original.length));
            note.length += (length - original.length) * a.durationStrength;
        }
        else if (mode == "end")
        {
            const double end = original.beat + original.length;
            const auto matched =
                a.groove ? std::optional<GrooveMatch>(matchGroove(ends, a.groove->cycle, end, a.sensitivity))
                         : std::nullopt;
            const double snappedEnd = matched ? (matched->eligible ? matched->beat : end) : snap(end);
            note.length = std::max(minimum, end + (snappedEnd - end) * a.durationStrength - note.beat);
        }
        else if (mode == "leaveEnd")
        {
            // Never move a short note beyond its preserved end.
            note.beat =
                std::min(note.beat, original.beat + original.length - std::min(minimum, original.length));
            note.length = original.beat + original.length - note.beat;
        }
        // A displaced step becomes a score note; exact sixteenths keep Rack identity.
        if (note.step && std::abs(note.beat * 4 - std::round(note.beat * 4)) > 1.e-9)
            note.step = false;
        result.push_back(note);
    }
    for (const auto &note : target->notes)
        if (!originals.contains(note.id))
            result.push_back(note);
    target->notes = std::move(result);
    return juce::Result::ok();
}
} // namespace vibera
