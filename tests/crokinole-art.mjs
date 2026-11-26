// crokinole's frames for the launcher art and the README (games/crokinole/tools/make-art.sh composes them), taken
// through romdev from the real cart: CPU matches stepped to a good moment, each frame once with the HUD (the labelled
// screenshots) and the key frame again without it (cover, background, title, icon). Writes out/art/*.png.
//
//   node tests/crokinole-art.mjs [gl|webgpu]
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { openCart, call } from './lib/romdev.mjs';

const gpu = process.argv[2] || 'gl';
mkdirSync('out/art', { recursive: true });

/* one cart run: env, then frames stepped (or until a field reaches a value), then a screenshot */
async function take(file, env, { frames = 0, until = null, after = 0 }) {
  const h = await openCart('crokinole_auto', { gpu, tag: `crok-art-${file}`, load: { firstCommand: `env ${env}` } });
  try {
    if (frames) await h.step(frames - h.frame);
    if (until) {
      const r = await call('input', { op: 'sequence', steps: [{ input: { ports: [{}] }, frames: 30000 }], stopWhen: until, fields: [until.field] }, h.session, { timeoutMs: 3600000 });
      if (!r.stopped) throw new Error(`${file}: ${until.field} never reached ${JSON.stringify(until)}`);
    }
    if (after) await h.step(after);
    await h.shot(resolve(`out/art/${file}.png`));
    const st = await h.state();
    console.log(file, JSON.stringify({ phase: st['game.phase'], shots: st['game.shots'], onboard: st['board.discs'], score: st['game.score'] }));
  } finally { await h.close(); }
}

const TWO = 'CRK_SEATS=c2:c1 CRK_SEED=21';
const FOUR = 'CRK_SEATS=c0:c0:c0:c0 CRK_SEED=8';
/* mid-round, a CPU lining up with the board full of discs */
await take('play-1', TWO, { frames: 3600, until: { field: 'aim.charging', equals: 1 }, after: 40 });
await take('key-1', `${TWO} CRK_NO_HUD=1`, { frames: 3600, until: { field: 'aim.charging', equals: 1 }, after: 40 });
/* four seats, two teams (Easy hands leave more discs about), late in the round */
await take('play-2', FOUR, { frames: 6000, until: { field: 'aim.charging', equals: 1 }, after: 30 });
await take('key-2', `${FOUR} CRK_NO_HUD=1`, { frames: 6000, until: { field: 'aim.charging', equals: 1 }, after: 30 });
/* a round counted */
await take('play-3', TWO, { until: { field: 'game.phase', equals: 3 }, after: 40 });
/* three seats, each its own colour, lining up late in the round */
await take('play-4', 'CRK_SEATS=c1:c0:c0 CRK_SEED=5', { frames: 5200, until: { field: 'aim.charging', equals: 1 }, after: 30 });
