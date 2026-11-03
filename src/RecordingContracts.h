// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "McpSchema.h"
#include <cstdint>
namespace vibera
{
// Authoritative provider-neutral MIDI take contracts. Control-thread only;
// no capture/DSP implementation, opaque note editing or hardware claims here.
inline juce::var recordingStateSchema()
{
    return juce::JSON::parse(
        R"JSON({"type":"object","additionalProperties":false,"required":["serial","pending","capturing","uiUpdatePending","phase","noteCount","canRetry","canCancel"],"properties":{"serial":{"type":"integer","minimum":0,"maximum":9007199254740991,"description":"Current or last take identity, scoped to this application instance;0 means none. Retry/cancel require the exact positive current token."},"pending":{"type":"boolean","description":"Retained envelope after capture stops; capture acceptance is not note commit."},"capturing":{"type":"boolean"},"uiUpdatePending":{"type":"boolean"},"phase":{"type":"string","enum":["idle","capturing","sealing","ready","applying","invalidCapture","completed","cancelled"]},"committedRevision":{"type":"integer","minimum":0,"description":"Terminal revision; committed note count is meaningful only in completed phase."},"projectIdentity":{"type":"string"},"pattern":{"type":"integer","minimum":1},"placement":{"type":"integer","minimum":0},"song":{"type":"boolean"},"songBeat":{"type":"number","minimum":0,"description":"Original Song destination, quarter-note beats."},"noteCount":{"type":"integer","minimum":0,"description":"Retained or completed count; held notes and pending input are separate and partial invalid data is never a completed take."},"heldCount":{"type":"integer","minimum":0},"pendingInput":{"type":"boolean"},"failureCode":{"type":"string","enum":["none","take.preparation_failed","take.invalid_capture","take.source_changed","take.target_missing","take.placement_changed","take.busy","take.command_rejected"]},"captureFailure":{"type":"integer","minimum":0,"maximum":4,"description":"0 none,1 invalidTime,2 invalidEvent,3 noteQuota,4 captureInput."},"canRetry":{"type":"boolean","description":"Current admission only; memory or source validation can still refuse."},"canCancel":{"type":"boolean"}}})JSON");
}

inline const char *recordingActionForCommand(const juce::String &id) noexcept
{
    if (id == "recording.finish")
        return "finish";
    if (id == "recording.retry")
        return "retry";
    if (id == "recording.cancel")
        return "cancel";
    return nullptr;
}
inline juce::var recordingInputSchema()
{
    return juce::JSON::parse(
        R"JSON({"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","finish","retry","cancel"]},"serial":{"type":"integer","minimum":1,"maximum":9007199254740991,"description":"Required only for retry/cancel. Exact token from recording readback; no default/current-token substitution."},"offset":{"type":"integer","minimum":0,"maximum":2147483647,"default":0,"description":"State only: offset into currently retained complete note segments. Held notes/pending input remain separate."},"limit":{"type":"integer","minimum":1,"maximum":512,"default":128,"description":"State only: bounded read-only retained-note page; id0 is not a canonical project note ID."},"projectIdentity":{"type":"string","maxLength":100,"description":"Optional current project-session identity guard."}},"required":["action"]})JSON");
}
inline juce::var recordingOutputSchema()
{
    auto output = juce::JSON::parse(
        R"JSON({"type":"object","additionalProperties":false,"required":["action","completionState","recordArmed","documentChanged","recording","notes","notesTotal","notesOffset","notesPartial"],"properties":{"action":{"type":"string"},"completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},"recordArmed":{"type":"boolean","description":"Arm request only; not proof capture started or MIDI hardware worked."},"documentChanged":{"type":"boolean"},"recording":{"type":"object"},"notes":{"type":"array","maxItems":512,"items":{"type":"object","additionalProperties":false,"required":["id","channel","beat","length","pitch","velocity","releaseVelocity","releaseMidiVelocity"],"properties":{"id":{"type":"integer","const":0},"channel":{"type":"integer","minimum":1},"beat":{"type":"number","minimum":0,"description":"Pattern source quarter-note beats."},"length":{"type":"number","exclusiveMinimum":0,"description":"Quarter-note beats."},"pitch":{"type":"integer","minimum":0,"maximum":127},"velocity":{"type":"number","minimum":0,"maximum":1},"releaseVelocity":{"type":"number","minimum":0,"maximum":1,"description":"Normalized physical MIDI note-off velocity; synthetic closes0, not ADSR duration."},"releaseMidiVelocity":{"type":"integer","minimum":0,"maximum":127,"description":"Rounded MIDI note-off byte; retained source state, not native delivery acknowledgement."}}}},"notesTotal":{"type":"integer","minimum":0},"notesOffset":{"type":"integer","minimum":0},"notesPartial":{"type":"boolean","description":"True for capturing/sealing/invalid capture; notes are diagnostic incomplete segments, not a completed take."},"error":{"type":"object","additionalProperties":false,"required":["code","message"],"properties":{"code":{"type":"string"},"message":{"type":"string"}}}}})JSON");
    output["properties"].getDynamicObject()->setProperty("recording", recordingStateSchema());
    return output;
}
inline juce::var recordingToolDefinition()
{
    auto tool = juce::JSON::parse(
        R"JSON({"name":"vibera_recording","description":"Inspect, finish, retry or explicitly cancel the shared MIDI take owner. Arm/disarm with canonical vibera_command transport.record. Capture/pending is not note commit. Retry/cancel require the exact current positive take serial; completed/consumed tokens refuse. No MIDI injection, hardware claim or arbitrary audio-recording controls."})JSON");
    tool.getDynamicObject()->setProperty("inputSchema", recordingInputSchema());
    tool.getDynamicObject()->setProperty("outputSchema", recordingOutputSchema());
    return tool;
}
struct RecordingRequest
{
    juce::String action;
    std::uint64_t serial = 0; // 0 only state/finish; never substitutes a retry/cancel token.
    int offset = 0, limit = 128;
};
inline RecordingRequest parseRecordingRequest(const juce::var &args)
{
    validateMcpSchema(recordingInputSchema(), args);
    RecordingRequest request;
    request.action = args["action"].toString();
    const bool tokenAction = request.action == "retry" || request.action == "cancel";
    if (tokenAction != args.hasProperty("serial"))
        throw McpFailure("invalid_arguments", "retry/cancel require serial; state/finish do not accept it");
    if (request.action != "state" && (args.hasProperty("offset") || args.hasProperty("limit")))
        throw McpFailure("invalid_arguments", "Retained-note pagination applies only to state");
    // Schema has established exact finite integer types/ranges before conversion.
    if (tokenAction)
        request.serial = std::uint64_t(double(args["serial"]));
    request.offset = int(args.getProperty("offset", 0));
    request.limit = int(args.getProperty("limit", 128));
    return request;
}
inline juce::var recordingCommandArgumentSchema(const juce::String &id)
{
    const auto *action = recordingActionForCommand(id);
    if (!action)
        return juce::JSON::parse(R"({"type":"object","properties":{},"additionalProperties":false})");
    auto schema = recordingInputSchema();
    auto properties = schema["properties"];
    properties.getDynamicObject()->removeProperty("offset");
    properties.getDynamicObject()->removeProperty("limit");
    auto fixed = juce::JSON::parse(R"({"type":"string"})");
    fixed.getDynamicObject()->setProperty("enum", juce::Array<juce::var>{action});
    fixed.getDynamicObject()->setProperty("default", action);
    properties.getDynamicObject()->setProperty("action", fixed);
    if (id == "recording.finish")
        properties.getDynamicObject()->removeProperty("serial");
    else
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"action", "serial"});
    return schema;
}
} // namespace vibera
