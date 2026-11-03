// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline juce::var mixerEffectsBrowserSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["open","targetCurrent","insert","projectIdentity","insertsPlugin"],"properties":{
        "open":{"type":"boolean","description":"Actual Settings Plugins component is visible; not OS foreground or a loaded plugin acknowledgement."},
        "targetCurrent":{"type":"boolean","description":"Target belongs to the current project identity."},
        "insert":{"type":["integer","null"],"minimum":0,"description":"Existing Mixer index,0 Master; null when unavailable/stale."},
        "projectIdentity":{"type":"string","description":"Current browser target binding, or empty when unavailable/stale."},
        "insertsPlugin":{"const":false,"description":"Opening this browser never creates a plugin or starts a scan."}
      }})JSON");
}
inline juce::var mixerEffectsPaneSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["visible","insert","offset","maximumOffset","visibleRows","effectCount","rowUnits"],"properties":{
        "visible":{"type":"boolean","description":"Logical pane has usable geometry; not physical-screen visibility."},
        "insert":{"type":"integer","minimum":0,"description":"Current selected Mixer index."},
        "offset":{"type":"integer","minimum":0,"description":"Clamped first visible slot; zero-based, not a plugin ID."},
        "maximumOffset":{"type":"integer","minimum":0},"visibleRows":{"type":"integer","minimum":0},
        "effectCount":{"type":"integer","minimum":0},"rowUnits":{"type":"string"},
        "wetGesture":{"type":["object","null"],"additionalProperties":false,
          "description":"Pending UI host wet edit, using the same canonical plugin.effect.mix/history as semantic mixerEffects(sourceInsert) gesture. Values in plugin list are live model previews; wetBlend is actual processing readback. Null is no UI owner, never proof of completed or audibly settled audio.",
          "required":["completionState","plugin","insert","projectIdentity","revision","valid"],"properties":{
            "completionState":{"const":"pending"},"plugin":{"type":"integer","minimum":1},"insert":{"type":"integer","minimum":0},
            "projectIdentity":{"type":"string"},"revision":{"type":"integer","minimum":0},"valid":{"type":"boolean"}}},
        "reorder":{"type":["object","null"],"additionalProperties":false,
          "description":"Pending UI-only grip intent; model/audio/history remain unchanged until drop's canonical plugin.effect.move. Null means no current intent, not proof of a successful move. Semantic equivalent is plugin.effect.move with projectIdentity/revision guards; read actual ordered IDs afterward.",
          "required":["completionState","plugin","insert","projectIdentity","sourceRevision","sourceSlot","destinationSlot","moved","valid"],
          "properties":{"completionState":{"type":"string","const":"pending"},"plugin":{"type":"integer","minimum":1},
            "insert":{"type":"integer","minimum":0},"projectIdentity":{"type":"string"},"sourceRevision":{"type":"integer","minimum":0},
            "sourceSlot":{"type":"integer","minimum":0,"maximum":15},"destinationSlot":{"type":["integer","null"],"minimum":0,"maximum":15},
            "moved":{"type":"boolean"},"valid":{"type":"boolean","description":"Exact source project/revision/insert/order/descriptor still match; application busy admission checked again on drop."}}}
      }})JSON");
}
inline juce::var mixerEffectsArguments(const juce::String &id)
{
    return juce::JSON::parse(
        id == "mixer.effects.browse"
            ? R"({"type":"object","additionalProperties":false,"properties":{"insert":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required existing Mixer index. Opens Settings Plugins for this project/insert; preserves class selection and scan configuration; no document/Undo/plugin state change."}},"required":["insert"]})"
            : R"({"type":"object","additionalProperties":false,"properties":{"offset":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required first visible slot of the current selected insert;0..effectsPane.maximumOffset. No document/Undo/native editor change."}},"required":["offset"]})");
}
} // namespace vibera
