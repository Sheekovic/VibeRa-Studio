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
inline juce::var mixerAutomationArguments(const juce::String &id)
{
    auto schema = juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{
      "mixer":{"type":"integer","minimum":0,"maximum":2147483647,"default":0,"description":"Existing stable Mixer ID, never display row; omitted retains legacy Master0."},
      "automationControl":{"type":"string","enum":["gain","pan","stereoSeparation","send"],"default":"gain","description":"Shared canonical target. gain uses linear0..2,1 unity. pan and separation use -1..1,0 centre/identity. Source points stay normalized0..1. Send uses linear0..2 and requires automationSendTarget; Channel targets remain unsupported."},
      "automationSendTarget":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required only for automationControl=send. Existing stable destination Mixer ID,0=Master. mixer is existing non-Master source; an exact enabled route is required. target still means Automation source ID. Omitted for every other control."}},"required":[]})");
    auto *properties = schema["properties"].getDynamicObject();
    juce::Array<juce::var> required;
    if (id == "automation.create")
    {
        properties->setProperty("beat", juce::JSON::parse(R"({"type":"number","minimum":0,"maximum":1000000,"default":0,"description":"Placement start in quarter-note beats."})"));
        properties->setProperty("length", juce::JSON::parse(R"({"type":"number","exclusiveMinimum":0,"maximum":1000000,"default":0.25,"description":"Source/placement duration in quarter-note beats."})"));
        properties->setProperty("lane", juce::JSON::parse(R"({"type":"integer","minimum":0,"maximum":127,"default":0})"));
    }
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
