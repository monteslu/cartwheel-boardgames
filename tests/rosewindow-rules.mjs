// Rose Window's rules headless (games/rosewindow/tests/rules_test.c): placement, scoring, the frame table, dice conservation,
// determinism, random and CPU full games at 1 to 4 seats, each check with a control that must fail; with --strength N also
// the CPU levels' two-seat win rates over N seeded games per pairing (seats swapped), which must be ordered, and solo results.
//
//   node tests/rosewindow-rules.mjs [--strength N]      (builds build/native first)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const si = argv.indexOf('--strength');
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['rosewindow_rules_test'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) { console.error(`FAIL build\n${b.stdout}${b.stderr}`); process.exit(1); }
const exe = join(ROOT, 'build/native/games/rosewindow/rosewindow_rules_test');
if (!existsSync(exe)) { console.error(`FAIL no ${exe}`); process.exit(1); }
const r = spawnSync(exe, si >= 0 ? ['strength', argv[si + 1] || '40'] : [], { encoding: 'utf8', timeout: 900000 });
const out = `${r.stdout || ''}${r.stderr || ''}`;
const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
if (r.status !== 0) { console.error(`FAIL rosewindow rules (exit ${r.status ?? r.signal})\n${out}`); process.exit(1); }
const want = si >= 0 ? 5 : 4;
if (controls < want) { console.error(`FAIL rosewindow rules: ${controls} controls caught, expected ${want}\n${out}`); process.exit(1); }
console.log(out.split('\n').filter((l) => /^(strength| {2}(Normal|Hard|solo)|rosewindow rules)/.test(l)).join('\n'));
console.log(`ok   rosewindow rules (${controls} controls caught)`);
