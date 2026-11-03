// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerAutomationTarget.h"

namespace vibera
{
inline juce::var samplerAutomationDiscovery()
{
    auto result = std::make_unique<juce::DynamicObject>();
    result->setProperty("kind", 7);
    result->setProperty("owner", "stableChannelId");
    result->setProperty("parameterEncoding", "destinationCode*32+fieldCode");
    result->setProperty("targetStride", int(samplerAutomationTargetStride));
    result->setProperty("validPairs", int(samplerAutomationParameterCount));
    result->setProperty("curveUnits", "normalized0..1 mapped through physical link endpoints");
    result->setProperty("discreteCurvePolicy", "boolean threshold0.5; shape nearest integer, ties upward");
    result->setProperty("playbackPolicy",
                        "Existing Song automation placement/initial-value policy; Stop/Pattern manual "
                        "fallback. Global oscillator phase is shared; note delay/fade remains independent.");
    juce::Array<juce::var> destinations, fields;
    for (const auto &target : samplerAutomationTargetNames)
    {
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty("id", juce::String(target.id.data(), target.id.size()));
        row->setProperty("code", int(target.code));
        row->setProperty("envelopeAvailable", target.code != SamplerAutomationTargetCode::pan);
        row->setProperty("lfoAvailable", true);
        destinations.add(juce::var(row.release()));
    }
    for (const auto &field : samplerAutomationFields)
    {
        const auto metadata =
            lookupSamplerAutomationParameter(std::uint32_t(field.code)); // Volume supports all fields.
        if (!metadata)
            continue;
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty("id", juce::String(field.id.data(), field.id.size()));
        row->setProperty("code", int(field.code));
        row->setProperty("minimum", metadata->minimum);
        row->setProperty("maximum", metadata->maximum);
        row->setProperty("default", metadata->initial);
        row->setProperty("units", juce::String(field.units.data(), field.units.size()));
        row->setProperty("valueKind", field.kind == SamplerAutomationValueKind::boolean ? "boolean0or1"
                                      : field.kind == SamplerAutomationValueKind::shape ? "shape0to2"
                                                                                        : "continuous");
        row->setProperty("timebaseFlag", field.timebase == SamplerAutomationTimebase::envelopeTempo ? "tempo"
                                         : field.timebase == SamplerAutomationTimebase::lfoTempo ? "lfoTempo"
                                                                                                 : "none");
        fields.add(juce::var(row.release()));
    }
    result->setProperty("destinations", destinations);
    result->setProperty("fields", fields);
    return juce::var(result.release());
}
inline juce::var samplerAutomationArguments(const juce::String &id)
{
    auto schema = juce::JSON::parse(R"({"type":"object","additionalProperties":false,
      "required":["channel","samplerParameter"],"properties":{
        "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable Channel ID. New assignments require a local sample; native/plugin and Audio Clip sampler curves remain dormant and recoverable."},
        "samplerParameter":{"type":"integer","description":"Stable sampler destination/field code from sampler automation discovery; not a selected page, row or plugin parameter ID."}}})");
    auto *properties = schema["properties"].getDynamicObject();
    juce::Array<juce::var> codes;
    for (const auto &target : samplerAutomationTargetNames)
        for (const auto &field : samplerAutomationFields)
        {
            const auto code =
                std::uint32_t(target.code) * samplerAutomationTargetStride + std::uint32_t(field.code);
            if (lookupSamplerAutomationParameter(code))
                codes.add(int(code));
        }
    properties->getProperty("samplerParameter").getDynamicObject()->setProperty("enum", codes);
    auto required = *schema["required"].getArray();
    if (id == "automation.sampler.create")
    {
        properties->setProperty(
            "beat",
            juce::JSON::parse(
                R"({"type":"number","minimum":0,"maximum":1000000,"default":0,"description":"Placement start in quarter-note beats."})"));
        properties->setProperty(
            "length",
            juce::JSON::parse(
                R"({"type":"number","exclusiveMinimum":0,"maximum":1000000,"default":0.25,"description":"Curve and placement duration in quarter-note beats."})"));
        properties->setProperty(
            "lane", juce::JSON::parse(R"({"type":"integer","minimum":0,"maximum":127,"default":0})"));
    }
    if (id == "automation.sampler.init")
    {
        properties->setProperty(
            "value",
            juce::JSON::parse(
                R"({"type":"number","minimum":-1,"maximum":64,"description":"Explicit physical target value, not normalized curve ordinate. Exact parameter domain applies; booleans require0/1 and shape requires0/1/2. Time uses seconds or quarter-note beats according to the corresponding Tempo flag."})"));
        required.add("value");
    }
    if (id == "automation.sampler.link" || id == "automation.sampler.unlink" ||
        id == "automation.sampler.range")
    {
        properties->setProperty(
            "target",
            juce::JSON::parse(
                R"({"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing Automation source ID, never placement or Channel ID."})"));
        required.add("target");
    }
    if (id == "automation.sampler.range")
    {
        for (const auto *field : {"minimum", "maximum"})
        {
            properties->setProperty(
                field,
                juce::JSON::parse(
                    R"({"type":"number","minimum":-1,"maximum":64,"description":"Physical target value at normalized source ordinate0 (minimum) or1 (maximum). Inverted and constant ranges are allowed; exact parameter domain/discrete validation applies."})"));
            required.add(field);
        }
    }
    schema.getDynamicObject()->setProperty("required", required);
    return schema;
}
} // namespace vibera
