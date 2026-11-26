// Real-world map data for the games' boards, shared by every game that draws a real map (Fever Line, Kilowatt
// Country). Node only, no dependencies.
//
// Data, fetched once into out/ (never committed; each file is checked by sha256 so every run reads the same bytes):
//   Natural Earth 5.1.2 (public domain, naturalearthdata.com): land, countries, lakes, rivers, state lines
//   GeoNames cities15000 (CC BY 4.0, geonames.org): towns and cities of 15000 people or more
// A game keeps the places it uses in its own committed data/places.tsv, so its maps are made the same way every time.
//
// Geometry helpers: polygon rings with bounding boxes, an even-odd point test, a Delaunay triangulation, a segment
// crossing test, the great-circle distance, and polygons and lines rasterised onto a board-sized grid.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
export const ROOT = join(HERE, '..', '..', '..');
const NE_CACHE = join(ROOT, 'out', 'ne-cache');
const NE = 'https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson';
const NE_DATA = {
  land50: ['ne_50m_land.geojson', 'e874b27a51d146452be360cafb3cc50c86001074a67d534113e6534682f9826b'],
  land110: ['ne_110m_land.geojson', '9e0729ee253ca7d7a5c4ae9395fb1902264c5377c52e224d13dd85010e2835d9'],
  countries50: ['ne_50m_admin_0_countries.geojson', '3e458fc036ad0a66411f2c1e6cac49c5d7bfb81cb1123bc513b22511a2b7fdeb'],
  lakes50: ['ne_50m_lakes.geojson', 'd350b75978b26fe839b797c2c529b2fb8f47fb3983c03f4964e36d5df9378a52'],
  rivers50: ['ne_50m_rivers_lake_centerlines.geojson', 'f286e0ce978fde999ca2d7a78c764be08542e19b63cded52b05c12d5173ccc51'],
  states50: ['ne_50m_admin_1_states_provinces_lines.geojson', '72cca93c850d412628a5da4bc5ebfe21ba4d376eb34611bde6b623ee73f0fdcf'],
};
function fetchChecked(dir, file, url, sha) {
  const path = join(dir, file);
  if (!existsSync(path)) {
    mkdirSync(dir, { recursive: true });
    execFileSync('curl', ['-sfL', '-o', path, url]);
  }
  const buf = readFileSync(path);
  const got = createHash('sha256').update(buf).digest('hex');
  if (got !== sha) throw new Error(`${file}: sha256 ${got}, expected ${sha}`);
  return { path, buf };
}
/* one Natural Earth layer as parsed GeoJSON */
export function loadData(name) {
  const d = NE_DATA[name];
  if (!d) throw new Error(`no Natural Earth layer ${name}`);
  return JSON.parse(fetchChecked(NE_CACHE, d[0], `${NE}/${d[0]}`, d[1]).buf.toString('utf8'));
}
/* GeoNames cities15000 rows: { name (the ASCII name), local (the name as written there), lat, lon, cc (country code), fcode, pop, admin1 } */
export function loadGeoNames() {
  const { path } = fetchChecked(join(ROOT, 'out', 'geonames-cache'), 'cities15000.zip', 'https://download.geonames.org/export/dump/cities15000.zip',
    'ef92c9b4d801f1da959372f53d2f586517b704239ea0b8756045a3d804438779');
  const txt = execFileSync('unzip', ['-p', path, 'cities15000.txt'], { maxBuffer: 1 << 28 }).toString('utf8');
  const rows = [];
  for (const line of txt.split('\n')) {
    const f = line.split('\t');
    if (f.length < 15) continue;
    rows.push({ name: f[2], local: f[1], lat: +f[4], lon: +f[5], fcode: f[7], cc: f[8], admin1: f[10], pop: +f[14] });
  }
  return rows;
}

// ── geometry ───────────────────────────────────────────────────────
/* every ring of every polygon in a GeoJSON layer (outer rings and holes alike), each with its bounding box; `keep`
 * filters features */
export function landRings(geo, keep = () => true) {
  const rings = [];
  for (const f of geo.features) {
    if (!keep(f)) continue;
    const g = f.geometry;
    if (!g) continue;
    const polys = g.type === 'Polygon' ? [g.coordinates] : g.type === 'MultiPolygon' ? g.coordinates : [];
    for (const p of polys)
      for (const r of p) {
        let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (const [x, y] of r) x0 = Math.min(x0, x), x1 = Math.max(x1, x), y0 = Math.min(y0, y), y1 = Math.max(y1, y);
        rings.push({ pts: r, box: [x0, y0, x1, y1] });
      }
  }
  return rings;
}
/* even-odd: inside an odd number of rings (a hole flips it back) */
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
/* every line of a GeoJSON layer of lines (LineString, MultiLineString) */
export function lines(geo, keep = () => true) {
  const out = [];
  for (const f of geo.features) {
    if (!keep(f) || !f.geometry) continue;
    const g = f.geometry;
    if (g.type === 'LineString') out.push(g.coordinates);
    else if (g.type === 'MultiLineString') for (const l of g.coordinates) out.push(l);
  }
  return out;
}

// Bowyer-Watson Delaunay triangulation of points [[x, y]...]: triangles as index triples
export function delaunay(P) {
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

export function segCross(a, b, c, d) {
  const o = (p, q, r) => Math.sign((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]));
  return o(a, b, c) * o(a, b, d) < 0 && o(c, d, a) * o(c, d, b) < 0;
}

/* the great-circle distance in km */
export function km(lon1, lat1, lon2, lat2) {
  const r = Math.PI / 180, dl = (lat2 - lat1) * r, dn = (lon2 - lon1) * r;
  const a = Math.sin(dl / 2) ** 2 + Math.cos(lat1 * r) * Math.cos(lat2 * r) * Math.sin(dn / 2) ** 2;
  return 2 * 6371 * Math.asin(Math.min(1, Math.sqrt(a)));
}

// ── rasters: a W x H grid over a board, with a function from pixel centre to lon/lat ─────
/* fills polygons into an Int16Array grid with the value of each feature (`value(feature)`, -1 skips it), even-odd
 * per feature, by scanlines: `toPx(lon, lat)` maps to pixel coordinates (x right, y down) */
export function fillPolygons(W, H, geo, toPx, value, grid = new Int16Array(W * H).fill(-1)) {
  for (const f of geo.features) {
    const v = value(f);
    if (v < 0 || !f.geometry) continue;
    const g = f.geometry;
    const polys = g.type === 'Polygon' ? [g.coordinates] : g.type === 'MultiPolygon' ? g.coordinates : [];
    const segs = [];
    let y0 = 1e9, y1 = -1e9;
    for (const p of polys)
      for (const r of p) {
        const q = r.map(([lon, lat]) => toPx(lon, lat));
        for (let i = 0, j = q.length - 1; i < q.length; j = i++) {
          segs.push([q[j][0], q[j][1], q[i][0], q[i][1]]);
          y0 = Math.min(y0, q[i][1]), y1 = Math.max(y1, q[i][1]);
        }
      }
    if (!segs.length) continue;
    for (let y = Math.max(0, Math.floor(y0)); y <= Math.min(H - 1, Math.ceil(y1)); y++) {
      const yc = y + 0.5, xs = [];
      for (const [ax, ay, bx, by] of segs) if ((ay > yc) !== (by > yc)) xs.push(ax + ((yc - ay) * (bx - ax)) / (by - ay));
      xs.sort((a, b) => a - b);
      for (let k = 0; k + 1 < xs.length; k += 2) {
        const xa = Math.max(0, Math.ceil(xs[k] - 0.5)), xb = Math.min(W - 1, Math.floor(xs[k + 1] - 0.5));
        for (let x = xa; x <= xb; x++) grid[y * W + x] = v;
      }
    }
  }
  return grid;
}
/* the distance in pixels from each pixel to the nearest seed pixel (`seed(i)` true; a seed counts as half a pixel
 * away), by a two-pass chamfer transform capped at `cap` */
export function seedDistance(W, H, seed, cap = 64) {
  const d = new Float32Array(W * H).fill(cap);
  for (let i = 0; i < W * H; i++) if (seed(i)) d[i] = 0.5;
  const A = 1, B = Math.SQRT2;
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const i = y * W + x;
      let v = d[i];
      if (x > 0) v = Math.min(v, d[i - 1] + A);
      if (y > 0) v = Math.min(v, d[i - W] + A);
      if (x > 0 && y > 0) v = Math.min(v, d[i - W - 1] + B);
      if (x < W - 1 && y > 0) v = Math.min(v, d[i - W + 1] + B);
      d[i] = v;
    }
  for (let y = H - 1; y >= 0; y--)
    for (let x = W - 1; x >= 0; x--) {
      const i = y * W + x;
      let v = d[i];
      if (x < W - 1) v = Math.min(v, d[i + 1] + A);
      if (y < H - 1) v = Math.min(v, d[i + W] + A);
      if (x < W - 1 && y < H - 1) v = Math.min(v, d[i + W + 1] + B);
      if (x > 0 && y < H - 1) v = Math.min(v, d[i + W - 1] + B);
      d[i] = v;
    }
  return d;
}
/* the distance in pixels to the nearest edge of `mask` (a pixel with a 4-neighbour of another value), on either
 * side; `count(a, b)` (default: always) says which pairs of values make an edge */
export function edgeDistance(W, H, mask, cap = 64, count = () => true) {
  return seedDistance(W, H, (i) => {
    const x = i % W, y = (i - x) / W, m = mask[i];
    const nb = [x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1];
    return nb.some((j) => j >= 0 && mask[j] !== m && count(m, mask[j]));
  }, cap);
}
/* the distance in pixels from each pixel to the nearest of the given polylines (pixel coordinates), up to `reach`:
 * a Float32Array with Infinity beyond it */
export function lineDistance(W, H, polylines, reach) {
  const d = new Float32Array(W * H).fill(Infinity);
  for (const pl of polylines)
    for (let k = 1; k < pl.length; k++) {
      const [ax, ay] = pl[k - 1], [bx, by] = pl[k];
      const x0 = Math.max(0, Math.floor(Math.min(ax, bx) - reach)), x1 = Math.min(W - 1, Math.ceil(Math.max(ax, bx) + reach));
      const y0 = Math.max(0, Math.floor(Math.min(ay, by) - reach)), y1 = Math.min(H - 1, Math.ceil(Math.max(ay, by) + reach));
      if (x0 > x1 || y0 > y1) continue;
      const vx = bx - ax, vy = by - ay, vv = vx * vx + vy * vy || 1;
      for (let y = y0; y <= y1; y++)
        for (let x = x0; x <= x1; x++) {
          const px = x + 0.5, py = y + 0.5, t = Math.max(0, Math.min(1, ((px - ax) * vx + (py - ay) * vy) / vv));
          const e = Math.hypot(px - ax - vx * t, py - ay - vy * t), i = y * W + x;
          if (e < d[i]) d[i] = e;
        }
    }
  return d;
}
