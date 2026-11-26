// Smalti: every kind of move through the real input, on the pad AND the pointer, in the cart through romdev.
//
//   node tests/smalti-moves.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/smalti-moves]
//
// A turn is two choices: a group (from a tray or from the centre) and where it goes (a staging row or straight to
// the breakage strip), so there are four kinds of move. Seats: a person on pad 1 and an Easy computer, bag seed 5.
// On each of the person's turns the test takes a kind it has not yet driven with that input and enters it:
//   - the pad: reads where the cursor and the wanted target sit on the screen, hops the d-pad toward it (one hop a
//     call, as a player looks after each hop), then presses choose;
//   - the pointer: taps the target (the cursor goes there), taps it again (chosen).
// After each move the rules' count of that kind (game.person_kinds) must have gone up by one and no other.
// CONTROL that must fail: the pad confirms the ghost's own row while the check asks for the breakage strip; the
// strip's count does not move, so the check must report it.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'smalti-moves', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/smalti-moves');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

const CENTRE = 9, FLOOR = 5;
const KINDS = ['tray to a row', 'tray to the strip', 'centre to a row', 'centre to the strip'];
const kindOf = (centre, floor) => (centre ? 2 : 0) + (floor ? 1 : 0);
// a move of kind k was made: its count went up by one and no other count moved
const drove = (before, after, k) => after.every((v, i) => v === before[i] + (i === k ? 1 : 0));

async function run(gpu) {
  const h = await openCart('smalti', { gpu, seed: 1, tag: `${gpu}-moves` });
  const st = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('game.')) o[k.slice(5)] = v; return o; };
  const untilMine = async (limit = 4000) => {
    let s = await st();
    for (let n = 0; (s.players !== 2 || s.ntargets === 0 || s.turn !== 0 || s.busy || s.over || s.mode !== 0) && n < limit; n += 30) { await h.step(30); s = await st(); }
    return s;
  };
  const k = (h.loaded.fbWidth || 1280) / 1280;
  const pt = async (x, y, frames = 16) => {
    await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: true }, h.session);
    await h.step(2);
    await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: false }, h.session);
    await h.step(frames);
  };
  const press = async (button, frames = 14) => { const at = await h.now(); await h.timeline([tap(at + 2, button)], at + frames); };
  // the pad: hop toward target i by what the screen shows, then choose
  const padTo = async (i) => {
    let s = await st();
    for (let hop = 0; hop < 30 && s.cursor !== i; hop++) {
      const cx = s.target_px[2 * s.cursor], cy = s.target_px[2 * s.cursor + 1], tx = s.target_px[2 * i], ty = s.target_px[2 * i + 1];
      const dx = tx - cx, dy = ty - cy;
      const ways = Math.abs(dx) >= Math.abs(dy) ? [dx > 0 ? 'right' : 'left', dy > 0 ? 'down' : 'up'] : [dy > 0 ? 'down' : 'up', dx > 0 ? 'right' : 'left'];
      const was = s.cursor;
      await press(ways[0]);
      s = await st();
      if (s.cursor === was) { await press(ways[1]); s = await st(); }
      if (s.cursor === was) break;
    }
    if (s.cursor !== i) return false;
    await press('south', 24);
    return true;
  };
  const enter = async (how, i) => {
    if (how === 'pad') return padTo(i);
    /* a first tap points the cursor there (or chooses at once when it already stood there), a second chooses */
    const s = await st();
    await pt(s.target_px[2 * i], s.target_px[2 * i + 1], 30);
    const t = await st();
    if (t.mode === s.mode && t.moves === s.moves) await pt(s.target_px[2 * i], s.target_px[2 * i + 1], 30);
    return true;
  };
  try {
    await h.command('env SMALTI_SEATS=p,c0 SMALTI_SEED=5 SMALTI_FAST=1');
    const todo = [];
    for (const how of ['pad', 'pointer']) for (let kd = 0; kd < 4; kd++) todo.push({ how, kd });
    let controlDone = false;
    for (let turn = 0; turn < 30 && todo.length; turn++) {
      let s = await untilMine();
      if (s.turn !== 0 || s.mode !== 0 || s.over) { check(false, `${gpu}: seat 1's turn came (turn ${s.turn}, mode ${s.mode}, over ${s.over})`); break; }
      // the control first, once: a pad move onto the ghost's row, checked as if it were the strip
      if (!controlDone) {
        const g = s.target_code.findIndex((c, i) => i < s.ntargets && c >= 0 && Math.floor(c / 16) !== CENTRE);
        const before = [...s.person_kinds];
        await enter('pad', g);
        s = await st();
        const row = s.target_code.findIndex((c, i) => i < s.ntargets && c >= 1000 && c < 1000 + FLOOR);
        if (s.cursor !== row) await padTo(row); else await press('south', 24);
        s = await st();
        check(drove(before, s.person_kinds, kindOf(false, false)), `${gpu}: the control's move went onto a row (${before} -> ${s.person_kinds})`);
        check(!drove(before, s.person_kinds, kindOf(false, true)), `${gpu} CONTROL: a row move checked as the strip is reported: caught`);
        controlDone = true;
        continue;
      }
      const centreHas = s.target_code.some((c, i) => i < s.ntargets && c >= 0 && Math.floor(c / 16) === CENTRE);
      const ti = todo.findIndex((t) => t.kd < 2 || centreHas);
      if (ti < 0) { /* only centre kinds left and the centre is empty: take any tray group onto a row */
        await enter('pad', s.target_code.findIndex((c, i) => i < s.ntargets && c >= 0));
        s = await st();
        await press('south', 24);
        continue;
      }
      const { how, kd } = todo[ti];
      const centre = kd >= 2, floor = kd % 2 === 1;
      const g = s.target_code.findIndex((c, i) => i < s.ntargets && c >= 0 && (Math.floor(c / 16) === CENTRE) === centre);
      const before = [...s.person_kinds];
      const okPick = await enter(how, g);
      s = await st();
      check(okPick && s.mode === 1, `${gpu} ${how}: took a group from ${centre ? 'the centre' : 'a tray'} (mode ${s.mode})`);
      const dest = s.target_code.findIndex((c, i) => i < s.ntargets && (floor ? c === 1000 + FLOOR : c >= 1000 && c < 1000 + FLOOR));
      const okPlace = dest >= 0 && await enter(how, dest);
      s = await st();
      const ok = okPlace && drove(before, s.person_kinds, kd);
      check(ok, `${gpu} ${how}: ${KINDS[kd]} (kinds ${before} -> ${s.person_kinds})`);
      if (ok) {
        if (how === 'pointer' && kd === 3) await h.shot(join(OUT, `${gpu}-pointer-strip.png`));
        todo.splice(ti, 1);
      } else break;
    }
    check(todo.length === 0, `${gpu}: every kind of move entered with the pad and with the pointer (${todo.length} left: ${todo.map((t) => `${t.how} ${KINDS[t.kd]}`).join(', ')})`);
    await h.shot(join(OUT, `${gpu}-end.png`));
  } finally {
    await h.close();
  }
}

for (const gpu of gpusArg(process.argv)) await run(gpu);
console.log(failed ? `${failed} failed` : 'smalti moves: all passed');
process.exit(failed ? 1 : 0);
