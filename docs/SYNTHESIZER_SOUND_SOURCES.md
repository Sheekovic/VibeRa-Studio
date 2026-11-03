# Synthesizer recorded sound sources

VibeRa's recorded starter library uses original WAV/FLAC samples from three
public CC0 collections. The synthesizer's oscillator presets are independently
implemented synthesis. This document describes the recorded assets; their
presence does not establish engine, preset, listening or release qualification.

The curated manifest is [assets/synthesizer/library.json](../assets/synthesizer/library.json).
It declares 20 instruments and 123 original files (110 WAV, 13 FLAC),
178,426,339 bytes (about 170 MiB).
No proprietary reference installation, commercial edition, test rendering or
generated tone was used as an acoustic recording source.

The installed copy is `Content/Synthesizer/library.json` beside the executable,
with `samples/` and `sources/` below the same directory. The source manifest's
`pathBase` is provenance metadata; the runtime does not search the checkout.
The packager installs this notice under `licenses/` and rewrites the relative
runtime manifest/license links below to `../Content/Synthesizer/`. Engineering
scripts and acquisition/validation receipts mentioned as source paths are not
included in the runtime content whitelist.

## Primary sources and licenses

| Source | Pinned source revision | License and retained notice |
| --- | --- | --- |
|[VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE)|`6dd651d55dde97fd4028699be9d4481f26917891` (SFZ branch)|CC0-1.0; [license](../assets/synthesizer/sources/VSCO-2-CE-LICENSE), [README](../assets/synthesizer/sources/VSCO-2-CE-README.md), [original terms/credits](../assets/synthesizer/sources/VSCO-2-CE-Readme.txt)|
|[Versilian Community Sample Library](https://github.com/sgossner/VCSL)|`dfcf4a4918771eee884b96ad4493de82ef84daf6` (sfz branch)|CC0-1.0; [license](../assets/synthesizer/sources/VCSL-LICENSE), [README](../assets/synthesizer/sources/VCSL-README.md)|
|[FreePats Electric Guitar FSBS Clean](https://github.com/freepats/electric-guitar-FSBS-clean)|`192cf0d9bf2c4ba6ead8e3524ba3f78818e4fe91`|CC0-1.0; [license](../assets/synthesizer/sources/electric-guitar-FSBS-clean-LICENSE.txt), [README](../assets/synthesizer/sources/electric-guitar-FSBS-clean-README.txt)|

The VSCO pinned branch includes the full CC0 legal text. VCSL's pinned sfz
README declares CC0; its full legal text is retained from the same publisher
repository's master revision `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e`,
recorded separately as `licenseRevision` in the manifest. The publisher's
[VSCO Community page](https://versilian-studios.com/vsco-community/) identifies
the original WAV collection as CC0 and permits using and adapting its samples.
The publisher's [VCSL page](https://versilian-studios.com/vcsl/) explicitly
describes integrating the collection into DAWs, samplers and other software.
Those are the primary-source bases for redistribution compatibility. VSCO's
older Readme.txt also requests courtesy credit and asks against direct sample
resale; its CC0 license and the publisher's explicit CC0 statement are retained
alongside that wording. VibeRa supplies these recordings inside a free DAW.

Courtesy credits: Sam Gossner / Versilian Studios; Simon Dalzell / Ivy Audio;
VSCO sample cutting by Elan Hickler / Soundemote; VCSL contributors as identified
in the retained upstream README. These names identify recording provenance;
they do not imply endorsement. The Community sample collections are distinct
from VSCO Standard/Professional commercial products, which are excluded.
The FreePats guitar's own README and full license dedicate its recordings to
CC0; FreePats contributors are credited as a courtesy. Its source describes a
directly recorded electric guitar processed through an amplifier/effects rack
for a clean tone. The original small-bank FLAC files are retained unchanged.

## Curated contents

| IDs | Recorded content | Selection |
| --- | --- | --- |
|`piano-upright`, `violin-solo`|Upright piano; solo violin arco vibrato|Six recorded key centres each, two recorded dynamic layers each|
|`contrabass-pizz`, `cello-ensemble`, `harp`|Pizzicato acoustic contrabass; cello ensemble vibrato; harp|One recorded dynamic per selected key centre|
|`flute`, `clarinet`|Flute non-vibrato sustain; clarinet sustain|Five selected key centres each|
|`trumpet-staccato`, `horn`, `trombone`, `tuba`|Staccato trumpet; sustained French horn, tenor trombone and tuba|Four to six key centres each|
|`marimba`|Struck marimba|Six selected key centres|
|`strumstick`|Finger-plucked three-string acoustic strumstick|Six key centres; this is not a six-string guitar|
|`guitar-clean`|Clean six-string electric guitar|Thirteen source small-bank key centres; ten explicit authored sustain loops|
|`electric-piano-fm`|Recorded Yamaha TX81Z FM electric-piano keyboard timbre|Six key centres, two recorded dynamics; not an electromechanical Rhodes/Wurlitzer or acoustic piano|
|`kalimba`, `balafon`|Tanzanian kalimba; traditional-mallet balafon|Six key centres each|
|`conga`, `bass-drum`, `snare-drum`|Conga/quinto/tumba hits; concert bass drum; normal snare, roll and stick sounds|Separate root-note-only percussion zones; not a General MIDI drum kit|

This is a small starter selection, not the complete upstream libraries. Missing
scope includes steel/nylon-string acoustic guitar, electromechanical electric
piano, comprehensive articulations, close key spacing, complete acoustic ranges,
extra recorded dynamics, round robins, natural release
triggers, pedal behavior and physical modeling. One-shot and finite sustained
recordings can finish while a note remains held. No sustain loop is invented.

## Machine-readable map contract

The JSON has `schemaVersion:1`, source IDs/pinned revisions/license paths/hashes,
and instruments with stable IDs, names, families and `regions`. Paths in
`region.sample` are relative to `assets/synthesizer`. Each region records:

- MIDI `rootNote`, inclusive `lowNote`/`highNote` and inclusive
  `lowVelocity`/`highVelocity` in 0..127. MIDI 60 is middle C. Roots come directly
  from upstream SFZ `pitch_keycenter`; filename octave conventions differ
  between instruments and are not used as a substitute.
- SHA256, pinned upstream Git blob SHA1, byte size, source ID/path, original
  SFZ map/hash, exact sample rate/channel/frame/bit-depth information and
  original source velocity range. WAV/FLAC bytes are unchanged.
- `sourceOffsetFrames`, `tuneCents`, `gainDb`, normalized `pan` and explicit
  `loop:{mode,startFrame,endFrame}`. Ten guitar regions use the source author's
  explicit loops; all other regions use `none`. Loop ends are exclusive after
  converting the [inclusive SFZ loop_end](https://sfzformat.com/opcodes/loop_end/)
  by adding one. Embedded WAV sampler metadata is retained as evidence when
  present, without enabling an undocumented loop.

The loader must decode and prepare PCM off the audio callback. It applies the
source offset while preparing the buffers and applies tuning in cents to the
playback ratio, for example through sampleRate times `2^(tuneCents/1200)`.
The recorded source sample rate stays separately available in the manifest.

VibeRa's curated key zones use nearest selected roots and a four-semitone outer
extension. Percussion zones stay on their declared roots. Single selected
dynamic recordings span the velocity range; piano, violin and FM keyboard keep
two recorded layers. Source per-region gains receive 12 dB additional headroom.
The map retains the original velocity ranges and source maps, making these changes explicit.
The application provides its own envelopes and synthesis processing; this is
not an SFZ player or a claim of upstream SFZ playback equivalence.

## Developer acquisition and verification

The source-only `assets/synthesizer/curate_library.py` reproduces the selection
from immutable upstream revisions. Running it without arguments
prints the planned counts/size; `--fetch` acquires only the declared files and
retains the source maps/notices. It performs at most four concurrent public
downloads. The substantial download was announced before acquisition.

Every WAV/FLAC is checked against its source Git blob SHA1 and byte size before
retention; SHA256 and source audio layout are then recorded. Invalid/unknown
existing bytes are rejected. Positive mono/stereo frame counts and source
offset bounds are required. The developer acquisition receipt is
`assets/synthesizer/acquisition.json` in the source workspace.
Initial HTTP timeouts, a later connection reset and the missing sfz-branch
license-text lookup are retained in three acquisition-attempt receipts;
all 123 audio files passed the final complete identity check. The final short
receipt duration describes the cached-byte
recheck/notice refresh, not the total network acquisition.

The historical source-only receipt `assets/synthesizer/asset-validation.json`
checks every identity, source-map/license hash, offset and note/velocity zone.
The source-only `assets/synthesizer/validate_library.py` independently parses WAV
data and decodes every FLAC through the developer's installed ffmpeg, checking
decoded frame counts. Its `--library` option accepts a source or installed
library directory/manifest; `--output` defaults to the owned
`build/windows/synthesizer-content/verification.json`. Qualification never writes
back into the input library. Missing NumPy/ffmpeg produces an explicit environment
failure. These engineering dependencies are not added to the application runtime.
All melodic maps have contiguous nonoverlapping coverage within their declared
ranges. Percussion keeps its sparse explicit key assignments. The complete
library contains 42,516,052 source frames (340,128,416 bytes as an upper bound
for stereo float PCM before offset trimming). The ten authored guitar loop
bounds and source-to-map endpoint conversions pass. The largest single-region
peak after mapped gain is 0.403644. This is a scalar PCM measurement, not a
polyphonic peak or listening qualification.

These checks establish acquisition identity and parseable sample/map metadata.
They do not establish musical suitability, artifact-free transposition, heard
sound, complete source-library behavior or production readiness. The final
Synthesizer integration, state/history/MCP/GUI/export qualification and package
provenance remain separately required.

## Factory patch catalogue

The native factory catalogue has56 patches:20 recorded-source, one hybrid,
and35 synthesized. Genre tags reuse the same patch; they are not duplicate banks.
The source labels below describe sound generation, not listening qualification.

| Preset | Family | Source | Genres |
| --- | --- | --- | --- |
|Velvet Upright|Piano|Recorded: piano-upright|Pop, R&B|
|Solo Violin|Violin|Recorded: violin-solo|Cinematic, Electronic|
|Pizzicato Contrabass|Bass|Recorded: contrabass-pizz|Jazz, R&B|
|Cello Ensemble|Strings|Recorded: cello-ensemble|Cinematic, Electronic|
|Clear Harp|Strings|Recorded: harp|Pop, R&B|
|Air Flute|Woodwinds|Recorded: flute|Arabic|
|Studio Clarinet|Woodwinds|Recorded: clarinet|Jazz, R&B|
|Trumpet Staccato|Brass|Recorded: trumpet-staccato|Funk, Hip-Hop|
|Horizon Horn|Brass|Recorded: horn|Cinematic, Electronic|
|Warm Trombone|Brass|Recorded: trombone|Jazz, R&B|
|Low Brass|Brass|Recorded: tuba|Cinematic, Electronic|
|Wood Marimba|Plucks|Recorded: marimba|Pop, R&B|
|Acoustic Strumstick|Guitar|Recorded: strumstick|Rock, Pop|
|Soft Kalimba|Plucks|Recorded: kalimba|R&B|
|Wood Balafon|Arabic / World|Recorded: balafon|Arabic|
|Studio Conga|Drums / Percussion|Recorded: conga|Funk, Hip-Hop|
|Round Bass Drum|Drums / Percussion|Recorded: bass-drum|Hip-Hop|
|Dry Snare|Drums / Percussion|Recorded: snare-drum|Rock, Pop|
|Crystal Pluck|Plucks|Synthesis|Pop, R&B|
|Air Ensemble|Pads|Synthesis|Pop, R&B|
|Round Bass|Bass|Synthesis|Pop, R&B|
|Neon Chords|Synth Leads|Synthesis|Pop, R&B|
|Glass Electric Keys|Electric Piano|Recorded upright + synthesis: piano-upright|Jazz, R&B|
|Mellow Lead|Synth Leads|Synthesis|Jazz, R&B|
|Velour Bass|Bass|Synthesis|Jazz, R&B|
|Hush Pad|Pads|Synthesis|Jazz, R&B|
|Pocket Square|Bass|Synthesis|Funk, Hip-Hop|
|Violet Stab|Plucks|Synthesis|Funk, Hip-Hop|
|Snap Circuit|Drums / Percussion|Synthesis|Funk, Hip-Hop|
|Elastic Lead|Synth Leads|Synthesis|Funk, Hip-Hop|
|Low Current|Bass|Synthesis|Hip-Hop|
|Dust Keys|Synth Leads|Synthesis|Hip-Hop|
|Haze Ensemble|Pads|Synthesis|Hip-Hop|
|Circuit Click|Drums / Percussion|Synthesis|Hip-Hop|
|Silk Chords|Synth Leads|Synthesis|R&B|
|Sine Ribbon|Synth Leads|Synthesis|R&B|
|Warm Anchor|Bass|Synthesis|R&B|
|Evening Drift|Pads|Synthesis|R&B|
|Prism Lead|Synth Leads|Synthesis|Electronic|
|Glass Sequence|Plucks|Synthesis|Electronic|
|Foundation|Bass|Synthesis|Electronic|
|Digital Snow|Drums / Percussion|Synthesis|Electronic|
|Edge Lead|Synth Leads|Synthesis|Rock, Pop|
|Pulse Organ|Synth Leads|Synthesis|Rock, Pop|
|Copper Bass|Bass|Synthesis|Rock, Pop|
|Backlight|Pads|Synthesis|Rock, Pop|
|Slow Horizon|Pads|Synthesis|Cinematic, Electronic|
|Deep Horizon|Bass|Synthesis|Cinematic, Electronic|
|Airfield|Synth Leads|Synthesis|Cinematic, Electronic|
|Signal Bloom|Plucks|Synthesis|Cinematic, Electronic|
|Amber Pulse|Synth Leads|Synthesis|Arabic|
|Desert Glass|Plucks|Synthesis|Arabic|
|Dune Bass|Bass|Synthesis|Arabic|
|Night Veil|Pads|Synthesis|Arabic|
|Clean Electric Guitar|Guitar|Recorded: guitar-clean|Pop, Rock, Funk|
|FM Electric Keys|Electric Piano|Recorded: electric-piano-fm|Jazz, Pop, R&B|
