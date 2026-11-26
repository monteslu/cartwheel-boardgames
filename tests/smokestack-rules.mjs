// Smokestack's rules headless (games/smokestack/tests/test_rules.c): setup, the auction, building and its costs,
// legality, goods conservation and whole-board invariants after every move of random and policy games at two,
// three and four players, determinism, CPU games through the engine search at every count with a legal winner;
// each check family with a control that must fail. Also the map generator's check (the committed maps are what
// tools/gen-map.mjs makes and keep its rules) and its control (a walled-in city must fail).
//
//   node tests/smokestack-rules.mjs [games-per-count]
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const fail = (m) => { console.error(`FAIL ${m}`); process.exit(1); };
const gen = join(ROOT, 'games/smokestack/tools/gen-map.mjs');
const chk = spawnSync('node', [gen, '--check'], { encoding: 'utf8' });
if (chk.status !== 0) fail(`map check\n${chk.stdout}${chk.stderr}`);
const ctl = spawnSync('node', [gen, '--control'], { encoding: 'utf8' });
if (ctl.status !== 0 || !/caught/.test(ctl.stdout)) fail(`map control not caught\n${ctl.stdout}${ctl.stderr}`);
console.log(`ok   maps (${ctl.stdout.trim()})`);
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['smokestack_test_rules'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) fail(`build\n${b.stdout}${b.stderr}`);
const exe = join(ROOT, 'build/native/games/smokestack/smokestack_test_rules');
if (!existsSync(exe)) fail(`no ${exe}`);
const r = spawnSync(exe, [process.argv[2] || '20'], { encoding: 'utf8', timeout: 1800000 });
const out = `${r.stdout || ''}${r.stderr || ''}`;
const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
process.stdout.write(out);
if (r.status !== 0) fail(`smokestack rules (exit ${r.status ?? r.signal})`);
if (controls < 5) fail(`smokestack rules: ${controls} controls caught, expected 5`);
console.log(`ok   smokestack rules (${controls} controls caught)`);
