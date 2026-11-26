// First Hearth played through romdev on GL and on WebGPU, with pictures to look at.
//
//   node tests/firsthearth-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/firsthearth]
//
// One run per GPU (a person first, a Normal CPU second, seed 7):
//   - the title: "How to play" and "Credits" open their pages (the shell's screen 6) and back returns to the title
//     (a load of its own: the seats switch is read on every menu tick, so a play run sends it first);
//   - the match opens on the how-to-play card; confirm closes it and plays nothing;
//   - the pointer: tap a place (the cursor goes there and the ghost kin show), tap it again (chosen: a gathering
//     place then asks how many);
//   - the pad: one more kin (right), send them (confirm);
//   - the CPU sends its kin through the pad's actions, then the person confirms the highlighted choices for a while;
//   - `replay`: the match so far replayed from its own input recording must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame (the rules do not depend on the renderer).
// CONTROLS that must fail: the same replay with CW_TEST_SET_UNRECORDED=1 (the CPU's presses left out of the
// recording) or CW_TEST_POINTER_UNRECORDED=1 (the pointer left out) must not match; with no seats switch the title
// stays put (the menu tick is what starts a test match).
// Pictures: <out>/<gpu>-<step>.png. Each run takes the GPU lock (tools/with-gpu-suite.mjs) unless already inside one.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'firsthearth-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/firsthearth');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

async function titlePages(gpu) {
  const h = await openCart('firsthearth', { gpu, seed: 1, tag: `${gpu}-title` });
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
  const h = await openCart('firsthearth', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const g = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('firsthearth.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; o.screen = s['shell.screen']; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env FIRSTHEARTH_SEED=7' : `env FIRSTHEARTH_SEATS=p:c1 FIRSTHEARTH_SEED=7${sw}`);
    await h.step(150);
    let s = await g();
    if (control === 'nostart') {
      check(s.screen === 1 && !s.matches, `CONTROL ${gpu}: with no seats switch the title stays (screen ${s.screen}, matches ${s.matches}): caught`);
      return null;
    }
    check(s.help === 1 && s.turn === 0 && s.decisions === 0, `${gpu}: the match opens on the how-to card, the person to move (help ${s.help}, turn ${s.turn})`);
    await shot('1-card');
    let at = await h.now();
    await h.timeline([tap(at + 5, 'south')], at + 40);
    s = await g();
    check(s.help === 0 && s.decisions === 0, `${gpu}: confirm closes the card and plays nothing (help ${s.help}, decisions ${s.decisions})`);
    /* the pointer: tap a place the cursor is not on (the first tap only points), then tap it again */
    const k = (h.loaded.fbWidth || 1280) / 1280; /* canvas px (1280 wide) to the cart's pixels */
    const pt = async (i, frames = 12) => {
      const px = (await g()).target_px;
      const x = Math.round(px[2 * i] * k), y = Math.round(px[2 * i + 1] * k);
      await call('input', { op: 'pointer', x, y, left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x, y, left: false }, h.session);
      await h.step(frames);
    };
    const FOREST = 1; /* targets are the places in order while placing: the forest asks how many */
    const pick = s.cursor === FOREST ? 2 : FOREST;
    await pt(pick, 30);
    s = await g();
    check(s.decisions === 0 && s.cursor === pick && s.mode === 0, `${gpu}: a tap on a place puts the cursor there and shows the ghost kin (cursor ${s.cursor}, decisions ${s.decisions})`);
    await shot('2-pointer-ghost');
    await pt(pick, 30);
    s = await g();
    check(s.decisions === 0 && s.mode === 1, `${gpu}: a second tap chooses it and asks how many (mode ${s.mode})`);
    /* the pad: one kin fewer (or more, from one), then send */
    at = await h.now();
    const before = s.cursor;
    await h.timeline([tap(at + 5, before > 0 ? 'left' : 'right')], at + 30);
    s = await g();
    check(s.cursor !== before, `${gpu}: the pad moves the count (${before} to ${s.cursor})`);
    await shot('3-pad-count');
    at = await h.now();
    await h.timeline([tap(at + 5, 'south')], at + 60);
    s = await g();
    check(s.decisions - s.cpu_moves === 1 && s.mode === 0, `${gpu}: confirm sends the kin (the person's decisions ${s.decisions - s.cpu_moves}, the CPU's ${s.cpu_moves})`);
    /* the CPU, then the person confirming the highlighted choice, for a while */
    at = await h.now();
    const presses = [];
    for (let i = 0; i < 12; i++) presses.push(tap(at + 90 + i * 60, 'south'));
    await h.timeline(presses, at + 900);
    s = await g();
    check(s.cpu_moves >= 2 && s.decisions >= 4, `${gpu}: the CPU sent its kin through the pad's actions and the person kept playing (cpu moves ${s.cpu_moves}, decisions ${s.decisions})`);
    await shot('4-after-cpu');
    check(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = s.hash, frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 8 && !(await g()).replay_done; i++) await h.step(300);
    s = await g();
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
console.log(failed ? `${failed} failed` : 'firsthearth play: all passed');
process.exit(failed ? 1 : 0);
