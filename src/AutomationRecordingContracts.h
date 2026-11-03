// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline juce::var automationRecordingTool()
{
    return juce::JSON::parse(R"JSON({"name":"vibera_automationRecording",
      "description":"Shared Song-mode MIDI Mixer automation take. Arm one existing insert's gain/pan/separation and current sends; existing linked/initialized automation refuses. Controller values after pickup use actual producer source-clock stamps. Value is a real semantic manual edit recorded at backend source time, never MIDI injection or caller-supplied time. Finish creates ordinary Automation Clips in one Undo; cancel restores exact preview baseline. Pattern, loops, overwrite/overdub and native parameter recording are explicitly unsupported by this bounded increment. MIDI notes retain independent capture/history. No physical input/listening or DSP settling claim.",
      "inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{
        "action":{"type":"string","enum":["state","arm","value","finish","retry","cancel"]},
        "insert":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required arm: existing stable insert, Master allowed without sends."},
        "lane":{"type":"integer","minimum":0,"maximum":127,"default":0},
        "projectIdentity":{"type":"string","maxLength":100},"revision":{"type":"integer","minimum":0},
        "token":{"type":"string","minLength":1,"maxLength":100,"description":"Exact active take token; required value/finish/retry/cancel."},
        "control":{"type":"string","enum":["gain","pan","stereoSeparation","send"]},
        "sendTarget":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required only for control=send; exact existing destination,0 Master."},
        "value":{"type":"number","minimum":-1,"maximum":2,"description":"Actual target units: gain/send0..2,pan/separation-1..1."},
        "offset":{"type":"integer","minimum":0,"maximum":65536,"default":0,"description":"State only; active captured event pagination. After completion inspect actual canonical sources/points."},
        "limit":{"type":"integer","minimum":1,"maximum":512,"default":128}}},
      "outputSchema":{"type":"object","additionalProperties":false,"required":["completionState"],"properties":{
        "completionState":{"type":"string","enum":["completed","pending","failed","cancelled"]},
        "phase":{"type":"string","enum":["idle","armed","capturing","sealing","ready","invalid","failed","completed","cancelled"]},
        "token":{"type":"string"},"insert":{"type":"integer"},"eventCount":{"type":"integer","minimum":0,"maximum":65536},
        "sourceChannel":{"type":"integer"},"sourceEpoch":{"type":"integer"},"originBeat":{"type":"number"},
        "targets":{"type":"array","items":{"type":"object"}},"active":{"type":"boolean"},
        "cancelRequested":{"type":"boolean"},
        "events":{"type":"array","maxItems":512,"items":{"type":"object","additionalProperties":false,
          "required":["kind","owner","parameter","seconds","value"],"properties":{"kind":{"type":"integer"},"owner":{"type":"integer"},"parameter":{"type":"integer"},"seconds":{"type":"number"},"value":{"type":"number"}}}},
        "eventsOffset":{"type":"integer","minimum":0},"eventsTruncated":{"type":"boolean"},
        "pendingEvent":{"type":"object","additionalProperties":false,"required":["kind","owner","parameter","seconds","value"],
          "properties":{"kind":{"type":"integer"},"owner":{"type":"integer"},"parameter":{"type":"integer"},"seconds":{"type":"number"},"value":{"type":"number"}}},
        "documentChanged":{"type":"boolean"},"undoable":{"type":"boolean"},"revision":{"type":"integer"},
        "nativeParameterRecording":{"type":"boolean","const":false},"dspSettlingAcknowledged":{"type":"boolean","const":false},
        "completionScope":{"type":"string","const":"canonicalSongAutomationTake"},
        "error":{"type":"object","additionalProperties":false,"required":["code","message"],
          "properties":{"code":{"type":"string"},"message":{"type":"string"},"retryable":{"type":"boolean"}}}}}})JSON");
}
} // namespace vibera
