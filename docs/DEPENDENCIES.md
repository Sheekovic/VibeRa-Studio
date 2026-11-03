# Dependency and attribution register



| Component | Pin | Selected license | Acquisition/verification |
| --- | --- | --- | --- |
| VibeRa-owned code | repository commit | AGPL-3.0-or-later | Original code/assets |
| Tracktion Engine | 4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86 | GPL-3.0-or-later | Archive SHA256 verified; actual LICENSE.md retained; native Windows build passed |
| JUCE | 37c894f83d379179b2070d437ccd0f1cd9af9576 | AGPL-3.0-only | Exact gitlink verified through pinned Git tree; archive SHA256 verified; actual LICENSE.md retained |
| VSCO 2 Community Edition recorded subset | 6dd651d55dde97fd4028699be9d4481f26917891 | CC0-1.0 | Original pinned WAV/SFZ identities and full license retained under assets/synthesizer |
| VCSL recorded subset | dfcf4a4918771eee884b96ad4493de82ef84daf6 | CC0-1.0 | Pinned SFZ branch declares CC0; full legal text separately pinned to c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e |
| FreePats Electric Guitar FSBS Clean subset | 192cf0d9bf2c4ba6ead8e3524ba3f78818e4fe91 | CC0-1.0 | Original small-bank FLACs, SFZ loops, README and full license retained |

Audit every enabled transitive dependency and embedded notice before distribution. Retain actual license texts. Disable unnecessary optional integrations. WASAPI first; no redistributable driver bundles, proprietary plugins, FL samples or artwork.

The development Synthesizer uses these three free recorded collections alongside
original VibeRa synthesis/preset code. Its 20 maps/123 recordings total about
170 MiB; every original WAV/FLAC has pinned source identity and SHA256 metadata.
Courtesy credits and source-specific limits are recorded in the source document
`docs/SYNTHESIZER_SOUND_SOURCES.md`; the installed copy is
`licenses/SYNTHESIZER_SOUND_SOURCES.md`. Runtime packaging
includes the original maps/notices/licenses under `Content/Synthesizer/sources`
and a source notice under `licenses/`. CC0 recordings do not change the selected
application/dependency code licenses. Curation/asset auditing uses developer
Python/NumPy/ffmpeg; playback uses the existing JUCE WAV/FLAC decoders and needs
none of those tools. Initial content checks do not qualify the full application,
module or exact package; the complete source checklist
`docs/modules/SYNTHESIZER.md` remains required. No commercial library edition,
proprietary plugin or FL asset is added.

## Unreleased native editor attachment adapter

The same pinned JUCE GUI wrapper also has an original native-attachment status
bridge in `patches/juce-vst3-editor`. It exposes only the actual SDK `attached`
result to its statically paired Worker, preserving JUCE's editor-deletion owner.
`scripts/apply-vst3-editor-patch.ps1` checks pin/payload/target/pre-postimage and
reparse boundaries before changing the one known GUI source. Nine isolated
applier cases passed, including wrong-postimage and junction refusal; source/
native/provider qualification is separate. The raw CRLF payload is `-text` in
.gitattributes to retain its exact pinned application hash across checkout.
No dependency revision, distribution license or proprietary asset changed.

## Unreleased retained compensation adapter

`patches/tracktion-retained-history` follows the sidechain/cache adapter on the
same pinned Tracktion revision. Twelve exact vendor changes implement opt-in
bounded audio history; a tiny bridge includes original `src/CompensationHistory.h`
through VibeRaCore's public src include path. The guarded manifest verifies exact
vendor bytes and LF-normalized original source content. Production opt-in remains
off pending live/MIDI/admission qualification. Include this bundle, guard script,
authoritative source and unchanged dependency source/notices with corresponding
source. No download, foundational replacement or license selection changed.

## Development0.0.63 Mixer send adapter

Unreleased Mixer module also applies `patches/tracktion-sidechain-bus`: an optional
graph receiver bus override (empty by default) plus a SummingNode property cache
guard during preparation. Its three-file original/patched identities,
allowed paths, idempotence and unknown-byte refusal are checked by
`scripts/apply-sidechain-bus-patch.ps1`, included by the normal patch entry point.
The existing Tracktion revision and GPL notices remain unchanged. Include this
manifest, patch/application script and original corresponding dependency source
at the eventual module distribution boundary. No new dependency/license selection.

`patches/tracktion-mixer-sends` retains the pinned Tracktion revision/license and
adds a virtual realtime send-gain hook in two files. VibeRa reads immutable
prepared levels instead of mutable engine parameters. The strict application
script verifies allowed paths plus original/patched SHA256 hashes and refuses
unknown vendor bytes. Corresponding source must include this patch, its manifest
and `scripts/apply-mixer-send-patch.ps1` alongside the unchanged dependency source
and GPL notices. No dependency download or distribution-license change.

Public release must include equivalent access to complete corresponding source, vendored dependency sources, patches and build scripts, plus third-party notices. Dependency URLs alone are not a complete source-distribution package.

## Enabled embedded dependencies
Source notices are retained under licenses/third-party. Tracktion: CHOC (ISC), crill (BSL-1.0), concurrentqueue/lightweight semaphore (BSD/BSL options in embedded headers), farbot/rigtorp/magic_enum (MIT), rpmalloc (public domain), expected (CC0), libsamplerate (BSD-2-Clause). doctest is vendored but upstream tests are not enabled in this preview. JUCE enabled modules include FLAC/Ogg Vorbis (BSD notices), JPEG (IJG), PNG/zlib (zlib), HarfBuzz (MIT-style) and SheenBidi (Apache-2.0).

VST3 hosting is enabled only in the isolated Windows x64 plugin worker. The main process does not load plugin DLLs. The SDK bundled in the pinned JUCE source has a 2025 Steinberg MIT license; its actual text is retained as licenses/third-party/VST3-SDK-LICENSE.txt and its source is included in the JUCE archive. No third-party plugin binary, preset or license is redistributed. VST2/AU/LADSPA, ASIO, ARA, ReWire, Automap, video, Cmajor and stretching integrations remain disabled. Full public-distribution license/SBOM qualification remains an M6 gate.

Source archives and hashes for code dependencies are in dependencies.lock.json;
original generated sampler sounds are in src/Sampler.h. The separate recorded
Synthesizer library has its source pins/hashes in assets/synthesizer/library.json.
All example project content is original. No FL installation files or assets were copied.

Development0.0.27 applies a small original JUCE VST3 metadata/sample-event adapter to the same pinned base revision. The two file patches, original/patched hashes and unchanged AGPL license are recorded in patches/juce-vst3-parameters/manifest.json. Build/fetch invokes scripts/apply-dependency-patches.ps1; unknown vendor edits are preserved and rejected. Corresponding source must include these patches and their application script along with the original dependency archives. The developer-only original parameter VST3 fixture uses the existing bundled SDK; no new dependency download or distribution license change. See [parameter scope and evidence](PLUGIN_PARAMETERS.md).

## IPC6 local adapter update

Development0.0.29 updates the same original two-file JUCE adapter for8192 prepared events, ordered-append/final-queue echoing and program-change rejection. Node capacity includes native cached parameters, bounded mapped MIDI and the full timed-event budget outside DSP. The manifest retains original archive hashes and records new patched hashes; fresh original-archive/Git LF reconstruction passed at build/dense-parameter-patch-reconstruction/result.json. Base revisions/actual license texts and dependencies.lock.json are unchanged. Distribution still requires original corresponding dependency source plus these exact patches/application script; no installed plugin binary or opaque-state project is redistributed.
## Branding used by 0.0.2

The user supplied Reference/Logo.png, the Midnight Motion visual concept and standalone V icon variants. These reference files remain untouched. Their raster transparency and edges were inspected; the V was independently redrawn as two smooth vector ribbons in assets/vibera-symbol.svg. The application embeds this SVG and uses a Segoe UI wordmark. No font binary is bundled; Windows supplies the font. assets/VibeRa.ico contains 16/24/32/48/64/128/256 px variants; JUCE's embedded executable icon contains 16/32/48/256 px variants. Regeneration uses scripts/generate-branding.py and developer-only Pillow; Pillow is not a runtime dependency. The redraw is VibeRa-owned artwork under the repository license. Public release rights/attribution qualification remains part of M6.


## Isolated stretch experiment (not production-enabled)

2026-10-15: the existing GPL Rubber Band research checkout has explicit,
hash-pinned preparation/storage, real-source reserve, stereo-phase and
compile-gated resource-observer patches in the four `rubberband-*.json` manifests
under `experiments/tune`, applied by `scripts/apply-tune-resampler-patch.py`.
Original revision/notices remain; unexpected tracked changes are preserved/refused.
The MIT Signalsmith research copy has a separately pinned phase-frequency-map
experiment (`signalsmith-phase-frequency.json`, `apply-tune-signal-patch.py`);
the engine vendor copy is untouched. These change research source only, not
dependencies.lock.json or an enabled application dependency. Measured provider,
stereo and instrumented resource gates now pass for the latest GPL experiment;
latency gates still fail. Source/patch/notice obligations must accompany any
future enabled distribution. Exact source hashes/results:
[TUNE_CORRECTION_EXPERIMENT.md](TUNE_CORRECTION_EXPERIMENT.md).

Tune correction additionally compares public Signalsmith revisiona670068d9aeb64913331d5cc29337b19a457a7df/Linear0.6.4 de55e6a50ffcf6f8f43f649692d94691c7025151 (MIT) and Rubber Band4.0.0 revision1d95888bec3ae0a17c0c4af791810d5a63f6bc35 (actual README/header GPL2-or-later, GPL route; COPYING retained in licenses/third-party/RubberBand-COPYING.txt). These remain isolated build-directory candidates fetched by scripts/fetch-tune-candidates.ps1, not enabled DAW dependencies. [Correction evidence/reproduction](TUNE_CORRECTION_EXPERIMENT.md) records failed accuracy/allocation/stereo gates and limits. Production dependencies.lock.json and application licensing remain unchanged.

experiments/stretch independently exercises the Signalsmith Stretch/Linear copies already pinned inside Tracktion4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86. Both actual MIT texts are retained in licenses/third-party/Signalsmith-Stretch-LICENSE.txt and Signalsmith-Linear-LICENSE.txt. The current application compile flags remain unchanged; these libraries are not enabled by preview0.0.20. Source/header hashes, measured limits, primary documentation and exact reproduction commands are in STRETCH_EXPERIMENT.md. Enabling a production integration requires the recorded acceptance work, not another material license/architecture approval; the open-source AGPL product direction is unchanged.

## Unreleased app-owned metering adapter

`patches/tracktion-owned-metering` follows the sidechain adapter on the same
pinned Tracktion source. Two original opt-in conditions omit unused native
track/master meter nodes for app-created Edits with their own atomic meters.
Defaults and preview metering remain unchanged. The strict single-file guard
verifies pin/predecessor/path/reparse/payload/post-image and preserves unknown
bytes; the normal entry point applies it idempotently.18/18 isolated guard
cases and source53/53 passed; see MIXER_BALANCE.md. Include this bundle and
`scripts/apply-owned-metering-patch.ps1` with corresponding source/notices. No
dependency revision, license choice, download or foundation replacement changed.

## Unreleased bounded MIDI graph successor

`patches/tracktion-retained-midi` follows the retained-history family on the same
Tracktion4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86/archive pin. Four original
file patches add explicit default-OFF bounded short-MIDI history and transactional
player rate/block metadata publication. Source content, exact patch bytes,
predecessor and final vendor identities are guarded;26 disposable cases passed.
Original independent code is src/MidiCompensationHistory.h. Include this family
and scripts/apply-retained-midi-patch.ps1 with matching source/notices. No download,
revision, foundational architecture or distribution-license change. Focused
runtime evidence and remaining unsupported crossings/qualification are recorded
in VERIFICATION; the guard alone is not audio qualification.

## Unreleased graph-local routing and selection adapter

`patches/tracktion-detached-graph` and `patches/tracktion-context-ready` modify
only the same pinned GPL Tracktion source. Original opt-in code adds private
same-ID app adapter construction, fixed-owner initialization, immutable native
output routing and typed actual-selected graph receipts. Default native paths
remain available. Exact raw manifest/payload/vendor lineage is enforced by
`scripts/guard-structural-graph.py` and the normal dependency entrypoint; carry
these bundles/scripts and existing notices with corresponding source. Current
18-target contract adds original preowned native send-gain state and a throwing
CONTROL-only preparation hook. Normal native sends retain their default behavior;
VibeRa sends opt into five-ms ramps independent of the prepared maximum block.
Exact old endpoints remain rejected where superseded. Source guard40/40 and
actual native/application send PCM passed; native Wave routing also passed its
bounded same-Edit check. Production PDC and complete Mixer integration remain
OFF/incomplete. Include all corresponding source/payloads and existing notices.
No revision, download, foundation or distribution-license change. Scope and
retained failures: VERIFICATION. The permanent distribution remains unchanged.

## Unreleased CONTROL device preparation fence

`patches/tracktion-device-fence` modifies two DeviceManager files on the same
pinned GPL Tracktion revision. Original optional owner-verified CONTROL callbacks
invalidate external prepared graph leases before device teardown. Effective live
overlays refuse before teardown; configuration staging, full canonical restore
and explicit exact-owner async retry preserve recovery ownership and truthful
pending/completed status. Normal default paths remain unchanged. Exact raw
source/payload identities and reparse-safe replay are enforced by
`scripts/apply-device-fence-patch.ps1`; carry both files, manifest and helper in
corresponding source with existing notices. Actual synthetic device lifecycle
passes in build/dev/20261017-051030; arbitrary hosted/hardware restoration remains
unqualified. No revision, dependency download or distribution-license change.


## Unreleased bounded Summing property reuse

`patches/tracktion-summing-properties-reuse` is an original correction to the
same pinned GPL Tracktion source: reuse already obtained properties for the same
logical input, retaining separate wrapper lookup. The strict structural guard
requires33targets/45stages with exact pinned predecessors; include payload,
manifest, helpers and existing notices with corresponding source. Unlike the
experimental PDC/source opt-ins, this correction applies wherever compiled.
Original old-code bounded counter fails; corrected runtime129-bus Pattern/Song
cases and reproducible17-case source guard pass unchanged. No vendor revision,
download or distribution-license change. This does not qualify complete Mixer
or route-wide compensation; exact development evidence remains in VERIFICATION.


The existing pinned JUCE prepared-history family now supports a preallocated
exact one-transaction inverse replacement with cached history cost checks.
Same vendor revision/license, payload manifest and notices; no overlapping
patch family. Manager/Controller focused acceptance passes; actual native dirty
plugin inverse coordination remains unqualified.

`patches/tracktion-stage-difference` adds immutable typed refusal metadata only,
after the pinned stable-bus-stage successor. Same Tracktion revision/GPL notices.
The strict33target/45stage closure verifies every predecessor/payload/final;
actual native FIFO refusal/reuse acceptance passes. No live queue introspection,
DSP transition admission, default enabling or qualified joint compensation is
implied. Carry payload, manifest and strict helpers with corresponding source.

Development adds `patches/tracktion-forward-stage-drain` (two exact native
headers) and `patches/tracktion-native-phase-probe` (five existing native targets),
same pinned Tracktion revision and GPL-compatible distribution obligations.
Combined strict closure is34targets/52stages. Official applier verified all
inputs and applied seven pending files; exact metadata and source receipts are
in build/mixer-native-combined-guard-draft/FREEZE.json. C++/runtime qualification
is pending. The one-shot drain requires actual DSP predecessor clock/history
coverage, complete audio-only stage inventory and one processing owner; it does
not admit current MIDI-capable application graphs or solve general/inverse PDC.
Scalar witness bookkeeping leaves ordinary audio arithmetic intact but adds
work; no timing benefit is claimed. Phase capture is default-null, fixed
caller-owned storage for a quiescent synthetic diagnostic only. No arbitrary
physical-device installation/concurrent-reader safety is promised. These are
explicit internal experiments, not production activation or new dependency pins.


Development adds `patches/tracktion-send-gain-block`: five exact existing native
successors and one original optional contract header on the same pinned Tracktion
revision. Existing native portions retain their GPL notices; the new independently
authored header carries AGPL-3.0-or-later as project source. Include all exact
payloads, manifest and strict adapter with corresponding source. The provider is
default-disabled and no canonical send automation target, app UI/MCP/persistence,
actual retained adoption or application WAV export is qualified by this internal
native-clock experiment. Ordinary no-provider behavior is preserved in source;
its regression and all experimental C++/runtime checks remain separate evidence.
No dependency revision, download or distribution-license change.


`patches/tracktion-node-prefix-clear` adds one exact Node.h successor after its
retained-history/MIDI/stable-stage chain. This original GPL native correction
clears only the current visible prefix of owned prepared storage; borrowed views,
MIDI, topology and musical/timing oracles remain unchanged. It applies universally
where compiled: productionEnabled:false is experimental metadata, not a switch
disabling this optimization. Preserve payload, manifest and strict helpers with
corresponding source. Combined closure35targets/59stages. No measured speedup or
runtime qualification follows from source replay; retain comparable baseline and
post-change callback/PCM evidence separately.

