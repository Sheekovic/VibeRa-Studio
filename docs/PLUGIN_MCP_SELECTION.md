# Loaded-plugin MCP UI selection

Development0.0.37 extends existing vibera_select; it adds no tool, filesystem
access, plugin discovery or native parameter writes. The final metadata-corrected build is qualified:15/15 source and14/14 packaged
checks, raw MCP432 and official SDK1.32.1/16 checks. Exact identity is in STATUS.md.
The verified sole distribution remains the exact tuple recorded in STATUS.md.

## Interface and ownership

Provide `plugin` (an existing project plugin ID) and optionally `instance` (the
runtime UUID read from audio.pluginWorkers). `pluginView` selects `controls`
(default), `native` or `closeNative`. The controls view additionally accepts one
unsigned native `parameter` ID, including0, or `lastTweaked:true`. Pattern/Channel/
scope fields cannot be mixed into the same plugin request. Unsupported/malformed/
unloaded/stale requests refuse before changing an inspector or launching anything.

The target must already exist in the current musical model and have an alive
runtime host. No caller-supplied descriptor, file path, opaque plugin state, IPC
pairing token or arbitrary process/window handle is accepted. Instance UUIDs
identify runtime lifetime; they are not worker pairing credentials. Normal tool
initialization, schema/size validation and current document/action boundaries
remain active. Selecting a parameter does not set its value or enter project Undo.

Repeated requests reuse the same inspector for the same plugin/host. A host
replacement cannot reuse stale controls. Parameter requests refresh metadata
asynchronously; read `audio.pluginControls.ready` and poll. An unavailable or
hidden native ID refuses; read-only controls can still be selected for inspection.
The existing selector path avoids Pattern/Channel selection and rebuild requests,
preserving held MIDI and graph/device/worker identity.

The bounded inspector snapshot exposes current native gesture serial, parameter,
generation and final value. The cached begin value is deliberately not presented
as canonical Undo authority. Schema/epoch/loss rules remain in
NATIVE_PLUGIN_GESTURES.md; no new DSP or IPC protocol changes are introduced.

## Qualification and retained negatives

The first focused stdio run passed432 checks, including invalid/unloaded plugin
requests and unchanged project/worker state. Original SDK GUI cases passed native
Last tweaked selection through MCP, inspector reuse, stale-instance refusal and
held-MIDI preservation. Later metadata/observation additions require the final
matching-source whole-suite/package run before delivery is declared qualified.

The initial4e41e68 transaction passed15/15 source and14/14 packaged checks. Its
concise receipts are retained as build/plugin-mcp-initial-{identity,qualification}.json;
the application is updated in place, not kept as a second runnable checkpoint.

An adjacent metadata failure was then reproduced with the original native SDK
provider: metadataAvailable=false/error3 while cached ready=true. Source checkpoint
065d841 and build/plugin-mcp-metadata-red retain the actual failure, snapshot and
hashes. Ready now additionally requires available error-free metadata. The focused
fix passed both Pattern/Song failure/recovery cases with the same inspector,
document, held MIDI, worker and graph/device; MCP refuses selection while metadata
is rejected. The final fixture uses a negative step count; the first red/blue
case used a rejected maximum count, which is retained separately. No upstream
step-count overflow is needed for the final regression. This is structural
metadata failure/recovery, not audio callback deadline qualification. Final
matching-source/package qualification still follows.

Installed Analog Lab V can be loaded, its inspector selected through MCP, and its
actual native editor opened. These observations do not qualify its native gesture
callbacks. The bounded desktop probe retains these separate results:

- Synthetic component input did not keep the expected Rack popup available in
  hidden or visible test roots. The selection API removes this test dependency.
- A first800ms editor observation was too early. Bounded creation polling and a
  three-second settling interval produced a fully rendered native capture. The
  initial white/partial capture is retained; it is not labeled a product defect.
- Windows declined ordinary foreground activation. Global mouse input was never
  sent. No foreground/security settings or unrelated windows were altered.
- Messages to the initial window target changed neither Master value nor native
  gesture serial. This is not evidence of a gesture-notification defect. Nested
  native child geometry is retained for follow-up.
- A test-only GetTickCount DLL lookup failed before input and was corrected.
  The next attempt paused on the recent-user-input guard, with no native input.
  A separate image-inspection timeout closed its owned instance without input.

Receipts, scoped native before/after PNGs and process/hash identities remain under
`build/native-provider-gesture-probe/`, with terminal logs under
`build/native-provider-mcp-*.log`. They are failed/unqualified experiments, not
passed acceptance. Every retained task instance exited; no user app was terminated.

## Exact continuation

Source/full packaged verification for the selector is complete. Qualify the actual
provider with a working scoped native interaction mechanism when the desktop is
idle; first prove the native value actually changes, then require a fresh gesture
serial/native ID and Last tweaked selection with the same worker identity. Inspect
the native accessibility tree as a read-only alternative before assuming raw
messages emulate real input. Do not force foreground through unrelated threads
or weaken user-activity/ownership guards. The full provider scenario still needs
playback, persistence/export and gesture Undo; listening/hardware stay separate.
Independent automation/DAW work continues while this specific UI check is pending.

Final74b8bbe correction passed full source/package qualification with the negative
metadata fixture. Both bad-state snapshots report ready=false/error3; recovery
and held-MIDI/identity checks pass. Fresh inspector/main/provider renders inspected;
main native shutdown graceful0. Real-provider gesture compatibility remains open.

## Continuation0.0.38

The same interface now also reports audio.lastTweakedPlugin or null; its native
ID/runtime/timestamp are notification state, not a canonical Undo-before value.
Source15/15, packaged14/14, raw432 and official SDK16 pass for0.0.38. Actual0.0.37
Analog Lab Master gesture/Last tweaked now has stopped-state native evidence via
the accessibility-identified JUCE input peer. Earlier raw-message failures remain.
The final0.0.38 native menu/save test was refused by the recent-input guard after
its corrected disposable project/editor loaded; it is unqualified, root exit0.
See LAST_TWEAKED.md and STATUS.md for exact subsequent gates.
