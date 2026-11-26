// Rose Window's CPU levels measured: seeded CPU games (games/rosewindow/tests/rules_test.c `bench`) at two, three and
// four seats, run in parallel, then each level's wins, the head-to-head count of every pair of levels (who scored more
// in the same game), and the winning margins (the winner's score over the best other seat's).
//
//   node tests/rosewindow-strength.mjs [--seats 2,3,4] [--games N] [--seed S] [--jobs J]
//
// Env RW_REWARD=place|margin and RW_RSCALE=x pass to the bench (the MCTS seats' reward, to compare). The check: Hard
// outscores Normal and Normal outscores Easy head to head at every seat count. CONTROL that must fail: the same check
// on the same games with each game's levels given back at random.
import { spawn, spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const arg = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const SEATS = arg('--seats', '2,3,4').split(',').map(Number);
const GAMES = Number(arg('--games', '20'));
const SEED = Number(arg('--seed', '7000'));
const JOBS = Number(arg('--jobs', '16'));
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['rosewindow_rules_test'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) { console.error(`FAIL build\n${b.stdout}${b.stderr}`); process.exit(1); }
const exe = join(ROOT, 'build/native/games/rosewindow/rosewindow_rules_test');

const tasks = [];
for (const p of SEATS) for (let k = 0; k < GAMES; k++) tasks.push([p, SEED + k]);
const games = [];
let next = 0;
const run1 = ([p, seed]) => new Promise((res, rej) => {
  const c = spawn(exe, ['bench', String(p), String(seed), '1'], { env: process.env });
  let out = '', err = '';
  c.stdout.on('data', (d) => { out += d; });
  c.stderr.on('data', (d) => { err += d; });
  c.on('close', (code) => {
    if (code !== 0) return rej(new Error(`bench ${p} ${seed} exit ${code}\n${err}`));
    for (const line of out.split('\n')) {
      const m = line.match(/^game (\d) (\d+) (.*) w(\d+)$/);
      if (!m) continue;
      const seats = m[3].split(' ').map((t) => t.split(':').map(Number));
      games.push({ players: Number(m[1]), seed: Number(m[2]), levels: seats.map((s) => s[0]), scores: seats.map((s) => s[1]), w: Number(m[4]) });
    }
    res();
  });
});
await Promise.all(Array.from({ length: JOBS }, async () => { while (next < tasks.length) await run1(tasks[next++]); }));

const NAME = ['Easy', 'Normal', 'Hard'];
const median = (a) => { const s = [...a].sort((x, y) => x - y); return s.length ? s[Math.floor(s.length / 2)] : 0; };
function summary(gs) {
  const out = {};
  for (const p of SEATS) {
    const g = gs.filter((x) => x.players === p);
    const wins = [0, 0, 0], seats = [0, 0, 0], h2h = {}, margins = [];
    for (const x of g) {
      const nw = x.scores.filter((_, i) => (x.w >> i) & 1).length;
      x.levels.forEach((lv, i) => { seats[lv]++; if ((x.w >> i) & 1) wins[lv] += 1 / nw; });
      const top = Math.max(...x.scores), second = Math.max(...x.scores.filter((_, i) => !((x.w >> i) & 1)), -1000);
      margins.push(second > -1000 ? top - second : 0);
      for (let i = 0; i < p; i++) for (let j = 0; j < p; j++) {
        const a = x.levels[i], c = x.levels[j];
        if (i === j || a === c) continue;
        const key = `${a}${c}`;
        h2h[key] ??= [0, 0];
        if (x.scores[i] > x.scores[j]) h2h[key][0]++;
        else if (x.scores[i] < x.scores[j]) h2h[key][1]++;
      }
    }
    out[p] = { games: g.length, wins, seats, h2h, margins };
  }
  return out;
}
const ordered = (s) => SEATS.every((p) => { const h = s[p].h2h; return h['21'] && h['10'] && h['21'][0] > h['21'][1] && h['10'][0] > h['10'][1]; });

const s = summary(games);
console.log(`reward ${process.env.RW_REWARD || 'margin'} scale ${process.env.RW_RSCALE || 'game default'}, seeds ${SEED}..${SEED + GAMES - 1}`);
for (const p of SEATS) {
  const x = s[p];
  const wins = x.wins.map((w, lv) => x.seats[lv] ? `${NAME[lv]} ${w.toFixed(1)}/${x.seats[lv] / (p === 4 && lv === 1 ? 2 : 1)}` : null).filter(Boolean).join(', ');
  const h = (a, c) => x.h2h[`${a}${c}`] ? `${x.h2h[`${a}${c}`][0]} to ${x.h2h[`${a}${c}`][1]}` : '-';
  console.log(`  ${p} seats, ${x.games} games: wins ${wins}; head to head Hard-Normal ${h(2, 1)}, Hard-Easy ${h(2, 0)}, Normal-Easy ${h(1, 0)}; winning margin median ${median(x.margins)} (mean ${(x.margins.reduce((a, c) => a + c, 0) / x.margins.length).toFixed(1)})`);
}
let failed = 0;
if (ordered(s)) console.log('ok   Hard outscores Normal and Normal outscores Easy head to head at every seat count');
else { console.log('FAIL the levels are not ordered at every seat count'); failed++; }
/* the control: levels given back at random per game (seeded) */
let r = 12345;
const rnd = () => ((r = (r * 1103515245 + 12345) >>> 0) / 4294967296);
const shuffled = games.map((x) => { const lv = [...x.levels]; for (let i = lv.length - 1; i > 0; i--) { const j = Math.floor(rnd() * (i + 1)); [lv[i], lv[j]] = [lv[j], lv[i]]; } return { ...x, levels: lv }; });
const cs = summary(shuffled);
if (!ordered(cs)) console.log('CONTROL levels shuffled per game are not ordered: caught');
else { console.log('FAIL control: shuffled levels still read as ordered'); failed++; }
process.exit(failed ? 1 : 0);
