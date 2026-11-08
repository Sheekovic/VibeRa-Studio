// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerAutomationTarget.h"
namespace vibera
{
inline juce::var mixerAutomationResultSchema()
{
    return juce::JSON::parse(R"({"type":"object","additionalProperties":true,
      "required":["completionState","completionScope","dspSettlingAcknowledged","revision"],
      "properties":{"completionState":{"type":"string","const":"completed"},
        "completionScope":{"type":"string","const":"canonicalAutomationTarget"},
        "dspSettlingAcknowledged":{"type":"boolean","const":false},
        "revision":{"type":"integer","minimum":0}}})");
}
// Modulator shape/rate/depth/phase fields shared by add and set.
inline void addModulatorFields(juce::DynamicObject &properties)
{
    properties.setProperty("text", juce::JSON::parse(R"({"type":"string","enum":["sine","triangle","sawUp","sawDown","square","random","smoothRandom"],"default":"sine","description":"Modulator shape. random holds a new random value each cycle; smoothRandom glides between them."})"));
    properties.setProperty("value", juce::JSON::parse(R"({"type":"number","minimum":0,"maximum":1024,"default":4,"description":"Period in quarter-note beats per cycle, tempo-synced (0.25 a sixteenth note cycle, 4 one 4/4 bar). 0 means the default 4; otherwise 1/64..1024."})"));
    properties.setProperty("strength", juce::JSON::parse(R"({"type":"number","minimum":0,"maximum":1,"default":1,"description":"Depth: peak-to-peak swing as a fraction of the control's full range, centred on its manual (or initialized) value and clamped to the range."})"));
    properties.setProperty("offset", juce::JSON::parse(R"({"type":"number","minimum":0,"exclusiveMaximum":1,"default":0,"description":"Phase: 0..1 of a cycle at Song/Pattern beat 0."})"));
}
inline juce::var modulatorArguments(const juce::String &id, const juce::var &bindingSchema)
{
    auto schema = juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{}})");
    auto *properties = schema["properties"].getDynamicObject();
    juce::Array<juce::var> required;
    if (id == "automation.plugin.modulator")
    {
        properties->setProperty("target", juce::JSON::parse(R"({"type":"integer","minimum":1,"maximum":1000000000,"description":"Stable plugin ID (instrument or Mixer effect)."})"));
        properties->setProperty("binding", bindingSchema);
        required.add("target");
        required.add("binding");
    }
    else
    {
        properties->setProperty("target", juce::JSON::parse(R"({"type":"integer","minimum":1,"maximum":1000000000,"description":"Stable modulator ID from vibera_inspect modulators."})"));
        required.add("target");
    }
    if (id == "automation.modulator.enable")
        properties->setProperty("enabled", juce::JSON::parse(R"({"type":"boolean","default":true,"description":"false keeps the modulator but leaves the control at its manual value."})"));
    if (id == "automation.plugin.modulator" || id == "automation.modulator.set")
        addModulatorFields(*properties);
    schema.getDynamicObject()->setProperty("required", required);
    return schema;
}
inline juce::var mixerAutomationArguments(const juce::String &id)
{
    auto schema = juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{
      "mixer":{"type":"integer","minimum":0,"maximum":2147483647,"default":0,"description":"Existing stable Mixer ID, never display row; omitted retains legacy Master0."},
      "automationControl":{"type":"string","enum":["gain","pan","stereoSeparation","send","channelGain","channelPan"],"default":"gain","description":"Shared canonical target. gain uses linear0..2,1 unity. pan and separation use -1..1,0 centre/identity. Source points stay normalized0..1. Send uses linear0..2 and requires automationSendTarget. channelGain (linear0..2) and channelPan (-1..1) automate a Channel Rack volume/pan and require channel."},
      "channel":{"type":"integer","minimum":1,"description":"Required only for channelGain/channelPan: the stable Channel ID."},
      "automationSendTarget":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required only for automationControl=send. Existing stable destination Mixer ID,0=Master. mixer is existing non-Master source; an exact enabled route is required. target still means Automation source ID. Omitted for every other control."}},"required":[]})");
    auto *properties = schema["properties"].getDynamicObject();
    juce::Array<juce::var> required;
    if (id == "automation.create")
    {
        properties->setProperty("beat", juce::JSON::parse(R"({"type":"number","minimum":0,"maximum":1000000,"default":0,"description":"Placement start in quarter-note beats."})"));
        properties->setProperty("length", juce::JSON::parse(R"({"type":"number","exclusiveMinimum":0,"maximum":1000000,"default":0.25,"description":"Source/placement duration in quarter-note beats."})"));
        properties->setProperty("lane", juce::JSON::parse(R"({"type":"integer","minimum":0,"maximum":127,"default":0})"));
    }
    else if (id == "automation.modulator")
        addModulatorFields(*properties);
    else if (id == "automation.init")
    {
        properties->setProperty(
            "value",
            juce::JSON::parse(
                R"({"type":"number","minimum":-1,"maximum":2,"default":0,"description":"Explicit initialized value in target units. Shared canonical resolver further requires gain/send0..2 or pan/separation -1..1; never normalized source ordinate."})"));
    }
    if (id == "automation.link" || id == "automation.unlink" || id == "automation.range")
    {
        properties->setProperty("target", juce::JSON::parse(R"({"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing Automation source ID, not placement or Mixer ID."})"));
        required.add("target");
    }
    if (id == "automation.range")
    {
        properties->setProperty(
            "beat",
            juce::JSON::parse(
                R"({"type":"number","minimum":-1,"maximum":2,"default":0,"description":"Legacy field name: LOWER source ordinate's output value in target units, not beats. Inverted ranges supported; gain/send0..2 or pan/separation -1..1."})"));
        properties->setProperty("length", juce::JSON::parse(R"({"type":"number","minimum":-1,"maximum":2,"default":0.25,"description":"Legacy field name: UPPER source ordinate's output value in target units, not duration. Canonical target-domain validation applies."})"));
    }
    schema.getDynamicObject()->setProperty("required", required);
    return schema;
}
} // namespace vibera
