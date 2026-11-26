// Smokestack played through romdev on GL and on WebGPU, with pictures to look at.
//
//   node tests/smokestack-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/smokestack]
//
// One run per GPU (seats: a person on pad 1 and a Normal CPU, seed 7):
//   - the title (a load of its own, no switch): "How to play" and "Credits" open their pages (the shell's screen 6)
//     and back returns to the title; then the test switch starts the match from the shell's menu tick;
//   - the how-to-play card is up at the start and the confirm action closes it;
//   - the pointer: two taps on the highlighted plaque drop out of the auction (a pointer pick, recorded);
//   - the pad: the person confirms whatever the cursor offers, decision after decision, and once takes a tile
//     back with the back action (undo) while choosing its exit; the CPU plays its turns through the pad's actions;
//   - pictures: the card, the auction, a tile's ghost preview, a train on its run, later in the game;
//   - `replay`: the match so far replayed from its own input recording must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame. CONTROLS that must fail: the same replay with
// CW_TEST_SET_UNRECORDED=1 (the CPU's presses left out of the recording) must not match; with
// CW_TEST_POINTER_UNRECORDED=1 (the pointer channel left out) neither; and with no seats switch the title stays.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'smokestack-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/smokestack');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const PH = { BID: 0, ACTION: 1, BUILD: 3, EXIT: 4, HAUL: 7, OVER: 11 };

const tapB = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

// the title's pages, on a load of their own (the seats switch is read on every menu tick, so a play run sends it first)
async function titlePages(gpu) {
  const h = await openCart('smokestack', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  const shot = async (name) => h.shot(join(OUT, `${gpu}-${name}.png`));
  try {
    await h.step(40);
    let t = await sh();
    check(t.screen === 1, `${gpu}: the title shows with no test switch (screen ${t.screen})`);
    const a0 = await h.now();
    await h.timeline([tapB(a0 + 5, 'down'), tapB(a0 + 20, 'south')], a0 + 50);
    t = await sh();
    check(t.screen === 6 && t.page === 0, `${gpu}: How to play opens its page (screen ${t.screen}, page ${t.page})`);
    await shot('0-how-to-play');
    const a1 = await h.now();
    await h.timeline([tapB(a1 + 5, 'east'), tapB(a1 + 25, 'down'), tapB(a1 + 40, 'south')], a1 + 70);
    t = await sh();
    check(t.screen === 6 && t.page === 1, `${gpu}: Credits opens its page (screen ${t.screen}, page ${t.page})`);
    await shot('0-credits');
    const a2 = await h.now();
    await h.timeline([tapB(a2 + 5, 'east')], a2 + 30);
    t = await sh();
    check(t.screen === 1, `${gpu}: back returns to the title (screen ${t.screen})`);
  } finally {
    await h.close();
  }
}

async function play(gpu, { control = '' } = {}) {
  const h = await openCart('smokestack', { gpu, seed: 1, tag: `${gpu}${control ? '-control' : ''}` });
  const rl = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('rl.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  const press = async (button, frames = 26) => { const at = await h.now(); await h.timeline([{ frame: at + 2, buttons: [button], until: at + 5 }], at + frames); };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env SMOKESTACK_SEED=7' : `env SMOKESTACK_SEATS=p,c1 SMOKESTACK_SEED=7${sw}`);
    await h.step(60);
    if (control === 'nostart') {
      const t = (await h.state())['shell.screen'];
      check(t === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${t}): caught`);
      return null;
    }
    let s = await rl();
    check(s.help === 1 && s.phase === PH.BID && s.turn === 0, `${gpu}: the how-to-play card is up, seat 1 to bid (help ${s.help}, phase ${s.phase}, turn ${s.turn})`);
    await shot('1-help');
    await press('south', 30);
    s = await rl();
    check(s.help === 0 && s.decisions === 0, `${gpu}: confirm closes the card without playing a move (help ${s.help}, decisions ${s.decisions})`);
    await shot('2-auction');
    /* the pointer: tap the highlighted plaque twice (look, then play) */
    const k = (h.loaded.fbWidth || 1280) / 1280;
    const tap = async () => {
      const px = (await rl()).target_px;
      await call('input', { op: 'pointer', x: Math.round(px[0] * k), y: Math.round(px[1] * k), left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x: Math.round(px[0] * k), y: Math.round(px[1] * k), left: false }, h.session);
      await h.step(10);
    };
    await tap();
    await tap();
    s = await rl();
    check(s.pointer_picks === 1 && s.phase !== PH.BID, `${gpu}: two taps on the highlighted plaque play it (pointer picks ${s.pointer_picks}, phase ${s.phase})`);
    /* the pad: confirm what the cursor offers; take one tile back while choosing its exit */
    let ghostShot = false, trainShot = false, undone = false, guard = 0;
    while (guard++ < 120) {
      s = await rl();
      if (s.phase === PH.OVER || (s.pad_moves >= 24 && undone && trainShot)) break;
      if (s.turn !== 0) { await h.step(60); continue; }
      if (s.phase === PH.EXIT && !ghostShot) { await shot('3-ghost'); ghostShot = true; }
      if (s.phase === PH.EXIT && !undone) {
        await press('east');
        const t = await rl();
        check(t.undos === 1 && t.phase === PH.BUILD, `${gpu}: back takes the tile back before it is laid (undos ${t.undos}, phase ${t.phase})`);
        undone = true;
        continue;
      }
      await press('south');
      const t = await rl();
      if (!trainShot && t.trains_run > 0) { await h.step(40); await shot('4-train'); trainShot = true; }
    }
    s = await rl();
    check(s.pad_moves >= 12 && s.cpu_moves >= 6, `${gpu}: the person and the CPU both play (pad moves ${s.pad_moves}, CPU moves ${s.cpu_moves})`);
    check(ghostShot && s.ghost_shown > 0, `${gpu}: a tile's ghost shows before it is laid (ghost frames ${s.ghost_shown})`);
    check(s.tiles_landed > 0 && s.trains_run > 0, `${gpu}: tiles land and a train runs (tiles ${s.tiles_landed}, trains ${s.trains_run})`);
    await shot('5-later');
    check(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = s.hash, frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 10 && !(await rl()).replay_done; i++) await h.step(300);
    s = await rl();
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
console.log(failed ? `${failed} failed` : 'smokestack play: all passed');
process.exit(failed ? 1 : 0);
