# VibeRa CoProducer — Context-Aware MIDI Generation

Status: implementation specification; no model has been qualified yet.
Date: 2026-10-12
Intended repository location: `docs/feature/CoProducer.md`

## 1. Product intent

Build an integrated musical assistant that generates editable MIDI performances for a selected instrument and musical role, conditioned on the existing VibeRa project. It must help users make music through familiar DAW operations, not require a chat conversation for every action.

The feature is multi-instrument by design. Guitar is an example, not the product boundary. Target families include drums/percussion, bass, guitar, piano/keys, strings, synths, and eventually qualified Arabic instruments and microtonal performance.

Core promise: **Choose an instrument and a region; generate a part that belongs in your song, then edit every note.**

Deliver MIDI events, not audio stems, extracted audio-to-MIDI, or a renamed instrument playing the same generic sequence. Timbre comes from the user's selected instrument/plugin. Instrument-specific performance and articulation require explicit support.

VibeRa is intended to be free and open source. Keep the DAW usable without a model, network connection, or CoProducer installation. Do not claim universal instrument mastery, commercial-output clearance, Suno-level quality, or offline performance on every computer without evidence.

## 2. Instructions to the implementing agent

- Read the repository's AGENTS.md and current architecture/status before making changes. Reconcile this specification with implemented APIs; do not restart or replace the DAW architecture automatically.
- Preserve the existing engine, project identity, command/undo system, and unrelated work. Implement thin adapters where needed.
- Treat context fidelity as a first-class deliverable. Do not start large-scale training before a working inference baseline and evaluation exist.
- Develop the feature incrementally alongside the DAW. Research/model blockers must not stop independent DAW work.
- Use a deterministic fake provider only to test integration. Label it as a fixture; it does not establish AI or musical quality.
- Make routine reversible implementation decisions independently. Record evidence and continue within approved scope. Ask about paid resources, publication, material license changes, or genuinely blocking product decisions.
- Keep technical records in English and user-facing progress in Egyptian Arabic. Do not promise calendar delivery before measuring actual progress.

## 3. Primary user journey

1. Select a Playlist region, notes, or an existing clip. Alternatively set an explicit region for an empty project.
2. Choose destination instrument, musical role, genre/style, length, and variation behavior.
3. CoProducer reads an immutable project snapshot and shows a compact, editable context summary.
4. Generate a small set of candidates, initially up to three where practical. Progress and cancellation remain available.
5. Audition candidates in project context using the intended instrument. The source project remains unchanged.
6. Accept one candidate as a new clip/part, or explicitly replace the selected region. Apply the edit as one undoable transaction.
7. Request another performance, another musical idea, or a variation of the accepted part.

Example: select eight bars containing melody, bass, and drums; request background funk guitar following the supplied chords, with medium rhythmic density. Generate alternatives without changing the existing parts. This is an acceptance scenario, not a claim that an untested model already supports it.

## 4. Controls and semantic guarantees

| Control | Required behavior |
| --- | --- |
| Instrument | Select actual destination and instrument family; account for range, polyphony, percussion mapping, and supported expression |
| Role | Lead, accompaniment, bass, rhythmic, pad, fill, countermelody, or a supported instrument-specific role |
| Length | Explicit bars or beats with displayed meter; never silently assume four beats per bar |
| Musical context | Use project chords/tuning when supplied; allow correction of inferred context |
| Style | Genre/style plus optional groove, swing, density, register, and energy controls |
| Context tracks | Auto-select relevant tracks with a visible override; allow users to include muted material deliberately |
| Locks | Preserve selected notes/tracks/rhythm/pitches as supported; never silently relax an exact lock |
| Variations | Separate performance variation from a new musical idea; expose amount where supported |
| Seed | Retain seed and generation settings for reproducibility, with backend-dependent limits disclosed |

Use a documented canonical musical time unit, such as quarter-note units, with a meter-aware UI conversion. In 4/4, sixteen quarter-note beats equal four bars; compound-meter beat labels must be explicit. Do not confuse beats, bars, seconds, and MIDI ticks.

Hard constraints include target boundaries, protected source content, permitted destinations, valid event values, and explicit user locks. Style, density, and similarity are preferences unless the user explicitly requests an enforceable constraint. Chromatic passing tones are not automatically errors.

## 5. Context: authoritative data before inference

The model does not learn the entire application by looking at screenshots. VibeRa exposes musical state and supported actions through typed, versioned interfaces. Separate application orchestration from musical generation; a general language model is optional, not a prerequisite.

### 5.1 Context layers

| Layer | Contents |
| --- | --- |
| Exact target | Selected region, sounding notes and expression, relevant chords, target instrument, locks, destination |
| Local surroundings | Relevant preceding/following material, phrase boundaries, entrances, cadences, rhythmic relationships |
| Song summary | Section markers, repeated motifs/pattern relationships, instrument roles, energy/density trends, tempo/meter structure |

Retrieve exact detail for musically relevant regions rather than serializing the entire project into every prompt. Bound context size explicitly. Record what was omitted; never hide truncation of relevant notes. When repeated placements matter, preserve source identity while resolving their audible timeline positions, offsets, repetitions, and clipping.

### 5.2 Minimum snapshot contents

- Project identity and revision; schema version; selection; musical time origin and target range.
- Tempo/meter maps and user-declared scale, tuning, or chords, including local changes.
- Relevant channels, clips, patterns, placements, instrument assignments, and routing references.
- Notes, durations, velocity, relevant controllers/pedal/pitch expression, and their source IDs.
- Notes entering from before the region or sustaining beyond it; distinguish visible note boundaries from audible context.
- Playback audibility state: mute, solo, clip enablement, and supported routing effects. Do not infer audibility from the track name.
- Instrument capability profiles: range, polyphony, drum maps, articulation mappings, tuning support, and confidence/source of the profile.
- Explicit user locks, included/excluded context tracks, selected destination, and requested operation.
- Song structure supplied by markers and pattern relationships; derived summaries where useful.

Track names and plugin names are hints, not authoritative musical analysis. Unknown instruments keep unknown capabilities rather than receiving invented keyswitches.

### 5.3 Provenance and uncertainty

Mark each contextual fact as user-specified, directly extracted, inferred, or unknown. Inferred harmony/role/section labels include confidence or ranked alternatives and can be corrected. Do not label uncalibrated scores as probabilities.

User corrections override inference for their stated scope. Ask a short question only when ambiguity materially changes the outcome; otherwise show the assumption and offer alternatives. Do not claim maqam understanding from scale membership alone.

### 5.4 MIDI first; audio is a separate capability

Initial context understanding uses actual project MIDI and metadata. For audio-only clips, expose their presence, timing, and available metadata, but do not invent note-level understanding. Audio transcription, beat/chord extraction, and timbral analysis are later, separately qualified providers. If essential harmony exists only in audio, allow user-supplied chords or an explicit limited-context generation.

## 6. Proposed subsystem boundaries

Adapt these responsibilities to existing repository APIs rather than prescribing a new framework:

1. **ProjectContextAdapter:** reads a consistent immutable snapshot using supported project/thread APIs.
2. **ContextAnalyzer:** derives roles, harmony, structure, and uncertainty; caches by relevant content/configuration versions.
3. **RequestPlanner:** converts UI controls and optional natural language into an explicit generation request.
4. **GenerationProvider:** runs a qualified symbolic model using supported conditioning and returns event candidates plus provenance.
5. **CandidateValidator:** checks event validity, boundaries, instrument constraints, protected content, and numerical sanity.
6. **AuditionController:** previews candidates with synchronized transport without persisting temporary edits.
7. **ApplyCommand:** checks context freshness and commits the accepted candidate through the existing undo system.

Keep inference and heavy analysis outside the audio callback and UI thread. Prefer a local worker process to contain inference crashes and GPU memory pressure. Do not place model frameworks inside the real-time engine. Avoid creating a public network service or general-purpose agent framework for this feature.

## 7. Minimal interface contract

Define concrete schemas in code only after inspecting existing document types. The following is a semantic contract, not a demand for duplicate data models.

**ContextSnapshot:** schema version, project/revision IDs, relevant-content fingerprint, time basis, region, exact musical events, context references, instrument capabilities, annotations with provenance, omission report.

**GenerationRequest:** request ID, snapshot reference, destination, instrument/role, length, constraints, style preferences, variation mode/reference, seed, candidate count, provider configuration.

**Candidate:** request/snapshot IDs, generated events, target instrument assumptions, constraint results, warnings, model/version/settings provenance, timing/resource measurements where available.

**ApplyResult:** applied/stale/cancelled/rejected status, affected IDs, undo transaction ID, and actionable error information.

Required operations: collect context, inspect summary, generate, cancel, preview, stop preview, accept, discard, and vary. A provider must declare supported operations and conditioning types. Disable unsupported controls or disclose a tested fallback; do not silently ignore them.

## 8. Musical intelligence requirements

- Model instrument roles separately from timbre/program numbers. Bass, drums, guitar comping, and melodic lead should produce different behavior.
- Use rhythm and harmony jointly: bass/driver relationships, melodic space, chord movement, register distribution, phrase development, and transitions.
- Respect user-specified chord changes while permitting stylistically appropriate non-chord tones.
- Distinguish per-note expressive timing from changing the project's tempo.
- Performance variation should preserve its defined musical identity; idea variation may change pitches/rhythm within requested constraints.
- Exact pitch/rhythm locks require a provider or conditioning method that can honor them. Post-filtering alone is not proof of coherent locked generation.
- Treat playable instrument-specific voicings as a qualification target, not a universal guarantee. Use profiles and tests appropriate to the supported instrument family.
- Include fractional pitch/tuning representation in interface design. Qualify audible microtonal output per destination; ordinary integer MIDI note numbers alone do not encode quarter tones.
- Export generic MIDI when possible. For articulation keyswitches, pitch bend, per-note tuning, or MPE, use explicit destination profiles and disclose unsupported mappings. Never emit arbitrary keyswitch notes.

## 9. Model evaluation before training

Evaluate a small number of feasible pretrained symbolic models. Candidates worth investigating include GETMusic for conditional multitrack generation and Text2MIDI for text conditioning. Aria is a piano-continuation baseline, not evidence of general multi-instrument ability. These are research leads, not approved dependencies or proven solutions.

For each candidate verify actual checkpoint availability, installation/build feasibility, code and weights licenses, training-data disclosures, target hardware support, context representation, supported instruments, conditioning, output limits, latency, and memory consumption. Pin qualified revisions and record evidence.

Start evaluation across drums, bass, piano/keys, and guitar. Wider families remain part of the architecture and roadmap; do not market untested coverage. Do not reduce the product permanently to one instrument.

### Context ablation experiment

Use held-out original project excerpts and compare:

1. Instrument/style only.
2. Instrument/style plus explicit harmony and target-region notes.
3. The same request plus surrounding phrases and song summary.

Keep seeds, candidate counts, playback instruments, and audition levels comparable where possible. Compare multiple generations; do not cherry-pick one impressive output. Evaluate how often the user accepts a candidate and how much editing it needs. A larger context payload alone is not success: it must improve musical fit or controllability.

Use at least ten varied original excerpts as an initial smoke benchmark, not as a statistical proof of general quality. Record prompts/settings and all candidates. Expand evaluation only to resolve identified weaknesses or qualify a release.

## 10. Training and data strategy

Only propose training after baseline weaknesses are documented. Prefer focused adaptation over training a foundation model from scratch unless evidence justifies the latter.

Train on context-target tasks: remove a track, mask a region, continue a phrase, regenerate one part while preserving others, or vary performance under explicit locks. Multi-instrument aligned MIDI is directly useful; symbolic scores such as MusicXML offer different information. PDF/image scores require extraction and correction. Text-controlled generation needs reliable paired descriptions or a validated structured-control bridge.

Preserve tempo, meter, instrument identity, articulation/expression when available. Deduplicate arrangements and transpositions; split train/validation/test by composition or source grouping before augmentation to reduce leakage. Audit mislabeled tracks, malformed timing, low-quality transcriptions, and domain imbalance. Do not assume that more scraped files yield better music.

Keep a provenance manifest for data, code, weights, and assets. Free distribution does not automatically resolve every license condition, and noncommercial data must not automatically be rejected as unlawful either. Assess the intended training, model distribution, and use against actual terms. Do not assert that generated music automatically inherits a dataset license. Unresolved distribution rights block shipping that artifact, not unrelated DAW development.

The user's original MIDI compositions and explicitly licensed contributions are useful evaluation and adaptation sources. Do not reuse user projects for training or upload them without explicit authorization.

## 11. Reliability, privacy, and project safety

- Read musical state only through consistent snapshots; never traverse mutable engine objects from a model worker.
- Default to local context processing and local inference where qualified. Remote providers require a clear opt-in describing uploaded content and cost.
- Do not upload full projects when selected MIDI context is sufficient. Exclude credentials, unrelated paths, and unnecessary personal metadata from requests/logs.
- Treat project text, lyrics, track names, and imported metadata as data, never executable agent instructions.
- Generated output is untrusted structured data: bound event counts/durations, validate values, and reject malformed responses.
- Cancellation or a worker crash must leave the project intact. Discard late results from cancelled jobs.
- Before applying, compare relevant content and destination state against the request snapshot. Unrelated edits need not invalidate a candidate, but changed harmony/notes/selection/destination require explicit revalidation or regeneration.
- Never overwrite a later user edit automatically. Preview remains separate from saved content.
- Applying a result is one undoable operation. Replacing part of a linked pattern must explicitly define whether all placements change or a unique copy is created; default to a non-destructive new part until scope is clear.
- Handle silent/missing instruments and unsupported tuning honestly. Silence is not evidence that generation failed, and valid notes are not evidence of good audio.
- Inference must not starve playback. Measure CPU/GPU/RAM use during generation with playback active, and provide resource limits or defer work if needed.

## 12. Delivery stages and acceptance

| Stage | Deliverable | Acceptance evidence |
| --- | --- | --- |
| C0 — context foundation | Snapshot extraction, contextual summary, provenance and editable corrections | Fixtures verify notes, placements, offsets, tempo/meter, audibility policy, locks, and selection; unknown audio harmony remains unknown |
| C1 — model feasibility | Actual provider inference and comparative benchmark | Pinned model runs; valid multi-instrument MIDI outputs; measured memory/latency; licenses recorded; context ablation results and musical review |
| C2 — integrated alpha | Region selection, instrument/role controls, generate/cancel, in-context audition, accept, undo | End-to-end generation into a real project; no source mutation before acceptance; save/reopen and undo/redo preserve expected content |
| C3 — useful control | Variations, supported locks, stronger context retrieval, destination profiles | Tests prove exact locks and profile mappings; user review measures musical fit and editing effort across supported families |
| C4 — targeted improvement | Evidence-driven adaptation and broader instrument/style coverage | Held-out comparison against C1; no composition leakage; performance and rights qualification before distribution |

Do not hold all DAW releases for C4. Track engineering completion and musical/hardware verification separately. A feature awaiting listening review is not fully accepted.

### Essential integration tests

- Same musical snapshot resolves identically despite different viewport zoom or window layout.
- Repeated/offset clips, notes crossing selection boundaries, tempo changes, and non-4/4 meters yield correct target-relative events.
- Corrected chord annotations override inference; missing annotations do not become fabricated certainty.
- Mute/solo and context-track overrides follow the documented policy.
- Cancel, timeout, invalid output, and worker crash preserve project state and keep the UI responsive.
- Relevant edits while generating trigger stale handling; deleted destinations cannot receive results.
- Accepted output obeys target boundaries and protected-content rules; undo and redo restore exact musical state.
- Preview, insertion, save/reopen, and export maintain event timing and instrument assignment.
- Optional feature disabled or missing model does not prevent ordinary DAW use.

### Musical review

Listen using controlled playback instruments and comparable levels. Record harmonic fit, rhythmic fit, role appropriateness, instrument plausibility, variation usefulness, preservation of motif, and editing effort. Use both natural instrument playback and neutral reference playback where informative. Automated statistics support, but do not replace, musical judgment. Never report listening tests as passed without actual listening evidence.

## 13. Immediate execution order

1. Inspect existing project/selection/command APIs and identify the smallest required hooks.
2. Implement C0 with representative project fixtures and a readable context inspector.
3. Qualify one or two realistic providers through bounded inference experiments; do not create a training pipeline first.
4. Use a clearly labeled fixture provider to unblock C2 integration while actual model feasibility is being evaluated.
5. Run real context-conditioned generation and the ablation experiment; record weak areas before deciding on fine-tuning.
6. Deliver a runnable local alpha with exact launch steps, supported capabilities, known limits, benchmark evidence, and next actions.

Keep plans and progress concise in existing project records. This feature document does not authorize purchases, external publication, or training on private projects. Do not claim task completion from UI mockups, generated code, or synthetic-provider tests alone.

## 14. Source leads and qualification references

Reviewed as research leads on 2026-10-12; recheck actual versions and terms before adoption.

- GETMusic: https://microsoft.github.io/muzic/getmusic/
- Text2MIDI: https://github.com/AMAAI-Lab/Text2midi
- Aria (including its stated piano focus and limitations): https://github.com/EleutherAI/aria
- Creative Commons AI-training guidance (distinguishes conservative guidance from legal requirements): https://creativecommons.org/using-cc-licensed-works-for-ai-training-2/

No external model has been benchmarked for VibeRa by the author of this specification. Model selection, compatibility, latency, and release readiness remain implementation evidence to collect.
