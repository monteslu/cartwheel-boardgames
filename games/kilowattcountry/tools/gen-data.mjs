#!/usr/bin/env node
// Kilowatt Country data: the maps (real countries, tools/gen-maps.mjs), the power plant set and the game's tables.
// Writes src/kc_data.h (the rules and the view read it) and data/maps.json (tools/make-textures.mjs paints each
// night map from it).
//
//   node games/kilowattcountry/tools/gen-data.mjs            write both files
//   node games/kilowattcountry/tools/gen-data.mjs --check    the files match what the data and the rules make, every map
//                                                             holds its rules and the plants their balance; writes nothing
//   node games/kilowattcountry/tools/gen-data.mjs --control  the checks on a broken map and a broken plant set: both
//                                                             must be caught (exit 0 when they are)
//
// The maps: five regions of six cities each (tools/gen-maps.mjs says how they are made). With fewer players the game
// leaves regions out, keeping the ones in play joined, so every joined set of regions the game may deal must be one
// network.
//
// The plants: numbers 3 to 46. A plant's number is its lowest bid; what it powers grows with the number and the
// fuel it burns per city falls, so later plants are better buys. The check fits a line to each plant's worth
// (cities powered, less what its fuel costs at a mid-market price) against its number and fails a plant that sits
// far off the line or a band of numbers that is not better than the band below it.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { MAP_W, MAX_LINK, NCITY, NREGION, buildMaps, checkMap, regionAdjacency, regionSets } from './gen-maps.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
const args = new Set(process.argv.slice(2));

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
const q = (s) => JSON.stringify(s);
const f = (v) => `${v.toFixed(4)}f`;
function header(maps, plants) {
  const nlink = Math.max(...maps.map((m) => m.links.length));
  let h = `/* Kilowatt Country data: generated by tools/gen-data.mjs (do not edit; edit the script and run it again). The maps'
 * cities and places come from GeoNames (CC BY 4.0, geonames.org); their land from Natural Earth (public domain). */
#ifndef KC_DATA_H
#define KC_DATA_H

#include <stdint.h>

#define KC_NMAPS ${maps.length}
#define KC_NREGION ${NREGION}
#define KC_NCITY ${NCITY}
#define KC_MAX_LINK ${nlink} /* the most links a map has */
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
/* a map: its cities and links, which regions border which, and the joined sets of 3, 4 and 5 regions the game deals */
typedef struct kc_map_def {
  const char *key, *title;
  int nlink, km_per; /* a link costs 2 plus one per km_per kilometres */
  kc_city_def cities[KC_NCITY];
  kc_link_def links[KC_MAX_LINK];
  uint8_t region_adj[KC_NREGION];
  int8_t nsets[3];
  uint8_t sets[3][10];
} kc_map_def;

static const kc_map_def kc_maps[KC_NMAPS] = {
`;
  for (const m of maps) {
    const adj = regionAdjacency(m.cities, m.links);
    const sets = [3, 4, 5].map((k) => regionSets(adj, k));
    h += `  { ${q(m.key)}, ${q(m.title)}, ${m.links.length}, ${m.kmPer},\n    {\n`;
    h += m.cities.map((c) => `      { ${q(c.name)}, ${c.region}, ${f(c.x / MAP_W)}, ${f(c.z)} },`).join('\n') + '\n    },\n';
    h += `    { ${m.links.map((l) => `{ ${l.a}, ${l.b}, ${l.cost} }`).join(', ')} },\n`;
    h += `    { ${adj.join(', ')} }, { ${sets.map((x) => x.length).join(', ')} },\n`;
    h += `    { ${sets.map((x) => `{ ${x.join(', ')} }`).join(', ')} } },\n`;
  }
  h += `};
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

#endif
`;
  return h;
}

function main() {
  const plants = makePlants();
  if (args.has('--control')) {
    const maps = buildMaps();
    const m = maps[0];
    /* a map with its first region cut off: every link out of region 0 gone */
    const broken = { ...m, links: m.links.filter((l) => (m.cities[l.a].region === 0) === (m.cities[l.b].region === 0)) };
    const e1 = checkMap(broken);
    const bad = plants.map((p) => ({ ...p }));
    Object.assign(bad[1], { kind: 'uranium', need: 1, cities: 6 }); // a 4 that powers six on one uranium
    const e2 = checkPlants(bad).errs;
    console.log(`CONTROL ${m.title} with its first region cut off: ${e1.length ? 'caught' : 'NOT caught'} (${e1[0] ?? ''})`);
    console.log(`CONTROL plant 4 made the strongest: ${e2.length ? 'caught' : 'NOT caught'} (${e2[0] ?? ''})`);
    process.exit(e1.length && e2.length ? 0 : 1);
  }
  const maps = buildMaps();
  const pc = checkPlants(plants);
  const errs = [...maps.flatMap(checkMap), ...pc.errs];
  if (errs.length) { console.error(errs.join('\n')); process.exit(1); }
  const h = header(maps, plants);
  const j = JSON.stringify({ aspect: MAP_W, maps }, null, 1) + '\n';
  const HDR = join(GAME, 'src', 'kc_data.h'), JSONF = join(GAME, 'data', 'maps.json');
  const summary = maps.map((m) => `${m.key} ${m.cities.length} cities ${m.links.length} links`).join(', ');
  if (args.has('--check')) {
    let same = false;
    try { same = readFileSync(HDR, 'utf8') === h && readFileSync(JSONF, 'utf8') === j; } catch { same = false; }
    if (!same) { console.error('src/kc_data.h or data/maps.json differ from what the data makes: run tools/gen-data.mjs'); process.exit(1); }
    console.log(`maps ok: ${summary}; ${plants.length} plants, worth = ${pc.fit.a.toFixed(1)} + ${pc.fit.b.toFixed(2)} x number`);
    process.exit(0);
  }
  writeFileSync(HDR, h);
  mkdirSync(dirname(JSONF), { recursive: true });
  writeFileSync(JSONF, j);
  console.log(`src/kc_data.h and data/maps.json: ${summary}; ${plants.length} plants`);
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
