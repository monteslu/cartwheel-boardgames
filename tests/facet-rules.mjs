// Facet's rules headless (games/facet/tests/rules_test.c, cpu_test.c): rules, stone and card conservation, legality,
// hand-backs, patrons and the end, random and greedy full games at 2, 3 and 4 seats; a full game of computer
// guilds at every level and seat count to a legal winner, and the same seed playing the same game; each with a
// control that must fail. With --strength N also the levels' two-seat win rates over N seeded games per pairing
// (seats swapped), which must be ordered (Normal over Easy, Hard over Normal, Hard over Easy).
//
//   node tests/facet-rules.mjs [--strength N]      (builds build/native first)
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const si = argv.indexOf('--strength');
const b = spawnSync(join(ROOT, 'scripts/build.sh'), ['facet_tests'], { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) { console.error(`FAIL build\n${b.stdout}${b.stderr}`); process.exit(1); }
let failed = 0;
for (const [name, args, want] of [['facet_rules_test', [], 4], ['facet_cpu_test', [si >= 0 ? argv[si + 1] || '20' : '0'], 1]]) {
  const exe = join(ROOT, 'build/native/games/facet', name);
  if (!existsSync(exe)) { console.error(`FAIL no ${exe}`); failed++; continue; }
  const r = spawnSync(exe, args, { encoding: 'utf8', timeout: 1800000 });
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
  if (r.status !== 0) { console.error(`FAIL ${name} (exit ${r.status ?? r.signal})\n${out}`); failed++; continue; }
  if (controls < want) { console.error(`FAIL ${name}: ${controls} controls caught, expected ${want}\n${out}`); failed++; continue; }
  console.log(out.split('\n').filter((l) => / vs |greedy|all level/.test(l)).join('\n'));
  console.log(`ok   ${name} (${controls} control${controls > 1 ? 's' : ''} caught)`);
}
process.exit(failed ? 1 : 0);
