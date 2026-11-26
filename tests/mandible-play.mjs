// Mandible played through romdev on GL and on WebGPU, with pictures to look at.
//
//   node tests/mandible-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/mandible]
//
// The title first, on a load of its own: "How to play" opens its page (the shell's screen 6) and right turns it to
// the second, "Credits" opens its page, and back returns to the title.
// Then one run per GPU (seats: a person on pad 1 as Ivory and a Normal CPU as Ebony, seed 7, the switch read in the
// shell's menu tick):
//   - the how-to-play card is open at the start; confirm closes it;
//   - the pad: take the first piece in the tray (south), put it back (east), take it again, hop to another
//     space (right), place it (south);
//   - the CPU plays its reply through the pad's actions;
//   - the pointer: tap a piece in the tray (the legal spaces light up, a ghost shows), tap a space (placed);
//   - `replay`: the match so far replayed from its own input recording must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame (the rules do not depend on the renderer).
// CONTROLS that must fail: the same replay with CW_TEST_SET_UNRECORDED=1 (the CPU's presses and the pointer picks,
// which the game turns into presses of its point action, left out of the recording) must not match; with no seats
// switch the title stays put (the menu tick is what starts a test match).
// Pictures: <out>/<gpu>-<step>.png. Each run takes the GPU lock (tools/with-gpu-suite.mjs) unless already inside one.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'mandible-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/mandible');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

// the title's pages, on a load of their own (the seats switch is read on every menu tick, so a play run sends it first)
async function titlePages(gpu) {
  const h = await openCart('mandible', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'], help_page: s['game.help_page'] }; };
  const shot = async (name) => h.shot(join(OUT, `${gpu}-${name}.png`));
  try {
    /* down to How to play, open, turn to its second page, back; down to Credits, open, back */
    await h.step(40);
    let t = await sh();
    check(t.screen === 1, `${gpu}: the title shows with no test switch (screen ${t.screen})`);
    const a0 = await h.now();
    await h.timeline([tap(a0 + 5, 'down'), tap(a0 + 20, 'south')], a0 + 50);
    t = await sh();
    check(t.screen === 6 && t.page === 0, `${gpu}: How to play opens its page (screen ${t.screen}, page ${t.page})`);
    await shot('0-how-to-play');
    const ap = await h.now();
    await h.timeline([tap(ap + 5, 'right')], ap + 30);
    t = await sh();
    check(t.screen === 6 && t.help_page === 1, `${gpu}: right turns How to play to its second page (screen ${t.screen}, help page ${t.help_page})`);
    await shot('0-how-to-play-2');
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
  const h = await openCart('mandible', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const g = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('game.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env MANDIBLE_SEED=7' : `env MANDIBLE_SEATS=p:c1 MANDIBLE_SEED=7${sw}`);
    if (control === 'nostart') {
      await h.step(60);
      const sc = (await h.state())['shell.screen'];
      check(sc === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${sc}): caught`);
      return null;
    }
    await h.step(60);
    let s = await g();
    check(s.help === 1 && s.turn === 0 && s.ply === 0, `${gpu}: the match opens on the how-to card, Ivory to move (help ${s.help}, turn ${s.turn})`);
    await shot('1-card');
    let at = await h.now();
    await h.timeline([tap(at + 5, 'south'), tap(at + 25, 'south'), tap(at + 45, 'east'), tap(at + 65, 'south'), tap(at + 85, 'right')], at + 100);
    s = await g();
    check(s.help === 0 && s.phase === 2, `${gpu}: card closed, a tray piece taken, put back and taken again (phase ${s.phase})`);
    await shot('2-ghost');
    at = await h.now();
    await h.timeline([tap(at + 5, 'south')], at + 40);
    s = await g();
    check(s.human_moves === 1 && s.turn === 1 && s.ply === 1, `${gpu}: placed with the pad (human moves ${s.human_moves}, ply ${s.ply})`);
    await h.step(420);
    s = await g();
    check(s.cpu_moves === 1 && s.turn === 0 && s.ply === 2, `${gpu}: the CPU replied through the pad's actions (cpu moves ${s.cpu_moves}, ply ${s.ply})`);
    await shot('3-after-cpu');
    /* the pointer: tap the first pick (a tray kind), then a space */
    const k = (h.loaded.fbWidth || 1280) / 1280; /* canvas px (1280 wide) to the cart's pixels */
    const pt = async (i, frames = 12) => {
      const px = (await g()).pick_px;
      const x = Math.round(px[2 * i] * k), y = Math.round(px[2 * i + 1] * k);
      await call('input', { op: 'pointer', x, y, left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x, y, left: false }, h.session);
      await h.step(frames);
    };
    await pt(0, 30);
    s = await g();
    check(s.phase === 2 && s.pointer_picks === 1, `${gpu}: a tap on a tray piece takes it (phase ${s.phase}, pointer picks ${s.pointer_picks})`);
    await shot('4-pointer-target');
    await pt(1, 40);
    s = await g();
    check(s.human_moves === 2 && s.ply === 3 && s.pointer_picks === 2, `${gpu}: a tap on a lit space places it (human moves ${s.human_moves}, ply ${s.ply})`);
    await h.step(420);
    s = await g();
    await shot('5-later');
    check(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = s.state_hash, frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 6 && !(await g()).replay_done; i++) await h.step(300);
    s = await g();
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
for (const control of ['set', 'nostart']) await play(gpus[0], { control });
console.log(failed ? `${failed} failed` : 'mandible play: all passed');
process.exit(failed ? 1 : 0);
