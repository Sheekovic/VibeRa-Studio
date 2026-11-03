// SPDX-License-Identifier: AGPL-3.0-or-later
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function seedRandom(seed) {
  if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff) throw new Error('Seed must be unsigned 32-bit');
  let state = seed >>> 0;
  Math.random = () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let value = state;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
  };
}
async function loadModel(installation, id) {
  if (!['basic_rnn', 'multitrack_chords'].includes(id)) throw new Error('Unsupported model');
  const root = path.resolve(installation), packageRoot = path.join(root, 'node_modules');
  const version = JSON.parse(fs.readFileSync(path.join(packageRoot, '@magenta/music/package.json'), 'utf8')).version;
  if (version !== '1.23.1') throw new Error('Expected pinned @magenta/music 1.23.1');
  const records = JSON.parse(fs.readFileSync(path.join(root, 'checkpoints/provenance.json'), 'utf8'));
  const record = records.find(item => item.id === id);
  if (!record || !Array.isArray(record.records) || record.records.length > 128) throw new Error('Missing checkpoint provenance');
  const expected = new Map(record.records.map(item => [item.relative, item]));
  let localRequests = 0;
  const offlineFetch = async url => {
    const prefix = `https://vibera.invalid/models/${id}/`;
    url = String(url);
    if (!url.startsWith(prefix)) throw new Error('Network is disabled during model inference');
    const member = url.slice(prefix.length), pinned = expected.get(member);
    if (!/^[a-zA-Z0-9_.-]+$/.test(member) || !pinned || pinned.bytes > 64 * 1024 * 1024)
      throw new Error('Unpinned or excessive checkpoint member');
    const bytes = fs.readFileSync(path.join(root, 'checkpoints', id, member));
    if (bytes.length !== pinned.bytes || crypto.createHash('sha256').update(bytes).digest('hex') !== pinned.sha256)
      throw new Error('Checkpoint identity mismatch');
    ++localRequests;
    return new Response(bytes, { status: 200 });
  };
  globalThis.fetch = offlineFetch;
  const fetchModule = require.resolve('node-fetch', { paths: [packageRoot] });
  require(fetchModule);
  require.cache[fetchModule].exports = offlineFetch;
  const tf = require(path.join(packageRoot, '@tensorflow/tfjs'));
  await tf.setBackend('cpu'); await tf.ready();
  const type = id === 'basic_rnn' ? require(path.join(packageRoot, '@magenta/music/node/music_rnn.js')).MusicRNN :
    require(path.join(packageRoot, '@magenta/music/node/music_vae.js')).MusicVAE;
  const model = new type(`https://vibera.invalid/models/${id}`);
  await model.initialize();
  return { model, tf, version: `${id}/magenta-1.23.1/${crypto.createHash('sha256').update(JSON.stringify(record.records)).digest('hex')}`,
    localRequests: () => localRequests };
}
module.exports = { loadModel, seedRandom };
