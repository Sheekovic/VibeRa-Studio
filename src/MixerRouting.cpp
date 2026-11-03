// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MixerRouting.h"
#include <array>
#include <cmath>
#include <set>
namespace vibera
{
const MixerInsert *mixerInsert(const Project &p, int source)
{
    auto i = std::find_if(p.mixer.begin(), p.mixer.end(), [&](const auto &m) { return m.index == source; });
    return i == p.mixer.end() ? nullptr : &*i;
}
const MixerRoute *mixerRoute(const Project &p, int source, int target)
{
    if (auto *m = mixerInsert(p, source))
        for (const auto &r : m->routes)
            if (r.target == target)
                return &r;
    return nullptr;
}
juce::Result canMixerRoute(const Project &p, int source, int target)
{
    if (source <= 0 || !mixerInsert(p, source))
        return juce::Result::fail("Select a non-Master source insert");
    if (target < 0 || !mixerInsert(p, target))
        return juce::Result::fail("Mixer send destination is unavailable");
    std::set<int> seen;
    auto reaches = [&](auto &&self, int node) -> bool
    {
        if (node == source)
            return true;
        if (node < 0 || !seen.insert(node).second)
            return false;
        if (auto *m = mixerInsert(p, node))
            for (const auto &r : m->routes)
                if (self(self, r.target))
                    return true;
        // Detector PCM is an AUDIO dependency even when currently muted,
        // bypassed or wet-zero. Never infer topology from observed silence.
        for (const auto &row : p.mixer)
            for (const auto &effect : row.effects)
                if (effect.detector && effect.detector->sourceMixer == node && self(self, row.index))
                    return true;
        return false;
    };
    if (reaches(reaches, target))
        return juce::Result::fail("Mixer routing would create a feedback cycle");
    return juce::Result::ok();
}
juce::Result validateMixerRouting(const Project &p)
{
    for (const auto &m : p.mixer)
    {
        if (m.index == 0 && !m.routes.empty())
            return juce::Result::fail("Master cannot send to an insert");
        if (m.routes.size() > 128)
            return juce::Result::fail("Too many Mixer sends");
        std::set<int> targets;
        for (const auto &r : m.routes)
        {
            if (!targets.insert(r.target).second || !std::isfinite(r.gain) || r.gain < 0 || r.gain > 2)
                return juce::Result::fail("Mixer sends need unique destinations and finite linear gains0..2");
            if (auto result = canMixerRoute(p, m.index, r.target); result.failed())
                return result;
        }
    }
    for (const auto &row : p.mixer)
        for (const auto &effect : row.effects)
            if (effect.detector)
            {
                const auto &detector = *effect.detector;
                if (effect.instrument || !detector.valid())
                    return juce::Result::fail(
                        "sidechain.invalidAssignment: Effect-only mono/stereo auxiliary input required");
                if (!mixerInsert(p, detector.sourceMixer))
                    return juce::Result::fail(
                        "sidechain.sourceUnavailable: Referenced source insert cannot be removed or missing");
                if (detector.sourceMixer == row.index)
                    return juce::Result::fail(
                        "sidechain.selfReference: Detector source must differ from its effect insert");
                if (auto cycle = canMixerRoute(p, detector.sourceMixer, row.index); cycle.failed())
                    return juce::Result::fail("sidechain.cycle: " + cycle.getErrorMessage());
            }
    return juce::Result::ok();
}
juce::Result setMixerRoute(Project &p, int source, int target, bool enabled, bool only)
{
    if (source <= 0 || !mixerInsert(p, source) || target < 0 || !mixerInsert(p, target) || source == target)
        return juce::Result::fail(
            "Select distinct valid source and destination inserts; Master is destination only");
    if (enabled)
        if (auto r = canMixerRoute(p, source, target); r.failed())
            return r;
    auto *m = const_cast<MixerInsert *>(mixerInsert(p, source));
    const auto *old = mixerRoute(p, source, target);
    if ((!only && enabled && old) || (!enabled && !old) || (only && enabled && old && m->routes.size() == 1))
        return juce::Result::ok();
    // Refuse before any route mutation: no dangling target or implicit retarget.
    for (const auto &edge : m->routes)
        if (only ? (!enabled || edge.target != target) : (!enabled && edge.target == target))
        {
            const AutomationKey key{AutomationParameter::mixerSend, source, std::uint32_t(edge.target)};
            bool referenced =
                std::any_of(p.automation.initialValues.begin(), p.automation.initialValues.end(),
                            [&](const auto &value) { return value.key == key; });
            for (const auto &curve : p.automation.sources)
                referenced = referenced || std::any_of(curve.links.begin(), curve.links.end(),
                                                       [&](const auto &link) { return link.key == key; });
            if (referenced)
                return juce::Result::fail("automation.routeReferenced: Unlink and remove send initialization "
                                          "before removing this route");
        }
    const float level = old ? old->gain : 1.f;
    if (only)
        m->routes.clear();
    else
        std::erase_if(m->routes, [&](const auto &r) { return r.target == target; });
    if (enabled)
        m->routes.push_back({target, level});
    std::sort(m->routes.begin(), m->routes.end(),
              [](const auto &a, const auto &b) { return a.target < b.target; });
    return juce::Result::ok();
}
juce::Result setMixerSend(Project &p, int source, int target, double gain)
{
    if (!std::isfinite(gain) || gain < 0 || gain > 2)
        return juce::Result::fail("Send gain must be a finite linear factor0..2 (1=unity)");
    auto *r = const_cast<MixerRoute *>(mixerRoute(p, source, target));
    if (!r || source == 0)
        return juce::Result::fail("Enable the Mixer route before adjusting its level");
    r->gain = float(gain);
    return juce::Result::ok();
}
juce::Result setPluginDetector(Project &project, Id effectId,
                               std::optional<PluginDetectorAssignment> assignment)
{
    auto candidate = project; // Fallible resources prepared before changing the supplied model.
    PluginSpec *target = nullptr;
    for (auto &row : candidate.mixer)
        for (auto &effect : row.effects)
            if (effect.id == effectId && !effect.instrument)
                target = &effect;
    if (!target)
        return juce::Result::fail("sidechain.unavailableTarget: Exact existing Mixer effect required");
    target->detector = assignment;
    if (auto valid = candidate.validate(); valid.failed())
        return valid;
    project = std::move(candidate);
    return juce::Result::ok();
}
juce::var pluginDetectorState(const Project &project, Id effectId)
{
    auto *out = new juce::DynamicObject;
    const PluginSpec *effect = nullptr;
    int owner = -1;
    for (const auto &row : project.mixer)
        for (const auto &slot : row.effects)
            if (slot.id == effectId && !slot.instrument)
            {
                effect = &slot;
                owner = row.index;
            }
    out->setProperty("effect", juce::int64(effectId));
    out->setProperty("exists", effect != nullptr);
    out->setProperty("ownerMixer", owner);
    out->setProperty("connected", effect && effect->detector.has_value());
    if (effect && effect->detector)
    {
        out->setProperty("sourceMixer", effect->detector->sourceMixer);
        out->setProperty("inputBus", effect->detector->inputBus);
        out->setProperty("channels", effect->detector->channels);
    }
    return juce::var(out); // Saved intent only; actual native availability is independent.
}
juce::var mixerDetectorReferences(const Project &project, int sourceMixer)
{
    juce::Array<juce::var> references;
    for (const auto &row : project.mixer)
        for (const auto &effect : row.effects)
            if (effect.detector && effect.detector->sourceMixer == sourceMixer)
            {
                auto *entry = new juce::DynamicObject;
                entry->setProperty("effect", juce::int64(effect.id));
                entry->setProperty("ownerMixer", row.index);
                entry->setProperty("inputBus", effect.detector->inputBus);
                entry->setProperty("channels", effect.detector->channels);
                references.add(juce::var(entry));
            }
    return references;
}
juce::var mixerRoutesState(const Project &p, int source, bool availability)
{
    juce::Array<juce::var> rows;
    if (auto *m = mixerInsert(p, source))
        for (const auto &destination : p.mixer)
        {
            const auto *route = mixerRoute(p, source, destination.index);
            if (!availability && !route)
                continue;
            auto *o = new juce::DynamicObject;
            o->setProperty("target", destination.index);
            o->setProperty("name", destination.name);
            o->setProperty("enabled", route != nullptr);
            o->setProperty("gain", route ? route->gain : 0.f);
            if (availability)
            {
                auto r = canMixerRoute(p, source, destination.index);
                o->setProperty("available", r.wasOk());
                o->setProperty("reason", r.getErrorMessage());
            }
            rows.add(juce::var(o));
        }
    return rows;
}
} // namespace vibera
