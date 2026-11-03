// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerGroups.h"
namespace vibera
{
inline juce::var mixerOrganisationStateSchema();
inline juce::var mixerOrganisationArguments(const juce::String &id)
{
    auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,"properties":{},"required":[]})JSON");
    auto *properties = schema["properties"].getDynamicObject();
    auto *required = schema["required"].getArray();
    const bool bulk = id.startsWith("mixer.selection.");
    if (id == "mixer.group.create" || id == "mixer.group.assign" || bulk)
    {
        auto ids = juce::JSON::parse(R"JSON({"type":"array","minItems":1,"maxItems":129,"uniqueItems":true,
          "items":{"type":"integer","minimum":1,"maximum":2147483647},
          "description":"Existing stable Mixer IDs, not rows. Display groups exclude Master0; bulk rename/colour may include it. Whole list validates before mutation."})JSON");
        if (bulk) ids["items"].getDynamicObject()->setProperty("minimum", 0);
        properties->setProperty("ids", ids); required->add("ids");
    }
    if (!bulk && id != "mixer.group.create")
    {
        properties->setProperty("mixerGroup", juce::JSON::parse(id == "mixer.group.assign"
            ? R"JSON({"type":"string","maxLength":32,"pattern":"^([0-9a-f]{32})?$","description":"Required stable group UUID from organisation readback; empty string explicitly ungroups these members. Never a group name/row."})JSON"
            : R"JSON({"type":"string","minLength":32,"maxLength":32,"pattern":"^[0-9a-f]{32}$","description":"Required current stable group UUID. Read group state after Undo/Redo/project replacement; deleted UUIDs never retarget newly created groups."})JSON"));
        required->add("mixerGroup");
    }
    if (id == "mixer.group.colourMembers")
    {
        properties->setProperty("mixer",
                                juce::JSON::parse(R"JSON({"type":"integer","minimum":1,"maximum":2147483647,
          "description":"Required existing stable SOURCE insert ID belonging to mixerGroup. Copies its stored ARGB to all current members. Stored0 resets each member to its own theme/default; never samples rendered RGB, changes global theme or changes the group marker. No member selection or routing changes."})JSON"));
        required->add("mixer");
    }
    if (id.endsWith("rename") || id == "mixer.group.create")
    {
        auto text = juce::JSON::parse(R"JSON({"type":"string","minLength":1,"maxLength":64,
          "description":"Trimmed name without tabs/newlines. Group names unique ignoring case. Bulk rename uses this prefix plus space and1-based number in current display order; stable routing IDs remain unchanged."})JSON");
        if (bulk) text.getDynamicObject()->setProperty("maxLength", 55);
        properties->setProperty("text", text); required->add("text");
    }
    if (id.endsWith("colour"))
    {
        properties->setProperty("value", juce::JSON::parse(R"JSON({"type":"integer","minimum":0,"maximum":4294967295,
          "default":0,"description":"Theme0 or opaque ARGB0xff000000..0xffffffff. Group colour paints a group marker; it does not overwrite individual strip colours."})JSON"));
        required->add("value");
    }
    if (id == "mixer.group.mute" || id == "mixer.group.solo" || id == "mixer.group.soloConnected")
        properties->setProperty("enabled", juce::JSON::parse(R"JSON({"type":"boolean","default":true,
          "description":"Mute sets all members and ends solo restore ownership, like existing manual mute. Solo true captures original mask once and keeps all member output paths; connected also keeps upstream inputs. False exits only this group's solo."})JSON"));
    if (id == "mixer.group.solo" || id == "mixer.group.soloConnected" || id == "mixer.group.unsolo")
        properties->setProperty("restore", juce::JSON::parse(R"JSON({"type":"boolean","description":"Solo exit only. Omitted follows Workflow preference, initially true; false unmutes all inserts. The captured baseline survives switching group/single solo."})JSON"));
    return schema;
}
inline juce::var mixerOrganisationCompletionSchema()
{
    auto result = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":true,
      "required":["completionState","completionScope","dspSettlingAcknowledged","mixerOrganisation"],
      "properties":{"completionState":{"type":"string","enum":["completed"]},
        "completionScope":{"type":"string","enum":["canonicalMixerOrganisation"]},
        "dspSettlingAcknowledged":{"type":"boolean","const":false},"mixerOrganisation":{"type":"object"}}})JSON");
    result["properties"].getDynamicObject()->setProperty("mixerOrganisation", mixerOrganisationStateSchema());
    return result;
}
inline juce::var mixerOrganisationStateSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["selected","active","anchor","activeGroup","groups","projectIdentity","revision","selectionUndoable","selectionPersisted","groupMeaning","insertCreationAvailable","insertDeletion"],
      "properties":{"selected":{"type":"array","minItems":1,"maxItems":129,"uniqueItems":true,"items":{"type":"integer","minimum":0,"maximum":2147483647}},
        "active":{"type":"integer","minimum":0,"maximum":2147483647},"anchor":{"type":"integer","minimum":0,"maximum":2147483647},
        "activeGroup":{"type":"string","maxLength":32,"pattern":"^([0-9a-f]{32})?$"},
        "groups":{"type":"array","maxItems":128,"items":{"type":"object","additionalProperties":false,
          "required":["id","name","colour","members","solo"],"properties":{
            "id":{"type":"string","minLength":32,"maxLength":32,"pattern":"^[0-9a-f]{32}$"},
            "name":{"type":"string","minLength":1,"maxLength":64},"colour":{"type":"integer","minimum":0,"maximum":4294967295},
            "members":{"type":"array","maxItems":128,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":2147483647}},"solo":{"type":"boolean"}}}},
        "projectIdentity":{"type":"string"},"revision":{"type":"integer","minimum":0},
        "selectionUndoable":{"type":"boolean","const":false},"selectionPersisted":{"type":"boolean","const":false},
        "groupMeaning":{"type":"string"},"insertCreationAvailable":{"type":"boolean","description":"Model capacity/solo eligibility. Current recording/gesture/file/native preparation prerequisites still apply to actual asynchronous admission."},
        "insertDeletion":{"type":"object","required":["insert","exists","masterProtected","available","errorCode","assignedChannels","incomingRoutes","automationSources","initializedTargets","groups","effects","soloSession","reason","requiredDeletePolicy"],
          "properties":{"insert":{"type":"integer","minimum":0,"maximum":2147483647},"exists":{"type":"boolean"},"masterProtected":{"type":"boolean"},
            "available":{"type":"boolean","description":"Model target eligibility with explicit requiredDeletePolicy; actual application/native admission can refuse busy or incompatible recording."},"errorCode":{"type":"string"},
            "assignedChannels":{"type":"array","items":{"type":"integer","minimum":1}},"incomingRoutes":{"type":"array","items":{"type":"integer","minimum":1,"maximum":2147483647}},
            "automationSources":{"type":"array","items":{"type":"integer","minimum":1}},"initializedTargets":{"type":"integer","minimum":0},
            "groups":{"type":"array","items":{"type":"string","minLength":32,"maxLength":32}},"effects":{"type":"integer","minimum":0},
            "soloSession":{"type":"boolean"},"reason":{"type":"string"},"requiredDeletePolicy":{"type":"string"}}}}})JSON");
}
inline juce::var mixerSelectionIdsSchema()
{
    return juce::JSON::parse(R"JSON({"type":"array","minItems":1,"maxItems":129,"uniqueItems":true,
      "items":{"type":"integer","minimum":0,"maximum":2147483647},
      "description":"Current stable insert IDs. Multi-selection is workspace state, separate from the active send source, and never persisted/Undoable."})JSON");
}
} // namespace vibera
