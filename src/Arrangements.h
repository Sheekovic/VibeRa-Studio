// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <set>

// Multiple Playlist arrangements. The current arrangement lives in the Project's own
// placement/marker fields (so playback, rendering and editing are unchanged); the others
// are stored in Project::arrangements and swapped in by arrangement.select.
namespace vibera::arrangements
{
inline juce::String currentName(const Project &p)
{
    return p.arrangementName.isNotEmpty() ? p.arrangementName : juce::String("Arrangement 1");
}

// Moves the active arrangement's data out of the Project (leaving it empty).
inline Arrangement takeActive(Project &p)
{
    Arrangement a;
    a.id = p.arrangementId != 0 ? p.arrangementId : p.allocate();
    a.name = currentName(p);
    a.placements = std::move(p.placements);
    a.audioPlacements = std::move(p.audioPlacements);
    a.automationPlacements = std::move(p.automation.placements);
    a.clipGroups = std::move(p.clipGroups);
    a.markers = std::move(p.markers);
    p.placements.clear();
    p.audioPlacements.clear();
    p.automation.placements.clear();
    p.clipGroups.clear();
    p.markers.clear();
    return a;
}

inline void makeActive(Project &p, Arrangement &&a)
{
    p.arrangementId = a.id;
    p.arrangementName = a.name;
    p.placements = std::move(a.placements);
    p.audioPlacements = std::move(a.audioPlacements);
    p.automation.placements = std::move(a.automationPlacements);
    p.clipGroups = std::move(a.clipGroups);
    p.markers = std::move(a.markers);
}

// A copy with fresh IDs for every clip, group and marker (groups follow their members).
inline Arrangement cloneWithNewIds(Project &p, const Arrangement &source, const juce::String &name)
{
    Arrangement copy = source;
    copy.id = p.allocate();
    copy.name = name;
    std::map<Id, Id> ids;
    for (auto &c : copy.placements)
        c.id = ids[c.id] = p.allocate();
    for (auto &c : copy.audioPlacements)
        c.id = ids[c.id] = p.allocate();
    for (auto &c : copy.automationPlacements)
        c.id = ids[c.id] = p.allocate();
    for (auto &g : copy.clipGroups)
    {
        g.id = p.allocate();
        for (auto &member : g.members)
            member = ids.count(member) ? ids[member] : member;
    }
    for (auto &m : copy.markers)
        m.id = p.allocate();
    return copy;
}

// Stored arrangements drop clips whose Pattern, Audio Channel or automation source is gone.
inline void pruneStored(Project &p)
{
    for (auto &a : p.arrangements)
    {
        std::erase_if(a.placements, [&](const auto &c) { return !p.pattern(c.pattern); });
        std::erase_if(a.audioPlacements, [&](const auto &c)
                      { return !p.channel(c.channel) || !p.channel(c.channel)->audioClip; });
        std::erase_if(a.automationPlacements, [&](const auto &c) { return !p.automationSource(c.source); });
        std::set<Id> alive;
        for (const auto &c : a.placements)
            alive.insert(c.id);
        for (const auto &c : a.audioPlacements)
            alive.insert(c.id);
        for (const auto &c : a.automationPlacements)
            alive.insert(c.id);
        for (auto &g : a.clipGroups)
            std::erase_if(g.members, [&](Id m) { return !alive.count(m); });
        std::erase_if(a.clipGroups, [](const auto &g) { return g.members.size() < 2; });
    }
}

inline juce::ValueTree toTree(const Arrangement &a)
{
    juce::ValueTree t("ARRANGEMENT");
    t.setProperty("id", juce::int64(a.id), nullptr);
    t.setProperty("name", a.name, nullptr);
    for (const auto &c : a.placements)
    {
        juce::ValueTree e("PLACEMENT");
        e.setProperty("id", juce::int64(c.id), nullptr);
        e.setProperty("pattern", juce::int64(c.pattern), nullptr);
        e.setProperty("lane", c.lane, nullptr);
        e.setProperty("beat", c.beat, nullptr);
        e.setProperty("length", c.length, nullptr);
        e.setProperty("offset", c.offset, nullptr);
        e.setProperty("muted", c.muted, nullptr);
        t.appendChild(e, nullptr);
    }
    for (const auto &c : a.audioPlacements)
    {
        juce::ValueTree e("AUDIO_PLACEMENT");
        e.setProperty("id", juce::int64(c.id), nullptr);
        e.setProperty("channel", juce::int64(c.channel), nullptr);
        e.setProperty("lane", c.lane, nullptr);
        e.setProperty("beat", c.beat, nullptr);
        e.setProperty("offset", c.offset, nullptr);
        e.setProperty("duration", c.duration, nullptr);
        e.setProperty("muted", c.muted, nullptr);
        e.setProperty("fadeIn", c.fadeIn, nullptr);
        e.setProperty("fadeOut", c.fadeOut, nullptr);
        t.appendChild(e, nullptr);
    }
    for (const auto &c : a.automationPlacements)
    {
        juce::ValueTree e("AUTOMATION_PLACEMENT");
        e.setProperty("id", juce::int64(c.id), nullptr);
        e.setProperty("source", juce::int64(c.source), nullptr);
        e.setProperty("lane", c.lane, nullptr);
        e.setProperty("beat", c.beat, nullptr);
        e.setProperty("length", c.length, nullptr);
        e.setProperty("offset", c.offset, nullptr);
        e.setProperty("muted", c.muted, nullptr);
        t.appendChild(e, nullptr);
    }
    for (const auto &g : a.clipGroups)
    {
        juce::ValueTree e("GROUP");
        e.setProperty("id", juce::int64(g.id), nullptr);
        juce::StringArray members;
        for (auto m : g.members)
            members.add(juce::String(juce::int64(m)));
        e.setProperty("members", members.joinIntoString(","), nullptr);
        t.appendChild(e, nullptr);
    }
    for (const auto &m : a.markers)
    {
        juce::ValueTree e("MARKER");
        e.setProperty("id", juce::int64(m.id), nullptr);
        e.setProperty("beat", m.beat, nullptr);
        e.setProperty("name", m.name, nullptr);
        t.appendChild(e, nullptr);
    }
    return t;
}

inline juce::Result fromTree(const juce::ValueTree &t, Arrangement &a)
{
    if (!t.hasType("ARRANGEMENT"))
        return juce::Result::fail("Invalid arrangement");
    a.id = juce::int64(t["id"]);
    a.name = t["name"].toString();
    for (const auto &e : t)
    {
        if (e.hasType("PLACEMENT"))
            a.placements.push_back({juce::int64(e["id"]), juce::int64(e["pattern"]), int(e["lane"]), double(e["beat"]),
                                    double(e["length"]), double(e["offset"]), bool(e["muted"])});
        else if (e.hasType("AUDIO_PLACEMENT"))
        {
            AudioPlacement c{juce::int64(e["id"]), juce::int64(e["channel"]), int(e["lane"]), double(e["beat"]),
                             double(e["offset"]), double(e["duration"]), bool(e["muted"])};
            c.fadeIn = double(e["fadeIn"]);
            c.fadeOut = double(e["fadeOut"]);
            a.audioPlacements.push_back(c);
        }
        else if (e.hasType("AUTOMATION_PLACEMENT"))
        {
            AutomationPlacement c;
            c.id = juce::int64(e["id"]);
            c.source = juce::int64(e["source"]);
            c.lane = int(e["lane"]);
            c.beat = double(e["beat"]);
            c.length = double(e["length"]);
            c.offset = double(e["offset"]);
            c.muted = bool(e["muted"]);
            a.automationPlacements.push_back(c);
        }
        else if (e.hasType("GROUP"))
        {
            ClipGroup g;
            g.id = juce::int64(e["id"]);
            for (const auto &m : juce::StringArray::fromTokens(e["members"].toString(), ",", ""))
                g.members.push_back(m.getLargeIntValue());
            a.clipGroups.push_back(std::move(g));
        }
        else if (e.hasType("MARKER"))
            a.markers.push_back({juce::int64(e["id"]), double(e["beat"]), e["name"].toString()});
        else
            return juce::Result::fail("Unknown arrangement data");
    }
    return juce::Result::ok();
}
} // namespace vibera::arrangements
