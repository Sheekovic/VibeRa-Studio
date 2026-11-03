// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace vibera
{
struct McpFailure : std::runtime_error
{
    juce::String code;
    McpFailure(juce::String category, const juce::String &message)
        : std::runtime_error(message.toStdString()), code(std::move(category))
    {
    }
};
inline void validateMcpSchema(const juce::var &schema, const juce::var &value, juce::String path = "args")
{
    auto fail = [&](const juce::String &message)
    { throw McpFailure("invalid_arguments", path + ": " + message); };
    const auto type = schema["type"].toString();
    const bool numeric = value.isInt() || value.isInt64() || value.isDouble();
    if ((type == "object" && !value.isObject()) || (type == "array" && !value.isArray()) ||
        (type == "string" && !value.isString()) || (type == "boolean" && !value.isBool()) ||
        ((type == "number" || type == "integer") && (!numeric || !std::isfinite(double(value)))) ||
        (type == "integer" && numeric && std::floor(double(value)) != double(value)))
        fail("expected " + type);
    if (const auto *options = schema["enum"].getArray())
        if (!options->contains(value))
            fail("value is not in the supported enum");
    if (numeric)
    {
        const auto number = double(value);
        if ((schema.hasProperty("minimum") && number < double(schema["minimum"])) ||
            (schema.hasProperty("maximum") && number > double(schema["maximum"])) ||
            (schema.hasProperty("exclusiveMinimum") && number <= double(schema["exclusiveMinimum"])))
            fail("number outside valid range");
    }
    if (value.isString() && schema.hasProperty("maxLength") &&
        value.toString().length() > int(schema["maxLength"]))
        fail("text exceeds maximum length");
    if (const auto *values = value.getArray())
    {
        if (schema.hasProperty("minItems") && values->size() < int(schema["minItems"]))
            fail("too few items");
        if (schema.hasProperty("maxItems") && values->size() > int(schema["maxItems"]))
            fail("too many items");
        const bool uniqueIntegers =
            bool(schema["uniqueItems"]) && schema["items"]["type"].toString() == "integer";
        std::unordered_set<double> seenIntegers;
        for (int i = 0; i < values->size(); ++i)
        {
            if (schema.hasProperty("items"))
                validateMcpSchema(schema["items"], (*values)[i], path + "[" + juce::String(i) + "]");
            if (uniqueIntegers && !seenIntegers.insert(double((*values)[i])).second)
                fail("duplicate item");
            if (bool(schema["uniqueItems"]) && !uniqueIntegers)
                for (int j = 0; j < i; ++j)
                    if ((*values)[i] == (*values)[j])
                        fail("duplicate item");
        }
    }
    if (const auto *object = value.getDynamicObject())
    {
        if (const auto *required = schema["required"].getArray())
            for (const auto &key : *required)
                if (!object->hasProperty(key.toString()))
                    fail("missing " + key.toString());
        const auto properties = schema["properties"];
        for (const auto &property : object->getProperties())
        {
            if (properties.hasProperty(property.name))
                validateMcpSchema(properties[property.name], property.value,
                                  path + "." + property.name.toString());
            else if (schema.hasProperty("additionalProperties") && !bool(schema["additionalProperties"]))
                fail("unknown field " + property.name.toString());
        }
    }
}
} // namespace vibera
