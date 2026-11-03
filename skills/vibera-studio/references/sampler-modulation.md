# Sampler per-note Mod X/Y

Development-source workflow; discover the connected executable before use.
This does not claim the qualified0.0.65 distribution contains these controls.

Select a local sample Channel, open Channel Rack Graph Editor with Ctrl+K, and
choose **Mod X (cutoff)** or **Mod Y (resonance)**. Values are normalized0..1;
the midpoint.5 is neutral. Left-drag paints, Shift adjusts finely, Ctrl translates
the original baseline by the anchor offset, right-drag ramps and Alt resets.5.
Escape cancels exactly; a committed stroke has one Undo/Redo. Chords and muted/
zero-velocity retained steps keep IDs and activation. Targets remain raw,
on-grid, in-window step sources, independently of Channel-loop repetitions.

Mod X offsets the sampler filter cutoff; Mod Y offsets resonance. VibeRa uses
the independently specified composition `clamp(global + 2*(note-.5), 0, 1)`.
This is not a recovered FL formula or a claim of matching proprietary DSP.
Filter Off has no audible modulation; LP default full-open/no-resonance stays
dry for neutral notes. Use the Channel-name sampler window to choose an enabled
filter and sensible global cutoff/resonance. Global/manual edits retain note
offsets, and held score-note edits preserve source/phase/envelope ownership.

1. Inspect `vibera_state` and `vibera_rack action=state` for real IDs and
   availability. Discover `steps.modX` / `steps.modY` with `vibera_commands`
   `includeSchemas=true`. Configure `vibera_rack action=configure` with
   `graphVisible=true, graphProperty=modX|modY`; this is view state, not Undo.
2. Execute the canonical command with `pattern`, `channel`, raw `ids` and
   `value`. Empty IDs mean all eligible step sources, not the Piano selection.
   `text=set|reset|scale|ramp`; reset is.5. Scale requires a targeted `target`
   source anchor and translates its immutable baseline. Ramp `value`/`offset`
   are0..1, and `beat`/`length` use quarter-note beats with quarter-step-aligned
   in-window endpoints. Reverse/zero spans work; shrinking restores the baseline
   outside the latest span. Invalid ranges/types/foreign IDs refuse atomically.
3. For continuous changes begin `vibera_gesture domain=stepGraph`, update the
   returned operation using the same command/arguments, then commit or cancel.
   Pending is a preview, not completion. Expiry/cancel restores the whole baseline.
   These controls share the existing recording/take ownership and busy policies.
4. Inspect notes, note occurrences, main state and Rack Graph for raw `modX`,
   `modY` and `noteModulationSupported` / `noteModulationScope`. Native Synth,
   VST3 and Audio Clip targets do not implement these local sampler properties;
   they may retain stored values for later instrument replacement. Never infer
   plugin control or audible application from storage. The acknowledgement field
   is null; inspect pending audio publication separately and verify actual output.
5. Check exact Undo/Redo and unrelated fields, then save/reopen through actual
   terminal job outcomes. Project36 sparsely stores nonneutral values; missing
   fields mean.5. Any modulation field below36 refuses, including neutral.5.
   Older readers refuse the newer project rather than silently drop its sound.
6. Imported-note macro payloads need profile16 only when nonneutral modulation
   is present. Use typed `recordedNotes` fields and current canonical discovery;
   old profiles cannot carry the new fields. Numeric Mod commands share ordinary
   canonical macro behavior. Do not reuse selection IDs as raw step targets.
7. WAV preserves the sampler response; await completed/failed and inspect the
   actual file. MIDI1 cannot encode independent per-note sampler Mod X/Y.
   Export explicitly reports omitted expression occurrences only on success;
   pending/failed counts remain unknown. Use project/WAV to retain the sound.

Direct continuous MIDI CC learn for note Mod X/Y is not exposed yet: the current
binding has no captured raw-note target and owned stroke. Use a current-context
macro or UI/MCP gestures; the complete controller roadmap remains open. Channel
filter cutoff/resonance CC mappings are separate existing controls.

Full Piano Roll property UI, filter envelopes/LFO/tempo synchronization, alternate
filter algorithms and whole-module/human qualification remain separate open work.
Actual test results, limitations and exact source identity belong to STATUS and
verification receipts, not this workflow description.
