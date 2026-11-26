#!/usr/bin/env node
// Facet: one frame of a cart through romdev, for the launcher art and the README's screenshot.
//
//   node games/facet/tools/shot.mjs <cart.wasc> <out.png> [--size WxH] [--gl] [--steps '<json>'] [KEY=VALUE ...]
//
// Loads the cart (seeded, so the same frame every time), sends KEY=VALUE as the engine's env request (read from
// frame 2), runs the input schedule --steps (romdev input sequence steps: [{"input":{"ports":[{...}]},"frames":N}],
// one request), writes the frame and shuts the host down. ROMDEV_PORT names the server (default 7331).
import { writeFileSync } from 'node:fs';
import { basename, resolve } from 'node:path';

const args = process.argv.slice(2);
const pos = [], env = [];
let size = '1920x1080', gl = false, steps = null, stop = null;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--size') size = args[++i];
  else if (a === '--gl') gl = true;
  else if (a === '--steps') steps = JSON.parse(args[++i]);
  else if (a === '--stop') { const [field, v] = args[++i].split('='); stop = { field, equals: +v }; } // end the schedule early, then 60 frames more
  else if (/^[A-Z_][A-Z0-9_]*=/.test(a)) env.push(a);
  else pos.push(a);
}
const [cart, out] = pos.map((p) => resolve(p));
if (!cart || !out || !steps) { console.error('usage: shot.mjs <cart.wasc> <out.png> --steps <json> [--size WxH] [--gl] [KEY=VALUE ...]'); process.exit(2); }
const [w, h] = size.split('x').map(Number);
const session = `facet-art-${basename(out)}-${process.pid}`;
async function call(tool, a) {
  const r = await fetch(`http://127.0.0.1:${process.env.ROMDEV_PORT || 7331}/tool/${tool}`, {
    method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': session }, body: JSON.stringify(a) });
  const t = await r.text();
  if (!r.ok || /"error"/.test(t.slice(0, 200))) throw new Error(`romdev ${tool}: ${t.slice(0, 300)}`);
  return JSON.parse(t);
}
try {
  // a frame at another size loads as a player's host does (unseeded): only then does the preferred size set the render scale
  const seeded = size === '1920x1080' ? { deterministicSeed: 1, deterministicStepMs: 1000 / 60 } : {};
  const load = { platform: 'wasmcart', path: cart, ...seeded, gpuPower: 'low-power', wgpu: !gl,
                 preferredSize: { width: w, height: h } };
  if (env.length) load.firstCommand = `env ${env.join(' ')}`;
  await call('loadMedia', load);
  const seq = { op: "sequence", steps };
  if (stop) seq.stopWhen = stop;
  await call("input", seq);
  if (stop) await call("frame", { op: "step", frames: 60 });
  const s = await call('frame', { op: 'screenshot', path: out });
  console.log(`${out}: ${s.width}x${s.height}`);
} finally {
  await call('host', { op: 'shutdown' }).catch(() => {});
}
