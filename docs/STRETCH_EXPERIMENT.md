# Pitch-preserving stretch experiment (2026-10-13)

This is isolated development research after packaged preview0.0.20. It does not enable a stretch mode in VibeRa. The released preview supports explicitly labeled Resample fitting only.

## Candidate and provenance

Pinned Tracktion engine4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86 already vendors Signalsmith Stretch and Signalsmith Linear. Actual license texts were read and retained as licenses/third-party/Signalsmith-Stretch-LICENSE.txt and Signalsmith-Linear-LICENSE.txt, both MIT. Main engine defaults for SoundTouch/Rubber Band/Signalsmith are0; no production compile flag changed. The SoundTouch copy advertises2.1pre and LGPL2.1-or-later; its wrapper/source was inspected, but it was not enabled. Prefer testing the maintained Signalsmith candidate with independently implemented VibeRa integration rather than assuming a wrapper guarantees FL behavior.

Primary sources: [Signalsmith maintained repository](https://github.com/Signalsmith-Audio/signalsmith-stretch), [SoundTouch license and actual text](https://soundtouch.surina.net/license.html), [SoundTouch algorithm/latency explanation](https://soundtouch.surina.net/README.html). Runtime behavior below is measured from the pinned vendored code, not inferred from current upstream documentation. No proprietary code, algorithms, samples or assets were copied.

Header SHA2561188667959ac19dd40c0a6abbce694e44705615ec4f0e8db8af0f1cfb4c5dea7; Linear STFT header4e11e85e823aaf2ec9bd5bd6bd55e6665126de1de2604af84574009ed1f3b5f2. Exact pinned archive remains in dependencies.lock.json. Isolated CMake/MSVC x64 Release build under build/stretch-experiment. No new download, installation or runtime dependency in preview0.0.20.

## Performed checks

Run powershell -ExecutionPolicy Bypass -File scripts/verify-stretch-experiment.ps1. It checks the pinned header, configures/builds the isolated console executable, runs the experiment and writes provenance. Latest script exit0. Logs: build/stretch-configure.log, stretch-build.log, stretch-test.log, stretch-streaming-script.log; measurements/provenance: build/stretch-experiment/verification.txt and provenance.json.

Ten offline stereo cases use original four-second220/330Hz tones, tempo ratios0.5/0.83333/1/1.2/2 and pitch ratios1/2. Central output pitch tolerance4Hz, finite bounded PCM and explicit output frame lengths passed. Preparation about17-73ms in the initial run, excluding any actual DAW/hardware path. The default algorithm exposes2880input+2880output latency frames at48kHz (120ms total). Exact offline helper handles its own alignment/flush; real-time integration needs deliberate latency/pre-roll handling. Short128-frame sources return failure and zero the requested buffer. Never publish that as a successful stretch or silently lose a one-shot.

Prepared streaming calls with split computation, varying input batch lengths and live transpose changes were measured over512blocks at256outputframes/48kHz:
- 1 stereo voice: max0.0887ms, mean0.04646ms,0overruns.
- 16 stereo voices: max1.548ms, mean0.76212ms,0overruns.
- 64 stereo voices: max6.5102ms, mean3.35804ms,22overruns against5.333ms.

Ordinary global C++ new/new[] hooks counted zero allocations during those prepared processing calls. This is not a complete malloc/aligned-allocation/lock/destruction audit or proof of DAW callback safety. Synthetic streaming input/last-voice finite-output checks are not musical quality, voice mixing, realtime note alignment, hardware or listening evidence. Experiment exit0 means its defined DSP/property/allocation checks passed; it explicitly reports the failed64voice timing benchmark rather than hiding it or raising the buffer.

## Next ready experiments and integration boundaries

1. Compare mono/stereo, cheaper/manual window configurations, source onset/impulse alignment and pitch preservation; expose quality/latency costs before choosing a production preset. Keep64voice overload evidence and test typical chords/multiple Channels through the actual engine.
2. Audit enabled call paths for allocation, blocking, lifetime and reset; prepare every DSP/resource outside callbacks. Do not infer safety from the allocation hook alone.
3. Evaluate worker-prepared PCM versus prepared streaming voices against the full intended behavior: independent pitch/duration for chord voices, moving/looping notes, tempo changes, reverse/source range, ADSR, preview/MIDI ownership and export. A tempo-only transformed buffer with pitch-dependent note duration is not complete FL Stretch parity.
4. Implement a stable canonical mode/persistence migration only after the preferred path is tested. Keep Resample as the familiar supported default; expose any fallback/unsupported source clearly. Do not impose a silent new polyphony cap, block on workers, increase the device buffer, mute edits or rebuild the entire graph.
5. Validate live Pattern/Song/chords/rapid edit/undo/loop/offline/project cases, then native interaction and user listening. No prototype result marks the stretch feature complete.


## Expanded candidate matrix and failed results

The follow-up tests default/cheaper and manually configured1024/2048/4096-frame windows, with mono/stereo512-block streaming banks and separate-mono octave processing. An independent Hann-windowed32768-point spectral dominant-frequency estimate supplements zero crossings; the current musical pitch gate is5cents. Failed cases are labeled FAIL individually and the experiment exits1 after collecting all comparisons, rather than stopping before other candidates are measured.

Latest matrix:50stereo cases/100channel pitch checks,25failed pitch checks, no missing impulse markers. Independent mono at requested440Hz measured default440.032Hz (0.127cents), cheaper443.967Hz (15.54cents), window1024:452.784Hz (49.58cents), window2048:446.18Hz (24.15cents), window4096:442.427Hz (9.52cents). Thus faster configurations have actual measured tuning errors; the separate-mono result does not support attributing those errors solely to stereo channel coupling. No root cause in the library is proven.

Ten synthetic eight-impulse cases recorded peak-placement errors under1ms; this does not establish percussion/vocal musical quality. Latest default64stereo streaming: maximum9.1911ms/28overruns versus5.333ms, mean3.44336ms. Faster modes improve timing but fail pitch; no candidate has passed all intended gates. Initial and intermediate failures remain in build/stretch-experiment/verification-initial.txt, verification-four-presets-failure.txt and verification-five-presets-failure.txt with matching retained provenance. Latest verification.txt/provenance.json record the actual failed run. The script now retains a specific process handle/PID/creation and records exit even when stderr reports failure, avoiding stale successful provenance.

Keep production stretch disabled. Next compare verified API/pitch mapping against current primary upstream material and suitable alternatives, then test actual-engine latency/resource ownership and musical sources; do not weaken tuning criteria or adopt the fastest incorrect preset.
