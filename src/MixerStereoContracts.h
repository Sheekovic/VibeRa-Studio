// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline bool isMixerStereoCommand(const juce::String &command)
{
    return command == "mixer.stereoSeparation" || command == "mixer.phaseInvert" ||
           command == "mixer.stereoSwap";
}
inline juce::var mixerStereoArguments(const juce::String &command)
{
    auto result = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["mixer"],"properties":{"mixer":{"type":"integer","minimum":0,"maximum":2147483647,
      "description":"Required existing stable insert ID;0 is Master, never display row."}}})JSON");
    auto *fields = result["properties"].getDynamicObject();
    if (command == "mixer.stereoSeparation")
    {
        fields->setProperty("value",
                            juce::JSON::parse(R"JSON({"type":"number","minimum":-1,"maximum":1,"default":0,
          "description":"Required separation fraction.0 original,+1 averages L/R to mono; negative values subtract the opposite channel, reaching L-R/R-L at-1. Centre content can cancel. Original linear cross-mix, not an exact FL curve claim. Five-ms coefficient ramp, post-effects before existing pan/fader. Does not alter pan law or graph latency."})JSON"));
        result["required"].getArray()->add("value");
    }
    else
    {
        fields->setProperty("enabled", juce::JSON::parse(R"JSON({"type":"boolean","default":false,
          "description":"Required saved switch target: phaseInvert flips both channels' sign; stereoSwap exchanges left/right. False restores the original. Five-ms matrix ramp prevents a discontinuous switch; model completion does not acknowledge DSP settling. One Undo; no plugin state/transport/device reset."})JSON"));
        result["required"].getArray()->add("enabled");
    }
    return result;
}
inline juce::var mixerStereoCompletionSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":true,
      "required":["completionState","completionScope","dspSettlingAcknowledged"],
      "properties":{"completionState":{"type":"string","enum":["completed"]},
      "completionScope":{"type":"string","enum":["canonicalMixerStereoTarget"]},
      "dspSettlingAcknowledged":{"type":"boolean","const":false}}})JSON");
}
} // namespace vibera
