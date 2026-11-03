// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "McpContracts.h"
namespace vibera
{
inline juce::var pluginIoStateSchema()
{
    auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["requested","negotiated","catalogueKnown","available","mixerChannels","simultaneousMultiOutput","capabilityScope","inputs","outputs"],
      "properties":{
        "catalogueKnown":{"type":"boolean"},"available":{"type":"boolean"},
        "mixerChannels":{"type":"integer","const":2},"simultaneousMultiOutput":{"type":"boolean","const":false},
        "capabilityScope":{"type":"string"},
        "auxiliaryInputBus":{"type":"integer","minimum":-1,"maximum":31},
        "auxiliaryChannels":{"type":"integer","enum":[0,1,2]}}})JSON");
    auto properties = schema["properties"].getDynamicObject();
    const auto selection = commandArgumentSchema("plugin.io")["properties"]["pluginIo"];
    properties->setProperty("requested", selection);
    auto *negotiated = new juce::DynamicObject;
    negotiated->setProperty("anyOf",
                            juce::Array<juce::var>{selection, juce::JSON::parse(R"({"type":"null"})")});
    properties->setProperty("negotiated", juce::var(negotiated));
    const auto rows = juce::JSON::parse(R"JSON({"type":"array","maxItems":32,"items":{
      "type":"object","additionalProperties":false,"required":["index","name","defaultChannels","actualChannels"],
      "properties":{"index":{"type":"integer","minimum":0,"maximum":31},"name":{"type":"string"},
        "defaultChannels":{"type":"integer","minimum":0},"actualChannels":{"type":"integer","minimum":0,"maximum":2}}}})JSON");
    properties->setProperty("inputs", rows);
    properties->setProperty("outputs", rows);
    return schema;
}
} // namespace vibera
