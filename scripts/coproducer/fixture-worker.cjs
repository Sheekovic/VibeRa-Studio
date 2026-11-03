// SPDX-License-Identifier: AGPL-3.0-or-later
// Explicit integration fixture. No model or musical-quality evidence.
const fs = require('node:fs');
const path = require('node:path');
const mode = path.basename(process.argv[2]);
const input = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const output = process.argv[4];
if (mode === 'crash') process.exit(7);
if (mode === 'hang') { setInterval(() => {}, 1000); }
else {
  const response = { schema: 1, requestId: input.requestId, fingerprint: input.fingerprint, status: 'ok',
    candidates: [{ provider: 'deterministic integration fixture', modelVersion: 'fixture-1', testFixture: true, seed: input.seed,
      events: [{ start: 0, length: .25, pitch: 60, velocity: .75, cents: 0 }] }], warnings: [] };
  if (mode === 'malformed') response.candidates[0].events[0].pitch = '60';
  fs.writeFileSync(output, JSON.stringify(response));
}
