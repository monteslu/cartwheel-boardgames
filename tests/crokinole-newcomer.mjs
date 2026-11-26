// crokinole's newcomer gate: can someone who has never played finish a game against the computer from what the screen
// shows, on a pad alone?
//
// A bot plays the real cart (crokinole.wasc) through romdev, on GL and WebGPU, as a newcomer would:
//   - it reaches the match with presses: Play on the title, joins a seat, Start (alone, the game seats a CPU across the
//     board), then closes the how-to card;
//   - on its turn it knows only what is drawn: its disc on the shooting line and the dashed aim line (aim.disc_*,
//     aim.dir_*), whether the line ends on a highlighted opposing disc (ghost.opp: the green ring), the line under the
//     turn banner (board.open: "Open board: come to rest inside the 15 ring"), the centre hole where it always is, and
//     while charging the ring where the disc would stop (ghost.stop_mm, its distance along the line) and the power
//     meter (aim.meter). Never the rules state;
//   - it plays with a person's error: its aim settles a step or two off and it lets go a little early or late;
//   - pad only, every move the game has: the d-pad slides, the shoulders aim, up and down set spin (the Spin setting is
//     on: CRK_SPIN=1), south charges and shoots; on its first turn it also tries spin and charges once and puts the
//     charge down (east), as someone finding the controls would, and once watches the last shot again (north).
// It passes when the match finishes (match.finished), at most maxFoulShare of its shots were fouls and it used every
// move type (game.slides, aim turns, game.spins, game.cancels, game.replays, its flicks).
//
// The CONTROL must fail: the same bot ignoring the cues (it never turns its aim and lets go at a random power), as a
// player who cannot read the line would. It fouls on most shots against discs it cannot hit straight on.
//
//   node tests/crokinole-newcomer.mjs [--gpu gl|webgpu|both] [--seed N] [--no-control] [--max-frames N]
import { request as httpRequest } from 'node:http';
import { existsSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GPUS = opt('gpu', 'both') === 'both' ? ['gl', 'webgpu'] : [opt('gpu', 'gl')];
const SEED = +opt('seed', 3);
const MAX_FRAMES = +opt('max-frames', 40000);
const MAX_FOUL_SHARE = 0.34;
const OUT = join(ROOT, 'out/gate/crokinole-newcomer');
mkdirSync(OUT, { recursive: true });
const CART = join(resolve(ROOT, process.env.BG_CART_DIR || 'build/cart-wgpu/carts'), 'crokinole.wasc');
const ROMDEV = new URL(process.env.ROMDEV_URL || 'http://127.0.0.1:7331');

function call(tool, args, session, timeoutMs = 900000) {
  const body = JSON.stringify(args || {});
  return new Promise((res, rej) => {
    const req = httpRequest({ host: ROMDEV.hostname, port: +ROMDEV.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session } }, (r) => {
      let data = '';
      r.setEncoding('utf8');
      r.on('data', (c) => { data += c; });
      r.on('end', () => {
        let b = data;
        try { b = JSON.parse(data); } catch { /* text */ }
        if (b && Array.isArray(b.content)) {
          const t = b.content.find((c) => c.type === 'text');
          if (b.isError) return rej(new Error(`romdev ${tool}: ${t ? t.text : data}`));
          try { return res(JSON.parse(t.text)); } catch { return res(t ? t.text : b); }
        }
        if (r.statusCode !== 200 || (b && (b.isError || b.error))) return rej(new Error(`romdev ${tool}: http ${r.statusCode} ${data.slice(0, 500)}`));
        res(b);
      });
    });
    const timer = setTimeout(() => req.destroy(new Error(`romdev ${tool}: no reply in ${timeoutMs} ms`)), timeoutMs);
    req.on('error', (e) => { clearTimeout(timer); rej(e); });
    req.on('close', () => clearTimeout(timer));
    req.end(body);
  });
}

function rng(seed) {
  let a = seed >>> 0;
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t ^= t + Math.imul(t ^ (t >>> 7), 61 | t); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

const LOOK = ['game.phase', 'game.turn', 'game.overlay', 'aim.cpu', 'aim.disc_x', 'aim.disc_y', 'aim.dir_x', 'aim.dir_y', 'aim.aim', 'aim.pos',
  'aim.meter', 'ghost.opp', 'ghost.stop_mm', 'board.open', 'match.finished', 'match.winner', 'match.score', 'seat.shots', 'seat.fouls', 'game.shots',
  'aim.spin', 'aim.charging', 'game.slides', 'game.spins', 'game.cancels', 'game.replays', 'game.pad_shots'];

async function open(gpu, tag) {
  if (!existsSync(CART)) throw new Error(`no ${CART}: build it (scripts/build.sh)`);
  const session = `crokinole-newcomer-${tag}-${gpu}-${process.pid}`;
  const loaded = await call('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', wgpu: gpu === 'webgpu', deterministicSeed: SEED,
    firstCommand: 'env CRK_TARGET=1 CRK_SPIN=1' }, session);
  if (gpu === 'webgpu' && loaded.gpu !== 'webgpu') throw new Error(`asked for WebGPU, runs on ${loaded.gpu}`);
  let frames = 1;
  const h = {
    session,
    get frames() { return frames; },
    /** segments [{ buttons, frames }] in ONE request; the fields after it (stopWhen ends it early) */
    async run(segments, stopWhen = null) {
      const steps = segments.filter((s) => s.frames > 0).map((s) => ({ input: { ports: [Object.fromEntries((s.buttons || []).map((b) => [b, true]))] }, frames: s.frames }));
      const total = steps.reduce((a, s) => a + s.frames, 0);
      const r = await call('input', { op: 'sequence', steps, fields: LOOK, ...(stopWhen ? { stopWhen } : {}) }, session, 600000 + total * 3000);
      if (!r.fields) throw new Error('romdev returned no fields with the input sequence');
      frames += r.framesRun;
      const f = {};
      for (const [k, v] of Object.entries(r.fields)) f[k] = v && typeof v === 'object' && 'value' in v ? v.value : v;
      return f;
    },
    shot: (file) => call('frame', { op: 'screenshot', path: resolve(file) }, session),
    close: () => call('host', { op: 'shutdown' }, session).catch(() => {}),
  };
  return h;
}

const idle = (n) => ({ buttons: [], frames: n });
const press = (b, n = 3) => ({ buttons: [b], frames: n });

async function play(gpu, blind) {
  const tag = blind ? 'control' : 'newcomer';
  const h = await open(gpu, tag);
  const r = rng(SEED * 7919 + (blind ? 1 : 0));
  const log = [];
  try {
    /* the menus: Play, join, Start, then the how-to card */
    let f = await h.run([idle(30), press('south'), idle(30), press('south'), idle(20), press('start'), idle(60)]);
    for (let i = 0; i < 6 && f['game.overlay'] !== 1; i++) f = await h.run([idle(30)]);
    if (f['game.overlay'] !== 1) throw new Error('the match never showed its how-to card (the menus did not lead to play)');
    f = await h.run([idle(25), press('south'), idle(10)]);
    let myShots = 0, aimTurns = 0, tried = false, replayed = false;
    while (h.frames < MAX_FRAMES && !(f['match.finished'] >= 1)) {
      if (f['game.phase'] === 3) { f = await h.run([idle(60), press('south'), idle(20)]); continue; } /* a round card: next round */
      const mine = f['game.phase'] === 0 && f['game.turn'] === 0 && !f['aim.cpu'] && !f['game.overlay'];
      if (!mine) { f = await h.run([idle(40)]); continue; }
      if (!blind && !tried) { /* finding the controls: a step of spin each way, then a charge put down */
        f = await h.run([press('up', 1), idle(8), press('down', 1), idle(8), press('south', 30), { buttons: ['south', 'east'], frames: 3 }, idle(10)]);
        tried = true;
      }
      if (!blind && !replayed && f['game.shots'] > 0) { /* the last shot again, then back to aiming */
        f = await h.run([press('north', 3), idle(20)]);
        f = await h.run([idle(900)], { field: 'game.phase', equals: 0 });
        replayed = true;
      }
      const open = f['board.open'] === 1;
      const aimBefore = f['aim.aim'];
      if (blind) { /* straight in from where it stands, any power */
        f = await h.run([press('south', 20 + Math.floor(r() * 150)), idle(2)]);
      } else if (open) {
        /* turn the line onto the hole, then let go when the ring sits on it */
        for (let tries = 0; tries < 12; tries++) {
          const want = Math.atan2(-f['aim.disc_y'], -f['aim.disc_x']), have = Math.atan2(f['aim.dir_y'], f['aim.dir_x']);
          let d = want - have;
          while (d > Math.PI) d -= 2 * Math.PI;
          while (d < -Math.PI) d += 2 * Math.PI;
          const steps = Math.round(d / 0.0025);
          if (Math.abs(steps) <= 1) break;
          const b = steps > 0 ? 'l' : 'r';
          f = Math.abs(steps) > 40 ? await h.run([press(b, 12 + Math.min(40, Math.floor(Math.abs(steps) / 12))), idle(4)])
            : await h.run(Array.from({ length: Math.min(Math.abs(steps), 12) }, () => [press(b, 1), idle(2)]).flat());
        }
        const err = Math.round((r() - 0.5) * 4); /* a step or two off */
        if (err) f = await h.run([press(err > 0 ? 'l' : 'r', 1), idle(2)]);
        const dist = Math.hypot(f['aim.disc_x'], f['aim.disc_y']) * 1000 + (r() - 0.5) * 20;
        f = await h.run([press('south', 260)], { field: 'ghost.stop_mm', atLeast: Math.round(dist) });
        f = await h.run([idle(2)]);
      } else {
        /* sweep the line until it lands on a highlighted disc, then a firm shot */
        let found = false, dir = 'r';
        for (const [b, lim] of [['r', 2], ['l', 4]]) {
          for (let k = 0; k < lim && !found; k++) {
            f = await h.run([press(b, 90), idle(2)], { field: 'ghost.opp', equals: 1 });
            found = f['ghost.opp'] === 1, dir = b;
          }
          if (found) break;
        }
        if (!found) { /* slide along the line and look again */
          f = await h.run([press(r() < 0.5 ? 'left' : 'right', 40), idle(4)]);
          for (const b of ['r', 'l', 'l']) {
            f = await h.run([press(b, 150), idle(2)], { field: 'ghost.opp', equals: 1 });
            if ((found = f['ghost.opp'] === 1)) { dir = b; break; }
          }
        }
        if (found) { /* a few steps further into the disc, as a person centres the line */
          f = await h.run(Array.from({ length: 6 }, () => [press(dir, 1), idle(2)]).flat());
          if (f['ghost.opp'] !== 1) f = await h.run(Array.from({ length: 4 }, () => [press(dir === 'r' ? 'l' : 'r', 1), idle(2)]).flat());
        }
        const meter = 95 + Math.floor(r() * 50);
        f = await h.run([press('south', 260)], { field: 'aim.meter', atLeast: meter });
        f = await h.run([idle(2)]);
      }
      myShots++;
      if (f['aim.aim'] !== aimBefore) aimTurns++;
      const before = f['seat.fouls'][0];
      f = await h.run([idle(900)], { field: 'game.phase', equals: 2 });
      f = await h.run([idle(2)]);
      log.push({ frame: h.frames, open, meter: f['aim.meter'], fouled: f['seat.fouls'][0] > before });
      if (process.env.CRK_NEWCOMER_LOG) console.log(JSON.stringify(log[log.length - 1]));
    }
    await h.shot(join(OUT, `${tag}-${gpu}.png`));
    const shots = f['seat.shots'][0], fouls = f['seat.fouls'][0];
    const used = { slides: f['game.slides'], aimTurns, spins: f['game.spins'], cancels: f['game.cancels'], replays: f['game.replays'], flicks: f['game.pad_shots'] };
    return { used, finished: f['match.finished'] >= 1, shots, fouls, share: shots ? fouls / shots : 1, winner: f['match.winner'], score: f['match.score'], frames: h.frames, myShots };
  } finally {
    await h.close();
  }
}

let failed = 0;
for (const gpu of GPUS) {
  const a = await play(gpu, false);
  const everyMove = Object.values(a.used).every((n) => n > 0);
  const ok = a.finished && a.share <= MAX_FOUL_SHARE && everyMove;
  console.log(`${ok ? 'ok  ' : 'FAIL'} newcomer ${gpu}: finished ${a.finished}, ${a.fouls} fouls in ${a.shots} shots (${(a.share * 100).toFixed(0)}%), winner side ${a.winner}, score ${a.score}, ${a.frames} frames; moves used ${JSON.stringify(a.used)}`);
  if (!ok) failed++;
  if (!argv.includes('--no-control')) {
    const c = await play(gpu, true);
    const caught = !(c.finished && c.share <= MAX_FOUL_SHARE);
    console.log(`${caught ? 'CONTROL' : 'FAIL CONTROL'} blind ${gpu}: finished ${c.finished}, ${c.fouls} fouls in ${c.shots} shots (${(c.share * 100).toFixed(0)}%)${caught ? ': caught' : ' passed (the gate cannot tell a reader of the cues from a blind player)'}`);
    if (!caught) failed++;
  }
}
process.exit(failed ? 1 : 0);
