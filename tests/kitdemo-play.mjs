// kitdemo played through romdev on GL and on WebGPU: the reference game's whole path through the kit, with
// pictures to look at.
//
//   node tests/kitdemo-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/kitdemo]
//
// One run per GPU (seats: a person on pad 1 and a Normal CPU, seed 7):
//   - the title: "How to play" and "Credits" open their pages (the shell's screen 6) and back returns to the title;
//     then the test switch starts the match from the shell's menu tick (bk_title_desc);
//   - the pad: hop right, take the card (south), put it back (east: undo), take it again, bank it;
//   - the CPU plays its turn through the pad's actions;
//   - the pointer: tap a market card (the cursor goes there, its ghost shows), tap it again (taken), tap the
//     die twice (roll): a throw starts and the die comes to rest;
//   - `replay`: the match so far replayed from its own input recording must reach the same state and input
//     hash (pad presses and CPU presses as actions, pointer picks through the engine's pointer channel).
// Both GPUs must end in the same state hash at the same frame (the rules do not depend on the renderer).
// CONTROLS that must fail: the same replay with CW_TEST_SET_UNRECORDED=1 (the engine leaves set actions, the
// CPU's presses, out of the recording) must not match; with CW_TEST_POINTER_UNRECORDED=1 (the pointer left out)
// neither; and with no switch the title stays put (the menu tick is what starts a test match).
// Pictures: <out>/<gpu>-<step>.png. Each run takes the GPU lock (tools/with-gpu-suite.mjs) unless already inside one.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'kitdemo-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/kitdemo');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

// the title's pages, on a load of their own (the seats switch is read on every menu tick, so a play run sends it first)
async function titlePages(gpu) {
  const h = await openCart('kitdemo', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  const shot = async (name) => h.shot(join(OUT, `${gpu}-${name}.png`));
  try {
    /* the title's pages: down to How to play, open, back; down to Credits, open, back */
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
  const h = await openCart('kitdemo', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const kd = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('kd.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  try {
    const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
    let s;
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1', nostart: '' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env KITDEMO_SEED=7' : `env KITDEMO_SEATS=p,c1 KITDEMO_SEED=7${sw}`);
    await h.step(60);
    if (control === 'nostart') {
      const t = await sh();
      check(t.screen === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${t.screen}): caught`);
      return null;
    }
    s = await kd();
    check(s.phase === 0 && s.turn === 0, `${gpu}: the match starts with seat 1 to pick (phase ${s.phase}, turn ${s.turn})`);
    await shot('1-start');
    /* the pad: right, take, undo, take, bank */
    const at = await h.now();
    await h.timeline([tap(at + 5, 'right'), tap(at + 25, 'south'), tap(at + 50, 'east'), tap(at + 75, 'south')], at + 90);
    s = await kd();
    check(s.undos === 1 && s.phase === 1, `${gpu}: took a card, put it back, took it again (undos ${s.undos}, phase ${s.phase})`);
    await shot('2-holding');
    const at2 = await h.now();
    await h.timeline([tap(at2 + 5, 'south')], at2 + 30);
    s = await kd();
    check(s.pad_moves === 3 && s.score[0] > 0 && s.turn === 1, `${gpu}: banked it: ${s.score[0]} points, the CPU to move (pad moves ${s.pad_moves})`);
    /* the CPU's turn */
    await h.step(420);
    s = await kd();
    check(s.cpu_moves >= 2 && s.turn === 0, `${gpu}: the CPU played its turn through the pad's actions (cpu moves ${s.cpu_moves}, turn ${s.turn})`);
    await shot('3-after-cpu');
    /* the pointer: tap slot 2 (look: the cursor starts on slot 0), tap again (take), tap the die twice (roll) */
    const px = s.target_px, k = (h.loaded.fbWidth || 1280) / 1280; /* canvas px (1280 wide) to the cart's pixels */
    const pt = async (t, frames = 12) => {
      await call('input', { op: 'pointer', x: Math.round(px[2 * t] * k), y: Math.round(px[2 * t + 1] * k), left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x: Math.round(px[2 * t] * k), y: Math.round(px[2 * t + 1] * k), left: false }, h.session);
      await h.step(frames);
    };
    await pt(2, 20);
    s = await kd();
    await shot('4-ghost');
    check(s.phase === 0 && s.ghost_shown > 0, `${gpu}: a first tap points (still picking, ghost shown ${s.ghost_shown})`);
    await pt(2, 20);
    s = await kd();
    check(s.phase === 1, `${gpu}: a second tap takes the card (phase ${s.phase})`);
    await pt(4, 6);
    await pt(4, 14);
    s = await kd();
    check(s.throws === 1 && s.pointer_picks === 2, `${gpu}: two taps on the die roll it (throws ${s.throws}, pointer picks ${s.pointer_picks})`);
    await shot('5-throw');
    await h.step(90);
    s = await kd();
    await shot('6-rolled');
    check(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = s.hash, frame = h.frame;
    /* the replay */
    await h.command('replay');
    for (let i = 0; i < 6 && !(await kd()).replay_done; i++) await h.step(300);
    s = await kd();
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
console.log(failed ? `${failed} failed` : 'kitdemo play: all passed');
process.exit(failed ? 1 : 0);
