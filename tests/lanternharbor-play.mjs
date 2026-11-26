// Lantern Harbor played through romdev on GL and on WebGPU: a person's turn on the pad, the computer's turn, a job
// taken with the pointer, and a replay of the match from its own recording, with pictures to look at.
//
//   node tests/lanternharbor-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/lanternharbor]
//
// One run per GPU (seats: a person on pad 1 and a Normal computer, seed 5, short waits):
//   - the title (a load of its own, no switch): "How to play" and "Credits" open their pages (the shell's screen 6)
//     and back returns to the title; then the test switch starts the match from the shell's menu tick;
//   - the pad: hop right twice to the mason, take it (south), then choose whatever the cursor offers until the job is done;
//   - the computer plays its turn through the pad's actions;
//   - the pointer: tap a job (the cursor goes there), tap it again (taken): two recorded pointer picks and a move;
//   - `replay`: the match so far replayed from its input recording must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame. CONTROLS that must fail: the same replay with
// CW_TEST_SET_UNRECORDED=1 (set actions, the computer's presses, left out of the recording) must not match; with
// CW_TEST_POINTER_UNRECORDED=1 (the pointer channel left out) neither; and with no seats switch the title stays.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'lanternharbor-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/lanternharbor');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });
const PH_PICK = 0;

// the title's pages, on a load of their own (the seats switch is read on every menu tick, so a play run sends it first)
async function titlePages(gpu) {
  const h = await openCart('lanternharbor', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  const shot = async (name) => h.shot(join(OUT, `${gpu}-${name}.png`));
  try {
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
  const h = await openCart('lanternharbor', { gpu, seed: 1, tag: `${gpu}${control ? '-control' : ''}` });
  const st = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('game.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  const myPick = (s) => s.players === 2 && s.ntargets > 0 && s.turn === 0 && !s.busy && !s.over && s.phase === PH_PICK && s.mode === 0;
  const untilMinePick = async (limit = 6000) => {
    let s = await st();
    for (let n = 0; !myPick(s) && n < limit; n += 30) {
      if (s.turn === 0 && !s.busy && s.ntargets > 0 && s.phase !== PH_PICK) { const at = await h.now(); await h.timeline([tap(at + 3, 'south')], at + 14); }
      else await h.step(30);
      s = await st();
    }
    return s;
  };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env LANTERNHARBOR_SEED=5' : `env LANTERNHARBOR_SEATS=p,c1 LANTERNHARBOR_SEED=5 LANTERNHARBOR_FAST=1${sw}`);
    if (control === 'nostart') {
      await h.step(60);
      const t = (await h.state())['shell.screen'];
      check(t === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${t}): caught`);
      return null;
    }
    let s = await untilMinePick();
    check(myPick(s), `${gpu}: the match starts with seat 1 to take a job (turn ${s.turn}, phase ${s.phase}, ${s.ntargets} jobs)`);
    await shot('1-start');
    /* the pad: hop right, take that job */
    let at = await h.now();
    await h.timeline([tap(at + 5, 'right'), tap(at + 15, 'right'), tap(at + 30, 'south')], at + 48); /* the third job: the mason */
    s = await st();
    check(s.moves_by[0] === 1, `${gpu}: took a job with the pad (seat 1 decisions ${s.moves_by[0]}, phase ${s.phase})`);
    await shot('2-job');
    /* the rest of that job: choose what the cursor offers until the job is over */
    let shotChoosing = false;
    for (let i = 0; i < 40 && !(s.turn !== 0 || (s.phase === PH_PICK && !s.busy)); i++) {
      if (s.turn === 0 && !s.busy && s.ntargets > 0) {
        if (!shotChoosing) { await shot('3-choosing'); shotChoosing = true; } /* the cursor on a choice, its focus panel up */
        at = await h.now(); await h.timeline([tap(at + 3, 'south')], at + 16);
      } else await h.step(10);
      s = await st();
    }
    check(s.moves_by[0] >= 1, `${gpu}: played on through the job (seat 1 decisions ${s.moves_by[0]})`);
    /* the computer's turns, then seat 1 to take a job again */
    s = await untilMinePick();
    check(s.cpu_moves >= 1 && myPick(s), `${gpu}: the computer played through the pad's actions (computer moves ${s.cpu_moves})`);
    await shot('4-after-cpu');
    /* the pointer: a job twice */
    const k = (h.loaded.fbWidth || 1280) / 1280;
    const pt = async (x, y, frames = 16) => {
      await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x: Math.round(x * k), y: Math.round(y * k), left: false }, h.session);
      await h.step(frames);
    };
    const before = s.moves_by[0], picks = s.pointer_picks;
    const g = s.ntargets > 1 ? 1 : 0;
    const gx = s.target_px[2 * g], gy = s.target_px[2 * g + 1];
    await pt(gx, gy);
    s = await st();
    check(s.moves_by[0] === before && s.pointer_picks === picks + 1, `${gpu}: a first tap on a job points at it (seat 1 decisions ${s.moves_by[0]}, picks ${s.pointer_picks})`);
    await shot('5-pointer');
    await pt(gx, gy, 40);
    s = await st();
    check(s.moves_by[0] === before + 1 && s.pointer_picks === picks + 2, `${gpu}: a second tap takes it (seat 1 decisions ${s.moves_by[0]}, pointer picks ${s.pointer_picks})`);
    await shot('6-pointer-taken');
    check((s['three.missing_states'] ?? s['three.missing'] ?? 0) === 0, `${gpu}: every draw state has its program`);
    const hash = s.state_hash, frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 12 && !(await st()).replay_done; i++) await h.step(300);
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
console.log(failed ? `${failed} failed` : 'lanternharbor play: all passed');
process.exit(failed ? 1 : 0);
