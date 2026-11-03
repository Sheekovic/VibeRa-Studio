// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Groove.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace vibera
{
juce::Result validateGroove(const GrooveTemplate &groove)
{
    if (groove.name.isEmpty() || groove.name.length() > 96 || groove.name.trim() != groove.name ||
        groove.name.containsAnyOf("\r\n\t") || !std::isfinite(groove.cycle) || groove.cycle < .03125 ||
        groove.cycle > 65536 || groove.points.empty() || groove.points.size() > 4096)
        return juce::Result::fail(
            "Groove needs a name (1..96 characters), cycle0.03125..65536 beats and1..4096 points");
    double previous = -1;
    for (const auto &point : groove.points)
    {
        if (!std::isfinite(point.beat) || point.beat < 0 || point.beat >= groove.cycle ||
            point.beat <= previous || !std::isfinite(point.length) || point.length <= 0 ||
            point.length > 65536 || !std::isfinite(point.velocity) || point.velocity < 0 ||
            point.velocity > 1)
            return juce::Result::fail(
                "Groove points need unique ascending onsets inside cycle, positive lengths and velocity0..1");
        previous = point.beat;
    }
    return juce::Result::ok();
}
juce::var grooveState(const GrooveTemplate &groove)
{
    auto result = juce::var(new juce::DynamicObject());
    auto *object = result.getDynamicObject();
    object->setProperty("format", "VibeRa.Groove");
    object->setProperty("version", 1);
    object->setProperty("name", groove.name);
    object->setProperty("cycleBeats", groove.cycle);
    juce::Array<juce::var> points;
    for (const auto &point : groove.points)
    {
        auto item = juce::var(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("beat", point.beat);
        item.getDynamicObject()->setProperty("length", point.length);
        item.getDynamicObject()->setProperty("velocity", point.velocity);
        points.add(item);
    }
    object->setProperty("points", points);
    return result;
}
bool grooveNumber(const juce::var &value)
{
    return (value.isInt() || value.isInt64() || value.isDouble()) && std::isfinite(double(value));
}
bool grooveKeys(const juce::var &value, const juce::StringArray &keys)
{
    auto *object = value.getDynamicObject();
    if (!object || object->getProperties().size() != keys.size())
        return false;
    for (const auto &key : keys)
        if (!value.hasProperty(key))
            return false;
    return true;
}
juce::Result parseGroove(const juce::var &value, GrooveTemplate &destination)
{
    if (!grooveKeys(value, {"format", "version", "name", "cycleBeats", "points"}) ||
        !value["format"].isString() || value["format"].toString() != "VibeRa.Groove" ||
        !value["version"].isInt() || int(value["version"]) != 1 || !value["name"].isString() ||
        !grooveNumber(value["cycleBeats"]) || !value["points"].isArray() || value["points"].size() > 4096)
        return juce::Result::fail("Unsupported or malformed VibeRa.Groove version1 template");
    GrooveTemplate next{value["name"].toString(), double(value["cycleBeats"]), {}};
    for (const auto &item : *value["points"].getArray())
    {
        if (!grooveKeys(item, {"beat", "length", "velocity"}) || !grooveNumber(item["beat"]) ||
            !grooveNumber(item["length"]) || !grooveNumber(item["velocity"]))
            return juce::Result::fail("Groove points require numeric beat, length and velocity");
        next.points.push_back({double(item["beat"]), double(item["length"]), double(item["velocity"])});
    }
    if (auto valid = validateGroove(next); valid.failed())
        return valid;
    destination = std::move(next);
    return juce::Result::ok();
}
juce::Result captureGroove(const Project &project, Id patternId, Id channelId, const std::vector<Id> &ids,
                           double cycle, const juce::String &name, GrooveTemplate &destination)
{
    const auto *pattern = project.pattern(patternId);
    const auto *channel = project.channel(channelId);
    if (!pattern || (ids.empty() && (!channel || channel->audioClip)) || ids.size() > 100000)
        return juce::Result::fail("Select a playable Channel part or explicit score notes for capture");
    const std::set<Id> targets(ids.begin(), ids.end());
    if (targets.size() != ids.size())
        return juce::Result::fail("Duplicate groove capture note identities");
    std::vector<const Note *> chosen;
    std::set<Id> found;
    for (const auto &note : pattern->notes)
        if (targets.empty() ? note.channel == channelId : targets.contains(note.id))
        {
            found.insert(note.id);
            if (note.beat >= cycle && !targets.empty())
                return juce::Result::fail("Selected groove note begins outside the capture cycle");
            if (note.beat < cycle)
                chosen.push_back(&note);
        }
    if (!targets.empty() && found != targets)
        return juce::Result::fail("Groove capture note target is stale");
    std::sort(chosen.begin(), chosen.end(), [](const auto *a, const auto *b)
              { return a->beat != b->beat ? a->beat < b->beat : a->id < b->id; });
    GrooveTemplate next{name, cycle, {}};
    for (const auto *note : chosen)
        if (next.points.empty() || next.points.back().beat != note->beat)
            next.points.push_back({note->beat, note->length, note->velocity});
    if (auto valid = validateGroove(next); valid.failed())
        return valid;
    destination = std::move(next);
    return juce::Result::ok();
}
std::vector<GrooveTemplate> factoryGrooves()
{
    GrooveTemplate straight{"VibeRa straight sixteenths", 4, {}}, swing{"VibeRa light swing", 4, {}};
    for (int i = 0; i < 16; ++i)
    {
        straight.points.push_back({i * .25, .2, i % 4 == 0 ? .9 : .65});
        swing.points.push_back({i * .25 + (i % 2 ? .0625 : 0), .2, i % 2 ? .55 : .9});
    }
    return {straight,
            swing,
            {"VibeRa backbeat dynamics", 4, {{0, .75, .6}, {1, .75, 1}, {2, .75, .7}, {3, .75, 1}}}};
}
std::vector<double> grooveGuidePositions(const GrooveTemplate &groove, double begin, double end, size_t limit)
{
    std::vector<double> result;
    if (groove.points.empty() || !std::isfinite(groove.cycle) || groove.cycle < .03125 || limit == 0 ||
        end > 1.e12 || !std::isfinite(begin) || !std::isfinite(end) || begin < 0 || end <= begin)
        return result;
    const auto repetition = std::int64_t(std::floor(begin / groove.cycle));
    const double phase = begin - repetition * groove.cycle;
    const auto first = std::lower_bound(groove.points.begin(), groove.points.end(), phase,
                                        [](const auto &point, double value) { return point.beat < value; });
    const auto count = std::int64_t(groove.points.size());
    auto index = repetition * count + std::int64_t(first - groove.points.begin());
    const auto stride =
        std::max<std::int64_t>(1, std::int64_t(std::ceil((end - begin) / groove.cycle * count / limit)));
    result.reserve(limit);
    while (result.size() < limit)
    {
        const double beat = (index / count) * groove.cycle + groove.points[size_t(index % count)].beat;
        if (beat > end)
            break;
        result.push_back(beat);
        index += stride;
    }
    return result;
}
juce::Result loadGroove(const juce::File &file, GrooveTemplate &destination)
{
    if (!file.existsAsFile() || file.getSize() > 1048576)
        return juce::Result::fail("Groove must be an existing file no larger than1MiB");
    auto stream = file.createInputStream();
    if (!stream)
        return juce::Result::fail("Could not read groove template");
    juce::MemoryBlock bytes;
    stream->readIntoMemoryBlock(bytes, 1048577);
    if (bytes.getSize() > 1048576 || !stream->isExhausted())
        return juce::Result::fail("Groove template exceeds1MiB");
    juce::var parsed;
    if (auto result = juce::JSON::parse(
            juce::String::createStringFromData(bytes.getData(), int(bytes.getSize())), parsed);
        result.failed())
        return result;
    return parseGroove(parsed, destination);
}
juce::Result saveGroove(const GrooveTemplate &groove, const juce::File &file, bool overwrite)
{
    if (auto valid = validateGroove(groove); valid.failed())
        return valid;
    if (file.exists() && (!overwrite || !file.existsAsFile()))
        return juce::Result::fail("Groove destination exists; explicit file overwrite is required");
    return file.replaceWithText(juce::JSON::toString(grooveState(groove), true))
               ? juce::Result::ok()
               : juce::Result::fail("Could not atomically save groove template");
}
} // namespace vibera
