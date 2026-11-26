// crokinole's rules and physics tests (games/crokinole/tools/crk_test.c), headless: the trig, a twenty, the open-board and
// must-hit fouls, a head-on hit, no disc ever through another or a peg over hundreds of full-power shots, full CPU games
// at two, three and four seats ending with a winner and the same hash for the same seed. Each check runs a control that
// must fail (the test prints "control:" checks among its own); with --levels N it also plays N seeded games of each
// pairing of CPU levels and prints the win rates.
//
// With --levels-game N the CPUs play as the game runs them (cw_search with each level's mistakes, then the hand's error).
//
//   node tests/crokinole-rules.mjs [--levels N | --levels-game N]
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['crokinole_test'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) { console.error(`FAIL build\n${b.stdout}${b.stderr}`); process.exit(1); }
const argv = process.argv.slice(2);
const opt = (name) => argv.includes(name) ? argv[argv.indexOf(name) + 1] || '20' : null;
const args = opt('--levels-game') ? ['levels-game', opt('--levels-game')] : opt('--levels') ? ['levels', opt('--levels')] : [];
const r = spawnSync(join(ROOT, 'build/native/games/crokinole/crokinole_test'), args, { encoding: 'utf8', timeout: 3600000 });
process.stdout.write(r.stdout || '');
process.stderr.write(r.stderr || '');
if (r.status !== 0 || !/all passed/.test(r.stdout || '')) { console.error('FAIL crokinole rules'); process.exit(1); }
console.log('ok   crokinole rules');
