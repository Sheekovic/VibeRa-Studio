# Prepared channel and Mixer balance

Unreleased Mixer module work,2026-10-16. This corrects two demonstrated problems:
the native VolumeAndPan stage took a VCA spin lock on every callback, and each
custom insert/Master meter preceded its own pan despite reporting a post-pan tap.
The red own-meter/PCM regression remains in build/dev/20261016-203341.

ChannelBalance now follows each sampler/instrument or audio clip. AutomationGain
owns Mixer/Master pan alongside the existing gain/mute stage, before publishing
the custom meter and before outgoing sends/direct-only Master gate. Targets come
from the same immutable LiveTimeline as the score. No native balance parameter
writes or graph/device resets are required for ordinary gain/pan edits. Native
balance objects remain disabled for engine accessors; unity-enabled is insufficient.

PreparedBalance reuses JUCE smoothing and Tracktion's pure explicit-law helpers.
The control thread resolves the actual law before publication. Existing linear
behavior is L=gain*(1-pan), R=gain*(1+pan), including hard-pan x2 and inclusive
manual centre snap at+-0.005. The15ms coefficient ramps retain joint channel
gain/pan interpolation, mono and extra-channel behavior. Channel gain retains the
old-100dB floor/+6dB ceiling; Mixer gain retains its existing raw0..2 range.
This is old-project audio compatibility, not a claim of FL circular pan parity.

Unchanged live graphs retain existing processing state. True activation or rate/
buffer preparation reads current admitted controls, including edits not yet
consumed by a callback. A ChannelBalance callback retains only scalar targets;
inactive clip tracks cannot pin old documents through this new stage. Bounded
acquisition failure retains its last target and continues PCM. Sample/worker/
document resources are retired outside callbacks.

Unused native track meters are removed at app-created Edit construction. The
strict pinned owned-metering adapter omits remaining native meter nodes only
when that Edit sets viberaOwnMetering before graph creation. Ordinary engine
Edits and preview metering keep their defaults. VibeRa UI/MCP read its own fixed
atomic stereo meter. This removes these specific known locks/first-client
allocation paths, not every possible upstream synchronization hazard.

Source53/53 passed build/dev/20261016-205605 (compile50.459s/tests395.297s).
Independent analytical checks cover laws, conversion limits, every ramp sample,
retargets, channel shape and invalid inputs;256000 changing-target frames use no
callback heap/destruction. Actual Pattern/Song tests cover hard-pan own meters,
Channel pan Undo, mute/solo/history/loop edits, lifecycle/resource identity and
first-sample fresh/rate-reactivation.1500 callbacks each, maxima0.4197/0.4067ms,
zero overruns/heap/destruction/replacements at48k256. Those are virtual PCM checks.

Native Windows gain/pan/history/rename/solo/save passed16.498s at
build/mixer-module-native-pan-meter-fader-raised. Main/rename captures were opened;
the context-menu capture was uninformative. Initial foreground refusal remains
in build/mixer-module-native-pan-meter-fader. The harness now raises only its
owned ordinary window; all input/PID/point/foreground guards remain.
Actual installed Analog Lab V and MVibratoMB branches passed49.526s at
build/mixer-module-provider-graph-publication-host. No listening/hardware or
complete module/provider/parity qualification is implied. Qualified dist0.0.63
remains unchanged; production retained-history opt-in remains off.

## Transient sample-overload increment

Focused5/5 passed at build/dev/20261016-213326 (compile8.650s/tests26.712s).
Actual finite sample peak>=1 latches each side independently; silence, decay and
stale level displays retain it. Clear uses a control-only completed-block fence,
excluding in-flight/new clipping; it never rewrites DSP payload or restarts audio.
True quiesced meter reprepare resets transient history. It is neither true-peak
measurement nor evidence of hardware clipping. GUI meter click/context command
and vibera_mixer action=resetOverload share resetMixerOverload; typed discovery,
structured errors and actual fence/readback require an existing prepared meter.
Reset changes no project, dirty flag, revision, Undo or voice/transport state.

Original tests cover threshold/sign/asymmetry, multiple non-consuming readers,
stale retention, clear/re-latch, lifecycle44.1k reset and1000 heap-free callbacks.
An actual concurrent writer was observed: clearFence1, clipped/completedBlock2,
and the new clip remained latched. Actual sustained Pattern/Song PCM overloaded
left2.615594387, cleared at block31 and re-latched at32.1522callbacks per mode,
maxima0.5029/0.4151ms, zero overrun/heap/destruction/graph-device-cache replacement.
The component executes both MCP and real component mouse reset; stdio tests
verify discovery, invalid arguments and transient state. Fresh populated CLIP
component capture opened; this is not native OS input or listening. Native reset
requires the explicitly owned synthetic-driver hook currently being prepared.

Retained red evidence:212431 discovery returned an empty schema for this custom
non-document mapping;212959 corrected it and passed the semantic workflow.
Variable-waveform probe initially lacked the stopped playback context; its worker
baselines passed, and the setup failure is retained separately. Selecting the
Channel through the existing engine API corrected the fixture. No buffer,
deadline, musical oracle or failure assertion was relaxed.
