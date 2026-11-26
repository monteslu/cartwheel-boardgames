// First Hearth's rules headless (games/firsthearth/tests/test_rules.c): setup and closed sites, dice outcome weights,
// tools, scoring, goods and building conservation, determinism, random and CPU full games at 2, 3 and 4 seats with
// a legal winner, the CPU beating random play; each with a control that must fail. Also the card and building
// balance check (games/firsthearth/tools/gen-data.mjs) and its control.
//
//   node tests/firsthearth-rules.mjs      (builds build/native first)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const fail = (m) => { console.error(`FAIL ${m}`); process.exit(1); };
const gen = join(ROOT, 'games/firsthearth/tools/gen-data.mjs');
const bal = spawnSync('node', [gen, '--check'], { encoding: 'utf8' });
if (bal.status !== 0) fail(`balance check\n${bal.stdout}${bal.stderr}`);
const ctl = spawnSync('node', [gen, '--control'], { encoding: 'utf8' });
if (ctl.status !== 0) fail(`balance control not caught\n${ctl.stdout}${ctl.stderr}`);
console.log(`ok   balance: ${bal.stdout.trim().split('\n')[0]} (control caught)`);
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['firsthearth_test_rules'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) fail(`build\n${b.stdout}${b.stderr}`);
const exe = join(ROOT, 'build/native/games/firsthearth/firsthearth_test_rules');
if (!existsSync(exe)) fail(`no ${exe}`);
const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 900000 });
const out = `${r.stdout || ''}${r.stderr || ''}`;
const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
if (r.status !== 0) fail(`firsthearth rules (exit ${r.status ?? r.signal})\n${out}`);
if (controls < 6) fail(`firsthearth rules: ${controls} controls caught, expected 6\n${out}`);
console.log(out.split('\n').filter((l) => /^(\d players|CPU game)/.test(l)).join('\n'));
console.log(`ok   firsthearth rules (${controls} controls caught)`);
