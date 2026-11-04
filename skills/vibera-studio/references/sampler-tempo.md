# Source-bound sampler tempo

Discover `vibera_samplerTempo` from the connected application. This workflow
targets a stable sample Channel ID, independently of the current Rack selection.
It requires the **current prepared sample**; native plugin instruments and
streamed Audio Clip Channels are unavailable. Source loading supports up to
30 seconds. Detection analyzes loaded immutable PCM, bounded to its first
120 seconds; this ceiling does not extend the 30-second import limit.

`state` returns source BPM, fit, full `sourceSeconds`, `trimmedSeconds`, project
identity, `method="resample"` and `changesPitch=true`. Wait for actual source
preparation after loading/replacing a sample. An unavailable response is not an
analysis session or successful preparation. Source files and PCM are not edited.
`operations` lists retained current-project tokens for this Channel, including
UI-created analysis/confirmation. Poll those tokens with `job`. At most four
analyses may be unfinished globally; cancel or await them after `.busy`.

## Detect, confirm and apply

```json
{"action":"detect","channel":7,"projectIdentity":"current-session"}
{"action":"job","operation":"returned-token"}
{"action":"apply","operation":"returned-token","fit":true}
```

Detection may return `pending` or already `completed`. Poll its exact token to
terminal. `completed` with `phase="confirmation"` means an estimate is ready;
**no edit has been applied**. Check `applied`, `documentChanged`, `estimateBpm`
and any `error`. The original onset detector searches 60–200 BPM; silence,
short samples and weak rhythmic evidence can return `samplerTempo.noEstimate`.
Half/double tempo and non-percussive material remain ambiguous. Confirm manually;
do not treat a successful analysis as proof of musical tempo.

Use `capture` instead of `detect` for manual BPM or musical-length confirmation.
Both retain a witness of the exact project, Channel, prepared Sample, source and
all sampler settings. Source/settings replacement makes that token stale, even
if another Channel is now selected. A stale token cannot overwrite newer settings.
Capture a new session instead. Unrelated document edits are not an automatic
settings-witness failure; bars intentionally use the meter at actual application.

Apply accepts **at most one** of:

| Input | Meaning |
| --- | --- |
| `sourceBpm` | Explicit original BPM, 20–400. Omitted for a detected result uses its estimate; a manual capture uses its captured source BPM. |
| `beats` | Length of the trimmed source in quarter notes, 0.0625–2048. BPM is `60 × beats / trimmedSeconds`. |
| `bars` | Integer 1–512 current document bars. Quarter notes are `bars × numerator × 4 / denominator`. |

Beats/bars enable fitting by default. For BPM, omitted `fit` preserves the
captured fitting setting; pass `fit=true` to enable it explicitly. `fit=false`
records BPM without fitting. The resulting BPM must remain 20–400 regardless of
the input field's own range. Full-source BPM versus trimmed musical length is
explicit: the length modes derive BPM from the current captured trim, not the
whole file. Applying BPM and fit is one canonical Undo step. No-op application
sets `applied=true` with `documentChanged=false` and adds no history.

Fitting is **Resample**: project BPM/source BPM multiplies playback speed,
alongside note/root tuning, so pitch and duration change together. It is not
pitch-preserving stretch, automatic project-tempo detection, source-file editing
or Audio Clip fitting. Preserve this limitation when explaining results.

## Cancellation, ownership and errors

`cancel` accepts a pending analysis or completed unapplied confirmation. Poll
until `cancelled`; cancellation never applies settings. A worker may have
finished before the cancel request arrives, so do not require observing a pending
phase. Already-applied sessions refuse further apply/cancel with
`samplerTempo.alreadyApplied`; use ordinary Undo. Tokens are session-scoped,
bounded to 32 retained records, and finished records may expire. Unknown/expired
tokens return `samplerTempo.unknownOperation` and must not be guessed or retried
as mutations.

Other structured refusals include `invalid_arguments` for malformed/exclusive
fields, `stale_project`, `samplerTempo.stale`, `.pending`, `.cancelled`, `.range`,
`.unavailable`, `.busy` and `.applyFailed`. A failed analysis is not a failed
document edit. After a completed failed estimate, explicitly applying `sourceBpm`
on that unchanged, unapplied session can recover manually; a successful apply
has no failure error. Alternatively capture a new confirmation.
`samplerTempo.analysisFailed` reports worker/submission failure. Never silently
substitute a guessed tempo.

After apply, inspect current `state`, canonical sampler settings and
`audioUpdatePending`. Document completion is separate from audio publication.
Verify save/open and actual playback or offline export for the intended music.
Original-WAV/semantic tests do not establish hardware, native mouse input,
listening, real-loop detection accuracy or full reference-application parity.

## Keep pitch (time stretch, project42)

`channel.sampler.stretch` (`enabled`) changes how Fit to tempo works. Off (default)
resamples: playback speeds up or slows down and the pitch moves. On keeps pitch:
the application prepares a time-stretched copy of the source (bundled MIT
Signalsmith Stretch) for the current project-tempo/sample-tempo ratio, and plays it
at the normal rate. A tempo change prepares a new copy (brief structural
re-preparation). It has no effect while Fit is off. Read back `stretch` in Channel
sampler state. Very short sources the stretcher cannot analyse fall back to
resampling the length. Quality is the stretcher default preset; no listening
qualification is claimed.

## Time stretching (PITCH / MUL / TIME / Mode)

The Sample page Time stretching section mirrors the familiar layout. Commands take
`args {channel, value}` and are one Undo each; read back in `vibera_state` channels[].sampler.

- `channel.sampler.stretch` (`enabled`): Mode. false = Resample (length and pitch change together),
  true = Stretch (the length changes, the pitch stays).
- `channel.sampler.stretchPitch` (cents, -1200..1200): Stretch transposes without changing the
  length; Resample detunes in real time like playback speed.
- `channel.sampler.timeMul` (0.25..4): multiplies the length.
- `channel.sampler.timeBeats` (0..256 quarter-note beats, 0 = off): sets the length at the project
  tempo (MUL still multiplies it). Without TIME, tempo fit (`fitTempo`) sets the length.

Any length change, or PITCH in Stretch mode, plays a prepared copy of the source (the file and
shared PCM are unchanged); while it is prepared the Channel reports the stretch as pending.
