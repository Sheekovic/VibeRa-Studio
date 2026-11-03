// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepCompose.h"
#include "McpSchema.h"

namespace vibera
{
inline juce::var stepComposeRecipeState(const StepComposeRecipe &r)
{
    auto *o = new juce::DynamicObject;
    o->setProperty("channel", juce::int64(r.channel));
    o->setProperty("sequenceSteps", r.sequenceSteps);
    o->setProperty("mode", r.mode == StepComposeMode::manual   ? "manual"
                           : r.mode == StepComposeMode::everyN ? "every"
                                                               : "euclidean");
    o->setProperty("every", r.every);
    o->setProperty("pulses", r.pulses);
    o->setProperty("rotation", r.rotation);
    o->setProperty("seed", juce::int64(r.seed));
    o->setProperty("dice", r.dice);
    o->setProperty("loop", r.loop);
    o->setProperty("convertMelodic", r.convertMelodic);
    juce::Array<juce::var> manual;
    for (auto bit : r.manual)
        manual.add(int(bit));
    o->setProperty("manual", manual);
    return juce::var(o);
}
inline juce::var stepComposeRecipeSchema()
{
    return juce::JSON::parse(
        R"JSON({"type":"object","additionalProperties":false,"required":["channel"],"properties":{
      "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable instrument Channel ID, never a row index; Audio Clips excluded."},
      "sequenceSteps":{"type":"integer","minimum":1,"maximum":4096,"default":16,"description":"Source cycle in sixteenth-note cells; one cell=0.25 quarter-note beats. Replaces only source notes before cycle end."},
      "mode":{"type":"string","enum":["manual","every","euclidean"],"default":"every","description":"Original mask algorithm. Every starts at zero; Euclidean hits floor(k*steps/pulses)."},
      "every":{"type":"integer","minimum":1,"maximum":4096,"default":4,"description":"Every-N spacing, must not exceed sequenceSteps in every mode."},
      "pulses":{"type":"integer","minimum":0,"maximum":4096,"default":4,"description":"Euclidean hit count, must not exceed sequenceSteps in euclidean mode."},
      "rotation":{"type":"integer","minimum":-4096,"maximum":4096,"default":0,"description":"Signed cell shift; positive moves later, modulo cycle."},
      "seed":{"type":"integer","minimum":0,"maximum":4294967295,"default":0,"description":"Exact unsigned32 deterministic Dice seed."},
      "dice":{"type":"boolean","default":false,"description":"Redistribute generated hit count deterministically, before rotation."},
      "loop":{"type":"boolean","default":true,"description":"True sets this Pattern part's fixed cycle; false removes its loop and may reveal retained source notes beyond the replacement window."},
      "convertMelodic":{"type":"boolean","default":false,"description":"Explicit consent to floor in-cycle melodic/off-grid notes to step cells and set length0.25. Otherwise refuse atomically."},
      "manual":{"type":"array","maxItems":4096,"items":{"type":"integer","minimum":0,"maximum":1},"description":"Manual mode needs exactly sequenceSteps binary values."}}})JSON");
}
inline juce::var stepComposeRecipesSchema()
{
    auto v = juce::JSON::parse(R"({"type":"array","minItems":1,"maxItems":128})");
    v.getDynamicObject()->setProperty("items", stepComposeRecipeSchema());
    return v;
}
inline juce::Result parseStepComposeRecipes(const juce::var &value, std::vector<StepComposeRecipe> &out)
{
    try
    {
        validateMcpSchema(stepComposeRecipesSchema(), value, "recipes");
    }
    catch (const McpFailure &e)
    {
        return juce::Result::fail("stepCompose.invalidArguments: " + juce::String(e.what()));
    }
    std::vector<StepComposeRecipe> parsed;
    for (const auto &v : *value.getArray())
    {
        StepComposeRecipe r;
        r.channel = juce::int64(v["channel"]);
        r.sequenceSteps = int(v.getProperty("sequenceSteps", 16));
        const auto mode = v.getProperty("mode", "every").toString();
        r.mode = mode == "manual"  ? StepComposeMode::manual
                 : mode == "every" ? StepComposeMode::everyN
                                   : StepComposeMode::euclidean;
        r.every = int(v.getProperty("every", 4));
        r.pulses = int(v.getProperty("pulses", 4));
        r.rotation = int(v.getProperty("rotation", 0));
        r.seed = std::uint32_t(juce::int64(v.getProperty("seed", juce::int64(0))));
        r.dice = bool(v.getProperty("dice", false));
        r.loop = bool(v.getProperty("loop", true));
        r.convertMelodic = bool(v.getProperty("convertMelodic", false));
        if (const auto *bits = v["manual"].getArray())
            for (const auto &bit : *bits)
                r.manual.push_back(std::uint8_t(int(bit)));
        std::vector<std::uint8_t> mask;
        if (auto checked = stepCompositionMask(r, mask); checked.failed())
            return checked;
        if (std::any_of(parsed.begin(), parsed.end(),
                        [&](const auto &old) { return old.channel == r.channel; }))
            return juce::Result::fail("stepCompose.duplicateChannel: Duplicate recipe Channel");
        parsed.push_back(std::move(r));
    }
    out = std::move(parsed);
    return juce::Result::ok();
}
inline juce::var stepComposeArguments()
{
    auto schema = juce::JSON::parse(
        R"({"type":"object","additionalProperties":false,"required":["pattern","recipes"],"properties":{
      "pattern":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Explicit stable Pattern ID. Atomic multi-Channel source-cycle replacement and loop settings; one Undo. Source beyond cycle survives; ordinary playback uses incremental scheduling. Document completion is not DSP adoption."}}})");
    schema["properties"].getDynamicObject()->setProperty("recipes", stepComposeRecipesSchema());
    schema["properties"].getDynamicObject()->setProperty(
        "retainSteps",
        juce::JSON::parse(
            R"({"type":"boolean","description":"Omit to follow Workflow muteRemovedSteps. True keeps off-mask notes muted and reactivates on-mask notes, preserving existing step properties/chord IDs. False uses legacy replacement deletion/mute behavior. Explicit melodic conversion still required; opening/unchanged interactive Accept applies no recipes and is a no-op."})"));
    return schema;
}
inline juce::var stepComposeSessionSchema()
{
    auto v = juce::JSON::parse(
        R"({"type":"object","additionalProperties":false,"required":["action"],"properties":{
      "action":{"type":"string","enum":["state","begin","update","select","reset","preview","solo","dice","manual","commit","cancel","play"],"description":"Solo requires enabled; temporary audition follows selection and restores opening mutes on Accept/Cancel. Dice advances the current draft seed; manual freezes its final mask."},
      "operation":{"type":"string","maxLength":100,"description":"Exact pending token required for every mutation after begin. Read state to recover after disconnect."},
      "pattern":{"type":"integer","minimum":1,"maximum":1000000000},
      "channels":{"type":"array","minItems":1,"maxItems":128,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000},"description":"Begin requires explicit playable Channel IDs. MIDI target is unchanged."},
      "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Select retained draft; never changes live MIDI recipient."},
      "all":{"type":"boolean","default":false,"description":"Reset restores opening draft for current Channel or all targets."},
      "enabled":{"type":"boolean","description":"Required for preview or solo. Preview false restores opening score while retaining drafts and any active temporary Solo. Solo respects canonical mute locks."},
      "leaseMs":{"type":"integer","minimum":250,"maximum":120000,"default":30000,"description":"Remote idle timeout cancels and restores baseline. State reads do not renew it. Opening is nonmutating. Active recording is incompatible with the exclusive preview lease."},
      "projectIdentity":{"type":"string","maxLength":100,"description":"Optional exact current project session guard; stale requests refuse before cancelling or modifying any session."}}})");
    v["properties"].getDynamicObject()->setProperty("recipes", stepComposeRecipesSchema());
    return v;
}
inline juce::var stepComposeSessionOutputSchema()
{
    auto v = juce::JSON::parse(R"({"type":"object","additionalProperties":false,
      "required":["completionState","playing","dspSettlingAcknowledged","pendingAudioUpdate"],"properties":{
      "completionState":{"type":"string","enum":["idle","pending","completed","cancelled"],"description":"Pending owns the live document preview; only Accept/Cancel/expiry reaches terminal state. Tool errors preserve the previous pending session for retry/cancel."},
      "playing":{"type":"boolean"},"dspSettlingAcknowledged":{"type":"boolean","const":false},
      "pendingAudioUpdate":{"type":"boolean"},"operation":{"type":"string","maxLength":100},
      "pattern":{"type":"integer","minimum":1},"activeChannel":{"type":"integer","minimum":1},
      "preview":{"type":"boolean"},"solo":{"type":"boolean"},"reason":{"type":"string"},"projectIdentity":{"type":"string"},
      "sourceRevision":{"type":"integer","minimum":0},"remote":{"type":"boolean"},
      "leaseMs":{"type":"integer","minimum":250,"maximum":120000},"dirtyRecipes":{"type":"integer","minimum":0,"maximum":128}}})");
    auto rows = stepComposeRecipesSchema();
    auto row = rows["items"];
    juce::Array<juce::var> required;
    for (const auto &field : row["properties"].getDynamicObject()->getProperties())
        required.add(field.name.toString());
    required.add("name");
    required.add("mask");
    row.getDynamicObject()->setProperty("required", required);
    row["properties"].getDynamicObject()->setProperty(
        "name", juce::JSON::parse(R"({"type":"string","readOnly":true})"));
    row["properties"].getDynamicObject()->setProperty(
        "mask",
        juce::JSON::parse(
            R"({"type":"array","minItems":1,"maxItems":4096,"items":{"type":"integer","minimum":0,"maximum":1},"readOnly":true,"description":"Final post-Dice/rotation cells; not an accepted update field."})"));
    v["properties"].getDynamicObject()->setProperty("recipes", rows);
    return v;
}
} // namespace vibera
