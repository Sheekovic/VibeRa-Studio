// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline juce::var samplerTempoTool()
{
    return juce::JSON::parse(R"JSON({
      "name":"vibera_samplerTempo",
      "description":"Shared source-bound sampler tempo workflow. state reads prepared source and current settings; capture snapshots settings for manual confirmation; detect analyzes immutable PCM off-thread without editing. Poll job until completed/failed/cancelled. apply explicitly commits a captured or detected result as one Undo step. Cancellation never edits. Fit is RESAMPLE (changes pitch), not stretch. Beats are quarter notes in the trimmed source; bars use current document meter. Requires a prepared sample Channel, not a streamed Audio Clip or VST3. Sessions retain at most32 records; old finished tokens may expire.",
      "inputSchema":{"type":"object","additionalProperties":false,"properties":{
        "action":{"type":"string","enum":["state","capture","detect","job","cancel","apply"]},
        "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Stable Channel ID; required for state/capture/detect."},
        "operation":{"type":"string","minLength":1,"maxLength":100,"description":"Exact token from capture/detect; required for job/cancel/apply."},
        "sourceBpm":{"type":"number","minimum":20,"maximum":400,"description":"apply only. Explicit source BPM; omitted uses the completed estimate or captured source tempo."},
        "beats":{"type":"number","minimum":0.0625,"maximum":2048,"description":"apply only. Musical duration of the trimmed source, in quarter notes; exclusive with bars/sourceBpm. Enables fit by default."},
        "bars":{"type":"integer","minimum":1,"maximum":512,"description":"apply only. Trimmed source duration in current document bars; exclusive with beats/sourceBpm. Enables fit by default."},
        "fit":{"type":"boolean","description":"apply only. Omitted preserves captured fit for BPM, enables fit for beats/bars. Resampling changes pitch with duration."},
        "projectIdentity":{"type":"string","maxLength":100,"description":"Optional current project-session guard; old job/cancel tokens remain queryable after project replacement."}
      },"required":["action"]},
      "outputSchema":{"type":"object","additionalProperties":true,"properties":{
        "completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},
        "phase":{"type":"string","enum":["state","analysis","confirmation","applied"]},
        "operation":{"type":"string"},"channel":{"type":"integer"},
        "stale":{"type":"boolean"},"applied":{"type":"boolean"},"documentChanged":{"type":"boolean"},
        "estimateBpm":{"type":"number"},"sourceBpm":{"type":"number"},"fit":{"type":"boolean"},
        "trimmedSeconds":{"type":"number"},"sourceSeconds":{"type":"number"},
        "error":{"type":"string"},"message":{"type":"string"},"audioUpdatePending":{"type":"boolean"},
        "operations":{"type":"array","items":{"type":"string"},"description":"Current-project retained session tokens for this Channel, including UI-created sessions. Poll job for actual outcome."},
        "method":{"type":"string","enum":["resample"]},"changesPitch":{"type":"boolean"},
        "projectIdentity":{"type":"string"},"sourceProjectIdentity":{"type":"string"}
      }},"annotations":{"readOnlyHint":false,"destructiveHint":false,"openWorldHint":false}
    })JSON");
}
} // namespace vibera
