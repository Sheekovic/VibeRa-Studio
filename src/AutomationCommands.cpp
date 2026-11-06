// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AutomationCommands.h"
#include <algorithm>

namespace vibera
{
namespace
{
AutomationSource independentAutomationSource(Project &project, const AutomationSource &original,
                                             const char *suffix, bool preserveLinks)
{
    auto copy = original;
    copy.id = project.allocate();
    if (copy.name.size() + std::char_traits<char>::length(suffix) <= 1024)
        copy.name += suffix;
    for (auto &point : copy.points)
        point.id = project.allocate();
    if (!preserveLinks)
        copy.links.clear();
    return copy;
}
} // namespace
juce::Result applyAutomationCommand(Project &project, const juce::String &id, const CommandArgs &a)
{
    auto fail = [] { return juce::Result::fail("Automation target or operation is unavailable"); };
    auto &doc = project.automation;
    auto *source = project.automationSource(a.target);
    auto placed = std::find_if(doc.placements.begin(), doc.placements.end(),
                               [&](const auto &p) { return p.id == a.target; });
    const bool sampler = isSamplerAutomationTargetCommand(id);
    const auto operation = sampler ? id.replace("automation.sampler.", "automation.") : id;
    SamplerAutomationTarget samplerTarget;
    if (sampler)
    {
        if (!a.samplerAutomationParameter)
            return juce::Result::fail("automation.invalidArguments: Explicit sampler parameter required");
        if (auto resolved = resolveSamplerAutomationTarget(
                project, a.channel, *a.samplerAutomationParameter, samplerTarget,
                operation == "automation.unlink" || operation == "automation.init.remove");
            resolved.failed())
            return resolved;
    }
    MixerAutomationTarget mixer;
    const bool mixerTarget = isMixerAutomationTargetCommand(id);
    if (mixerTarget)
        if (auto resolved = resolveMixerAutomationTarget(
                project, a.mixer, a.automationControl.value_or(MixerAutomationControl::gain), mixer,
                a.automationSendTarget, std::int64_t(a.channel));
            resolved.failed())
            return resolved;
    const bool native = id.startsWith("automation.plugin.");
    PluginSpec *plugin = native ? project.plugin(a.target) : nullptr;
    if (native)
    {
        if (!plugin || !a.binding || !validPluginBindings({&*a.binding, 1}))
            return fail();
        auto &bindings = plugin->automationBindings;
        const auto found = std::find_if(bindings.begin(), bindings.end(),
                                        [&](const auto &b) { return b.id == a.binding->id; });
        if (found != bindings.end() && (found->flags != a.binding->flags || found->steps != a.binding->steps))
            return juce::Result::fail("Native parameter capabilities changed; review its saved binding");
        if (operation != "automation.plugin.init.remove")
        {
            if (found == bindings.end())
                bindings.push_back(*a.binding);
            else
                *found = *a.binding; // Explicit user selection captures the current manual fallback.
        }
        else if (found == bindings.end())
            return fail();
    }
    const AutomationKey key =
        native    ? AutomationKey{AutomationParameter::pluginNormalized, plugin->id, a.binding->id}
        : sampler ? samplerTarget.key
                  : mixer.key;
    auto initialized = std::find_if(doc.initialValues.begin(), doc.initialValues.end(),
                                    [&](const auto &v) { return v.key == key; });
    if (operation == "automation.create" || operation == "automation.plugin.create")
    {
        const float current = native    ? a.binding->manual
                              : sampler ? float(samplerTarget.manual)
                                        : mixer.manual;
        const float minimum = native ? 0.f : sampler ? float(samplerTarget.parameter.minimum) : mixer.minimum;
        const float maximum = native ? 1.f : sampler ? float(samplerTarget.parameter.maximum) : mixer.maximum;
        const float normalized = (current - minimum) / (maximum - minimum);
        AutomationSource next;
        next.id = project.allocate();
        next.length = a.length;
        const auto nativeName = native && a.binding->name.isEmpty()
                                    ? "Parameter " + juce::String(juce::int64(a.binding->id))
                                : native ? a.binding->name
                                         : juce::String{};
        next.name = (native    ? plugin->name + " - " + nativeName
                     : sampler ? samplerTarget.name
                               : mixer.name)
                        .toStdString();
        next.points = {{project.allocate(), 0, normalized},
                       {project.allocate(), a.length, normalized}};
        next.links = {{key, minimum, maximum}};
        if (initialized == doc.initialValues.end())
            doc.initialValues.push_back({key, current});
        doc.placements.push_back({project.allocate(), next.id, a.beat, a.length, 0, a.lane, false});
        doc.sources.push_back(std::move(next));
    }
    else if (operation == "automation.init" || operation == "automation.init.remove" ||
             operation == "automation.plugin.init" || id == "automation.plugin.init.remove")
    {
        if (!operation.endsWith(".remove") && !native &&
            !(sampler ? samplerTarget.contains(a.value) : mixer.contains(a.value)))
            return juce::Result::fail("automation.invalidValue: Initialization is outside the target domain");
        if (operation.endsWith(".remove"))
        {
            if (initialized != doc.initialValues.end())
                doc.initialValues.erase(initialized);
        }
        else if (initialized == doc.initialValues.end())
            doc.initialValues.push_back({key, float(a.value)});
        else
            initialized->value = float(a.value);
    }
    else if (id.startsWith("automation.point."))
    {
        AutomationPoint *point = nullptr;
        if (operation != "automation.point.add")
        {
            source = nullptr;
            for (auto &candidate : doc.sources)
                for (auto &p : candidate.points)
                    if (p.id == a.target)
                    {
                        source = &candidate;
                        point = &p;
                    }
        }
        if (!source)
            return fail();
        if (operation == "automation.point.add")
        {
            if (std::any_of(source->points.begin(), source->points.end(),
                            [&](const auto &p) { return p.beat == a.beat; }))
                return juce::Result::fail("An automation point already exists at that beat");
            source->points.push_back({project.allocate(), a.beat, float(a.value)});
        }
        else if (!point)
            return fail();
        else if (operation == "automation.point.move")
        {
            const double oldLength = source->length;
            if (point == &source->points.front() && a.beat != 0)
                return fail();
            if (point == &source->points.back())
            {
                source->length = a.beat;
                for (auto &p : doc.placements)
                    if (p.source == source->id && p.offset + p.length == oldLength)
                        p.length = a.beat - p.offset;
            }
            point->beat = a.beat;
            point->value = float(a.value);
        }
        else if (operation == "automation.point.delete")
        {
            if (point == &source->points.front() || point == &source->points.back())
                return juce::Result::fail("Automation endpoint points cannot be deleted");
            const auto removed = point->id;
            std::erase_if(source->points, [&](const auto &p) { return p.id == removed; });
        }
        else if (operation == "automation.point.shape")
        {
            if (a.text == "single")
                point->incoming = AutomationShape::single;
            else if (a.text == "hold")
                point->incoming = AutomationShape::hold;
            else if (a.text == "smooth")
                point->incoming = AutomationShape::smooth;
            else if (a.text == "stairs")
                point->incoming = AutomationShape::stairs;
            else if (a.text == "pulse")
                point->incoming = AutomationShape::pulse;
            else if (a.text == "wave")
                point->incoming = AutomationShape::wave;
            else if (a.text == "double")
                point->incoming = AutomationShape::doubleCurve;
            else if (a.text == "smoothStairs")
                point->incoming = AutomationShape::smoothStairs;
            else
                return juce::Result::fail("Unsupported automation curve shape");
            point->tension = float(a.value);
        }
        else
            return fail();
        std::sort(source->points.begin(), source->points.end(),
                  [](const auto &p, const auto &q) { return p.beat < q.beat; });
    }
    else if (operation == "automation.source.clone")
    {
        if (!source)
            return juce::Result::fail("automation.sourceMissing: Existing Automation source ID required");
        if (a.text.isNotEmpty() && a.text != "clear" && a.text != "preserve")
            return juce::Result::fail("automation.invalidLinkPolicy: Expected clear or preserve");
        // No placement or initialized target is created: an unused curve cannot change playback.
        // Copy before growing sources, which invalidates source pointers. The canonical caller
        // validates capacity/identities/targets before committing this entire candidate.
        auto copy = independentAutomationSource(project, *source, " - Clone", a.text == "preserve");
        doc.sources.push_back(std::move(copy));
    }
    else if (operation == "automation.source.rename")
    {
        if (!source)
            return fail();
        source->name = a.text.toStdString();
    }
    else if (operation == "automation.source.delete")
    {
        if (!source)
            return fail();
        std::erase_if(doc.placements, [&](const auto &p) { return p.source == a.target; });
        std::erase_if(doc.sources, [&](const auto &s) { return s.id == a.target; });
    }
    else if (operation == "automation.link" || operation == "automation.unlink" ||
             operation == "automation.range")
    {
        if (!source || (!mixerTarget && !sampler))
            return fail();
        auto link = std::find_if(source->links.begin(), source->links.end(),
                                 [&](const auto &l) { return l.key == key; });
        if (operation == "automation.unlink")
        {
            if (link != source->links.end())
                source->links.erase(link);
        }
        else if (operation == "automation.range")
        {
            if (link == source->links.end())
                return fail();
            if (sampler ? (!samplerTarget.contains(a.beat) || !samplerTarget.contains(a.length))
                        : (!mixer.contains(a.beat) || !mixer.contains(a.length)))
                return juce::Result::fail("automation.invalidRange: Both endpoints must be in target units");
            link->minimum = float(a.beat);
            link->maximum = float(a.length);
        }
        else
        {
            if (link == source->links.end())
                source->links.push_back({key,
                                         sampler ? float(samplerTarget.parameter.minimum) : mixer.minimum,
                                         sampler ? float(samplerTarget.parameter.maximum) : mixer.maximum});
            if (initialized == doc.initialValues.end())
                doc.initialValues.push_back({key, sampler ? float(samplerTarget.manual) : mixer.manual});
        }
    }
    else if (operation == "automation.place")
    {
        if (!source)
            return fail();
        doc.placements.push_back({project.allocate(), source->id, a.beat, a.length, a.value, a.lane, false});
    }
    else
    {
        if (placed == doc.placements.end())
            return fail();
        if (operation == "automation.move")
        {
            placed->beat = a.beat;
            placed->length = a.length;
            placed->offset = a.value;
            placed->lane = a.lane;
        }
        else if (operation == "automation.mute")
            placed->muted = a.enabled;
        else if (operation == "automation.delete")
            doc.placements.erase(placed);
        else if (operation == "automation.duplicate")
        {
            auto copy = *placed;
            copy.id = project.allocate();
            copy.beat += copy.length;
            doc.placements.push_back(copy);
        }
        else if (operation == "automation.unique")
        {
            auto *original = project.automationSource(placed->source);
            if (!original)
                return fail();
            auto copy = independentAutomationSource(project, *original, " - Unique", false);
            placed->source = copy.id;
            doc.sources.push_back(std::move(copy));
        }
        else
            return fail();
    }
    return juce::Result::ok(); // Canonical caller validates and commits one snapshot/gesture.
}
} // namespace vibera
