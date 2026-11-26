// The board-game kit's headless tests (games/common/boardkit/tests/test_*.c): each builds as a native program
// bk_test_<part> (no window, no GPU), checks its part deterministically and runs a control that must fail; it
// prints "CONTROL ... caught" for each control and exits non-zero on any failure.
//
//   node tests/boardkit.mjs [part ...]      (builds build/native first)
import { spawnSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dir = join(ROOT, 'games/common/boardkit/tests');
const all = readdirSync(dir).filter((f) => /^test_.*\.c$/.test(f)).map((f) => f.slice(0, -2)).sort();
const want = process.argv.slice(2);
const parts = want.length ? want.map((p) => (p.startsWith('test_') ? p : `test_${p}`)) : all;
if (!parts.length) { console.error('FAIL no kit tests found'); process.exit(1); }
for (const p of parts) if (!all.includes(p)) { console.error(`FAIL no test ${p} in ${dir}`); process.exit(1); }
const b = spawnSync(join(ROOT, 'scripts/build.sh'), parts.map((p) => `bk_${p}`), { env: { ...process.env, TARGETS: 'native' }, encoding: 'utf8' });
if (b.status !== 0) { console.error(`FAIL build\n${b.stdout}${b.stderr}`); process.exit(1); }
let failed = 0;
for (const p of parts) {
  const exe = join(ROOT, 'build/native', `bk_${p}`);
  if (!existsSync(exe)) { console.error(`FAIL ${p}: no ${exe}`); failed++; continue; }
  const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 300000 });
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const controls = (out.match(/^CONTROL .* caught$/gm) || []).length;
  if (r.status !== 0) { failed++; console.error(`FAIL ${p} (exit ${r.status ?? r.signal})\n${out}`); }
  else if (!controls) { failed++; console.error(`FAIL ${p}: ran no control that must fail\n${out}`); }
  else console.log(`ok   ${p} (${controls} control${controls > 1 ? 's' : ''} caught)`);
}
process.exit(failed ? 1 : 0);
