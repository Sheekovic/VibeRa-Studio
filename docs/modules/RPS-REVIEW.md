# RPS row review (2026-10-25)

Row-by-row honesty review of the Rack/Pattern/sampler checklist before the
0.0.66 module release. Each row lists the scope items, where they live, and the
gaps found. A row is marked `verified` in the ledger only when no software gap
remains; human listening, physical MIDI/audio hardware, native OS pointer input
and FL familiarity stay separate, unclaimed acceptance (see the module doc).

| Row | Delivered scope (code / tests) | Gaps found | Closed by |
| --- | --- | --- | --- |
| A fundamentals | Multi-instrument scores, linked/unique placements (`clip.unique`), add/replace sample/instrument, stable IDs, Rack selection/active MIDI target, filtered steps/drops, name-button instrument window, separate Piano Roll, empty-project recovery, missing-plugin placeholders (gui_workflow, musical_acceptance, channel_management_mcp, studio_channel_routing*) | none found | - |
| B organisation | Names/colours/icons, colour picker, sorting, groups/filter, bulk actions, settings-only clone, Automation Clone with links, global/group/locked solo with exact mute policy (channel_* tests, rack_groups_mcp) | none found | - |
| C step composition | Strokes, fills, melodic conversion, Advanced Fill (`steps.compose`), nine Graph properties with ramps/scale/reset, swing multiplier/truncation (step_* tests, swing_mcp) | none found | - |
| D extent/meter | Auto/manual extent, shrink/growth, per-Pattern meter, dormant notes, linked windows, export/record boundaries (pattern_extent*) | none found | - |
| E Channel loops | Per Pattern+Channel fixed/Step/Beat/Bar loops, repeated Rack preview, Burn with history (pattern_channel_loops*) | Burn refused during playback (Piano Roll repeats already existed: `pianoNoteOccurrences`, piano navigation acceptance) | Burn admitted during playback, recording still refuses; `live_burn_gui` |
| F Pattern management | Create/find-empty/rename/colour/order/clone/delete/split/transpose/Picker/groups; bulk clone/delete, unused cleanup, Pattern WAV, render to audio clip (pattern_management, clip_groups_*) | none remaining | e8ec55c9, afa40067 |
| G source controls | Waveform/keyboard/root/pitch/range/reverse/normalize/ADSR, fades, Preview source vs Preview processed | interpolation quality choice (linear only) | HQ Hermite option (`channel.sampler.hqInterpolation`), sampler_fade error oracle |
| H filter/modulation | Filter types, mod envelopes/LFO with tension/tempo sync, five destinations, automation clips, destination-aware MIDI Learn (8969fad5) | none found (the module doc note predated 8969fad5) | - |
| I voices | Cut/cut-by/self-cut, polyphony, Mono/porta/gate, per-note Slide/Porta/colour groups | none found | - |
| J tempo/loops/stretch | Owned cancellable tempo analysis, BPM/beat/bar fit, manual/ping-pong loops, loop crossfade, Keep pitch stretch | embedded WAV loop metadata; stretch prepared synchronously on the UI thread | smpl loop read + `channel.sampler.loopPoints`/Use file loop; owned background stretch job with preparationPending; sampler_fade, rps_gaps_mcp |
| K presets | Sampler and instrument Channel presets with fresh native capture, overwrite confirmation, missing-sample/plugin recovery, typed errors | MCP could apply but not save a preset | `vibera_project` savePreset/loadPreset jobs; rps_gaps_mcp |
| L safety/persistence | One history per action, exact refusals, project42 migration, save/reopen/export of every new setting, structural recovery | stretch UI-thread stall (see J) | closed with J |
| M semantics/guidance | Canonical commands for every new control, skill references updated | MCP preset save; Preview processed has no MCP path | savePreset/loadPreset; `vibera_transport` previewNote |

Decision record: rows whose gaps are closed and whose focused plus full-suite
tests pass on the release binaries get a structured receipt bound to the source
files and exact candidate binaries. Any row with an unclosed gap stays open and
the module gate keeps refusing the release.
