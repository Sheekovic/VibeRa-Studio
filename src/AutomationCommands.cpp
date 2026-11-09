// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AutomationCommands.h"
#include "MixerRouting.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
    else if (operation == "automation.modulator" || operation == "automation.plugin.modulator" ||
             operation == "automation.modulator.set" || operation == "automation.modulator.enable" ||
             operation == "automation.modulator.delete")
    {
        // text: shape; value: period in beats; strength: depth 0..1; offset: phase 0..1.
        const bool adding = operation == "automation.modulator" || operation == "automation.plugin.modulator";
        auto &list = doc.modulators;
        auto found = adding ? std::find_if(list.begin(), list.end(), [&](const auto &m) { return m.link.key == key; })
                            : std::find_if(list.begin(), list.end(), [&](const auto &m) { return m.id == a.target; });
        if (adding && found != list.end())
            return juce::Result::fail("modulator.duplicateTarget: This control already has a modulator");
        if (!adding && found == list.end())
            return juce::Result::fail("modulator.missing: Existing modulator ID required");
        if (operation == "automation.modulator.delete")
        {
            list.erase(found);
            return juce::Result::ok();
        }
        if (operation == "automation.modulator.enable")
        {
            found->enabled = a.enabled;
            return juce::Result::ok();
        }
        const auto shape = modulatorShapeFromName((a.text.isEmpty() ? juce::String("sine") : a.text).toStdString());
        const double period = a.value == 0 ? 4. : a.value;
        if (!shape)
            return juce::Result::fail("modulator.invalidShape: sine, triangle, sawUp, sawDown, square, random, smoothRandom or follower");
        if (!std::isfinite(period) || period < minimumModulatorPeriod || period > maximumModulatorPeriod)
            return juce::Result::fail("modulator.invalidPeriod: Period is 1/64..1024 beats");
        if (!std::isfinite(a.strength) || a.strength < 0 || a.strength > 1)
            return juce::Result::fail("modulator.invalidDepth: Depth is 0..1 of the control's range");
        if (!std::isfinite(a.offset) || a.offset < 0 || a.offset >= 1)
            return juce::Result::fail("modulator.invalidPhase: Phase is 0..1 (below one cycle)");
        AutomationModulator next = adding ? AutomationModulator{} : *found;
        next.shape = *shape;
        if (*shape == ModulatorShape::follower)
        {
            // The source is chosen when the follower is added (target = source insert, 0 Master);
            // set keeps it. To follow another insert, delete and add again.
            if (adding)
            {
                if (a.target < 0 || a.target > std::numeric_limits<int>::max() || !mixerInsert(project, int(a.target)))
                    return juce::Result::fail("modulator.invalidSource: The follower source is an existing Mixer insert (target)");
                next.source = a.target;
            }
            else if (found->shape != ModulatorShape::follower)
                return juce::Result::fail("modulator.followerSource: Add a new follower modulator to choose its source");
        }
        next.period = period;
        next.depth = float(a.strength);
        next.phase = float(a.offset);
        if (adding)
        {
            if (!native && !mixerTarget)
                return fail();
            next.id = project.allocate();
            next.link = {key, native ? 0.f : mixer.minimum, native ? 1.f : mixer.maximum};
            const auto nativeName = native && a.binding->name.isEmpty()
                                        ? "Parameter " + juce::String(juce::int64(a.binding->id))
                                    : native ? a.binding->name
                                             : juce::String{};
            next.name = ("LFO - " + (native ? plugin->name + " - " + nativeName : mixer.name)).toStdString();
            list.push_back(std::move(next));
        }
        else
            *found = std::move(next);
    }
    else if (operation.startsWith("automation.macro.") || operation == "automation.plugin.macro.link")
    {
        // Macro knobs. add: text name. set: target macro, value 0..1. rename: target, text. delete:
        // target. link (Mixer/Channel target fields, or plugin target + binding): ids = {macro};
        // text "range" uses beat..length as the target values at 0 and 1, otherwise the full range.
        // unlink: target macro, value = target index.
        auto &macros = doc.macros;
        const auto findMacro = [&](Id macroId)
        { return std::find_if(macros.begin(), macros.end(), [&](const auto &m) { return m.id == macroId; }); };
        if (operation == "automation.macro.add")
        {
            if (macros.size() >= 256 || a.text.length() > 64)
                return juce::Result::fail("macro.invalid: At most 256 macros, names up to 64 characters");
            AutomationMacro macro;
            macro.id = project.allocate();
            macro.name = (a.text.isNotEmpty() ? a.text : "Macro " + juce::String(int(macros.size()) + 1)).toStdString();
            macros.push_back(std::move(macro));
            return juce::Result::ok();
        }
        const bool linking = operation == "automation.macro.link" || operation == "automation.plugin.macro.link";
        const auto found = findMacro(linking ? (a.ids.empty() ? 0 : a.ids.front()) : a.target);
        if (found == macros.end())
            return juce::Result::fail("macro.missing: Existing macro ID required");
        if (operation == "automation.macro.set")
        {
            if (!std::isfinite(a.value) || a.value < 0 || a.value > 1)
                return juce::Result::fail("macro.invalidValue: Macro value is 0..1");
            found->value = float(a.value);
        }
        else if (operation == "automation.macro.rename")
        {
            if (a.text.isEmpty() || a.text.length() > 64)
                return juce::Result::fail("macro.invalidName: 1..64 characters");
            found->name = a.text.toStdString();
        }
        else if (operation == "automation.macro.delete")
            macros.erase(found);
        else if (operation == "automation.macro.unlink")
        {
            const int index = int(a.value);
            if (index < 0 || index >= int(found->targets.size()))
                return juce::Result::fail("macro.missingTarget: Target index of the macro required");
            found->targets.erase(found->targets.begin() + index);
        }
        else if (linking)
        {
            if (!native && !mixerTarget)
                return fail();
            if (found->targets.size() >= 64)
                return juce::Result::fail("macro.tooManyTargets: At most 64 targets per macro");
            const float minimum = native ? 0.f : mixer.minimum, maximum = native ? 1.f : mixer.maximum;
            AutomationLink link{key, minimum, maximum};
            if (a.text == "range")
            {
                const bool ok = native ? (a.beat >= 0 && a.beat <= 1 && a.length >= 0 && a.length <= 1)
                                       : (mixer.contains(a.beat) && mixer.contains(a.length));
                if (!ok)
                    return juce::Result::fail("macro.invalidRange: Both range ends must be inside the target's values");
                link.minimum = float(a.beat);
                link.maximum = float(a.length);
            }
            found->targets.push_back(link);
        }
        else
            return fail();
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
    else if (operation == "automation.source.flip" || operation == "automation.source.normalize" ||
             operation == "automation.source.smooth" || operation == "automation.source.scale" ||
             operation == "automation.source.decimate")
    {
        // Articulator tools on one curve (every linked placement follows): flip vertically,
        // normalize to the full 0..1 range, turn every point smooth, or value * value + offset.
        if (!source)
            return juce::Result::fail("automation.sourceMissing: Existing Automation source ID required");
        auto &points = source->points;
        if (operation == "automation.source.flip")
            for (auto &p : points)
                p.value = 1 - p.value;
        else if (operation == "automation.source.smooth")
            for (size_t i = 1; i < points.size(); ++i)
            {
                points[i].incoming = AutomationShape::smooth;
                points[i].tension = 0;
            }
        else if (operation == "automation.source.decimate")
        {
            // FL "Decimate points": drop interior points that lie within the tolerance (value, 0 = 0.01)
            // of the straight line between the neighbours that are kept; first and last always stay.
            const double tolerance = a.value == 0 ? .01 : a.value;
            if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > .5)
                return juce::Result::fail("automation.invalidTolerance: Decimate tolerance is 0..0.5");
            if (points.size() < 3)
                return juce::Result::fail("automation.nothingToDecimate: The curve has no interior points");
            std::vector<AutomationPoint> kept{points.front()};
            for (size_t i = 1; i + 1 < points.size(); ++i)
            {
                const auto &left = kept.back(), &right = points[i + 1], &p = points[i];
                const double span = right.beat - left.beat;
                const double line = span > 0 ? left.value + (right.value - left.value) * (p.beat - left.beat) / span
                                             : left.value;
                if (std::abs(p.value - line) > tolerance || p.incoming != AutomationShape::single)
                    kept.push_back(p);
            }
            kept.push_back(points.back());
            if (kept.size() == points.size())
                return juce::Result::fail("automation.nothingToDecimate: No point lies on a straight line");
            points = std::move(kept);
        }
        else if (operation == "automation.source.normalize")
        {
            const auto [low, high] = std::minmax_element(points.begin(), points.end(),
                                                         [](const auto &x, const auto &y) { return x.value < y.value; });
            const float from = low->value, span = high->value - low->value;
            if (span <= 0)
                return juce::Result::fail("automation.flatCurve: A flat curve cannot be normalized");
            for (auto &p : points)
                p.value = (p.value - from) / span;
        }
        else
        {
            if (!std::isfinite(a.value) || !std::isfinite(a.offset) || std::abs(a.value) > 16 || std::abs(a.offset) > 1)
                return juce::Result::fail("automation.invalidScale: Multiply by -16..16 and offset by -1..1");
            for (auto &p : points)
                p.value = std::clamp(float(p.value * a.value + a.offset), 0.f, 1.f);
        }
    }
    else if (operation == "automation.merge")
    {
        // Merge Automation Clips (ids: placements, at least two): one new curve over their span
        // keeping the first clip's target links; each clip's visible window is copied in time
        // order (later clips win where they overlap). One clip replaces them on the first lane.
        std::vector<AutomationPlacement> chosen;
        for (const auto &p : doc.placements)
            if (std::find(a.ids.begin(), a.ids.end(), p.id) != a.ids.end())
                chosen.push_back(p);
        if (chosen.size() < 2 || chosen.size() != a.ids.size())
            return juce::Result::fail("automation.mergeTargets: Choose two or more Automation Clips");
        std::sort(chosen.begin(), chosen.end(), [](const auto &x, const auto &y) { return x.beat < y.beat; });
        const double start = chosen.front().beat;
        double end = start;
        for (const auto &p : chosen)
            end = std::max(end, p.beat + p.length);
        const auto *first = project.automationSource(chosen.front().source);
        if (!first)
            return fail();
        AutomationSource merged;
        merged.id = project.allocate();
        merged.length = end - start;
        merged.name = first->name + " - Merged";
        merged.colour = first->colour;
        merged.links = first->links;
        std::vector<std::pair<double, float>> curve;
        for (size_t k = 0; k < chosen.size(); ++k)
        {
            const auto &p = chosen[k];
            const auto *s = project.automationSource(p.source);
            if (!s)
                return fail();
            const double from = p.beat - start, to = from + p.length,
                         until = k + 1 < chosen.size() ? std::min(to, chosen[k + 1].beat - start) : to;
            std::erase_if(curve, [&](const auto &q) { return q.first >= from; });
            curve.push_back({from, automationSourceValueAt(*s, p.offset)});
            for (const auto &point : s->points)
            {
                const double at = from + point.beat - p.offset;
                if (at > from && at < until)
                    curve.push_back({at, point.value});
            }
            curve.push_back({until, automationSourceValueAt(*s, p.offset + (until - from))});
        }
        if (curve.back().first < merged.length)
            curve.push_back({merged.length, curve.back().second});
        double previous = -1;
        for (const auto &[beat, value] : curve)
            if (beat > previous + 1.e-9)
            {
                merged.points.push_back({project.allocate(), beat, std::clamp(value, 0.f, 1.f)});
                previous = beat;
            }
        merged.points.back().beat = merged.length;
        const int lane = chosen.front().lane;
        std::erase_if(doc.placements, [&](const auto &p)
                      { return std::find(a.ids.begin(), a.ids.end(), p.id) != a.ids.end(); });
        doc.placements.push_back({project.allocate(), merged.id, start, merged.length, 0, lane, false});
        doc.sources.push_back(std::move(merged));
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
