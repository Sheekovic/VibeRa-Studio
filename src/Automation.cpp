// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Automation.h"
#include "SamplerAutomationParameters.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace vibera
{
namespace
{
bool validKey(AutomationKey k)
{
    if (unsigned(k.kind) > unsigned(AutomationParameter::samplerModulation))
        return false;
    if (k.kind == AutomationParameter::samplerModulation)
        return k.owner > 0 && lookupSamplerAutomationParameter(k.parameter).has_value();
    if (k.kind == AutomationParameter::mixerSend)
        return k.owner > 0 && k.owner <= std::numeric_limits<int>::max() &&
               k.parameter <= std::uint32_t(std::numeric_limits<int>::max()) && k.owner != k.parameter;
    if (k.kind != AutomationParameter::pluginNormalized && k.parameter != 0)
        return false;
    return k.kind == AutomationParameter::mixerGain || k.kind == AutomationParameter::mixerPan ||
                   k.kind == AutomationParameter::mixerStereoSeparation
               ? k.owner >= 0 && k.owner <= std::numeric_limits<int>::max()
               : k.owner > 0;
}
bool validValue(AutomationKey key, float value)
{
    if (!std::isfinite(value))
        return false;
    if (key.kind == AutomationParameter::samplerModulation)
    {
        const auto parameter = lookupSamplerAutomationParameter(key.parameter);
        double physical = 0;
        return parameter && samplerAutomationFloatToPhysical(*parameter, value, physical) &&
               samplerAutomationValueValid(*parameter, physical);
    }
    if (key.kind == AutomationParameter::mixerPan || key.kind == AutomationParameter::channelPan ||
        key.kind == AutomationParameter::mixerStereoSeparation)
        return value >= -1 && value <= 1;
    return value >= 0 && value <= (key.kind == AutomationParameter::pluginNormalized ? 1.f : 2.f);
}
struct Clip
{
    const AutomationSource *source;
    const AutomationPlacement *placement;
    const AutomationLink *link;
};
struct Event
{
    double beat;
    int kind; // 0 end, 1 start, 2 point boundary; exact end/start is right-continuous.
    std::size_t clip;
};
AutomationProgram::Span segment(const Clip &clip, double beat, bool held)
{
    AutomationProgram::Span result;
    result.beat = beat;
    const auto local = held ? clip.placement->offset + clip.placement->length
                            : clip.placement->offset + beat - clip.placement->beat;
    const auto &points = clip.source->points;
    auto next = std::upper_bound(points.begin(), points.end(), local,
                                 [](double b, const auto &p) { return b < p.beat; });
    // Use the exact expression that produced a point event. Subtracting a large
    // placement beat from it can otherwise select the preceding Hold segment.
    while (!held && next != points.end() &&
           clip.placement->beat + next->beat - clip.placement->offset == beat)
        ++next;
    const auto &left = next == points.begin() ? points.front() : *std::prev(next);
    const double range = double(clip.link->maximum) - clip.link->minimum;
    result.coefficients[0] = clip.link->minimum + range * left.value;
    if (next != points.end() && next != points.begin())
    {
        result.origin = clip.placement->beat + left.beat - clip.placement->offset;
        result.inverseLength = 1. / (next->beat - left.beat);
        const double difference = range * (double(next->value) - left.value);
        switch (next->incoming)
        {
        case AutomationShape::hold:
            break;
        case AutomationShape::smooth:
            result.coefficients[2] = 3 * difference;
            result.coefficients[3] = -2 * difference;
            break;
        case AutomationShape::stairs:
        case AutomationShape::pulse:
        case AutomationShape::wave:
        case AutomationShape::doubleCurve:
        case AutomationShape::smoothStairs:
            result.shape = unsigned(next->incoming);
            result.from = result.coefficients[0];
            result.delta = difference;
            result.count = automationShapeCount(next->tension);
            break;
        case AutomationShape::single:
            // Original bounded monotonic quadratic, not copied FL spline mathematics.
            result.coefficients[1] = difference * (1 + next->tension);
            result.coefficients[2] = -difference * next->tension;
            break;
        }
        if (held)
        {
            const float value = result.valueAt(clip.placement->beat + clip.placement->length);
            result.coefficients[0] = value;
            result.coefficients[1] = result.coefficients[2] = result.coefficients[3] = 0;
            result.inverseLength = 0;
        }
    }
    return result;
}
} // namespace
float automationSourceValueAt(const AutomationSource &source, double beat) noexcept
{
    if (source.points.empty() || !std::isfinite(beat))
        return 0;
    const AutomationPlacement placement{0, source.id, 0, source.length, 0};
    const AutomationLink link{{AutomationParameter::mixerGain, 0, 0}, 0, 1};
    return segment({&source, &placement, &link}, std::clamp(beat, 0., source.length), false).valueAt(beat);
}
float AutomationProgram::Span::valueAt(double position) const noexcept
{
    if (inverseLength == 0)
        return float(coefficients[0]);
    const auto x = std::clamp((position - origin) * inverseLength, 0., 1.);
    if (shape == unsigned(AutomationShape::stairs))
        return float(from + delta * std::min(1., std::floor(x * count) / count));
    if (shape == unsigned(AutomationShape::pulse))
        return float(from + (std::fmod(x * count, 1.) < .5 ? 0. : delta));
    if (shape == unsigned(AutomationShape::wave))
        return float(from + delta * (.5 - .5 * std::cos(6.283185307179586 * x * count)));
    if (shape == unsigned(AutomationShape::doubleCurve))
    {
        // count (1..16) as the curve exponent: 1 linear, 4 a firm S, 16 nearly a step.
        const double p = std::max(1., count / 2.);
        const double s = x < .5 ? .5 * std::pow(2 * x, p) : 1 - .5 * std::pow(2 - 2 * x, p);
        return float(from + delta * s);
    }
    if (shape == unsigned(AutomationShape::smoothStairs))
    {
        const double steps = x * count, k = std::min(count - 1, std::floor(steps)), f = steps - k;
        const double eased = f * f * (3 - 2 * f); // Smoothstep inside each step.
        return float(from + delta * std::min(1., (k + eased) / count));
    }
    return float(((coefficients[3] * x + coefficients[2]) * x + coefficients[1]) * x + coefficients[0]);
}
float AutomationProgram::Lane::valueAt(double beat) const noexcept
{
    if (!std::isfinite(beat))
        return initial;
    auto next = std::upper_bound(spans.begin(), spans.end(), beat,
                                 [](double b, const Span &s) { return b < s.beat; });
    return next == spans.begin() ? initial : std::prev(next)->valueAt(beat);
}
const AutomationProgram::Lane *AutomationProgram::find(AutomationKey key) const noexcept
{
    auto i = std::lower_bound(lanes.begin(), lanes.end(), key,
                              [](const Lane &lane, AutomationKey k) { return lane.key < k; });
    return i != lanes.end() && i->key == key ? &*i : nullptr;
}
std::optional<float> AutomationProgram::valueAt(AutomationKey key, double beat) const noexcept
{
    if (auto *lane = find(key))
        return lane->valueAt(beat);
    return {};
}
std::span<const AutomationProgram::Lane> AutomationProgram::pluginLanes(std::int64_t owner) const noexcept
{
    if (owner <= 0)
        return {};
    const auto first = std::lower_bound(lanes.begin(), lanes.end(),
                                        AutomationKey{AutomationParameter::pluginNormalized, owner, 0},
                                        [](const Lane &lane, AutomationKey key) { return lane.key < key; });
    const auto last = std::upper_bound(first, lanes.end(),
                                       AutomationKey{AutomationParameter::pluginNormalized, owner,
                                                     std::numeric_limits<std::uint32_t>::max()},
                                       [](AutomationKey key, const Lane &lane) { return key < lane.key; });
    return first == last ? std::span<const Lane>{}
                         : std::span<const Lane>{&*first, std::size_t(last - first)};
}
std::size_t AutomationProgram::spanCount() const noexcept
{
    std::size_t count = 0;
    for (const auto &lane : lanes)
        count += lane.spans.size();
    return count;
}
bool AutomationProgram::compile(const AutomationDocument &doc, bool songMode, AutomationProgram &output,
                                std::string &error, std::span<const AutomationInitialValue> manualDefaults)
{
    error.clear();
    auto fail = [&](const char *message)
    {
        error = message;
        return false;
    };
    if (doc.sources.size() > 1024 || doc.placements.size() > 65536 || doc.initialValues.size() > 4096)
        return fail("Automation document exceeds prepared limits");
    std::set<std::int64_t> ids;
    auto unique = [&](std::int64_t id) { return id > 0 && ids.insert(id).second; };
    std::map<AutomationKey, float> initial;
    for (const auto &value : manualDefaults)
        if (!validKey(value.key) || !validValue(value.key, value.value) ||
            !initial.emplace(value.key, value.value).second)
            return fail("Invalid or duplicated manual automation default");
    std::set<AutomationKey> initialized;
    for (const auto &value : doc.initialValues)
        if (!validKey(value.key) || !validValue(value.key, value.value) ||
            !initialized.insert(value.key).second)
            return fail("Invalid or duplicated initialized automation target");
        else
            initial[value.key] = value.value;
    std::map<std::int64_t, const AutomationSource *> sources;
    std::size_t totalPoints = 0, totalLinks = 0;
    for (const auto &source : doc.sources)
    {
        totalPoints += source.points.size();
        totalLinks += source.links.size();
        if (!unique(source.id) || !std::isfinite(source.length) || source.length <= 0 ||
            source.length > 1.e6 || source.name.empty() || source.name.size() > 1024 ||
            (source.colour != 0 && (source.colour >> 24) != 255) || source.points.size() < 2 ||
            totalPoints > 1000000 || totalLinks > 4096 || source.points.front().beat != 0 ||
            source.points.back().beat != source.length)
            return fail("Invalid automation source or endpoint");
        double previous = -1;
        for (const auto &point : source.points)
        {
            if (!unique(point.id) || !std::isfinite(point.beat) || point.beat <= previous ||
                point.beat > source.length || !std::isfinite(point.value) || point.value < 0 ||
                point.value > 1 || !std::isfinite(point.tension) || std::abs(point.tension) > 1 ||
                unsigned(point.incoming) > unsigned(AutomationShape::smoothStairs) ||
                !std::isfinite(1. / (point.beat - previous)))
                return fail("Invalid automation point, curve or ordering");
            previous = point.beat;
        }
        std::set<AutomationKey> linked;
        for (const auto &link : source.links)
            if (!validKey(link.key) || !initial.contains(link.key) || !validValue(link.key, link.minimum) ||
                !validValue(link.key, link.maximum) || !linked.insert(link.key).second)
                return fail("Invalid automation link/range or missing initialization");
        sources.emplace(source.id, &source);
    }
    std::map<AutomationKey, std::vector<Clip>> clips;
    std::size_t linkedPlacements = 0;
    for (const auto &placement : doc.placements)
    {
        auto source = sources.find(placement.source);
        if (!unique(placement.id) || source == sources.end() || placement.lane < 0 || placement.lane > 127 ||
            !std::isfinite(placement.beat) || placement.beat < 0 || !std::isfinite(placement.length) ||
            placement.length <= 0 || !std::isfinite(placement.offset) || placement.offset < 0 ||
            placement.offset + placement.length > source->second->length + 1.e-9 ||
            placement.beat + placement.length > 1.e6)
            return fail("Invalid automation placement/source window");
        if (songMode && !placement.muted)
            for (const auto &link : source->second->links)
            {
                if (++linkedPlacements > 200000)
                    return fail("Automation linked placement limit exceeded");
                clips[link.key].push_back({source->second, &placement, &link});
            }
    }
    AutomationProgram result;
    std::size_t expandedEvents = 0;
    if (songMode)
        for (const auto &[key, value] : initial)
        {
            auto &list = clips[key];
            if (!initialized.contains(key) && list.empty())
                continue;
            Lane lane{key, value, {}};
            std::vector<Event> events;
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                const auto &clip = list[i];
                const auto &p = *clip.placement;
                const auto begin =
                    std::upper_bound(clip.source->points.begin(), clip.source->points.end(), p.offset,
                                     [](double beat, const auto &point) { return beat < point.beat; });
                const auto end = std::lower_bound(clip.source->points.begin(), clip.source->points.end(),
                                                  p.offset + p.length, [](const auto &point, double beat)
                                                  { return point.beat < beat; });
                const auto count = std::size_t(std::distance(begin, end)) + 2;
                if (count > 2000000 - expandedEvents)
                    return fail("Automation expansion exceeds prepared limits");
                expandedEvents += count;
                events.push_back({p.beat, 1, i});
                events.push_back({p.beat + p.length, 0, i});
                for (auto point = begin; point != end; ++point)
                    events.push_back({p.beat + point->beat - p.offset, 2, i});
            }
            std::sort(events.begin(), events.end(), [](const auto &a, const auto &b)
                      { return std::tie(a.beat, a.kind, a.clip) < std::tie(b.beat, b.kind, b.clip); });
            using Priority = std::tuple<double, std::int64_t, std::size_t>;
            std::set<Priority> active;
            std::optional<Priority> lastEnded;
            for (std::size_t i = 0; i < events.size();)
            {
                const double beat = events[i].beat;
                do
                {
                    const auto event = events[i++];
                    const auto &p = *list[event.clip].placement;
                    const Priority priority{p.beat, p.id, event.clip};
                    if (event.kind == 1)
                        active.insert(priority);
                    else if (event.kind == 0)
                    {
                        active.erase(priority);
                        Priority ended{event.beat, p.id, event.clip};
                        if (!lastEnded || ended > *lastEnded)
                            lastEnded = ended;
                    }
                } while (i < events.size() && events[i].beat == beat);
                if (!active.empty())
                    lane.spans.push_back(segment(list[std::get<2>(*active.rbegin())], beat, false));
                else if (lastEnded)
                    lane.spans.push_back(segment(list[std::get<2>(*lastEnded)], beat, true));
            }
            result.lanes.push_back(std::move(lane));
        }
    output = std::move(result);
    return true;
}
bool AutomationProgram::applyGain(AutomationKey key, float *const *channels, int channelCount, int frames,
                                  double beat, double beatsPerFrame) const noexcept
{
    if (!channels || channelCount < 1 || frames < 0 || !std::isfinite(beat) ||
        !std::isfinite(beatsPerFrame) || beatsPerFrame <= 0 ||
        (key.kind != AutomationParameter::mixerGain && key.kind != AutomationParameter::channelGain))
        return false;
    const auto *lane = find(key);
    if (!lane)
        return false;
    for (int ch = 0; ch < channelCount; ++ch)
        if (!channels[ch])
            return false;
    for (int frame = 0; frame < frames; ++frame)
    {
        const float gain = lane->valueAt(beat + frame * beatsPerFrame);
        for (int ch = 0; ch < channelCount; ++ch)
            channels[ch][frame] *= gain;
    }
    return true;
}
} // namespace vibera
