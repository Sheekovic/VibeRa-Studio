# Select plugin native audio input and output

This is current Mixer development guidance, not qualification of an older
package. Discover `plugin.io` through `vibera_commands` with
`includeSchemas=true`, and the connected `vibera_plugin` tool's `buses` action.
The ordinary default remains native bus0, stereo mapping, one stereo output.
This selects native plugin buses onto the existing stereo Mixer path; it does
not implement simultaneous multi-output, surround mixing, hardware audio input
selection, monitoring or record-arm/capture. Recording-module dependencies remain
explicit; a native audio-input bus is not a physical audio interface input.

Read `vibera_state` for current `projectIdentity` and `revision`, then paginate
`vibera_plugin action=list` to find the actual existing instrument/effect ID and
its exact `instance`. A slot ordinal, Channel ID, Mixer row or bus name is not a
plugin ID. IDs belong to the current project; instances change after native
replacement/recovery. Rediscover after either changes.

Query actual preparation metadata using `vibera_plugin`:

```json
{"action":"buses","plugin":17,"instance":"<exact current instance>","revision":12}
```

The numeric values above are illustrative; use the actual current values.
Read `audioIo.requested`, `negotiated`, `catalogueKnown`, `available`, `inputs`
and `outputs`. Each bus row contains its native `index`, `name`,
`defaultChannels` and `actualChannels`. Zero actual channels means disabled.
`auxiliaryInputBus`/`auxiliaryChannels` describe the separately negotiated
existing detector. `mixerChannels=2` and `simultaneousMultiOutput=false` describe
the graph boundary. A completed read-only query changes no document/history and
is not an acknowledgement of audible processing or graph activation.

Actual names/default counts are not a support matrix. A bus with six default
channels is not proof it accepts this stereo output contract; every requested
combined arrangement must negotiate privately before publication. Unavailable
or missing providers have no known current catalogue and retain requested
project data for recovery. Do not invent bus names/capabilities from descriptors.

Change layout through `vibera_command`, with the current outer `revision` and
all four required fields in `args.pluginIo`:

```json
{"command":"plugin.io","revision":12,"args":{"target":17,"pluginIo":{"inputBus":1,"outputBus":0,"inputChannels":1,"monoSource":3}}}
```

| Field | Values | Meaning |
| --- | --- | --- |
| `inputBus` | integer0..31 | One actual native main input bus. It must differ from any assigned detector input. |
| `outputBus` | integer0..31 | One actual native output bus negotiated as stereo. Other output buses are disabled. |
| `inputChannels` | integer1 or2 | Mono or stereo main input; actual plugin support is negotiated. |
| `monoSource` | integer0..3 | Stereo requires0. Mono requires1=left,2=right,3=average `(L+R)/2`. |

Specify exactly these four payload fields; omit none and do not attach the
payload to another command. The native indices are bounded, not proof those
buses exist. Existing detector assignment remains separate:
`plugin.effect.sidechain` selects one auxiliary bus1..31, mono/stereo, with its
own source and cycle/layout validation. A conflicting main input is refused;
this command does not silently disconnect or move the detector.

For a genuine no-audio-input instrument/generator, the exact old default0/stereo
request can represent no native audio input; inspect the actual input rows.
A nondefault nonexistent input is refused. Main mono conversion feeds only the
native wet input; existing host stereo dry/bypass behavior remains independent.

The Channel instrument and Mixer effect context menus expose **Audio
input/output**, with native main input bus, native stereo output bus, input
channel mapping and **Restore default audio I/O**. They use this same canonical
command. Reopen a stale menu instead of applying an old target/instance intent.

Changing layout is a structural transaction: finish gestures, native editor/
parameter requests, file/capture/preparation work and recording first. Armed,
capturing or retained MIDI takes refuse incompatible structural work without
silently discarding their data. The current plugin must be available/prepared;
honor explicit preparation/device refusals. Brief bounded audio interruption is
accepted; uninterrupted arbitrary tails or unrelated voices are not promised.

A changed command returns an exact session string `operation`. Poll
`vibera_mixerchain action=job, operation=<that token>` until actual
`completed`, `failed` or `cancelled`. Do not use a latest-operation query, retry
the mutation while pending, or treat model acceptance as activation. A genuine
no-op may return direct completed document state without creating a job/Undo.
The ordinary read-only `buses` action is not this mutation's job observer.

Inspect `documentChanged`, `undoable`, `audioState`, `nativeRecoveryRequired`,
`code` and `message`. Unsupported/missing/conflicting requested layouts must
fail before canonical commit and preserve incumbent state/history. This differs
from reopening a genuinely missing-provider project as a recoverable placeholder.
After publication, failure may retain the intended committed state/history and
require audio recovery; failure does not imply rollback. `deferredNoDevice`, if
returned, means document/deferred preparation only, never heard/native activation.
The existing Apply activation watchdog is10000ms; startup/preparation has its own
bounds. Query actual current job/audio state after timeout before another edit.

Cancel through `vibera_mixerchain action=cancel` with the exact operation token.
Before publication, cancellation drains owned capture/private preparation and
remains pending until it really settles. After commit, `effectChain.tooLate`
leaves the original activation/recovery operation authoritative. Session tokens
cannot be resumed in a newly launched Studio instance. Never report an already
published native layout as rolled back merely because cancellation was requested.

One completed change owns one canonical Undo step. `edit.undo`/`edit.redo` use
fresh captured native state and the same bounded structural restoration; observe
their returned job through completion, then rediscover the current instance and
read both requested and negotiated layout. Save/reopen preserves all four fields
in schema26, while genuine older projects retain default0/stereo. Portable
chain Format2 preserves nondefault main I/O; default slots keep genuine Format1
encoding. A saved selection does not prove another installed provider/instance
will negotiate it. See [Effect-chain reuse](effect-chains.md).

`plugin.io` is explicitly unsupported in synchronous document macros and
`vibera_batch`. Run it as its own operation and await the real result. Macro
storage/import/execution rejects `plugin.io` or a stray `pluginIo` payload with
`pluginIo.macroUnsupported`; batches reject them before any step with
`pluginIo.unsupportedBatch`. Never remove the payload or substitute another
command to make a sequence appear successful.

Repository source contracts (not executable-generated release evidence):
[typed layout](../../../src/PluginBuses.h),
[shared UI/readback](../../../src/StudioPluginIo.inc),
[canonical admission/history](../../../src/StudioRoutingCoordinator.inc),
[asynchronous application](../../../src/StudioMixerChains.inc),
[MCP schemas](../../../src/McpContracts.h) and
[native preparation](../../../src/PluginWorker.cpp).
Discover current support and retain actual original/installed-provider results;
this guide establishes no compiled/runtime/provider, listening, hardware or
complete-module qualification.
