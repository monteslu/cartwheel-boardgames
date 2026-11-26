// Rose Window played through romdev on GL and on WebGPU: every kind of move through the real controls, on the pad and
// on the pointer, the computer's turns, the title's pages, and a replay of the match from its own recording, with
// pictures to look at.
//
//   node tests/rosewindow-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/rosewindow]
//
// The title (a load of its own, no seats switch): How to play and Credits open their pages (the shell's screen 6) and
// back returns to the title.
// The match (seats: a person on pad 1, a hot seat played with the pointer, a Normal computer; game seed 5). Each move
// kind is made once on the pad (seat 1) and once with the pointer (seat 2), and each must change the rules state (the
// state hash, the move count and the person's count of that kind); before each, a step that only aims (a pad hop, a
// first tap) must leave the state alone:
//   - a window frame: pad right then confirm; pointer tap, tap (and a tap on bare table picks nothing);
//   - a die set in the window: pad take, put back (east), take again, set where the ghost is; pointer die, die,
//     pane, pane;
//   - a tool card and what it asks for, then take it back (pad east with nothing chosen; pointer the "take back"
//     choice on the card): the state hash must return to the one before the tool;
//   - the bell (pass);
//   - the how-to card (north) and the cards panel (west) open and close without changing the state;
//   - the computer plays its turns through the pad's actions;
//   - `replay`: the match so far replayed from its input recording must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame. CONTROLS that must fail: the same replay with
// CW_TEST_SET_UNRECORDED=1 (set actions, so the computer's presses, left out of the recording) or
// CW_TEST_POINTER_UNRECORDED=1 (the pointer left out) must not match; with no seats switch the title stays put.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'rosewindow-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/rosewindow');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });
// the game's choice kinds (src/game.h) and move kinds (src/rules.h)
const DIE = 1, PANE = 2, TOOL = 3, BELL = 4, FRAME = 6, BACK = 7;
const KIND = (id) => id >> 8;
const MV = { place: 0, pass: 1, tool: 2, frame: 3 };
// tools that roll no dice and leave the turn open (nudge, turn, tint, etch, twin): the ones a player can take back
// (a roll is final, and the glaze and lone tools set a die, which ends the turn)
const UNDOABLE = new Set([0, 1, 5, 6, 7]);
const undoable = (s, i) => UNDOABLE.has(s.tool_kinds[s.target_code[i] & 0xff]);

async function titlePages(gpu) {
  const h = await openCart('rosewindow', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  try {
    await h.step(40);
    let t = await sh();
    check(t.screen === 1, `${gpu}: the title shows with no test switch (screen ${t.screen})`);
    const a0 = await h.now();
    await h.timeline([tap(a0 + 5, 'down'), tap(a0 + 20, 'south')], a0 + 50);
    t = await sh();
    check(t.screen === 6 && t.page === 0, `${gpu}: How to play opens its page (screen ${t.screen}, page ${t.page})`);
    await h.shot(join(OUT, `${gpu}-0-how-to-play.png`));
    const a1 = await h.now();
    await h.timeline([tap(a1 + 5, 'east'), tap(a1 + 25, 'down'), tap(a1 + 40, 'south')], a1 + 70);
    t = await sh();
    check(t.screen === 6 && t.page === 1, `${gpu}: Credits opens its page (screen ${t.screen}, page ${t.page})`);
    await h.shot(join(OUT, `${gpu}-0-credits.png`));
    const a2 = await h.now();
    await h.timeline([tap(a2 + 5, 'east')], a2 + 30);
    t = await sh();
    check(t.screen === 1, `${gpu}: back returns to the title (screen ${t.screen})`);
  } finally {
    await h.close();
  }
}

async function play(gpu, { control = '' } = {}) {
  const h = await openCart('rosewindow', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const st = async () => {
    const s = await h.state(); const o = {};
    for (const [k, v] of Object.entries(s)) if (k.startsWith('game.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v;
    o.screen = s['shell.screen'];
    return o;
  };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  const targets = (s, kind) => { const out = []; for (let i = 0; i < s.ntargets; i++) if (KIND(s.target_code[i]) === kind) out.push(i); return out; };
  /* step in ranges until `seat` may choose (its turn, nothing moving, nothing chosen yet) */
  const until = async (seat, limit = 6000) => {
    let s = await st();
    for (let n = 0; (s.players !== 3 || s.ntargets === 0 || s.turn !== seat || s.busy || s.over || s.plen !== 0 || s.help) && n < limit; n += 30) { await h.step(30); s = await st(); }
    return s;
  };
  /* the pad (seat 1): a press, then frames for it to play out */
  const pad = async (button, frames = 20) => { const at = await h.now(); await h.timeline([tap(at + 3, button)], at + frames); return st(); };
  /* the pad: hop the cursor to target i (toward its canvas point) */
  const padTo = async (i) => {
    let s = await st();
    const want = s.target_code[i];
    for (let n = 0; n < 24 && s.cursor !== want; n++) {
      const ci = s.target_code.indexOf(s.cursor);
      const dx = s.target_px[2 * i] - s.target_px[2 * ci], dy = s.target_px[2 * i + 1] - s.target_px[2 * ci + 1];
      s = await pad(Math.abs(dx) >= Math.abs(dy) ? (dx > 0 ? 'right' : 'left') : (dy > 0 ? 'down' : 'up'), 12);
    }
    return s;
  };
  /* the pointer: a click at canvas point (x, y) */
  const k = (h.loaded.fbWidth || 1280) / 1280;
  const click = async (x, y, frames = 16) => {
    await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: true }, h.session);
    await h.step(2);
    await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: false }, h.session);
    await h.step(frames);
    return st();
  };
  const tapTarget = async (s, i, frames) => click(s.target_px[2 * i], s.target_px[2 * i + 1], frames);
  /* the pointer: choose target i (one tap if the cursor is on it already, else aim then choose) */
  const ptChoose = async (i, frames = 30) => {
    let s = await st();
    if (s.cursor !== s.target_code[i]) s = await tapTarget(s, i, 12);
    return tapTarget(s, i, frames);
  };
  /* what moved in the rules between two reads */
  const moved = (a, b, kind) => b.state_hash !== a.state_hash && b.moves === a.moves + 1 && b.person_kinds[MV[kind]] === a.person_kinds[MV[kind]] + 1;
  const still = (a, b) => b.state_hash === a.state_hash && b.moves === a.moves;
  /* finish a path on the pad (confirm the cursor's choice) or the pointer (the first choice that is not "put back")
   * until a move lands */
  const finish = async (how, before) => {
    let s = await st();
    for (let n = 0; n < 4 && s.moves === before.moves; n++) {
      if (how === 'pad') s = await pad('south', 30);
      else {
        const i = [...Array(s.ntargets).keys()].find((j) => KIND(s.target_code[j]) !== BACK);
        s = await ptChoose(i);
      }
      for (let w = 0; s.busy && w < 300; w += 20) { await h.step(20); s = await st(); }
    }
    return s;
  };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env ROSEWINDOW_SEED=5' : `env ROSEWINDOW_SEATS=p,h,c1 ROSEWINDOW_SEED=5${sw}`);
    if (control === 'nostart') {
      await h.step(200);
      const s = await st();
      check(s.screen === 1 && !s.players, `CONTROL ${gpu}: with no seats switch the title stays (screen ${s.screen}, players ${s.players}): caught`);
      return null;
    }
    /* frames: the pad (seat 1) */
    let s = await until(0);
    check(s.phase === 0 && KIND(s.target_code[0]) === FRAME, `${gpu}: the match opens with seat 1 choosing a frame (phase ${s.phase}, first choice kind ${KIND(s.target_code[0])})`);
    await shot('1-frames');
    let a = s;
    s = await pad('right');
    check(still(a, s) && s.cursor !== a.cursor, `${gpu}: pad: a hop moves the cursor to the other frame and changes no rules (cursor ${a.cursor} to ${s.cursor})`);
    s = await pad('south', 30);
    check(moved(a, s, 'frame'), `${gpu}: pad: confirm chooses the frame (moves ${a.moves} to ${s.moves}, frames by people ${s.person_kinds[MV.frame]})`);
    /* frames: the pointer (seat 2) */
    s = await until(1);
    a = s;
    s = await click(1270, 712);
    check(still(a, s) && s.pointer_picks === a.pointer_picks, `${gpu}: pointer: a tap on bare table picks nothing (picks ${s.pointer_picks})`);
    const fr = targets(s, FRAME).find((i) => s.target_code[i] !== s.cursor);
    s = await tapTarget(s, fr, 12);
    check(still(a, s) && s.cursor === s.target_code[fr] && s.pointer_picks === a.pointer_picks + 1, `${gpu}: pointer: a first tap on a frame only aims at it (cursor ${s.cursor})`);
    s = await tapTarget(s, fr, 30);
    check(moved(a, s, 'frame'), `${gpu}: pointer: a second tap chooses it (moves ${s.moves}, frames by people ${s.person_kinds[MV.frame]})`);
    /* a die: the pad (take, put back, take, set where the ghost shows) */
    s = await until(0);
    check(s.phase === 1 && KIND(s.target_code[s.target_code.indexOf(s.cursor)]) === DIE, `${gpu}: the dice are rolled and seat 1 picks a die (phase ${s.phase}, round ${s.round})`);
    await shot('2-pool');
    a = s;
    s = await pad('south');
    check(still(a, s) && s.plen === 1 && s.ghosts === 1, `${gpu}: pad: confirm takes the die, the ghost shows where it would go, no rules yet (path ${s.plen}, ghost ${s.ghosts})`);
    s = await pad('east');
    check(still(a, s) && s.plen === 0, `${gpu}: pad: back puts it down again (path ${s.plen})`);
    s = await pad('south');
    await shot('3-ghost');
    s = await pad('south', 40);
    check(moved(a, s, 'place'), `${gpu}: pad: confirm sets the die on the pane the ghost showed (moves ${s.moves}, dice set by people ${s.person_kinds[MV.place]})`);
    /* a die: the pointer */
    s = await until(1);
    a = s;
    const d = targets(s, DIE).find((i) => s.target_code[i] !== s.cursor);
    s = await tapTarget(s, d, 12);
    check(still(a, s) && s.plen === 0 && s.cursor === s.target_code[d], `${gpu}: pointer: a first tap on a die points at it (path ${s.plen})`);
    s = await tapTarget(s, d, 12);
    check(still(a, s) && s.plen === 1, `${gpu}: pointer: a second tap takes it (path ${s.plen})`);
    const panes = targets(s, PANE), p = panes.find((i) => s.target_code[i] !== s.cursor) ?? panes[0];
    s = await tapTarget(s, p, 12);
    await shot('4-pointer-ghost');
    check(still(a, s) && s.plen === 1 && s.ghosts === 1, `${gpu}: pointer: a tap on a pane moves the ghost there (ghost ${s.ghosts})`);
    s = await tapTarget(s, p, 40);
    check(moved(a, s, 'place'), `${gpu}: pointer: a second tap sets it (moves ${s.moves}, dice set by people ${s.person_kinds[MV.place]})`);
    /* the computer's two turns, then seat 2 again (the snake): a tool by the pointer, taken back, then the bell */
    s = await until(1);
    check(s.cpu_moves >= 3, `${gpu}: the computer played through the pad's actions (computer moves ${s.cpu_moves})`);
    await shot('5-after-cpu');
    a = s;
    const tools = targets(s, TOOL);
    check(tools.length > 0, `${gpu}: tool cards are lit for seat 2 (${tools.length})`);
    /* RW_PLAY_ANY_TOOL=1: the first tool off the cursor, whatever it does (a debugging aid: the take back checks then fail) */
    const tl = process.env.RW_PLAY_ANY_TOOL ? tools.find((i) => s.target_code[i] !== s.cursor) : tools.find((i) => undoable(s, i) && s.target_code[i] !== s.cursor) ?? tools.find((i) => undoable(s, i));
    check(tl !== undefined, `${gpu}: a tool that can be taken back is lit for seat 2 (kinds ${s.tool_kinds})`);
    s = await tapTarget(s, tl, 12);
    check(still(a, s) && s.plen === 0 && s.cursor === s.target_code[tl], `${gpu}: pointer: a first tap on a tool card aims at it (its card shows large)`);
    await shot('6-tool-focus');
    s = await tapTarget(s, tl, 20);
    s = await finish('pointer', a);
    check(moved(a, s, 'tool') && s.person_tools !== 0 && s.favour[1] < a.favour[1], `${gpu}: pointer: the tool card and its choices use the tool (moves ${s.moves}, tools by people ${s.person_kinds[MV.tool]}, favour ${a.favour[1]} to ${s.favour[1]})`);
    await shot('7-take-back');
    const back = targets(s, BACK);
    check(back.length === 1 && s.plen === 0, `${gpu}: the used tool's card offers to take it back (${back.length})`);
    const b0 = s;
    s = back.length ? await ptChoose(back[0], 30) : s;
    check(s.undos === b0.undos + 1 && s.state_hash === a.state_hash && s.favour[1] === a.favour[1], `${gpu}: pointer: take back returns the state to before the tool (undos ${s.undos}, state ${s.state_hash === a.state_hash ? 'same' : 'different'})`);
    s = await until(1, 300);
    a = s;
    const bell = targets(s, BELL);
    s = bell.length ? await ptChoose(bell[0], 40) : s;
    check(bell.length === 1 && moved(a, s, 'pass'), `${gpu}: pointer: the bell passes (moves ${s.moves}, passes by people ${s.person_kinds[MV.pass]})`);
    /* seat 1 on the pad: the how-to card and the cards panel, a tool taken back, then the bell */
    s = await until(0);
    a = s;
    s = await pad('north');
    check(s.help === 1 && still(a, s), `${gpu}: pad: north opens the how-to card (help ${s.help})`);
    await shot('8-help');
    s = await pad('south');
    check(s.help === 0 && still(a, s), `${gpu}: pad: confirm closes it and plays nothing`);
    s = await pad('west');
    await shot('9-cards');
    const cardsShot = s;
    s = await pad('east');
    check(still(a, cardsShot) && still(a, s) && s.plen === 0, `${gpu}: pad: west opens the cards panel, back closes it, no rules change`);
    s = await st();
    const ptl = targets(s, TOOL);
    check(ptl.length > 0, `${gpu}: tool cards are lit for seat 1 (${ptl.length})`);
    const pt0 = ptl.find((i) => undoable(s, i));
    check(pt0 !== undefined, `${gpu}: a tool that can be taken back is lit for seat 1 (kinds ${s.tool_kinds})`);
    s = await padTo(pt0);
    check(still(a, s) && s.cursor === s.target_code[pt0], `${gpu}: pad: hops reach a tool card and change no rules (cursor ${s.cursor})`);
    s = await pad('south', 20);
    s = await finish('pad', a);
    check(moved(a, s, 'tool') && s.favour[0] < a.favour[0], `${gpu}: pad: the tool card and its choices use the tool (tools by people ${s.person_kinds[MV.tool]}, favour ${a.favour[0]} to ${s.favour[0]})`);
    const u0 = s;
    s = await pad('east', 30);
    check(s.undos === u0.undos + 1 && s.state_hash === a.state_hash, `${gpu}: pad: back with nothing chosen takes the tool back (undos ${s.undos}, state ${s.state_hash === a.state_hash ? 'same' : 'different'})`);
    s = await until(0, 300);
    a = s;
    const pb = targets(s, BELL);
    s = await padTo(pb[0]);
    s = await pad('south', 40);
    check(pb.length === 1 && moved(a, s, 'pass'), `${gpu}: pad: the bell passes (passes by people ${s.person_kinds[MV.pass]})`);
    await h.step(120);
    s = await st();
    await shot('10-after');
    check(s.lit_panes >= 2, `${gpu}: the panes with dice light up (${s.lit_panes} lit)`);
    check(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = s.state_hash, frame = h.frame;
    await h.command('replay');
    try {
      for (let i = 0; i < 40 && !(await st()).replay_done; i++) await h.step(300);
    } catch (e) { /* a trap in the replay: what the cart said last */
      const ev = await h.events().catch(() => null);
      const last = JSON.stringify(ev).slice(-1500);
      /* with set actions left out, the computer's presses never reach the replayed match and bk_cpu stops the cart
       * (fail fast): the replay could not play the match, so the control is caught */
      if (control === 'set' && /never landed/.test(last)) {
        check(true, `CONTROL ${gpu}: with set actions left out of the recording the replay cannot play the computer's moves (bk_cpu stops the cart): caught`);
        return null;
      }
      console.log(last);
      throw e;
    }
    s = await st();
    if (control) check(s.replay_done === 1 && !(s.replay_match && s.replay_hash_match), `CONTROL ${gpu}: with ${control === 'set' ? 'set actions' : 'the pointer'} left out of the recording the replay differs (state ${s.replay_match}, input ${s.replay_hash_match}): caught`);
    else check(s.replay_done === 1 && s.replay_match === 1 && s.replay_hash_match === 1, `${gpu}: the replay reaches the same state and input hash (done ${s.replay_done}, state ${s.replay_match}, input ${s.replay_hash_match})`);
    return { hash, frame };
  } finally {
    await h.close();
  }
}

const gpus = gpusArg(process.argv);
const runs = {};
for (const gpu of gpus) { await titlePages(gpu); runs[gpu] = await play(gpu); }
if (runs.gl && runs.webgpu) check(runs.gl.hash === runs.webgpu.hash && runs.gl.frame === runs.webgpu.frame, `GL and WebGPU end in the same state (${runs.gl.hash} at frame ${runs.gl.frame}, ${runs.webgpu.hash} at ${runs.webgpu.frame})`);
for (const control of ['set', 'pointer', 'nostart']) await play(gpus[0], { control });
console.log(failed ? `${failed} failed` : 'rosewindow play: all passed');
process.exit(failed ? 1 : 0);
