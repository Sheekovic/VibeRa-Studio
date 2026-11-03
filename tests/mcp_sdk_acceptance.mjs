// SPDX-License-Identifier: AGPL-3.0-or-later
// Uses a caller-selected, pinned official SDK only for testing. No runtime dependency.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const app = path.resolve(process.argv[2]);
const output = path.resolve(process.argv[3]);
const sdk = path.resolve(process.argv[4]);
const version = JSON.parse(fs.readFileSync(path.join(sdk, 'package.json'), 'utf8')).version;
if (version !== '1.32.1') throw new Error('Expected pinned official MCP SDK 1.32.1');
const { Client } = await import(pathToFileURL(path.join(sdk, 'dist/esm/client/index.js')));
const { StdioClientTransport } = await import(pathToFileURL(path.join(sdk, 'dist/esm/client/stdio.js')));
fs.mkdirSync(output, { recursive: true });
const sha = () => crypto.createHash('sha256').update(fs.readFileSync(app)).digest('hex');
const beforeHash = sha();
const client = new Client({ name: 'VibeRa independent SDK acceptance', version: '1' });
const transport = new StdioClientTransport({ command: app, args: ['--mcp-test'], stderr: 'pipe' });
const errors = [];
transport.stderr?.on('data', chunk => errors.push(chunk.toString()));
let checks = 0;
function require(condition, message) {
  if (!condition) throw new Error(message);
  ++checks;
}
async function call(name, args = {}) {
  const result = await client.callTool({ name, arguments: args });
  require(!result.isError, `${name} failed: ${JSON.stringify(result)}`);
  return result;
}
try {
  await client.connect(transport);
  require(client.getServerVersion().name === 'VibeRa Studio local testing', 'Wrong application server');
  await client.ping();
  const { tools } = await client.listTools();
  require(tools.length === 10, 'Tool discovery/schema validation failed');
  const initial = (await call('vibera_state')).structuredContent;
  const edited = (await call('vibera_command', { command: 'notes.add', revision: initial.revision,
    args: { beat: 2, length: 1, pitch: 67, value: 0.7 } })).structuredContent;
  require(edited.notes.length === initial.notes.length + 1 && edited.notes.at(-1).pitch === 67,
    'Canonical edit through official SDK did not reach actual document');
  const rejected = await client.callTool({ name: 'vibera_command',
    arguments: { command: 'notes.delete', revision: initial.revision } });
  require(rejected.isError === true, 'Stale revision not preserved as MCP tool error');
  const restored = (await call('vibera_command', { command: 'edit.undo' })).structuredContent;
  require(restored.notes.length === initial.notes.length, 'Undo through official SDK failed');
  await call('vibera_command', { command: 'edit.redo' });
  await call('vibera_editor', { editor: 1, action: 'maximize' });
  const selected = (await call('vibera_key', { key: 65, ctrl: true })).structuredContent;
  require(selected.selectedNotes.includes(edited.notes.at(-1).id), 'Actual shortcut not delivered through SDK');
  const image = (await call('vibera_snapshot')).content.find(item => item.type === 'image');
  require(image?.mimeType === 'image/png', 'MCP image result failed SDK validation');
  const bytes = Buffer.from(image.data, 'base64');
  require(bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])), 'PNG payload invalid');
  fs.writeFileSync(path.join(output, 'sdk-project.png'), bytes);
  await client.close();
  require(sha() === beforeHash, 'Application binary changed during qualification');
  const result = { checks, sdkVersion: version, applicationSha256: beforeHash,
    evidence: 'Official MCP SDK actual stdio/application lifecycle, tools, edits, errors, undo/redo, shortcut and PNG; no OS input/audio/listening' };
  fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
} finally {
  await client.close();
  fs.writeFileSync(path.join(output, 'stderr.log'), errors.join(''));
}
