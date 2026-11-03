// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Groove.h"
namespace vibera
{
inline juce::var grooveArgumentSchema()
{
    return juce::JSON::parse(
        R"JSON({"type":"object","additionalProperties":false,"required":["format","version","name","cycleBeats","points"],"properties":{
      "format":{"type":"string","const":"VibeRa.Groove","description":"Original VibeRa template format; proprietary FSC payloads are not supported."},
      "version":{"type":"integer","const":1},"name":{"type":"string","minLength":1,"maxLength":96},
      "cycleBeats":{"type":"number","minimum":0.03125,"maximum":65536,"description":"Repeating period in quarter-note beats; origin is Pattern zero, independent of time signature."},
      "points":{"type":"array","minItems":1,"maxItems":4096,"description":"Strictly ascending unique starts; pitch is not stored. Capture keeps lowest stable ID at each chord onset.","items":{"type":"object","additionalProperties":false,"required":["beat","length","velocity"],"properties":{"beat":{"type":"number","minimum":0,"description":"Onset within cycle, less than cycleBeats."},"length":{"type":"number","exclusiveMinimum":0,"maximum":65536,"description":"Quarter-note beats; may cross the cycle boundary."},"velocity":{"type":"number","minimum":0,"maximum":1,"description":"Normalized level. Zero is an optional rest/deletion marker."}}}}
    }})JSON");
}
inline juce::var grooveToolDefinition()
{
    auto tool = juce::JSON::parse(
        R"JSON({"name":"vibera_groove","description":"Capture reusable note timing/velocity grooves, list original presets, validate/load/save bounded VibeRa.Groove templates and read active preview guides. These operations do not edit the project or create Undo; apply through canonical notes.quantize and optional vibera_gesture. File operations return completed only after actual synchronous I/O succeeds; no worker acceptance masquerades as completion.","inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{
      "action":{"type":"string","enum":["capture","presets","validate","load","save","preview"]},
      "pattern":{"type":"integer","minimum":1,"description":"Stable source Pattern for capture; defaults to active."},
      "channel":{"type":"integer","minimum":0,"description":"Stable source Channel for capture; default active. Explicit note IDs may include ghosts."},
      "ids":{"type":"array","maxItems":100000,"uniqueItems":true,"items":{"type":"integer","minimum":1},"default":[],"description":"Capture notes. Empty uses current Channel. Selected onsets outside cycle are rejected."},
      "cycleBeats":{"type":"number","minimum":0.03125,"maximum":65536,"description":"Capture period; defaults to source Pattern length. Keeps Pattern-zero phase; no implicit rebasing."},
      "name":{"type":"string","minLength":1,"maxLength":96,"default":"Captured groove"},
      "source":{"type":"string","enum":["current","previewBaseline"],"default":"current","description":"Capture from current score or original active Quantizer baseline (equivalent to modal Capture)."},
      "path":{"type":"string","minLength":1,"maxLength":32767,"description":"Absolute local template file. load limited to1MiB. No network operations."},
      "overwrite":{"type":"boolean","default":false,"description":"save refuses existing files unless true; never overwrites a directory."}
    }}})JSON");
    tool["inputSchema"]["properties"].getDynamicObject()->setProperty("groove", grooveArgumentSchema());
    return tool;
}
} // namespace vibera
