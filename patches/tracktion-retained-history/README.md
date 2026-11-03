# Opt-in retained audio compensation adapter

Original twelve-file adaptation against the pinned Tracktion revision. The tiny
vendor bridge includes the authoritative `src/CompensationHistory.h`; there is
no second editable DSP implementation. Tracktion consumers need the public src
include path exported by VibeRaCore. Existing dependency pins/licenses remain.

The combined guard validates every target before mutation, exact vendor/patch
hashes, predecessor chain and normalized source content identity. Unknown bytes
are preserved and rejected. Repeating a verified application leaves mtimes alone.

Unmarked graphs retain the existing behavior. A private prepared graph may opt
in with an explicit maximum delay and PCM storage budget. Summing/Connected
insert audio history taps even at zero compensation. Reuse keys contain stable
producer/consumer IDs and ordered port, independent of delay; duplicate keys or
incompatible configurations reject preparation. Only immutable configuration and
shared handles cross preparation; live PCM/cursors are never copied or read.
The single adopted graph owns bank writes. Old nodes retire off the callback.

DSP uses prepared storage, raw pointers, reference sample positions and bounded
channel/frame loops. Missing history and shape/capacity/discontinuity counters
remain explicit failures, never successful muting. Candidate budget includes
PCM rings and frame scratch; admission must also account for simultaneous old
and new graphs, native processors and other buffers.

Production opt-in is OFF. Primitive/isolated patch passes do not establish live
continuity. The internal AudioEngine binding is a bounded experiment, with an
actual audio-output adoption marker. It does not establish all-device atomic
publication or wet-packet/graph synchronization. Positive MIDI compensation
still uses upstream queues: incompatible delay changes can discard queued
NoteOffs and require reconciliation or explicit admission refusal. No universal
MIDI real-time or continuity claim. Nested transforms, resource admission,
failure recovery and live musical qualification remain required.

Retained draft/provenance: build/mixer-live-history-integration. Actual evolving
results are in docs/VERIFICATION.md and docs/modules/MIXER.json. Do not infer a
passed build/audio test from the presence of this patch bundle.
