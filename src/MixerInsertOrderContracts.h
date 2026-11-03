// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline juce::var mixerInsertOrderArguments(const juce::String &id)
{
    auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "properties":{"mixer":{"type":"integer","minimum":1,"maximum":2147483647,
        "description":"Required existing stable non-Master insert ID. Never a display row. Selection, Channel assignments, routes, effects and automation stay bound to this ID."}},"required":["mixer"]})JSON");
    if (id == "mixer.move")
    {
        schema["properties"].getDynamicObject()->setProperty("mixerPosition", juce::JSON::parse(R"JSON(
          {"type":"integer","minimum":0,"maximum":128,"description":"Required final zero-based display row0..currentInsertCount-1. Master0's CURRENT row is reserved. Other rows shift while Master remains fixed, including nonleading legacy Master. Invalid positions refuse without clamping; same row no-ops."})JSON"));
        schema["required"].getArray()->add("mixerPosition");
    }
    return schema;
}
inline juce::var mixerInsertOrderStateSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["order","selectedInsert","position","canMoveLeft","canMoveRight","units"],
      "properties":{"order":{"type":"array","minItems":1,"maxItems":129,"uniqueItems":true,
        "items":{"type":"integer","minimum":0,"maximum":2147483647},"description":"Stable IDs in actual saved/UI display order, including Master."},
        "selectedInsert":{"type":"integer","minimum":0,"maximum":2147483647},
        "position":{"type":["integer","null"],"minimum":0,"maximum":128},
        "canMoveLeft":{"type":"boolean"},"canMoveRight":{"type":"boolean"},"units":{"type":"string"}}})JSON");
}
inline juce::var mixerInsertOrderCompletionSchema()
{
    auto result = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":true,
      "required":["completionState","completionScope","changed","mixerOrder","revision","projectIdentity"],
      "properties":{"completionState":{"type":"string","enum":["completed"]},
        "completionScope":{"type":"string","enum":["canonicalProjectOrder"]},
        "changed":{"type":"boolean"},"revision":{"type":"integer","minimum":0},
        "projectIdentity":{"type":"string"}}})JSON");
    result["properties"].getDynamicObject()->setProperty("mixerOrder", mixerInsertOrderStateSchema());
    return result;
}
} // namespace vibera
