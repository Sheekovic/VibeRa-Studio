// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#include "AutomationState.h"
#include "SamplerAutomationParameters.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace vibera
{
namespace
{
bool fields(const juce::ValueTree &tree, std::initializer_list<const char *> names)
{
    if (tree.getNumProperties() != int(names.size()))
        return false;
    for (const auto *name : names)
        if (!tree.hasProperty(name))
            return false;
    return true;
}
bool integer(const juce::var &value, std::int64_t &number)
{
    if (value.isInt() || value.isInt64())
    {
        number = juce::int64(value);
        return true;
    }
    if (!value.isString())
        return false;
    const auto text = value.toString().toStdString();
    const auto result = vibera::number_text::fromChars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool number(const juce::var &value, double &number)
{
    if (value.isDouble() || value.isInt() || value.isInt64())
        number = double(value);
    else if (value.isString())
    {
        const auto text = value.toString().toStdString();
        const auto result =
            vibera::number_text::fromChars(text.data(), text.data() + text.size(), number, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            return false;
    }
    else
        return false;
    return std::isfinite(number);
}
bool scalar(const juce::var &v, float &f)
{
    double n;
    if (!number(v, n) || std::abs(n) > std::numeric_limits<float>::max())
        return false;
    f = float(n);
    return true;
}
bool key(const juce::ValueTree &tree, AutomationKey &out, int projectVersion)
{
    std::int64_t kind = 0, member = 0;
    if (!integer(tree["kind"], kind) || kind < 0 || kind > 7 || !integer(tree["owner"], out.owner) ||
        !integer(tree["parameter"], member) || member < 0 ||
        member > std::numeric_limits<std::uint32_t>::max())
        return false;
    if (projectVersion < 22 && (kind == 1 || kind == 5)) return false;
    if (projectVersion < 25 && kind == 6)
        return false;
    if (kind == 7 &&
        (projectVersion < 38 || out.owner <= 0 || !lookupSamplerAutomationParameter(std::uint32_t(member))))
        return false;
    out.kind = AutomationParameter(kind);
    out.parameter = std::uint32_t(member);
    return true;
}
// Preserve strict original double input before narrowing to existing float storage.
// A writer-emitted float endpoint encoding is also legal, exactly (no epsilon).
bool targetScalar(const juce::var &value, AutomationKey key, float &out)
{
    if (key.kind != AutomationParameter::samplerModulation)
        return scalar(value, out);
    const auto parameter = lookupSamplerAutomationParameter(key.parameter);
    double input = 0;
    if (!parameter || !number(value, input))
        return false;
    const bool endpointEncoding =
        input == double(float(parameter->minimum)) || input == double(float(parameter->maximum));
    if (!samplerAutomationValueValid(*parameter, input) && !endpointEncoding)
        return false;
    const float stored = float(input);
    double physical = 0;
    if (!samplerAutomationFloatToPhysical(*parameter, stored, physical) ||
        !samplerAutomationValueValid(*parameter, physical))
        return false;
    out = stored;
    return true;
}
void putKey(juce::ValueTree &tree, AutomationKey k)
{
    tree.setProperty("kind", int(k.kind), nullptr);
    tree.setProperty("owner", juce::int64(k.owner), nullptr);
    tree.setProperty("parameter", juce::int64(k.parameter), nullptr);
}
} // namespace
juce::ValueTree automationState(const AutomationDocument &doc)
{
    juce::ValueTree root("AUTOMATION"), sources("SOURCES"), placements("PLACEMENTS"),
        initial("INITIAL_VALUES");
    for (const auto &source : doc.sources)
    {
        juce::ValueTree s("SOURCE"), points("POINTS"), links("LINKS");
        s.setProperty("id", juce::int64(source.id), nullptr);
        s.setProperty("length", source.length, nullptr);
        s.setProperty("name", juce::String::fromUTF8(source.name.c_str()), nullptr);
        s.setProperty("colour", juce::int64(source.colour), nullptr);
        for (const auto &point : source.points)
        {
            juce::ValueTree p("POINT");
            p.setProperty("id", juce::int64(point.id), nullptr);
            p.setProperty("beat", point.beat, nullptr);
            p.setProperty("value", double(point.value), nullptr);
            p.setProperty("shape", int(point.incoming), nullptr);
            p.setProperty("tension", double(point.tension), nullptr);
            points.appendChild(p, nullptr);
        }
        for (const auto &link : source.links)
        {
            juce::ValueTree l("LINK");
            putKey(l, link.key);
            l.setProperty("minimum", double(link.minimum), nullptr);
            l.setProperty("maximum", double(link.maximum), nullptr);
            links.appendChild(l, nullptr);
        }
        s.appendChild(points, nullptr);
        s.appendChild(links, nullptr);
        sources.appendChild(s, nullptr);
    }
    for (const auto &placement : doc.placements)
    {
        juce::ValueTree p("PLACEMENT");
        p.setProperty("id", juce::int64(placement.id), nullptr);
        p.setProperty("source", juce::int64(placement.source), nullptr);
        p.setProperty("beat", placement.beat, nullptr);
        p.setProperty("length", placement.length, nullptr);
        p.setProperty("offset", placement.offset, nullptr);
        p.setProperty("lane", placement.lane, nullptr);
        p.setProperty("muted", placement.muted, nullptr);
        placements.appendChild(p, nullptr);
    }
    for (const auto &value : doc.initialValues)
    {
        juce::ValueTree v("INITIAL");
        putKey(v, value.key);
        v.setProperty("value", double(value.value), nullptr);
        initial.appendChild(v, nullptr);
    }
    root.appendChild(sources, nullptr);
    root.appendChild(placements, nullptr);
    root.appendChild(initial, nullptr);
    return root;
}
juce::Result parseAutomationState(const juce::ValueTree &root, AutomationDocument &out, int projectVersion)
{
    auto fail = []
    { return juce::Result::fail("Malformed or unsupported automation data; project was not changed"); };
    if (!root.hasType("AUTOMATION") || root.getNumProperties() != 0 || root.getNumChildren() != 3)
        return fail();
    const auto sources = root.getChildWithName("SOURCES"), placements = root.getChildWithName("PLACEMENTS"),
               initial = root.getChildWithName("INITIAL_VALUES");
    if (!sources.isValid() || !placements.isValid() || !initial.isValid() || sources.getNumProperties() ||
        placements.getNumProperties() || initial.getNumProperties() || sources.getNumChildren() > 1024 ||
        placements.getNumChildren() > 65536 || initial.getNumChildren() > 4096)
        return fail();
    AutomationDocument doc;
    std::size_t pointCount = 0, linkCount = 0;
    for (const auto &s : sources)
    {
        AutomationSource source;
        std::int64_t colour = 0;
        if (!s.hasType("SOURCE") || !fields(s, {"id", "length", "name", "colour"}) ||
            s.getNumChildren() != 2 || !integer(s["id"], source.id) || !number(s["length"], source.length) ||
            !s["name"].isString() || !integer(s["colour"], colour) || colour < 0 ||
            colour > std::numeric_limits<std::uint32_t>::max())
            return fail();
        source.name = s["name"].toString().toStdString();
        source.colour = std::uint32_t(colour);
        const auto points = s.getChildWithName("POINTS"), links = s.getChildWithName("LINKS");
        if (!points.isValid() || !links.isValid() || points.getNumProperties() || links.getNumProperties())
            return fail();
        pointCount += std::size_t(points.getNumChildren());
        linkCount += std::size_t(links.getNumChildren());
        if (pointCount > 1000000 || linkCount > 4096)
            return fail();
        for (const auto &p : points)
        {
            AutomationPoint point;
            std::int64_t shape = 0;
            if (!p.hasType("POINT") || !fields(p, {"id", "beat", "value", "shape", "tension"}) ||
                p.getNumChildren() || !integer(p["id"], point.id) || !number(p["beat"], point.beat) ||
                !scalar(p["value"], point.value) || !integer(p["shape"], shape) || shape < 0 || shape > 7 ||
                !scalar(p["tension"], point.tension))
                return fail();
            point.incoming = AutomationShape(shape);
            source.points.push_back(point);
        }
        for (const auto &l : links)
        {
            AutomationLink link;
            if (!l.hasType("LINK") || !fields(l, {"kind", "owner", "parameter", "minimum", "maximum"}) ||
                l.getNumChildren() || !key(l, link.key, projectVersion) ||
                !targetScalar(l["minimum"], link.key, link.minimum) ||
                !targetScalar(l["maximum"], link.key, link.maximum))
                return fail();
            source.links.push_back(link);
        }
        doc.sources.push_back(std::move(source));
    }
    for (const auto &p : placements)
    {
        AutomationPlacement placement;
        std::int64_t lane = 0, muted = 0;
        if (!p.hasType("PLACEMENT") ||
            !fields(p, {"id", "source", "beat", "length", "offset", "lane", "muted"}) || p.getNumChildren() ||
            !integer(p["id"], placement.id) || !integer(p["source"], placement.source) ||
            !number(p["beat"], placement.beat) || !number(p["length"], placement.length) ||
            !number(p["offset"], placement.offset) || !integer(p["lane"], lane) || lane < 0 || lane > 127)
            return fail();
        if (p["muted"].isBool())
            muted = bool(p["muted"]) ? 1 : 0;
        else if (!integer(p["muted"], muted))
            return fail();
        if (muted != 0 && muted != 1)
            return fail();
        placement.lane = int(lane);
        placement.muted = muted != 0;
        doc.placements.push_back(placement);
    }
    for (const auto &v : initial)
    {
        AutomationInitialValue value;
        if (!v.hasType("INITIAL") || !fields(v, {"kind", "owner", "parameter", "value"}) ||
            v.getNumChildren() || !key(v, value.key, projectVersion) ||
            !targetScalar(v["value"], value.key, value.value))
            return fail();
        doc.initialValues.push_back(value);
    }
    out = std::move(doc);
    return juce::Result::ok(); // Parent Project performs ownership/global-ID/domain validation.
}
} // namespace vibera
