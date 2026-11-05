// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "MixerRouting.h"
#include <set>

namespace vibera
{
// Control-thread mask. Ordinary solo retains downstream output paths; connected
// solo also retains transitive inputs. Master is a sink, never an input bridge.
inline std::set<int> mixerSoloMask(const Project &project, int insert, bool connected,
                                     const juce::String &groupId = {})
{
    std::set<int> mask{insert};
    if (groupId.isNotEmpty())
    {
        const auto group = std::find_if(project.mixerGroups.begin(), project.mixerGroups.end(),
                                         [&](const auto &g) { return g.id == groupId; });
        if (group != project.mixerGroups.end()) mask = std::set<int>(group->members.begin(), group->members.end());
    }
    std::set<int> upstream = mask;
    if (connected)
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto &m : project.mixer)
                for (const auto &r : m.routes)
                    if (r.target != 0 && upstream.count(r.target))
                        changed |= upstream.insert(m.index).second;
        }
    }
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto &m : project.mixer)
            if (mask.count(m.index))
                for (const auto &r : m.routes)
                    changed |= mask.insert(r.target).second;
    }
    mask.insert(upstream.begin(), upstream.end());
    return mask;
}
inline void reconcileMixerSolo(Project &project)
{
    if (!project.mixerSolo)
        return;
    auto mask = mixerSoloMask(project, project.mixerSolo->insert, project.mixerSolo->connected, project.mixerSolo->group);
    for (auto &m : project.mixer)
        m.muted = !mask.count(m.index);
}
inline juce::Result applyMixerControl(Project &project, const juce::String &id, const CommandArgs &a)
{
    if (id == "mixer.unmuteAll")
    {
        for (auto &insert : project.mixer)
            insert.muted = false;
        project.mixerSolo.reset();
        return juce::Result::ok();
    }
    auto it = std::find_if(project.mixer.begin(), project.mixer.end(),
                           [&](const auto &m) { return m.index == a.mixer; });
    if (it == project.mixer.end())
        return juce::Result::fail("Mixer insert is unavailable");
    if (id == "mixer.rename")
    {
        auto name = a.text.trim();
        if (name.isEmpty() || name.length() > 64 || name.containsAnyOf("\r\n\t"))
            return juce::Result::fail("Mixer name must contain1..64 characters without line breaks/tabs");
        it->name = name;
    }
    else if (id == "mixer.colour")
    {
        if (!std::isfinite(a.value) || a.value != std::floor(a.value) || a.value < 0 ||
            a.value > 4294967295. || (a.value != 0 && a.value < 4278190080.))
            return juce::Result::fail("Mixer colour must be0 (theme) or an opaque ARGB integer");
        it->colour = juce::uint32(a.value);
    }
    else if (id == "mixer.solo" || id == "mixer.soloConnected" || id == "mixer.unsolo")
    {
        if (id == "mixer.unsolo" || !a.enabled)
        {
            if (!project.mixerSolo)
                return juce::Result::ok();
            if (id != "mixer.unsolo" && project.mixerSolo->insert != a.mixer)
                return juce::Result::fail("Unsolo target does not own the current solo");
            for (auto &m : project.mixer)
                m.muted =
                    a.restore.value_or(true) &&
                    std::find(project.mixerSolo->mutedBefore.begin(), project.mixerSolo->mutedBefore.end(),
                              m.index) != project.mixerSolo->mutedBefore.end();
            project.mixerSolo.reset();
        }
        else
        {
            if (a.mixer == 0)
                return juce::Result::fail("Master cannot be soloed; use mute to silence the full output");
            if (!project.mixerSolo)
            {
                project.mixerSolo = Project::MixerSoloState{a.mixer, false, {}};
                for (const auto &m : project.mixer)
                    if (m.muted)
                        project.mixerSolo->mutedBefore.push_back(m.index);
            }
            project.mixerSolo->insert = a.mixer;
            project.mixerSolo->group.clear();
            project.mixerSolo->connected = id == "mixer.soloConnected";
        }
    }
    else if (id == "mixer.mute")
    {
        project.mixerSolo.reset(); // A manual mask edit ends the restore session, like Rack solo.
        it->muted = a.enabled;
    }
    else if (id == "mixer.stereoSeparation")
    {
        if (!std::isfinite(a.value) || a.value < -1 || a.value > 1)
            return juce::Result::fail("mixer.stereo.invalidValue: Separation requires a finite fraction -1..1");
        it->stereoSeparation = float(a.value);
    }
    else if (id == "mixer.eq")
    {
        // args.target band (0 low shelf, 1 mid peak, 2 high shelf), args.value gain dB,
        // args.offset frequency Hz (0 keeps the current frequency).
        if (a.target > 2)
            return juce::Result::fail("mixer.eq.invalidBand: Band is 0 (low), 1 (mid) or 2 (high)");
        const auto band = size_t(a.target);
        const float hz = a.offset > 0 ? float(a.offset) : it->eq.freq[band];
        if (!MixerEqSettings::valid(float(a.value), hz))
            return juce::Result::fail("mixer.eq.invalidValue: Gain is -18..18 dB and frequency 20..20000 Hz");
        it->eq.gain[band] = float(a.value);
        it->eq.freq[band] = hz;
    }
    else if (id == "mixer.eqReset")
        it->eq = {};
    else if (id == "mixer.phaseInvert") it->phaseInverted = a.enabled;
    else if (id == "mixer.stereoSwap") it->stereoSwapped = a.enabled;
    else if (id == "mixer.pan" || id == "mixer.gain")
    {
        if (!std::isfinite(a.value) || a.value < (id == "mixer.pan" ? -1. : 0.) ||
            a.value > (id == "mixer.pan" ? 1. : 2.))
            return juce::Result::fail("Mixer value is outside its finite range");
        (id == "mixer.pan" ? it->pan : it->gain) = float(a.value);
    }
    return juce::Result::ok();
}
} // namespace vibera
