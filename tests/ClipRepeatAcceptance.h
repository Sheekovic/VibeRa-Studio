// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Project.h"
#include "../src/PatternChannelLoops.h"
#include "../src/PatternAutomation.h"

// A Pattern clip longer than its Pattern repeats the Pattern (familiar Playlist behavior).
namespace clip_repeat_acceptance
{
using namespace vibera;
inline void tests()
{
    auto p = Project::empty();
    auto &part = p.patterns.front();
    part.notes.clear();
    part.length = 4;
    part.autoLength = false;
    const Id channel = p.channels.front().id;
    Note note;
    note.id = p.allocate();
    note.channel = channel;
    note.beat = 0;
    note.length = .5;
    note.pitch = 60;
    note.velocity = .8;
    part.notes.push_back(note);
    Note late = note;
    late.id = p.allocate();
    late.beat = 3.5;
    late.length = 1; // Crosses the Pattern end: cut there on every repeat.
    part.notes.push_back(late);
    p.placements.clear();
    const Id clip = p.allocate();
    p.placements.push_back({clip, part.id, 0, 8, 12, 0});
    require(p.validate().wasOk(), "A clip longer than its Pattern is valid");

    auto events = sequence(p, false, 0);
    std::vector<double> starts;
    std::set<std::uint32_t> ordinals;
    for (const auto &e : events)
        if (e.note == note.id)
        {
            starts.push_back(e.beat);
            ordinals.insert(e.repeatOrdinal);
        }
    require(starts == std::vector<double>{8, 12, 16}, "A 12-beat clip plays a 4-beat Pattern three times");
    require(ordinals.size() == 3, "Each repeat is a distinct note occurrence");
    for (const auto &e : events)
        if (e.note == late.id)
            require(std::abs(e.length - .5) < 1e-9, "A note crossing the Pattern end is cut at each repeat");

    // An offset clip starts inside the Pattern, then repeats from its start.
    p.placements.front() = {clip, part.id, 0, 8, 6, 2};
    events = sequence(p, false, 0);
    starts.clear();
    for (const auto &e : events)
        if (e.note == note.id)
            starts.push_back(e.beat);
    require(starts == std::vector<double>{10}, "Offset 2 over 6 beats repeats once from the Pattern start");

    // A clip inside its Pattern behaves exactly as before.
    p.placements.front() = {clip, part.id, 0, 8, 3, 0};
    events = sequence(p, false, 0);
    int inside = 0;
    for (const auto &e : events)
        inside += e.note == note.id ? 1 : 0;
    require(inside == 1, "A clip shorter than its Pattern plays its window once");

    // The repeat helper: segment positions, lengths and offsets.
    std::vector<std::tuple<double, double, double>> segments;
    forEachClipRepeat(10, 1, 4, [&](double position, double length, double offset, std::uint32_t)
                      { segments.emplace_back(position, length, offset); });
    require(segments == std::vector<std::tuple<double, double, double>>{{0, 3, 1}, {3, 4, 0}, {7, 3, 0}},
            "Clip repeat segments follow the Pattern window");

    // Pattern automation repeats with the Pattern.
    AutomationSource source;
    source.id = p.allocate();
    source.name = "Gain";
    source.length = 4;
    AutomationPoint first, second;
    first.id = p.allocate();
    first.value = .2f;
    second.id = p.allocate();
    second.beat = 4;
    second.value = .8f;
    source.points = {first, second};
    p.automation.sources.push_back(source);
    part.automation.push_back({0, source.id, 0, 4, 0, 0, false});
    p.placements.front() = {clip, part.id, 0, 8, 12, 0};
    const auto audible = audibleAutomation(p, false, 0);
    int windows = 0;
    for (const auto &placed : audible.document.placements)
        windows += placed.source == source.id ? 1 : 0;
    require(windows == 3, "Pattern automation repeats with a longer clip");
}
} // namespace clip_repeat_acceptance
