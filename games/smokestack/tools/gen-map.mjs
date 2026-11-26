// Smokestack's maps: a fictional river valley, generated from a seed and written as C data
// (games/smokestack/src/map_data.h). Two maps: a small one for two players and a large one for three or four.
//
//   node games/smokestack/tools/gen-map.mjs            write src/map_data.h
//   node games/smokestack/tools/gen-map.mjs --check    regenerate in memory and fail if the file differs, or a map
//                                                 breaks a rule below
//   node games/smokestack/tools/gen-map.mjs --control  the same check on a map with a city walled in by lakes: must fail
//
// Rules every map keeps: hexes are pointy, laid out in odd-r rows; every settlement stands on open ground, at
// least three steps from every other city and two from any town; every settlement has two or more open
// neighbours; every settlement can reach every other over open ground; every goods colour has a city.
import { readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '../src/map_data.h');

export const T = { PLAIN: 0, FOREST: 1, RIVER: 2, HILLS: 3, LAKE: 4, PEAK: 5 };
const COLORS = 5;
const DIRS = [[1, 0], [1, -1], [0, -1], [-1, 0], [-1, 1], [0, 1]]; // cw_hex_direction order

function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const pick = (r, a) => a[Math.floor(r() * a.length)];

const SYL_A = ['Ash', 'Bram', 'Cor', 'Dun', 'Ell', 'Fen', 'Gar', 'Hol', 'Ivy', 'Kel', 'Lark', 'Mar', 'Nor', 'Oak', 'Pell', 'Quar', 'Rook', 'Sal', 'Tam', 'Wick'];
const SYL_B = ['ford', 'by', 'wick', 'stead', 'mere', 'ton', 'holm', 'bury', 'dale', 'well', 'gate', 'moor'];

function build(spec) {
  const r = rng(spec.seed);
  const hex = [];
  const at = new Map();
  for (let row = 0; row < spec.rows; row++)
    for (let col = 0; col < spec.cols; col++) {
      const q = col - ((row - (row & 1)) >> 1);
      const h = { q, r: row, col, row, t: T.PLAIN, rin: -1, rout: -1, set: -1 };
      at.set(`${q},${row}`, hex.length);
      hex.push(h);
    }
  const nb = (i, d) => { const h = hex[i]; const k = at.get(`${h.q + DIRS[d][0]},${h.r + DIRS[d][1]}`); return k === undefined ? -1 : k; };
  const dist = (a, b) => { const dq = hex[a].q - hex[b].q, dr = hex[a].r - hex[b].r; return (Math.abs(dq) + Math.abs(dr) + Math.abs(dq + dr)) / 2; };
  // the river: in at the top, wandering down to the bottom edge
  let cur = at.get(`${Math.floor(spec.cols * (0.35 + 0.3 * r())) - 0},0`);
  {
    const h = hex[cur];
    cur = at.get(`${h.q},0`) ?? cur;
  }
  hex[cur].t = T.RIVER;
  hex[cur].rin = 2; // from beyond the top edge (north-west side)
  while (true) {
    const choices = [4, 5].map((d) => [d, nb(cur, d)]).filter(([, k]) => k >= 0);
    if (!choices.length) { hex[cur].rout = 5; break; }
    const keep = choices.filter(([, k]) => { const c = hex[k].col; return c > 1 && c < spec.cols - 2; });
    const [d, k] = pick(r, keep.length ? keep : choices);
    hex[cur].rout = d;
    hex[k].t = T.RIVER;
    hex[k].rin = (d + 3) % 6;
    cur = k;
  }
  const open = (i) => hex[i].t === T.PLAIN;
  const blob = (kind, count, size) => {
    for (let c = 0; c < count; c++) {
      let s = Math.floor(r() * hex.length);
      for (let tries = 0; tries < 40 && !open(s); tries++) s = Math.floor(r() * hex.length);
      if (!open(s)) continue;
      hex[s].t = kind;
      let edge = [s];
      for (let k = 1; k < size; k++) {
        const from = pick(r, edge), d = Math.floor(r() * 6), n = nb(from, d);
        if (n >= 0 && open(n)) { hex[n].t = kind; edge.push(n); }
      }
    }
  };
  blob(T.HILLS, spec.hills[0], spec.hills[1]);
  blob(T.FOREST, spec.forest[0], spec.forest[1]);
  blob(T.LAKE, spec.lakes, 1);
  blob(T.PEAK, spec.peaks[0], spec.peaks[1]);
  // settlements: cities first, then towns
  const sets = [];
  const place = (kind, n, gapCity, gapTown) => {
    for (let k = 0; k < n; k++) {
      let best = -1;
      for (let tries = 0; tries < 400 && best < 0; tries++) {
        const i = Math.floor(r() * hex.length);
        if (hex[i].t !== T.PLAIN && hex[i].t !== T.FOREST) continue;
        if (hex[i].row === 0 || hex[i].row === spec.rows - 1) continue;
        if (sets.some((s) => dist(s.hex, i) < (s.kind === 0 && kind === 0 ? gapCity : gapTown))) continue;
        best = i;
      }
      if (best < 0) return false;
      hex[best].t = T.PLAIN;
      hex[best].set = sets.length;
      sets.push({ hex: best, kind, color: 0, name: '' });
    }
    return true;
  };
  if (!place(0, spec.cities, 3, 2)) return null;
  if (!place(1, spec.towns, 2, 2)) return null;
  // colours: every colour once, the rest at random (two neighbouring cities never share one)
  const cities = sets.filter((s) => s.kind === 0);
  const order = [...cities.keys()].sort(() => r() - 0.5);
  order.forEach((ci, k) => {
    let c = k < COLORS ? k : Math.floor(r() * COLORS);
    for (let t = 0; t < COLORS && cities.some((o) => o !== cities[ci] && o.color === c && o.colored && dist(o.hex, cities[ci].hex) <= 4); t++) c = (c + 1) % COLORS;
    cities[ci].color = c;
    cities[ci].colored = true;
  });
  const used = new Set();
  for (const s of sets) {
    let name;
    do name = pick(r, SYL_A) + pick(r, SYL_B); while (used.has(name));
    used.add(name);
    s.name = name;
    delete s.colored;
  }
  return { ...spec, hex, sets, nb: hex.map((_, i) => DIRS.map((_, d) => nb(i, d))) };
}

export function check(m) {
  const errs = [];
  const passable = (i) => m.hex[i].t !== T.LAKE && m.hex[i].t !== T.PEAK;
  for (const s of m.sets) {
    const free = m.nb[s.hex].filter((k) => k >= 0 && passable(k) && m.hex[k].set < 0).length;
    if (free < 2) errs.push(`${s.name} has ${free} open neighbours`);
  }
  // every settlement reaches every other over open ground
  const seen = new Set([m.sets[0].hex]);
  const todo = [m.sets[0].hex];
  while (todo.length) { const i = todo.pop(); for (const k of m.nb[i]) if (k >= 0 && passable(k) && !seen.has(k)) { seen.add(k); todo.push(k); } }
  for (const s of m.sets) if (!seen.has(s.hex)) errs.push(`${s.name} cut off`);
  for (let c = 0; c < COLORS; c++) if (!m.sets.some((s) => s.kind === 0 && s.color === c)) errs.push(`no city of colour ${c}`);
  return errs;
}

const SPECS = [
  { name: 'Lower Vale', seed: 101, cols: 9, rows: 7, hills: [3, 3], forest: [3, 3], lakes: 2, peaks: [0, 0], cities: 6, towns: 4 },
  { name: 'Kessel Valley', seed: 202, cols: 12, rows: 8, hills: [4, 4], forest: [4, 3], lakes: 2, peaks: [2, 2], cities: 8, towns: 6 },
];

export function generate() {
  return SPECS.map((spec) => {
    for (let s = spec.seed; s < spec.seed + 500; s++) {
      const m = build({ ...spec, seed: s });
      if (m && check(m).length === 0) return m;
    }
    throw new Error(`no valid map for ${spec.name}`);
  });
}

function emit(maps) {
  const L = [];
  L.push('/* Smokestack maps: GENERATED by games/smokestack/tools/gen-map.mjs (do not edit; run the script). */');
  L.push('#ifndef RL_MAP_DATA_H\n#define RL_MAP_DATA_H\n');
  L.push('#include "rules.h"\n');
  L.push('static const rl_map_def RL_MAP_DEFS[RL_MAPS] = {');
  for (const m of maps) {
    const arr = (f) => `{ ${m.hex.map(f).join(', ')} }`;
    L.push('  {');
    L.push(`    .name = "${m.name}", .nhex = ${m.hex.length}, .cols = ${m.cols}, .rows = ${m.rows}, .nset = ${m.sets.length},`);
    L.push(`    .q = ${arr((h) => h.q)},`);
    L.push(`    .r = ${arr((h) => h.r)},`);
    L.push(`    .terrain = ${arr((h) => h.t)},`);
    L.push(`    .river_in = ${arr((h) => h.rin)},`);
    L.push(`    .river_out = ${arr((h) => h.rout)},`);
    L.push(`    .hex_set = ${arr((h) => h.set)},`);
    L.push(`    .nb = { ${m.nb.map((n) => `{ ${n.join(', ')} }`).join(', ')} },`);
    L.push(`    .set_hex = { ${m.sets.map((s) => s.hex).join(', ')} },`);
    L.push(`    .set_kind = { ${m.sets.map((s) => s.kind).join(', ')} },`);
    L.push(`    .set_color = { ${m.sets.map((s) => s.color).join(', ')} },`);
    L.push(`    .set_name = { ${m.sets.map((s) => `"${s.name}"`).join(', ')} },`);
    L.push('  },');
  }
  L.push('};\n\n#endif');
  return L.join('\n') + '\n';
}

const arg = process.argv[2];
if (arg === '--control') {
  const maps = generate();
  const m = maps[0];
  const s = m.sets[0];
  for (const k of m.nb[s.hex]) if (k >= 0 && m.hex[k].set < 0) m.hex[k].t = T.LAKE;
  const errs = check(m);
  if (errs.length === 0) { console.error('control: a walled-in city passed the check'); process.exit(1); }
  console.log(`control: caught (${errs[0]})`);
  process.exit(0);
}
const text = emit(generate());
if (arg === '--check') {
  const have = readFileSync(OUT, 'utf8');
  if (have !== text) { console.error('src/map_data.h differs from the generator: run gen-map.mjs'); process.exit(1); }
  console.log('maps: the file matches the generator and every map keeps the rules');
} else {
  writeFileSync(OUT, text);
  console.log(`wrote ${OUT}`);
}
