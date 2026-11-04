# Native VibeRa effects

Built-in effects are individually insertable Mixer processors. Each instance has
one fixed effect type and only its relevant parameters. No installation or scan
is required. Discover availability in the running app; the qualified distribution
may predate this unreleased integration.

1. Call `vibera_effects` with `action:list` for available types and native parameter
   units/ranges/defaults. Request `action:descriptor` with the exact discovered
   `type` for its opaque descriptor.
2. Use canonical `plugin.effect.insert` with the descriptor in `text` and an
   existing `mixer` index. Observe actual completion and worker readiness. Every
   insertion creates an independent ordered slot; add more types separately.
   UI: Mixer Add effect, choose the individual VibeRa effect.
3. Use `vibera_plugin` list, refresh and parameters for the exact runtime ID,
   instance and generation. Supported scalar edits take normalized values;
   normalization can be skewed. Poll set/refresh jobs until terminal and explicitly
   refresh for current readback. Admission alone is not applied state.
4. Use shared slot bypass, wet mix, reorder, replacement, removal, Undo/Redo and
   real project save/reopen/export. Editor controls and MCP use the same hosted
   parameters. Cancellation/errors retain existing job semantics.

External third-party VST3 discovery remains `vibera_catalogue`. Do not scan for
built-ins or select an Effect mode parameter. Stereo is the native layout.
Tails are transient, not saved audio. Peak Limiter is sample-peak/zero lookahead;
nonlinear effects are not oversampled. Full85-reference scope, FL parity,
listening, hardware and release qualification remain separate requirements.
See docs/modules/VIBERA_EFFECTS.json for full scope and actual evidence.

## Editor and measured output (development source)

Native editors use vertical rotary drags, or individual band/output faders for
Seven Band EQ. Readable values use the declared native units; double-click resets
to the discovered default. Host automation updates the same controls. The
stereo bars show actual latest processed-block sample peaks, without an invented
EQ response, gain-reduction meter or physical-device freshness claim.

`vibera_plugin action=list` exposes each row's typed `nativeOutputMeter`.
`status=ready` supplies finite nonnegative leftPeak/rightPeak in linear sample
peak units, before Host wet/dry/bypass. Zero is measured silence; unprocessed,
invalid, unavailable or unsupported readings have available=false and null
peaks. Stopped readings may retain the last processed block. CONTROL samples the
two existing native peak atomics; this is not packet-coherent timing evidence.
Third-party plugins report unsupported here; use their supported interfaces and
keep insert/Master meters distinct from per-effect output. Discover capabilities
from the running executable; current source is not a qualified distribution.

VibeRa Tune (type `tune`): pitch correction. Parameters: key 0..11 (C..B), scale 0..14
(Chromatic, Major, Minor, Harmonic minor, Minor pentatonic, Rast, Bayati, Saba, Sikah,
Huzam, Hijaz, Nahawand, Kurd, Nawa Athar, Jiharkah; maqamat use quarter tones; Sikah/Huzam
tonic is a half flat below the key), retune 0..400 ms (0 = hard robotic tune), amount, mix,
humanize 0..1 (slower correction on held notes), flex 0..100 cents (0 off; corrects only near
a degree, leaving slides alone), formants 0/1 (keep the voice character, default on) and
transpose -12..12 semitones. Shifting uses the vendored Signalsmith Stretch; the fixed delay
(about 110 ms at 48 kHz) is reported as plugin latency and compensated by the host.
Monophonic sources only.

Mixing and mastering processors:
- Parametric EQ (`parametric-eq`): low cut (20 Hz = off), low shelf frequency/gain, two
  bells (frequency, gain, Q), high shelf frequency/gain, high cut (20 kHz = off). The editor
  draws the exact response curve.
- De-esser (`de-esser`): frequency, threshold, range (max reduction dB), listen (hear only
  what is removed). Turns down only the band above the frequency while it is loud.
- Transient Shaper (`transient-shaper`): attack and sustain -100..100 %, output, mix.
- Multiband Dynamics (`multiband`): low/high crossover, depth, time, upward, downward
  (OTT-style three-band compression), low/mid/high gain, output. Depth 0 is transparent.
- Saturator (`saturator`): drive, character (tape to tube), tone tilt, mix, output.
- Maximizer (`maximizer`): gain, ceiling, release; 1.5 ms lookahead (reported latency),
  the output never exceeds the ceiling. De-esser, Multiband and Maximizer show gain reduction.
- Studio Reverb (`studio-reverb`): type (0 Room, 1 Plate, 2 Hall), size, decay (RT60
  seconds), pre-delay, damping, diffusion, modulation, low/high cut of the tail, width, mix.
  An 8-line feedback delay network; the measured decay follows the setting.
- Spectrum Analyzer (`spectrum-analyzer`): passes audio unchanged; its editor draws a live
  4096-point spectrum (20 Hz..20 kHz) with display slope (dB/oct) and falloff.
- Beat Gate (`beat-gate`): tempo-synced gate, stutter and half-time. pattern (0 Open, 1 Pulse,
  2 Offbeat, 3 Dotted, 4 Chop, 5 Broken, 6 Half bar, 7 Build; 16 steps), step (0 1/32, 1 1/16,
  2 1/8, 3 1/4), gate depth, smoothing (ms), stutter (0 Off, 1 1/4 .. 4 1/32; repeats the first
  slice of each hold segment), hold (0 1/2, 1 1, 2 2 beats), half-time (on/off; plays each cycle at
  half speed, one octave down), cycle (0 1, 1 2, 2 4 beats), mix. Follows the song beat while the
  transport plays; when stopped it keeps its own clock at the last tempo.

Vocoder (type `vocoder`): 16-band channel vocoder. The input (voice) shapes an internal
carrier: a band-limited saw at `note` (MIDI 24..96) mixed with `noise` (consonants);
`release` sets band decay, plus mix and output gain. There is no external carrier input.
