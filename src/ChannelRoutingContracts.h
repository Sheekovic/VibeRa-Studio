// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
// Authoritative operation receipt shared by Studio readback and registry discovery.
// No Studio/model/audio instance is needed to discover this contract.
inline juce::var channelRoutingResultSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["operation","projectIdentity","revision","retention","pendingAudioUpdate","completionState","state"],
      "properties":{"operation":{"type":"integer","minimum":0,"maximum":9007199254740991},
        "projectIdentity":{"type":"string","description":"Operation source session; historical jobs retain their original identity."},
        "revision":{"type":"integer","minimum":0,"description":"Current Studio revision; use destinationRevision for the historical job."},
        "currentProjectIdentity":{"type":"string"},"currentRevision":{"type":"integer","minimum":0},
        "retention":{"type":"string"},"pendingAudioUpdate":{"type":"boolean"},"audioProjectionDeferred":{"type":"boolean"},
        "completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},
        "state":{"type":"string","enum":["none","pending","completed","failed","cancelled"]},
        "stage":{"type":"string","description":"Structural effect phase: capture,workers,prepareGraph,membership,settling,cancelling or terminal; empty for existing synchronous routing preparation."},
        "committed":{"type":"boolean"},"documentCommitted":{"type":"boolean"},
        "audioDisposition":{"type":"string","enum":["liveGraph","deferredNoDevice","noAudioWork"]},
        "completionScope":{"type":"string","enum":["document","documentAndAudio","noChange"]},"audioPosted":{"type":"boolean"},"documentChanged":{"type":"boolean"},"noOp":{"type":"boolean"},
        "nativeCompletionKnown":{"type":"boolean"},"sourceRevision":{"type":"integer","minimum":0},
        "destinationRevision":{"type":"integer","minimum":0},"postedSerial":{"type":"integer","minimum":0},
        "selectedSerial":{"type":"integer","minimum":0},"nativePostedSerial":{"type":"integer","minimum":0},
        "referenceStart":{"type":"integer","description":"Actual selected callback reference sample start, not beat time."},
        "referenceEnd":{"type":"integer","description":"Half-open actual callback reference sample end."},
        "errorCode":{"type":"string"},"message":{"type":"string"},"cancelAvailability":{"type":"string"},
        "error":{"type":"object","additionalProperties":false,"required":["code","message"],
          "properties":{"code":{"type":"string"},"message":{"type":"string"}}}}})JSON");
}

inline juce::var channelMixerAssignmentArguments()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "properties":{
        "channel":{"type":"integer","minimum":1,"maximum":1000000000,
          "description":"Existing stable Channel ID from vibera_state.channels or vibera_inspect entity=channels; never Channel Rack row or plugin ID. Omit to use the current selectedChannel. Explicit0/all-Channels targeting is not supported."},
        "mixer":{"type":"integer","minimum":0,"maximum":2147483647,"default":0,
          "description":"Existing stable Mixer index from vibera_inspect entity=mixer;0 is Master, not a row position. Omitted meansMaster0. Must exist in the current project. Sparse indices are supported up to2147483647; this does not create an insert or treat an ordinal as its identity."}}})JSON");
}
} // namespace vibera
