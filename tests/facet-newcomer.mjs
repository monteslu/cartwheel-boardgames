// Facet: a newcomer plays a whole game with a pad alone, from what the screen offers, on romdev.
//
// The bot is seat 1 (a person on pad 0) against an Easy guild. It reads only what the table shows the seat to
// move (the cartwheel debug fields facet.*): the targets the cursor may stand on and where they sit on screen
// (the lit piles, cards and plaques), which cards say "You would pay" (facet.afford) and which plaques are lit.
// It moves the cursor with the d-pad the way the cursor hops (cartwheel/cursor.h: the target most nearly the way
// held), picks with the confirm button and steps back with back. It fulfils a card when one is affordable, else
// takes stones, sets a card aside when no take is possible, and answers hand-backs and patrons as they come.
// One romdev request per decision, never one per frame.
//
// Every third turn it takes two of a kind when a pile of four or more allows it (a stone, then the same pile again).
// On each of its looks the glow under the cards must be the rules' fulfillable set (facet.glow_mismatch 0).
// Pass: the match ends (facet.finished 1) with a legal winner, the person made moves, at least two two-of-a-kind takes
// went through, the glow was right on every look, and no press sequence got stuck. The CONTROL (a pad that only moves and never confirms) must fail: it never completes a turn.
//
//   node tests/facet-newcomer.mjs [--gpu gl|webgpu|both] [--cart <path>] [--frames-max N]
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { existsSync } from 'node:fs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const CART = resolve(opt('--cart', join(ROOT, 'build/cart-wgpu/carts/facet.wasc')));
const GPUS = opt('--gpu', 'both') === 'both' ? ['gl', 'webgpu'] : [opt('--gpu', 'webgpu')];
const FRAMES_MAX = +opt('--frames-max', 60000);
const PORT = process.env.ROMDEV_PORT || 7331;
if (!existsSync(CART)) { console.error(`FAIL no cart ${CART} (scripts/build.sh facet)`); process.exit(1); }

const TG = { BANK: 0, CARD: 6, DECK: 18, RES: 21, HAND: 24, PATRON: 30, OK: 40, ALT: 41 };
const FIELDS = ['facet.flow', 'facet.seat', 'facet.turns', 'facet.over', 'facet.winners', 'facet.finished', 'facet.cursor', 'facet.ui_mode',
  'facet.npick', 'facet.ok_on', 'facet.alt_on', 'facet.active_n', 'facet.active', 'facet.active_xy', 'facet.afford', 'facet.points', 'facet.moves', 'facet.players',
  'facet.bank_rules', 'facet.glow_mismatch'];

async function call(session, tool, args) {
  const r = await fetch(`http://127.0.0.1:${PORT}/tool/${tool}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': session }, body: JSON.stringify(args) });
  const t = await r.text();
  let j;
  try { j = JSON.parse(t); } catch { throw new Error(`romdev ${tool}: ${t.slice(0, 300)}`); }
  if (!r.ok || j.error) throw new Error(`romdev ${tool}: ${t.slice(0, 300)}`);
  return j;
}

/* the cursor's hop (cartwheel/cursor.h defaults): of the targets ahead the way held, the lowest along + 2.2 x side */
function hop(pts, from, dx, dy) {
  const len = Math.hypot(dx, dy); dx /= len; dy /= len;
  let best = -1, bs = Infinity;
  for (let i = 0; i < pts.length; i++) {
    if (i === from) continue;
    const vx = pts[i][0] - pts[from][0], vy = pts[i][1] - pts[from][1];
    const along = vx * dx + vy * dy, side = Math.abs(-vx * dy + vy * dx);
    if (along <= 0 || side > 1.75 * along) continue;
    const s = along + 2.2 * side;
    if (s < bs) { bs = s; best = i; }
  }
  return best;
}
const WAYS = [['right', [1, 0]], ['down', [0, 1]], ['left', [-1, 0]], ['up', [0, -1]]];
function wayToward(f, goalId) {
  const ids = f['facet.active'].slice(0, f['facet.active_n']);
  const pts = ids.map((_, i) => [f['facet.active_xy'][2 * i], f['facet.active_xy'][2 * i + 1]]);
  const from = ids.indexOf(f['facet.cursor']), to = ids.indexOf(goalId);
  if (to < 0) return null;
  if (from < 0) return 'right'; /* no cursor yet: any hop places it */
  let best = null, bd = Math.hypot(pts[from][0] - pts[to][0], pts[from][1] - pts[to][1]);
  for (const [name, [dx, dy]] of WAYS) {
    const h = hop(pts, from, dx, dy);
    if (h < 0) continue;
    const d = h === to ? -1 : Math.hypot(pts[h][0] - pts[to][0], pts[h][1] - pts[to][1]);
    if (d < bd) { bd = d; best = name; }
  }
  return best;
}

/* what the newcomer wants now: a target id to stand on and confirm, or 'back' */
function choose(f, turnNo) {
  const ids = new Set(f['facet.active'].slice(0, f['facet.active_n']));
  const mode = f['facet.ui_mode'];
  if (mode === 3) return f['facet.ok_on'] ? TG.OK : [...ids].find((t) => t >= TG.PATRON && t < TG.OK);
  if (mode === 2) return f['facet.ok_on'] ? TG.OK : [...ids].find((t) => t >= TG.HAND && t < TG.PATRON);
  if (mode === 1) return f['facet.ok_on'] ? TG.OK : f['facet.alt_on'] ? TG.ALT : 'back';
  const buy = [...ids].find((t) => t >= TG.CARD && t < TG.HAND && f['facet.afford'][t]);
  if (buy !== undefined) return f['facet.npick'] ? 'back' : buy;
  /* every third turn, two of a kind when a pile of four or more allows it: the first stone, then the same pile again
   * (the cursor stays on it) */
  const twoKind = turnNo % 3 === 0;
  const n = f['facet.npick'], cur = f['facet.cursor'];
  if (twoKind && n === 1 && cur < TG.CARD && ids.has(cur) && f['facet.pick0'] === cur) return cur;
  if (f['facet.ok_on']) return TG.OK;
  if (twoKind && n === 0) {
    const four = [...ids].find((t) => t < TG.CARD && f['facet.bank_rules'][t] >= 4);
    if (four !== undefined) return four;
  }
  const piles = [...ids].filter((t) => t < TG.CARD);
  if (piles.length) return piles[(turnNo + f['facet.npick']) % piles.length];
  if (f['facet.npick']) return 'back';
  /* nothing to take: set a market card aside */
  const card = [...ids].find((t) => t >= TG.CARD && t < TG.DECK);
  return card !== undefined ? card : TG.OK;
}

async function press(session, button) {
  const steps = [{ input: { ports: [{ [button]: true }] }, frames: 2 }, { input: { ports: [{}] }, frames: 4 }];
  const r = await call(session, 'input', { op: 'sequence', steps, fields: FIELDS });
  return r.fields;
}

async function play(gpu, control) {
  const session = `facet-newcomer-${gpu}-${control ? 'control' : 'run'}-${process.pid}`;
  const loaded = await call(session, 'loadMedia', { platform: 'wasmcart', path: CART, deterministicSeed: 1, deterministicStepMs: 1000 / 60, gpuPower: 'low-power', wgpu: gpu === 'webgpu', firstCommand: 'env FACET_SEATS=p,c0 FACET_SEED=7' });
  if (gpu === 'gl' && loaded.gpu !== 'gl') throw new Error(`asked for GL, the cart runs on ${loaded.gpu}`);
  let frames = 0, presses = 0, stuck = 0, myTurns = 0, lastMoves = -1, twoKinds = 0, glowLooks = 0, glowWrong = 0, pick0 = -1;
  try {
    let f = (await call(session, 'input', { op: 'sequence', steps: [{ input: { ports: [{}] }, frames: 90 }], fields: FIELDS })).fields;
    frames += 90;
    while (frames < FRAMES_MAX) {
      if (f['facet.finished'] >= 1) break;
      if (f['facet.flow'] === 0) { f = await press(session, 'south'); frames += 6; continue; } /* the how-to card */
      if (f['facet.seat'] !== 0 || f['facet.flow'] !== 1) { /* the guild's turn: wait for ours (or the end) */
        const r = await call(session, 'input', { op: 'sequence', steps: [{ input: { ports: [{}] }, frames: 1200 }], fields: FIELDS, stopWhen: { field: 'facet.seat', equals: 0 } });
        f = r.fields;
        frames += r.framesRun || 1200;
        continue;
      }
      if (f['facet.moves'] !== lastMoves) { lastMoves = f['facet.moves']; myTurns++; stuck = 0; }
      glowLooks++;
      if (f['facet.glow_mismatch'] !== 0) glowWrong++;
      if (f['facet.npick'] === 0) pick0 = -1;
      f['facet.pick0'] = pick0;
      if (++stuck > 60) throw new Error(`stuck: 60 presses on one turn (mode ${f['facet.ui_mode']}, cursor ${f['facet.cursor']})`);
      const want = control ? null : choose(f, myTurns);
      let button;
      if (control) button = ['right', 'down', 'left', 'up'][presses % 4];
      else if (want === 'back' || want === undefined) button = 'east';
      else if (f['facet.cursor'] === want) {
        button = 'south';
        if (want < TG.CARD && f['facet.npick'] === 0) pick0 = want;
        else if (want < TG.CARD && f['facet.npick'] === 1 && want === pick0) twoKinds++;
      }
      else button = wayToward(f, want) || 'east';
      f = await press(session, button);
      presses++;
      frames += 6;
    }
    return { finished: f['facet.finished'], winners: f['facet.winners'], points: f['facet.points'], turns: f['facet.turns'], myTurns, presses, frames, players: f['facet.players'], twoKinds, glowLooks, glowWrong };
  } finally {
    await call(session, 'host', { op: 'shutdown' }).catch(() => {});
  }
}

let failed = 0;
for (const gpu of GPUS) {
  const t0 = Date.now();
  let r;
  try { r = await play(gpu, false); } catch (e) { console.error(`FAIL facet-newcomer (${gpu}): ${e.message}`); failed++; continue; }
  const legal = r.winners > 0 && r.winners < (1 << r.players);
  if (r.finished !== 1 || !legal || r.myTurns < 10 || r.twoKinds < 2 || r.glowWrong) {
    console.error(`FAIL facet-newcomer (${gpu}): finished ${r.finished}, winners ${r.winners}, my turns ${r.myTurns}, two-of-a-kind takes ${r.twoKinds}, glow wrong on ${r.glowWrong} of ${r.glowLooks} looks`);
    failed++;
  } else console.log(`ok   facet-newcomer (${gpu}): a full game in ${r.turns} turns, ${r.myTurns} of them the newcomer's, ${r.presses} presses, points ${r.points.slice(0, 2).join('/')}, winners ${r.winners}, ${r.twoKinds} two-of-a-kind takes, the glow right on all ${r.glowLooks} looks (${((Date.now() - t0) / 1000).toFixed(0)} s)`);
  /* the control: a pad that never confirms must not get a single turn done */
  let c;
  try { c = await play(gpu, true); } catch (e) { c = { error: e.message }; }
  if (c.error || c.myTurns <= 1) console.log(`CONTROL facet-newcomer (${gpu}): a pad that never confirms finishes no turn: caught`);
  else { console.error(`FAIL facet-newcomer (${gpu}): CONTROL PASSED (${JSON.stringify(c)})`); failed++; }
}
process.exit(failed ? 1 : 0);
