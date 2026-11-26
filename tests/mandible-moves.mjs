// Mandible: every kind of move through the real input, on the pad AND the pointer, in the cart through romdev.
//
//   node tests/mandible-moves.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/mandible-moves]
//
// The kinds: place a piece from the hand, step a piece on the table, climb onto a stack, and pass (only when
// nothing else is legal). Seats: people on pads 1 and 2. The debug command `setup` puts a test position on the
// table with Ivory to choose ("moves": a Ram beside Ivory's Empress, which may slide or climb onto her, and a hand
// to place from; "pass": Ivory's one Ram ringed by Ebony's pieces, its Empress still in hand). For each kind and
// each input the test sets the position up and enters the move:
//   - the pad: the shoulder steps the cursor through the lit picks (one press a call, as a player looks), choose
//     takes the piece, the same for the lit space;
//   - the pointer: a tap on the piece, a tap on the space.
// After each move the count of that kind (game.person_kinds) must have gone up by one and no other.
// CONTROL that must fail: a pad step checked as a climb; the climb count does not move, so the check must report it.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'mandible-moves', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/mandible-moves');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

const PLACE = 0, STEP = 1, CLIMB = 2, PASS = 3;
const KINDS = ['place from the hand', 'step on the table', 'climb onto a stack', 'pass'];
const drove = (before, after, k) => after.every((v, i) => v === before[i] + (i === k ? 1 : 0));

async function run(gpu) {
  const h = await openCart('mandible', { gpu, seed: 1, tag: `${gpu}-moves` });
  const g = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('game.')) o[k.slice(5)] = v; return o; };
  const k = (h.loaded.fbWidth || 1280) / 1280;
  const press = async (button, frames = 12) => { const at = await h.now(); await h.timeline([tap(at + 2, button)], at + frames); };
  const pick = async (how, i) => {
    if (how === 'pad') {
      let s = await g();
      for (let n = 0; n < 40 && s.cursor !== i; n++) { await press('r'); s = await g(); }
      if (s.cursor !== i) return false;
      await press('south', 20);
      return true;
    }
    const px = (await g()).pick_px;
    const x = Math.round(px[2 * i] * k), y = Math.round(px[2 * i + 1] * k);
    await call('input', { op: 'pointer', x, y, left: true }, h.session);
    await h.step(2);
    await call('input', { op: 'pointer', x, y, left: false }, h.session);
    await h.step(20);
    return true;
  };
  // the first pick whose code passes `want`
  const find = (s, want) => s.pick_code.findIndex((c, i) => i < s.picks && c >= 0 && want(c));
  try {
    await h.command('env MANDIBLE_SEATS=p:p MANDIBLE_SEED=7');
    await h.step(60);
    let s = await g();
    check(s.phase === 1 && s.turn === 0, `${gpu}: a match of two people is in play, Ivory to move (phase ${s.phase}, turn ${s.turn})`);
    const enter = async (how, kind) => {
      await h.command(`setup ${kind === PASS ? 'pass' : 'moves'}`);
      await h.step(10);
      s = await g();
      const before = [...s.person_kinds];
      const src = find(s, kind === PASS ? (c) => c === 200 : kind === PLACE ? (c) => c >= 100 && c < 200 : (c) => c < 100);
      if (src < 0 || !(await pick(how, src))) return { ok: false, before, after: before, why: `no lit piece for it (${s.pick_code.slice(0, s.picks)})` };
      if (kind !== PASS) {
        s = await g();
        const dst = find(s, (c) => c === 1000 + kind);
        if (s.phase !== 2 || dst < 0 || !(await pick(how, dst))) return { ok: false, before, after: before, why: `no lit space for it (phase ${s.phase}, ${s.pick_code.slice(0, s.picks)})` };
      }
      await h.step(40);
      s = await g();
      return { ok: drove(before, s.person_kinds, kind), before, after: s.person_kinds, why: '' };
    };
    for (const how of ['pad', 'pointer'])
      for (const kind of [PLACE, STEP, CLIMB, PASS]) {
        const r = await enter(how, kind);
        check(r.ok, `${gpu} ${how}: ${KINDS[kind]} (kinds ${r.before} -> ${r.after}) ${r.why}`);
        if (kind === CLIMB) await h.shot(join(OUT, `${gpu}-${how}-climb.png`));
      }
    /* the control: a step, checked as a climb */
    const r = await enter('pad', STEP);
    check(r.ok, `${gpu}: the control's move was a step (kinds ${r.before} -> ${r.after})`);
    check(!drove(r.before, r.after, CLIMB), `${gpu} CONTROL: a step checked as a climb is reported: caught`);
  } finally {
    await h.close();
  }
}

for (const gpu of gpusArg(process.argv)) await run(gpu);
console.log(failed ? `${failed} failed` : 'mandible moves: all passed');
process.exit(failed ? 1 : 0);
