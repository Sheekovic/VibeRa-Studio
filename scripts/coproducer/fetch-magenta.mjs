// SPDX-License-Identifier: AGPL-3.0-or-later
// Explicit download step only. Inference uses local checkpoints and does not fetch.
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';

const root = path.resolve(process.argv[2] ?? 'build/coproducer-research/magenta/checkpoints');
const selected = process.argv.slice(3);
const allowed = {
  basic_rnn: 'music_rnn/basic_rnn',
  multitrack_chords: 'music_vae/multitrack_chords',
};
const provenance = [];
let previous = [];
try {
  previous = JSON.parse(await readFile(path.join(root, 'provenance.json'), 'utf8'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
for (const id of selected.length ? selected : Object.keys(allowed)) {
  if (!allowed[id]) throw new Error('Unsupported research checkpoint');
  const directory = path.join(root, id);
  await mkdir(directory, { recursive: true });
  const origin = 'https://storage.googleapis.com/magentadata/js/checkpoints/' + allowed[id] + '/';
  const records = [];
  const prior = previous.find(record => record.id === id && record.origin === origin);
  async function download(relative) {
    if (!/^[a-zA-Z0-9_.-]+$/.test(relative)) throw new Error('Unsafe checkpoint member');
    const target = path.join(directory, relative);
    const expected = prior?.records.find(record => record.relative === relative);
    if (expected) {
      try {
        const buffer = await readFile(target);
        if (buffer.length === expected.bytes && createHash('sha256').update(buffer).digest('hex') === expected.sha256) {
          records.push(expected);
          console.log(`${id}/${relative}: reused hash-verified local bytes`);
          return buffer;
        }
        throw new Error('Existing pinned checkpoint differs; preserve it and investigate before replacement.');
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
    }
    const response = await fetch(origin + relative, { signal: AbortSignal.timeout(120000) });
    if (!response.ok) throw new Error(`Checkpoint HTTP ${response.status}: ${relative}`);
    const buffer = Buffer.from(await response.arrayBuffer());
    if (buffer.length > 64 * 1024 * 1024) throw new Error('Research checkpoint member exceeds byte budget');
    await writeFile(target, buffer);
    records.push({ relative, bytes: buffer.length, sha256: createHash('sha256').update(buffer).digest('hex') });
    console.log(`${id}/${relative}: ${buffer.length} bytes`);
    return buffer;
  }
  await download('config.json');
  const manifest = JSON.parse((await download('weights_manifest.json')).toString('utf8'));
  if (!Array.isArray(manifest) || manifest.length > 128) throw new Error('Invalid checkpoint manifest');
  const members = [...new Set(manifest.flatMap(group => group.paths))];
  if (members.length > 128) throw new Error('Checkpoint member budget exceeded');
  for (const member of members) await download(member);
  provenance.push({ id, origin, fetchedAt: new Date().toISOString(), records,
    qualification: 'research only; inference, quality and checkpoint redistribution not yet qualified',
    codeLicense: 'Apache-2.0 (@magenta/music); checkpoint distribution terms require separate review',
    source: 'https://github.com/magenta/magenta-js/blob/master/music/checkpoints/README.md' });
}
const retained = previous.filter(record => !provenance.some(current => current.id === record.id));
await writeFile(path.join(root, 'provenance.json'), JSON.stringify([...retained, ...provenance], null, 2));
