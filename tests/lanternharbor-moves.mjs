// Lantern Harbor: every kind of move played through the real UI through romdev, each once on the pad and once with
// the pointer, and each checked against the rules (the match's state hash moved, and the game's count of that seat's
// moves of that kind, by that input, went up by one).
//
//   node tests/lanternharbor-moves.mjs [--gpu gl|webgpu] [--out out/lanternharbor]
//
// Seats: a person on pad 1 and an Easy computer, short waits. The test plays seat 1 as a player who wants to try
// everything: it takes the job whose phase offers a kind of move it still needs, buys and staffs the seed shed and the
// private jetty (their moves come only with them), and otherwise picks the first choice. Each needed kind is entered
// with the input it still lacks: the pad (the d-pad toward the choice's point on screen, then confirm) or the pointer
// (a tap to point at it, a tap again to choose). Up to four matches (seeds 11..14) until every kind has both.
// CONTROLS that must fail: a tap on open water away from every choice, and confirm on the pad while the
// how-to-play card is up, each run through the same "a move was played" check, which must find none.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'lanternharbor-moves', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/lanternharbor');
mkdirSync(OUT, { recursive: true });
const gi = argv.indexOf('--gpu');
const GPU = gi >= 0 ? argv[gi + 1] : 'webgpu';
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };

// rules.h: move kinds, jobs, the two buildings that bring a kind of move with them
const KIND = ['pass', 'job', 'take plot', 'stone pit', 'hidden plot', 'call back', 'staff plot', 'staff building', 'build', 'extra good', 'sell', 'load', 'jetty'];
const M = { PASS: 0, ROLE: 1, TAKE: 2, STONE: 3, SEED: 4, REARRANGE: 5, PLACE_PLOT: 6, PLACE_BLD: 7, BUILD: 8, EXTRA: 9, SELL: 10, LOAD: 11, JETTY: 12 };
const B_SEED_SHED = 7, B_JETTY = 17;
const ROLE_FOR = { [M.TAKE]: 0, [M.STONE]: 0, [M.SEED]: 0, [M.REARRANGE]: 1, [M.PLACE_PLOT]: 1, [M.PLACE_BLD]: 1, [M.BUILD]: 2, [M.EXTRA]: 3, [M.SELL]: 4, [M.LOAD]: 5, [M.JETTY]: 5 };
const PH_PICK = 0, PH_BUILD = 3;
// target_code (main.c): a move type * 100 + a * 4 + b; a good to load 2000 + good; put back 2100
const decode = (c) => c >= 2000 ? { good: c < 2100 ? c - 2000 : -1 } : { type: Math.floor(c / 100), a: Math.floor((c % 100) / 4), b: c % 4 };

const done = { pad: new Set(), pointer: new Set() };
const need = (type) => !done.pad.has(type) ? 'pad' : !done.pointer.has(type) ? 'pointer' : null;
const allDone = () => KIND.every((_, t) => !need(t));

async function match(seed) {
  const h = await openCart('lanternharbor', { gpu: GPU, seed: 1, tag: `moves-${GPU}-${seed}` });
  const k = (h.loaded.fbWidth || 1280) / 1280;
  const st = async () => { const s = await h.state(); const o = {}; for (const [n, v] of Object.entries(s)) if (n.startsWith('game.')) o[n.slice(5)] = v; return o; };
  const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });
  const press = async (button, after = 8) => { const at = await h.now(); await h.timeline([tap(at + 2, button)], at + 2 + after); };
  const point = async (x, y, after = 10) => {
    await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: true }, h.session);
    await h.step(2);
    await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: false }, h.session);
    await h.step(after);
  };
  const mine = (s) => s.turn === 0 && !s.busy && !s.over && !s.help && s.ntargets > 0;
  const settle = async (s, limit = 3000) => { for (let n = 0; !mine(s) && !s.over && n < limit; n += 20) { await h.step(20); s = await st(); } return s; };
  // the pad: the d-pad toward target i's point until the cursor is on it
  const padTo = async (i) => {
    let s = await st();
    for (let n = 0; s.cursor !== i && n < 24; n++) {
      const cx = s.target_px[2 * s.cursor], cy = s.target_px[2 * s.cursor + 1], tx = s.target_px[2 * i], ty = s.target_px[2 * i + 1];
      const dx = tx - cx, dy = ty - cy;
      let dir = Math.abs(dx) >= Math.abs(dy) ? (dx > 0 ? 'right' : 'left') : (dy > 0 ? 'down' : 'up');
      if (n >= 12) dir = ['right', 'down', 'left', 'up'][n % 4]; /* a hop that went nowhere: walk the table */
      await press(dir, 6);
      s = await st();
    }
    return s.cursor === i;
  };
  let last = null; /* the last choice's states, for the failure message */
  // choose target i with `how` ('pad' | 'pointer'); returns whether a move of that type landed with that input
  const choose = async (i, how, type) => {
    const before = await st();
    if (how === 'pad') {
      if (!(await padTo(i))) { check(false, `${GPU}: the pad reached choice ${i} (${KIND[type] ?? 'a good'})`); return false; }
      await press('south', 12);
    } else {
      const x = before.target_px[2 * i], y = before.target_px[2 * i + 1];
      /* a tap on the choice the cursor already holds plays it at once; on another, it points there first */
      if (before.cursor !== i) await point(x, y);
      await point(x, y, 12);
    }
    const after = await st();
    if (type === undefined) return after.mode === 1; /* a good picked to load: the boats are offered next */
    const counts = how === 'pad' ? 'pad_types' : 'pointer_types';
    last = { before, after, counts, type };
    return after.state_hash !== before.state_hash && after[counts][type] === before[counts][type] + 1;
  };
  let s;
  const jetty = { built: false }; /* this match: the private jetty raised */
  try {
    await h.command(`env LANTERNHARBOR_SEATS=p,c0 LANTERNHARBOR_SEED=${seed} LANTERNHARBOR_FAST=1`);
    s = await settle(await st());
    if (seed === 11) {
      /* CONTROL: a tap on open water (top left, away from every choice) */
      const b = s;
      await point(20, 20);
      await point(20, 20, 12);
      s = await st();
      const played = s.state_hash !== b.state_hash || s.pointer_types.some((v, t) => v !== b.pointer_types[t]);
      check(!played && s.pointer_picks === b.pointer_picks, `${GPU} CONTROL: a tap away from every choice plays no move (played ${played}, pointer picks ${b.pointer_picks} -> ${s.pointer_picks}): caught`);
      /* CONTROL: confirm on the pad under the how-to-play card */
      await press('north', 12); /* how_to_play */
      s = await st();
      const helpUp = s.help === 1;
      const b2 = s;
      await press('south', 6);
      s = await st();
      check(helpUp && s.state_hash === b2.state_hash && s.pad_types.every((v, t) => v === b2.pad_types[t]), `${GPU} CONTROL: confirm under the how-to-play card plays no move (card up ${helpUp}): caught`);
      if (s.help) { await press('south', 12); s = await st(); }
      s = await settle(s);
    }
    for (let guard = 0; !s.over && guard < 400 && !allDone(); guard++) {
      if (!mine(s)) { s = await settle(s); continue; }
      const t = Array.from({ length: s.ntargets }, (_, i) => ({ i, ...decode(s.target_code[i]) }));
      const moves = t.filter((x) => x.type !== undefined);
      let pick = null, how = 'pad';
      const want = (x) => x.type !== undefined && need(x.type);
      // a needed kind on the table now
      pick = moves.find((x) => x.type !== undefined && x.type !== M.PASS && want(x));
      // the buildings that bring the hidden plot and the jetty, and a place in them
      pick ??= moves.find((x) => (x.type === M.BUILD || x.type === M.PLACE_BLD) && ((x.a === B_SEED_SHED && need(M.SEED)) || (x.a === B_JETTY && need(M.JETTY))));
      // saving for the private jetty: no other building while it is still needed
      if (!pick && s.phase === PH_BUILD && need(M.JETTY)) pick = moves.find((x) => x.type === M.PASS);
      // a job whose phase offers a needed kind; with only the jetty left, the jobs that raise, staff and pay for it
      if (!pick && s.phase === PH_PICK) {
        const others = KIND.some((_, ty) => ty !== M.JETTY && need(ty));
        const order = others ? [...new Set(KIND.map((_, ty) => need(ty) && ROLE_FOR[ty] !== undefined ? ROLE_FOR[ty] : -1))] : jetty.built ? [1, 5, 4, 6, 3, 2, 0] : [2, 4, 6, 1, 5, 3, 0];
        for (const r of order) if (!pick) pick = moves.find((x) => x.type === M.ROLE && x.a === r);
      }
      // a good to load when a boat or the jetty is needed
      if (!pick && s.mode === 0 && t.some((x) => x.good >= 0) && (need(M.LOAD) || need(M.JETTY))) pick = t.find((x) => x.good >= 0);
      if (!pick && need(M.PASS)) pick = moves.find((x) => x.type === M.PASS);
      pick ??= moves[0] ?? t.find((x) => x.good >= 0) ?? t[0];
      if (pick.type !== undefined) how = need(pick.type) ?? 'pad';
      else how = s.mode === 0 ? 'pad' : 'pointer';
      const ok = await choose(pick.i, how, pick.type);
      if (ok && pick.type === M.BUILD && pick.a === B_JETTY) jetty.built = true;
      if (pick.type !== undefined && need(pick.type) === how) {
        check(ok, `${GPU} seed ${seed}: ${KIND[pick.type]} with the ${how} changed the rules state and counted once`);
        if (ok) done[how].add(pick.type);
        if (ok && done.pad.has(pick.type) && done.pointer.has(pick.type)) await h.shot(join(OUT, `${GPU}-move-${String(pick.type).padStart(2, '0')}-${KIND[pick.type].replace(/ /g, '-')}.png`));
      } else if (!ok && pick.type !== undefined) {
        const d = last && last.after[last.counts].map((v, ty) => v - last.before[last.counts][ty]).map((v, ty) => v ? `${KIND[ty]} +${v}` : '').filter(Boolean).join(', ');
        check(false, `${GPU} seed ${seed}: ${KIND[pick.type]} (choice ${pick.i}) with the ${how} did not land (cursor ${last?.before.cursor} -> ${last?.after.cursor}, counted: ${d || 'nothing'}, phase ${last?.before.phase} -> ${last?.after.phase})`);
      }
      s = await settle(await st());
    }
  } finally {
    await h.close();
  }
}

for (let seed = 11; seed <= 14 && !allDone(); seed++) await match(seed);
for (let t = 0; t < KIND.length; t++) check(done.pad.has(t) && done.pointer.has(t), `${GPU}: ${KIND[t]} played on the pad (${done.pad.has(t)}) and with the pointer (${done.pointer.has(t)})`);
console.log(failed ? `${failed} failed` : 'lanternharbor moves: all passed');
process.exit(failed ? 1 : 0);
