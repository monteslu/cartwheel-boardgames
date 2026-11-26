// Fever Line's rules headless (games/feverline/tests/test_rules.c): the map, setup, outbreaks and their chains, the
// Warden, surges, cures, every way to lose, determinism, random full games on every map and CPU games at 2, 3 and 4
// agents with a legal result, the CPU team against random play; each with a control that must fail. Also the maps'
// own check (games/feverline/tools/gen-maps.mjs: the files match the data, every map connected, nothing crossing,
// readable labels) and its control.
//
//   node tests/feverline-rules.mjs      (builds build/native first)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const fail = (m) => { console.error(`FAIL ${m}`); process.exit(1); };
const gen = join(ROOT, 'games/feverline/tools/gen-maps.mjs');
const map = spawnSync('node', [gen, '--check'], { encoding: 'utf8' });
if (map.status !== 0) fail(`map check\n${map.stdout}${map.stderr}`);
const ctl = spawnSync('node', [gen, '--control'], { encoding: 'utf8' });
if (ctl.status !== 0) fail(`map control not caught\n${ctl.stdout}${ctl.stderr}`);
console.log(`ok   ${map.stdout.trim()} (control caught)`);
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['feverline_test_rules'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) fail(`build\n${b.stdout}${b.stderr}`);
const exe = join(ROOT, 'build/native/games/feverline/feverline_test_rules');
if (!existsSync(exe)) fail(`no ${exe}`);
const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 900000 });
const out = `${r.stdout || ''}${r.stderr || ''}`;
const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
if (r.status !== 0) fail(`feverline rules (exit ${r.status ?? r.signal})\n${out}`);
if (controls < 11) fail(`feverline rules: ${controls} controls caught, expected 11\n${out}`);
console.log(out.split('\n').filter((l) => l.startsWith('ok')).join('\n'));
console.log(`ok   feverline rules (${controls} controls caught)`);
