// Mandible's rules tests (games/mandible/tests/test_rules.c), built natively: perft, every movement rule with a
// control that must come out the other way, random games with undo round trips, seeded CPU games, and with
// --levels N the CPU level ladder over N seeded games a pair (each level must beat the one below it clearly).
//
//   node tests/mandible-rules.mjs [--levels N]
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const li = argv.indexOf('--levels');
const levels = li >= 0 ? argv[li + 1] : '0';
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['mandible_test_rules'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) { console.error(`FAIL build\n${b.stdout}${b.stderr}`); process.exit(1); }
const exe = join(ROOT, 'build/native/games/mandible', 'mandible_test_rules');
if (!existsSync(exe)) { console.error(`FAIL no ${exe}`); process.exit(1); }
const r = spawnSync(exe, [levels], { encoding: 'utf8', timeout: 1800000 });
const out = `${r.stdout || ''}${r.stderr || ''}`;
const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
process.stdout.write(out);
if (r.status !== 0) { console.error(`FAIL mandible rules (exit ${r.status ?? r.signal})`); process.exit(1); }
if (!controls) { console.error('FAIL mandible rules ran no control that must fail'); process.exit(1); }
console.log(`ok   mandible rules (${controls} controls caught)`);
