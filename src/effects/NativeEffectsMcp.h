// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "NativeEffectsRegistry.h"
#include "../McpSchema.h"

namespace vibera::effects
{
inline juce::var nativeEffectsDiscovery(const juce::var &args)
{
    const auto object = [] { return juce::var(new juce::DynamicObject()); };
    const auto put = [](juce::var &target, const juce::Identifier &key, const juce::var &value)
    { target.getDynamicObject()->setProperty(key, value); };
    const auto action = args["action"].toString();
    if (action != "list" && action != "descriptor")
        throw McpFailure("invalid_arguments", "Choose list or descriptor");
    if ((action == "descriptor") != args.hasProperty("type"))
        throw McpFailure("invalid_arguments", "Only descriptor requires an exact type from list");
    juce::Array<juce::var> rows;
    for (const auto &spec : catalogueEntries())
    {
        if (action == "descriptor" && args["type"].toString() != spec.slug)
            continue;
        auto row = object();
        const auto description = nativeDescription(spec.id);
        put(row, "type", spec.slug);
        put(row, "name", description.name);
        put(row, "category", spec.category);
        put(row, "format", "VibeRa");
        put(row, "builtIn", true);
        juce::Array<juce::var> parameters;
        for (std::size_t i = 0; i < spec.parameterCount; ++i)
        {
            const auto &p = spec.parameters[i];
            auto parameter = object();
            put(parameter, "id", juce::int64((static_cast<unsigned>(spec.id) + 1) * 256 + i + 1));
            put(parameter, "key", p.id);
            put(parameter, "name", p.name);
            put(parameter, "minimum", p.minimum);
            put(parameter, "maximum", p.maximum);
            put(parameter, "default", p.initial);
            put(parameter, "unit", p.unit);
            put(parameter, "interval", p.interval);
            put(parameter, "skew", p.skew);
            parameters.add(parameter);
        }
        put(row, "parameters", parameters);
        if (action == "descriptor")
            put(row, "descriptor", description.createXml()->toString());
        rows.add(row);
    }
    if (action == "descriptor")
    {
        if (rows.isEmpty())
            throw McpFailure("effects.unknownType", "Unknown built-in effect type; rediscover list");
        return rows.getReference(0);
    }
    auto result = object();
    put(result, "effects", rows);
    put(result, "total", rows.size());
    return result;
}
} // namespace vibera::effects
