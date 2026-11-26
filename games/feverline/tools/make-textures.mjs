#!/usr/bin/env node
// Fever Line art: every texture the game loads, drawn here from the map (data/map.json, made by tools/gen-map.mjs),
// shapes and noise (no other inputs, Node only).
//
//   node games/feverline/tools/make-textures.mjs      writes games/feverline/assets/textures/*.png
//
// board_<map>.png   each map board at night (data/maps.json, made by tools/gen-maps.mjs): Natural Earth's land, tinted
//             by the nearest city's fever region, roads, sea lanes, city sockets (world x -11..11, z -6.2..6.2 on
//             2048 x 1152 pixels; src/layout.h uses the same numbers)
// table.png   the dark wood of the table the board lies on
// pdeck.png, fdeck.png   the backs of the city deck and the fever deck
// art.png     2D art the HUD and the card faces draw: the four fever emblems, the surge, the clinic, and the five
//             role portraits (256 px cells; src/view.c names the cells). Loaded as a 2D picture, so no .cwimport.
import { deflateSync } from 'node:zlib';
import { mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadData } from './gen-maps.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'textures');
const MAPS = JSON.parse(readFileSync(join(HERE, '..', 'data', 'maps.json'), 'utf8'));
const LAND = { land50: rings(loadData('land50')), land110: rings(loadData('land110')) };
function rings(geo) {
  const out = [];
  for (const f of geo.features) for (const p of f.geometry.type === 'Polygon' ? [f.geometry.coordinates] : f.geometry.coordinates) for (const r of p) out.push(r);
  return out;
}
mkdirSync(OUT, { recursive: true });

// ── PNG writer ──────────────────────────────────────────────────────
const CRC = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
const crc32 = (buf) => {
  let c = 0xffffffff;
  for (const b of buf) c = CRC[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
};
const chunk = (type, data) => {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
};
function writePng(name, img, ktx = true) {
  const { w, h, px } = img;
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    for (let i = 0; i < w * 4; i++) {
      const v = px[y * w * 4 + i];
      raw[y * (w * 4 + 1) + 1 + i] = Math.max(0, Math.min(255, Math.round((i & 3) === 3 ? v * 255 : lin2srgb(v) * 255)));
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
  writeFileSync(join(OUT, name), png);
  if (ktx) writeFileSync(join(OUT, `${name}.cwimport`), "texture.format = ktx2\ntexture.mode = uastc\ntexture.color = srgb\n");
  else rmSync(join(OUT, `${name}.cwimport`), { force: true });
  console.log(`${name} ${w}x${h} ${png.length} bytes`);
}
// colours are painted in linear light and stored as sRGB
const srgb2lin = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
const lin2srgb = (c) => { c = Math.max(0, Math.min(1, c)); return c <= 0.0031308 ? c * 12.92 : 1.055 * c ** (1 / 2.4) - 0.055; };
const hex = (h, a = 1) => [srgb2lin(((h >> 16) & 255) / 255), srgb2lin(((h >> 8) & 255) / 255), srgb2lin((h & 255) / 255), a];
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const shade = (c, k) => [c[0] * k, c[1] * k, c[2] * k, c[3]];

// ── noise ───────────────────────────────────────────────────────────
function hash2(x, y, seed) {
  let h = Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263) + Math.imul(seed, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
function vnoise(x, y, seed) {
  const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
  const s = (t) => t * t * (3 - 2 * t);
  const a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
  return a + (b - a) * s(xf) + (c - a) * s(yf) + (a - b - c + d) * s(xf) * s(yf);
}
function fbm(x, y, seed, oct = 5) {
  let v = 0, amp = 0.5, f = 1;
  for (let o = 0; o < oct; o++) { v += amp * vnoise(x * f, y * f, seed + o * 17); f *= 2; amp *= 0.5; }
  return v;
}
function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (s + 0x6d2b79f5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

// ── an image with SDF shape painting (antialiased by distance) ─────
function image(w, h, fill = [0, 0, 0, 0]) {
  const px = new Float32Array(w * h * 4);
  for (let i = 0; i < w * h; i++) px.set(fill, i * 4);
  return { w, h, px };
}
function blend(img, x, y, c, a) {
  if (a <= 0 || x < 0 || y < 0 || x >= img.w || y >= img.h) return;
  const i = (y * img.w + x) * 4, p = img.px;
  const sa = c[3] * Math.min(1, a), da = p[i + 3], oa = sa + da * (1 - sa);
  for (let k = 0; k < 3; k++) p[i + k] = oa > 0 ? (c[k] * sa + p[i + k] * da * (1 - sa)) / oa : 0;
  p[i + 3] = oa;
}
// paint where sdf(x, y) < 0 (pixel centres, distance in pixels); col may be a function of (x, y)
function paint(img, bbox, sdf, col, soft = 1) {
  const [x0, y0, x1, y1] = bbox.map(Math.round);
  for (let y = Math.max(0, y0 - 2); y < Math.min(img.h, y1 + 2); y++)
    for (let x = Math.max(0, x0 - 2); x < Math.min(img.w, x1 + 2); x++) {
      const d = sdf(x + 0.5, y + 0.5);
      const a = Math.max(0, Math.min(1, 0.5 - d / soft));
      if (a > 0) blend(img, x, y, typeof col === 'function' ? col(x + 0.5, y + 0.5) : col, a);
    }
}
const circle = (img, cx, cy, r, col, soft) => paint(img, [cx - r, cy - r, cx + r, cy + r], (x, y) => Math.hypot(x - cx, y - cy) - r, col, soft);
const ring = (img, cx, cy, r, t, col) => paint(img, [cx - r - t, cy - r - t, cx + r + t, cy + r + t], (x, y) => Math.abs(Math.hypot(x - cx, y - cy) - r) - t / 2, col);
const ellipse = (img, cx, cy, rx, ry, col, rot = 0) => paint(img, [cx - Math.max(rx, ry), cy - Math.max(rx, ry), cx + Math.max(rx, ry), cy + Math.max(rx, ry)], (x, y) => {
  const c = Math.cos(rot), s = Math.sin(rot), dx = x - cx, dy = y - cy, u = dx * c + dy * s, v = -dx * s + dy * c;
  const k = Math.hypot(u / rx, v / ry);
  return (k - 1) * Math.min(rx, ry);
}, col);
function rbox(img, cx, cy, hw, hh, r, col, rot = 0, soft = 1) {
  const R = Math.hypot(hw, hh);
  paint(img, [cx - R, cy - R, cx + R, cy + R], (x, y) => {
    const c = Math.cos(rot), s = Math.sin(rot), dx = x - cx, dy = y - cy;
    const u = Math.abs(dx * c + dy * s) - hw + r, v = Math.abs(-dx * s + dy * c) - hh + r;
    return Math.hypot(Math.max(u, 0), Math.max(v, 0)) + Math.min(Math.max(u, v), 0) - r;
  }, col, soft);
}
function segDist(x, y, ax, ay, bx, by) {
  const px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
  const t = Math.max(0, Math.min(1, (px * dx + py * dy) / (dx * dx + dy * dy || 1)));
  return Math.hypot(px - dx * t, py - dy * t);
}
const line = (img, ax, ay, bx, by, t, col) => paint(img, [Math.min(ax, bx) - t, Math.min(ay, by) - t, Math.max(ax, bx) + t, Math.max(ay, by) + t],
  (x, y) => segDist(x, y, ax, ay, bx, by) - t / 2, col);
function poly(img, pts, col, soft) {
  const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  paint(img, [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)], (x, y) => {
    let d = Infinity, inside = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      const [xi, yi] = pts[i], [xj, yj] = pts[j];
      d = Math.min(d, segDist(x, y, xi, yi, xj, yj));
      if ((yi > y) !== (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside ? -d : d;
  }, col, soft);
}
// a polyline drawn as round-capped segments
const stroke = (img, pts, t, col) => { for (let i = 1; i < pts.length; i++) line(img, pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1], t, col); };
// per-pixel texture over an area (noise grain)
function grain(img, amt, scale, seed, x0 = 0, y0 = 0, x1 = img.w, y1 = img.h) {
  for (let y = y0; y < y1; y++)
    for (let x = x0; x < x1; x++) {
      const i = (y * img.w + x) * 4;
      if (img.px[i + 3] <= 0) continue;
      const n = 1 + (fbm(x / scale, y / scale, seed, 3) - 0.5) * amt + (hash2(x, y, seed + 9) - 0.5) * amt * 0.35;
      for (let k = 0; k < 3; k++) img.px[i + k] *= n;
    }
}


// ── the fevers' colours (src/view.c uses the same) ─────────────────
export const FEVER = [0xff6a2a, 0x2ad4e0, 0x7ae04a, 0xb46aff];
const TINT = [0x4a2c1e, 0x1d3a44, 0x2b3a1f, 0x35243f];

// ── the map board ──────────────────────────────────────────────────
const BW = 2048, BH = 1152, KX = BW / 22, KZ = BH / 12.4;
const wx = (x) => (x + 11) * KX, wz = (z) => (z + 6.2) * KZ;

function board(m) {
  const img = image(BW, BH, [0, 0, 0, 1]);
  const cities = m.cities;
  const P = m.proj, ca = Math.cos((P.deg * Math.PI) / 180), sa = Math.sin((P.deg * Math.PI) / 180);
  const fwd = (lon, lat) => { const x = (lon - P.lonm) * P.k, y = -(lat - P.latm); return [(x * ca - y * sa - P.cx) * P.scale, (x * sa + y * ca - P.cy) * P.scale]; };
  // the land: Natural Earth's polygons, projected and filled (even-odd scanlines)
  const mask = new Uint8Array(BW * BH);
  const rings = LAND[m.land];
  for (const ring of rings) {
    const pts = ring.map(([lon, lat]) => { const [x, z] = fwd(lon, lat); return [wx(x), wz(z)]; });
    let y0 = Infinity, y1 = -Infinity, x0 = Infinity, x1 = -Infinity;
    for (const [x, y] of pts) y0 = Math.min(y0, y), y1 = Math.max(y1, y), x0 = Math.min(x0, x), x1 = Math.max(x1, x);
    if (y1 < 0 || y0 >= BH || x1 < 0 || x0 >= BW) continue;
    for (let y = Math.max(0, Math.floor(y0)); y <= Math.min(BH - 1, Math.ceil(y1)); y++) {
      const yc = y + 0.5, xs = [];
      for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
        const [xi, yi] = pts[i], [xj, yj] = pts[j];
        if ((yi > yc) !== (yj > yc)) xs.push(xi + ((yc - yi) * (xj - xi)) / (yj - yi));
      }
      xs.sort((a, b) => a - b);
      for (let k = 0; k + 1 < xs.length; k += 2)
        for (let x = Math.max(0, Math.ceil(xs[k] - 0.5)); x <= Math.min(BW - 1, Math.floor(xs[k + 1] - 0.5)); x++) mask[y * BW + x] ^= 1;
    }
  }
  // a softened copy of the mask for the coast and the shallows
  let soft = Float32Array.from(mask);
  for (let pass = 0; pass < 3; pass++) {
    const tmp = new Float32Array(BW * BH), R = 4;
    for (let y = 0; y < BH; y++) { let acc = 0; for (let x = -R; x < BW + R; x++) { if (x + R < BW) acc += soft[y * BW + x + R]; if (x - R - 1 >= 0) acc -= soft[y * BW + x - R - 1]; if (x >= 0 && x < BW) tmp[y * BW + x] = acc / (2 * R + 1); } }
    const out = new Float32Array(BW * BH);
    for (let x = 0; x < BW; x++) { let acc = 0; for (let y = -R; y < BH + R; y++) { if (y + R < BH) acc += tmp[(y + R) * BW + x]; if (y - R - 1 >= 0) acc -= tmp[(y - R - 1) * BW + x]; if (y >= 0 && y < BH) out[y * BW + x] = acc / (2 * R + 1); } }
    soft = out;
  }
  const cx = cities.map((c) => wx(c.x)), cz = cities.map((c) => wz(c.z)), reach = 2.6 * KX;
  for (let y = 0; y < BH; y++)
    for (let x = 0; x < BW; x++) {
      const i = y * BW + x, sw = fbm(x / 90 + y / 300, y / 60, 5, 4);
      let col = mix(hex(0x050d1a), hex(0x0c1d33), 0.35 + sw * 0.5);
      const f = soft[i];
      if (!mask[i] && f > 0.02) col = mix(col, hex(0x143c5e), Math.min(1, f * 1.6) * 0.6); // shallows
      if (mask[i]) {
        let best = 1e18, reg = -1;
        for (let k = 0; k < cities.length; k++) { const d = (cx[k] - x) ** 2 + (cz[k] - y) ** 2; if (d < best) best = d, reg = cities[k].region; }
        const tone = fbm(x / 70, y / 70, 61, 5), near = Math.max(0, 1 - Math.sqrt(best) / reach);
        const base = mix(hex(0x2a2a2c), hex(TINT[reg]), 0.35 + 0.65 * near);
        col = mix(base, shade(base, 1.6), tone * 0.8);
        if (f < 0.75) col = mix(col, hex(0xb9a87a), (0.75 - f) / 0.75 * 0.6); // the coast
      }
      const gx = Math.abs(((x + 64) % 256) - 128), gy = Math.abs(((y + 64) % 192) - 96);
      if (gx > 127 || gy > 95) col = mix(col, hex(0x3a5878), 0.16);
      img.px[i * 4] = col[0], img.px[i * 4 + 1] = col[1], img.px[i * 4 + 2] = col[2], img.px[i * 4 + 3] = 1;
    }
  const dashed = (ax, ay, bx, by) => {
    const len = Math.hypot(bx - ax, by - ay), n = Math.max(2, Math.floor(len / 22));
    for (let k = 0; k < n; k += 2) {
      const t0 = k / n, t1 = Math.min(1, (k + 1) / n);
      line(img, ax + (bx - ax) * t0, ay + (by - ay) * t0, ax + (bx - ax) * t1, ay + (by - ay) * t1, 3.4, hex(0x9cc4e4, 0.75));
    }
  };
  for (const r of m.roads) {
    const A = cities[r.a], B = cities[r.b];
    const ax = wx(A.x), ay = wz(A.z), bx = wx(B.x), by = wz(B.z);
    if (r.kind === 0) {
      line(img, ax, ay, bx, by, 7, hex(0x0a0806, 0.55));
      line(img, ax, ay, bx, by, 3.2, hex(0xe8d6a4, 0.8));
    } else if (r.kind === 1) dashed(ax, ay, bx, by);
    else { // round the back of the world: each end runs off its own edge
      const [w, e] = A.x < B.x ? [[ax, ay], [bx, by]] : [[bx, by], [ax, ay]];
      const midz = (w[1] + e[1]) / 2;
      dashed(w[0], w[1], 30, midz);
      dashed(e[0], e[1], BW - 30, midz);
    }
  }
  for (const c of cities) {
    circle(img, wx(c.x), wz(c.z), 26, hex(0x07060a, 0.75), 3);
    ring(img, wx(c.x), wz(c.z), 24, 3, hex(0xe8d6a4, 0.55));
  }
  for (const [inset, t] of [[14, 4], [26, 1.5]]) {
    line(img, inset, inset, BW - inset, inset, t, hex(0xc9a964, 0.85));
    line(img, inset, BH - inset, BW - inset, BH - inset, t, hex(0xc9a964, 0.85));
    line(img, inset, inset, inset, BH - inset, t, hex(0xc9a964, 0.85));
    line(img, BW - inset, inset, BW - inset, BH - inset, t, hex(0xc9a964, 0.85));
  }
  writePng(`board_${m.key}.png`, img);
}

// ── the table: dark walnut ─────────────────────────────────────────
function table() {
  const S = 512, img = image(S, S, [0, 0, 0, 1]);
  for (let y = 0; y < S; y++)
    for (let x = 0; x < S; x++) {
      const plank = Math.floor(x / 128), u = x / S, v = y / S;
      const g = fbm(u * 3 + plank * 7.1, v * 40, 5, 5), ring2 = Math.sin((v * 60 + g * 9 + plank * 3) * 1.0) * 0.5 + 0.5;
      let c = mix(hex(0x1c120c), hex(0x3a2418), 0.35 + ring2 * 0.35 + (g - 0.5) * 0.6);
      if (x % 128 < 2 || x % 128 > 125) c = shade(c, 0.45);
      img.px.set(c.slice(0, 3), (y * S + x) * 4);
      img.px[(y * S + x) * 4 + 3] = 1;
    }
  writePng('table.png', img);
}

// ── deck backs ─────────────────────────────────────────────────────
function deckBack(name, base, line1, emblem) {
  const W = 256, H = 352, img = image(W, H, hex(base));
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const i = (y * W + x) * 4, n = fbm(x / 30, y / 30, 3, 3);
      for (let k = 0; k < 3; k++) img.px[i + k] *= 0.8 + n * 0.4;
    }
  for (let k = -12; k < 24; k++) {
    line(img, k * 22, 0, k * 22 + H, H, 1.4, hex(line1, 0.35));
    line(img, k * 22, H, k * 22 + H, 0, 1.4, hex(line1, 0.35));
  }
  rbox(img, W / 2, H / 2, W / 2 - 12, H / 2 - 12, 14, hex(0, 0), 0);
  ring(img, W / 2, H / 2, 70, 5, hex(0xe0c88a, 0.9));
  ring(img, W / 2, H / 2, 56, 2, hex(0xe0c88a, 0.7));
  emblem(img, W / 2, H / 2, 46);
  for (const [i, t] of [[10, 4], [18, 1.5]]) {
    line(img, i, i, W - i, i, t, hex(0xe0c88a, 0.9)); line(img, i, H - i, W - i, H - i, t, hex(0xe0c88a, 0.9));
    line(img, i, i, i, H - i, t, hex(0xe0c88a, 0.9)); line(img, W - i, i, W - i, H - i, t, hex(0xe0c88a, 0.9));
  }
  writePng(name, img);
}

// ── emblems (drawn at a centre and a size) ─────────────────────────
function flame(img, cx, cy, s, col) { // Ember: a flame drop
  poly(img, [[cx, cy - s], [cx + s * 0.55, cy - s * 0.1], [cx + s * 0.62, cy + s * 0.45], [cx + s * 0.3, cy + s * 0.85], [cx, cy + s * 0.95],
    [cx - s * 0.3, cy + s * 0.85], [cx - s * 0.62, cy + s * 0.45], [cx - s * 0.45, cy - s * 0.05], [cx - s * 0.2, cy + s * 0.15]], col);
  poly(img, [[cx + s * 0.05, cy - s * 0.2], [cx + s * 0.32, cy + s * 0.4], [cx, cy + s * 0.75], [cx - s * 0.3, cy + s * 0.45]], hex(0xffe0a0, 0.9));
}
function wave(img, cx, cy, s, col) { // Tide: a curling crest over two swells
  const pts = [];
  for (let k = 0; k <= 24; k++) { const a = Math.PI * 1.15 - (k / 24) * Math.PI * 1.5; pts.push([cx + Math.cos(a) * s * 0.55, cy - s * 0.1 + Math.sin(a) * s * 0.55]); }
  stroke(img, pts, s * 0.18, col);
  for (const dy of [0.45, 0.8]) {
    const w = [];
    for (let k = 0; k <= 30; k++) { const t = k / 30; w.push([cx - s * 0.9 + t * s * 1.8, cy + s * dy + Math.sin(t * Math.PI * 3) * s * 0.08]); }
    stroke(img, w, s * 0.12, col);
  }
}
function leaf(img, cx, cy, s, col) { // Moss: a leaf with its vein
  ellipse(img, cx, cy, s * 0.42, s * 0.92, col, 0.6);
  line(img, cx - s * 0.5, cy + s * 0.62, cx + s * 0.45, cy - s * 0.6, s * 0.07, hex(0x0a1a08, 0.8));
  for (let k = -2; k <= 2; k++) line(img, cx + k * s * 0.18, cy - k * s * 0.22, cx + k * s * 0.18 + s * 0.25, cy - k * s * 0.22 + s * 0.12, s * 0.05, hex(0x0a1a08, 0.6));
}
function crescent(img, cx, cy, s, col) { // Dusk: a crescent and a star
  paint(img, [cx - s, cy - s, cx + s, cy + s], (x, y) => Math.max(Math.hypot(x - cx, y - cy) - s * 0.8, -(Math.hypot(x - cx - s * 0.38, y - cy + s * 0.15) - s * 0.66)), col);
  const st = [];
  for (let k = 0; k < 10; k++) { const a = -Math.PI / 2 + (k * Math.PI) / 5, r = k % 2 ? s * 0.12 : s * 0.3; st.push([cx + s * 0.45 + Math.cos(a) * r, cy - s * 0.1 + Math.sin(a) * r]); }
  poly(img, st, col);
}
const EMBLEMS = [flame, wave, leaf, crescent];
function spiral(img, cx, cy, s, col) { // the surge: a spiral bursting outward
  const pts = [];
  for (let k = 0; k <= 80; k++) { const t = k / 80, a = t * Math.PI * 5, r = s * (0.08 + t * 0.85); pts.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]); }
  stroke(img, pts, s * 0.13, col);
  for (let k = 0; k < 8; k++) { const a = (k * Math.PI) / 4 + 0.2; line(img, cx + Math.cos(a) * s * 1.0, cy + Math.sin(a) * s * 1.0, cx + Math.cos(a) * s * 1.2, cy + Math.sin(a) * s * 1.2, s * 0.08, col); }
}
function clinicIcon(img, cx, cy, s, col) { // a small house with a lit window and a lantern
  poly(img, [[cx - s * 0.75, cy - s * 0.05], [cx, cy - s * 0.75], [cx + s * 0.75, cy - s * 0.05]], col);
  rbox(img, cx, cy + s * 0.4, s * 0.58, s * 0.48, 4, col);
  rbox(img, cx - s * 0.22, cy + s * 0.28, s * 0.14, s * 0.14, 2, hex(0xffd27a));
  rbox(img, cx + s * 0.22, cy + s * 0.28, s * 0.14, s * 0.14, 2, hex(0xffd27a));
  rbox(img, cx, cy + s * 0.66, s * 0.13, s * 0.22, 2, hex(0x1a120c));
}

// ── role portraits: a medallion, a figure, and the role's tool ─────
export const ROLE_COL = [0xe9e2d0, 0xe8c04a, 0xf07ab0, 0xc8844a, 0x8fb0d8];
function portrait(img, cx, cy, role) {
  const s = 120, base = hex(ROLE_COL[role]);
  circle(img, cx, cy, s, (x, y) => mix(shade(base, 0.32), shade(base, 0.12), Math.hypot(x - cx, y - cy - 40) / (s * 1.4)));
  ring(img, cx, cy, s - 4, 6, hex(0xe0c88a));
  // the figure: shoulders and head
  // a faint map of roads behind the figure
  for (let k = 0; k < 7; k++) {
    const a = k * 0.9 + role, r1 = s * (0.35 + 0.08 * k);
    line(img, cx + Math.cos(a) * r1, cy + Math.sin(a) * r1, cx + Math.cos(a + 1.1) * (r1 + 18), cy + Math.sin(a + 1.1) * (r1 + 18), 2, hex(ROLE_COL[role], 0.25));
    circle(img, cx + Math.cos(a) * r1, cy + Math.sin(a) * r1, 4, hex(ROLE_COL[role], 0.4));
  }
  const fig = shade(base, 0.85), lit = shade(base, 1.15), rim = shade(base, 0.45);
  // shoulders and head, lit from the upper left, a dark rim to lift them off the medallion
  ellipse(img, cx, cy + s * 0.78, s * 0.66, s * 0.46, rim);
  ellipse(img, cx, cy + s * 0.78, s * 0.62, s * 0.42, (x, y) => mix(lit, fig, Math.min(1, Math.max(0, (x - cx + s * 0.4) / (s * 0.9)))));
  circle(img, cx, cy - s * 0.12, s * 0.3, rim);
  circle(img, cx, cy - s * 0.12, s * 0.27, (x, y) => mix(lit, fig, Math.min(1, Math.max(0, (Math.hypot(x - cx + s * 0.1, y - cy + s * 0.25)) / (s * 0.35)))));
  const dark = hex(0x120c10, 0.85);
  if (role === 0) { // Field Medic: a satchel strap and a lamp
    line(img, cx - s * 0.45, cy + s * 0.42, cx + s * 0.35, cy + s * 0.85, s * 0.1, dark);
    rbox(img, cx + s * 0.55, cy + s * 0.05, s * 0.13, s * 0.2, 6, hex(0xffd27a));
    line(img, cx + s * 0.55, cy - s * 0.2, cx + s * 0.55, cy - s * 0.12, s * 0.05, hex(0xffd27a));
  } else if (role === 1) { // Analyst: a lens
    ring(img, cx + s * 0.38, cy + s * 0.18, s * 0.2, s * 0.07, hex(0xf6e7b8));
    line(img, cx + s * 0.52, cy + s * 0.33, cx + s * 0.7, cy + s * 0.55, s * 0.09, hex(0xf6e7b8));
  } else if (role === 2) { // Courier: a sealed letter with wings
    rbox(img, cx + s * 0.4, cy + s * 0.25, s * 0.26, s * 0.17, 4, hex(0xf6efe0));
    poly(img, [[cx + s * 0.14, cy + s * 0.09], [cx + s * 0.4, cy + s * 0.3], [cx + s * 0.66, cy + s * 0.09]], hex(0xb8a890));
    poly(img, [[cx + s * 0.66, cy + s * 0.2], [cx + s * 0.95, cy - s * 0.05], [cx + s * 0.8, cy + s * 0.28]], hex(0xf6efe0));
  } else if (role === 3) { // Engineer: a spanner
    line(img, cx + s * 0.15, cy + s * 0.6, cx + s * 0.6, cy + s * 0.1, s * 0.11, hex(0xd8d8d0));
    ring(img, cx + s * 0.65, cy + s * 0.05, s * 0.12, s * 0.08, hex(0xd8d8d0));
  } else if (role === 4) { // Warden: a shield
    poly(img, [[cx + s * 0.2, cy + s * 0.02], [cx + s * 0.75, cy + s * 0.02], [cx + s * 0.72, cy + s * 0.42], [cx + s * 0.47, cy + s * 0.68], [cx + s * 0.23, cy + s * 0.42]], hex(0xd8e4f0));
    line(img, cx + s * 0.47, cy + s * 0.08, cx + s * 0.47, cy + s * 0.58, s * 0.05, dark);
  }
  // keep it inside the medallion, then the gilt ring over the edge
  for (let y = Math.floor(cy - 128); y < cy + 128; y++)
    for (let x = Math.floor(cx - 128); x < cx + 128; x++) {
      const d = Math.hypot(x + 0.5 - cx, y + 0.5 - cy) - (s - 2);
      if (d > 0) img.px[(y * img.w + x) * 4 + 3] *= Math.max(0, 1 - d);
    }
  ring(img, cx, cy, s - 4, 6, hex(0xe0c88a));
}

function atlas() {
  const img = image(2048, 512, [0, 0, 0, 0]);
  const cell = (i) => [128 + (i % 8) * 256, 128 + Math.floor(i / 8) * 256];
  for (let f = 0; f < 4; f++) {
    const [cx, cy] = cell(f);
    circle(img, cx, cy, 116, hex(0x0a0d14, 0.85));
    ring(img, cx, cy, 110, 6, hex(FEVER[f]));
    EMBLEMS[f](img, cx, cy, 72, hex(FEVER[f]));
  }
  { const [cx, cy] = cell(4); circle(img, cx, cy, 116, hex(0x1a0808, 0.9)); ring(img, cx, cy, 110, 6, hex(0xff5040)); spiral(img, cx, cy, 80, hex(0xff7050)); }
  { const [cx, cy] = cell(5); circle(img, cx, cy, 116, hex(0x0a0d14, 0.85)); ring(img, cx, cy, 110, 6, hex(0xffd27a)); clinicIcon(img, cx, cy, 80, hex(0xf2ead8)); }
  for (let r = 0; r < 5; r++) { const [cx, cy] = cell(8 + r); portrait(img, cx, cy, r); }
  writePng('art.png', img, false);
}

for (const m of MAPS) board(m);
rmSync(join(OUT, 'board.png'), { force: true }), rmSync(join(OUT, 'board.png.cwimport'), { force: true });
table();
deckBack('pdeck.png', 0x14284a, 0x6a8cc0, (img, cx, cy, s) => clinicIcon(img, cx, cy, s * 0.9, hex(0xe0c88a)));
deckBack('fdeck.png', 0x1e1410, 0xb06040, (img, cx, cy, s) => spiral(img, cx, cy, s * 0.8, hex(0xe0c88a)));
atlas();
