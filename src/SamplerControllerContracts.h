// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "McpSchema.h"
#include "SamplerAutomationParameters.h"
#include "SamplerControllerLinks.h"

namespace vibera
{
inline juce::String samplerControllerUnits(const Project &project, const SamplerControllerLink &link)
{
    const auto parameter = lookupSamplerAutomationParameter(link.parameter);
    if (!parameter)
        return "unsupported";
    const auto *owner = project.channel(link.channel);
    if (owner && parameter->timebase != SamplerAutomationTimebase::none)
    {
        const auto &settings = owner->sampler.modulation[size_t(parameter->registryTarget)];
        const bool tempo = parameter->timebase == SamplerAutomationTimebase::envelopeTempo
                               ? settings.tempo
                               : settings.lfoTempo;
        return tempo ? "quarter-note beats" : "seconds";
    }
    return juce::String(parameter->units.data(), parameter->units.size());
}

inline bool isSamplerControllerCommand(const juce::String &id)
{
    return id == "channel.sampler.controller.create" || id == "channel.sampler.controller.update" ||
           id == "channel.sampler.controller.remove";
}
inline juce::var samplerControllerLinkSchema()
{
    auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["id","channel","parameter","port","midiChannel","cc","layer","minimum","maximum","pickup","enabled","label"],
      "properties":{
        "id":{"type":"integer","minimum":0,"maximum":1000000000,"description":"0 for creation; exact existing stable link ID for update. Project-owned, never a global mapping index."},
        "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Fixed owner Channel ID in this project. Does not follow selection. Native instruments and Audio Clips are dormant existing targets; new assignments require a local sampler."},
        "parameter":{"type":"integer","description":"Stable destination*32+field sampler parameter code; code0 is valid. See samplerAutomationContract for physical units/ranges and tempo behavior."},
        "port":{"type":"integer","minimum":0,"maximum":1,"description":"Existing logical ingress:0 Performance,1 Control. Does not open or configure hardware."},
        "midiChannel":{"type":"integer","minimum":0,"maximum":16,"description":"0 source-channel wildcard;1..16 exact incoming MIDI channel. This is independent of target Channel selection."},
        "cc":{"type":"integer","minimum":0,"maximum":127,"description":"Incoming MIDI continuous-controller number."},
        "layer":{"type":"integer","minimum":0,"maximum":3,"default":0,"description":"Existing controller modifier layer."},
        "minimum":{"type":"number","description":"Physical output at CC0, within target metadata. Must be less than maximum."},
        "maximum":{"type":"number","description":"Physical output at CC127, within target metadata. Time uses the target's seconds or quarter-note-beats setting."},
        "pickup":{"type":"boolean","default":true,"description":"Catch the current value before applying an absolute hardware position."},
        "enabled":{"type":"boolean","default":true},
        "label":{"type":"string","maxLength":120,"description":"Project-owned display label."}}})JSON");
    juce::Array<juce::var> codes;
    for (std::uint32_t code = 0; code < 160; ++code)
        if (lookupSamplerAutomationParameter(code))
            codes.add(int(code));
    schema["properties"]["parameter"].getDynamicObject()->setProperty("enum", codes);
    return schema;
}
inline juce::var samplerControllerArguments(const juce::String &id)
{
    auto schema =
        juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,"properties":{}})JSON");
    auto *properties = schema["properties"].getDynamicObject();
    juce::Array<juce::var> required;
    if (id != "channel.sampler.controller.create")
    {
        properties->setProperty(
            "target",
            juce::JSON::parse(
                R"JSON({"type":"integer","minimum":1,"maximum":1000000000,"description":"Exact existing project link ID, never a row number."})JSON"));
        required.add("target");
    }
    if (id != "channel.sampler.controller.remove")
    {
        properties->setProperty("controllerLink", samplerControllerLinkSchema());
        properties->setProperty(
            "replacements",
            juce::JSON::parse(
                R"JSON({"type":"array","uniqueItems":true,"maxItems":512,"items":{"type":"integer","minimum":1,"maximum":1000000000},"description":"Explicit complete set of conflicting PROJECT link IDs to replace atomically. Omitted/empty refuses conflicts. Never deletes global settings."})JSON"));
        required.add("controllerLink");
    }
    schema.getDynamicObject()->setProperty("required", required);
    return schema;
}
inline juce::var samplerControllerLinkState(const SamplerControllerLink &link)
{
    auto value = juce::var(new juce::DynamicObject);
    auto *out = value.getDynamicObject();
    out->setProperty("id", juce::int64(link.id));
    out->setProperty("channel", juce::int64(link.channel));
    out->setProperty("parameter", int(link.parameter));
    out->setProperty("port", link.port);
    out->setProperty("midiChannel", link.midiChannel);
    out->setProperty("cc", link.cc);
    out->setProperty("layer", link.layer);
    out->setProperty("minimum", link.minimum);
    out->setProperty("maximum", link.maximum);
    out->setProperty("pickup", link.pickup);
    out->setProperty("enabled", link.enabled);
    out->setProperty("label", link.label);
    return value;
}
inline juce::Result decodeSamplerControllerArguments(const juce::String &id, const juce::var &values,
                                                     CommandArgs &args)
{
    try
    {
        validateMcpSchema(samplerControllerArguments(id), values);
        if (values.hasProperty("target"))
            args.target = juce::int64(values["target"]);
        if (id != "channel.sampler.controller.remove")
        {
            const auto value = values["controllerLink"];
            SamplerControllerLink link;
            link.id = juce::int64(value["id"]);
            link.channel = juce::int64(value["channel"]);
            link.parameter = std::uint32_t(int(value["parameter"]));
            link.port = int(value["port"]);
            link.midiChannel = int(value["midiChannel"]);
            link.cc = int(value["cc"]);
            link.layer = int(value["layer"]);
            link.minimum = double(value["minimum"]);
            link.maximum = double(value["maximum"]);
            link.pickup = bool(value["pickup"]);
            link.enabled = bool(value["enabled"]);
            link.label = value["label"].toString();
            args.samplerControllerLink = std::move(link);
            if (const auto *replacements = values["replacements"].getArray())
                for (const auto &replacement : *replacements)
                    args.samplerControllerReplacements.push_back(juce::int64(replacement));
        }
        return juce::Result::ok(); // Canonical model validator still owns target/conflict/range checks.
    }
    catch (const std::exception &failure)
    {
        return juce::Result::fail("controller.invalidArguments: " + juce::String(failure.what()));
    }
}

inline juce::var samplerControllerLearnArguments(const juce::String &action)
{
    auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "properties":{"action":{"type":"string"},"projectIdentity":{"type":"string"}}})JSON");
    auto *fields = schema["properties"].getDynamicObject();
    juce::Array<juce::var> required{"action"};
    if (action == "samplerLearnStart")
    {
        fields->setProperty(
            "targetChannel",
            juce::JSON::parse(
                R"JSON({"type":"integer","minimum":1,"maximum":1000000000,"description":"Fixed existing local sampler Channel ID captured for this operation."})JSON"));
        fields->setProperty("samplerParameter", samplerControllerLinkSchema()["properties"]["parameter"]);
        required.add("targetChannel");
        required.add("samplerParameter");
    }
    else
    {
        fields->setProperty(
            "token",
            juce::JSON::parse(
                R"JSON({"type":"string","maxLength":64,"description":"Exact operation returned by samplerLearnStart. Poll samplerLearnState; identification stays pending until explicit Accept."})JSON"));
        required.add("token");
    }
    if (action == "samplerLearnAccept")
    {
        fields->setProperty(
            "minimum",
            juce::JSON::parse(
                R"JSON({"type":"number","description":"Optional physical output at CC0; paired with maximum. Defaults to authoritative parameter minimum."})JSON"));
        fields->setProperty(
            "maximum",
            juce::JSON::parse(
                R"JSON({"type":"number","description":"Optional physical output at CC127; paired with minimum. Defaults to authoritative parameter maximum."})JSON"));
        fields->setProperty("pickup", juce::JSON::parse(R"JSON({"type":"boolean","default":true})JSON"));
        fields->setProperty(
            "replacements",
            samplerControllerArguments("channel.sampler.controller.create")["properties"]["replacements"]);
    }
    schema.getDynamicObject()->setProperty("required", required);
    return schema;
}
} // namespace vibera
