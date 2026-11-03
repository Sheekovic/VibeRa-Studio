// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline const char *mixerChainAction(const juce::String &id) noexcept
{
    if (id == "mixer.chain.paste")
        return "paste";
    if (id == "mixer.chain.loadApply")
        return "loadApply";
    if (id == "mixer.chain.copy")
        return "copy";
    if (id == "mixer.chain.save")
        return "save";
    if (id == "mixer.chain.loadClipboard")
        return "loadClipboard";
    if (id == "mixer.chain.directory")
        return "configureDirectory";
    if (id == "mixer.chain.cancel")
        return "cancel";
    return nullptr;
}
inline juce::var mixerChainWorkflowArguments()
{
    return juce::JSON::parse(
        R"JSON({"type":"object","additionalProperties":false,"required":["action"],"properties":{
      "action":{"type":"string","enum":["state","copy","save","loadClipboard","paste","loadApply","configureDirectory","job","cancel"]},
      "mixer":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required existing destination for paste/loadApply or source for copy/save; Master0 supported. Sparse stable insert index, not display row."},
      "projectIdentity":{"type":"string","minLength":1,"maxLength":100,"description":"Required exact current project session for copy/save/paste/loadApply. Discover current identity first."},
      "revision":{"type":"integer","minimum":0,"maximum":9007199254740991,"description":"Required exact current musical revision for copy/save/paste/loadApply; checked again after native capture, file loading and immediately before structural commit as applicable."},
      "path":{"type":"string","minLength":1,"maxLength":240,"description":"Required save/loadClipboard/loadApply absolute LOCAL .vibfx file, direct child of configured existing preset directory. No network, links, device paths or implicit overwrite."},
      "directory":{"type":"string","minLength":1,"maxLength":200,"description":"Required configureDirectory absolute existing LOCAL ordinary directory. No mkdir. One session worker; active work refuses reconfiguration, preserving old root."},
      "overwrite":{"type":"boolean","default":false,"description":"Save only. Explicit permission for atomic replacement; absent means no replacement."},
      "operation":{"type":"string","minLength":1,"maxLength":100,"description":"Required exact session-scoped job token for job/cancel. Compact terminal receipts live until Studio closes; opaque chains/native handles are not retained in job history."}}})JSON");
}
inline juce::var mixerChainWorkflowResultSchema()
{
    auto canonical = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["completionState","code","message","documentChanged","undoable"],"properties":{
      "completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},
      "code":{"type":"string"},"message":{"type":"string"},"operation":{"type":"string"},
      "action":{"type":"string"},"phase":{"type":"string"},"projectIdentity":{"type":"string"},
      "revision":{"type":"integer","minimum":0},"mixer":{"type":"integer","minimum":0},"path":{"type":"string"},
      "fileCommitted":{"type":"boolean"},"nativeError":{"type":"integer","minimum":0},
      "temporaryPath":{"type":"string"},"temporaryRemaining":{"type":"boolean"},"cleanupError":{"type":"integer","minimum":0},
      "documentChanged":{"type":"boolean"},"undoable":{"type":"boolean"},
      "nativeRecoveryRequired":{"type":"boolean","description":"True when irreversible publication requires explicit native recovery; never interpret as successful active playback."},
      "recoveryOperation":{"type":"integer","minimum":1,"maximum":9007199254740991,"description":"Exact separately owned device-preparation operation, if automatic recovery was admitted. A chain timeout does not cancel or silently destroy this persistent owner; inspect vibera_mixer action=devicePreparation with this operation."},
      "audioState":{"type":"string","enum":["preparing","pending","active","deferredNoDevice","fault"],"description":"Apply audio projection state. Queued preparation/model acceptance is not actual active playback; deferredNoDevice has no active device. Published activation has a10000ms QPC bound; timeout fails retaining intended model/history with nativeRecoveryRequired."},
      "editorClosures":{"type":"array","maxItems":2192,"items":{"type":"object","additionalProperties":false,
        "required":["plugin","instance","serial","outcome","nativeCompletionKnown","dispatched","windowExists"],"properties":{
        "plugin":{"type":"integer","minimum":1},"instance":{"type":"string"},"serial":{"type":"integer","minimum":1},
        "outcome":{"type":"string"},"nativeCompletionKnown":{"type":"boolean"},"dispatched":{"type":"boolean"},
        "windowExists":{"type":"boolean"}}}},
      "provenance":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["plugin","instance","stateSource"],"properties":{
        "plugin":{"type":"integer","minimum":1},"instance":{"type":"string"},"revision":{"type":"integer","minimum":0},
        "stateSource":{"type":"string","enum":["nativeAcknowledgement","retainedProjectBytes","retainedRecoveryBytes"]}}}},
      "state":{"type":"object","additionalProperties":false,"required":["directory","available","busy","activeOperation","clipboardAvailable","clipboard","sessionHistory"],"properties":{
        "directory":{"type":"string"},"available":{"type":"boolean"},"busy":{"type":"boolean"},"activeOperation":{"type":"string"},
        "clipboardAvailable":{"type":"boolean"},"sessionHistory":{"type":"integer","minimum":0},
        "clipboard":{"type":"object","additionalProperties":false,"properties":{"name":{"type":"string"},"effectsBypassed":{"type":"boolean"},
          "sourceProjectIdentity":{"type":"string"},"sourceRevision":{"type":"integer","minimum":0},"sourceInsert":{"type":"integer","minimum":0},
          "slots":{"type":"array","maxItems":16,"items":{"type":"object","additionalProperties":false,"required":["name","bypass","wet"],"properties":{"name":{"type":"string"},"bypass":{"type":"boolean"},"wet":{"type":"number","minimum":0,"maximum":1}}}}}}}}}})JSON");
    // The stdio server can reject malformed wire arguments before application
    // dispatch. Advertise that exact non-admitted failure without weakening the
    // canonical service/result contract or bypassing transport validation.
    auto wireFailure = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["completionState","error"],"properties":{
        "completionState":{"type":"string","const":"failed"},
        "error":{"type":"object","additionalProperties":false,"required":["code","message","retryable"],
          "properties":{"code":{"type":"string","minLength":1},"message":{"type":"string"},"retryable":{"type":"boolean","const":false}}}}})JSON");
    auto schema = juce::var(new juce::DynamicObject);
    schema.getDynamicObject()->setProperty("type", "object");
    schema.getDynamicObject()->setProperty("oneOf", juce::Array<juce::var>{canonical, wireFailure});
    return schema;
}
inline juce::var mixerChainWorkflowTool()
{
    auto tool = juce::var(new juce::DynamicObject);
    tool.getDynamicObject()->setProperty("name", "vibera_mixerchain");
    tool.getDynamicObject()->setProperty(
        "description",
        "Shared Mixer chain Copy, atomic Save preset, Load to clipboard, destination Paste "
        "and Load-and-apply, plus session job/cancel and explicitly configured preset directory. "
        "Capture/file/preparation are asynchronous; poll until terminal. Paste/loadApply "
        "replace the destination chain through canonical one-Undo preparation and explicit "
        "audio suspend/reconfigure/resume; acceptance is not active playback. Copy/save/"
        "loadClipboard do not change Project/history/audio topology. Portable state excludes "
        "source IDs, routing and automation links. Referenced destination automation refuses "
        "rather than silently unlinking. Registration is not qualification; consult exact evidence.");
    tool.getDynamicObject()->setProperty("inputSchema", mixerChainWorkflowArguments());
    tool.getDynamicObject()->setProperty("outputSchema", mixerChainWorkflowResultSchema());
    return tool;
}
} // namespace vibera
