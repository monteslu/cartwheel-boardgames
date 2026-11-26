// Crokinole played through romdev on GL and on WebGPU, every move type through the real UI, with pictures to look at.
//
//   node tests/crokinole-play.mjs [--gpu gl|webgpu|gl,webgpu] [--out out/crokinole-play]
//
// One run per GPU (a person on pad 1 against a Normal CPU, one round, Spin on, seed 3):
//   - the title (a load of its own, no switch): "How to play" and "Credits" open their pages (the shell's screen 6) and
//     back returns to the title;
//   - the how-to card is up at the start and a mouse click closes it;
//   - the pad, on the person's turns: the d-pad slides the disc along the line, the shoulders turn the aim, up and down
//     set spin, south held charges and east puts the charge down (no shot), south held and let go flicks; north replays
//     the last shot;
//   - the pointer: the mouse drags along the arc to slide the disc, and a finger pulls back from the disc and lets go
//     (a pointer shot); a finger landing beside the board picks nothing up (no shot);
//   - the rest of the round on the pad, then a tap on the result card starts the next match;
//   - `input_replay`: the session so far replayed from its own input recording reaches the same state and input hash.
// Both GPUs must end in the same state hash at the same frame. CONTROLS that must fail: the same replay with
// CW_TEST_POINTER_UNRECORDED=1 (the pointer left out of the recording: the click, the drags and the tap are lost) must
// not match, and with no seats switch the title stays.
import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { openCart, call, gpusArg, ROOT } from './lib/romdev.mjs';

const argv = process.argv.slice(2);
if (!process.env.CW_GPU_SUITE_INNER && !process.env.CW_SUITE_LOCK_HELD) { /* a pool job (or a suite) already holds its GPU */
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/with-gpu-suite.mjs'), 'crokinole-play', process.execPath, ...process.argv.slice(1)],
    { stdio: 'inherit', env: { ...process.env, CW_GPU_SUITE_INNER: '1' } });
  process.exit(r.status ?? 1);
}
const outIdx = argv.indexOf('--out');
const OUT = resolve(ROOT, outIdx >= 0 ? argv[outIdx + 1] : 'out/crokinole-play');
mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const PH = { AIM: 0, FLIGHT: 1, RESULT: 2, ROUND: 3, OVER: 4 };
const tapB = (frame, button, len = 3) => ({ frame, buttons: [button], until: frame + len });

async function titlePages(gpu) {
  const h = await openCart('crokinole', { gpu, seed: 1, tag: `${gpu}-title` });
  const sh = async () => { const s = await h.state(); return { screen: s['shell.screen'], page: s['shell.page'] }; };
  try {
    await h.step(40);
    let t = await sh();
    check(t.screen === 1, `${gpu}: the title shows with no test switch (screen ${t.screen})`);
    await h.shot(join(OUT, `${gpu}-0-title.png`));
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
  const h = await openCart('crokinole', { gpu, seed: 1, tag: `${gpu}${control ? '-' + control : ''}` });
  const st = async () => h.state();
  const shot = async (name) => { if (!control) await h.shot(join(OUT, `${gpu}-${name}.png`)); };
  const k = (h.loaded.fbWidth || 1280) / 1280;
  const P = (v) => Math.round(v * k);
  /* holds on absolute frames, then the frames up to `len` after the start, in one call */
  const hold = async (button, frames, after = 20) => { const at = await h.now(); await h.timeline([{ frame: at + 2, buttons: [button], until: at + 2 + frames }], at + 2 + frames + after); };
  /* a pointer gesture: down at the first point, moved through the rest (a few frames each), up after the last */
  const gesture = async (pts, id = 0) => {
    const finger = id > 0;
    for (let i = 0; i < pts.length; i++) {
      await call('input', { op: 'pointer', id, x: P(pts[i][0]), y: P(pts[i][1]), left: true, ...(finger ? { active: true } : {}) }, h.session);
      await h.step(i === 0 ? 3 : 4);
    }
    const [x, y] = pts[pts.length - 1];
    await call('input', { op: 'pointer', id, x: P(x), y: P(y), left: false, ...(finger ? { active: false } : {}) }, h.session);
    await h.step(4);
  };
  /* wait (in ranges) until it is the person's turn to aim, or the round card is up */
  const myTurn = async () => {
    for (let i = 0; i < 80; i++) {
      const s = await st();
      if (s['game.phase'] === PH.ROUND || s['game.phase'] === PH.OVER) return s;
      if (s['game.phase'] === PH.AIM && s['game.turn'] === 0 && !s['aim.cpu'] && !s['game.overlay']) return s;
      await h.step(60);
    }
    throw new Error(`${gpu}: the person's turn never came`);
  };
  const seq = async (steps) => { await call('input', { op: 'sequence', steps: steps.map(([b, n]) => ({ input: { ports: [b ? { [b]: true } : {}] }, frames: n })) }, h.session); await h.now(); };
  const flick = async (frames) => { await hold('south', frames, 4); await h.step(30); };
  try {
    const sw = control === 'pointer' ? ' CW_TEST_POINTER_UNRECORDED=1' : '';
    await h.command(control === 'nostart' ? 'env CRK_SEED=3' : `env CRK_SEATS=p,c1 CRK_SEED=3 CRK_TARGET=1 CRK_IDLE=0 CRK_SPIN=1${sw}`);
    await h.step(60);
    if (control === 'nostart') {
      const t = (await st())['shell.screen'];
      check(t === 1, `CONTROL ${gpu}: with no seats switch the title stays (screen ${t}): caught`);
      return null;
    }
    let s = await st();
    check(s['game.overlay'] === 1 && s['game.spin_on'] === 1, `${gpu}: the how-to card is up at the start, Spin on (overlay ${s['game.overlay']}, spin ${s['game.spin_on']})`);
    await shot('1-howto');
    await h.step(30);
    await gesture([[640, 360]]); /* a click on the card */
    s = await st();
    check(s['game.overlay'] === 0 && s['game.shots'] === 0, `${gpu}: a click closes the card without a shot (overlay ${s['game.overlay']}, shots ${s['game.shots']})`);

    /* the pad: slide, aim, spin, charge and put down, then the flick */
    s = await myTurn();
    const pos0 = s['aim.pos'], aim0 = s['aim.aim'];
    await hold('right', 14);
    s = await st();
    check(s['aim.pos'] > pos0 && s['game.slides'] > 0, `${gpu}: the d-pad slides the disc along the line (place ${pos0} -> ${s['aim.pos']})`);
    await hold('r', 20);
    s = await st();
    check(s['aim.aim'] < aim0, `${gpu}: the right shoulder turns the aim (aim ${aim0} -> ${s['aim.aim']})`);
    await hold('l', 10);
    await hold('up', 1);
    await hold('up', 1);
    s = await st();
    check(s['aim.spin'] === 2 && s['game.spins'] === 2, `${gpu}: up sets spin (spin ${s['aim.spin']})`);
    await hold('down', 1);
    s = await st();
    check(s['aim.spin'] === 1, `${gpu}: down takes spin back (spin ${s['aim.spin']})`);
    {
      const at = await h.now(); /* south held, east while it charges: the charge is put down */
      await h.timeline([{ frame: at + 2, buttons: ['south'], until: at + 40 }, { frame: at + 30, buttons: ['south', 'east'], until: at + 33 }], at + 60);
    }
    s = await st();
    check(s['game.cancels'] === 1 && s['game.shots'] === 0 && s['aim.charging'] === 0, `${gpu}: east puts the charge down, no shot (cancels ${s['game.cancels']}, shots ${s['game.shots']})`);
    await seq([[null, 2], ['south', 64]]); /* the last frame still holds south: the charge is on screen */
    s = await st();
    check(s['aim.charging'] === 1 && s['aim.meter'] > 40, `${gpu}: south held charges the meter (meter ${s['aim.meter']})`);
    await shot('2-charge');
    await seq([[null, 10]]);
    s = await st();
    check(s['game.pad_shots'] === 1, `${gpu}: letting go flicks (pad shots ${s['game.pad_shots']})`);
    await h.step(40);
    await shot('3-flight');

    /* north: the last shot again */
    s = await myTurn();
    const rep0 = s['game.replays'];
    await hold('north', 3, 30);
    s = await st();
    check(s['game.replays'] === rep0 + 1 && s['game.phase'] === PH.FLIGHT, `${gpu}: north replays the last shot (replays ${s['game.replays']}, phase ${s['game.phase']})`);
    await shot('4-replay');
    s = await myTurn();

    /* the pointer: the mouse slides along the arc, a finger beside the board does nothing, a finger pulls and lets go */
    const dx = s['aim.disc_wx'], dy = s['aim.disc_wy'], posA = s['aim.pos'];
    await gesture([[dx + 160, dy - 10], [dx + 120, dy - 6], [dx + 60, dy - 2], [dx + 60, dy - 2]]);
    s = await st();
    check(s['aim.pos'] !== posA, `${gpu}: the mouse drags the disc along the arc (place ${posA} -> ${s['aim.pos']})`);
    const ps0 = s['game.pointer_shots'], sh0 = s['game.shots'];
    await gesture([[60, 700], [70, 704], [90, 710]], 2);
    s = await st();
    check(s['game.pointer_shots'] === ps0 && s['game.shots'] === sh0, `${gpu}: a finger beside the board makes no shot (shots ${s['game.shots']})`);
    const ex = s['aim.disc_wx'], ey = s['aim.disc_wy'];
    {
      /* the pull, held at its end for a picture of the charge it shows */
      const pts = [[ex, ey], [ex + 6, ey + 20], [ex + 12, ey + 45], [ex + 16, ey + 60]];
      for (let i = 0; i < pts.length; i++) {
        await call('input', { op: 'pointer', id: 1, x: P(pts[i][0]), y: P(pts[i][1]), left: true, active: true }, h.session);
        await h.step(i === 0 ? 3 : 4);
      }
      s = await st();
      check(s['aim.charging'] === 1 && s['aim.meter'] > 0, `${gpu}: a finger pulling back from the disc charges (meter ${s['aim.meter']})`);
      await shot('5-pull');
      await call('input', { op: 'pointer', id: 1, x: P(ex + 16), y: P(ey + 60), left: false, active: false }, h.session);
      await h.step(4);
    }
    s = await st();
    check(s['game.pointer_shots'] === ps0 + 1, `${gpu}: letting go flicks by touch (pointer shots ${s['game.pointer_shots']})`);

    /* the rest of the round on the pad (firm shots straight in), then a tap on the card */
    for (let i = 0; i < 30; i++) {
      s = await myTurn();
      if (s['game.phase'] === PH.ROUND || s['game.phase'] === PH.OVER) break;
      await flick(110);
    }
    s = await myTurn();
    check(s['game.phase'] === PH.OVER && s['match.finished'] === 1, `${gpu}: the round is played out and the match won (phase ${s['game.phase']}, finished ${s['match.finished']}, score ${s['match.score']})`);
    await h.step(100);
    await shot('6-result');
    const m0 = s['game.matches'];
    await gesture([[640, 400]], 1);
    s = await st();
    check(s['game.matches'] === m0 + 1 && s['game.phase'] === PH.AIM, `${gpu}: a tap on the result card starts the next match (matches ${s['game.matches']})`);
    check(s['cw.three.missing'] === 0, `${gpu}: every draw state has its program (missing ${s['cw.three.missing']})`);

    /* the replay of the whole session */
    const hash = s['game.hash'], frame = h.frame;
    await h.command('input_replay');
    for (let i = 0; i < 40 && !(await st())['crk.replay_done']; i++) await h.step(300);
    s = await st();
    if (control) check(s['crk.replay_done'] === 1 && !(s['crk.replay_match'] && s['crk.replay_hash_match']), `${gpu} CONTROL: with the pointer left out of the recording the replay differs (state ${s['crk.replay_match']}, input ${s['crk.replay_hash_match']}): caught`);
    else check(s['crk.replay_done'] === 1 && s['crk.replay_match'] === 1 && s['crk.replay_hash_match'] === 1, `${gpu}: the replay reaches the same state and input hash (done ${s['crk.replay_done']}, state ${s['crk.replay_match']}, input ${s['crk.replay_hash_match']})`);
    return { hash, frame };
  } finally {
    await h.close();
  }
}

const gpus = gpusArg(process.argv);
const runs = {};
for (const gpu of gpus) { await titlePages(gpu); runs[gpu] = await play(gpu); console.log(`${gpu}: ends in state ${runs[gpu].hash} at frame ${runs[gpu].frame}`); }
if (runs.gl && runs.webgpu) check(runs.gl.hash === runs.webgpu.hash && runs.gl.frame === runs.webgpu.frame, `GL and WebGPU end in the same state (${runs.gl.hash} at frame ${runs.gl.frame}, ${runs.webgpu.hash} at ${runs.webgpu.frame})`);
for (const control of ['pointer', 'nostart']) await play(gpus[0], { control });
console.log(failed ? `${failed} failed` : 'crokinole play: all passed');
process.exit(failed ? 1 : 0);
