// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera
{
inline juce::var rackToolDefinition()
{
    return juce::JSON::parse(
        R"JSON({"name":"vibera_rack","description":"Shared Channel Rack selection/filtering, groups and asynchronous settings-only Channel clone/delete. Clone/delete capture selected plugin state before one document transaction; poll job/cancel until terminal, then read audio/plugin preparation health separately. Reorder uses channel.moveUp/moveDown via vibera_command. Filtering never changes routing or the active MIDI target. Discover stable IDs first.","inputSchema":{"type":"object","additionalProperties":false,"properties":{
      "action":{"type":"string","enum":["state","filter","select","all","group","scroll","configure","clone","delete","job","cancel"]},
      "operation":{"type":"string","maxLength":100,"description":"Current-session Channel clone/delete job ID, required for job/cancel. Poll until terminal. Completion means the actual document transaction committed after state capture; read audio state for subsequent engine preparation/plugin health."},
      "muteRemovedSteps":{"type":"boolean","description":"configure only: persist the Workflow preference. False deletes removed step chords; true retains their source IDs/properties as muted notes. Reactivation restores them. Initially false for legacy compatibility; not musical Undo. Do not combine with another persistent preference or graph changes. Activation edits the raw source position, independently of playback-loop repetition or shrinkage."},
      "restoreMuteAfterSolo":{"type":"boolean","description":"For configure: persist the Workflow solo-restore preference before changing live settings. Initially true. Preferences are not musical Undo."},
      "graphProperty":{"type":"string","enum":["velocity","pitch","pan","fine","shift","repeat","release","modX","modY"],"description":"For configure: select the graph property through the shared view service. Session view state, not musical Undo. Do not combine with restoreMuteAfterSolo or muteRemovedSteps. Pitch has MIDI0-127 units/reset60; local sampler Pan normalized-1..1 and Fine Pitch cents-100..100 reset0. Shift percent0-100 resets0 and delays percent*0.002475 quarter-note beats; supported on all score instruments. Repeat selects rhythmic rate0-14 (Off through64th triplet), all score instruments; source duration gates repeats and source IDs remain stable. Release normalized0..1/default0 is MIDI note-off data, not ADSR duration; local sampler and current native Synth ignore it, other instruments are patch-dependent. Native/VST3 pan/fine unsupported. Mod X/Y0..1 neutral0.5 offset local sampler filter cutoff/resonance using original clamp(global+2*(note-0.5),0,1); Filter Off is a no-op. Native/VST3 Mod X/Y unsupported; source metadata is not a voice/DSP ACK. Edit canonical steps.pitch/pan/fine/shift/repeat/release/modX/modY via stepGraph lease."},
      "graphVisible":{"type":"boolean","description":"For configure: show/hide the selected-Channel step Graph Editor. Initially false; session view state, not musical Undo. Ctrl+K/UI share this service. Edit existing in-window on-grid source steps (including muted/zero-velocity notes, retaining activation) through steps.velocity and vibera_gesture domain=stepVelocity. Pitch/Pan/Fine/Shift/Repeat/Release/Mod X/Mod Y use graphProperty and canonical steps.pitch/pan/fine/shift/repeat/release/modX/modY, with stepGraph history. Pan/Fine and Mod X/Y require local sampler targets. Other properties remain unsupported."},
      "firstRow":{"type":"integer","minimum":0,"maximum":128,"description":"For scroll: zero-based visible Channel row, clamped to viewport bounds. Omit to retain."},
      "firstStep":{"type":"integer","minimum":0,"maximum":4000000,"description":"For scroll: zero-based step column, clamped to the full explicit Pattern length. Omit to retain."},
      "kind":{"type":"string","enum":["all","unsorted","group"],"description":"Required for filter. All/Unsorted are virtual views, not group IDs."},
      "group":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Stable group ID, required only for kind=group."},
      "ids":{"type":"array","maxItems":128,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000},"description":"Stable Channel IDs, required for select. Group/clone/delete default to shared Rack selection; explicit clone/delete needs 1..128 IDs. Clone inserts settings-only copies adjacent to each source and selects them; delete removes their notes in every Pattern and owned Audio Clips, never files."},
      "mode":{"type":"string","enum":["replace","add","remove","toggle"],"default":"replace"},
      "activeChannel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Optional explicit active instrument/MIDI recipient. Selection alone preserves the active recipient and held take."},
      "allChannels":{"type":"boolean","default":false,"description":"For all: true selects the whole project; false selects visible rows."},
      "name":{"type":"string","maxLength":128,"description":"For group: existing or new name, 1..128 characters, no outer spaces, All/Unsorted reserved. One Undo includes creation and assignment."},
      "revision":{"type":"integer","minimum":0},
      "projectIdentity":{"type":"string","maxLength":100}
    },"required":["action"]}})JSON");
}
} // namespace vibera
