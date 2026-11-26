#!/usr/bin/env node
// Fever Line's maps: real cities at their real places, picked and linked by our own rules from public-domain
// geographic data (Natural Earth: populated places for the cities, land polygons for the coasts), never from any
// other game's board.
//
//   node games/feverline/tools/gen-maps.mjs            writes src/feverline_maps.h and data/maps.json
//   node games/feverline/tools/gen-maps.mjs --check    the files match what the data and the rules make, and every
//                                                      map holds its rules (below)
//   node games/feverline/tools/gen-maps.mjs --control  the same checks on a map with a link cut out: must fail
//
// The rules, per map:
//   1. Project the map's box (longitude scaled by the cosine of its middle latitude) and scale it to fill the
//      22 x 12.4 board; a long diagonal country (Japan) is turned by the angle that lets it fill the board best.
//   2. Cities: the most populous places in the box first, each kept only when it stands far enough from the cities
//      already kept and its name label (drawn under it) overlaps no other label or city: the board stays readable.
//      Names longer than 11 letters are left out.
//   3. Links: the Delaunay triangulation of the cities, cut down to its Gabriel edges (no city inside the circle
//      over a link), so nothing crosses; links that run mostly over water are sea lanes, and a long sea lane goes.
//      Shortest triangulation edges come back where a city would have fewer than two links or the map would fall
//      apart; no city keeps more than six. The world map adds two links across the Pacific edge.
//   4. A cure takes the map's own number of cards (cure: 4 on most maps, 3 on the smallest and the hardest ones),
//      and the deck may hold calm cards (each: the next spread flips one card fewer) on a map whose short deck
//      ran the team out of time; both tuned with the CPU team's win rates (tests/cpu_bench.c).
//   5. Four fever regions: k-means on the board positions, evened out, ordered north-west, north-east, south-west,
//      south-east. The first clinic stands in the most populous city.
//
// Data: Natural Earth 5.1.2 (public domain) land polygons, fetched once into out/ne-cache (checked by sha256), and
// cities from GeoNames (CC BY 4.0, geonames.org): `--extract <cities15000.txt>` keeps the places the maps can use
// (towns and cities, not city districts, the bigger ones) in data/places.tsv, which is committed, so the maps are
// made the same way every time.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
const ROOT = join(GAME, '..', '..');
const CACHE = join(ROOT, 'out', 'ne-cache');
const NE = 'https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson';
const DATA = {
  land50: ['ne_50m_land.geojson', 'e874b27a51d146452be360cafb3cc50c86001074a67d534113e6534682f9826b'],
  land110: ['ne_110m_land.geojson', '9e0729ee253ca7d7a5c4ae9395fb1902264c5377c52e224d13dd85010e2835d9'],
};
export function loadData(name) {
  const [file, sha] = DATA[name];
  const path = join(CACHE, file);
  if (!existsSync(path)) {
    mkdirSync(CACHE, { recursive: true });
    execFileSync('curl', ['-sfL', '-o', path, `${NE}/${file}`]);
  }
  const buf = readFileSync(path);
  const got = createHash('sha256').update(buf).digest('hex');
  if (got !== sha) throw new Error(`${file}: sha256 ${got}, expected ${sha}`);
  return JSON.parse(buf.toString('utf8'));
}

export const BOARD_W = 22, BOARD_D = 12.4;
const FIT_W = 20.0, FIT_D = 10.8; // the cities' area inside the board's frame
export const MAPS = [
  { key: 'world', title: 'World', cure: 4, calm: 0, lon: [-125, 153], lat: [-40, 62], n: 48, land: 'land110', wrap: true },
  { key: 'namerica', title: 'North America', cure: 4, calm: 0, lon: [-124, -66], lat: [17, 52], n: 44, land: 'land50' },
  { key: 'europe', title: 'Europe', cure: 4, calm: 4, lon: [-10, 32], lat: [36, 60], n: 46, land: 'land50' },
  { key: 'nordic', title: 'Nordic', cure: 4, calm: 2, lon: [4, 31], lat: [54.5, 70.5], n: 40, land: 'land50' },
  { key: 'britain', title: 'British Isles', cure: 3, calm: 0, lon: [-10.5, 1.9], lat: [49.9, 58.7], n: 40, land: 'land50' },
  { key: 'india', title: 'India', cure: 3, calm: 4, lon: [61, 97], lat: [6, 34], n: 44, land: 'land50' },
  { key: 'africa', title: 'Africa', cure: 4, calm: 4, lon: [-18, 51], lat: [-35, 37], n: 44, land: 'land50' },
  { key: 'eastasia', title: 'East Asia', cure: 4, calm: 2, lon: [96, 142], lat: [10, 45], n: 46, land: 'land50' },
  { key: 'japan', title: 'Japan', cure: 3, calm: 2, lon: [129.5, 145.5], lat: [31, 44], n: 40, land: 'land50', turn: true },
];

// ── geometry ───────────────────────────────────────────────────────
function landRings(geo) {
  const rings = [];
  for (const f of geo.features) {
    const g = f.geometry;
    const polys = g.type === 'Polygon' ? [g.coordinates] : g.coordinates;
    for (const p of polys)
      for (const r of p) {
        let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (const [x, y] of r) x0 = Math.min(x0, x), x1 = Math.max(x1, x), y0 = Math.min(y0, y), y1 = Math.max(y1, y);
        rings.push({ pts: r, box: [x0, y0, x1, y1] });
      }
  }
  return rings;
}
export function onLand(rings, lon, lat) {
  let inside = false;
  for (const { pts, box } of rings) {
    if (lon < box[0] || lon > box[2] || lat < box[1] || lat > box[3]) continue;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      const [xi, yi] = pts[i], [xj, yj] = pts[j];
      if ((yi > lat) !== (yj > lat) && lon < ((xj - xi) * (lat - yi)) / (yj - yi) + xi) inside = !inside;
    }
  }
  return inside;
}
/* a map's projection: lon/lat to board x/z and back */
export function projection(m) {
  const latm = (m.lat[0] + m.lat[1]) / 2, k = Math.cos((latm * Math.PI) / 180), lonm = (m.lon[0] + m.lon[1]) / 2;
  const raw = (lon, lat) => [(lon - lonm) * k, -(lat - latm)];
  const corners = [];
  for (const lon of [m.lon[0], m.lon[1]]) for (const lat of [m.lat[0], m.lat[1]]) corners.push(raw(lon, lat));
  let best = null;
  for (let deg = m.turn ? -40 : 0; deg <= (m.turn ? 40 : 0); deg += 5) { /* only a map shaped like a long diagonal is turned */
    const a = (deg * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
    const pts = (m.fitPts || corners).map(([x, y]) => [x * c - y * s, x * s + y * c]);
    const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
    const w = Math.max(...xs) - Math.min(...xs), h = Math.max(...ys) - Math.min(...ys);
    const scale = Math.min(FIT_W / w, FIT_D / h);
    if (!best || scale > best.scale + 1e-9) best = { deg, scale, cx: (Math.max(...xs) + Math.min(...xs)) / 2, cy: (Math.max(...ys) + Math.min(...ys)) / 2 };
  }
  const a = (best.deg * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
  const fwd = (lon, lat) => { const [x, y] = raw(lon, lat); return [(x * c - y * s - best.cx) * best.scale, (x * s + y * c - best.cy) * best.scale]; };
  const inv = (bx, bz) => {
    const x = bx / best.scale + best.cx, y = bz / best.scale + best.cy;
    const rx = x * c + y * s, ry = -x * s + y * c;
    return [rx / k + lonm, -ry + latm];
  };
  return { fwd, inv, params: { lonm, latm, k, deg: best.deg, scale: best.scale, cx: best.cx, cy: best.cy } };
}

// Bowyer-Watson Delaunay triangulation of points [[x, y]...]: triangles as index triples
function delaunay(P) {
  const n = P.length, big = 1e4;
  const pts = P.concat([[-big, -big], [big, -big], [0, big]]);
  let tris = [[n, n + 1, n + 2]];
  const circ = (t) => {
    const [ax, ay] = pts[t[0]], [bx, by] = pts[t[1]], [cx, cy] = pts[t[2]];
    const d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    const ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
    const uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
    return [ux, uy, (ax - ux) ** 2 + (ay - uy) ** 2];
  };
  for (let i = 0; i < n; i++) {
    const [px, py] = pts[i];
    const bad = tris.filter((t) => { const [ux, uy, r2] = circ(t); return (px - ux) ** 2 + (py - uy) ** 2 < r2; });
    const edges = new Map();
    for (const t of bad)
      for (const [a, b] of [[t[0], t[1]], [t[1], t[2]], [t[2], t[0]]]) {
        const k = a < b ? `${a},${b}` : `${b},${a}`;
        edges.set(k, (edges.get(k) || 0) + 1);
      }
    tris = tris.filter((t) => !bad.includes(t));
    for (const [k, c] of edges) if (c === 1) { const [a, b] = k.split(',').map(Number); tris.push([a, b, i]); }
  }
  return tris.filter((t) => t.every((v) => v < n));
}

function segCross(a, b, c, d) {
  const o = (p, q, r) => Math.sign((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]));
  return o(a, b, c) * o(a, b, d) < 0 && o(c, d, a) * o(c, d, b) < 0;
}

// ── one map ────────────────────────────────────────────────────────
const MIN_SPACE = 1.3;   // board units between cities
const labelBox = (c) => { const w = 0.18 * c.name.length + 0.15; return [c.x - w / 2, c.z + 0.24, c.x + w / 2, c.z + 0.64]; };
const boxHit = (a, b) => a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];

export function makeMap(m, places, rings) {
  const cand = places.filter((p) => p.lon >= m.lon[0] && p.lon <= m.lon[1] && p.lat >= m.lat[0] && p.lat <= m.lat[1])
    .sort((a, b) => b.pop - a.pop || a.name.localeCompare(b.name));
  m.fitPts = null;
  const proj0 = projection(m);
  // fit the board to the populous places themselves, not the whole box
  const raw = (p) => { const latm = proj0.params.latm, k = proj0.params.k, lonm = proj0.params.lonm; return [(p.lon - lonm) * k, -(p.lat - latm)]; };
  m.fitPts = cand.slice(0, Math.max(m.n * 3, 60)).map(raw);
  const proj = projection(m);
  const cities = [];
  for (const p of cand) {
    if (cities.length >= m.n) break;
    if (!/^[A-Za-z .'-]+$/.test(p.name) || p.name.length > 11) continue;
    const [x, z] = proj.fwd(p.lon, p.lat);
    if (Math.abs(x) > BOARD_W / 2 - 0.7 || z < -BOARD_D / 2 + 0.6 || z > BOARD_D / 2 - 1.0) continue;
    const c = { name: p.name, x: +x.toFixed(2), z: +z.toFixed(2), lon: p.lon, lat: p.lat, pop: p.pop };
    if (cities.some((o) => Math.hypot(o.x - c.x, o.z - c.z) < MIN_SPACE)) continue;
    const lb = labelBox(c);
    if (cities.some((o) => boxHit(lb, labelBox(o)) || boxHit(lb, [o.x - 0.3, o.z - 0.55, o.x + 0.3, o.z + 0.3]) ||
                         boxHit(labelBox(o), [c.x - 0.3, c.z - 0.55, c.x + 0.3, c.z + 0.3]))) continue;
    if (cities.some((o) => o.name === c.name)) continue;
    cities.push(c);
  }
  const n = cities.length;
  const P = cities.map((c) => [c.x, c.z]);
  const tris = delaunay(P);
  const dEdges = new Map();
  for (const t of tris) for (const [a, b] of [[t[0], t[1]], [t[1], t[2]], [t[2], t[0]]]) dEdges.set(a < b ? `${a}-${b}` : `${b}-${a}`, [Math.min(a, b), Math.max(a, b)]);
  const len = ([a, b]) => Math.hypot(P[a][0] - P[b][0], P[a][1] - P[b][1]);
  const waterShare = ([a, b]) => {
    let wet = 0;
    for (let i = 1; i < 12; i++) {
      const t = i / 12, [lon, lat] = proj.inv(P[a][0] + (P[b][0] - P[a][0]) * t, P[a][1] + (P[b][1] - P[a][1]) * t);
      if (!onLand(rings, lon, lat)) wet++;
    }
    return wet / 11;
  };
  const all = [...dEdges.values()].map((e) => ({ a: e[0], b: e[1], len: len(e), sea: waterShare(e) > 0.35 }));
  const gabriel = (e) => {
    const mx = (P[e.a][0] + P[e.b][0]) / 2, mz = (P[e.a][1] + P[e.b][1]) / 2, r = e.len / 2;
    return !P.some((p, k) => k !== e.a && k !== e.b && Math.hypot(p[0] - mx, p[1] - mz) < r - 1e-6);
  };
  const landLens = all.filter((e) => !e.sea && gabriel(e)).map((e) => e.len).sort((x, y) => x - y);
  const median = landLens.length ? landLens[Math.floor(landLens.length / 2)] : 2.5;
  let keep = all.filter((e) => gabriel(e) && e.len <= (e.sea ? 1.7 : 2.4) * median);
  const deg = () => { const d = Array(n).fill(0); for (const e of keep) d[e.a]++, d[e.b]++; return d; };
  const comps = () => {
    const id = Array(n).fill(-1);
    let k = 0;
    for (let s = 0; s < n; s++) {
      if (id[s] >= 0) continue;
      const q = [s];
      id[s] = k;
      while (q.length) { const u = q.pop(); for (const e of keep) { const v = e.a === u ? e.b : e.b === u ? e.a : -1; if (v >= 0 && id[v] < 0) id[v] = k, q.push(v); } }
      k++;
    }
    return { id, k };
  };
  const spare = all.filter((e) => !keep.includes(e)).sort((x, y) => (x.sea - y.sea) || x.len - y.len);
  // every city at least two links
  for (const e of spare) { const d = deg(); if ((d[e.a] < 2 || d[e.b] < 2) && d[e.a] < 6 && d[e.b] < 6) keep.push(e); }
  // one network
  for (let guard = 0; guard < 50; guard++) {
    const { id, k } = comps();
    if (k <= 1) break;
    const e = spare.filter((x) => !keep.includes(x) && id[x.a] !== id[x.b]).sort((x, y) => x.len - y.len)[0];
    if (!e) break;
    keep.push(e);
  }
  // at most six links a city: the longest go while both ends keep two and the network holds
  for (let guard = 0; guard < 100; guard++) {
    const d = deg(), over = d.findIndex((v) => v > 6);
    if (over < 0) break;
    const mine = keep.filter((e) => e.a === over || e.b === over).sort((x, y) => y.len - x.len);
    let cut = false;
    for (const e of mine) {
      if (d[e.a] <= 2 || d[e.b] <= 2) continue;
      keep = keep.filter((x) => x !== e);
      if (comps().k === 1) { cut = true; break; }
      keep.push(e);
    }
    if (!cut) break;
  }
  const roads = keep.map((e) => ({ a: e.a, b: e.b, kind: e.sea ? 1 : 0 }));
  // the world's edge: two links across the Pacific, between the westmost and eastmost cities closest round the back
  if (m.wrap) {
    const west = cities.map((c, i) => i).filter((i) => cities[i].x < -FIT_W / 2 + 3.5);
    const east = cities.map((c, i) => i).filter((i) => cities[i].x > FIT_W / 2 - 3.5);
    const pairs = [];
    for (const a of west) for (const b of east) pairs.push([(cities[a].x + FIT_W / 2) + (FIT_W / 2 - cities[b].x) + Math.abs(cities[a].z - cities[b].z), a, b]);
    pairs.sort((x, y) => x[0] - y[0]);
    const used = new Set();
    for (const [, a, b] of pairs) {
      if (roads.filter((r) => r.kind === 2).length >= 2) break;
      if (used.has(a) || used.has(b)) continue;
      used.add(a), used.add(b);
      roads.push({ a: Math.min(a, b), b: Math.max(a, b), kind: 2 });
    }
  }
  // four fever regions: k-means, evened out
  let cent = [];
  const far = (pts) => pts.reduce((b, p) => (cent.length === 0 ? (p.x + p.z < b.x + b.z ? p : b) : Math.min(...cent.map((c) => Math.hypot(c[0] - p.x, c[1] - p.z))) > Math.min(...cent.map((c) => Math.hypot(c[0] - b.x, c[1] - b.z))) ? p : b));
  for (let k = 0; k < 4; k++) { const p = far(cities); cent.push([p.x, p.z]); }
  let reg = Array(n).fill(0);
  for (let it = 0; it < 40; it++) {
    reg = cities.map((c) => cent.map((q, k) => [Math.hypot(q[0] - c.x, q[1] - c.z), k]).sort((x, y) => x[0] - y[0])[0][1]);
    const want = Math.max(5, Math.floor(n / 4) - 1);
    for (let pass = 0; pass < 20; pass++) { // even out: a small region takes the nearest city of a bigger one
      const count = [0, 1, 2, 3].map((k) => reg.filter((r) => r === k).length);
      const small = count.findIndex((v) => v < want);
      if (small < 0) break;
      let best = -1, bd = 1e9;
      reg.forEach((r, i) => { if (count[r] > want) { const d = Math.hypot(cent[small][0] - cities[i].x, cent[small][1] - cities[i].z); if (d < bd) bd = d, best = i; } });
      if (best < 0) break;
      reg[best] = small;
    }
    cent = [0, 1, 2, 3].map((k) => { const mine = cities.filter((c, i) => reg[i] === k); return mine.length ? [mine.reduce((s, c) => s + c.x, 0) / mine.length, mine.reduce((s, c) => s + c.z, 0) / mine.length] : cent[k]; });
  }
  // order: the two northern regions first (west, east), then the southern ones
  const order = [0, 1, 2, 3].sort((a, b) => cent[a][1] - cent[b][1]);
  const top = order.slice(0, 2).sort((a, b) => cent[a][0] - cent[b][0]), bottom = order.slice(2).sort((a, b) => cent[a][0] - cent[b][0]);
  const rank = new Map([...top, ...bottom].map((k, i) => [k, i]));
  cities.forEach((c, i) => { c.region = rank.get(reg[i]); });
  const nb = cities.map(() => []);
  for (const r of roads) nb[r.a].push(r.b), nb[r.b].push(r.a);
  nb.forEach((list, i) => list.sort((p, q) => Math.atan2(cities[p].z - cities[i].z, cities[p].x - cities[i].x) - Math.atan2(cities[q].z - cities[i].z, cities[q].x - cities[i].x)));
  let start = 0;
  cities.forEach((c, i) => { if (c.pop > cities[start].pop) start = i; });
  return { key: m.key, title: m.title, cure: m.cure, calm: m.calm, land: m.land, wrap: !!m.wrap, proj: proj.params, cities: cities.map((c, i) => ({ ...c, nb: nb[i] })), roads, start };
}

export function checkMap(m) {
  const out = [];
  const n = m.cities.length;
  if (n < 20 || n > 48) out.push(`${m.key}: ${n} cities (20..48)`);
  for (const [i, c] of m.cities.entries()) {
    if (c.nb.length < 2 || c.nb.length > 6) out.push(`${m.key}: ${c.name} has ${c.nb.length} links (2..6)`);
    if (c.name.length > 11) out.push(`${m.key}: ${c.name} is too long a label`);
    for (const j of c.nb) if (!m.cities[j].nb.includes(i)) out.push(`${m.key}: ${c.name} one way`);
  }
  const seen = new Set([m.start]), q = [m.start];
  while (q.length) for (const j of m.cities[q.pop()].nb) if (!seen.has(j)) seen.add(j), q.push(j);
  if (seen.size !== n) out.push(`${m.key}: only ${seen.size} of ${n} cities reachable`);
  const flat = m.roads.filter((r) => r.kind !== 2);
  for (let i = 0; i < flat.length; i++)
    for (let j = i + 1; j < flat.length; j++) {
      const a = flat[i], b = flat[j];
      if (a.a === b.a || a.a === b.b || a.b === b.a || a.b === b.b) continue;
      const P = (k) => [m.cities[k].x, m.cities[k].z];
      if (segCross(P(a.a), P(a.b), P(b.a), P(b.b))) out.push(`${m.key}: ${m.cities[a.a].name}-${m.cities[a.b].name} crosses ${m.cities[b.a].name}-${m.cities[b.b].name}`);
    }
  for (let k = 0; k < 4; k++) {
    const c = m.cities.filter((x) => x.region === k).length;
    if (c < 5) out.push(`${m.key}: region ${k} has ${c} cities`);
  }
  for (let i = 0; i < n; i++)
    for (let j = i + 1; j < n; j++) if (boxHit(labelBox(m.cities[i]), labelBox(m.cities[j]))) out.push(`${m.key}: labels of ${m.cities[i].name} and ${m.cities[j].name} overlap`);
  return out;
}

function header(maps) {
  const L = [];
  const q = (s) => `"${s.replace(/"/g, '')}"`;
  L.push("/* Fever Line's maps, made by tools/gen-maps.mjs from Natural Earth (public domain): do not edit by hand. */");
  L.push('#ifndef FEVERLINE_MAPS_H');
  L.push('#define FEVERLINE_MAPS_H');
  L.push('#include <stdint.h>');
  L.push('#define FV_NCITY 48 /* the most cities a map has */');
  L.push('#define FV_MAX_NB 6');
  L.push('#define FV_MAX_ROADS 112');
  L.push(`#define FV_NMAPS ${maps.length}`);
  L.push('enum { FV_ROAD_LAND, FV_ROAD_SEA, FV_ROAD_WRAP };');
  L.push('typedef struct fv_city { const char *name; int8_t colour; int8_t nnb; int8_t nb[FV_MAX_NB]; float x, z; } fv_city;');
  L.push('typedef struct fv_map { const char *key, *title; int ncity, start, nroads, cure, calm; fv_city city[FV_NCITY]; int8_t road[FV_MAX_ROADS][3]; } fv_map;');
  L.push('static const fv_map FV_MAPS[FV_NMAPS] = {');
  for (const m of maps) {
    if (m.roads.length > 112) throw new Error(`${m.key}: ${m.roads.length} roads (112 at most)`);
    L.push(`  { ${q(m.key)}, ${q(m.title)}, ${m.cities.length}, ${m.start}, ${m.roads.length}, ${m.cure}, ${m.calm},`);
    L.push('    {');
    for (const c of m.cities) {
      const nb = c.nb.concat(Array(6 - c.nb.length).fill(-1));
      L.push(`      { ${q(c.name)}, ${c.region}, ${c.nb.length}, { ${nb.join(', ')} }, ${c.x.toFixed(2)}f, ${c.z.toFixed(2)}f },`);
    }
    L.push('    },');
    L.push('    { ' + m.roads.map((r) => `{ ${r.a}, ${r.b}, ${r.kind} }`).join(', ') + ' } },');
  }
  L.push('};');
  L.push('#endif');
  return L.join('\n') + '\n';
}

const PLACES = join(GAME, 'data', 'places.tsv');
function extract(file) {
  const keepCodes = new Set(['PPL', 'PPLA', 'PPLA2', 'PPLA3', 'PPLC', 'PPLG']);
  const rows = [];
  for (const line of readFileSync(file, 'utf8').split('\n')) {
    const f = line.split('\t');
    if (f.length < 15 || !keepCodes.has(f[7])) continue;
    const pop = +f[14], lat = +f[4], lon = +f[5];
    let name = f[2];
    const SHORT = { 'New York City': 'New York', 'Ho Chi Minh City': 'Ho Chi Minh' }; /* the common short names */
    if (SHORT[name]) name = SHORT[name];
    const small = MAPS.some((m) => m.n <= 40 && lon >= m.lon[0] && lon <= m.lon[1] && lat >= m.lat[0] && lat <= m.lat[1]);
    if (pop < (small ? 15000 : 60000) || !/^[A-Za-z .'-]+$/.test(name) || name.length > 11) continue;
    rows.push([name, lat.toFixed(4), lon.toFixed(4), pop]);
  }
  rows.sort((a, b) => b[3] - a[3] || a[0].localeCompare(b[0]));
  writeFileSync(PLACES, '# name\tlat\tlon\tpopulation: from GeoNames cities15000 (CC BY 4.0, geonames.org), towns and cities only\n' +
    rows.map((r) => r.join('\t')).join('\n') + '\n');
  console.log(`wrote ${rows.length} places`);
}
export function buildAll() {
  const places = readFileSync(PLACES, 'utf8').split('\n').filter((l) => l && !l.startsWith('#')).map((l) => { const [name, lat, lon, pop] = l.split('\t'); return { name, lat: +lat, lon: +lon, pop: +pop }; });
  const land = { land50: landRings(loadData('land50')), land110: landRings(loadData('land110')) };
  return MAPS.map((m) => makeMap({ ...m }, places, land[m.land]));
}

const isMain = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];
if (isMain) {
  const arg = process.argv[2];
  if (arg === '--extract') { extract(process.argv[3]); process.exit(0); }
  const HDR = join(GAME, 'src', 'feverline_maps.h'), JSONF = join(GAME, 'data', 'maps.json');
  if (arg === '--control') { /* the world map with one city's links cut out must fail the check */
    const maps = buildAll();
    const w = maps[0], i = w.cities.findIndex((c) => c.nb.length === 2);
    const cut = w.cities[i].nb[0];
    w.cities[i].nb = w.cities[i].nb.filter((x) => x !== cut);
    w.cities[cut].nb = w.cities[cut].nb.filter((x) => x !== i);
    const p = checkMap(w);
    if (!p.length) { console.log('control NOT caught: a city with one link passed'); process.exit(1); }
    console.log(`control caught: ${p[0]}`);
    process.exit(0);
  }
  const maps = buildAll();
  const problems = maps.flatMap(checkMap);
  if (problems.length) { console.error(problems.join('\n')); process.exit(1); }
  const h = header(maps), j = JSON.stringify(maps, null, 1) + '\n';
  if (arg === '--check') {
    let ok = true;
    try { ok = readFileSync(HDR, 'utf8') === h && readFileSync(JSONF, 'utf8') === j; } catch { ok = false; }
    if (!ok) { console.error('map files differ from what the data makes: run tools/gen-maps.mjs'); process.exit(1); }
    console.log(`maps ok: ${maps.map((m) => `${m.key} ${m.cities.length}`).join(', ')}`);
    process.exit(0);
  }
  mkdirSync(dirname(JSONF), { recursive: true });
  writeFileSync(HDR, h);
  writeFileSync(JSONF, j);
  for (const m of maps) {
    const deg = m.cities.map((c) => c.nb.length);
    console.log(`${m.key.padEnd(9)} ${m.cities.length} cities, ${m.roads.length} links (${m.roads.filter((r) => r.kind === 1).length} sea), turn ${m.proj.deg}, start ${m.cities[m.start].name}, degree ${(deg.reduce((a, b) => a + b, 0) / deg.length).toFixed(2)}, regions ${[0, 1, 2, 3].map((k) => m.cities.filter((c) => c.region === k).length).join('/')}`);
  }
}
