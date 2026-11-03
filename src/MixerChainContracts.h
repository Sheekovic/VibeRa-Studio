// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline juce::var mixerChainPluginStateSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","properties":{
      "mainInputBus":{"type":"integer","minimum":0,"maximum":31,"default":0},
      "mainOutputBus":{"type":"integer","minimum":0,"maximum":31,"default":0,"description":"One selected stereo native output; no multi-output graph."},
      "mainInputChannels":{"type":"integer","enum":[1,2],"default":2},
      "mainInputMonoSource":{"type":"integer","enum":[0,1,2,3],"default":0,"description":"Stereo0; mono1 left,2 right,3 average; actual layout negotiation required."},
      "wet":{"type":"number","minimum":0,"maximum":1,"default":1,"description":"Saved host wet target, not native parameter or actual ramp progress. Instruments require1."},
      "chainBypassed":{"type":"boolean","description":"Current containing Mixer chain override;false for instruments."},
      "effectiveBypass":{"type":"boolean","description":"Model target:slot bypass OR chain override. Does not acknowledge healthy ramp completion or graph adoption."}}})JSON");
}
inline juce::var mixerChainControlArguments(const juce::String &command)
{
    return juce::JSON::parse(
        command == "plugin.effect.mix"
            ? R"JSON({"type":"object","additionalProperties":false,"required":["target","value"],"properties":{
            "target":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Required stable Mixer effect ID, never Channel/instrument or slot index."},
            "value":{"type":"number","minimum":0,"maximum":1,"default":1,"description":"Required linear host wet fraction:0 latency-aligned dry,1 fully processed. Independent of native plugin mix parameters. Healthy changes ramp over five milliseconds; workers keep processing at0/bypass. Preserves native opaque state, slot bypass and automation bindings. One canonical Undo; no-op preserves history. Read wet and effectiveBypass from plugin list or Mixer inspection. Model completion is not DSP settling/audio acknowledgement."}}})JSON"
            : R"JSON({"type":"object","additionalProperties":false,"required":["mixer","enabled"],"properties":{
            "mixer":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required existing Mixer insert index, including Master0. Empty chains retain this setting for later insertions."},
            "enabled":{"type":"boolean","default":false,"description":"Required chain bypass override. True makes each slot effectively bypassed while preserving its own bypass/wet/state and latency. False restores those per-slot targets. Existing five-millisecond slot bypass transitions apply; no whole-chain constant-power crossfade, native suspension, CPU-save or tail-stop promise. One Undo. Read effectsBypassed and per-slot effectiveBypass. Committed model is distinct from DSP settling."}}})JSON");
}
} // namespace vibera
