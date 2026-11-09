// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MacroConfiguration.h"
#include "McpContracts.h"
namespace vibera
{
inline bool mixerMacroCommandSupported(const juce::String &id)
{
    return Customization::macroAllowed(id);
}
inline bool mixerMacroSupported(const Macro &macro)
{
    return std::all_of(macro.steps.begin(),macro.steps.end(),
                        [](const auto &step) { return mixerMacroCommandSupported(step.action); });
}
inline juce::var mixerMacroArguments(const juce::String &id, bool context)
{
    auto result = commandArgumentSchema(id);
    if (!context) return result;
    auto *required = result["required"].getArray();
    if (!required) return result;
    for (int i = required->size(); --i >= 0;)
    {
        const auto field = (*required)[i].toString();
        if (field == "pattern" || (field == "channel" && id != "audio.add") ||
            ((id.startsWith("mixer.") || isMixerAutomationTargetCommand(id)) && field == "mixer") ||
            (isMixerGroupCommand(id) && (field == "ids" || field == "mixerGroup")) ||
            (id.startsWith("notes.") && id != "notes.slice" && field == "ids") ||
            ((id.startsWith("clip.") || id == "audio.trim") && (field == "ids" || field == "target")) ||
            ((id == "rack.group.assign" || id.startsWith("steps.fill") || id == "rack.muteSelected" ||
              id == "rack.unmuteSelected" || id == "channel.moveUp" || id == "channel.moveDown" ||
              id == "rack.swingMix" || id == "rack.truncateSwing" || id == "rack.muteLock" ||
              id == "rack.properties" || id == "rack.sort") &&
             field == "ids"))
            required->remove(i);
    }
    return result;
}
inline juce::var mixerMacrosTool()
{
    auto tool = juce::JSON::parse(R"JSON({"name":"vibera_macros","description":"Inspect/persist saved macros through the SAME Customization supported-command predicate, authoritative command schemas and Settings application service. Run the real saved-recipe runner with one history transaction, or its existing asynchronous Channel-routing service. bindShortcut/unbindShortcut/resetShortcuts edit keyboard shortcuts in the same profile, like Settings > Shortcuts (not Undo; generation-guarded; read back in shortcuts). Other settings editing remains outside this tool. Completed means canonical history/configuration, never DSP/native readiness; routing runs return their actual pending operation for vibera_mixer routingJob polling.",
      "inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{
        "action":{"type":"string","enum":["state","create","update","remove","run","bindShortcut","unbindShortcut","resetShortcuts"]},
        "includeShortcuts":{"type":"boolean","default":false,"description":"state only: also list every keyboard shortcut (shortcut actions always return them)."},
        "shortcut":{"type":"object","additionalProperties":false,"required":["command","scope"],"properties":{
          "command":{"type":"string","minLength":1,"maxLength":256,"description":"Registered action or saved macro.* ID."},
          "scope":{"type":"integer","minimum":0,"maximum":7,"description":"Command scope, as in vibera_commands (0 global)."},
          "key":{"type":"integer","minimum":1,"maximum":65535,"description":"bindShortcut: JUCE key code (letters upper-case, e.g. 75 = K)."},
          "ctrl":{"type":"boolean","default":false},"shift":{"type":"boolean","default":false},"alt":{"type":"boolean","default":false},
          "alternate":{"type":"boolean","default":false,"description":"bindShortcut: keep the existing shortcut and add this one."}},
          "description":"bindShortcut/unbindShortcut. A key already used by another command in the same scope is refused (shortcut.conflict). unbind removes every shortcut of the command in that scope; resetShortcuts restores the familiar defaults and keeps macros."},
        "id":{"type":"string","minLength":6,"maxLength":256,"description":"Stable macro.* ID; state optionally returns its complete recipe, remove/run require it. No name or row alias."},
        "generation":{"type":"string","minLength":64,"maxLength":64,"pattern":"^[0-9a-f]{64}$","description":"Required unchanged state generation for create/update/remove/run. Includes canonical settings and exact saved-profile identity. Re-read after any profile edit; stale refuses before work."},
        "includeSchemas":{"type":"boolean","default":false,"description":"state only: registry-derived explicit/current-context command schemas for this bounded Mixer recipe subset."},
        "macro":{"type":"object","additionalProperties":false,"required":["id","name","steps"],"properties":{
          "id":{"type":"string","minLength":6,"maxLength":256},"name":{"type":"string","minLength":1,"maxLength":128},
          "steps":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object","additionalProperties":false,"required":["action","parameters"],"properties":{
            "action":{"type":"string","description":"Exact actual Customization-supported canonical command ID from state.supportedCommands/vibera_commands; no toolbar simulation."},
            "parameters":{"type":"object","description":"Validated against registry-derived command schema; state includeSchemas supplies explicit/current-context forms."},
            "currentContext":{"type":"boolean","default":true,"description":"Resolves active insert/selection/group at run. Group creation passes its new UUID to following context steps; explicit targets remain exact IDs."}}}}}}}}})JSON");
    tool.getDynamicObject()->setProperty("outputSchema", juce::JSON::parse(R"JSON({
      "type":"object","additionalProperties":false,
      "required":["completionState","completionScope","generation","profileVersion","profileReadbackScope","profileExists","profileMatchesMemory","configurationCommitted","configurationUndoable","dspSettlingAcknowledged","cancellation","macros","supportedCommands","projectRevision","projectIdentity"],
      "properties":{
        "completionState":{"type":"string","enum":["pending","completed","failed"]},
        "completionScope":{"type":"string","enum":["canonicalMacroRun","macroConfigurationReadback","persistedMacroConfiguration"]},
        "generation":{"type":"string","description":"SHA256 of canonical full settings and exact persisted bytes, or absent marker."},
        "profileVersion":{"type":"integer","minimum":1,"maximum":18},
        "profileReadbackScope":{"type":"string","enum":["preRunAdmission","completionReadback"],"description":"Run retains the exact admitted settings snapshot; later external profile changes do not conceal accepted async routing. Re-inspect state for current configuration."},
        "profileExists":{"type":"boolean"},"profileMatchesMemory":{"type":"boolean"},
        "configurationCommitted":{"type":"boolean","description":"True only for actual successful create/update/remove disk and memory publication; run is a musical history action."},
        "configurationUndoable":{"type":"boolean","const":false},"dspSettlingAcknowledged":{"type":"boolean","const":false},
        "cancellation":{"type":"string"},"projectRevision":{"type":"integer","minimum":0},"projectIdentity":{"type":"string"},
        "operation":{"type":"integer","minimum":1,"maximum":9007199254740991},
        "routing":{"type":"object","description":"Actual shared routing-job state; poll vibera_mixer action=routingJob with operation until terminal. Published routing requires checked Undo; cancelRouting honestly reports tooLate."},
        "error":{"type":"object","required":["code","message"],"properties":{"code":{"type":"string"},"message":{"type":"string"}}},
        "macros":{"type":"array","maxItems":128,"items":{"type":"object","additionalProperties":false,
          "required":["id","name","stepCount","editable","persisted"],"properties":{
            "id":{"type":"string"},"name":{"type":"string"},"stepCount":{"type":"integer","minimum":1,"maximum":64},
            "editable":{"type":"boolean"},"persisted":{"type":"boolean"},
            "steps":{"type":"array","maxItems":64,"items":{"type":"object","required":["action","parameters","currentContext"],"properties":{
              "action":{"type":"string"},"parameters":{"type":"object"},"currentContext":{"type":"boolean"}}}}}}},
        "shortcuts":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["command","scope","key","ctrl","shift","alt","text"],"properties":{
          "command":{"type":"string"},"scope":{"type":"integer"},"key":{"type":"integer"},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"},"alt":{"type":"boolean"},"text":{"type":"string"}}}},
        "supportedCommands":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["id"],"properties":{
          "id":{"type":"string"},"explicitArguments":{"type":"object"},"currentArguments":{"type":"object"}}}}
      }})JSON"));
    return tool;
}
} // namespace vibera
