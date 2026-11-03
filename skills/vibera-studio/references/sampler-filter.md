# Sampler filter

Discover `channel.sampler.filterType`, `channel.sampler.filterCutoff` and
`channel.sampler.filterResonance` with `vibera_commands includeSchemas=true`.
This is source guidance; an older qualified executable can lack these commands.

Use a stable local sample/original-instrument Channel ID from `vibera_state`.
Native Synth, VST3 instruments and streamed Audio Clips refuse these commands;
their own supported plugin parameters are separate. Every command requires
`channel` and `value`, changes the same document as the sampler window and has
canonical Undo/Redo. Invalid targets/types/ranges refuse without consuming history.

| Command suffix | Value | Default |
| --- | --- | --- |
| `filterType` | Integer:0 Off,1 LP12,2 LP24,3 HP12,4 BP12,5 BS12 | 1 |
| `filterCutoff` | Normalized0..1, logarithmic20..20000Hz, capped at45% of output rate | 1 |
| `filterResonance` | Normalized0..1, linear Q0.7071067811865476..12 | 0 |

These are independently implemented VibeRa filters. Do not claim exact FL filter
algorithms or curves. Off is dry. LP12/LP24 at cutoff1/resonance0 are also fully
dry; HP/BP/BS never implicitly bypass at that endpoint. Resonance can raise
peaks, especially in cascaded LP24. There is no automatic limiter.

For example, discover the Channel then execute:

```json
{"command":"channel.sampler.filterCutoff","args":{"channel":1,"value":0.5663233348}}
```

The illustrative value targets approximately1000Hz. Always calculate from
`log(frequency/20)/log(1000)` and use the actual stable ID. UI sliders show
percent, whereas MCP/macros/controller bindings use normalized units.

For continuous editing, select the intended Channel with `vibera_rack`, begin
`vibera_gesture domain=samplerFilter`, and retain its ownership token. Update
only the three filter commands with explicit `channel` and `value`. Commit
creates one Undo for the whole gesture; Cancel/Escape/local preemption/lease
expiry restores its baseline. Selection changes cancel the lease. This domain
permits an already armed/capturing MIDI take; retained pending takes and unrelated
busy/project-I/O guards still apply. Do not call accepted previews completed.

Read `channels[].sampler.filterType`, `filterTypeName`, `filterCutoff`,
`filterResonance`, `filterQ`, `filterBypassed` and `filterAvailable`.
`filterSampleRate` and `filterCutoffHz` are null if no output device is available;
they are not inferred from source-file sample rate. Document completion does
not acknowledge DSP adoption. Check `pendingAudioUpdate` separately; sounding
voices approach coefficient/mode changes over about5ms without resetting their
source position or volume envelope. Stop/seek/source replacement have separate
voice-lifetime behavior.

Click the Channel name to open its sampler, then use the Sampler filter section.
`Preview source` auditions dry source PCM. Play the sampler keyboard, MIDI input
or a Pattern to hear processing. Saving/reopening retains all three controls;
WAV export uses them at the render rate. Follow actual asynchronous save/export
jobs to completion or failure and verify the resulting file. These settings
require project35; do not relabel a modern project as an older schema.

Settings macros can store these canonical numeric commands without a new macro
profile revision. MIDI controller mappings use the same commands and units;
filterType rounds the mapped value to an integer. Physical controller behavior
requires separate hardware qualification.

Per-note Mod X/Y, modulation envelopes/LFO, tempo sync and alternate FL filter
architectures remain unfinished. This manual filter foundation does not imply
those workflows work, or that audio has been listened to.
