// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "SamplerModulationSettings.h"
#include "synth/SynthContracts.h"
#include "synth/SynthMcp.h"
#include "RackContracts.h"
#include "GrooveContracts.h"
#include "RecordingContracts.h"
#include "MixerEffectsContracts.h"
#include "MixerChainContracts.h"
#include "MixerChainWorkflowContracts.h"
#include "AutomationRecordingContracts.h"
#include "MixerInsertOrderContracts.h"
#include "MixerOrganisationContracts.h"
#include "MixerStereoContracts.h"
#include "MixerAutomationContracts.h"
#include "SamplerAutomationContracts.h"
#include "SamplerControllerContracts.h"
#include "ChannelRoutingContracts.h"
#include "ChannelAppearanceContracts.h"
#include "StepComposeContracts.h"
#include "StepExpression.h"
#include "NoteTiming.h"
#include "NoteRepeat.h"
namespace vibera
{
inline const CommandInfo *mcpCommandInfo(const juce::String &id)
{
    for (const auto &command : commandRegistry())
        if (command.id == id)
            return &command;
    return nullptr;
}
inline juce::var commandArgumentSchema(const juce::String &id)
{
    if (isSamplerControllerCommand(id))
        return samplerControllerArguments(id);
    if (isSamplerVoiceModeCommand(id))
    {
        const bool flag = id == "channel.sampler.mono" || id == "channel.sampler.portamento" ||
                          id == "channel.sampler.fullPortamento";
        auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "properties":{"channel":{"type":"integer","minimum":1,"maximum":1000000000,
          "description":"Stable existing local sampler Channel; native/VST3 instruments and Audio Clips refuse."}}})JSON");
        auto field = juce::var(new juce::DynamicObject);
        auto *value = field.getDynamicObject();
        value->setProperty("type", flag ? "boolean" : "number");
        value->setProperty("default",
                           flag ? juce::var(false) : juce::var(id.endsWith("slideTime") ? .06 : 0.));
        if (!flag)
        {
            value->setProperty("minimum", 0);
            value->setProperty("maximum", id.endsWith("slideTime") ? 10 : 64);
        }
        value->setProperty("description",
                           flag ? "Explicit original sampler voice-mode flag. Stored independently; no "
                                  "implicit changes to the other flags. Default false."
                           : id.endsWith("slideTime")
                               ? "Slide time in seconds0..10; default0.06. Original implementation, no exact "
                                 "FL curve/default claim."
                               : "Gate in quarter-note beats0..64;0 disables the automatic gate. Default0.");
        const auto name = flag ? "enabled" : "value";
        schema["properties"].getDynamicObject()->setProperty(name, field);
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"channel", name});
        schema.getDynamicObject()->setProperty(
            "description",
            "Canonical five-control sampler voice increment. Shared history/cancel and samplerVoices "
            "gesture. "
            "Read requested values in channels[].sampler and pendingAudioUpdate separately; model completion "
            "does not acknowledge a sounding voice. Per-note Porta/silent Slide/color-group behavior remains "
            "separate.");
        return schema;
    }
    if (id.startsWith("channel.sampler.modulation."))
    {
        const auto suffix = id.fromFirstOccurrenceOf("channel.sampler.modulation.", false, false);
        if (const auto *parameter = samplerModulationParameter(suffix.toStdString()))
        {
            auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
                "properties":{"channel":{"type":"integer","minimum":1,"maximum":1000000000,
                "description":"Stable local sampler Channel. Native/VST3 instruments and Audio Clips refuse."},
                "text":{"type":"string","description":"Explicit modulation destination; Pan supports LFO only."}}})JSON");
            auto *properties = schema["properties"].getDynamicObject();
            juce::Array<juce::var> targets;
            const bool lfo = suffix.startsWith("lfo");
            for (size_t i = 0; i < samplerModulationTargets.size(); ++i)
                if (lfo || i != 1)
                    targets.add(juce::String(samplerModulationTargets[i].data()));
            properties->getProperty("text").getDynamicObject()->setProperty("enum", targets);
            const auto field = parameter->boolean ? "enabled" : "value";
            auto value = juce::var(new juce::DynamicObject);
            value.getDynamicObject()->setProperty("type", parameter->boolean    ? "boolean"
                                                          : parameter->integral ? "integer"
                                                                                : "number");
            value.getDynamicObject()->setProperty("default", parameter->boolean
                                                                 ? juce::var(parameter->initial != 0)
                                                                 : juce::var(parameter->initial));
            if (!parameter->boolean)
            {
                value.getDynamicObject()->setProperty("minimum", parameter->minimum);
                value.getDynamicObject()->setProperty("maximum", parameter->maximum);
            }
            value.getDynamicObject()->setProperty(
                "description",
                parameter->maximum == 64
                    ? "Seconds, or quarter-note beats when the corresponding envelope/LFO Tempo flag is "
                      "enabled. Period is a complete LFO cycle."
                : suffix == "lfoShape"
                    ? "0 sine,1 triangle,2 square; original oscillator starts at rising zero crossing."
                : parameter->boolean
                    ? "Explicit on/off. Global shares oscillator phase across this Channel's "
                      "score/MIDI/typing/preview notes; delay/fade remains per-note."
                    : "Normalized depth/level or signed curve tension. Amount0 is neutral; signed amount "
                      "inverts modulation. Pitch depth1 is24 semitones; cutoff/resonance add before final "
                      "normalized clamp.");
            properties->setProperty(field, value);
            schema.getDynamicObject()->setProperty("required",
                                                   juce::Array<juce::var>{"channel", "text", field});
            schema.getDynamicObject()->setProperty(
                "description",
                "Canonical sampler envelope/LFO parameter. Shared UI/MCP/history and samplerModulation "
                "gesture ownership; no transport restart. Read channels[].sampler.modulation by target and "
                "pendingAudioUpdate separately. Full Volume envelope replaces legacy ADSR only when enabled "
                "and amount nonzero. Source/amplitude end limits other modulation release. Original "
                "curves/ranges, no exact FL DSP parity claim.");
            return schema;
        }
    }
    if (id == "channel.sampler.filterType" || id == "channel.sampler.filterCutoff" ||
        id == "channel.sampler.filterResonance")
    {
        auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "required":["channel","value"],"properties":{
            "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing local sample/original-instrument Channel; VST3, native Synth and Audio Clips refuse."},
            "value":{"type":"number","minimum":0,"maximum":1}}})JSON");
        auto *value = schema["properties"]["value"].getDynamicObject();
        if (id == "channel.sampler.filterType")
        {
            value->setProperty("type", "integer");
            value->setProperty("maximum", 5);
            value->setProperty("default", 1);
            value->setProperty("description",
                               "0 Off,1 LP12,2 LP24,3 HP12,4 BP12,5 BS12. Original VibeRa filters, not exact "
                               "vendor algorithms. LP modes at cutoff1/resonance0 bypass completely; other "
                               "modes never implicitly bypass.");
        }
        else if (id == "channel.sampler.filterCutoff")
        {
            value->setProperty("default", 1);
            value->setProperty("description",
                               "Normalized0..1;20*1000^value Hz, capped at45 percent of current output "
                               "sample rate. Default1. Use state sampler.filterCutoffHz and "
                               "filterSampleRate; source sample rate is not the output device rate.");
        }
        else
        {
            value->setProperty("default", 0);
            value->setProperty("description",
                               "Normalized0..1; Q0.7071067811865476..12 linearly. Default0. Resonance can "
                               "raise peaks; no automatic limiter or gain normalization is applied.");
        }
        schema.getDynamicObject()->setProperty(
            "description",
            "One canonical Undo; sounding local sample voices retain phase, source and envelopes. "
            "Coefficients/mode approach their targets over about5ms. Read state sampler filter fields and "
            "pendingAudioUpdate separately: document completion is not callback acknowledgement. Continuous "
            "MCP edits use samplerFilter gesture domain; cancel restores exact document/history. Preview "
            "source remains dry; audition with the sampler keyboard, live MIDI or score.");
        return schema;
    }
    if (id == "steps.compose") return stepComposeArguments();
    if (id == "pattern.extent")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "required":["pattern","text"],
          "allOf":[{"if":{"properties":{"text":{"const":"manual"}}},"then":{"properties":{"length":{"type":"number","minimum":0.25,"maximum":1000000,"multipleOf":0.25}}}}],
          "properties":{
            "pattern":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable Pattern ID."},
            "text":{"type":"string","enum":["auto","manual"],"description":"Auto follows stored source note ends and fixed Channel cycles, rounded to the effective Pattern bar, minimum one bar. Manual retains outside notes dormant. Existing Playlist windows never resize."},
            "length":{"type":"number","default":0.25,"description":"Manual only:0.25..1000000 quarter-note beats, multiples of0.25; validated by the canonical command. Ignored in Auto. One Undo; changing an active recorded Pattern window or meter requires Stop. Invalid/no-op requests preserve the take. Completion acknowledges document state, not DSP adoption."}}})JSON");
    if (id == "pattern.meter")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "required":["pattern","enabled"],"properties":{
            "pattern":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable Pattern ID."},
            "enabled":{"type":"boolean","description":"False inherits document meter. True sets this Pattern's independent grid/bar and Pattern playback clock. Song clock remains the document meter. Auto extent recomputes; Manual length/notes/Playlist windows retain quarter-note positions."},
            "numerator":{"type":"integer","minimum":1,"maximum":32,"default":4,"description":"Custom meter numerator; ignored when disabled."},
            "denominator":{"type":"integer","enum":[2,4,8,16],"default":4,"description":"Custom meter beat unit; ignored when disabled. One Undo; changing an active recorded Pattern window or meter requires Stop. Invalid/no-op requests preserve the take. No arbitrary tempo map or Song meter event is created."}}})JSON");
    if (id == "automation.source.clone")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "required":["target"],"properties":{
            "target":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable Automation source ID from vibera_inspect automationSources, never a placement/point/Channel ID."},
            "text":{"type":"string","enum":["clear","preserve"],"default":"clear","description":"clear: independent curve without target links. preserve: independent curve retaining the exact existing target owner IDs and mapping ranges. Both copy settings and allocate fresh source/point IDs, create no placement, and retain target initialization. One Undo; no native plugin capture."}}})JSON");
    if (id == "channel.muteLock" || id == "rack.muteLock")
    {
        auto schema = juce::JSON::parse(
            R"({"type":"object","additionalProperties":false,"required":["enabled"],"properties":{
          "enabled":{"type":"boolean","description":"Lock/unlock the current score mute. An actual change ends solo ownership without changing audible mutes; one Undo. Locked Channels are excluded from solo and bulk mute; explicit channel.mute refuses until unlocked."}}})");
        const bool bulk = id == "rack.muteLock";
        const char *key = bulk ? "ids" : "channel";
        schema["properties"].getDynamicObject()->setProperty(
            key,
            juce::JSON::parse(
                bulk
                    ? R"({"type":"array","minItems":1,"maxItems":128,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000},"description":"Explicit stable Channel IDs; atomic lock change, never UI row indices."})"
                    : R"({"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable Channel ID; no MIDI recipient or selection change."})"));
        schema["required"].getArray()->add(key);
        return schema;
    }
    if (id == "channel.properties" || id == "rack.properties" || id == "rack.sort")
        return channelAppearanceSchema(id);
    if (id == "channel.sampler.tempo")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["channel","value","enabled"],"properties":{
          "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing sampler or original-instrument Channel; not VST3/Audio Clip."},
          "value":{"type":"number","minimum":20,"maximum":400,"description":"Source BPM;20-400. Changes only source metadata, never project tempo or source PCM."},
          "enabled":{"type":"boolean","description":"Fit by Resample: duration and pitch change together. Both settings form one canonical Undo step. For source-bound asynchronous detection/confirmation or beat/bar length use vibera_samplerTempo."}}})");
    if (mixerChainAction(id)) return mixerChainWorkflowArguments();
    if (isMixerGroupCommand(id)) return mixerOrganisationArguments(id);
    if (isSamplerAutomationTargetCommand(id))
        return samplerAutomationArguments(id);
    if (isMixerAutomationTargetCommand(id)) return mixerAutomationArguments(id);
    if (isMixerStereoCommand(id)) return mixerStereoArguments(id);
    if (id == "mixer.move" || id == "mixer.moveLeft" || id == "mixer.moveRight")
        return mixerInsertOrderArguments(id);
    if (id == "channel.mixer")
        return channelMixerAssignmentArguments();
    if (id == "mixer.effects.browse" || id == "mixer.effects.scroll")
        return mixerEffectsArguments(id);
    if (id == "plugin.effect.mix" || id == "mixer.effects.bypass")
        return mixerChainControlArguments(id);
    if (id == "mixer.insert.create")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,"properties":{
          "text":{"type":"string","maxLength":64,"default":"","description":"Optional trimmed insert name; empty generates Insert N. Appends a neutral insert routed to Master. Existing18 defaults remain; maximum129 total. IDs are session-reserved and never display ordinals."},
          "value":{"type":"integer","minimum":0,"maximum":32,"default":1,"description":"How many inserts to add in one Undo step (0 or 1 adds one)."},
          "target":{"type":"integer","minimum":0,"maximum":2147483647,"default":0,"description":"Parallel chains (FX container): an existing non-Master insert. Each new insert (named '<source> Parallel N' unless text is given) receives a unity send from it and outputs to the same destinations at the same levels, so effects added to it run in parallel with the source's dry path. 0 adds ordinary inserts routed to Master."}}})JSON");
    if (id == "mixer.insert.delete")
        return juce::JSON::parse(
            R"JSON({"type":"object","additionalProperties":false,"required":["mixer"],"properties":{
          "mixer":{"type":"integer","minimum":1,"maximum":2147483647,"description":"Exact existing non-Master stable insert ID."},
          "mixerDelete":{"type":"object","additionalProperties":false,"properties":{
            "replacement":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Explicit surviving target for assigned Channels and incoming sends. Preserves send levels; self, duplicate destination and cycles refuse. Omitted refuses those references."},
            "removeEffects":{"type":"boolean","default":false,"description":"Explicitly remove this insert's freshly captured effect slots; old native state is owned by Undo."},
            "removeAutomation":{"type":"boolean","default":false,"description":"Explicitly unlink only deleted insert/effect targets and remove their initialized values. Curves, points, placements and unrelated links remain."},
            "removeGroupMembership":{"type":"boolean","default":false,"description":"Explicitly remove only this insert's display-group membership; groups and other members remain."}}}}})JSON");
    if (id == "plugin.io")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "required":["target","pluginIo"],"properties":{
            "target":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Exact existing instrument or effect ID."},
            "pluginIo":{"type":"object","additionalProperties":false,"required":["inputBus","outputBus","inputChannels","monoSource"],"properties":{
              "inputBus":{"type":"integer","minimum":0,"maximum":31},
              "outputBus":{"type":"integer","minimum":0,"maximum":31,"description":"One selected stereo native output, not a multi-output graph."},
              "inputChannels":{"type":"integer","enum":[1,2]},
              "monoSource":{"type":"integer","enum":[0,1,2,3],"description":"Stereo requires0; mono requires1 left,2 right or3 average. Main input must differ from assigned detector bus. Actual worker negotiation is authoritative."}}}}})JSON");
    if (id == "plugin.effect.sidechain")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "required":["target","detector"],"properties":{
            "target":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Exact surviving Mixer effect ID, not instrument/insert ID."},
            "detector":{"description":"null disconnects. Object requests a non-Master source insert, aux bus and mono/stereo channels. Requires active prepared device/source; actual worker layout negotiation must succeed before commit. No capability inferred from descriptor.",
              "anyOf":[{"type":"null"},{"type":"object","additionalProperties":false,"required":["sourceMixer","inputBus","channels"],"properties":{
                "sourceMixer":{"type":"integer","minimum":1,"maximum":2147483647},
                "inputBus":{"type":"integer","minimum":1,"maximum":31},
                "channels":{"type":"integer","enum":[1,2]}}}]}}})JSON");
    if (id == "plugin.effect.replace")
        return juce::JSON::parse(
            R"JSON({"type":"object","additionalProperties":false,"required":["target","text"],"properties":{
              "target":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Exact existing Mixer effect ID, not insert/row/Channel. Instruments refuse. Other slots retain their IDs/order/state/bindings."},
              "text":{"type":"string","minLength":1,"maxLength":65536,"description":"Scanned VST3 effect descriptor XML. Replacement creates a fresh stable ID/default native state; retains host slot wet/bypass and chain bypass. No raw captured-state or old parameter binding input."}}})JSON");
    if (id == "plugin.effect.move")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "properties":{"target":{"type":"integer","minimum":1,"maximum":1000000000,
            "description":"Required stable Mixer effect ID from vibera_plugin list; instruments/Channels are not effect slots."},
            "lane":{"type":"integer","minimum":0,"maximum":15,"default":0,
              "description":"Final zero-based index in this effect's current chain,0..chainSize-1. Omitted means0 for compatibility. No clamping; invalid destinations fail. Same-slot is a no-op without history/revision/dirty changes. One move preserves complete plugin state/bindings/identity and is one Undo. Requires no unrelated gesture, armed/retained take, project file work or recovery. Owned document gesture updates use the same admission. Model completion is not DSP graph adoption; inspect pendingAudioUpdate/audio qualification separately."}},"required":["target"]})JSON");
    if (id == "notes.properties")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "properties":{"pattern":{"type":"integer","minimum":1,"maximum":1000000000},
            "channel":{"type":"integer","minimum":1,"maximum":1000000000},
            "ids":{"type":"array","minItems":1,"maxItems":100000,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000}},
            "noteProperties":{"type":"object","additionalProperties":false,"minProperties":1,
              "properties":{"slide":{"type":"boolean"},"portamento":{"type":"boolean"},"muted":{"type":"boolean"},
                "colourGroup":{"type":"integer","minimum":0,"maximum":15},"pitch":{"type":"integer","minimum":0,"maximum":127},
                "velocity":{"type":"number","minimum":0,"maximum":1},"pan":{"type":"number","minimum":-1,"maximum":1},
                "fineCents":{"type":"number","minimum":-100,"maximum":100},"modX":{"type":"number","minimum":0,"maximum":1},
                "modY":{"type":"number","minimum":0,"maximum":1},"releaseVelocity":{"type":"number","minimum":0,"maximum":1},
                "beat":{"type":"number","minimum":0,"maximum":1000000},"length":{"type":"number","exclusiveMinimum":0,"maximum":1000000}},
              "description":"Sparse atomic Note Properties. Timing/pitch only for one selected Note; Manual-window dormant notes retained. Sampler-only fields refuse hosted targets. One exact Undo/gesture, no clamping or model-to-DSP completion claim."}},
          "required":["pattern","channel","ids","noteProperties"]})JSON");
    if (id == "notes.slide" || id == "notes.portamento" || id == "notes.colourGroup")
    {
        auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "properties":{"pattern":{"type":"integer","minimum":1,"maximum":1000000000},
            "channel":{"type":"integer","minimum":1,"maximum":1000000000},
            "ids":{"type":"array","minItems":1,"maxItems":100000,"uniqueItems":true,
              "items":{"type":"integer","minimum":1,"maximum":1000000000}}},
          "required":["pattern","channel","ids"]})JSON");
        const bool group = id == "notes.colourGroup";
        schema["properties"].getDynamicObject()->setProperty(
            group ? "value" : "enabled",
            juce::JSON::parse(
                group
                    ? R"({"type":"integer","minimum":0,"maximum":15,"description":"Source voice colour group0..15, UI1..16; not a DAW Channel or Rack editing group."})"
                    : R"({"type":"boolean","description":"Independent stored sampler Note mode. Local sampler edits only; Slide requires Repeat Off. Stored flags survive later source-role changes dormant."})"));
        schema["required"].getArray()->add(group ? "value" : "enabled");
        return schema;
    }
    if (recordingActionForCommand(id))
        return recordingCommandArgumentSchema(id);
    auto schema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,"properties":{
      "pattern":{"type":"integer","minimum":1,"description":"Stable Pattern ID; defaults to selected Pattern."},
      "channel":{"type":"integer","minimum":0,"description":"Stable Channel ID; defaults to selected Channel. Zero means all Channels only for operations explicitly supporting it."},
      "target":{"type":"integer","minimum":0,"default":0,"description":"Stable operation-specific clip/source/plugin/point ID."},
      "ids":{"type":"array","items":{"type":"integer","minimum":1},"maxItems":1024,"uniqueItems":true,"default":[],"description":"Stable target IDs, never row numbers; Patterns for grouping, source IDs for picker.place, notes/clips for their commands."},
      "beat":{"type":"number","default":0,"description":"Zero-based quarter-note beats; signed delta for move/duplicate/slip, normalized range start for sampler loopRange."},
      "length":{"type":"number","default":0.25,"description":"Quarter-note beats, except audio.add/trim uses seconds and sampler range uses normalized fractions. notes.move uses a signed duration delta with default zero."},
      "offset":{"type":"number","default":0,"description":"Source offset in seconds for Audio Clips."},
      "pitch":{"type":"integer","minimum":-127,"maximum":127,"default":60,"description":"MIDI pitch; notes.move uses a signed semitone delta with default zero."},
      "value":{"type":"number","default":0,"description":"Operation-specific value. Finite numbers only; authoritative musical validation remains in Controller."},
      "interval":{"type":"integer","enum":[2,4,8],"default":4,"description":"Sixteenth-note interval for steps.fill. Named fill2/fill4/fill8 actions fix their interval."},
      "mixer":{"type":"integer","minimum":0,"maximum":127,"default":0,"description":"Mixer insert index, zero is Master."},
      "lane":{"type":"integer","minimum":0,"maximum":127,"default":0,"description":"Zero-based Playlist lane; pattern.move instead uses zero-based selector destination."},
      "numerator":{"type":"integer","minimum":1,"maximum":32,"default":4},
      "denominator":{"type":"integer","enum":[2,4,8,16],"default":4},
      "enabled":{"type":"boolean","default":true},"step":{"type":"boolean","default":false},
      "grouping":{"type":"boolean","description":"Clip targeting override: omit follows Playlist grouping behavior; true expands groups, false bypasses while retaining membership."},
      "restore":{"type":"boolean","description":"Solo exit override. Omitted follows Workflow restore preference, initially true. False unmutes all Channels."},
      "text":{"type":"string","maxLength":65536,"default":"","description":"Operation-specific name, absolute sample path, VST3 descriptor, source kind or curve shape. See command prerequisites."},
      "generation":{"type":"string","maxLength":65536,"default":"","description":"Inert historical generation metadata; never proof that generation occurred."},
      "recordedNotes":{"type":"array","minItems":1,"maxItems":100000,"items":{"type":"object","additionalProperties":false,"properties":{"channel":{"type":"integer","minimum":1,"maximum":1000000000},"beat":{"type":"number","minimum":0},"length":{"type":"number","exclusiveMinimum":0},"pitch":{"type":"integer","minimum":0,"maximum":127},"velocity":{"type":"number","minimum":0,"maximum":1},"step":{"type":"boolean","default":false},"pan":{"type":"number","minimum":-1,"maximum":1,"default":0},"fineCents":{"type":"number","minimum":-100,"maximum":100,"default":0},"shiftPercent":{"type":"number","minimum":0,"maximum":100,"default":0},"repeatMode":{"type":"integer","minimum":0,"maximum":14,"default":0},"releaseVelocity":{"type":"number","minimum":0,"maximum":1,"default":0},"modX":{"type":"number","minimum":0,"maximum":1,"default":0.5},"modY":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"required":["channel","beat","length","pitch","velocity"]}},
      "binding":{"type":"object","additionalProperties":false,"properties":{"id":{"type":"integer","minimum":0,"maximum":4294967295},"flags":{"type":"integer","minimum":0,"maximum":4294967295},"steps":{"type":"integer","minimum":0,"maximum":2147483647},"manual":{"type":"number","minimum":0,"maximum":1},"name":{"type":"string","maxLength":256},"label":{"type":"string","maxLength":256}},"required":["id","flags","steps","manual"]}
    }})JSON");
    auto properties = schema["properties"];
    if (id == "automation.plugin.modulator" || id.startsWith("automation.modulator."))
        return modulatorArguments(id, properties["binding"]);
    if ((id.startsWith("automation.macro.") && id != "automation.macro.link") || id == "automation.plugin.macro.link")
        return macroArguments(id, properties["binding"]);
    if (id == "notes.add")
    {
        properties.getDynamicObject()->setProperty(
            "slide",
            juce::JSON::parse(
                R"({"type":"boolean","default":false,"description":"Create a silent local-sampler Slide instruction atomically, without an intermediate audible Note."})"));
        properties.getDynamicObject()->setProperty(
            "portamento",
            juce::JSON::parse(
                R"({"type":"boolean","default":false,"description":"Independent local-sampler Note Portamento flag; can coexist with Slide."})"));
        properties.getDynamicObject()->setProperty(
            "colourGroup",
            juce::JSON::parse(
                R"({"type":"integer","minimum":0,"maximum":15,"default":0,"description":"Per-note voice colour group0..15 (UI1..16); independent of Channel and Rack group."})"));
    }
    if (id == "audio.takes")
    {
        properties.getDynamicObject()->setProperty(
            "takes",
            juce::JSON::parse(
                R"({"type":"array","minItems":1,"maxItems":64,"description":"One window of the recorded file per take (loop pass): placed at beat on lane, reading offset..offset+duration seconds of the source. Clips are grouped; muted marks takes that do not play.","items":{"type":"object","additionalProperties":false,"properties":{"beat":{"type":"number","minimum":0},"lane":{"type":"integer","minimum":0,"maximum":127},"offset":{"type":"number","minimum":0},"duration":{"type":"number","exclusiveMinimum":0},"muted":{"type":"boolean","default":false},"source":{"type":"integer","minimum":0,"maximum":15,"default":0,"description":"Index into files (several input tracks)."}},"required":["beat","lane","offset","duration"]}})"));
        properties.getDynamicObject()->setProperty(
            "files",
            juce::JSON::parse(
                R"({"type":"array","minItems":1,"maxItems":16,"items":{"type":"string","maxLength":4096},"description":"Several input tracks recorded together: one absolute file each (all `length` seconds; files[0] equals text). Each gets its own Audio Clip Channel and take group; a take's source picks its file. With mixer > 0, file k feeds Mixer insert mixer + k."})"));
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"text", "length", "takes"});
    }
    if (id.startsWith("arrangement."))
    {
        auto describe = [&](const char *field, const char *text)
        { properties[field].getDynamicObject()->setProperty("description", text); };
        describe("target", "Arrangement ID from vibera_inspect entity=arrangements (select/rename/delete; 0 = current).");
        describe("text", "Name, at most 128 characters (add/clone/rename).");
    }
    if (id == "channel.sliceToChannels")
    {
        properties.getDynamicObject()->setProperty(
            "slicePoints",
            juce::JSON::parse(R"({"type":"array","minItems":2,"maxItems":64,"items":{"type":"number","minimum":0,"exclusiveMaximum":1},"description":"Slice starts as fractions of the sample, increasing, first 0."})"));
        properties["length"].getDynamicObject()->setProperty("description", "Source sample duration in seconds.");
    }
    if (id.startsWith("track."))
    {
        juce::String extra;
        if (id == "track.rename")
            extra = R"(,"text":{"type":"string","maxLength":128,"description":"Track name; empty restores Track n."})";
        else if (id == "track.colour")
            extra = R"(,"value":{"type":"number","minimum":0,"maximum":4294967295,"description":"Opaque ARGB colour (0xFFrrggbb), or 0 for the theme colour."})";
        else if (id == "track.mute")
            extra = R"(,"enabled":{"type":"boolean","description":"true mutes every clip on the track (clip mute flags are untouched)."})";
        else if (id == "track.group")
            extra = R"(,"enabled":{"type":"boolean","description":"true joins the group of the track above (consecutive grouped tracks form one group under its first track); false leaves it."})";
        else if (id == "track.collapse")
            extra = R"(,"enabled":{"type":"boolean","description":"true hides the group's other tracks in the Playlist (they still play); false shows them."})";
        else if (id == "track.lock")
            extra = R"(,"enabled":{"type":"boolean","description":"true protects the track's clips: clip edits that would add, move, change or remove them are refused."})";
        else if (id == "track.height")
            extra = R"(,"value":{"type":"number","minimum":0.5,"maximum":3,"description":"Row height relative to the Playlist lane height (0.5 small, 1 normal, 2 large, 3 extra large)."})";
        else if (id == "track.move")
            extra = R"(,"value":{"type":"integer","enum":[-1,1],"description":"-1 swaps with the track above, 1 with the one below (clips and settings)."})";
        else if (id == "track.instrument")
            extra = R"(,"channel":{"type":"integer","minimum":0,"description":"Sampler or instrument Channel ID the track belongs to (drawing on it makes Patterns for that Channel); 0 makes it a free track."})";
        else if (id == "track.audio")
            extra = R"(,"enabled":{"type":"boolean","description":"true makes it an audio track: audio recordings land on the first audio track with room (stacked passes use the tracks below it); false makes it a free track."})";
        else if (id == "track.instrumentClip")
            extra = R"(,"beat":{"type":"number","minimum":0,"description":"Where the new one-bar Pattern for the track's Channel is placed."})";
        const auto required = id == "track.insert" || id == "track.delete" || id == "track.solo" || id == "track.clone" ||
                                      id == "track.autoName" || id == "track.reset" // These take the lane only.
                                  ? R"(["lane"])"
                              : id == "track.rename"                      ? R"(["lane","text"])"
                              : id == "track.instrument"                  ? R"(["lane","channel"])"
                              : id == "track.instrumentClip"              ? R"(["lane","beat"])"
                              : id == "track.mute" || id == "track.group" || id == "track.collapse" || id == "track.lock" || id == "track.audio"
                                  ? R"(["lane","enabled"])"
                                                                          : R"(["lane","value"])";
        return juce::JSON::parse(
            juce::String(R"({"type":"object","additionalProperties":false,"properties":{"lane":{"type":"integer","minimum":0,"maximum":127,"description":")") +
            (id == "track.insert" ? "Insert an empty track here; this and later tracks move down one (the last must be empty)."
             : id == "track.delete" ? "Delete this track's clips and settings; later tracks move up one."
                                    : "Playlist track lane, 0-based (Track 1 is lane 0).") +
            R"("})" + extra + R"(},"required":)" + required + "}");
    }
    if (id.startsWith("meter.change."))
    {
        if (id == "meter.change.delete")
            return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"target":{"type":"integer","minimum":1,"description":"Time-signature change ID from vibera_inspect entity=meters."}},"required":["target"]})");
        return juce::JSON::parse(juce::String(R"({"type":"object","additionalProperties":false,"properties":{)") +
                                 (id == "meter.change.set" ? R"("target":{"type":"integer","minimum":1},)" : "") +
                                 R"("beat":{"type":"number","exclusiveMinimum":0,"description":"Song beat on a bar line of the time signature before it."},"numerator":{"type":"integer","minimum":1,"maximum":32},"denominator":{"type":"integer","enum":[2,4,8,16]}},"required":[)" +
                                 (id == "meter.change.set" ? R"("target",)" : "") + R"("beat","numerator","denominator"]})");
    }
    if (id == "performance.assignPad")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"value":{"type":"integer","minimum":1,"maximum":16,"description":"Pad number."},"pattern":{"type":"integer","minimum":0,"description":"Stable Pattern ID the pad launches; 0 restores the default (the n-th Pattern). Saved with the project (v55)."}},"required":["value","pattern"]})");
    if (id == "performance.saveScene")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"value":{"type":"integer","minimum":1,"maximum":8,"description":"Scene number."},"ids":{"type":"array","maxItems":1024,"items":{"type":"integer","minimum":1},"description":"Stable Pattern IDs the scene launches together (empty clears it). Saved with the project (v52); performance.scene1..8 launch it."}},"required":["value","ids"]})");
    if (id.startsWith("notes.controller."))
    {
        const juce::String common = R"("pattern":{"type":"integer","minimum":1,"description":"Stable Pattern ID (default selected)."})";
        const juce::String value = R"("value":{"type":"number","minimum":-1,"maximum":1,"description":"0..1 for CC/pressure, -1..1 for pitch bend, program = value*127."})";
        if (id == "notes.controller.add")
            return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{)" + common +
                                     R"(,"channel":{"type":"integer","minimum":1},"beat":{"type":"number","minimum":0},"text":{"type":"string","enum":["cc","pitchBend","pressure","program"],"default":"cc"},"pitch":{"type":"integer","minimum":0,"maximum":127,"description":"CC number (cc only)."},)" +
                                     value + R"(},"required":["channel","beat","value"]})");
        if (id == "notes.controller.set")
            return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{)" + common +
                                     R"(,"target":{"type":"integer","minimum":1,"description":"Controller event ID from vibera_inspect controllers."},)" +
                                     value + R"(},"required":["target","value"]})");
        if (id == "notes.controller.interpolate")
            return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{)" + common +
                                     R"(,"channel":{"type":"integer","minimum":1},"text":{"type":"string","enum":["cc","pitchBend","pressure"],"default":"cc"},"pitch":{"type":"integer","minimum":0,"maximum":127,"description":"CC number (cc only)."},"beat":{"type":"number","minimum":0,"description":"Range start (beats)."},"length":{"type":"number","exclusiveMinimum":0,"maximum":512,"description":"Range length (beats). The first and last events of the lane inside it stay; everything between becomes a straight line every 1/16 beat (one Undo)."}},"required":["channel","beat","length"]})");
        if (id == "notes.controller.lfo")
            return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{)" + common +
                                     R"(,"channel":{"type":"integer","minimum":1},"text":{"type":"string","enum":["cc","pitchBend","pressure"],"default":"cc","description":"Lane the LFO replaces for this Channel."},"pitch":{"type":"integer","minimum":0,"maximum":127,"default":1,"description":"CC number for text cc (1 = mod wheel)."},"target":{"type":"integer","minimum":0,"maximum":3,"default":0,"description":"Shape: 0 sine, 1 triangle, 2 square, 3 saw."},"length":{"type":"number","minimum":0.25,"maximum":64,"description":"Period in beats."},"value":{"type":"number","exclusiveMinimum":0,"maximum":1,"description":"Depth around the centre."},"offset":{"type":"number","minimum":-1,"maximum":1,"default":0.5,"description":"Centre: 0..1 for CC / pressure, -1..1 for pitch bend. One point per 1/16 beat over the Pattern; one Undo step."}},"required":["channel","length","value"]})");
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{)" + common +
                                 R"(,"target":{"type":"integer","minimum":1,"description":"Controller event ID to delete."}},"required":["target"]})");
    }
    if (id == "pattern.launchQuantum")
        return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
          "properties":{"pattern":{"type":"integer","minimum":1,"description":"Stable Pattern ID."},
          "value":{"type":"number","minimum":-1,"maximum":64,"description":"Performance launch step in beats: -1 one bar of the Song meter (default), 0 immediately, otherwise 0.0625..64. Saved with the Pattern (project v49)."}},
          "required":["pattern","value"]})JSON");
    if (id == "channel.mtsEsp")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"channel":{"type":"integer","minimum":1},"enabled":{"type":"boolean","default":true,"description":"Broadcast this Channel's tuning table or scale to every MTS-ESP client plugin (enabling clears it on other Channels; saved, project v56). Needs the user-installed MTS-ESP library (LIBMTS.dll) and no other master running; read vibera_inspect channels mtsEspActive."}},"required":["channel"]})");
    if (id == "channel.mpe")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"channel":{"type":"integer","minimum":1,"description":"Plugin instrument Channel."},"enabled":{"type":"boolean","default":true,"description":"Play the Channel tuning table/scale and per-note fine cents through MPE: each note on its own member channel 2..16 with a pitch bend of its cents; the zone (channels 2..16, bend range 48 semitones) is announced on every playback start. The plugin must support MPE. Saved with the Channel (project v56)."}},"required":["channel"]})");
    if (id == "channel.scale")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"channel":{"type":"integer","minimum":1},"scale":{"type":"array","maxItems":128,"items":{"type":"number","exclusiveMinimum":0,"maximum":4800},"description":"Degree cents after the implicit 0, rising; the last is the period (1200 for an octave). Empty clears. Consecutive keys play consecutive degrees (e.g. 24 values of 50..1200 = 24-EDO)."},"pitch":{"type":"integer","minimum":0,"maximum":127,"description":"Root key that plays at its normal pitch (default 60)."}},"required":["channel","scale"]})");
    if (id == "channel.tuning")
    {
        properties.getDynamicObject()->setProperty(
            "tuning",
            juce::JSON::parse(R"({"type":"array","minItems":12,"maxItems":12,"items":{"type":"number","minimum":-100,"maximum":100},"description":"Cents added to every note of each pitch class C, C#, ... B (all zero = equal temperament). Played by the local sampler, and by plugin instruments with channel.mpe enabled; other plugin instruments ignore it."})"));
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"channel", "tuning"});
    }
    if (id == "tempo.fromAutomation")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"target":{"type":"integer","minimum":1,"description":"Automation clip placement ID whose drawn curve becomes the tempo."},"offset":{"type":"number","minimum":10,"maximum":999,"description":"BPM at the bottom of the clip (value 0)."},"value":{"type":"number","minimum":10,"maximum":999,"description":"BPM at the top of the clip (value 1)."}},"required":["target","offset","value"]})");
    if (id == "tempo.curve")
        return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{"beat":{"type":"number","minimum":0,"description":"Song beat where the curve starts from the tempo playing there."},"length":{"type":"number","minimum":0.25,"description":"Curve length in beats."},"value":{"type":"number","minimum":10,"maximum":999,"description":"Tempo in BPM reached at the end and kept after it."},"text":{"type":"string","enum":["linear","ease","in","out"],"default":"linear","description":"Shape: linear, ease (slow-fast-slow), in (starts slowly) or out (ends slowly). Drawn as 16 linear ramps; tempo changes inside the span are replaced (one Undo)."}},"required":["beat","length","value"]})");
    if (id.startsWith("tempo.change."))
    {
        auto describe = [&](const char *field, const char *text)
        { properties[field].getDynamicObject()->setProperty("description", text); };
        describe("target", "Stable tempo change ID from vibera_inspect entity=tempo (set/delete).");
        describe("beat", "Song beat where the change applies, > 0 (the base project tempo covers beat 0).");
        describe("value", "Tempo in BPM, 10..999.");
        describe("step", "true: ramp linearly from the previous tempo into this one; false: jump at the beat.");
    }
    if (id.startsWith("marker."))
    {
        auto describe = [&](const char *field, const char *text)
        { properties[field].getDynamicObject()->setProperty("description", text); };
        describe("target", "Stable marker ID from vibera_inspect entity=markers (rename/move/delete).");
        describe("beat", "Song position in quarter-note beats (add/move).");
        describe("text", "Marker name, at most 128 characters (add: empty gives 'Marker N').");
    }
    if (id == "audio.fade")
    {
        for (auto name : {"fadeIn", "fadeOut"})
            properties.getDynamicObject()->setProperty(
                name, juce::JSON::parse(R"({"type":"number","minimum":0,"maximum":60,"description":"Seconds; omitted keeps the clip's current fade. fadeIn + fadeOut may not exceed the clip."})"));
    }
    if (id == "audio.comp")
    {
        properties.getDynamicObject()->setProperty(
            "target",
            juce::JSON::parse(
                R"({"type":"integer","minimum":1,"description":"Audio take clip to play over its own span (or over range). Other audio takes in its clip group are cut at the span edges and muted inside it; outside is unchanged."})"));
        properties.getDynamicObject()->setProperty(
            "range",
            juce::JSON::parse(
                R"({"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","minimum":0},"description":"Swipe comp: [fromBeat, toBeat]. The chosen take is cut at these edges and only that part plays; must overlap the take."})"));
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"target"});
    }
    if (id == "notes.import")
    {
        auto payload = properties["recordedNotes"];
        payload.getDynamicObject()->setProperty(
            "description", "Imported score payload. Runtime import allocates fresh note IDs and targets the "
                           "requested Channel; saved macros preserve every supplied source field.");
        auto fields = payload["items"]["properties"];
        fields.getDynamicObject()->setProperty("slide",
                                               juce::JSON::parse(R"({"type":"boolean","default":false})"));
        fields.getDynamicObject()->setProperty("portamento",
                                               juce::JSON::parse(R"({"type":"boolean","default":false})"));
        fields.getDynamicObject()->setProperty(
            "colourGroup", juce::JSON::parse(R"({"type":"integer","minimum":0,"maximum":15,"default":0})"));
        fields.getDynamicObject()->setProperty(
            "id",
            juce::JSON::parse(
                R"({"type":"integer","minimum":0,"maximum":1000000000,"default":0,"description":"Source metadata; zero is a fresh placeholder. Import always allocates a new runtime note ID."})"));
        fields.getDynamicObject()->setProperty(
            "muted",
            juce::JSON::parse(
                R"({"type":"boolean","default":false,"description":"Preserve imported note activation without discarding its other fields."})"));
    }
    if (isStepRetentionCommand(id))
        properties.getDynamicObject()->setProperty(
            "retainSteps",
            juce::JSON::parse(
                R"({"type":"boolean","description":"Omit to follow Workflow muteRemovedSteps (initially false). True retains disabled step chords with source IDs/properties and reactivates remembered notes; false deletes disabled chords. Step activation targets the raw beat, never modulo a playback cycle, including after automatic loop shrinkage. Existing notes outside the edit window remain exact. Read canonical notes.muted; one Undo/gesture history, model completion is not DSP adoption."})"));
    if (id == "steps.set")
        properties["beat"].getDynamicObject()->setProperty(
            "description",
            "Absolute zero-based source position in quarter-note beats, not modulo loop length. "
            "Playback repetitions are visualization; fixed playback extent remains explicit. "
            "Retained mode requires an in-window sixteenth grid. Legacy nonlooped off-grid/future admission "
            "remains.");
    if (id == "mixer.unmuteAll")
        return juce::JSON::parse(
            R"({"type":"object","additionalProperties":false,"properties":{},"required":[],"description":"Unmute every existing Mixer track including Master and end solo restoration, even without an active solo. No arguments or selected target required. One Undo restores the exact prior mute mask and solo session. Completion reports canonical state, not DSP ramp settling; no device required."})");
    if (id == "mixer.meterReset")
        return juce::JSON::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"insert":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Existing Mixer index;0 is Master. Requires its prepared meter. Use vibera_mixer action=resetOverload; transient/no Undo or project dirty change. Clears completed blocks only; new clipping can re-latch."}},"required":["insert"]})");
    if (id == "mixer.rename" || id == "mixer.colour" || id == "mixer.solo" || id == "mixer.soloConnected" ||
        id == "mixer.unsolo" || id == "mixer.gain" || id == "mixer.pan" || id == "mixer.mute")
    {
        auto *fields = new juce::DynamicObject;
        fields->setProperty(
            "mixer",
            juce::JSON::parse(
                R"({"type":"integer","minimum":0,"maximum":2147483647,"default":0,"description":"Existing stable Mixer index;0 is Master. Master cannot be soloed."})"));
        juce::Array<juce::var> required;
        if (id == "mixer.rename")
        {
            fields->setProperty(
                "text",
                juce::JSON::parse(
                    R"({"type":"string","minLength":1,"maxLength":64,"description":"Trimmed insert name; no tabs or line breaks. Duplicate names allowed; target by stable index."})"));
            required.add("text");
        }
        else if (id == "mixer.colour" || id == "mixer.pan" || id == "mixer.gain")
        {
            fields->setProperty(
                "value",
                juce::JSON::parse(
                    id == "mixer.colour"
                        ? R"({"type":"integer","minimum":0,"maximum":4294967295,"description":"Opaque ARGB integer0xff000000..0xffffffff, or0 for theme. Partial alpha rejected."})"
                    : id == "mixer.pan"
                        ? R"({"type":"number","minimum":-1,"maximum":1,"description":"Existing engine stereo balance: -1 left,0 centre,+1 right. Preserves existing project pan law; not FL cross-channel summing."})"
                        : R"({"type":"number","minimum":0,"maximum":2,"description":"Linear post-FX gain;1=unity,0=silence,2=+6.02dB. Automation may override playback; read display gain/meters."})"));
            required.add("value");
        }
        else if (id != "mixer.unsolo")
            fields->setProperty(
                "enabled",
                juce::JSON::parse(
                    R"({"type":"boolean","default":true,"description":"Mute true silences this insert and ends solo restoration. Solo true retains target/output paths; connected solo additionally retains upstream contributors. False exits only this target's solo."})"));
        if (id == "mixer.solo" || id == "mixer.soloConnected" || id == "mixer.unsolo")
            fields->setProperty(
                "restore",
                juce::JSON::parse(
                    R"({"type":"boolean","description":"Solo exit only: omitted follows Workflow restore preference (default true); false unmutes all. Original baseline survives solo switching."})"));
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        schema.getDynamicObject()->setProperty("required", required);
        return schema;
    }
    if (id == "mixer.route" || id == "mixer.routeOnly" || id == "mixer.send")
    {
        auto *fields = new juce::DynamicObject;
        fields->setProperty(
            "mixer",
            juce::JSON::parse(
                R"({"type":"integer","minimum":1,"maximum":2147483647,"description":"Existing source Mixer insert index; Master0 cannot send."})"));
        fields->setProperty(
            "target",
            juce::JSON::parse(
                R"({"type":"integer","minimum":0,"maximum":2147483647,"description":"Existing destination Mixer index;0 is Master. Self/feedback routes are rejected."})"));
        juce::Array<juce::var> required;
        required.add("mixer");
        required.add("target");
        if (id == "mixer.send")
        {
            fields->setProperty(
                "value",
                juce::JSON::parse(
                    R"({"type":"number","minimum":0,"maximum":2,"description":"Linear send gain;0=silent,1=unity,2=200%. Route must already exist; zero gain retains its topology."})"));
            required.add("value");
        }
        if (id == "mixer.route")
            fields->setProperty(
                "enabled",
                juce::JSON::parse(
                    R"({"type":"boolean","default":true,"description":"Enable while retaining other sends; existing level survives redundant enable. False removes this route."})"));
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        schema.getDynamicObject()->setProperty("required", required);
        return schema;
    }
    if (id != "channel.solo" && id != "channel.unsolo")
        properties.getDynamicObject()->removeProperty("restore");
    if (id == "channel.solo")
        properties.getDynamicObject()->setProperty(
            "soloScope",
            juce::JSON::parse(
                R"({"type":"string","enum":["global","group"],"default":"global","description":"Global solos one unlocked Channel across the Rack. Group solos the target only within its actual filter group, including Unsorted0; other groups and locked Channels stay unchanged. Target must be unlocked. Switching scopes restores the previous affected members to the first baseline. Exit restore applies only to affected unlocked members. Never changes live MIDI recipient."})"));
    auto change = [&](const char *field, const char *key, juce::var value)
    { properties[field].getDynamicObject()->setProperty(key, std::move(value)); };
    if (!id.startsWith("clip.") || id == "clip.add" || id == "clip.unique" || id == "clip.ungroup")
        properties.getDynamicObject()->removeProperty("grouping");
    if (id.startsWith("clip."))
        change("ids", "maxItems", 100000);
    if (id == "clip.move")
    {
        change("length", "default", 0);
        change("length", "minimum", -1000000);
        change("length", "maximum", 1000000);
        change("length", "description",
               "Signed duration delta in quarter-note beats, default0. Common clamp across effective "
               "selection preserves relative lengths; bounded by audio/automation source windows.");
        change("beat", "minimum", -1000000);
        change("beat", "maximum", 1000000);
        change("lane", "minimum", -127);
        change("lane", "description",
               "Signed lane delta. Common boundary clamp preserves relative lanes and beat spacing across "
               "the effective selection.");
    }
    if (id == "clip.group" || id == "clip.ungroup")
    {
        auto fields = new juce::DynamicObject();
        fields->setProperty("ids", properties["ids"]);
        if (id == "clip.group")
            fields->setProperty("grouping", properties["grouping"]);
        change("ids", "minItems", id == "clip.group" ? 2 : 1);
        change("ids", "description",
               id == "clip.group"
                   ? "At least two distinct existing placement IDs across Pattern/Audio/Automation Clips. "
                     "Enabled grouping unions intersected groups; bypass regroups only explicit members. "
                     "Sources stay linked. One exact Undo; read clipGroups/effectiveSelectedClips and "
                     "paginated vibera_inspect."
                   : "Existing member IDs; dissolves every intersected group regardless of grouping "
                     "behavior. One exact Undo. Does not remove any Clip or source.");
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"ids"});
    }
    if (id == "notes.stretch")
    {
        auto *fields = new juce::DynamicObject;
        for (const auto *name : {"pattern", "channel", "ids", "beat", "value"})
            fields->setProperty(name, properties[name]);
        fields->getProperty("ids").getDynamicObject()->setProperty("minItems", 1);
        fields->getProperty("ids").getDynamicObject()->setProperty("maxItems", 100000);
        fields->getProperty("beat").getDynamicObject()->setProperty(
            "description",
            "Fixed anchor in quarter-note source Pattern beats; cannot follow a targeted onset.");
        fields->getProperty("value").getDynamicObject()->setProperty(
            "description", "Absolute time/duration scale relative to the captured source, not a repeated "
                           "delta. Source-dependent minimum and maximum are validated atomically.");
        fields->getProperty("value").getDynamicObject()->setProperty("minimum", 0.000001);
        fields->getProperty("value").getDynamicObject()->setProperty("maximum", 1000000);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"ids", "beat", "value"});
    }
    if (id == "notes.slice")
    {
        auto *fields = new juce::DynamicObject();
        for (const auto *name : {"pattern", "channel", "ids"})
            fields->setProperty(name, properties[name]);
        fields->setProperty("slice", sliceLineSchema());
        fields->getProperty("ids").getDynamicObject()->setProperty("maxItems", 100000);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"slice"});
    }
    if (id == "notes.mute" || id == "notes.unmute")
    {
        auto fields = new juce::DynamicObject();
        for (const auto *field : {"pattern", "channel", "ids"})
            fields->setProperty(field, properties[field]);
        change("ids", "maxItems", 100000);
        change("ids", "items", juce::JSON::parse(R"({"type":"integer","minimum":1,"maximum":1000000000})"));
        change(
            "ids", "description",
            "Existing distinct Note IDs in the target Pattern, including explicit ghost-Channel selections. "
            "Omit/empty targets the current Channel part. Musical properties/IDs remain intact. One Undo; "
            "read notes.muted, and pendingAudioUpdate separately from document completion.");
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
    }
    if (id == "pattern.length")
    {
        change("length", "exclusiveMinimum", 0);
        change("length", "maximum", 1000000);
        change("length", "description",
               "Sets Manual length in positive quarter-note beats. The legacy playback cycle floors at0.25 "
               "beats; source data is "
               "retained outside a shortened Pattern. Shared history; active recorded Pattern windows require Stop before a length change.");
    }
    if (id == "pattern.channelLoop" || id == "pattern.burnChannelLoop")
    {
        auto *fields = new juce::DynamicObject();
        for (const auto *name : {"pattern", "channel"})
        {
            fields->setProperty(name, properties[name]);
            fields->getProperty(name).getDynamicObject()->setProperty("minimum", 1);
        }
        juce::Array<juce::var> required{"pattern", "channel"};
        if (id == "pattern.channelLoop")
        {
            fields->setProperty(
                "text",
                juce::JSON::parse(
                    R"({"type":"string","enum":["off","fixed","step","beat","bar"],"description":"Required per-Pattern+Channel loop mode. Automatic modes round the last active note end up to a sixteenth-note Step, metrical Beat4/denominator, or document Bar. Empty parts use one unit. Source notes outside the loop window remain dormant. This does not enable transport looping."})"));
            fields->setProperty(
                "length",
                juce::JSON::parse(
                    R"({"type":"number","minimum":0.25,"maximum":1000000,"multipleOf":0.25,"default":0.25,"description":"Fixed mode only: quarter-note beats in sixteenth-note multiples. Automatic/off modes ignore this field. Expansion limited to100000 occurrences per looped Pattern. Crossing notes keep their duration; final Pattern/clip window clips playback."})"));
            required.add("text");
        }
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        schema.getDynamicObject()->setProperty("required", required);
        schema.getDynamicObject()->setProperty(
            "description",
            id == "pattern.channelLoop"
                ? "One canonical Undo. Read state.patterns[].channelLoops mode/resolvedLength and inspect "
                  "pendingAudioUpdate separately. Prepared updates retain graph/device and unchanged voices."
                : "One canonical Undo: replace this part inside the Pattern window with raw repeated score, "
                  "disable only its loop. Repetitions get fresh Note IDs; original first-cycle IDs and notes "
                  "beyond Pattern end remain. Dormant notes inside the window are replaced, recoverable with "
                  "Undo. Other parts/placements/native state stay intact. Allowed during playback (a repeat "
                  "sounding at that instant may retrigger once); refused while recording is armed/active.");
    }
    if (id == "channel.sampler.cut" || id == "channel.sampler.cutBy" || id == "channel.sampler.maxPoly")
    {
        change("value", "type", "integer");
        change("value", "minimum", 0);
        change("value", "maximum", 99);
        change("value", "default", 0);
        change("value", "description",
               id == "channel.sampler.maxPoly"
                   ? "Maximum sample/original-instrument voices across score/MIDI/typing/preview.0 means no "
                     "user limit (128-voice runtime ceiling);1..99 steals oldest immediately. Not "
                     "Mono/portamento. VST3 and Audio Clips are refused."
                   : "Sample/original-instrument Cut sender or Cut-by receiver group.0=None,1..99 matches by "
                     "number. Cross-Channel score/live notes release matching voices through captured "
                     "envelopes. Same-Channel self-cut originates only from step notes. Future triggers use "
                     "changes; VST3 and Audio Clips are refused.");
    }
    if (id == "channel.sampler.normalize")
    {
        auto fields = new juce::DynamicObject();
        fields->setProperty("channel", properties["channel"]);
        fields->setProperty("enabled", properties["enabled"]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        fields->getProperty("channel").getDynamicObject()->setProperty("minimum", 1);
        fields->getProperty("enabled").getDynamicObject()->setProperty(
            "description",
            "True normalizes the whole loaded stereo source peak to unity before Channel/Mixer gain. "
            "Default project state false; command default true. Shared gain preserves stereo balance; "
            "silence remains silence. Source/trim/velocity/envelopes remain unchanged. Sounding sample "
            "voices approach the new gain over about5ms. VST3 and Audio Clips are refused. "
            "Read Channel sampler.normalize and prepared sourcePeak/normalizationGain, then "
            "pendingAudioUpdate.");
    }
    if (id == "channel.sampler.cutItself")
        change("enabled", "description",
               "True assigns the same unused Cut/Cut-by group1..99, retaining an already unique pair. False "
               "clears both. One Undo; no available group fails atomically. Sample/original-instrument "
               "Channels only; self-cut triggers from steps, not Piano notes.");
    if (id == "project.masterPitch")
    {
        change("value", "minimum", -1200);
        change("value", "maximum", 1200);
        change("value", "description",
               "Cents added to every tunable instrument (local sampler, MPE plugins, MTS-ESP); 0 = A440. "
               "One Undo step; read back as project masterPitch.");
    }
    if (id == "project.swing" || id == "channel.swingMix" || id == "rack.swingMix")
    {
        change("value", "minimum", 0);
        change("value", "maximum", 1);
        change("value", "description",
               "Normalized 0..1 (UI percent / 100). Global default 0, Channel Mix default 1. Maximum "
               "odd-sixteenth delay is 0.125 quarter-note beats times both multipliers. Non-destructive "
               "score timing; not Audio Clip stretching or live MIDI input delay.");
    }
    if (id == "rack.swingMix" || id == "rack.truncateSwing")
    {
        change("ids", "maxItems", 128);
        change("ids", "description",
               "Unique stable instrument Channel IDs; omit/empty uses shared Rack selection, including "
               "selected hidden Channels. Audio Clips refused atomically. One Undo for the whole selection.");
    }
    if (id == "channel.truncateSwing" || id == "rack.truncateSwing")
        change("enabled", "description",
               "Default false in new/legacy projects. True preserves the original note end when swing delays "
               "its onset, with minimum duration 0.000001 quarter-note beats. Playback is always cropped by "
               "the Pattern/Playlist source window. Source notes are never rewritten.");
    if (id == "steps.release")
    {
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change(
            "text", "description",
            "Set, reset0, proportional baseline-anchor scale (zero anchor uses additive lift), or "
            "linear ramp. MIDI note-off data for score instruments, not ADSR duration. Audio Clips refuse.");
        for (const auto *field : {"value", "offset"})
        {
            change(field, "minimum", 0);
            change(field, "maximum", 1);
            change(field, "default", 0);
        }
        change(
            "value", "description",
            "Normalized release velocity0..1, rounded to MIDI byte0..127 at preparation/export. "
            "Default0 preserves legacy note-offs. Local sampler/current native Synth ignore its audio "
            "response; other instruments/patches may respond. Completion is source state, not delivery ACK.");
        change("offset", "description", "Ramp ending normalized release velocity0..1.");
        change("target", "description", "Scale requires one targeted baseline source-note anchor.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Distinct raw on-grid in-window step-note IDs, including chords/muted/zero members. "
               "Empty selects every eligible source step. Generated Repeat hits are not separate targets.");
        auto *fields = new juce::DynamicObject;
        for (const auto *field :
             {"pattern", "channel", "target", "ids", "text", "value", "offset", "beat", "length"})
            fields->setProperty(field, properties[field]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        return schema;
    }
    if (id == "steps.repeat")
    {
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change("text", "description",
               "Set, reset0/Off, baseline-anchor index translation/clamp, or nearest-index linear ramp. "
               "All score instruments; Audio Clips refused. Source IDs/grid/duration stay fixed.");
        for (const auto *field : {"value", "offset"})
        {
            change(field, "type", "integer");
            change(field, "minimum", 0);
            change(field, "maximum", 14);
            change(field, "default", 0);
        }
        change("value", "description",
               "Rhythmic rate index0..14, not a trigger count.0 Off;1 quarter;2 dotted eighth;3 quarter "
               "triplet;4 eighth;5 dotted sixteenth;6 eighth triplet;7 sixteenth;8 dotted32nd;9 sixteenth "
               "triplet;10 32nd;11 dotted64th;12 32nd triplet;13 64th;14 64th triplet. Retriggers fill "
               "realized source duration from once-shifted/swung origin; each onset/end is window-cropped.");
        change("offset", "description", "Ramp ending rhythmic rate index0..14.");
        change("target", "description", "Scale requires one targeted baseline source-note anchor.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Distinct raw on-grid in-window step-note IDs. Empty selects every eligible source step, "
               "including chords and muted/zero members. Generated hit ordinals are not source targets.");
        auto *fields = new juce::DynamicObject;
        for (const auto *field :
             {"pattern", "channel", "target", "ids", "text", "value", "offset", "beat", "length"})
            fields->setProperty(field, properties[field]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        return schema;
    }
    if (id == "steps.shift")
    {
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change("text", "description",
               "Set, reset0, proportional baseline-anchor scale (zero anchor uses additive lift), or "
               "linear ramp. All non-Audio-Clip instrument Channels. Logical cells, duration, mute and "
               "source identity remain unchanged; playback still crops at Pattern/placement windows.");
        for (const auto *field : {"value", "offset"})
        {
            change(field, "minimum", 0);
            change(field, "maximum", 100);
            change(field, "default", 0);
        }
        change("value", "description",
               "Displayed source-note Shift percent0..100. Delay is percent*0.002475 quarter-note beats; "
               "100 delays0.2475 beats. Applied after raw-position swing, once during preparation. "
               "Set/scale anchor or ramp start; model completion is not audio adoption.");
        change("offset", "description", "Ramp endpoint in displayed Shift percent0..100.");
        change("target", "description", "Scale requires one targeted baseline source-note anchor.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Distinct raw on-grid in-window step-note IDs in the Pattern/Channel. Empty selects all "
               "eligible source steps, including chords and muted/zero members. Repeats address sources.");
        auto *fields = new juce::DynamicObject;
        for (const auto *field :
             {"pattern", "channel", "target", "ids", "text", "value", "offset", "beat", "length"})
            fields->setProperty(field, properties[field]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        return schema;
    }
    if (id == "steps.modX" || id == "steps.modY")
    {
        const auto *info =
            stepExpressionInfo(id == "steps.modX" ? StepExpression::modX : StepExpression::modY);
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change("text", "description",
               "Local sampler only: set; reset neutral0.5; additive baseline anchor scale; linear ramp. "
               "Native Synth/VST3 and Audio Clip targets refuse atomically. Muted/zero/chord source-note IDs "
               "and unrelated fields remain unchanged.");
        for (const auto *field : {"value", "offset"})
        {
            change(field, "minimum", info->minimum);
            change(field, "maximum", info->maximum);
            change(field, "default", info->defaultValue);
        }
        change("value", "description",
               id == "steps.modX"
                   ? "Normalized per-note sampler cutoff Mod X0..1; neutral0.5. Requested set/scale anchor "
                     "or ramp start. Original composition clamps global cutoff + 2*(noteModX-0.5) to0..1. "
                     "Filter Off is a no-op; model completion is not a voice/DSP adoption ACK."
                   : "Normalized per-note sampler resonance Mod Y0..1; neutral0.5. Requested set/scale "
                     "anchor or ramp start. Original composition clamps global resonance + 2*(noteModY-0.5) "
                     "to0..1. Filter Off is a no-op; model completion is not a voice/DSP adoption ACK.");
        change("offset", "description",
               "Ramp ending normalized Mod X/Y0..1. Quarter-cell source endpoints must be in-window; "
               "reverse/zero spans supported. Targeted notes outside span restore their gesture baseline.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Distinct raw aligned in-window step-note IDs on the explicit Pattern/Channel; empty selects "
               "all eligible muted/zero/chord members. No generated occurrence or loop-period remapping.");
        change("target", "description",
               "Scale requires one targeted baseline source-note anchor. Add requested anchor minus "
               "baseline anchor to each baseline value, then clamp0..1 (not proportional scaling).");
        auto *fields = new juce::DynamicObject;
        for (const auto *field :
             {"pattern", "channel", "target", "ids", "text", "value", "offset", "beat", "length"})
            fields->setProperty(field, properties[field]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        return schema;
    }
    if (id == "steps.pan" || id == "steps.fine")
    {
        const auto *info = stepExpressionInfo(id == "steps.pan" ? StepExpression::pan : StepExpression::fine);
        const double limit = info->maximum;
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change("text", "description",
               "Local sampler only: set; reset0; additive baseline anchor scale; linear ramp. Native "
               "Synth/VST3 and Audio Clip targets refuse atomically. Muted/zero-velocity/chord source notes "
               "retain identity.");
        for (const auto *field : {"value", "offset"})
        {
            change(field, "minimum", -limit);
            change(field, "maximum", limit);
            change(field, "default", 0);
        }
        change("value", "description",
               id == "steps.pan" ? "Normalized source-note pan -1..1, center0. Requested set/scale anchor or "
                                   "ramp start; model commit is not audio adoption."
                                 : "Source-note Fine Pitch +/-100 cents, neutral0. Requested set/scale "
                                   "anchor or ramp start; independent of Channel-wide tuning.");
        change("offset", "description",
               "Ramp ending expression; finite and within the same property range. Targeted notes outside "
               "span restore baseline. Both endpoints quarter-step aligned/in-window; reverse and zero spans "
               "supported.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Distinct current raw step-note IDs in this Pattern/Channel; empty selects all aligned "
               "in-window source steps, including muted/chord/zero-velocity members. Never remap source "
               "cells by playback loop period.");
        change("target", "description",
               "Scale requires one targeted source-note anchor; adds requested anchor minus baseline anchor "
               "to each baseline value, clamping at range boundaries.");
        auto *fields = new juce::DynamicObject;
        for (const auto *field :
             {"pattern", "channel", "target", "ids", "text", "value", "offset", "beat", "length"})
            fields->setProperty(field, properties[field]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        return schema;
    }
    if (id == "steps.pitch")
    {
        change("pitch", "minimum", 0);
        change("pitch", "maximum", 127);
        change("pitch", "default", 60);
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change("text", "description",
               "Pitch mode for aligned in-window step notes. set assigns pitch; reset uses60; scale "
               "transposes the gesture baseline by requested anchor interval; ramp rounds linear semitone "
               "positions. Other note fields and step identity remain exact.");
        change("pitch", "description",
               "Integral MIDI note0-127, default60. Requested pitch for set/scale; starting pitch for ramp. "
               "Scale needs target anchor and clamps each transposed note0-127.");
        change("offset", "type", "integer");
        change("offset", "minimum", 0);
        change("offset", "maximum", 127);
        change("offset", "default", 0);
        change("offset", "description",
               "Ramp ending integral MIDI pitch. Both endpoints must be aligned in-window cells; reverse and "
               "zero-span supported. Explicitly supply this for ramp.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Optional unique existing step-note IDs in the explicit Pattern/Channel. Empty targets all "
               "eligible notes, including chords and offscreen notes. No new notes, melodic conversion or "
               "step deletion.");
        change("target", "description",
               "Scale anchor step-note ID, required within targets; transposition uses its gesture-start "
               "pitch.");
        change("beat", "description",
               "Ramp starting quarter-note beat, aligned to0.25 and inside the Pattern window.");
        change("length", "description",
               "Ramp signed span in quarter-note beats, aligned to0.25; endpoint must be in-window. Targeted "
               "notes outside the ramp restore baseline pitch.");
        auto *fields = new juce::DynamicObject;
        for (const auto *name : {"pattern", "channel", "target", "ids", "text", "pitch", "beat", "length", "offset"})
            fields->setProperty(name, properties[name]);
        schema.getDynamicObject()->setProperty("properties", juce::var(fields));
        return schema; // Do not advertise ignored Mixer/plugin/recording arguments.
    }
    if (id == "steps.velocity")
    {
        change("text", "enum", juce::Array<juce::var>{"set", "reset", "scale", "ramp"});
        change("text", "default", "set");
        change("text", "description",
               "Velocity mode. Only existing aligned in-loop step events change; no new or removed steps. "
               "Repeated scale/ramp updates use the canonical gesture-start baseline. Outside a gesture "
               "baseline is current project.");
        change("ids", "maxItems", 100000);
        change("ids", "description",
               "Optional unique stable step-note IDs in this Pattern/Channel; omitted/empty targets all "
               "eligible step notes, including offscreen ones. Other Channels, melodic and out-of-loop notes "
               "are excluded. Invalid explicit targets fail atomically.");
        change("value", "minimum", 0);
        change("value", "maximum", 1);
        change("value", "description",
               "Normalized velocity 0-1. set assigns it; scale sets anchor velocity and scales the baseline "
               "by its ratio (zero anchor uses additive lift); ramp is starting velocity. Reset uses0.8 "
               "regardless of value.");
        change("target", "description",
               "For scale: required stable step-note anchor ID belonging to target set.");
        change("beat", "description",
               "For ramp: starting sixteenth-note aligned quarter-note beat, inside explicit Pattern loop.");
        change("length", "description",
               "For ramp: signed quarter-note beat span to the end column, aligned to0.25. Zero edits one "
               "column. Reverse ramps supported. Both endpoints must be in-loop.");
        change("offset", "minimum", 0);
        change("offset", "maximum", 1);
        change("offset", "description",
               "For ramp: ending normalized velocity. Targeted notes outside the ramp restore "
               "gesture-baseline values; untargeted notes unchanged.");
    }
    if (id == "channel.solo")
        change("enabled", "description",
               "True sets the explicit solo target; false leaves that target's solo. UI Ctrl-click toggles "
               "using readback. Score muting leaves live MIDI playable.");
    if (id == "channel.mute" || id == "rack.muteSelected" || id == "rack.unmuteSelected")
    {
        change("ids", "maxItems", 128);
        change("ids", "description",
               "Stable Channels for atomic score mute; channel.mute otherwise uses channel. Selected aliases "
               "default to shared Rack selection. No active MIDI redirection.");
    }
    if (id == "channel.moveUp" || id == "channel.moveDown")
    {
        change("ids", "maxItems", 128);
        change("ids", "description",
               "Stable Channel IDs; omitted uses active Channel. Move selected blocks one global slot, "
               "preserving relative order, score, routing and active MIDI. One Undo; edge no-op does not add "
               "history.");
    }
    if (id == "notes.move")
    {
        change("pitch", "default", 0);
        change("length", "default", 0);
    }
    else
        change("pitch", "minimum", 0);
    if (id == "pattern.move")
        change("lane", "maximum", 1023);
    if (id == "pattern.group")
        change("text", "maxLength", 128);
    if (id.startsWith("steps.fill"))
    {
        change("interval", "description",
               "Fill interval in sixteenth-note steps; generic default4. Named fill2/4/8 commands fix their "
               "interval. Applies from beat0 over the explicit Pattern length, not the visible page.");
        change("ids", "maxItems", 128);
        change("ids", "description",
               "Optional stable playable Channel IDs for atomic bulk fill; otherwise channel defaults to "
               "active. Legacy mode converts to floor-aligned quarter-beat steps and removes off-mask notes. "
               "retainSteps=true requires existing on-grid steps, keeps their durations/chord properties, "
               "mutes off-mask and unmutes on-mask. Outside-window notes stay exact; one Undo.");
    }
    if (id.startsWith("rack.group."))
    {
        change("text", "maxLength", 128);
        change("text", "description",
               "Channel group name, 1..128 characters without outer spaces; All/Unsorted reserved; "
               "case-insensitive unique. assign may reuse an existing name or create atomically.");
        change("target", "description",
               "Stable Channel group ID. assign uses zero and empty text to unassign, or zero plus text to "
               "create/reuse by name. rename/delete require a current group ID.");
        change("ids", "maxItems", 128);
        change("ids", "description",
               "Stable Channel IDs for assign, not Pattern/note/row IDs; group metadata never changes "
               "routing or playback ownership.");
        if (id == "rack.group.assign")
        {
            change("ids", "minItems", 1);
            schema.getDynamicObject()->setProperty("required", juce::Array<juce::var>{"ids"});
        }
    }
    if (id == "channel.add" || id == "plugin.instrument.create" || id == "audio.add")
        change("target", "description",
               "For new Channels: optional existing Channel group ID. Zero inherits the current named Rack "
               "filter; use All/Unsorted view to create unassigned. Existing source replacement keeps "
               "membership.");
    if (id == "notes.quantize" || id == "notes.quickQuantize" || id == "notes.quantizeStarts")
    {
        auto *properties = schema["properties"].getDynamicObject();
        for (auto name : {"strength", "sensitivity", "durationStrength"})
        {
            auto value = juce::JSON::parse(R"({"type":"number","minimum":0,"maximum":1,"default":1})");
            value.getDynamicObject()->setProperty(
                "description", juce::String(name) + ": normalized 0..1. Start blend, half-grid attraction "
                                                    "radius, or duration/end blend respectively.");
            properties->setProperty(name, value);
        }
        change("length", "minimum", .03125);
        change("length", "maximum", 64);
        change("ids", "maxItems", 100000);
        change("length", "description",
               "Grid in quarter-note beats. Default0.25; positive range0.03125..64.");
        change("text", "enum", juce::Array<juce::var>{"", "duration", "end", "leaveDuration", "leaveEnd"});
        change("text", "description",
               "Mode: duration, end, leaveDuration, leaveEnd. Empty retains legacy onset-only leaveDuration. "
               "Quick actions force duration/leaveDuration respectively. Grid-based; no groove-template "
               "support yet.");
        change("ids", "description",
               "Stable note IDs including editable ghosts. Empty targets current Channel part only. Preview "
               "with vibera_gesture document domain: repeated updates use its original baseline; reset by "
               "default parameters, commit one Undo, cancel/lease expiry restores exact source. Read actual "
               "notes via vibera_inspect. No recording take allowed.");
    }
    if (id == "notes.quantize")
    {
        auto *properties = schema["properties"].getDynamicObject();
        properties->setProperty("groove", grooveArgumentSchema());
        properties->setProperty(
            "velocityStrength",
            juce::JSON::parse(
                R"({"type":"number","minimum":0,"maximum":1,"default":0,"description":"Blend eligible note velocity toward groove level. Unsupported note properties are not represented."})"));
        properties->setProperty(
            "zeroVelocityDeletes",
            juce::JSON::parse(
                R"({"type":"boolean","default":true,"description":"Eligible zero-velocity template points remove source notes as rests; preview cancel/parameter changes restore baseline notes."})"));
    }
    if (id == "notes.chop")
    {
        change("length", "minimum", .03125);
        change("length", "maximum", 64);
        change("length", "description",
               "Slice interval in quarter-note beats, relative to each note onset. Default 0.25. Preserves "
               "exact final remainder; at most 4096 additional notes per operation.");
    }
    if (id == "notes.legato" || id == "notes.chop")
        change("ids", "description",
               "Selected stable note IDs, optionally across editable ghost Channels; processed independently "
               "per Channel. Empty means all notes of the selected/explicit Channel in the Pattern. Legato "
               "connects to the next selected strictly later onset, leaving final onset lengths unchanged. "
               "Chop preserves pitch/velocity, first IDs and exact span. Both are one Undo and synchronous "
               "document completion; read notes with vibera_inspect.");
    if (id == "notes.riff" || id == "notes.randomize") // The generators draw from a seed, like the menu's.
        schema["properties"].getDynamicObject()->setProperty(
            "seed", juce::JSON::parse(R"({"type":"integer","minimum":0,"maximum":2147483647,"default":1,"description":"Seed for the generated part; the same seed replays the same result."})"));
    if (id == "notes.chordify" || id == "notes.arpeggiate" || id == "notes.strum" || id == "notes.humanize" ||
        id == "notes.scaleSnap" || id == "notes.maqam")
    {
        auto *fields = schema["properties"].getDynamicObject();
        auto set = [&](const char *name, const char *json) { fields->setProperty(name, juce::JSON::parse(json)); };
        change("ids", "description",
               "Selected stable note IDs (ghost Channels included); empty means the whole selected/explicit "
               "Channel part in the Pattern. One synchronous Undo step; read notes back with vibera_inspect.");
        set("seed", R"({"type":"integer","minimum":0,"maximum":2147483647,"default":1,"description":"Seed for random choices; the same seed replays the same result."})");
        if (id == "notes.chordify")
        {
            set("text", R"({"type":"string","enum":["major","minor","dim","aug","sus2","sus4","maj7","min7","dom7","dim7","m7b5","6","m6","add9","maj9","min9","9","power"],"description":"Chord built on every selected note as its root."})");
            set("value", R"({"type":"integer","minimum":0,"maximum":4,"default":0,"description":"Inversion: how many of the lowest chord notes move up an octave."})");
        }
        if (id == "notes.arpeggiate")
        {
            set("text", R"({"type":"string","enum":["up","down","updown","random"],"description":"Order of the chord notes."})");
            set("length", R"({"type":"number","minimum":0.015625,"maximum":16,"default":0.25,"description":"Step between arpeggio notes in beats; each chord fills its longest note."})");
            set("value", R"({"type":"number","minimum":0,"maximum":1,"default":1,"description":"Gate: note length as a fraction of the step (0 means 1)."})");
        }
        if (id == "notes.strum")
        {
            set("text", R"({"type":"string","enum":["up","down","alternate"],"description":"up starts from the lowest note, down from the highest; alternate strums successive chords up, down, up..."})");
            set("length", R"({"type":"number","exclusiveMinimum":0,"maximum":1,"default":0.25,"description":"Delay between successive chord notes in beats; note ends stay."})");
            set("value", R"({"type":"number","minimum":0,"maximum":1,"default":0,"description":"Velocity taper toward the last strummed note."})");
        }
        if (id == "notes.humanize")
        {
            set("beat", R"({"type":"number","minimum":0,"maximum":1,"default":0,"description":"Maximum onset shift +/- in beats."})");
            set("value", R"({"type":"number","minimum":0,"maximum":1,"default":0,"description":"Maximum velocity change +/-."})");
            set("offset", R"({"type":"number","minimum":0,"maximum":0.9,"default":0,"description":"Maximum length change +/- as a fraction of each note."})");
        }
        if (id == "notes.maqam")
        {
            set("text", R"({"type":"string","enum":["rast","bayati","saba","sikah","huzam","hijaz","nahawand","kurd","ajam","nawaAthar","jiharkah"],"description":"Maqam in 24-tone equal temperament. Notes move to the nearest degree as MIDI pitch plus fine cents (quarter tones = -50 cents on the note above). The local sampler plays fine cents; MIDI 1 plugin instruments ignore them."})");
            set("pitch", R"({"type":"integer","minimum":0,"maximum":127,"default":60,"description":"Tonic: any MIDI pitch, its pitch class is used. Sikah and Huzam start a half flat below it (E gives E half-flat)."})");
        }
        if (id == "notes.scaleSnap")
        {
            set("text", R"({"type":"string","enum":["major","minor","harmonicMinor","melodicMinor","dorian","phrygian","lydian","mixolydian","locrian","majorPentatonic","minorPentatonic","blues","hijaz","nahawand","kurd","ajam"],"description":"Scale; maqam names are 12-tone approximations without quarter tones."})");
            set("pitch", R"({"type":"integer","minimum":0,"maximum":127,"default":60,"description":"Scale root: any MIDI pitch, its pitch class is used (0 = C, 60 = C)."})");
        }
    }
    if (id == "notes.glue")
        change("ids", "description",
               "Stable note selection, including editable ghosts. Empty means the whole selected/explicit "
               "Channel part in the Pattern. Joins touching or overlapping same-pitch notes per Channel; "
               "real gaps stay separate. Retains the leftmost ID, onset and velocity; removes joined later "
               "IDs. Numerical tolerance is eight double-precision epsilon units scaled by beat magnitude, "
               "not snap. One synchronous document Undo. Read survivors with vibera_inspect and "
               "vibera_selection action=state,target=notes.");
    if (id == "picker.place")
    {
        change("text", "enum", juce::Array<juce::var>{"pattern", "audio", "automation"});
        change("ids", "maxItems", 128);
    }
    if (id == "project.tempo" || id.endsWith(".gain") || id.endsWith(".pan") || id == "notes.velocity" ||
        id == "pattern.transpose")
    {
        const double minimum = id == "project.tempo"       ? 20
                               : id.endsWith(".pan")       ? -1
                               : id == "pattern.transpose" ? -127
                                                           : 0;
        const double maximum = id == "project.tempo"       ? 400
                               : id.endsWith(".gain")      ? 2
                               : id == "pattern.transpose" ? 127
                                                           : 1;
        change("value", "minimum", minimum);
        change("value", "maximum", maximum);
        if (id == "pattern.transpose")
            change("value", "type", "integer");
    }
    return schema;
}
inline juce::var mcpCommandContracts(bool includeSchemas = false, const juce::String &requested = {})
{
    juce::Array<juce::var> operations;
    for (const auto &command : commandRegistry())
    {
        if (requested.isNotEmpty() && command.id != requested)
            continue;
        auto *item = new juce::DynamicObject;
        const juce::String mapping(command.mcp);
        const bool supported = mapping == "command" || mapping == "action";
        if (isSamplerVoiceModeCommand(command.id))
        {
            item->setProperty("macroAvailable", true);
            item->setProperty("asynchronous", false);
            item->setProperty("stateReadback", "vibera_state channels[].sampler and pendingAudioUpdate; "
                                               "samplerWindows.controls; canonical history/revision");
            item->setProperty("prerequisitesDetail",
                              "Existing local sampler Channel. Flags are independent; Slide seconds0..10 "
                              "and Gate quarter-note beats0..64 (0 Off). Shared canonical UI/MCP/history, "
                              "samplerVoices gesture and global selected-Channel controller mapping. "
                              "These five controls are not in the project modulation-CC/Automation Clip "
                              "registry. Admission-captured Gate/FullPorta and transition-captured Slide "
                              "do not reset held notes. Completion is canonical, not a DSP/audio ACK; "
                              "voiceModeAcknowledged is null. Per-note Porta, silent Slide and color-group "
                              "behavior remain separate module work.");
        }
        if (isSamplerControllerCommand(command.id))
        {
            item->setProperty("macroAvailable", false);
            item->setProperty("asynchronous", false);
            item->setProperty("stateReadback",
                              "vibera_state samplerControllers.links/effective/learn/projectionError; "
                              "vibera_controller action=state samplerControllers; document history/revision");
            item->setProperty(
                "prerequisitesDetail",
                "Complete typed project-owned link with fixed Channel and sampler parameter. "
                "Create requires a local sampler. Existing unsupported-role links remain "
                "dormant and reserve their source. Conflicts require explicit exact project "
                "replacement IDs; global preferences are retained. Shared validation and "
                "one document Undo; no-op does not create history. Configuration completion "
                "is canonical document publication, not a DSP or disk acknowledgement. "
                "Use pending samplerLearnStart/State/Accept/Cancel for hardware identification; "
                "ordinary project Save persists it. Typed mapping payloads are unavailable "
                "in the current scalar document-macro format.");
        }
        if (isSamplerAutomationTargetCommand(command.id))
        {
            item->setProperty("macroAvailable", true);
            item->setProperty(
                "stateReadback",
                "vibera_state channels[].sampler.modulation and modulationAutomation; paginated "
                "automationSources/automationPoints/automationLinks/automationInitialValues; "
                "pendingAudioUpdate separately");
            item->setProperty(
                "prerequisitesDetail",
                "Explicit stable Channel and samplerParameter code from samplerAutomationContract. New "
                "assignments require a local sample. Existing native/plugin/AudioClip curves stay dormant "
                "and recoverable. Source points use normalized0..1; initial values and link ranges use "
                "physical parameter units. Song placement policy applies; Stop/Pattern retain manual "
                "fallback. Boolean curves switch at0.5; shape rounds nearest with ties upward. Completion is "
                "canonical document application, not DSP or listening acknowledgement. Shared "
                "validation/history and structured errors; await actual file jobs for save/export.");
            if (includeSchemas)
                item->setProperty("resultSchema", mixerAutomationResultSchema());
        }
        if (isMixerAutomationTargetCommand(command.id))
        {
            item->setProperty("macroAvailable", true);
            item->setProperty("stateReadback", "vibera_inspect mixer automationTargets; paginated automationSources/automationPoints/automationLinks/automationInitialValues; vibera_state pendingAudioUpdate separately");
            if (includeSchemas) item->setProperty("resultSchema", mixerAutomationResultSchema());
            item->setProperty(
                "prerequisitesDetail",
                "Existing stable Mixer ID; omitted automationControl preserves legacy gain. pan/separation "
                "are bipolar -1..1; gain/send linear0..2. Send requires automationSendTarget=existing "
                "destination ID (Master0) and an enabled route from non-Master mixer source. Points remain "
                "normalized0..1. Create captures saved manual target, not audible state. Song lanes hold "
                "their final endpoint; deleting a clip does not delete initialization. Stop/Pattern use "
                "manual ramps. Channel targets and controller touch recording remain unsupported. Read "
                "canonical send pair/manual/initialization/linked sources through Mixer automationTargets; "
                "DSP settling is separate. Shared history/cancel/admission; completion is canonical only, "
                "not DSP/heard acknowledgement.");
        }
        item->setProperty("id", command.id);
        item->setProperty("name", command.name);
        item->setProperty("scope", int(command.scope));
        item->setProperty("documentEdit", command.documentEdit);
        item->setProperty("userFacing", command.userAction);
        if (command.id == "automation.source.clone")
        {
            item->setProperty("asynchronous", false);
            item->setProperty("macroAvailable", true);
            item->setProperty("stateReadback",
                              "createdSource; vibera_inspect "
                              "automationSources/automationPoints/automationLinks/automationInitialValues; "
                              "vibera_state pendingAudioUpdate separately");
            item->setProperty(
                "prerequisitesDetail",
                "Existing source, no conflicting document/take/file/native preparation. Copies curve "
                "independently, leaves placements and initialized values unchanged. UI selects the new "
                "unused source; semantic clients select it explicitly using vibera_picker. Duplicate "
                "placement remains linked; Make Unique clears links. Completion is canonical document "
                "publication, not DSP or listening acknowledgement.");
            if (includeSchemas)
                item->setProperty("resultSchema", juce::JSON::parse(R"JSON({"type":"object",
                  "required":["completionState","completionScope","createdSource","linkPolicy","changed","revision"],
                  "properties":{"completionState":{"const":"completed"},"completionScope":{"const":"canonicalAutomationSource"},
                    "createdSource":{"type":"integer","minimum":1},"linkPolicy":{"enum":["clear","preserve"]},
                    "changed":{"const":true},"revision":{"type":"integer","minimum":0},
                    "dspSettlingAcknowledged":{"const":false}},"additionalProperties":true})JSON"));
        }
        item->setProperty("mapping", supported ? "vibera_command" : mapping);
        if (command.id == "pattern.channelLoop" || command.id == "pattern.burnChannelLoop")
        {
            item->setProperty(
                "stateReadback",
                "vibera_inspect entity=patterns (paginated) channelLoops; vibera_state "
                "patterns[].channelLoops and pendingAudioUpdate; vibera_rack rows loopMode/loopLength");
            item->setProperty("asynchronous", false);
            item->setProperty(
                "prerequisitesDetail",
                command.id == "pattern.channelLoop"
                    ? "Existing Pattern and non-Audio-Clip Channel. Prepared timeline only, no device/graph "
                      "restart. Maximum100000 expanded raw occurrences in looped Pattern. One shared Undo; "
                      "unchanged setting is a no-op."
                    : "Existing enabled Channel loop, stopped transport and no armed/capturing/retained "
                      "take. Replaces dormant in-window notes with repetitions; out-of-horizon and other "
                      "Channel notes remain. One Undo. Live Burn remains unfinished.");
            if (includeSchemas)
                item->setProperty(
                    "resultSchema",
                    juce::JSON::parse(
                        R"({"type":"object","required":["completionState","completionScope","changed","revision","dspSettlingAcknowledged"],"properties":{"completionState":{"const":"completed"},"completionScope":{"const":"canonicalPatternChannelLoop"},"changed":{"type":"boolean"},"revision":{"type":"integer","minimum":0},"dspSettlingAcknowledged":{"const":false}},"additionalProperties":true})"));
        }
        item->setProperty("supported", supported || mapping.startsWith("vibera_"));
        item->setProperty(
            "qualification",
            "Consult coverage inventory and exact workflow receipts; registration is not verification.");
        if (includeSchemas)
            item->setProperty(
                "arguments",
                (command.id == "rack.graph" || command.id == "rack.soloRestore" ||
                 command.id == "channel.clone" || command.id == "channel.delete")
                    ? rackToolDefinition()["inputSchema"]
                : (supported && command.documentEdit) || command.id == "mixer.meterReset"
                    ? commandArgumentSchema(command.id)
                    : juce::JSON::parse(
                          "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"));
        if (command.id == "steps.compose" || command.id == "steps.composeDialog")
        {
            item->setProperty("macroAvailable", command.id == "steps.compose");
            item->setProperty("asynchronous", command.id == "steps.composeDialog");
            item->setProperty("stateReadback", "vibera_stepCompose action=state; vibera_inspect notes and Pattern channelLoops; vibera_state pendingAudioUpdate separately");
            item->setProperty("prerequisitesDetail", "Explicit stable Pattern and distinct instrument Channel recipes. Atomic validation, source-cycle replacement, fixed/off Channel loops and one history transaction; no Audio Clips or implicit melodic conversion. Dialog owns a cancellable document lease, refuses active recording, and remains pending until Accept/Cancel/expiry; opening and unchanged drafts are nonmutating. Exact operation token required for subsequent semantic session actions. Macro recipe Channels stay explicit; no native vendor widgets or DSP adoption acknowledged.");
        }
        if (command.id.startsWith("automation.record."))
        {
            auto *invocation = new juce::DynamicObject;
            invocation->setProperty("tool", "vibera_automationRecording");
            invocation->setProperty("action", command.id.endsWith(".cancel")   ? "cancel"
                                              : command.id.endsWith(".finish") ? "finish"
                                                                               : "arm");
            item->setProperty("invocation", juce::var(invocation));
            item->setProperty("macroAvailable", false);
            item->setProperty("asynchronous", true);
            item->setProperty("stateReadback",
                              "vibera_automationRecording action=state; exact active token. Song MIDI take "
                              "remains pending until explicit finish/cancel; completed is one canonical "
                              "Automation Clip history action, not DSP acknowledgement.");
            item->setProperty(
                "prerequisitesDetail",
                "Song without loop, actual prepared source clock, existing stable insert and current sends; "
                "no linked/initialized automation on captured keys or conflicting edit/file/native "
                "preparation. Armed/held MIDI note capture remains independent. Native parameters, Pattern "
                "events and overdub are not implemented by this recording increment.");
            if (includeSchemas)
            {
                const auto tool = automationRecordingTool();
                item->setProperty("arguments", tool["inputSchema"]);
                item->setProperty("resultSchema", tool["outputSchema"]);
            }
        }
        if (const auto* action = mixerChainAction(command.id))
        {
            auto invocation = juce::var(new juce::DynamicObject);
            invocation.getDynamicObject()->setProperty("tool", "vibera_mixerchain");
            invocation.getDynamicObject()->setProperty("action", action);
            item->setProperty("invocation", invocation);
            item->setProperty("macroAvailable", false);
            item->setProperty("asynchronous", command.id != "mixer.chain.directory");
            item->setProperty("stateReadback", "vibera_mixerchain action=state; poll action=job,operation exact session token until completed/failed/cancelled. Pending is not completion.");
            item->setProperty(
                "prerequisitesDetail",
                "Copy/save/paste/loadApply require current projectIdentity/revision/existing Mixer index and "
                "no gesture/take/native control/capture/projection work. Paste requires portable clipboard; "
                "loadApply decodes a direct .vibfx child of the configured existing local preset directory. "
                "Apply fresh-captures outgoing target state, privately prepares one canonical replacement, "
                "permits a brief existing audio reconfiguration, and completes only after publication and "
                "actual source activation (or explicit initial deferredNoDevice document completion). Pure "
                "structural Mixer-effects Undo/Redo uses the same interrupted path; no mixed-route/music "
                "fallback. Cancel drains actual owned capture/file/worker preparation before publication; "
                "after publication tooLate retains intended model and explicit native recovery state. "
                "Copy/save/loadClipboard remain nonmutating. Configure preserves root/history on refusal. "
                "Native provenance covers acknowledged supported interfaces; unsignalled widget edits remain "
                "outside guarantees.");
            if (includeSchemas) {
                item->setProperty("arguments", mixerChainWorkflowArguments());
                item->setProperty("resultSchema", mixerChainWorkflowResultSchema());
            }
        }
        if (command.id == "mixer.effects.browse" || command.id == "mixer.effects.scroll")
        {
            if (includeSchemas)
                item->setProperty("arguments", mixerEffectsArguments(command.id));
            auto *invocation = new juce::DynamicObject;
            invocation->setProperty("tool", "vibera_mixer");
            invocation->setProperty("action",
                                    command.id == "mixer.effects.browse" ? "browseEffects" : "scrollEffects");
            item->setProperty("invocation", juce::var(invocation));
            item->setProperty("stateReadback",
                              "vibera_mixer action=state: effectsBrowser/effectsPane; vibera_plugin "
                              "action=list for stable plugin IDs and zero-based order");
            item->setProperty(
                "prerequisitesDetail",
                "CONTROL navigation, current project/existing Mixer index. Browsing refuses active gestures "
                "and stale project replacement; does not load/replace/scan plugins. Scroll uses current "
                "selected insert and exact bounded offset. No history, dirty or transport changes; native "
                "editor open/close remains acknowledged vibera_plugin job work.");
            item->setProperty("macroAvailable", false);
        }
        if (command.id == "plugin.effect.mix" || command.id == "mixer.effects.bypass")
        {
            auto *invocation = new juce::DynamicObject;
            invocation->setProperty("tool", "vibera_command");
            invocation->setProperty("command", command.id);
            item->setProperty("invocation", juce::var(invocation));
            item->setProperty("macroAvailable", true);
            item->setProperty(
                "stateReadback",
                "vibera_plugin action=list: wet/chainBypassed/effectiveBypass; vibera_inspect entity=mixer: "
                "effectsBypassed/effects; vibera_state audio.pluginWorkers: wetBlend/bypassBlend");
            item->setProperty(
                "prerequisitesDetail",
                "Current project identity/revision and actual effect ID or Mixer index. Shared Controller "
                "validation/history and owned document or mixerEffects(sourceInsert) gestures; unrelated "
                "owners refuse. Live control "
                "targets do not finalize MIDI takes or change native state. No-op preserves "
                "revision/dirty/history. Mix0 and bypass keep native processing/latency. Completion means "
                "model commit; actual ramp/graph/audio and installed providers require separate "
                "qualification. Schema20 required for saved nondefault values.");
        }
        if (command.id == "mixer.insert.create" || command.id == "mixer.insert.delete")
        {
            item->setProperty("asynchronous", true);
            item->setProperty("macroAvailable", false);
            item->setProperty(
                "stateReadback",
                "Actual application structural job plus vibera_inspect entity=mixer and Mixer organisation "
                "selection/reference readback. Prepared model alone is not native completion.");
            item->setProperty(
                "prerequisitesDetail",
                "Fresh native-state capture and owned suspended full-projection preparation before canonical "
                "commit; no active gesture/take/solo/file/native work. Master protected. Delete defaults "
                "refuse references; explicit mixerDelete fields preserve incoming gain and reject "
                "collisions/cycles, never silently prune effects/automation/groups. Runtime/private "
                "sidechain ownership is not persisted in Project and requires separate engine admission. New "
                "IDs reserve only within current project session; Undo/discard does not recycle, reopening "
                "an earlier saved snapshot starts a new identity session. Public UI/MCP activation requires "
                "actual native/full-rebuild wiring and current receipts; model registration is not "
                "qualification. No seamless fixed-universe routing fallback.");
        }
        if(command.id.startsWith("synth."))
        {
            item->setProperty("semanticTool","vibera_synth");
            item->setProperty("semanticAction",synth::actionForCommand(command.id));
            item->setProperty("macroAvailable",false);
            item->setProperty("asynchronous",command.id=="synth.preset.load" || command.id=="synth.preset.previous" || command.id=="synth.preset.next" || command.id=="synth.user.save" || command.id=="synth.user.load" || command.id=="synth.parameter.set" || command.id=="synth.editor.open" || command.id=="synth.editor.close");
            if(includeSchemas) {
                auto schema=synth::synthToolDefinition()["inputSchema"].clone();
                auto fixedAction=juce::var(new juce::DynamicObject);
                fixedAction.getDynamicObject()->setProperty("const",synth::actionForCommand(command.id));
                schema["properties"].getDynamicObject()->setProperty("action",fixedAction);
                item->setProperty("arguments",schema);
            }

            item->setProperty("prerequisitesDetail","Discover stable preset IDs and native parameter keys/ranges with vibera_synth list. Sound writes require current plugin/instance/generation; structural recording guards remain active.");
            item->setProperty("stateReadback","vibera_synth browser/list/listUserPresets/parameters plus vibera_plugin job/editorState and canonical project history. Browser/navigation/preview are transient; sound patches use captured Undo.");
            item->setProperty("completionGuidance","Poll the advertised existing plugin or canonical structural job until completed/failed/cancelled. Navigation completion stores preferences; native window visibility is acknowledged separately. No pointer simulation.");
        }
        if (command.id == "plugin.io")
        {
            item->setProperty("asynchronous", true);
            item->setProperty("macroAvailable", false);
            if (includeSchemas)
                item->setProperty("resultSchema", mixerChainWorkflowResultSchema());
            item->setProperty("stateReadback", "Poll returned exact vibera_mixerchain job; inspect persisted "
                                               "main I/O and actual worker bus catalogue/layout separately.");
            item->setProperty(
                "prerequisitesDetail",
                "Fresh native capture, existing instrument/effect ID, no recording/gesture/foreign native "
                "work. Actual candidate bus negotiation before canonical publication; unsupported layouts "
                "refuse without model mutation. Same ID, state, bindings, wet/bypass preserved; captured "
                "Undo/Redo uses the same bounded rebuild. No multi-output graph or seamless transition "
                "promise.");
        }
        if (command.id == "plugin.effect.sidechain")
        {
            item->setProperty("asynchronous", true);
            item->setProperty("macroAvailable", false);
            if (includeSchemas)
                item->setProperty("resultSchema", mixerChainWorkflowResultSchema());
            item->setProperty(
                "stateReadback",
                "Returned exact Apply operation: poll vibera_mixerchain action=job,operation; "
                "inspect persisted plugin detector assignment separately from negotiated layout.");
            item->setProperty(
                "prerequisitesDetail",
                "Active prepared device/source, existing Mixer effect and surviving non-Master "
                "source insert; self-dependencies/cycles refuse. Fresh target native capture, "
                "canonical prepared history and actual candidate bus negotiation precede commit. "
                "Same effect ID/state/bindings are retained. One Undo/Redo uses the same Apply "
                "path. Recording/gestures/macros refuse; cancel drains owned work before commit, "
                "tooLate retains committed state. No-device changes refuse sidechain.deviceRequired. "
                "Provider capability and live routing remain subject to actual engine admission.");
        }
        if (command.id == "plugin.effect.replace")
        {
            item->setProperty("asynchronous", true);
            item->setProperty("macroAvailable", false);
            if (includeSchemas)
                item->setProperty("resultSchema", mixerChainWorkflowResultSchema());
            item->setProperty("stateReadback",
                              "Returned exact chain operation: poll vibera_mixerchain action=job,operation "
                              "until terminal; vibera_inspect mixer and vibera_plugin list show new stable "
                              "effect ID/order. Pending is not completion.");
            item->setProperty(
                "prerequisitesDetail",
                "Existing current Mixer effect target and valid VST3 effect descriptor; shared "
                "UI/application admission must fresh-capture native state before preparing the canonical "
                "replacement. Target automation links/initialization refuse rather than silently unlink. "
                "Other slots, bindings, routes and insert state remain exact; replacement native bindings "
                "are not copied. One real Undo/fresh Redo. Explicit suspend/reconfigure/resume is permitted; "
                "completion requires actual activation or truthful deferredNoDevice document state. Cancel "
                "drains owned work before publication; tooLate retains intended model and recovery readback. "
                "No capture freshness, arbitrary provider state, live audio or graph qualification is "
                "implied by registry presence.");
        }
        if (command.id == "plugin.effect.insert" || command.id == "plugin.remove")
        {
            item->setProperty("asynchronous", true);
            if (includeSchemas)
                item->setProperty("stoppedEffectResultSchema", channelRoutingResultSchema());
            item->setProperty("stateReadback", "For a returned numeric operation poll vibera_mixer "
                                               "action=routingJob; routing.completionState is authoritative. "
                                               "vibera_plugin action=list and vibera_inspect entity=mixer "
                                               "show actual stable effect IDs/order/state.");
            item->setProperty("completionGuidance",
                              "Healthy ordinary stopped prepared sources use a pending transaction for "
                              "empty-chain append or single-effect removal. "
                              "Capture, fresh worker startup, native source-close expansion, canonical "
                              "commit, exact graph selection and final-bank "
                              "retirement are distinct phases. Accepted is not completed. Before publication "
                              "cancelRouting accepts cancellation "
                              "but remains pending until owned work drains; after publication cancellation "
                              "is too late. Terminal outcomes include "
                              "completed,failed,cancelled. A failed operation may already have "
                              "documentCommitted=true: inspect before retrying. "
                              "Other existing structural paths retain ordinary completion behavior; this is "
                              "not general live-chain transition qualification.");
            item->setProperty("prerequisitesDetail",
                              "Existing Mixer insert or stable plugin ID, valid scanned Windows x64 VST3 "
                              "effect descriptor for insertion; "
                              "no recording/retained take, gesture, native editor/parameter work, "
                              "save/export/recovery or competing transaction. "
                              "Prepared path currently requires a healthy ordinary stopped worker source, "
                              "exact captured native state, empty-chain "
                              "append or complete single-effect inverse; broader chains/playing transport "
                              "remain separate required module work. "
                              "Shared Controller commands and checked Undo/Redo own the same musical "
                              "history; arbitrary native UI control is unsupported.");
        }
        if (command.id == "channel.mixer")
        {
            if (includeSchemas)
                item->setProperty("resultSchema", channelRoutingResultSchema());
            item->setProperty("invocation",
                juce::JSON::parse(R"JSON({"tool":"vibera_command","command":"channel.mixer"})JSON"));
            item->setProperty("macroAvailable", true);
            item->setProperty("asynchronous", true);
            item->setProperty("stateReadback",
                "Direct command receipt uses the same typed schema as vibera_state.channelRouting. "
                "Poll vibera_mixer action=routingJob,operation=<exact numeric receipt ID>; its receipt "
                "is nested in routing. routingState queries the latest operation. Inspect current "
                "Channel.mixer and currentProjectIdentity/currentRevision separately from historical job fields.");
            item->setProperty("resultPolling", juce::JSON::parse(
                R"JSON({"tool":"vibera_mixer","action":"routingJob","operationField":"operation",
                  "resultField":"routing","completionField":"routing.completionState",
                  "pending":"pending","terminal":["completed","failed"],
                  "retention":"Latest32 terminal operations; unknown/expired IDs return routing.unknownOperation."})JSON"));
            item->setProperty("cancellation", juce::JSON::parse(
                R"JSON({"tool":"vibera_mixer","action":"cancelRouting","operationField":"operation",
                  "availability":"too_late_after_publication","errorCode":"routing.tooLate",
                  "description":"The synchronous preparation/publication has finished before command admission returns. A returned job cannot be rolled back by cancelling. Too-late is failure of the cancellation request, not completion/failure of the underlying route: keep polling the original job; use checked Undo only after its terminal state."})JSON"));
            item->setProperty("prerequisitesDetail",
                "Existing current-session Channel and Mixer index; selectedChannel is only a default target. "
                "Read current project identity/revision and supply optional top-level projectIdentity/revision guards to refuse a replaced session or stale revision. "
                "No active local/remote gesture, recording/retained take, project/import/export/state/recovery "
                "or competing plugin mutation; shared Studio/Controller admission may refuse or finish only "
                "its supported checked work. Does not replace instruments or alter score. One changed assignment "
                "is one canonical Undo/Redo; assigning the existing insert is an exact no-op. Assignment-only "
                "batches/macros use the coordinator; mixed route/content sequences refuse atomically.");
            item->setProperty("completionGuidance",
                "Accepted/pending is not completed: require the exact operation's terminal receipt. "
                "For liveGraph, nativeCompletionKnown and posted/selectedSerial plus actual referenceStart/End "
                "describe software callback adoption; neither proves listening or hardware. "
                "deferredNoDevice with completionScope=document commits model/history only, retaining "
                "audioProjectionDeferred; it does not start a device, render offline audio or prove audible routing. "
                "noChange is a no-op. Inspect documentCommitted/documentChanged and structured error after "
                "failure because a later context/device interruption can follow a committed document. "
                "Never blindly retry a pending or committed job; poll/read current state first.");
        }
        if (isMixerStereoCommand(command.id))
        {
            item->setProperty("stateReadback", "vibera_inspect entity=mixer: index/stereoSeparation/phaseInverted/stereoSwapped; targets, not measured DSP settling");
            item->setProperty("macroAvailable", true);
            item->setProperty("prerequisitesDetail", "Existing stable insert including Master0. Shared document/recording/file/gesture admission. One Undo; exact gesture cancel. Continuous separation supports mixerControl lease. Completion is canonical target only, never callback/heard acknowledgement. Original linear crossmix curve; no exact FL DSP claim. Native main Mixer is stereo.");
            if (includeSchemas) item->setProperty("resultSchema", mixerStereoCompletionSchema());
        }
        if (command.id == "mixer.selectAll" || command.id == "mixer.organisation.menu")
        {
            auto call = juce::JSON::parse(R"({"tool":"vibera_mixer","action":"state"})");
            if (command.id == "mixer.selectAll") call.getDynamicObject()->setProperty("action", "selectAll");
            item->setProperty("invocation", call);
            item->setProperty("stateReadback", "vibera_mixer organisation; the menu's operations are the typed mixer.group/selection commands");
            item->setProperty("macroAvailable", false);
        }
        if (isMixerGroupCommand(command.id))
        {
            item->setProperty("stateReadback", "vibera_state mixerOrganisation; vibera_mixer organisation; UUID groups and stable insert IDs, canonical targets only");
            item->setProperty("macroAvailable", true);
            item->setProperty("prerequisitesDetail", "Existing stable insert IDs in the current Project; Master cannot join a display group. No unowned gesture or pending file/device/routing work. Armed/capturing/retained MIDI take preserved. Membership/delete refuses owning group solo. One Undo; owned macro/document gesture cancel exact. Groups never imply buses. Completion is canonical, never DSP settling.");
            if (command.id == "mixer.group.colourMembers")
                item->setProperty(
                    "stateReadback",
                    "vibera_inspect entity=mixer: each stable member index/colour; vibera_mixer organisation "
                    "retains marker/selection. Copies stored ARGB;0 resets each member's own defaults, not "
                    "identical rendered RGB. Canonical only; no DSP settling claim.");
            if (includeSchemas) item->setProperty("resultSchema", mixerOrganisationCompletionSchema());
        }
        if (command.id == "mixer.meterReset")
        {
            item->setProperty("invocation",
                              juce::JSON::parse(R"({"tool":"vibera_mixer","action":"resetOverload"})"));
            item->setProperty("stateReadback",
                              "vibera_inspect entity=mixer: meter overload/fence per insert");
            item->setProperty("macroAvailable", false);
        }
        if (command.id == "mixer.move" || command.id == "mixer.moveLeft" || command.id == "mixer.moveRight")
        {
            item->setProperty("stateReadback", "vibera_state mixerOrder; vibera_mixer order; vibera_inspect entity=mixer position/index");
            item->setProperty("macroAvailable", true);
            if (includeSchemas) item->setProperty("resultSchema", mixerInsertOrderCompletionSchema());
            item->setProperty("prerequisitesDetail", "Existing stable Mixer ID and non-Master source. Shared CONTROL admission refuses file/device/routing work and unowned gestures; pure order retains armed/capturing/retained MIDI take. One discrete Undo or one owned document gesture/macro; cancel restores exact order and dirty state. Absolute mixerPosition is a row, never an ID. Relative boundary is no-op; Master current row cannot move. Completion is canonical order only, not a DSP/heard acknowledgement. Insert membership uses mixer.insert.create/delete with reference safeguards and bounded native preparation; display groups have separate typed commands.");
        }
        if (mapping == "vibera_recording")
        {
            if (includeSchemas)
            {
                item->setProperty("arguments", recordingCommandArgumentSchema(command.id));
                item->setProperty("resultSchema", recordingOutputSchema());
            }
            auto *invocation = new juce::DynamicObject;
            invocation->setProperty("tool", "vibera_recording");
            invocation->setProperty("action", recordingActionForCommand(command.id));
            item->setProperty("invocation", juce::var(invocation));
            item->setProperty("stateReadback", "vibera_recording action=state; vibera_state recording; "
                                               "vibera_inspect entity=notes after completion");
            item->setProperty("macroAvailable", false);
            item->setProperty(
                "prerequisitesDetail",
                "Shared CONTROL take owner. Finish is idle no-op or stops capture and applies the existing "
                "take; it does not arm/start capture. MCP retry/cancel require exact current positive "
                "serial1..9007199254740991, scoped to this instance; consumed/stale tokens refuse. Retry "
                "requires original project/Pattern/Channels/Song placement and no gesture/file operation. "
                "Invalid captures require explicit cancel; applying refuses retry/cancel. Cancellation "
                "creates no document history; successful nonempty take is one Undo. Completion means "
                "canonical application only, not DSP adoption, heard audio or hardware. Read structured "
                "error and retained note pages; pending is not completion. UI shortcuts resolve the current "
                "take context; synchronous document macros do not support these actions.");
        }
        if (mapping == "vibera_groove")
        {
            item->setProperty("arguments", grooveToolDefinition()["inputSchema"]);
            item->setProperty("invocation",
                              "vibera_groove action=" + command.id.fromLastOccurrenceOf(".", false, false));
        }
        if (command.id == "rack.graph")
        {
            item->setProperty(
                "invocation",
                juce::JSON::parse(
                    "{\"tool\":\"vibera_rack\",\"action\":\"configure\",\"graphVisible\":true}"));
            item->setProperty(
                "stateReadback",
                "vibera_rack action=state: graph; notes are visible columns, inspect for all notes");
        }
        if (command.id == "rack.graphProperty")
        {
            item->setProperty(
                "invocation",
                juce::JSON::parse(
                    "{\"tool\":\"vibera_rack\",\"action\":\"configure\",\"graphProperty\":\"pitch\"}"));
            item->setProperty(
                "stateReadback",
                "vibera_rack action=state: graph.property, supportedProperties, units/defaultValue and "
                "visible notes; transient view state without musical Undo");
        }
        if (command.id == "steps.modX" || command.id == "steps.modY")
        {
            item->setProperty("macroAvailable", true);
            item->setProperty("stateReadback",
                              "vibera_rack state.graph and vibera_inspect notes/noteOccurrences: modX, modY, "
                              "noteModulationSupported/scope and null noteModulationAcknowledged. Source "
                              "values do not acknowledge prepared document or voice adoption.");
            item->setProperty("prerequisitesDetail",
                              "Existing raw aligned source steps on a local sampler Channel only. Mod X/Y "
                              "0..1 neutral0.5 offset cutoff/resonance by original composition "
                              "clamp(global + 2*(note-0.5),0,1); global Filter Off is a no-op. "
                              "Set/reset/additive-scale/ramp share UI and stepGraph lease history, Cancel "
                              "and held-take admission. Native/VST3 and Audio Clips unsupported. MIDI1 score "
                              "export omits modulation; project/WAV retain the local sampler expression.");
        }
        if (command.id == "steps.release")
        {
            item->setProperty("macroAvailable", true);
            item->setProperty("stateReadback",
                              "vibera_rack state.graph and vibera_inspect notes: releaseVelocity, "
                              "releaseMidiVelocity and releaseVelocityAudioResponse. Source state, "
                              "not callback adoption, MIDI delivery or audible-response ACK.");
            item->setProperty("prerequisitesDetail",
                              "Aligned source steps in an existing Pattern/non-audio Channel. All score "
                              "instruments retain/export data. Local sampler and current native Synth "
                              "ignore release velocity; other instruments are patch-defined. Natural "
                              "scored/Repeat offs use the value; forced Stop/seek/deletion use zero. "
                              "stepGraph lease shares history/cancel and held-input admission.");
        }
        if (command.id == "steps.repeat")
        {
            item->setProperty("macroAvailable", true);
            item->setProperty("stateReadback",
                              "vibera_rack state.graph and vibera_inspect notes: repeatMode, repeatRate, "
                              "repeatIntervalBeats and raw note IDs/beat/duration. Never DSP adoption ACK.");
            item->setProperty(
                "prerequisitesDetail",
                "Aligned source steps in an existing Pattern/non-audio Channel. Sampler, "
                "native and VST3 score scheduling supported; generated hits retain the source "
                "ID plus internal occurrence ordinal. Raw extent and Channel periods stay fixed. "
                "stepGraph lease shares live history/cancel and held-input admission. Expanded "
                "Repeat documents are bounded at100000 events; hosted processing also refuses "
                "repeat.midiCapacity before edits when dense score packets exceed the current "
                "block's conservative budget. Ordinary merged-input burst limits remain. "
                "One-shot sample tails follow existing envelope/choke/polyphony controls.");
        }
        if (command.id == "steps.shift")
        {
            item->setProperty("macroAvailable", true);
            item->setProperty("stateReadback",
                              "vibera_rack action=state graph and vibera_inspect entity=notes: "
                              "shiftPercent, shiftDelayBeats, raw beat and projected playbackBeat. "
                              "Source values are not callback adoption acknowledgements.");
            item->setProperty("prerequisitesDetail",
                              "Existing aligned source steps in the selected Pattern/non-audio Channel. "
                              "steps.shift supports sampler, native and VST3 score scheduling. "
                              "Use vibera_gesture domain=stepGraph for one live baseline/history/cancel, "
                              "including held MIDI. Delay persists after melodic conversion. Auto "
                              "extent and Channel periods use raw source cells; no implicit wrap. "
                              "MIDI part export encodes Shift but remains unswung and uncropped.");
        }
        if (command.id == "steps.pitch")
        {
            item->setProperty("macroAvailable", true);
            item->setProperty(
                "prerequisitesDetail",
                "Existing aligned step notes in a playable Pattern/Channel. Canonical set/reset/semitone "
                "transposition/ramp retain step identity; use vibera_gesture domain=stepGraph for live "
                "baseline-relative history/cancel including held recording. Read rack.graph pitch/value and "
                "inspect canonical notes. Other per-note expression properties remain unfinished.");
        }
        if (command.id == "steps.velocity")
        {
            item->setProperty("macroAvailable", true);
            item->setProperty(
                "prerequisitesDetail",
                "Selected playable Channel has aligned active step events. UI drag or vibera_gesture "
                "domain=stepVelocity shares one baseline/history and can retain a held MIDI take; domain "
                "targets cannot change. Settings Macros supports value-based current-Channel step edits. "
                "Positive velocities render through MIDI7-bit rounding; zero emits no scored hit. Pitch "
                "uses steps.pitch. Remaining graph properties, remembered removed steps and zero-velocity cut-note semantics are not "
                "implemented.");
        }
        if (command.id == "rack.soloRestore")
        {
            item->setProperty(
                "invocation",
                juce::JSON::parse(
                    "{\"tool\":\"vibera_rack\",\"action\":\"configure\",\"restoreMuteAfterSolo\":true}"));
            item->setProperty("stateReadback", "vibera_rack action=state: restoreMuteAfterSolo");
        }
        if (command.id == "channel.clone" || command.id == "channel.delete")
        {
            auto *invocation = new juce::DynamicObject;
            invocation->setProperty("tool", "vibera_rack");
            invocation->setProperty("action", command.id == "channel.clone" ? "clone" : "delete");
            item->setProperty("invocation", juce::var(invocation));
            item->setProperty("macroAvailable", false);
            item->setProperty("prerequisitesDetail",
                              "No recording take/gesture/macro/file operation. Shared asynchronous state "
                              "capture; poll vibera_rack job/cancel. Completion is model commit, not plugin "
                              "activation. Clone excludes score/events/external automation; delete removes "
                              "notes in every Pattern and owned audio placements. Undo restores captured "
                              "instrument state. Not supported by synchronous document macros.");
        }
        if (command.id == "transport.loop")
            item->setProperty("prerequisitesDetail",
                              "Prepared project audio; no active MIDI take when changing loop mode. "
                              "Canonical command toggles; vibera_transport action=loop enabled=<bool> is "
                              "idempotent. Runtime state only, not project Undo or saved loop regions. Read "
                              "audio.loopBounds; applied values are null without an audio context.");
        item->setProperty("prerequisites", command.id == "channel.mixer"
            ? "Current project and existing stable Channel/Mixer IDs; no conflicting owner/work. "
              "Shared checked command/history and prepared audio admission apply. Inspect operation "
              "scope and poll real native selection; document-only deferred completion is explicit."
            : "Use current-session IDs and revision. Document must validate; target kind must "
              "match operation. Undo is document-only. Availability depends on current "
              "selection, worker state and active gesture.");
        if (command.id == "channel.mixer" || command.id == "mixer.route" || command.id == "mixer.routeOnly" ||
            command.id == "mixer.send" || command.id == "plugin.effect.insert" ||
            command.id == "plugin.remove" || command.id == "plugin.effect.move" ||
            command.id == "edit.undo" || command.id == "edit.redo")
        {
            if (includeSchemas)
            {
                juce::Array<juce::var> outcomes;
                outcomes.add(channelRoutingResultSchema());
                outcomes.add(mixerChainWorkflowResultSchema());
                // Existing direct document/no-change responses remain valid.
                outcomes.add(juce::JSON::parse(R"JSON({"type":"object",
                    "required":["projectIdentity","revision","dirty","undoAvailable","redoAvailable","completionState"],
                    "properties":{"projectIdentity":{"type":"string"},"revision":{"type":"integer","minimum":0},
                        "dirty":{"type":"boolean"},"undoAvailable":{"type":"boolean"},"redoAvailable":{"type":"boolean"},
                        "completionState":{"type":"string","enum":["completed"]}},"additionalProperties":true})JSON"));
                auto schema = juce::var(new juce::DynamicObject);
                schema.getDynamicObject()->setProperty("anyOf", outcomes);
                item->setProperty("resultSchema", schema);
            }
            item->setProperty("asynchronous", true);
            item->setProperty(
                "stateReadback",
                "Numeric operation: existing vibera_mixer routingJob/cancelRouting. String operation: "
                "existing vibera_mixerchain job/cancel, exact token. Delayed/auxiliary or general FX "
                "structural operations use one full native capture and bounded interrupted Apply; "
                "no fallback after native guard refusal. Existing send-level updates retain live controls. "
                "Pending/cancelled/committed activation failure are distinct; inspect the returned actual "
                "job.");
            item->setProperty(
                "boundedStructuralPolling",
                juce::JSON::parse(
                    R"JSON({"tool":"vibera_mixerchain","action":"job","operationField":"operation",
                    "operationType":"string","completionField":"completionState",
                    "pending":"pending","terminal":["completed","failed","cancelled"]})JSON"));
            item->setProperty(
                "boundedStructuralCancellation",
                juce::JSON::parse(
                    R"JSON({"tool":"vibera_mixerchain","action":"cancel","operationField":"operation",
                    "availability":"before_model_publication_after_owned_drain; tooLate_after_commit"})JSON"));
            item->setProperty("boundedStructuralMacroAvailable", false);
        }
        operations.add(item);
    }
    return operations;
}
} // namespace vibera
