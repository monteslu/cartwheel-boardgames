// Kilowatt Country's rules headless (games/kilowattcountry/tests/test_rules.c): on every map, setup at every player
// count, routes against an independent shortest path, invariants over random and playout games at 2, 3 and 4 seats
// with a legal winner, determinism and full CPU games; once, fuel prices and storage, auctions and a fourth plant and a
// CPU beating random play; each with a control that must fail. Also the maps and plant balance check
// (games/kilowattcountry/tools/gen-data.mjs: the files match the data, every map holds its rules) and its controls.
//
//   node tests/kilowattcountry-rules.mjs      (builds build/native first)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const fail = (m) => { console.error(`FAIL ${m}`); process.exit(1); };
const gen = join(ROOT, 'games/kilowattcountry/tools/gen-data.mjs');
const bal = spawnSync('node', [gen, '--check'], { encoding: 'utf8' });
if (bal.status !== 0) fail(`map and plant check\n${bal.stdout}${bal.stderr}`);
const ctl = spawnSync('node', [gen, '--control'], { encoding: 'utf8' });
if (ctl.status !== 0 || (ctl.stdout.match(/: caught/g) || []).length !== 2) fail(`map and plant controls not caught\n${ctl.stdout}${ctl.stderr}`);
console.log(`ok   ${bal.stdout.trim().split('\n')[0]} (2 controls caught)`);
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['kilowattcountry_test_rules'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) fail(`build\n${b.stdout}${b.stderr}`);
const exe = join(ROOT, 'build/native/games/kilowattcountry/kilowattcountry_test_rules');
if (!existsSync(exe)) fail(`no ${exe}`);
const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 900000 });
const out = `${r.stdout || ''}${r.stderr || ''}`;
const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
if (r.status !== 0) fail(`kilowattcountry rules (exit ${r.status ?? r.signal})\n${out}`);
if (controls < 25) fail(`kilowattcountry rules: ${controls} controls caught, expected 25 (3 a map, 4 once)\n${out}`);
console.log(out.split('\n').filter((l) => /^(map |\d players|CPU game|ok )/.test(l)).join('\n'));
console.log(`ok   kilowattcountry rules (${controls} controls caught)`);
