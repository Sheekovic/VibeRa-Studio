// SPDX-License-Identifier: AGPL-3.0-or-later
// Research smoke probe. No user projects, MIDI uploads, audio or network inference.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { performance } = require('node:perf_hooks');

async function main() {
  const root = path.resolve(process.argv[2] ?? 'build/coproducer-research/magenta');
  const id = process.argv[3] ?? 'basic_rnn';
  const seed = Number(process.argv[4] ?? 17);
  if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff) throw new Error('Seed must be an unsigned 32-bit integer');
  let randomState = seed >>> 0;
  Math.random = () => {
    randomState = (randomState + 0x6d2b79f5) >>> 0;
    let value = randomState;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
  };
  if (!['basic_rnn', 'multitrack_chords'].includes(id)) throw new Error('Unknown model');
  const checkpointRoot = path.join(root, 'checkpoints');
  const provenance = JSON.parse(fs.readFileSync(path.join(checkpointRoot, 'provenance.json'), 'utf8'));
  const modelRecord = provenance.find(record => record.id === id);
  if (!modelRecord) throw new Error('Checkpoint provenance missing');
  const recordMap = new Map(modelRecord.records.map(record => [record.relative, record]));
  let localRequests = 0;
  globalThis.fetch = async (url) => {
    const prefix = `https://vibera.invalid/models/${id}/`;
    url = String(url);
    if (!url.startsWith(prefix)) throw new Error('Network access prohibited during inference');
    const member = url.slice(prefix.length);
    const expected = recordMap.get(member);
    if (!expected) throw new Error('Unpinned checkpoint member');
    const bytes = fs.readFileSync(path.join(checkpointRoot, id, member));
    if (bytes.length !== expected.bytes || crypto.createHash('sha256').update(bytes).digest('hex') !== expected.sha256)
      throw new Error('Checkpoint bytes changed');
    ++localRequests;
    return new Response(bytes, { status: 200 });
  };
  const packageRoot = path.join(root, 'node_modules');
  // The published Node bundle imports node-fetch directly, rather than global fetch.
  // Substitute its module only inside this research process; never edit installed packages.
  const fetchModule = require.resolve('node-fetch', { paths: [packageRoot] });
  require(fetchModule);
  require.cache[fetchModule].exports = globalThis.fetch;
  const tf = require(path.join(packageRoot, '@tensorflow/tfjs'));
  await tf.setBackend('cpu');
  await tf.ready();
  const { MusicRNN } = require(path.join(packageRoot, '@magenta/music/node/music_rnn.js'));
  const { MusicVAE } = require(path.join(packageRoot, '@magenta/music/node/music_vae.js'));
  const model = id === 'basic_rnn' ? new MusicRNN(`https://vibera.invalid/models/${id}`) :
    new MusicVAE(`https://vibera.invalid/models/${id}`);
  const initializedAt = performance.now();
  await model.initialize();
  const inferenceAt = performance.now();
  let sequence;
  if (id === 'basic_rnn') {
    const primer = { quantizationInfo: { stepsPerQuarter: 4 }, totalQuantizedSteps: 4,
      notes: [{ pitch: 60, quantizedStartStep: 0, quantizedEndStep: 2 },
              { pitch: 64, quantizedStartStep: 2, quantizedEndStep: 4 }] };
    sequence = await model.continueSequence(primer, 16, 1.0);
  } else {
    [sequence] = await model.sample(1, 1.0, { chordProgression: ['C'] }, 24, 120);
  }
  const finishedAt = performance.now();
  if (!sequence || !Array.isArray(sequence.notes) || sequence.notes.length === 0 || sequence.notes.length > 65536)
    throw new Error('Empty or excessive model output');
  for (const note of sequence.notes) {
    if (!Number.isInteger(note.pitch) || note.pitch < 0 || note.pitch > 127 ||
        !Number.isFinite(note.quantizedStartStep) || !Number.isFinite(note.quantizedEndStep) ||
        note.quantizedStartStep < 0 || note.quantizedEndStep <= note.quantizedStartStep)
      throw new Error('Invalid symbolic model output');
  }
  // Serialize with the package's MIDI dependency; importing Magenta's browser player
  // bundle is unnecessary and currently incompatible with Node 24's Tone resolution.
  const { Midi } = require(path.join(packageRoot, '@tonejs/midi'));
  const midi = new Midi();
  midi.header.setTempo(120);
  midi.header.timeSignatures.push({ ticks: 0, timeSignature: [4, 4] });
  const tracks = new Map();
  const stepsPerQuarter = sequence.quantizationInfo.stepsPerQuarter;
  if (!Number.isFinite(stepsPerQuarter) || stepsPerQuarter <= 0) throw new Error('Unknown quantization');
  for (const note of sequence.notes) {
    const key = `${note.instrument ?? 0}:${note.program ?? 0}:${Boolean(note.isDrum)}`;
    if (!tracks.has(key)) {
      const track = midi.addTrack();
      track.name = `Generated ${key}`;
      track.instrument.number = note.program ?? 0;
      track.channel = note.isDrum ? 9 : [...tracks.values()].filter(t => t.channel !== 9).length;
      if (track.channel >= 9 && !note.isDrum) ++track.channel;
      if (track.channel > 15) throw new Error('MIDI channel capacity exceeded');
      tracks.set(key, track);
    }
    tracks.get(key).addNote({ midi: note.pitch,
      ticks: Math.round(note.quantizedStartStep * midi.header.ppq / stepsPerQuarter),
      durationTicks: Math.round((note.quantizedEndStep - note.quantizedStartStep) * midi.header.ppq / stepsPerQuarter),
      velocity: (Object.hasOwn(note, 'velocity') ? note.velocity : 100) / 127 });
  }
  const midiPath = path.join(root, `${id}-probe-${seed}.mid`);
  const midiBytes = midi.toArray();
  const roundTrip = new Midi(midiBytes);
  if (roundTrip.tracks.reduce((sum, track) => sum + track.notes.length, 0) !== sequence.notes.length)
    throw new Error('MIDI round trip lost notes');
  fs.writeFileSync(midiPath, midiBytes);
  const result = { model: id, packageVersion: require(path.join(packageRoot, '@magenta/music/package.json')).version,
    backend: tf.getBackend(), networkInference: false, localRequests,
    initializationMs: inferenceAt - initializedAt, inferenceMs: finishedAt - inferenceAt,
    memory: process.memoryUsage(), peakRssBytes: process.resourceUsage().maxRSS * 1024,
    checkpointProvenance: modelRecord, midiPath, midiTracks: roundTrip.tracks.length,
    qualification: 'one synthetic inference; no musical quality, ablation, instrument-profile or release qualification',
    seed, seedLimitation: 'Determinism is restricted to the pinned CPU runtime and unsigned 32-bit seeds; cross-backend reproducibility is unqualified.', sequence };
  const output = path.join(root, `${id}-probe-${seed}.json`);
  fs.writeFileSync(output, JSON.stringify(result, null, 2));
  console.log(JSON.stringify({ output, notes: sequence.notes.length, initializationMs: result.initializationMs,
    inferenceMs: result.inferenceMs, peakRssBytes: result.peakRssBytes, localRequests }));
  model.dispose();
}
main().catch(error => { console.error(error.stack); process.exitCode = 1; });
