// SPDX-License-Identifier: AGPL-3.0-or-later
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(process.argv[2] ?? 'build/coproducer-research/magenta');
const jobs = path.join(root, 'worker-check-' + Date.now());
fs.mkdirSync(jobs);
async function run(model, variation, family, role) {
  const directory = path.join(jobs, model + '-' + variation.replaceAll(' ', '-'));
  fs.mkdirSync(directory);
  const request = { schema: 1, requestId: 'original-smoke-' + model + variation,
    fingerprint: 'a'.repeat(64), model, seed: 17, count: 1, role, style: '', variation, family,
    variationAmount: .25, length: 4, origin: 4, tempo: 120, numerator: 4, denominator: 4, destination: '1',
    events: [{ channel: '1', note: '2', start: -1, length: .5, pitch: 60, velocity: .8, locked: false, audible: true },
             { channel: '1', note: '3', start: 0, length: 1, pitch: 60, velocity: .8, locked: false, audible: true },
             { channel: '1', note: '4', start: 2, length: 1, pitch: 64, velocity: .8, locked: false, audible: true }],
    chords: model === 'basic_rnn' ? [] : [{ start: 0, length: 4, pitches: [0,4,7] }] };
  const input = path.join(directory, 'request.json'), output = path.join(directory, 'response.json');
  fs.writeFileSync(input, JSON.stringify(request));
  const started = Date.now();
  await new Promise((resolve, reject) => {
    const processHandle = spawn(process.execPath, ['--max-old-space-size=512', path.join(__dirname, 'model-worker.cjs'), root, input, output],
      { stdio: 'ignore', windowsHide: true });
    const timer = setTimeout(() => { processHandle.kill(); reject(new Error('Owned model probe timed out')); }, 120000);
    processHandle.on('error', error => { clearTimeout(timer); reject(error); });
    processHandle.on('exit', code => { clearTimeout(timer); code === 0 ? resolve() : reject(new Error('Worker exit ' + code)); });
  });
  const result = JSON.parse(fs.readFileSync(output, 'utf8'));
  assert.equal(result.status, 'ok', result.error);
  assert.equal(result.requestId, request.requestId);
  assert.equal(result.fingerprint, request.fingerprint);
  assert.equal(result.candidates.length, 1);
  assert.equal(result.candidates[0].testFixture, false);
  for (const note of result.candidates[0].events) {
    assert(Number.isFinite(note.start) && note.start >= 0);
    assert(Number.isFinite(note.length) && note.length > 0 && note.start+note.length <= request.length);
    assert(Number.isInteger(note.pitch) && note.pitch >= 0 && note.pitch <= 127);
  }
  console.log(JSON.stringify({ model, variation, family, notes: result.candidates[0].events.length, milliseconds: Date.now()-started, directory }));
}
(async () => {
  await run('basic_rnn', 'continuation', 'melody', 'lead');
  await run('multitrack_chords', 'new idea', 'guitar', 'native part');
  await run('multitrack_chords', 'idea variation', 'keys', 'native part');
})().catch(error => { console.error(error.stack); process.exitCode = 1; });
