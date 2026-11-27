// Kilowatt Country played through romdev on GL and on WebGPU: a person's choices on the pad, the computer's turns, a
// house built by the pointer, the cities lighting up, and a replay of the match from its own recording, with pictures
// to look at.
//
//   node tests/kilowattcountry-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/kilowattcountry]
//
// One run per GPU (seats: a person on pad 1 as Amber and a Normal computer as Cobalt, seed 3):
//   - the title, on a load of its own: "How to play" and "Credits" open their pages (the shell's screen 6) and back
//     returns to the title; then the play run's test switch starts the match from the shell's menu tick;
//   - the setup card: it opens on the first map with the cursor on Begin; the pad picks another map (up, confirm),
//     the pointer picks France (a tap, a second tap), and the pad walks down to Begin and starts the match there;
//   - the how-to-play card is open at the start; confirm closes it;
//   - the pad: on the person's first turn (an auction to open) hop left (the cursor moves), then confirm what the
//     cursor offers; every later choice up to the build is confirmed where the cursor starts (the suggestion);
//   - the computer plays its turns through the pad's actions;
//   - the pointer: in the build, tap a city (the cursor and the ghost house go there), tap it again (built);
//   - the round's power lights the houses (kc.lit);
//   - `replay`: the match so far replayed from its input recording (pad and computer presses as actions, the
//     pointer through the engine's pointer channel) must reach the same state and input hash.
// Both GPUs must end in the same state hash at the same frame. CONTROLS that must fail: the same replay with
// CW_TEST_SET_UNRECORDED=1 (set actions, the computer's presses, left out of the recording), with
// CW_TEST_POINTER_UNRECORDED=1 (the pointer left out), and with no seats switch the title stays put; with
// KILOWATTCOUNTRY_TEST_HIDE=map the setup card offers no map plates and the pad cannot change the map. The city names:
// none runs into another or sits on its own city on any map and any set of regions a match can deal there
// (kc.label_overlaps 0 once kc.label_maps covers every map), and no name hides another city or needs a leader that runs
// over another city or name (kc.label_strays 0); CONTROL: KILOWATTCOUNTRY_LABELS=below (every name straight under its
// city, as before) runs names together and leaves misleading names. Every name is nearer its own city's dot than any
// other, or has a leader line to its own (kc.label_misread 0); CONTROL: KILOWATTCOUNTRY_LABELS=footprint (leaders only
// where another city's footprint is as near, the 0.3.0 rule) leaves names a player reads as a neighbour's.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER) {
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'kilowattcountry-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/kilowattcountry');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const tap = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });
const PH = { auction: 0, discard: 1, fuel: 2, build: 3, over: 4 };

// the title's pages, on a load of their own (the seats switch is read on every menu tick, so a play run sends it first)
async function titlePages(gpu) {
  const h = await openCart('kilowattcountry', { gpu, seed: 1, tag: `${gpu}-title` });
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
  const h = await openCart('kilowattcountry', { gpu, seed: 1, tag: `${gpu}${control ? `-${control}` : ''}` });
  const st = async () => { const s = await h.state(); const o = {}; for (const [k, v] of Object.entries(s)) if (k.startsWith('kc.') || k.startsWith('cw.three')) o[k.slice(k.indexOf('.') + 1)] = v; return o; };
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  /* step in ranges until seat 0 may choose (its turn, nothing animating) */
  const untilMine = async (limit = 6000) => {
    let s = await st();
    for (let n = 0; (s.ntargets === 0 || s.turn !== 0 || s.busy || s.over) && n < limit; n += 30) { await h.step(30); s = await st(); }
    return s;
  };
  try {
    const sw = { set: ' CW_TEST_SET_UNRECORDED=1', pointer: ' CW_TEST_POINTER_UNRECORDED=1', nostart: '', labels: ' KILOWATTCOUNTRY_LABELS=below', footprint: ' KILOWATTCOUNTRY_LABELS=footprint', map: ' KILOWATTCOUNTRY_TEST_HIDE=map' }[control] ?? '';
    await h.command(control === 'nostart' ? 'env KILOWATTCOUNTRY_SEED=3' : `env KILOWATTCOUNTRY_SEATS=p,c1 KILOWATTCOUNTRY_SEED=3${sw}`);
    await h.step(90);
    if (control === 'nostart') {
      const t = (await h.state())['shell.screen'];
      check(t === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${t}): caught`);
      return null;
    }
    let s = await st();
    for (let n = 0; n < 10 && s.label_maps < 7; n++) { await h.step(10); s = await st(); }
    if (control === 'footprint') {
      check(s.label_maps === 7 && s.label_misread > 0, `CONTROL ${gpu}: leaders drawn by the old footprint rule leave names nearer another city's dot with no leader (${s.label_misread}): caught`);
      return null;
    }
    if (control === 'labels') {
      check(s.label_maps === 7 && s.label_overlaps > 0, `CONTROL ${gpu}: every name straight under its city runs names together on some map (${s.label_overlaps} over ${s.label_maps} maps): caught`);
      check(s.label_strays > 0, `CONTROL ${gpu}: every name straight under its city leaves names that hide a city or point the wrong way (${s.label_strays}): caught`);
      return null;
    }
    check(s.label_maps === 7 && s.label_overlaps === 0, `${gpu}: no city name runs into another or sits on its own city, on all ${s.label_maps} maps and every set of regions a match deals there (${s.label_overlaps}; over another city, a tag or a link: ${s.label_link_hits})`);
    check(s.label_misread === 0, `${gpu}: every city name is nearer its own city's dot than any other, or has a leader line to it, on every map (${s.label_misread} misread)`);
    check(s.label_strays === 0, `${gpu}: no city name hides another city and no leader runs over another city or name, on every map (${s.label_strays})`);
    /* the setup card */
    if (control !== 'map') check(s.setup === 1 && s.map === 0 && s.ntargets === 8 && s.cursor === 7, `${gpu}: the setup card opens on the first map, the cursor on Begin (setup ${s.setup}, map ${s.map}, targets ${s.ntargets}, cursor ${s.cursor})`);
    await shot('1-setup');
    let at = await h.now();
    await h.timeline([tap(at + 5, 'up'), tap(at + 20, 'south')], at + 40);
    s = await st();
    if (control === 'map') {
      /* (only Begin is left: the presses start the match, still on the first map) */
      check(s.map === 0 && s.map_picks === 0, `CONTROL ${gpu}: with no map plates the pad cannot change the map (map ${s.map}, picks ${s.map_picks}, targets ${s.ntargets}): caught`);
      return null;
    }
    check(s.setup === 1 && s.map === 6 && s.map_picks === 1, `${gpu}: the pad picks the map above Begin (map ${s.map}, picks ${s.map_picks})`);
    await shot('1-setup-pad');
    const k = (h.loaded.fbWidth || 1280) / 1280; /* canvas px (1280 wide) to the cart's pixels */
    const pt = async (i, frames = 12) => {
      const px = (await st()).target_px;
      const x = Math.round(px[2 * i] * k), y = Math.round(px[2 * i + 1] * k);
      await call('input', { op: 'pointer', x, y, left: true }, h.session);
      await h.step(2);
      await call('input', { op: 'pointer', x, y, left: false }, h.session);
      await h.step(frames);
    };
    await pt(3);
    s = await st();
    check(s.cursor === 3 && s.map === 6, `${gpu}: a tap on a map moves the cursor there (cursor ${s.cursor}, map ${s.map})`);
    await pt(3, 20);
    s = await st();
    check(s.map === 3 && s.map_picks === 2 && s.pointer_picks === 1, `${gpu}: a second tap picks it (map ${s.map}, pointer picks ${s.pointer_picks})`);
    await shot('1-setup-pointer');
    at = await h.now();
    await h.timeline([tap(at + 5, 'down'), tap(at + 15, 'down'), tap(at + 25, 'down'), tap(at + 35, 'down'), tap(at + 50, 'south')], at + 90);
    s = await st();
    check(s.setup === 0 && s.map === 3, `${gpu}: the pad walks down to Begin and the match begins on the map picked (setup ${s.setup}, map ${s.map})`);
    check(s.help === 1 && s.round === 1 && s.phase === PH.auction, `${gpu}: the match opens on the how-to-play card in round 1 (help ${s.help}, phase ${s.phase})`);
    await shot('1-card');
    at = await h.now();
    await h.timeline([tap(at + 5, 'south')], at + 30);
    s = await untilMine();
    check(s.help === 0 && s.turn === 0 && s.phase === PH.auction, `${gpu}: card closed, Amber's turn in the auction (help ${s.help}, turn ${s.turn})`);
    const c0 = s.cursor, d0 = s.decisions;
    at = await h.now();
    await h.timeline([tap(at + 5, 'left')], at + 20);
    s = await st();
    check(s.cursor !== c0 && s.decisions === d0, `${gpu}: the pad moves the cursor along the market (target ${c0} to ${s.cursor})`);
    await shot('2-market');
    at = await h.now();
    await h.timeline([tap(at + 5, 'south')], at + 20);
    s = await st();
    check(s.decisions === d0 + 1, `${gpu}: confirm opens an auction (decisions ${s.decisions})`);
    /* every later choice up to Amber's build: confirm where the cursor starts */
    for (let i = 0; i < 40; i++) {
      s = await untilMine();
      if (s.phase === PH.build || s.over) break;
      if (s.phase === PH.fuel && s.mode === 0 && i < 30) await shot('3-fuel');
      at = await h.now();
      await h.timeline([tap(at + 3, 'south')], at + 12);
    }
    check(s.phase === PH.build && s.turn === 0 && s.cpu_moves >= 1, `${gpu}: through the auction and the fuel to Amber's build (computer moves ${s.cpu_moves}, phase ${s.phase})`);
    /* the pointer: tap the first city target twice */
    const d1 = s.decisions, cities0 = s.cities[0], picks0 = s.pointer_picks;
    await pt(0, 20);
    s = await st();
    check(s.cursor === 0 && s.decisions === d1, `${gpu}: a tap on a city moves the cursor there, the ghost house shows (target ${s.cursor})`);
    await shot('4-ghost-house');
    await pt(0, 60);
    s = await st();
    check(s.pointer_picks === picks0 + 1 && s.cities[0] === cities0 + 1, `${gpu}: a second tap builds there (pointer picks ${s.pointer_picks}, cities ${s.cities[0]})`);
    await shot('5-built');
    /* done building; the round's power lights the houses */
    for (let i = 0; i < 20 && s.round === 1; i++) {
      s = await untilMine(600);
      if (s.round > 1) break;
      if (s.turn === 0 && !s.busy) { at = await h.now(); await h.timeline([tap(at + 3, 'south')], at + 12); }
    }
    await h.step(300);
    s = await st();
    check(s.round === 2 && s.lit > 0, `${gpu}: round 2, the powered cities glow (houses lit ${s.lit}, lit ${s.powered})`);
    await shot('6-lit');
    check(s['three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['three.missing']})`);
    const hash = s.hash, frame = h.frame;
    await h.command('replay');
    for (let i = 0; i < 12 && !(await st()).replay_done; i++) await h.step(300);
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
for (const control of ['set', 'pointer', 'nostart', 'labels', 'footprint', 'map']) await play(gpus[0], { control });
console.log(failed ? `${failed} failed` : 'kilowattcountry play: all passed');
process.exit(failed ? 1 : 0);
