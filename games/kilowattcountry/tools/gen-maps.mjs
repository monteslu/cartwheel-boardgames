#!/usr/bin/env node
// Kilowatt Country's maps: real countries with real cities at their real places, picked, linked and split into
// regions by our own rules from public data (GeoNames for the cities, Natural Earth for the land, the borders and
// the lakes), never from any other game's board. tools/gen-data.mjs writes them into src/kc_data.h and
// data/maps.json; tools/make-textures.mjs paints each one's night map.
//
//   node games/kilowattcountry/tools/gen-maps.mjs --extract   rewrite data/places.tsv from GeoNames cities15000
//   node games/kilowattcountry/tools/gen-maps.mjs             print each map's cities, links, regions and costs
//
// The rules, per map:
//   1. The map's box (longitude scaled by the cosine of its middle latitude) is scaled to fill the board's 3:2 face,
//      centred; north is up.
//   2. Cities: thirty of the map's own countries' places, the map's `first` ones and then the most populous, each kept
//      only when it stands at least `space` (a share of the board's depth) from the cities already kept and inside the
//      board's margin; the widest spacing that still finds thirty is used, so the cities spread over the whole country.
//      A name is the place's plain ASCII name, with a trailing part such as " am Main" cut; names longer than 12 letters
//      are left out.
//   3. Links: the Delaunay triangulation of the cities cut down to its Gabriel edges (no city inside the circle over a
//      link), so no two links cross; a link much longer than the map's median or one that runs mostly over sea or a
//      lake goes. The shortest triangulation edges come back where a city would have fewer than two links or the map
//      would fall apart; no city keeps more than five.
//   4. Five regions of six cities: k-means on the board positions with six seats a region, from several starts; the
//      split kept is the tightest one in which every region is one network on its own links and every joined set of
//      three or four regions (what 2 and 3 players get) is one network too. Ordered west to east by their centres.
//   5. A link's cost comes from the real distance between its cities: 2 plus one per `kmPer` kilometres, rounded, with
//      kmPer set per map (a rounded number) so the map's median link costs 9, as on the board this game was tuned on.
import { writeFileSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { delaunay, km, landRings, loadData, loadGeoNames, onLand, segCross } from '../../common/geo/geo.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
export const MAP_W = 1.5; // the board's face: x 0..1.5 (west to east), z 0..1 (north to south)
export const NCITY = 30, NREGION = 5, PER_REGION = 6, MAX_LINK = 80, MAX_DEG = 5;
const MARGIN_X = 0.05, MARGIN_Z = 0.06;

// The maps, the default first. cc: the countries whose places are cities on it; lon/lat: the box that fills the
// board; space: the closest two cities may stand (map units); states: draw this country's state lines; first: places
// taken before the others (a capital the data counts smaller than a neighbour that would crowd it out); skip: places
// left out because, packed among their neighbours, their names find no clear spot at the table camera (the game's
// label check, tests/kilowattcountry-play.mjs, finds them).
export const MAPS = [
  { key: 'usa', title: 'United States', cc: ['US'], lon: [-124.8, -67], lat: [24.8, 49.4], space: 0.097, states: 'USA', skip: ['Montgomery/US'] },
  { key: 'useast', title: 'Eastern States', cc: ['US'], lon: [-95.5, -69.5], lat: [28.8, 45.6], space: 0.1, states: 'USA' },
  { key: 'lakes', title: 'Great Lakes', cc: ['US', 'CA'], lon: [-93.5, -71], lat: [38.6, 47.4], space: 0.1 },
  { key: 'france', title: 'France', cc: ['FR'], lon: [-5.2, 8.4], lat: [42.3, 51.1], space: 0.1, skip: ['Aurillac/FR'] },
  { key: 'iberia', title: 'Iberia', cc: ['ES', 'PT'], lon: [-9.6, 3.4], lat: [35.9, 43.9], space: 0.1 },
  { key: 'balkans', title: 'The Balkans', cc: ['SI', 'HR', 'BA', 'RS', 'ME', 'XK', 'MK', 'AL', 'BG', 'RO', 'GR', 'MD'], lon: [13.4, 29.8], lat: [37.8, 47.6], space: 0.1 },
  { key: 'central', title: 'Central Europe', cc: ['DE', 'PL', 'CZ', 'AT', 'NL', 'BE', 'CH', 'SK', 'HU'], lon: [3.0, 24.5], lat: [45.6, 54.9], space: 0.1, first: ['Amsterdam/NL'] },
];

// ── places ─────────────────────────────────────────────────────────
const PLACES = join(GAME, 'data', 'places.tsv');
const CUT = [/ am .*$/, / an der .*$/, / im .*$/, / in .*$/, / \(.*\)$/, / upon .*$/, / on .*$/, / sur .*$/, / del .*$/];
// the English names of a few places whose own name is written differently
const SKIP = new Set(['Campina']); /* a district's name in the data, not a town's */
const ENGLISH = { 'New York City': 'New York', 'Köln': 'Cologne', 'Den Haag': 'The Hague', "'s-Gravenhage": 'The Hague', 'Bruxelles': 'Brussels', 'Kingston upon Hull': 'Hull' };
const LETTERS = { 'Ł': 'L', 'ł': 'l', 'ß': 'ss', 'Ø': 'O', 'ø': 'o', 'Æ': 'Ae', 'æ': 'ae', 'Đ': 'D', 'đ': 'd', 'ı': 'i' };
/* a place's label: its English name if it has one, else its own name with the accents dropped (Kraków: Krakow) */
export function cleanName(local, ascii) {
  let n = ENGLISH[local] ?? local;
  n = n.normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[ŁłßØøÆæĐđı]/g, (c) => LETTERS[c]);
  if (!/^[A-Za-z .'-]+$/.test(n)) n = ascii;
  for (const r of CUT) n = n.replace(r, '');
  return n;
}
function extract() {
  const keep = new Set(['PPL', 'PPLA', 'PPLA2', 'PPLA3', 'PPLA4', 'PPLC', 'PPLG']);
  const rows = [];
  for (const p of loadGeoNames()) {
    if (!keep.has(p.fcode) || p.pop < 15000) continue;
    const name = cleanName(p.local, p.name);
    if (!/^[A-Za-z .'-]+$/.test(name) || name.length > 12 || SKIP.has(name)) continue;
    if (!MAPS.some((m) => m.cc.includes(p.cc) && p.lon >= m.lon[0] && p.lon <= m.lon[1] && p.lat >= m.lat[0] && p.lat <= m.lat[1])) continue;
    rows.push([name, p.cc, p.lat.toFixed(4), p.lon.toFixed(4), p.pop]);
  }
  rows.sort((a, b) => b[4] - a[4] || a[0].localeCompare(b[0]) || a[1].localeCompare(b[1]));
  writeFileSync(PLACES, '# name\tcountry\tlat\tlon\tpopulation: from GeoNames cities15000 (CC BY 4.0, geonames.org), towns and cities in the maps\' countries\n' +
    rows.map((r) => r.join('\t')).join('\n') + '\n');
  console.log(`wrote ${rows.length} places`);
}
export function loadPlaces() {
  return readFileSync(PLACES, 'utf8').split('\n').filter((l) => l && !l.startsWith('#')).map((l) => {
    const [name, cc, lat, lon, pop] = l.split('\t');
    return { name, cc, lat: +lat, lon: +lon, pop: +pop };
  });
}

// ── the projection: lon/lat to map units and back ─────────────────
export function projection(m) {
  const latm = (m.lat[0] + m.lat[1]) / 2, lonm = (m.lon[0] + m.lon[1]) / 2, k = Math.cos((latm * Math.PI) / 180);
  const w = (m.lon[1] - m.lon[0]) * k, h = m.lat[1] - m.lat[0];
  const scale = Math.min((MAP_W - 2 * MARGIN_X) / w, (1 - 2 * MARGIN_Z) / h);
  const fwd = (lon, lat) => [MAP_W / 2 + (lon - lonm) * k * scale, 0.5 - (lat - latm) * scale];
  const inv = (x, z) => [lonm + (x - MAP_W / 2) / scale / k, latm - (z - 0.5) / scale];
  return { fwd, inv, params: { lonm, latm, k, scale } };
}

// ── one map ────────────────────────────────────────────────────────
function pickCities(m, places, proj, banned) {
  const cand = places.filter((p) => m.cc.includes(p.cc) && p.lon >= m.lon[0] && p.lon <= m.lon[1] && p.lat >= m.lat[0] && p.lat <= m.lat[1] &&
    !banned.has(`${p.name}/${p.cc}`));
  const first = new Set(m.first || []);
  for (const k of first) if (!cand.some((p) => `${p.name}/${p.cc}` === k)) throw new Error(`${m.key}: first place ${k} is not in the box`);
  for (const k of m.skip || []) if (!cand.some((p) => `${p.name}/${p.cc}` === k)) throw new Error(`${m.key}: skipped place ${k} is not in the box`);
  const skip = new Set(m.skip || []);
  for (let i = cand.length - 1; i >= 0; i--) if (skip.has(`${cand[i].name}/${cand[i].cc}`)) cand.splice(i, 1);
  cand.sort((p, q) => first.has(`${q.name}/${q.cc}`) - first.has(`${p.name}/${p.cc}`));
  for (let space = m.space; space >= 0.07 - 1e-9; space -= 0.0025) {
    const out = [];
    for (const p of cand) {
      if (out.length >= NCITY) break;
      const [x, z] = proj.fwd(p.lon, p.lat);
      if (x < MARGIN_X || x > MAP_W - MARGIN_X || z < MARGIN_Z || z > 1 - MARGIN_Z) continue;
      if (out.some((o) => o.name === p.name || Math.hypot(o.x - x, o.z - z) < space)) continue;
      out.push({ name: p.name, cc: p.cc, lon: p.lon, lat: p.lat, pop: p.pop, x, z });
    }
    if (out.length === NCITY) return { cities: out, space };
  }
  throw new Error(`${m.key}: fewer than ${NCITY} cities at spacing ${m.space}`);
}

function linkUp(cities, proj, water, ferries) {
  const n = cities.length, P = cities.map((c) => [c.x, c.z]);
  const dEdges = new Map();
  for (const t of delaunay(P)) for (const [a, b] of [[t[0], t[1]], [t[1], t[2]], [t[2], t[0]]]) dEdges.set(a < b ? `${a}-${b}` : `${b}-${a}`, [Math.min(a, b), Math.max(a, b)]);
  const wet = ([a, b]) => {
    let w = 0;
    for (let i = 1; i < 16; i++) {
      const t = i / 16, [lon, lat] = proj.inv(P[a][0] + (P[b][0] - P[a][0]) * t, P[a][1] + (P[b][1] - P[a][1]) * t);
      if (water(lon, lat)) w++;
    }
    return w / 15;
  };
  const all = [...dEdges.values()].map(([a, b]) => ({ a, b, len: Math.hypot(P[a][0] - P[b][0], P[a][1] - P[b][1]), wet: wet([a, b]) }));
  const gabriel = (e) => {
    const mx = (P[e.a][0] + P[e.b][0]) / 2, mz = (P[e.a][1] + P[e.b][1]) / 2, r = e.len / 2;
    return !P.some((p, k) => k !== e.a && k !== e.b && Math.hypot(p[0] - mx, p[1] - mz) < r - 1e-6);
  };
  const WET = 0.4, dry = (e) => e.wet < WET || (ferries && e.len <= 1.5 * median0);
  const lens0 = all.filter((e) => gabriel(e) && e.wet < WET).map((e) => e.len).sort((x, y) => x - y), median0 = lens0[lens0.length >> 1];
  const lens = all.filter((e) => gabriel(e) && dry(e)).map((e) => e.len).sort((x, y) => x - y);
  const median = lens[Math.floor(lens.length / 2)];
  let keep = all.filter((e) => gabriel(e) && dry(e) && e.len <= 2.1 * median);
  const deg = () => { const d = Array(n).fill(0); for (const e of keep) d[e.a]++, d[e.b]++; return d; };
  const comps = (edges) => {
    const id = Array(n).fill(-1);
    let k = 0;
    for (let s = 0; s < n; s++) {
      if (id[s] >= 0) continue;
      const q = [s];
      id[s] = k;
      while (q.length) { const u = q.pop(); for (const e of edges) { const v = e.a === u ? e.b : e.b === u ? e.a : -1; if (v >= 0 && id[v] < 0) id[v] = k, q.push(v); } }
      k++;
    }
    return { id, k };
  };
  const crosses = (e) => keep.some((o) => o.a !== e.a && o.a !== e.b && o.b !== e.a && o.b !== e.b && segCross(P[e.a], P[e.b], P[o.a], P[o.b]));
  const spare = () => all.filter((e) => !keep.includes(e) && !crosses(e)).sort((x, y) => !dry(x) - !dry(y) || x.len - y.len);
  /* every city two links, but never over open sea (a coastal city at the end of the line keeps one) */
  for (const e of spare()) { const d = deg(); if ((d[e.a] < 2 || d[e.b] < 2) && d[e.a] < MAX_DEG && d[e.b] < MAX_DEG && e.wet < 0.6 && e.len <= 2.5 * median && !crosses(e)) keep.push(e); }
  for (let guard = 0; guard < 60; guard++) {
    const { id, k } = comps(keep);
    if (k <= 1) break;
    const e = spare().filter((x) => id[x.a] !== id[x.b])[0];
    if (!e) break;
    keep.push(e);
  }
  for (let guard = 0; guard < 200; guard++) {
    const d = deg(), over = d.findIndex((v) => v > MAX_DEG);
    if (over < 0) break;
    const mine = keep.filter((e) => e.a === over || e.b === over).sort((x, y) => y.len - x.len);
    let cut = false;
    for (const e of mine) {
      if (d[e.a] <= 2 || d[e.b] <= 2) continue;
      const rest = keep.filter((x) => x !== e);
      if (comps(rest).k === 1) { keep = rest; cut = true; break; }
    }
    if (!cut) break;
  }
  return keep.map((e) => ({ a: e.a, b: e.b, sea: !dry(e), km: km(cities[e.a].lon, cities[e.a].lat, cities[e.b].lon, cities[e.b].lat), wet: e.wet }));
}

/* is the set of cities (a bit mask over regions, or a list of city indices) one network on its own links */
function joined(n, links, inSet) {
  const idx = [];
  for (let i = 0; i < n; i++) if (inSet(i)) idx.push(i);
  if (!idx.length) return false;
  const seen = new Set([idx[0]]), st = [idx[0]];
  while (st.length) {
    const v = st.pop();
    for (const l of links) {
      const w = l.a === v ? l.b : l.b === v ? l.a : -1;
      if (w >= 0 && !seen.has(w) && inSet(w)) seen.add(w), st.push(w);
    }
  }
  return idx.every((i) => seen.has(i));
}
export function regionAdjacency(cities, links) {
  const adj = Array(NREGION).fill(0);
  for (const l of links) {
    const ra = cities[l.a].region, rb = cities[l.b].region;
    if (ra !== rb) adj[ra] |= 1 << rb, adj[rb] |= 1 << ra;
  }
  return adj;
}
/* every set of k regions that is joined (the game deals one of these) */
export function regionSets(adj, k) {
  const out = [];
  for (let m = 1; m < 1 << NREGION; m++) {
    let bits = 0;
    for (let i = 0; i < NREGION; i++) bits += (m >> i) & 1;
    if (bits !== k) continue;
    let seen = m & -m, grow = true;
    while (grow) {
      grow = false;
      for (let i = 0; i < NREGION; i++)
        if ((seen >> i) & 1) { const nx = seen | (adj[i] & m); if (nx !== seen) seen = nx, grow = true; }
    }
    if (seen === m) out.push(m);
  }
  return out;
}
export function regionProblems(cities, links) {
  const errs = [], n = cities.length;
  for (let r = 0; r < NREGION; r++) {
    const c = cities.filter((x) => x.region === r).length;
    if (c !== PER_REGION) errs.push(`region ${r} has ${c} cities`);
    if (!joined(n, links, (i) => cities[i].region === r)) errs.push(`region ${r} is not one network`);
  }
  const adj = regionAdjacency(cities, links);
  for (const k of [3, 4, 5]) {
    const sets = regionSets(adj, k);
    if (!sets.length) errs.push(`no joined set of ${k} regions`);
    for (const m of sets) if (!joined(n, links, (i) => (m >> cities[i].region) & 1)) errs.push(`regions ${m.toString(2)} are not one network`);
  }
  return errs;
}

function splitRegions(cities, links) {
  const n = cities.length;
  const nb = cities.map(() => []);
  for (const l of links) nb[l.a].push(l.b), nb[l.b].push(l.a);
  let best = null;
  // seeds: five cities far apart (farthest-point from each start, then jittered); the regions grow from them along
  // the links, six cities each; then swaps of two cities that join up any region in pieces and tighten the split.
  // The tightest valid split of every trial is kept (seeded: the same every run)
  let x = 0x9e3779b9;
  const rnd = () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
  for (let trial = 0; trial < 400; trial++) {
    const start = trial % n, seeds = [start];
    const jitter = trial < n ? 0 : 0.35; /* after one pass of plain farthest-point seeds, jittered ones */
    while (seeds.length < NREGION) {
      let far = -1, fd = -1;
      cities.forEach((c, i) => { const d = Math.min(...seeds.map((q) => Math.hypot(cities[q].x - c.x, cities[q].z - c.z))) * (1 - jitter * rnd()); if (d > fd) fd = d, far = i; });
      seeds.push(far);
    }
    const reg = Array(n).fill(-1);
    seeds.forEach((c, r) => { reg[c] = r; });
    const cen = (r) => { const m = cities.filter((c, i) => reg[i] === r); return [m.reduce((s, c) => s + c.x, 0) / m.length, m.reduce((s, c) => s + c.z, 0) / m.length]; };
    let stuck = false;
    for (let placed = NREGION; placed < n && !stuck; placed++) {
      /* the smallest region that can still grow takes its nearest free neighbour (so no region is boxed in early) */
      let bi = -1, br = -1, bsize = 99, bd = 1e9;
      for (let r = 0; r < NREGION; r++) {
        const size = reg.filter((x) => x === r).length;
        if (size >= PER_REGION || size > bsize) continue;
        const [cx, cz] = cen(r);
        let wi = -1, wd = 1e9;
        for (let i = 0; i < n; i++) {
          if (reg[i] !== r) continue;
          for (const w of nb[i]) if (reg[w] < 0) { const d = Math.hypot(cities[w].x - cx, cities[w].z - cz) * (1 - jitter * 0.5 * rnd()); if (d < wd) wd = d, wi = w; }
        }
        if (wi < 0) continue;
        if (size < bsize || wd < bd) bsize = size, bd = wd, bi = wi, br = r;
      }
      if (bi < 0) stuck = true;
      else reg[bi] = br;
    }
    if (stuck) { /* boxed in: the rest go to the nearest region with room, and the swaps below join them up */
      for (let i = 0; i < n; i++) {
        if (reg[i] >= 0) continue;
        let br = -1, bd = 1e9;
        for (let r = 0; r < NREGION; r++) {
          if (reg.filter((x) => x === r).length >= PER_REGION) continue;
          const [cx, cz] = cen(r), d = Math.hypot(cities[i].x - cx, cities[i].z - cz);
          if (d < bd) bd = d, br = r;
        }
        reg[i] = br;
      }
    }
    /* swaps of two cities in different regions while they lower the cost: every piece a region falls into beyond the
     * first costs far more than any spread, so the regions join up first and tighten after */
    const pieces = (r) => {
      const idx = [];
      for (let i = 0; i < n; i++) if (reg[i] === r) idx.push(i);
      const seen = new Set();
      let k = 0;
      for (const s0 of idx) {
        if (seen.has(s0)) continue;
        k++;
        const st = [s0];
        seen.add(s0);
        while (st.length) { const v = st.pop(); for (const w of nb[v]) if (reg[w] === r && !seen.has(w)) seen.add(w), st.push(w); }
      }
      return k;
    };
    const cost = () => { let p = 0; for (let r = 0; r < NREGION; r++) p += pieces(r) - 1; return p * 1000 + spread(); };
    function spread() { const c = [0, 1, 2, 3, 4].map(cen); return cities.reduce((s, x, i) => s + Math.hypot(x.x - c[reg[i]][0], x.z - c[reg[i]][1]) ** 2, 0); }
    let now = cost();
    for (let pass = 0; pass < 40; pass++) {
      let improved = false;
      for (let a = 0; a < n; a++)
        for (let b = a + 1; b < n; b++) {
          if (reg[a] === reg[b]) continue;
          const ra = reg[a], rb = reg[b];
          reg[a] = rb, reg[b] = ra;
          const c2 = cost();
          if (c2 < now - 1e-9) { now = c2; improved = true; continue; }
          reg[a] = ra, reg[b] = rb;
        }
      if (!improved) break;
    }
    const c = [0, 1, 2, 3, 4].map(cen);
    const order = [0, 1, 2, 3, 4].sort((p, q) => c[p][0] - c[q][0] || c[p][1] - c[q][1]);
    const rank = new Map(order.map((r, i) => [r, i]));
    const split = cities.map((x, i) => ({ ...x, region: rank.get(reg[i]) }));
    if (regionProblems(split, links).length) continue;
    const sp = now;
    if (!best || sp < best.spread - 1e-12) best = { spread: sp, cities: split };
  }
  if (!best) throw new Error('no split into five joined regions of six');
  return best.cities;
}

const nice = (v) => { const steps = [5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80]; return steps.reduce((b, s) => (Math.abs(s - v) < Math.abs(b - v) ? s : b), steps[0]); };
export const MEDIAN_COST = 9;

export function makeMap(m, places, water) {
  const proj = projection(m);
  /* a city reached only over open water (an island, unless the map has ferries) is left out, and the next one taken */
  const banned = new Set();
  let cities, space, links;
  for (let guard = 0; ; guard++) {
    ({ cities, space } = pickCities(m, places, proj, banned));
    links = linkUp(cities, proj, water, !!m.ferries);
    /* the network over land and narrow water alone: every city outside its largest piece is an island */
    const id = cities.map((c, i) => i), root = (i) => (id[i] === i ? i : (id[i] = root(id[i])));
    for (const l of links) if (l.wet < 0.6 || m.ferries) id[root(l.a)] = root(l.b);
    const size = new Map();
    cities.forEach((c, i) => size.set(root(i), (size.get(root(i)) || 0) + 1));
    const main = [...size.entries()].sort((a, b) => b[1] - a[1])[0][0];
    const wetOnly = cities.filter((c, i) => root(i) !== main);
    if (!wetOnly.length) break;
    if (guard > 20) throw new Error(`${m.key}: islands keep coming: ${wetOnly.map((c) => c.name).join(', ')}`);
    for (const c of wetOnly) banned.add(`${c.name}/${c.cc}`);
  }
  const withRegions = splitRegions(cities, links);
  const kms = links.map((l) => l.km).sort((a, b) => a - b);
  const kmPer = nice(kms[Math.floor(kms.length / 2)] / (MEDIAN_COST - 2));
  for (const l of links) l.cost = Math.max(3, Math.round(2 + l.km / kmPer));
  return { key: m.key, title: m.title, cc: m.cc, lon: m.lon, lat: m.lat, states: m.states || null, proj: proj.params, space: +space.toFixed(4), kmPer,
    cities: withRegions.map((c) => ({ name: c.name, cc: c.cc, region: c.region, x: +c.x.toFixed(4), z: +c.z.toFixed(4), lon: c.lon, lat: c.lat, pop: c.pop })),
    links: links.sort((p, q) => p.a - q.a || p.b - q.b).map((l) => ({ a: l.a, b: l.b, cost: l.cost, km: Math.round(l.km), ...(l.sea ? { sea: true } : {}) })) };
}

export function checkMap(map) {
  const errs = [...regionProblems(map.cities, map.links)];
  const n = map.cities.length;
  if (n !== NCITY) errs.push(`${n} cities`);
  if (map.links.length > MAX_LINK) errs.push(`${map.links.length} links (${MAX_LINK} at most)`);
  const deg = Array(n).fill(0);
  map.links.forEach((l) => { deg[l.a]++, deg[l.b]++; });
  deg.forEach((d, i) => { if (d < 1) errs.push(`${map.cities[i].name} has no link`); });
  if (deg.filter((d) => d < 2).length > 2) errs.push(`${deg.filter((d) => d < 2).length} cities with one link (2 at most)`);
  for (const l of map.links) if (l.cost < 3 || l.cost > 30) errs.push(`link ${map.cities[l.a].name}-${map.cities[l.b].name} costs ${l.cost}`);
  for (let i = 0; i < map.links.length; i++)
    for (let j = i + 1; j < map.links.length; j++) {
      const a = map.links[i], b = map.links[j];
      if (a.a === b.a || a.a === b.b || a.b === b.a || a.b === b.b) continue;
      const P = (k) => [map.cities[k].x, map.cities[k].z];
      if (segCross(P(a.a), P(a.b), P(b.a), P(b.b))) errs.push(`${map.cities[a.a].name}-${map.cities[a.b].name} crosses ${map.cities[b.a].name}-${map.cities[b.b].name}`);
    }
  for (const c of map.cities) {
    if (c.name.length > 12) errs.push(`${c.name}: too long a name`);
    if (c.x < 0 || c.x > MAP_W || c.z < 0 || c.z > 1) errs.push(`${c.name} is off the board`);
  }
  const names = new Set(map.cities.map((c) => c.name));
  if (names.size !== n) errs.push('two cities share a name');
  return errs.map((e) => `${map.key}: ${e}`);
}

let WATER = null;
export function waterTest() {
  if (WATER) return WATER;
  const land = landRings(loadData('land50')), lakes = landRings(loadData('lakes50'), (f) => f.properties.scalerank <= 4);
  WATER = (lon, lat) => !onLand(land, lon, lat) || onLand(lakes, lon, lat);
  return WATER;
}
export function buildMaps() {
  const places = loadPlaces(), water = waterTest();
  return MAPS.map((m) => makeMap(m, places, water));
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv[2] === '--extract') { extract(); process.exit(0); }
  const places = loadPlaces(), water = waterTest();
  for (const m of MAPS) {
    let map;
    try { map = makeMap(m, places, water); } catch (e) { console.log(`${m.key}: ${e.message}`); continue; }
    const errs = checkMap(map);
    const costs = map.links.map((l) => l.cost).sort((a, b) => a - b);
    const per = [0, 1, 2, 3, 4].map((r) => map.cities.filter((c) => c.region === r).map((c) => c.name).join(', '));
    console.log(`${map.key.padEnd(8)} ${map.cities.length} cities, ${map.links.length} links, spacing ${map.space}, ${map.kmPer} km a money, costs ${costs[0]}..${costs[costs.length - 1]} (median ${costs[costs.length >> 1]}), sets ${[3, 4].map((k) => regionSets(regionAdjacency(map.cities, map.links), k).length).join('/')}${errs.length ? '\n  ' + errs.join('\n  ') : ''}`);
    per.forEach((p, r) => console.log(`   ${r}: ${p}`));
  }
}
