// Smalti played through romdev on GL and on WebGPU: a person's turn on the pad, the computer's turn, a turn by the
// pointer, and a replay of the match from its own recording, with pictures to look at.
//
//   node tests/smalti-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/smalti]
//
// The title first, on a load of its own: "How to play" and "Credits" open their pages (the shell's screen 6) and
// back returns to the title.
// Then one run per GPU (seats: a person on pad 1 and a Normal computer, bag seed 5, the switch read in the shell's
// menu tick):
//   - the pad: hop right, take the group (south), put it back (east), take it again, place it where the ghost shows;
//   - the computer plays its turn through the pad's actions;
//   - the pointer: tap a group (the cursor goes there), tap it again (taken), tap a row (the ghost moves there), tap it
//     again (placed): four recorded pointer picks and a move;
//   - `replay`: the match so far replayed from its input recording must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame. CONTROLS that must fail: the same replay with
// CW_TEST_SET_UNRECORDED=1 (set actions, the computer's presses, left out of the recording); with
// CW_TEST_POINTER_UNRECORDED=1 (the engine's pointer channel, so the taps, left out); and with no seats switch the
// title stays put (the menu tick is what starts a test match).
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'smalti-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/smalti');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

// the title's pages, on a load of their own (the seats switch is read on every menu tick, so a play run sends it first)
async function titlePages(gpu) {
  const h = await openCart('smalti', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  const shot = async (name) => h.shot(join(OUT, `${gpu}-${name}.png`));
  try {
    /* down to How to play, open, back; down to Credits, open, back */
    await h.step(40);
    let t = await sh();
    check(t.screen === 1, `${gpu}: the title shows with no test switch (screen ${t.screen})`);
    const a0 = await h.now();
    await h.timeline([tap(a0 + 5, 'down'), tap(a0 + 20, 'south')], a0 + 50);
    t = await sh();
    check(t.screen === 6 && t.page === 0, `${gpu}: How to play opens its page (screen ${t.screen}, page ${t.page})`);
    await shot('0-how-to-play');
    const a1 = await h.now();
    await h.timeline([tap(a1 + 5, 'east'), tap(a1 + 25, 'down'), tap(a1 + 40, 'south')], a1 + 70);
    t = await sh();
    check(t.screen === 6 && t.page === 1, `${gpu}: Credits opens its page (screen ${t.screen}, page ${t.page})`);
    await shot('0-credits');
    const a2 = await h.now();
    await h.timeline([tap(a2 + 5, 'east')], a2 + 30);
    t = await sh();
    check(t.screen === 1, `${gpu}: back returns to the title (screen ${t.screen})`);
  } finally {
    await h.close();
  }
}

async function play(gpu, { control = '' } = {}) {
  const h = await openCart('smalti', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const st = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('game.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  /* step in ranges until seat 0 may move (its turn, nothing animating) */
  const untilMine = async (limit = 3000) => {
    let s = await st();
    for (let n = 0; (s.players !== 2 || s.ntargets === 0 || s.turn !== 0 || s.busy || s.over || s.mode !== 0) && n < limit; n += 30) { await h.step(30); s = await st(); }
    return s;
  };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env SMALTI_SEED=5' : `env SMALTI_SEATS=p,c1 SMALTI_SEED=5${sw}`);
    if (control === 'nostart') {
      await h.step(60);
      const sc = (await h.state())['shell.screen'];
      check(sc === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${sc}): caught`);
      return null;
    }
    let s = await untilMine();
    check(s.turn === 0 && s.mode === 0 && s.ntargets > 0, `${gpu}: the match starts with seat 1 to pick (turn ${s.turn}, mode ${s.mode}, ${s.ntargets} groups)`);
    await shot('1-start');
    /* the pad: right, take, put back, take, place */
    let at = await h.now();
    await h.timeline([tap(at + 5, 'right'), tap(at + 25, 'south'), tap(at + 50, 'east'), tap(at + 75, 'south')], at + 95);
    s = await st();
    check(s.mode === 1 && s.moves === 0, `${gpu}: took a group, put it back, took it again (mode ${s.mode})`);
    await shot('2-holding');
    at = await h.now();
    await h.timeline([tap(at + 5, 'south')], at + 20);
    s = await st();
    check(s.moves === 1 && s.mode === 0, `${gpu}: placed it on the row the ghost showed (moves ${s.moves})`);
    await shot('3-placed');
    /* the computer's turn */
    s = await untilMine();
    check(s.cpu_moves >= 1 && s.turn === 0, `${gpu}: the computer played its turn through the pad's actions (computer moves ${s.cpu_moves})`);
    await shot('4-after-cpu');
    /* the pointer: a group twice, then a row twice */
    const k = (h.loaded.fbWidth || 1280) / 1280;
    const pt = async (x, y, frames = 16) => {
      await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: false }, h.session);
      await h.step(frames);
    };
    const g = s.ntargets > 1 ? 1 : 0; /* the second group: the cursor starts on the first */
    const gx = s.target_px[2 * g], gy = s.target_px[2 * g + 1];
    await pt(gx, gy);
    s = await st();
    check(s.mode === 0 && s.pointer_picks === 1, `${gpu}: a first tap on a group points at it (mode ${s.mode}, picks ${s.pointer_picks})`);
    await pt(gx, gy);
    s = await st();
    check(s.mode === 1, `${gpu}: a second tap takes it (mode ${s.mode})`);
    /* a row other than the one the cursor is on: the first listed row target that is not where the cursor is */
    const rows = []; for (let i = 0; i < s.ntargets; i++) if (s.target_code[i] >= 1000 && s.target_code[i] !== 998) rows.push(i);
    const r = rows[rows.length - 1];
    await pt(s.target_px[2 * r], s.target_px[2 * r + 1]);
    s = await st();
    await shot('5-pointer-ghost');
    check(s.mode === 1 && s.moves === 2, `${gpu}: a tap on a row shows the ghost there (still holding, moves ${s.moves})`);
    await pt(s.target_px[2 * r], s.target_px[2 * r + 1], 40);
    s = await st();
    check(s.moves === 3 && s.pointer_picks === 4, `${gpu}: a second tap places them (moves ${s.moves}, pointer picks ${s.pointer_picks})`);
    await shot('6-pointer-placed');
    check((s['three.missing_states'] ?? s['three.missing'] ?? 0) === 0, `${gpu}: every draw state has its program`);
    const hash = s.state_hash, frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 8 && !(await st()).replay_done; i++) await h.step(300);
    s = await st();
    if (control) check(s.replay_done === 1 && !(s.replay_match && s.replay_hash_match), `${gpu} CONTROL: with ${control === 'set' ? 'set actions' : 'the pointer'} left out of the recording the replay differs (state ${s.replay_match}, input ${s.replay_hash_match}): caught`);
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
console.log(failed ? `${failed} failed` : 'smalti play: all passed');
process.exit(failed ? 1 : 0);
