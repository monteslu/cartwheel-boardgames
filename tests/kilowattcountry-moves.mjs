// Kilowatt Country: every move a person can make, played through the real input on romdev, on the pad AND the pointer.
//
//   node tests/kilowattcountry-moves.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/kilowattcountry-moves]
//
// Two hot seats (both people, the free pad drives whoever is to move; seed 5, short beats). At each of their choices
// the driver looks at what the table offers (kc.target_kind) and picks a kind of move that still lacks a pad or a
// pointer play: open an auction, pass, raise, drop out, scrap a plant, buy fuel, done, build. The pad walks the
// cursor there with the direction actions and confirms; the pointer taps the target (the cursor goes there), then taps
// it again (a fuel row opens the count, whose chip the cursor starts on, and a tap there buys). After each move the rules state must show it:
//   open: an auction for a plant (or the plant bought outright); pass: the seat's turn ends; raise: the high bid goes
//   up; drop: the seat leaves the auction; scrap: the seat keeps three plants; fuel: the seat holds more fuel and has
//   less money; done: the turn or the phase moves on; build: one more city and less money.
// Every kind must be played at least once through each input before round 10.
// CONTROL that must fail: KILOWATTCOUNTRY_TEST_HIDE=raise (the rules allow raising but the table never offers it):
// raise is never played, by either input.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'kilowattcountry-moves', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/kilowattcountry-moves');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; return ok; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });
const KIND = ['', 'open', 'pass', 'raise', 'drop', 'discard', 'fuel', 'done', 'build'];
const K = Object.fromEntries(KIND.map((n, i) => [n, i]));
/* the rarest first: a kind that comes up seldom is taken whenever it is offered */
const PREFER = [K.discard, K.raise, K.drop, K.pass, K.build, K.open, K.fuel, K.done];

async function drive(gpu, { control = false, maxRound = 10 } = {}) {
  const h = await openCart('kilowattcountry', { gpu, seed: 1, tag: `${gpu}-moves${control ? '-control' : ''}` });
  const st = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('kc.')) o[k.slice(3)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  const k = (h.loaded.fbWidth || 1280) / 1280; /* canvas px (1280 wide) to the cart's pixels */
  /* step in ranges until a person may choose (nothing animating) */
  const settle = async (limit = 3000) => {
    let s = await st();
    for (let n = 0; n < limit && (s.busy || s.help || s.turn < 0) && !s.over; n += 30) { await h.step(30); s = await st(); }
    return s;
  };
  const press = async (button, frames = 10) => { const at = await h.now(); await h.timeline([tap(at + 2, button)], at + frames); };
  const tapAt = async (i, frames = 10) => {
    const px = (await st()).target_px;
    const x = Math.round(px[2 * i] * k), y = Math.round(px[2 * i + 1] * k);
    await call('input', { op: 'pointer', x, y, left: true }, h.session);
    await h.step(2);
    await call('input', { op: 'pointer', x, y, left: false }, h.session);
    await h.step(frames);
  };
  /* the pad: walk the cursor to target i with the direction actions */
  const walk = async (i) => {
    for (let n = 0; n < 16; n++) {
      const s = await st();
      if (s.cursor === i) return true;
      const px = s.target_px, c = s.cursor;
      const dx = px[2 * i] - px[2 * c], dy = px[2 * i + 1] - px[2 * c + 1];
      const order = Math.abs(dx) >= Math.abs(dy) ? [dx > 0 ? 'right' : 'left', dy > 0 ? 'down' : 'up'] : [dy > 0 ? 'down' : 'up', dx > 0 ? 'right' : 'left'];
      await press(order[0]);
      if ((await st()).cursor === c && (dx && dy)) await press(order[1]);
    }
    return (await st()).cursor === i;
  };
  const used = { pad: new Array(9).fill(0), pointer: new Array(9).fill(0) };
  const offered = new Array(9).fill(0);
  try {
    const sw = `env KILOWATTCOUNTRY_SEATS=h,h KILOWATTCOUNTRY_SEED=5 KILOWATTCOUNTRY_FAST=1${control ? ' KILOWATTCOUNTRY_TEST_HIDE=raise' : ''}`;
    await h.command(sw);
    await h.step(90);
    let s = await st();
    if (s.help) { await press('south', 20); s = await st(); }
    check(s.help === 0 && s.round === 1, `${gpu}: the match is open, the card closed (round ${s.round})`);
    let shots = 0;
    for (let iter = 0; iter < 600; iter++) {
      s = await settle();
      if (s.over || s.round > maxRound) break;
      const seat = s.turn, n = s.ntargets;
      const kinds = s.target_kind.slice(0, n);
      for (const kd of new Set(kinds)) offered[kd]++;
      /* the kind to play: the rarest one offered that still lacks an input; else whatever the cursor starts on */
      let want = PREFER.find((kd) => kinds.includes(kd) && (!used.pad[kd] || !used.pointer[kd]));
      let target = want ? kinds.indexOf(want) : s.cursor;
      if (want === K.done && s.phase === 3 && kinds.includes(K.build) && (used.pad[K.build] < 3 || used.pointer[K.build] < 3)) target = kinds.indexOf(want = K.build);
      if (!want) want = kinds[target];
      const via = !used.pad[want] ? 'pad' : !used.pointer[want] ? 'pointer' : iter % 2 ? 'pad' : 'pointer';
      const before = s;
      if (via === 'pad') {
        if (!check(await walk(target), `${gpu}: the pad walks the cursor to a ${KIND[want]} target`)) break;
        await press('south', 8);
      } else { /* a tap moves the cursor there; a tap on the cursor's target confirms it */
        if (s.cursor !== target) await tapAt(target);
        await tapAt(target, 8);
      }
      if (want === K.fuel) { /* the count: confirm where it starts (pad) or tap its chip twice (pointer) */
        const c = await st();
        if (!check(c.mode === 1, `${gpu}: the fuel row opens its count (mode ${c.mode}, by ${via})`)) break;
        if (via === 'pad') await press('south', 8);
        else await tapAt(c.cursor, 8);
      }
      const after = await settle();
      const cnt = after[`${via}_kinds`][want];
      const b = 1 << seat;
      const effect = {
        [K.open]: after.auc_plant > 0 || after.nplants[seat] > before.nplants[seat] || after.phase !== before.phase,
        [K.pass]: after.turn !== seat || after.phase !== before.phase,
        [K.raise]: after.auc_bid > before.auc_bid || after.nplants[seat] > before.nplants[seat],
        [K.drop]: !(after.auc_in & b),
        [K.discard]: after.nplants[seat] === 3,
        [K.fuel]: after.stored[seat] > before.stored[seat] && after.money[seat] < before.money[seat],
        [K.done]: after.turn !== seat || after.phase !== before.phase,
        [K.build]: after.cities[seat] === before.cities[seat] + 1 && after.money[seat] < before.money[seat],
      }[want];
      const fresh = !used[via][want];
      used[via][want] = cnt;
      if (!check(after.decisions === before.decisions + 1 && after.last_kind === want && cnt >= 1 && effect,
        `${gpu}: ${KIND[want]} by ${via} for seat ${seat} (round ${before.round}): decisions ${before.decisions} to ${after.decisions}, last ${KIND[after.last_kind]}, rules ${effect ? 'changed as expected' : 'NOT changed'}`)) break;
      if (fresh && shots < 20) { shots++; await shot(`${String(shots).padStart(2, '0')}-${KIND[want]}-${via}`); }
      if (KIND.slice(1).every((_, i) => used.pad[i + 1] && used.pointer[i + 1])) break;
    }
    const missing = [];
    for (let kd = 1; kd <= 8; kd++) for (const via of ['pad', 'pointer']) if (!used[via][kd]) missing.push(`${KIND[kd]} by ${via}`);
    if (control) check(!used.pad[K.raise] && !used.pointer[K.raise] && offered[K.raise] === 0 && used.pad[K.open] + used.pointer[K.open] > 0,
      `CONTROL ${gpu}: with raise left off the table it is never played (pad ${used.pad[K.raise]}, pointer ${used.pointer[K.raise]}, offered ${offered[K.raise]}): caught`);
    else check(missing.length === 0, `${gpu}: every move kind played through the pad and the pointer${missing.length ? ` (missing: ${missing.join(', ')})` : ''}`);
    console.log(`${gpu}${control ? ' control' : ''}: pad ${KIND.slice(1).map((n, i) => `${n} ${used.pad[i + 1]}`).join(', ')}; pointer ${KIND.slice(1).map((n, i) => `${n} ${used.pointer[i + 1]}`).join(', ')}`);
  } finally {
    await h.close();
  }
}

for (const gpu of gpusArg(process.argv)) await drive(gpu);
await drive(gpusArg(process.argv)[0], { control: true, maxRound: 3 });
console.log(failed ? `${failed} failed` : 'kilowattcountry moves: all passed');
process.exit(failed ? 1 : 0);
