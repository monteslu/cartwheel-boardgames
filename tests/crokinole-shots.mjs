// crokinole's look shots through romdev: the how-to card, a person's aim, the charge with its ghost ring, a shot in
// flight (out/shots/look2-<gpu>-*.png). Pictures to look at, not a gate.
//
//   node tests/crokinole-shots.mjs [gl|webgpu]
import { mkdirSync } from 'node:fs';
import { openCart, call } from './lib/romdev.mjs';
const gpu = process.argv[2] || 'gl';
mkdirSync('out/shots', { recursive: true });
const h = await openCart('crokinole', { gpu, tag: 'crok-look2', load: { firstCommand: 'env CRK_SEATS=p:c1 CRK_TARGET=1 CRK_SEED=4' } });
const seq = (steps) => call('input', { op: 'sequence', steps: steps.map(([b, n]) => ({ input: { ports: [b ? { [b]: true } : {}] }, frames: n })) }, h.session);
try {
  await seq([[null, 30]]);
  await h.shot(`out/shots/look2-${gpu}-howto.png`);
  await seq([['south', 4], [null, 40], ['l', 20], [null, 10]]);
  await h.shot(`out/shots/look2-${gpu}-aim.png`);
  await seq([['south', 70]]);
  await h.shot(`out/shots/look2-${gpu}-charge.png`);
  await seq([[null, 30]]);
  await h.shot(`out/shots/look2-${gpu}-flight.png`);
} finally { await h.close(); }
