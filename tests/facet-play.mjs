// Facet played through romdev on GL and on WebGPU, as a person plays it, with pictures to look at.
//
//   node tests/facet-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/facet]
//
// One run per GPU (seats: a person on pad 1 and an Easy guild, seed 7, two seats: every pile starts at four):
//   - the title (a load of its own, no switch): "How to play" and "Credits" open their pages (the shell's screen 6) and
//     back returns to the title; then the seats switch starts the match from the shell's menu tick;
//   - the how-to-play card is up at the start of play and confirm closes it;
//   - take two of a kind on the pad: confirm on a pile of four, the cursor stays on that pile, confirm again: the
//     take is whole (the cursor goes to Take), confirm: the rules give the seat two of that colour;
//   - the Easy guild plays its turn through the pad's actions;
//   - take two of a kind with the pointer: tap a pile (the cursor goes there), tap it again (one stone), again (two),
//     tap Take: two of that colour;
//   - the cursor after a stone joins a three-colour take: a pile that cannot give another stands on the nearest pile
//     that can, never the first one;
//   - the mats read stones, cards, buys from the top; the prompt offers back only once something is picked;
//   - stepping back: two stones picked, back twice: nothing picked and the rules state as at the turn's start; a right
//     click puts a stone back; a tap on a picked stone puts that stone back;
//   - the glow: on every one of the person's turns the cards lit under are exactly the ones the rules' legal moves can
//     fulfil (none on the guild's turns), and once one is lit its halo shines (a picture);
//   - `replay`: the match so far replayed from its own input recording reaches the same state and input hash.
// Both GPUs end in the same state hash at the same frame. CONTROLS that must fail: back pressed once instead of twice
// leaves a stone picked; a mat with the cards row first (FACET_TEST_ROWS_OLD=1) is out of order; a cursor that goes to the first target (FACET_TEST_CURSOR_FIRST=1) is not on the nearest
// pile; a glow on every card (FACET_TEST_GLOW_ALL=1) disagrees with the rules; the replay with
// CW_TEST_SET_UNRECORDED=1 (the guild's presses left out) and with CW_TEST_POINTER_UNRECORDED=1 (the pointer left
// out) differs; with no seats switch the title stays.
// Pictures: <out>/<gpu>-<step>.png. Each run takes the GPU lock (tools/with-gpu-suite.mjs) unless already inside one.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'facet-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/facet');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; return ok; };
const caught = (bad, msg) => check(bad, `CONTROL ${msg}: caught`);
const tapB = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });
const TG = { BANK: 0, CARD: 6, OK: 40 };
const NT = 6; /* stones per seat in facet.tok: five colours and topaz */

async function titlePages(gpu) {
  const h = await openCart('facet', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  try {
    await h.step(40);
    let t = await sh();
    check(t.screen === 1, `${gpu}: the title shows with no test switch (screen ${t.screen})`);
    const a0 = await h.now();
    await h.timeline([tapB(a0 + 5, 'down'), tapB(a0 + 20, 'south')], a0 + 50);
    t = await sh();
    check(t.screen === 6 && t.page === 0, `${gpu}: How to play opens its page (screen ${t.screen}, page ${t.page})`);
    await h.shot(join(OUT, `${gpu}-0-how-to-play.png`));
    const a1 = await h.now();
    await h.timeline([tapB(a1 + 5, 'east'), tapB(a1 + 25, 'down'), tapB(a1 + 40, 'south')], a1 + 70);
    t = await sh();
    check(t.screen === 6 && t.page === 1, `${gpu}: Credits opens its page (screen ${t.screen}, page ${t.page})`);
    await h.shot(join(OUT, `${gpu}-0-credits.png`));
    const a2 = await h.now();
    await h.timeline([tapB(a2 + 5, 'east')], a2 + 30);
    t = await sh();
    check(t.screen === 1, `${gpu}: back returns to the title (screen ${t.screen})`);
  } finally {
    await h.close();
  }
}

async function play(gpu, { control = '' } = {}) {
  const h = await openCart('facet', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const chk = (ok, msg) => (control ? ok : check(ok, msg)); /* a control run only reports what it must catch */
  const fc = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('facet.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  const press = async (button, frames = 24) => { const at = await h.now(); await h.timeline([tapB(at + 2, button)], at + frames); return fc(); };
  const k = (h.loaded.fbWidth || 1280) / 1280; /* canvas px (1280 wide) to the cart's pixels */
  const pointer = async (x, y, { right = false, frames = 14 } = {}) => {
    const px = Math.round(x * k), py = Math.round(y * k);
    await call('input', { op: 'pointer', x: px, y: py, left: !right, right }, h.session);
    await h.step(3);
    await call('input', { op: 'pointer', x: px, y: py, left: false, right: false }, h.session);
    await h.step(frames);
    return fc();
  };
  const xyOf = (s, target) => { const i = s.active.slice(0, s.active_n).indexOf(target); return i < 0 ? null : [s.active_xy[2 * i], s.active_xy[2 * i + 1]]; };
  const tapTarget = async (s, target) => { const p = xyOf(s, target); if (!p) throw new Error(`${gpu}: target ${target} is not on the table`); return pointer(p[0], p[1]); };
  const waitMyTurn = async () => {
    for (let i = 0; i < 40; i++) {
      const s = await fc();
      if (s.over || (s.seat === 0 && s.cpu_seat === 0 && s.flow === 1 && s.phase === 0)) return s;
      await h.step(60);
    }
    throw new Error(`${gpu}: the guild never handed the turn back`);
  };
  let glowSeen = false, glowBad = 0, glowChecks = 0;
  const glowCheck = (s) => { glowChecks++; if (s.glow_mismatch !== 0) glowBad++; };
  try {
    const sw = { rows: ' FACET_TEST_ROWS_OLD=1', set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1', cursor: ' FACET_TEST_CURSOR_FIRST=1', glow: ' FACET_TEST_GLOW_ALL=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env FACET_SEED=7' : `env FACET_SEATS=p,c0 FACET_SEED=7${sw}`);
    await h.step(60);
    if (control === 'nostart') {
      const t = (await h.state())['shell.screen'];
      caught(t === 1, `${gpu}: with no seats switch the title stays (screen ${t})`);
      return null;
    }
    let s = await fc();
    if (!control) chk(s.flow === 0 && s.seat === 0, `${gpu}: the how-to-play card is up, seat 1 to move (flow ${s.flow}, seat ${s.seat})`);
    await shot('1-how-to-play');
    s = await press('south', 30);
    if (!control) chk(s.flow === 1 && s.turns === 0 && s.npick === 0, `${gpu}: confirm closes the card without picking (flow ${s.flow}, picks ${s.npick})`);
    /* the mats: the stones row first (under the stacks), then the cards, then the buys */
    const ry = s.mat_row_y, rowsOk = ry[0] > 0 && ry[0] < ry[1] && ry[1] < ry[2];
    if (control === 'rows') { caught(!rowsOk, `${gpu}: a mat with the cards row first is caught (rows at ${ry.map(Math.round)})`); return null; }
    if (!control) chk(rowsOk, `${gpu}: the mat rows read stones, cards, buys from the top (at ${ry.map(Math.round)})`);
    if (!control) caught(s.prompt_back === 0, `${gpu}: with nothing picked the prompt offers no back`);
    if (control === 'glow') {
      /* a person's turn with the market out and nothing to pay with: a glow anywhere disagrees with the rules */
      glowCheck(s);
      caught(glowBad > 0, `${gpu}: a glow under every card disagrees with the rules' legal moves (mismatched targets ${s.glow_mismatch})`);
      return null;
    }

    /* take two of a kind on the pad */
    const c = s.cursor;
    if (!chk(c >= TG.BANK && c < TG.BANK + 5 && s.bank_rules[c] >= 4, `${gpu}: the cursor starts on a pile of four or more (target ${c}, pile ${s.bank_rules[c]})`)) return null;
    s = await press('south');
    glowCheck(s);
    if (!control) chk(s.prompt_back === 1, `${gpu}: with a stone picked the prompt offers back to put it back`);
    if (!control) chk(s.npick === 1 && s.cursor === c, `${gpu}: one stone picked, the cursor stays on its pile for a second of the same (picks ${s.npick}, cursor ${s.cursor}, pile ${c})`);
    s = await press('south');
    if (!control) chk(s.npick === 2 && s.cursor === TG.OK && s.ok_on === 1, `${gpu}: a second of the same colour makes the take whole, the cursor on Take (picks ${s.npick}, cursor ${s.cursor})`);
    await shot('2-take-two');
    s = await press('south', 40);
    if (!control) chk(s.turns === 1 && s.tok[c] === 2, `${gpu}: the pad took two of colour ${c} (turns ${s.turns}, stones ${s.tok.slice(0, NT)})`);

    /* the guild's turn */
    s = await waitMyTurn();
    if (!control) chk(s.turns === 2, `${gpu}: the Easy guild played its turn (turns ${s.turns})`);
    await shot('3-after-guild');
    glowCheck(s);

    /* take two of a kind with the pointer */
    const d = [0, 1, 2, 3, 4].find((x) => x !== c && s.bank_rules[x] >= 4 && s.tok[x] === 0);
    if (!chk(d !== undefined, `${gpu}: a pile of four or more for the pointer (piles ${s.bank_rules})`)) return null;
    const had = s.tok[d];
    s = await tapTarget(s, TG.BANK + d);
    if (!control) chk(s.cursor === TG.BANK + d && s.npick === 0, `${gpu}: a tap on a pile points at it (cursor ${s.cursor}, picks ${s.npick})`);
    s = await tapTarget(s, TG.BANK + d);
    s = await tapTarget(s, TG.BANK + d);
    if (!control) chk(s.npick === 2 && s.ok_on === 1, `${gpu}: two more taps pick two of colour ${d} (picks ${s.npick})`);
    s = await tapTarget(s, TG.OK); /* the cursor already stands on Take: a tap there confirms */
    if (s.turns === 2) s = await tapTarget(s, TG.OK);
    await h.step(30);
    s = await fc();
    if (!control) chk(s.turns === 3 && s.tok[d] === had + 2, `${gpu}: the pointer took two of colour ${d} (turns ${s.turns}, stones ${s.tok.slice(0, NT)})`);

    s = await waitMyTurn();
    glowCheck(s);
    const hash0 = [s.hash_lo, s.hash_hi], turns0 = s.turns;
    /* a three-colour take: a pile under four cannot give a second, so the cursor goes to the nearest pile that can */
    /* the lowest such pile but one, so the first pile on the table is not also the nearest (the control tells them apart) */
    const e = [4, 3, 2, 1].find((x) => s.bank_rules[x] > 0 && s.bank_rules[x] < 4 && xyOf(s, TG.BANK + x) && [0, 1, 2, 3, 4].some((y) => y < x - 1 && s.bank_rules[y] > 0));
    if (e !== undefined) {
      s = await tapTarget(s, TG.BANK + e); /* points at it (or picks it, when the cursor already stood there) */
      if (s.npick === 0) s = await tapTarget(s, TG.BANK + e);
      const act = s.active.slice(0, s.active_n).filter((t) => t >= TG.BANK && t < TG.BANK + 5);
      const best = Math.min(...act.map((t) => Math.abs(t - e)));
      const ok = s.npick === 1 && !act.includes(TG.BANK + e) && Math.abs(s.cursor - e) === best;
      if (control === 'cursor') caught(!ok, `${gpu}: a cursor sent to the first target is not on the nearest pile (cursor ${s.cursor}, pile ${e}, nearest ${best} away)`);
      else if (!control) chk(ok, `${gpu}: after a stone from pile ${e} (${s.bank_rules[e]} left) the cursor stands on the nearest pile that can still give one (cursor ${s.cursor}, piles open ${act})`);
      s = await press('east');
      if (!control) chk(s.npick === 0 && s.cursor === TG.BANK + e, `${gpu}: back puts it back and the cursor returns to its pile (picks ${s.npick}, cursor ${s.cursor})`);
    } else if (!control) chk(false, `${gpu}: no pile under four to test the cursor on (piles ${s.bank_rules})`);
    if (control === 'cursor') return null;

    /* stepping back: two stones, back twice */
    const startCursor = s.cursor;
    s = await press('south');
    s = await press('south');
    if (!control) chk(s.npick === 2, `${gpu}: two stones picked on the pad (picks ${s.npick})`);
    await shot('4-two-picked');
    s = await press('east');
    const once = s.npick === 0 && s.ui_mode === 0 && s.hash_lo === hash0[0] && s.hash_hi === hash0[1];
    if (!control) caught(!once, `${gpu}: back pressed once leaves a stone picked (picks ${s.npick})`);
    s = await press('east');
    if (!control) chk(s.npick === 0 && s.ui_mode === 0 && s.hash_lo === hash0[0] && s.hash_hi === hash0[1] && s.turns === turns0,
      `${gpu}: back twice: nothing picked and the rules state as at the turn's start (picks ${s.npick}, hash ${s.hash_hi}:${s.hash_lo})`);
    s = await press('east');
    if (!control) chk(s.npick === 0 && s.turns === turns0 && s.hash_lo === hash0[0], `${gpu}: a third back with nothing picked changes nothing (picks ${s.npick})`);
    void startCursor;
    /* a right click puts the stone back */
    s = await press('south');
    const cur = s.cursor;
    s = await pointer(640, 360, { right: true });
    if (!control) chk(s.npick === 0 && s.turns === turns0, `${gpu}: a right click puts the picked stone back (picks ${s.npick})`);
    /* a tap on a picked stone puts that one back */
    s = await press('south');
    if (!control) chk(s.npick === 1 && s.pick_xy[0] > 0, `${gpu}: a stone picked, floating at ${s.pick_xy.slice(0, 2).map(Math.round)}`);
    s = await pointer(s.pick_xy[0], s.pick_xy[1]);
    if (!control) chk(s.npick === 0 && s.turns === turns0, `${gpu}: a tap on the picked stone puts it back (picks ${s.npick}, cursor ${s.cursor}, was ${cur})`);

    /* play on: confirm whatever the cursor offers (a three-colour take, or the card the cursor is on) until a card
     * the person can fulfil is lit */
    for (let guard = 0; guard < 160 && !s.over; guard++) {
      s = await fc();
      if (s.seat !== 0 || s.cpu_seat || s.flow !== 1) { glowCheck(s); await h.step(60); continue; }
      glowCheck(s);
      const lit = s.glow.reduce((a, b) => a + b, 0);
      if (lit > 0 && !glowSeen) {
        await h.step(45);
        const t = await fc();
        glowCheck(t);
        chk(t.glow_lit >= lit, `${gpu}: ${lit} card(s) the person can fulfil glow (halos at half strength or more: ${t.glow_lit})`);
        await shot('5-glow');
        glowSeen = true;
        /* fulfil one: tap it, then Fulfil */
        const tg = t.glow.findIndex((v) => v);
        let u = await tapTarget(t, tg);
        u = await tapTarget(u, tg);
        if (u.ok_on) u = await tapTarget(u, TG.OK);
        if (u.ok_on && u.turns === t.turns) u = await tapTarget(u, TG.OK);
        await h.step(40);
        u = await fc();
        chk(u.turns > t.turns && u.cards[0] > t.cards[0], `${gpu}: the lit card was fulfilled with the pointer (cards ${u.cards[0]})`);
        break;
      }
      s = await press('south', 20);
    }
    chk(glowSeen, `${gpu}: a card the person could fulfil was lit at some point`);
    chk(glowBad === 0 && glowChecks > 5, `${gpu}: on every look (${glowChecks}) the glow was exactly the rules' fulfillable cards, none on the guild's turns (${glowBad} wrong)`);
    await shot('6-later');
    s = await fc();
    chk(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = [s.hash_lo, s.hash_hi].join(':'), frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 12 && !(await fc()).replay_done; i++) await h.step(300);
    s = await fc();
    if (control) caught(s.replay_done === 1 && !(s.replay_match && s.replay_hash_match), `${gpu}: with ${control === 'set' ? 'set actions' : 'the pointer'} left out of the recording the replay differs (state ${s.replay_match}, input ${s.replay_hash_match})`);
    else chk(s.replay_done === 1 && s.replay_match === 1 && s.replay_hash_match === 1, `${gpu}: the replay reaches the same state and input hash (done ${s.replay_done}, state ${s.replay_match}, input ${s.replay_hash_match})`);
    return { hash, frame };
  } finally {
    await h.close();
  }
}

const gpus = gpusArg(process.argv);
const runs = {};
for (const gpu of gpus) { await titlePages(gpu); runs[gpu] = await play(gpu); }
if (runs.gl && runs.webgpu) check(runs.gl.hash === runs.webgpu.hash && runs.gl.frame === runs.webgpu.frame, `GL and WebGPU end in the same state (${runs.gl.hash} at frame ${runs.gl.frame}, ${runs.webgpu.hash} at ${runs.webgpu.frame})`);
for (const control of ['rows', 'cursor', 'glow', 'set', 'pointer', 'nostart']) await play(gpus[0], { control });
console.log(failed ? `${failed} failed` : 'facet play: all passed');
process.exit(failed ? 1 : 0);
