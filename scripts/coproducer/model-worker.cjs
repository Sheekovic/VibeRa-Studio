// SPDX-License-Identifier: AGPL-3.0-or-later
// One local, structured request per owned process. No shell, network or application object access.
const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');
const { loadModel, seedRandom } = require('./magenta-runtime.cjs');
const installation = process.argv[2], inputFile = process.argv[3], outputFile = process.argv[4];
const warnings = new Set();
let request;
const finite = Number.isFinite;
function atomic(file, value) {
  const text = JSON.stringify(value);
  if (Buffer.byteLength(text) > 4 * 1024 * 1024) throw new Error('Response exceeds byte budget');
  fs.writeFileSync(file + '.tmp', text, { encoding: 'utf8', mode: 0o600 });
  for (let attempt = 0; ; ++attempt) {
    try { fs.renameSync(file + '.tmp', file); break; }
    catch (error) {
      if (attempt >= 4 || !['EACCES', 'EPERM', 'EBUSY'].includes(error.code)) throw error;
      // A Windows progress reader can briefly retain the old file. This wait is
      // inside the isolated model process, never a DSP or UI callback.
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
    }
  }
}
function cancelCheck() {
  if (fs.existsSync(path.join(path.dirname(outputFile), 'cancel'))) throw new Error('Generation cancelled');
}
function progress(value) { cancelCheck(); atomic(path.join(path.dirname(outputFile), 'progress.json'), { requestId: request.requestId, progress: value }); }
function validateRequest(r) {
  if (!r || r.schema !== 1 || typeof r.requestId !== 'string' || r.requestId.length > 128 ||
      typeof r.fingerprint !== 'string' || !/^[0-9a-f]{64}$/i.test(r.fingerprint) ||
      !['basic_rnn', 'multitrack_chords'].includes(r.model) || !Number.isInteger(r.count) || r.count < 1 || r.count > 3 ||
      !finite(r.variationAmount) || r.variationAmount < 0 || r.variationAmount > 1 ||
      !finite(r.length) || r.length <= 0 || r.length > 32 || r.numerator !== 4 || r.denominator !== 4 ||
      typeof r.destination !== 'string' || !/^\d+$/.test(r.destination) || !Array.isArray(r.events) || r.events.length > 8192 ||
      !Array.isArray(r.chords) || r.chords.length > 4096 || !finite(r.tempo) || r.tempo < 20 || r.tempo > 400 || r.style !== '')
    throw new Error('Invalid or unsupported request');
  seedRandom(r.seed);
  for (const event of r.events)
    if (!event || typeof event.channel !== 'string' || !/^\d+$/.test(event.channel) ||
        !finite(event.start) || !finite(event.length) || event.length <= 0 ||
        !Number.isInteger(event.pitch) || event.pitch < 0 || event.pitch > 127 ||
        !finite(event.velocity) || event.velocity <= 0 || event.velocity > 1 ||
        typeof event.audible !== 'boolean' || typeof event.locked !== 'boolean') throw new Error('Invalid context event');
  if (r.model === 'basic_rnn' && (r.role !== 'lead' || r.variation !== 'continuation' || r.chords.length !== 0 ||
      !Number.isInteger(r.length * 4))) throw new Error('MelodyRNN supports lead continuation on a sixteenth-note grid without chord conditioning');
  if (r.model === 'multitrack_chords' && (r.length % 4 !== 0 || r.role !== 'native part' ||
      !['new idea', 'idea variation'].includes(r.variation) || !['keys', 'bass', 'guitar', 'strings', 'synth'].includes(r.family)))
    throw new Error('MusicVAE supports native instrument parts in whole 4/4 bars; unsupported mappings must be qualified separately');
}
function primer(r) {
  const preceding = r.events.filter(e => e.channel === r.destination && e.start < 0 && e.start + e.length > -4)
    .sort((a, b) => a.start - b.start || a.pitch - b.pitch);
  if (preceding.some(e => e.pitch < 48 || e.pitch > 83)) throw new Error('MelodyRNN primer must be in its trained pitch range 48..83');
  const notes = [];
  let previousEnd = 0;
  for (const e of preceding) {
    const start = Math.max(0, Math.round((e.start + 4) * 4));
    const end = Math.min(16, Math.round((e.start + e.length + 4) * 4));
    if (end <= start) continue;
    if (start < previousEnd) throw new Error('MelodyRNN requires a monophonic primer; choose a melodic context');
    notes.push({ pitch: e.pitch, quantizedStartStep: start, quantizedEndStep: end }); previousEnd = end;
  }
  if (!notes.length) {
    const note = r.events.find(e => e.channel === r.destination && e.pitch >= 48 && e.pitch <= 83);
    notes.push({ pitch: note?.pitch ?? 60, quantizedStartStep: 0, quantizedEndStep: 4 });
    warnings.add(note ? 'No preceding melody: first available destination pitch used as primer.' : 'No melody context: default C4 primer used.');
    return { quantizationInfo: { stepsPerQuarter: 4 }, totalQuantizedSteps: 4, notes };
  }
  warnings.add('MelodyRNN consumes the preceding destination melody, quantized to sixteenth notes; other parts and later song context are not consumed.');
  return { quantizationInfo: { stepsPerQuarter: 4 }, totalQuantizedSteps: 16, notes };
}
function symbol(pitches) {
  if (!Array.isArray(pitches) || pitches.length !== 3 || pitches.some(p => !Number.isInteger(p) || p < 0 || p > 11))
    throw new Error('This MusicVAE adapter supports supplied major/minor triads only');
  const set = [...new Set(pitches)].sort((a,b) => a-b).join(',');
  const names = ['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B'];
  for (let root = 0; root < 12; ++root)
    for (const [third, suffix] of [[4, ''], [3, 'm']])
      if ([root,(root+third)%12,(root+7)%12].sort((a,b)=>a-b).join(',') === set) return names[root] + suffix;
  throw new Error('Supplied chord is not a supported major/minor triad');
}
function chordForBar(r, beat) {
  const overlapping = r.chords.filter(c => finite(c.start) && finite(c.length) && c.length > 0 && c.start < beat + 4 && c.start + c.length > beat);
  if (!overlapping.length) { warnings.add('Harmony was unspecified for some bars; no chord target was supplied there.'); return 'N.C.'; }
  if (overlapping.length !== 1 || overlapping[0].start > beat || overlapping[0].start + overlapping[0].length < beat + 4)
    throw new Error('Initial MusicVAE adapter requires one supplied chord covering each whole bar; sub-bar changes are not silently discarded');
  return symbol(overlapping[0].pitches);
}
const programs = { keys: [0,23,0], guitar: [24,31,25], bass: [32,39,33], strings: [40,55,48], synth: [80,103,80] };
async function similarBar(runtime, input, controls, tempo, amount) {
  // Use the public latent APIs so the decoder receives the checkpoint's 24-step time basis.
  // MusicVAE.similar() internally omits this argument and applies a four-step default.
  const encoded = await runtime.model.encode([input], controls);
  const mixed = runtime.tf.tidy(() => encoded.mul(1 - amount).add(runtime.tf.randomNormal([1, runtime.model.zDims]).mul(amount)));
  try { return await runtime.model.decode(mixed, 1.0, controls, 24, tempo); }
  finally { encoded.dispose(); mixed.dispose(); }
}
function sequenceForBar(r, beat) {
  const notes = r.events.filter(e => e.channel === r.destination && e.start < beat + 4 && e.start + e.length > beat).map(e => ({
    pitch: e.pitch, velocity: Math.round(e.velocity * 127), instrument: 0, program: programs[r.family][2],
    quantizedStartStep: Math.max(0, Math.round((e.start-beat)*24)),
    quantizedEndStep: Math.min(96, Math.round((e.start+e.length-beat)*24)) })).filter(e=>e.quantizedEndStep>e.quantizedStartStep);
  if (!notes.length) throw new Error('Idea variation needs destination notes in each target bar; empty bars require a new-idea request');
  return { quantizationInfo: { stepsPerQuarter: 24 }, totalQuantizedSteps: 96, notes };
}
function normalized(sequence, beat, family) {
  const spq = sequence?.quantizationInfo?.stepsPerQuarter;
  if (!finite(spq) || spq <= 0 || !Array.isArray(sequence.notes) || sequence.notes.length > 65536) throw new Error('Invalid model sequence');
  let notes = sequence.notes;
  if (family) notes = notes.filter(n => !n.isDrum && Number.isInteger(n.program) && n.program >= programs[family][0] && n.program <= programs[family][1]);
  return notes.map(n => {
    if (!Number.isInteger(n.pitch) || !finite(n.quantizedStartStep) || !finite(n.quantizedEndStep)) throw new Error('Invalid generated note');
    return { start: beat + n.quantizedStartStep / spq, length: (n.quantizedEndStep-n.quantizedStartStep)/spq,
      pitch: n.pitch, velocity: (Object.hasOwn(n, 'velocity') ? n.velocity : 100)/127, cents: 0 };
  });
}
async function main() {
  if (!inputFile || !outputFile || fs.statSync(inputFile).size > 2 * 1024 * 1024) throw new Error('Invalid request file');
  request = JSON.parse(fs.readFileSync(inputFile, 'utf8')); validateRequest(request); progress(0);
  const logs = [];
  console.log = (...args) => { if (logs.length < 16) logs.push(args.map(String).join(' ').slice(0, 512)); };
  const begun = performance.now(), runtime = await loadModel(installation, request.model), initialized = performance.now();
  const candidates = [];
  try {
    const initialPrimer = request.model === 'basic_rnn' ? primer(request) : null;
    for (let candidateIndex = 0; candidateIndex < request.count; ++candidateIndex) {
      seedRandom((request.seed + candidateIndex) >>> 0);
      const events = [];
      if (initialPrimer) {
        warnings.add('MelodyRNN does not generate note velocities; this adapter uses MIDI velocity 100.');
        cancelCheck();
        events.push(...normalized(await runtime.model.continueSequence(initialPrimer, request.length * 4, 1.0), 0));
      } else {
        for (let beat = 0; beat < request.length; beat += 4) {
          progress((candidateIndex + beat/request.length)/request.count);
          const controls = { chordProgression: [chordForBar(request, beat)] };
          let chosen = [];
          for (let attempt = 0; attempt < 3 && !chosen.length; ++attempt) {
            cancelCheck();
            const [sequence] = request.variation === 'idea variation' ?
              await similarBar(runtime, sequenceForBar(request, beat), controls, request.tempo, request.variationAmount) :
              await runtime.model.sample(1, 1.0, controls, 24, request.tempo);
            chosen = normalized(sequence, beat, request.family);
          }
          if (!chosen.length) throw new Error(`Model produced no ${request.family} part after bounded attempts; no generic part was substituted`);
          events.push(...chosen);
        }
        warnings.add('MusicVAE samples/varies one bar at a time; family tracks are selected from its ensemble output. Whole-song phrasing and playable voicings remain unqualified.');
        warnings.add('Other Channels, surrounding phrases and song summary are not consumed by this initial MusicVAE adapter.');
      }
      if (!events.length || events.length > 65536 || events.some(e => !finite(e.start) || e.start < 0 || !finite(e.length) ||
          e.length <= 0 || e.start+e.length > request.length || e.pitch < 0 || e.pitch > 127 || e.velocity <= 0 || e.velocity > 1))
        throw new Error('Generated events violate the target region; invalid output is rejected rather than repaired');
      candidates.push({ provider: 'Magenta.js local CPU', modelVersion: runtime.version, testFixture: false,
        seed: (request.seed + candidateIndex) >>> 0, events });
      progress((candidateIndex + 1)/request.count);
    }
    cancelCheck();
    atomic(outputFile, { schema: 1, status: 'ok', requestId: request.requestId, fingerprint: request.fingerprint,
      candidates, warnings: [...warnings, ...logs.filter(line=>/Ignoring|warning/i.test(line))],
      measurements: { initializationMs: initialized-begun, inferenceMs: performance.now()-initialized,
        peakRssBytes: process.resourceUsage().maxRSS*1024, nodeVersion: process.version, backend: 'cpu',
        seed: request.seed, localCheckpointReads: runtime.localRequests(), networkInference: false } });
  } finally { runtime.model.dispose(); }
}
main().catch(error => {
  try { atomic(outputFile, { schema: 1, status: 'error', requestId: request?.requestId ?? '',
    fingerprint: request?.fingerprint ?? '', error: String(error.message).slice(0, 1024) }); }
  catch { process.exitCode = 1; }
});
