#!/usr/bin/env node
// Kilowatt Country data: the map of a fictional country, the power plant set and the game's tables, all made here
// (no inputs). Writes src/kc_data.h (the rules and the view read it) and data/map.json (tools/make-textures.mjs
// paints the night map from it).
//
//   node games/kilowattcountry/tools/gen-data.mjs            write both files
//   node games/kilowattcountry/tools/gen-data.mjs --check    check the map and the plant balance, write nothing
//   node games/kilowattcountry/tools/gen-data.mjs --control  run the checks on a broken map and a broken plant set:
//                                                             both must be caught (exit 0 when they are)
//
// The map: five regions of six cities. City spots come from a seeded scatter inside each region's cell of the
// country; the links are the Gabriel graph of the cities (no two links cross), each costing more the longer it is
// and a little more where it crosses a region border. With fewer players the game leaves regions out, keeping the
// ones in play joined, so every subset of regions the game may pick must still be one connected network.
//
// The plants: numbers 3 to 38. A plant's number is its lowest bid; what it powers grows with the number and the
// fuel it burns per city falls, so later plants are better buys. The check fits a line to each plant's worth
// (cities powered, less what its fuel costs at a mid-market price) against its number and fails a plant that sits
// far off the line or a band of numbers that is not better than the band below it.
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
const args = new Set(process.argv.slice(2));

// ── seeded random ────────────────────────────────────────────────────
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
function valueNoise(seed) {
  const r = rng(seed), g = [];
  for (let i = 0; i < 64 * 64; i++) g.push(r());
  const at = (x, y) => g[((y & 63) * 64 + (x & 63))];
  return (x, y) => {
    const x0 = Math.floor(x), y0 = Math.floor(y), fx = x - x0, fy = y - y0;
    const sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
  };
}

// ── the country ──────────────────────────────────────────────────────
// map units: x 0..1.5 (west to east), z 0..1 (north to south); the board is 3:2
export const MAP_W = 1.5;
const REGIONS = [
  { name: 'Highmoor', seed: [0.3, 0.3], cities: ['Ashby', 'Wrenfield', 'Hollin', 'Stagmere', 'Brackwater', 'Corrie'] },
  { name: 'Copperdale', seed: [0.76, 0.25], cities: ['Tinmouth', 'Gallowfell', 'Redbank', 'Orrin', 'Kestlow', 'Mardale'] },
  { name: 'Saltreach', seed: [1.2, 0.3], cities: ['Port Selby', 'Gullhaven', 'Brinewick', 'Saltcote', 'Larkspit', 'Tidemore'] },
  { name: 'Fenmarsh', seed: [0.47, 0.72], cities: ['Reedham', 'Bittern', 'Mossley', 'Eelbrook', 'Sedgeby', 'Withy'] },
  { name: 'Sunward', seed: [1.03, 0.74], cities: ['Ambergate', 'Helio', 'Dunmere', 'Lantry', 'Brightwell', 'Solace'] },
];
const PER_REGION = 6;
const noise = valueNoise(4242);
export function land(x, z) { // > 0 on land
  const dx = (x - 0.75) / 0.7, dz = (z - 0.5) / 0.46;
  const n = noise(x * 5, z * 5) * 0.6 + noise(x * 11 + 7, z * 11 + 3) * 0.3 + noise(x * 23, z * 23) * 0.1;
  return 1 - dx * dx - dz * dz + (n - 0.5) * 0.55;
}
export function regionOf(x, z) {
  let best = 0, bd = 1e9;
  REGIONS.forEach((r, i) => { const d = (x - r.seed[0]) ** 2 + (z - r.seed[1]) ** 2; if (d < bd) bd = d, best = i; });
  return best;
}
function borderMargin(x, z, r) { // how far inside its own cell (distance to own seed vs the nearest other)
  const own = Math.hypot(x - REGIONS[r].seed[0], z - REGIONS[r].seed[1]);
  let other = 1e9;
  REGIONS.forEach((q, i) => { if (i !== r) other = Math.min(other, Math.hypot(x - q.seed[0], z - q.seed[1])); });
  return other - own;
}

function makeMap(seed) {
  const r = rng(seed);
  const cities = [];
  for (let reg = 0; reg < REGIONS.length; reg++) {
    let tries = 0;
    while (cities.filter((c) => c.region === reg).length < PER_REGION) {
      if (++tries > 20000) return null;
      const x = REGIONS[reg].seed[0] + (r() - 0.5) * 0.62, z = REGIONS[reg].seed[1] + (r() - 0.5) * 0.5;
      if (land(x, z) < 0.28 || regionOf(x, z) !== reg || borderMargin(x, z, reg) < 0.05) continue;
      if (cities.some((c) => Math.hypot(c.x - x, c.z - z) < 0.115)) continue;
      cities.push({ x, z, region: reg });
    }
  }
  cities.forEach((c, i) => { c.name = REGIONS[c.region].cities[i % PER_REGION]; });
  // the Gabriel graph: a link when no other city lies inside the circle on the link as diameter
  const links = [];
  for (let a = 0; a < cities.length; a++)
    for (let b = a + 1; b < cities.length; b++) {
      const A = cities[a], B = cities[b], mx = (A.x + B.x) / 2, mz = (A.z + B.z) / 2, rr = ((A.x - B.x) ** 2 + (A.z - B.z) ** 2) / 4;
      if (cities.some((C, c) => c !== a && c !== b && (C.x - mx) ** 2 + (C.z - mz) ** 2 < rr)) continue;
      const d = Math.hypot(A.x - B.x, A.z - B.z);
      if (d > 0.42) continue;
      links.push({ a, b, d, cost: Math.round(2 + d * 38 + (A.region !== B.region ? 3 : 0)) });
    }
  return { cities, links };
}

function regionAdjacency(map) {
  const adj = REGIONS.map(() => 0);
  for (const l of map.links) {
    const ra = map.cities[l.a].region, rb = map.cities[l.b].region;
    if (ra !== rb) adj[ra] |= 1 << rb, adj[rb] |= 1 << ra;
  }
  return adj;
}
// every set of k regions that is joined (the game picks among these)
export function regionSets(adj, k) {
  const out = [];
  for (let m = 1; m < 1 << REGIONS.length; m++) {
    let bits = 0;
    for (let i = 0; i < REGIONS.length; i++) bits += (m >> i) & 1;
    if (bits !== k) continue;
    const first = Math.log2(m & -m);
    let seen = 1 << first, grow = true;
    while (grow) {
      grow = false;
      for (let i = 0; i < REGIONS.length; i++)
        if ((seen >> i) & 1) { const nx = seen | (adj[i] & m); if (nx !== seen) seen = nx, grow = true; }
    }
    if (seen === m) out.push(m);
  }
  return out;
}
function connected(map, mask) {
  const idx = map.cities.map((c, i) => ((mask >> c.region) & 1 ? i : -1)).filter((i) => i >= 0);
  if (!idx.length) return false;
  const seen = new Set([idx[0]]), stack = [idx[0]];
  while (stack.length) {
    const v = stack.pop();
    for (const l of map.links) {
      const w = l.a === v ? l.b : l.b === v ? l.a : -1;
      if (w < 0 || seen.has(w) || !((mask >> map.cities[w].region) & 1)) continue;
      seen.add(w), stack.push(w);
    }
  }
  return idx.every((i) => seen.has(i));
}
function checkMap(map) {
  const errs = [];
  const adj = regionAdjacency(map);
  for (let r = 0; r < REGIONS.length; r++) if (!connected(map, 1 << r)) errs.push(`region ${REGIONS[r].name} is not one network`);
  for (const k of [3, 4, 5]) {
    const sets = regionSets(adj, k);
    if (!sets.length) errs.push(`no joined set of ${k} regions`);
    for (const m of sets) if (!connected(map, m)) errs.push(`regions ${m.toString(2)} are not one network`);
  }
  const deg = map.cities.map(() => 0);
  map.links.forEach((l) => { deg[l.a]++, deg[l.b]++; });
  deg.forEach((d, i) => { if (d < 2) errs.push(`${map.cities[i].name} has ${d} link(s)`); });
  for (const l of map.links) if (l.cost < 3 || l.cost > 24) errs.push(`link ${l.a}-${l.b} costs ${l.cost}`);
  return errs;
}

// ── the plants ───────────────────────────────────────────────────────
export const FUELS = ['coal', 'oil', 'garbage', 'uranium'];
const KIND = { coal: 0, oil: 1, garbage: 2, uranium: 3, hybrid: 4, green: 5 };
const NUMBERS = [];
for (let n = 3; n <= 30; n++) NUMBERS.push(n);
NUMBERS.push(32, 34, 36, 38, 40, 42, 44, 46);
const PATTERN = ['coal', 'oil', 'coal', 'hybrid', 'garbage', 'coal', 'oil', 'green', 'uranium', 'oil', 'coal', 'garbage', 'hybrid',
  'green', 'coal', 'uranium', 'oil', 'garbage', 'coal', 'green', 'oil', 'uranium', 'garbage', 'coal', 'hybrid', 'oil', 'green',
  'uranium', 'garbage', 'coal', 'green', 'uranium', 'oil', 'hybrid', 'garbage', 'coal'];
const FUELF = { coal: 1.0, oil: 0.85, garbage: 0.95, hybrid: 1.05, uranium: 0.45, green: 0 };
const MID_PRICE = { coal: 3, oil: 4, garbage: 4.5, uranium: 7, hybrid: 3.2, green: 0 };
function makePlants() {
  return NUMBERS.map((n, i) => {
    const kind = PATTERN[i];
    let cities = Math.round(0.6 + n / 7.5);
    let need = 0;
    if (kind === 'green') cities = Math.max(1, cities - 1);
    else need = Math.max(1, Math.min(kind === 'uranium' ? 2 : 3, Math.round(cities * (2.05 - n / 32) * FUELF[kind])));
    return { number: n, kind, need, cities };
  });
}
const worth = (p) => 11 * p.cities - p.need * MID_PRICE[p.kind];
function checkPlants(plants) {
  const errs = [];
  // least squares worth ~ a + b n
  const xs = plants.map((p) => p.number), ys = plants.map(worth), n = xs.length;
  const mx = xs.reduce((a, b) => a + b) / n, my = ys.reduce((a, b) => a + b) / n;
  let sxy = 0, sxx = 0;
  for (let i = 0; i < n; i++) sxy += (xs[i] - mx) * (ys[i] - my), sxx += (xs[i] - mx) ** 2;
  const b = sxy / sxx, a = my - b * mx;
  if (b <= 0) errs.push(`worth does not rise with the number (slope ${b.toFixed(2)})`);
  plants.forEach((p, i) => { const off = ys[i] - (a + b * p.number); if (Math.abs(off) > 16) errs.push(`plant ${p.number} is ${off.toFixed(1)} off the worth line`); });
  const bands = [[3, 10], [11, 18], [19, 26], [27, 40]];
  let last = -1e9;
  for (const [lo, hi] of bands) {
    const ws = plants.filter((p) => p.number >= lo && p.number <= hi).map(worth);
    const avg = ws.reduce((s, v) => s + v, 0) / ws.length;
    if (avg <= last) errs.push(`plants ${lo}-${hi} are not better than the band below (${avg.toFixed(1)} <= ${last.toFixed(1)})`);
    last = avg;
  }
  for (const k of Object.keys(KIND)) if (!plants.some((p) => p.kind === k)) errs.push(`no ${k} plant`);
  const caps = plants.map((p) => p.cities).sort((x, y) => y - x);
  if (caps[0] + caps[1] + caps[2] < 14) errs.push(`the best three plants power only ${caps[0] + caps[1] + caps[2]} cities`);
  if (plants.slice(0, 8).some((p) => p.kind === 'uranium')) errs.push('a uranium plant in the opening market');
  return { errs, fit: { a, b } };
}

// ── the tables ───────────────────────────────────────────────────────
const INCOME = [];
for (let n = 0; n <= 20; n++) INCOME.push(Math.max(INCOME[n - 1] ?? 0, Math.round(8 + 14 * n - 0.42 * n * n)));
const FUEL_SLOTS = [24, 24, 24, 12];
const FUEL_PRICE = [0, 1, 2].map(() => Array.from({ length: 24 }, (_, j) => 1 + Math.floor(j / 3)));
FUEL_PRICE.push([2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 15, 18]);
const FUEL_START = [21, 15, 6, 2];
// cubes back into the market after each round: [players 2..4][step 1..3][coal, oil, garbage, uranium]
const SUPPLY = [
  [[3, 2, 1, 1], [4, 2, 2, 1], [3, 3, 2, 1]],
  [[4, 3, 2, 1], [5, 3, 3, 1], [4, 4, 3, 2]],
  [[5, 3, 2, 1], [6, 4, 3, 2], [5, 5, 4, 2]],
];
const HOUSE_COST = [10, 15, 20];
const END_CITIES = [0, 0, 12, 11, 10];
const STEP2_CITIES = [0, 0, 6, 5, 5];
const REGIONS_USED = [0, 0, 3, 4, 5];
const PLANTS_OUT = [0, 0, 4, 3, 2];
const START_MONEY = 50;
const MAX_ROUNDS = 20;

// ── build, check, write ──────────────────────────────────────────────
function buildMap() {
  for (let seed = 1; seed < 500; seed++) {
    const m = makeMap(seed);
    if (m && !checkMap(m).length) return { map: m, seed };
  }
  throw new Error('no map seed passes the checks');
}

function main() {
if (args.has('--control')) {
  const { map } = buildMap();
  const broken = { cities: map.cities, links: map.links.filter((l) => map.cities[l.a].region === map.cities[l.b].region || (map.cities[l.a].region !== 0 && map.cities[l.b].region !== 0)) };
  const e1 = checkMap(broken);
  const plants = makePlants();
  const bad = plants.map((p) => ({ ...p }));
  Object.assign(bad[1], { kind: 'uranium', need: 1, cities: 6 }); // a 4 that powers six on one uranium
  const e2 = checkPlants(bad).errs;
  console.log(`CONTROL map with Highmoor cut off: ${e1.length ? 'caught' : 'NOT caught'} (${e1[0] ?? ''})`);
  console.log(`CONTROL plant 4 made the strongest: ${e2.length ? 'caught' : 'NOT caught'} (${e2[0] ?? ''})`);
  process.exit(e1.length && e2.length ? 0 : 1);
}

const { map, seed } = buildMap();
const plants = makePlants();
const pc = checkPlants(plants);
const adj = regionAdjacency(map);
if (args.has('--check')) {
  const errs = [...checkMap(map), ...pc.errs];
  console.log(`map seed ${seed}: ${map.cities.length} cities, ${map.links.length} links; ${plants.length} plants, worth = ${pc.fit.a.toFixed(1)} + ${pc.fit.b.toFixed(2)} x number`);
  for (const p of plants) console.log(`  ${String(p.number).padStart(2)} ${p.kind.padEnd(8)} burns ${p.need} powers ${p.cities}  worth ${worth(p).toFixed(1)}`);
  if (errs.length) { console.log(errs.join('\n')); process.exit(1); }
  process.exit(0);
}
if (pc.errs.length) throw new Error(pc.errs.join('\n'));

const q = (s) => JSON.stringify(s);
const f = (v) => `${v.toFixed(4)}f`;
let h = `/* Kilowatt Country data: generated by tools/gen-data.mjs (do not edit; edit the script and run it again). */
#ifndef KC_DATA_H
#define KC_DATA_H

#include <stdint.h>

#define KC_NREGION ${REGIONS.length}
#define KC_NCITY ${map.cities.length}
#define KC_NLINK ${map.links.length}
#define KC_NPLANT ${plants.length}
#define KC_MAP_ASPECT ${f(MAP_W)} /* the map is this wide for 1 deep */
#define KC_START_MONEY ${START_MONEY}
#define KC_MAX_ROUNDS ${MAX_ROUNDS}
#define KC_INCOME_MAX ${INCOME.length - 1}

enum { KC_COAL, KC_OIL, KC_GARBAGE, KC_URANIUM, KC_NFUEL };
enum { KC_K_COAL, KC_K_OIL, KC_K_GARBAGE, KC_K_URANIUM, KC_K_HYBRID, KC_K_GREEN };

typedef struct kc_city_def { const char *name; int8_t region; float x, z; } kc_city_def; /* x, z: 0..1 across the map */
typedef struct kc_link_def { int8_t a, b, cost; } kc_link_def;
typedef struct kc_plant_def { int8_t number, kind, need, cities; } kc_plant_def;

static const char *const kc_region_names[KC_NREGION] = { ${REGIONS.map((r) => q(r.name)).join(', ')} };
static const uint8_t kc_region_adj[KC_NREGION] = { ${adj.join(', ')} };
static const kc_city_def kc_cities[KC_NCITY] = {
${map.cities.map((c) => `  { ${q(c.name)}, ${c.region}, ${f(c.x / MAP_W)}, ${f(c.z)} },`).join('\n')}
};
static const kc_link_def kc_links[KC_NLINK] = {
${map.links.map((l) => `  { ${l.a}, ${l.b}, ${l.cost} },`).join('\n')}
};
static const kc_plant_def kc_plants[KC_NPLANT] = { /* sorted by number */
${plants.map((p) => `  { ${p.number}, KC_K_${p.kind.toUpperCase()}, ${p.need}, ${p.cities} },`).join('\n')}
};
static const int16_t kc_income[KC_INCOME_MAX + 1] = { ${INCOME.join(', ')} };
static const int8_t kc_fuel_slots[KC_NFUEL] = { ${FUEL_SLOTS.join(', ')} };
static const int8_t kc_fuel_price_tab[KC_NFUEL][24] = {
${FUEL_PRICE.map((r) => `  { ${r.join(', ')} },`).join('\n')}
};
static const int8_t kc_fuel_start[KC_NFUEL] = { ${FUEL_START.join(', ')} };
static const int8_t kc_supply[3][3][KC_NFUEL] = {
${SUPPLY.map((p) => `  { ${p.map((s) => `{ ${s.join(', ')} }`).join(', ')} },`).join('\n')}
};
static const int8_t kc_house_cost[3] = { ${HOUSE_COST.join(', ')} };
static const int8_t kc_end_tab[5] = { ${END_CITIES.join(', ')} };
static const int8_t kc_step2_tab[5] = { ${STEP2_CITIES.join(', ')} };
static const int8_t kc_regions_used[5] = { ${REGIONS_USED.join(', ')} };
static const int8_t kc_plants_out[5] = { ${PLANTS_OUT.join(', ')} };
`;
for (const k of [3, 4, 5]) {
  const sets = regionSets(adj, k);
  h += `static const uint8_t kc_region_sets_${k}[${sets.length}] = { ${sets.join(', ')} };\n`;
}
h += '\n#endif\n';
writeFileSync(join(GAME, 'src', 'kc_data.h'), h);
mkdirSync(join(GAME, 'data'), { recursive: true });
writeFileSync(join(GAME, 'data', 'map.json'), JSON.stringify({
  aspect: MAP_W, regions: REGIONS.map((r) => ({ name: r.name, seed: r.seed })), adjacency: adj,
  cities: map.cities.map((c) => ({ name: c.name, region: c.region, x: +c.x.toFixed(4), z: +c.z.toFixed(4) })),
  links: map.links.map((l) => ({ a: l.a, b: l.b, cost: l.cost })),
}, null, 1));
console.log(`src/kc_data.h and data/map.json: map seed ${seed}, ${map.cities.length} cities, ${map.links.length} links, ${plants.length} plants`);
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
