#!/usr/bin/env node
// First Hearth art: every texture the game loads, drawn here from shapes and noise (no inputs, Node only).
//
//   node games/firsthearth/tools/make-textures.mjs      writes games/firsthearth/assets/textures/*.png
//
// The village board's regions use the same numbers as src/layout.h (board 18 x 10 world units, x -9..9,
// z -6..4; a texture pixel is 2048/18 units across). Card faces follow data/firsthearth-data.mjs.
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CARDS } from '../data/firsthearth-data.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'textures');
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
function writePng(name, img) {
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
  writeFileSync(join(OUT, `${name}.cwimport`), "texture.format = ktx2\ntexture.mode = uastc\ntexture.color = srgb\n");
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

// ── digits: a small hand-cut numeral font (strokes) ────────────────
const DIGITS = {
  0: [[[0.2, 0], [0.8, 0], [1, 0.2], [1, 0.8], [0.8, 1], [0.2, 1], [0, 0.8], [0, 0.2], [0.2, 0]]],
  1: [[[0.25, 0.2], [0.55, 0], [0.55, 1]], [[0.25, 1], [0.85, 1]]],
  2: [[[0, 0.2], [0.2, 0], [0.8, 0], [1, 0.2], [1, 0.4], [0, 1], [1, 1]]],
  3: [[[0, 0], [1, 0], [0.45, 0.45], [0.8, 0.45], [1, 0.65], [1, 0.8], [0.8, 1], [0.2, 1], [0, 0.85]]],
  4: [[[0.7, 1], [0.7, 0], [0, 0.65], [1, 0.65]]],
  5: [[[1, 0], [0.1, 0], [0, 0.45], [0.75, 0.42], [1, 0.62], [1, 0.82], [0.8, 1], [0.15, 1], [0, 0.88]]],
  6: [[[0.9, 0], [0.4, 0], [0, 0.45], [0, 0.8], [0.2, 1], [0.8, 1], [1, 0.8], [1, 0.6], [0.8, 0.45], [0.2, 0.45], [0, 0.6]]],
  7: [[[0, 0], [1, 0], [0.35, 1]]],
  8: [[[0.5, 0.45], [0.15, 0.3], [0.15, 0.1], [0.3, 0], [0.7, 0], [0.85, 0.1], [0.85, 0.3], [0.5, 0.45], [0, 0.65], [0, 0.85], [0.2, 1], [0.8, 1], [1, 0.85], [1, 0.65], [0.5, 0.45]]],
  9: [[[1, 0.4], [0.8, 0.55], [0.2, 0.55], [0, 0.4], [0, 0.2], [0.2, 0], [0.8, 0], [1, 0.2], [1, 0.55], [0.6, 1], [0.1, 1]]],
};
function number(img, n, cx, cy, hgt, t, col) {
  const s = String(n), w = hgt * 0.62, gap = hgt * 0.22, total = s.length * w + (s.length - 1) * gap;
  let x = cx - total / 2;
  for (const ch of s) {
    for (const st of DIGITS[ch]) stroke(img, st.map(([u, v]) => [x + u * w, cy - hgt / 2 + v * hgt]), t, col);
    x += w + gap;
  }
}

// ── icons (each in a box of size s around cx, cy) ──────────────────
const C = {
  ink: hex(0x2a1c12), food: hex(0xb4492f), bone: hex(0xeee2c8), wood: hex(0x8a5a2b), woodend: hex(0xd8a868),
  clay: hex(0xc4672f), stone: hex(0x8d9196), gold: hex(0xe8b52c), green: hex(0x5f8f2f), star: hex(0xf2c94c), axe: hex(0x6b6f74),
};
const ICON = {
  food(img, cx, cy, s) { // a haunch: meat and a bone
    line(img, cx + s * 0.05, cy + s * 0.05, cx + s * 0.38, cy + s * 0.38, s * 0.12, C.bone);
    circle(img, cx + s * 0.4, cy + s * 0.34, s * 0.09, C.bone); circle(img, cx + s * 0.34, cy + s * 0.42, s * 0.09, C.bone);
    ellipse(img, cx - s * 0.08, cy - s * 0.08, s * 0.32, s * 0.24, C.food, -0.75);
    ellipse(img, cx - s * 0.14, cy - s * 0.15, s * 0.12, s * 0.07, shade(C.food, 1.5), -0.75);
  },
  wood(img, cx, cy, s) { // a log end-on and its side
    rbox(img, cx + s * 0.05, cy, s * 0.36, s * 0.17, s * 0.15, C.wood, -0.35);
    ellipse(img, cx - s * 0.27, cy + s * 0.12, s * 0.15, s * 0.19, C.woodend, -0.35);
    ring(img, cx - s * 0.27, cy + s * 0.12, s * 0.07, s * 0.03, C.wood);
  },
  clay(img, cx, cy, s) { // a brick
    poly(img, [[cx - s * 0.38, cy - s * 0.05], [cx + s * 0.1, cy - s * 0.28], [cx + s * 0.4, cy - s * 0.12], [cx - s * 0.08, cy + s * 0.12]], shade(C.clay, 1.3));
    poly(img, [[cx - s * 0.38, cy - s * 0.05], [cx - s * 0.08, cy + s * 0.12], [cx - s * 0.08, cy + s * 0.34], [cx - s * 0.38, cy + s * 0.17]], shade(C.clay, 0.8));
    poly(img, [[cx - s * 0.08, cy + s * 0.12], [cx + s * 0.4, cy - s * 0.12], [cx + s * 0.4, cy + s * 0.1], [cx - s * 0.08, cy + s * 0.34]], C.clay);
  },
  stone(img, cx, cy, s) {
    poly(img, [[cx - s * 0.36, cy + s * 0.22], [cx - s * 0.3, cy - s * 0.12], [cx - s * 0.05, cy - s * 0.32], [cx + s * 0.3, cy - s * 0.2], [cx + s * 0.38, cy + s * 0.18], [cx + s * 0.05, cy + s * 0.32]], C.stone);
    poly(img, [[cx - s * 0.3, cy - s * 0.12], [cx - s * 0.05, cy - s * 0.32], [cx + s * 0.3, cy - s * 0.2], [cx, cy - s * 0.02]], shade(C.stone, 1.35));
  },
  gold(img, cx, cy, s) {
    poly(img, [[cx - s * 0.34, cy + s * 0.12], [cx - s * 0.12, cy - s * 0.28], [cx + s * 0.22, cy - s * 0.22], [cx + s * 0.36, cy + s * 0.1], [cx + s * 0.05, cy + s * 0.3]], C.gold);
    poly(img, [[cx - s * 0.12, cy - s * 0.28], [cx + s * 0.22, cy - s * 0.22], [cx + s * 0.05, cy - s * 0.02]], shade(C.gold, 1.4));
    circle(img, cx + s * 0.16, cy - s * 0.16, s * 0.04, hex(0xfff6d0));
  },
  points(img, cx, cy, s) { // a sun disc with rays: lasting renown
    for (let k = 0; k < 10; k++) {
      const a = (k / 10) * Math.PI * 2;
      line(img, cx + Math.cos(a) * s * 0.22, cy + Math.sin(a) * s * 0.22, cx + Math.cos(a) * s * 0.4, cy + Math.sin(a) * s * 0.4, s * 0.07, C.star);
    }
    circle(img, cx, cy, s * 0.2, shade(C.star, 0.85));
    circle(img, cx - s * 0.05, cy - s * 0.05, s * 0.08, hex(0xfff2b0));
  },
  field(img, cx, cy, s) { // a sprout over a furrow
    ellipse(img, cx, cy + s * 0.3, s * 0.38, s * 0.1, hex(0x6b4423));
    line(img, cx, cy + s * 0.28, cx, cy - s * 0.15, s * 0.06, C.green);
    ellipse(img, cx - s * 0.15, cy - s * 0.08, s * 0.16, s * 0.07, C.green, 0.6);
    ellipse(img, cx + s * 0.15, cy - s * 0.2, s * 0.16, s * 0.07, shade(C.green, 1.2), -0.6);
  },
  tool(img, cx, cy, s) { // a hafted stone axe
    line(img, cx - s * 0.3, cy + s * 0.36, cx + s * 0.18, cy - s * 0.3, s * 0.08, C.wood);
    poly(img, [[cx + s * 0.02, cy - s * 0.38], [cx + s * 0.36, cy - s * 0.3], [cx + s * 0.32, cy - s * 0.02], [cx + s * 0.08, cy - s * 0.12]], C.axe);
    line(img, cx + s * 0.08, cy - s * 0.25, cx + s * 0.2, cy - s * 0.14, s * 0.05, hex(0xc9a06a));
  },
};
// the lore marks: Ember, Flint, Ochre, Flute, Drum, Cord, Basket, Starlore
const MARK = [
  (img, cx, cy, s, c) => { // ember: a flame
    poly(img, [[cx, cy - s * 0.45], [cx + s * 0.25, cy - s * 0.05], [cx + s * 0.28, cy + s * 0.2], [cx, cy + s * 0.4], [cx - s * 0.28, cy + s * 0.2], [cx - s * 0.2, cy - s * 0.02], [cx - s * 0.08, cy + s * 0.08]], c);
    poly(img, [[cx + s * 0.02, cy - s * 0.05], [cx + s * 0.13, cy + s * 0.15], [cx, cy + s * 0.32], [cx - s * 0.13, cy + s * 0.18]], hex(0xf8e0a0));
  },
  (img, cx, cy, s, c) => { // flint: a knapped point
    poly(img, [[cx, cy - s * 0.45], [cx + s * 0.22, cy + s * 0.05], [cx, cy + s * 0.42], [cx - s * 0.22, cy + s * 0.05]], c);
    for (let k = -2; k <= 2; k++) line(img, cx - s * 0.1, cy + k * s * 0.12, cx + s * 0.1, cy + k * s * 0.12 - s * 0.06, s * 0.025, shade(c, 1.5));
  },
  (img, cx, cy, s, c) => { // ochre: an open hand
    ellipse(img, cx, cy + s * 0.12, s * 0.2, s * 0.22, c);
    const f = [[-0.2, -0.05, -0.36, -0.3], [-0.08, -0.08, -0.12, -0.42], [0.05, -0.08, 0.04, -0.45], [0.16, -0.06, 0.2, -0.38], [0.18, 0.15, 0.4, 0.0]];
    for (const [a, b, x, y] of f) line(img, cx + a * s, cy + b * s, cx + x * s, cy + y * s, s * 0.1, c);
  },
  (img, cx, cy, s, c) => { // flute: a bone pipe with holes
    rbox(img, cx, cy, s * 0.42, s * 0.09, s * 0.09, c, -0.5);
    for (let k = -1; k <= 2; k++) circle(img, cx + Math.cos(-0.5) * k * s * 0.15, cy + Math.sin(-0.5) * k * s * 0.15, s * 0.035, C.ink);
  },
  (img, cx, cy, s, c) => { // drum: a hide drum with lacing
    rbox(img, cx, cy + s * 0.05, s * 0.28, s * 0.25, s * 0.06, c);
    ellipse(img, cx, cy - s * 0.2, s * 0.28, s * 0.08, shade(c, 1.4));
    stroke(img, [[cx - s * 0.26, cy - s * 0.12], [cx - s * 0.12, cy + s * 0.28], [cx, cy - s * 0.12], [cx + s * 0.12, cy + s * 0.28], [cx + s * 0.26, cy - s * 0.12]], s * 0.035, C.ink);
  },
  (img, cx, cy, s, c) => { // cord: a spiral
    const pts = [];
    for (let k = 0; k <= 60; k++) { const t = (k / 60) * Math.PI * 5, r = s * 0.04 + (k / 60) * s * 0.36; pts.push([cx + Math.cos(t) * r, cy + Math.sin(t) * r]); }
    stroke(img, pts, s * 0.07, c);
  },
  (img, cx, cy, s, c) => { // basket: a woven bowl
    paint(img, [cx - s * 0.4, cy - s * 0.1, cx + s * 0.4, cy + s * 0.4], (x, y) => Math.max(Math.hypot((x - cx) / 0.4, (y - (cy - s * 0.1)) / 0.45) - s, cy - s * 0.1 - y), c);
    for (let k = 0; k < 3; k++) line(img, cx - s * 0.36 + k * 0.02 * s, cy + k * s * 0.1, cx + s * 0.36 - k * 0.02 * s, cy + k * s * 0.1, s * 0.03, shade(c, 0.6));
    for (let k = -3; k <= 3; k++) line(img, cx + k * s * 0.09, cy - s * 0.08, cx + k * s * 0.07, cy + s * 0.3, s * 0.025, shade(c, 0.6));
  },
  (img, cx, cy, s, c) => { // starlore: seven stars
    const st = [[0, -0.35], [0.28, -0.18], [0.32, 0.15], [0.05, 0.36], [-0.28, 0.22], [-0.3, -0.12], [0, 0]];
    stroke(img, st.slice(0, 6).concat([st[0]]).map(([x, y]) => [cx + x * s, cy + y * s]), s * 0.015, shade(c, 0.7));
    for (const [x, y] of st) circle(img, cx + x * s, cy + y * s, s * 0.06, c);
  },
];
const ROLE = [
  (img, cx, cy, s, c) => ICON.field(img, cx, cy, s * 1.1),
  (img, cx, cy, s, c) => ICON.tool(img, cx, cy, s * 1.1),
  (img, cx, cy, s, c) => { // builders: a round hut
    rbox(img, cx, cy + s * 0.2, s * 0.3, s * 0.16, s * 0.04, hex(0xa0703c));
    poly(img, [[cx - s * 0.4, cy + s * 0.06], [cx, cy - s * 0.38], [cx + s * 0.4, cy + s * 0.06]], hex(0xc8a050));
    rbox(img, cx, cy + s * 0.25, s * 0.07, s * 0.11, s * 0.05, C.ink);
  },
  (img, cx, cy, s, c) => { // elders: a figure with a staff
    circle(img, cx - s * 0.05, cy - s * 0.28, s * 0.1, hex(0x7a4a2a));
    poly(img, [[cx - s * 0.2, cy + s * 0.38], [cx - s * 0.1, cy - s * 0.15], [cx + s * 0.02, cy - s * 0.15], [cx + s * 0.12, cy + s * 0.38]], hex(0x8a6a48));
    line(img, cx + s * 0.22, cy - s * 0.38, cx + s * 0.22, cy + s * 0.4, s * 0.05, C.wood);
  },
];
const FX = ['food', 'wood', 'clay', 'stone', 'gold', 'points', 'field', 'tool'];

// ── card faces: an atlas of 8 x 4 cells, 256 x 256 each, a card 184 x 256 in the middle of its cell ──
function cards() {
  const img = image(2048, 1024, [0, 0, 0, 0]);
  CARDS.forEach((c, i) => {
    const ox = (i % 8) * 256 + 36, oy = Math.floor(i / 8) * 256, w = 184, h = 256;
    const role = c.role !== undefined;
    const base = role ? hex(0xb8b48e) : hex(0xcfae7c);
    rbox(img, ox + w / 2, oy + h / 2, w / 2 - 1, h / 2 - 1, 14, shade(base, 0.62));
    rbox(img, ox + w / 2, oy + h / 2, w / 2 - 6, h / 2 - 6, 10, (x, y) => mix(base, shade(base, 0.82), fbm(x / 22, y / 22, 31 + i, 4)));
    // the emblem (the lore mark, or the role and its multiplier as a big number), then what the card gives now in
    // big icons and numbers: read at the table camera, the card's words are in the line under the board
    const ex = ox + w / 2, ey = oy + 70;
    circle(img, ex, ey, 54, role ? hex(0x5f6a3a, 0.55) : hex(0x8a4a2a, 0.5));
    ring(img, ex, ey, 54, 3, shade(base, 0.55));
    if (role) {
      ROLE[c.role](img, ex - 14, ey, 84, C.ink);
      // the multiplier: x and a big numeral
      line(img, ex + 22, ey + 4, ex + 34, ey + 18, 5, C.ink); line(img, ex + 34, ey + 4, ex + 22, ey + 18, 5, C.ink);
      number(img, c.mult, ex + 50, ey + 2, 40, 7, C.ink);
    } else {
      MARK[c.mark](img, ex, ey, 92, [hex(0xd2542a), hex(0x4b4f55), hex(0xb24a26), hex(0xe9dcc0), hex(0x8a5a35), hex(0x9a7b4a), hex(0xb08a4a), hex(0x3a4a8a)][c.mark]);
    }
    // the effects band: one effect large, two stacked
    rbox(img, ox + w / 2, oy + 190, w / 2 - 12, 54, 10, shade(base, 0.72));
    const fx = c.fx;
    if (!fx.length) {
      line(img, ox + w / 2 - 24, oy + 190, ox + w / 2 + 24, oy + 190, 6, shade(base, 0.45));
    }
    fx.forEach(([k, n], j) => {
      const one = fx.length === 1, cy = oy + 190 + (one ? 0 : (j ? 26 : -26)), s = one ? 74 : 52, nh = one ? 56 : 34;
      ICON[k](img, ox + w / 2 - (one ? 30 : 26), cy, s);
      number(img, n, ox + w / 2 + (one ? 36 : 30), cy, nh, one ? 9 : 7, C.ink);
    });
  });
  grain(img, 0.12, 6, 3);
  writePng('cards.png', img);
}

// ── dice: 3 x 2 faces of 128 px (1..6), carved bone with dark pips ──
function dice() {
  const img = image(384, 256, hex(0xe8dcc0));
  const P = { 1: [[0, 0]], 2: [[-1, -1], [1, 1]], 3: [[-1, -1], [0, 0], [1, 1]], 4: [[-1, -1], [1, -1], [-1, 1], [1, 1]], 5: [[-1, -1], [1, -1], [0, 0], [-1, 1], [1, 1]], 6: [[-1, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [1, 1]] };
  for (let f = 1; f <= 6; f++) {
    const ox = ((f - 1) % 3) * 128, oy = Math.floor((f - 1) / 3) * 128;
    rbox(img, ox + 64, oy + 64, 62, 62, 18, (x, y) => mix(hex(0xf0e6cc), hex(0xcdbb96), fbm(x / 18, y / 18, 77, 4)));
    for (const [u, v] of P[f]) {
      circle(img, ox + 64 + u * 30, oy + 64 + v * 30, 12, hex(0x3a2516));
      circle(img, ox + 62 + u * 30, oy + 62 + v * 30, 7, hex(0x1e120a));
    }
  }
  writePng('dice.png', img);
}

// ── the table: oiled dark wood planks ──
function table() {
  const img = image(512, 512, [0, 0, 0, 1]);
  for (let y = 0; y < 512; y++)
    for (let x = 0; x < 512; x++) {
      const plank = Math.floor(x / 128), u = x / 512, v = y / 512;
      const g = fbm(u * 3 + plank * 7.1, v * 40, 5, 5), ring2 = Math.sin((v * 60 + g * 9 + plank * 3) * 1.0) * 0.5 + 0.5;
      let c = mix(hex(0x3b2414), hex(0x6a4226), 0.35 + ring2 * 0.35 + (g - 0.5) * 0.6);
      if (x % 128 < 2 || x % 128 > 125) c = shade(c, 0.45);
      img.px.set(c.slice(0, 3), (y * 512 + x) * 4);
      img.px[(y * 512 + x) * 4 + 3] = 1;
    }
  writePng('table.png', img);
}

// ── a player mat: a stretched hide, darker toward its ragged edge, with hair-grain, a few old scars and a painted
// ochre zigzag band inside the stitching ──
function mat() {
  const img = image(512, 512, hex(0x5a3e26));
  const outline = [[20, 40], [140, 10], [370, 22], [492, 8], [500, 160], [486, 360], [500, 490], [330, 500], [150, 492], [12, 502], [24, 300], [8, 150]];
  poly(img, outline, (x, y) => {
    const blot = fbm(x / 60, y / 60, 12, 5), fine = fbm(x / 9, y / 22, 31, 3);
    let c = mix(hex(0xb8925f), hex(0x7a5a38), blot * 0.85 + (fine - 0.5) * 0.25);
    const ex = Math.min(x, 512 - x) / 256, ey = Math.min(y, 512 - y) / 256, edge = Math.min(ex, ey);
    return shade(c, 0.48 + 0.52 * Math.min(1, edge * 2.4)); // the edge darkens like cured hide
  });
  grain(img, 0.25, 14, 21);
  // hair-grain: short soft strokes running one way
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
  for (let k = 0; k < 900; k++) {
    const x = 30 + rnd() * 450, y = 40 + rnd() * 430, l = 4 + rnd() * 7;
    line(img, x, y, x + l * 0.35, y + l, 1, rnd() < 0.5 ? hex(0x5e4228, 0.18) : hex(0xd2ae7c, 0.14));
  }
  // a few old scars
  for (let k = 0; k < 5; k++) {
    const x = 80 + rnd() * 340, y = 90 + rnd() * 320, l = 18 + rnd() * 26, a = rnd() * Math.PI;
    line(img, x, y, x + Math.cos(a) * l, y + Math.sin(a) * l, 2, hex(0x4e3420, 0.35));
  }
  // the painted band: an ochre zigzag between two lines, inside the stitching on the top and bottom edges
  for (const yb of [62, 446]) {
    line(img, 40, yb - 10, 472, yb - 10, 3, hex(0x8a3e18, 0.8));
    line(img, 40, yb + 10, 472, yb + 10, 3, hex(0x8a3e18, 0.8));
    for (let x = 40; x < 472; x += 16) line(img, x, yb + 6, x + 8, yb - 6, 3, hex(0xa84a1c, 0.85)), line(img, x + 8, yb - 6, x + 16, yb + 6, 3, hex(0xa84a1c, 0.85));
  }
  // stitching
  for (let k = 0; k < 40; k++) {
    const t = k / 40;
    const pts = [[30 + t * 450, 36 - 14 * Math.sin(t * 9)], [30 + t * 450, 476 + 10 * Math.sin(t * 7)]];
    for (const [x, y] of pts) line(img, x - 5, y, x + 5, y, 3, hex(0x4a3020));
  }
  writePng('mat.png', img);
}

// ── the village board ──────────────────────────────────────────────
// world x -9..9 -> 0..2048, z -6..4 -> 0..1138 (the texture is 2048 x 1152: 64 px per... scale K)
const BW = 2048, BH = 1152, K = BW / 18;
const wx = (x) => (x + 9) * K, wz = (z) => (z + 6) * K;
export const REGIONS = {
  forest: [-6.3, -3.7, 2.4, 1.6], clay: [-2.1, -3.8, 1.8, 1.45], quarry: [2.1, -3.8, 1.8, 1.45], river: [6.3, -3.7, 2.4, 1.6],
  hunt: [-5.6, 0.0, 3.0, 1.55], hearth: [-1.55, 0.0, 1.25, 1.25], garden: [1.55, 0.0, 1.25, 1.25], tool: [5.5, 0.0, 1.9, 1.3],
};
function board() {
  const img = image(BW, BH, [0, 0, 0, 1]);
  // the land: earth and grass
  for (let y = 0; y < BH; y++)
    for (let x = 0; x < BW; x++) {
      const n = fbm(x / 160, y / 160, 1, 5), m = fbm(x / 30, y / 30, 2, 3);
      const c = mix(mix(hex(0x6f7a3a), hex(0x9a8a50), n), hex(0x5a6a2e), m * 0.35);
      img.px.set(c.slice(0, 3), (y * BW + x) * 4);
    }
  const R = (name) => { const [x, z, hw, hh] = REGIONS[name]; return [wx(x), wz(z), hw * K, hh * K]; };
  const blob = (name, col, seed, wob = 0.18) => {
    const [cx, cy, rx, ry] = R(name);
    paint(img, [cx - rx * 1.3, cy - ry * 1.3, cx + rx * 1.3, cy + ry * 1.3], (x, y) => {
      const a = Math.atan2(y - cy, x - cx), w = 1 + (fbm(Math.cos(a) * 2 + 9, Math.sin(a) * 2 + seed, seed, 3) - 0.5) * wob * 2;
      return (Math.hypot((x - cx) / rx, (y - cy) / ry) - w) * Math.min(rx, ry);
    }, col, 6);
  };
  // forest floor: dark moss and needles
  blob('forest', (x, y) => mix(hex(0x2f4a22), hex(0x4a5a2a), fbm(x / 20, y / 20, 3, 4)), 3);
  // clay bank: orange strata
  blob('clay', (x, y) => mix(hex(0xa4562a), hex(0xd08a4a), 0.5 + 0.5 * Math.sin(y / 9 + fbm(x / 60, y / 60, 4, 3) * 8)), 4);
  // quarry: grey rock with cracks
  blob('quarry', (x, y) => { const n = fbm(x / 35, y / 35, 5, 5); return mix(hex(0x6a6e72), hex(0xa8a8a4), n) ; }, 5);
  // the river: a band of water from the top edge down past the gold bar
  const [rcx, rcy, rrx, rry] = R('river');
  paint(img, [rcx - rrx * 1.6, 0, rcx + rrx * 1.6, rcy + rry * 1.4], (x, y) => {
    const mid = rcx + Math.sin(y / 90) * 60 - (y / BH) * 40;
    return Math.abs(x - mid) - (rrx * 0.62 + Math.sin(y / 37) * 12);
  }, (x, y) => mix(hex(0x1f4f6a), hex(0x3f8aa0), fbm(x / 50, y / 14, 6, 4)), 8);
  blob('river', (x, y) => mix(hex(0xbfa66a), hex(0xd8c48a), fbm(x / 14, y / 14, 7, 3)), 7, 0.1); // the gravel bar
  // hunting grounds: open meadow with tracks
  blob('hunt', (x, y) => mix(hex(0x8c9a48), hex(0xb0a85a), fbm(x / 40, y / 40, 8, 4)), 8, 0.12);
  const rr = rng(44);
  const [hx, hy, hrx, hry] = R('hunt');
  for (let k = 0; k < 9; k++) { // hoof prints in two trails
    const t = k / 8, x = hx - hrx * 0.8 + t * hrx * 1.6, y = hy + Math.sin(t * 5) * hry * 0.4 + (k % 2 ? 12 : -12);
    ellipse(img, x - 5, y, 6, 10, hex(0x4a3a20, 0.55), 1.2);
    ellipse(img, x + 5, y + 2, 6, 10, hex(0x4a3a20, 0.55), 1.2);
  }
  // hearth: packed earth ring with ash
  blob('hearth', (x, y) => mix(hex(0x6a4a30), hex(0x8a6a48), fbm(x / 18, y / 18, 9, 3)), 9, 0.06);
  { const [cx, cy] = R('hearth'); circle(img, cx, cy, 34, hex(0x2a2420)); for (let k = 0; k < 9; k++) { const a = k / 9 * Math.PI * 2; circle(img, cx + Math.cos(a) * 46, cy + Math.sin(a) * 46, 11, hex(0x8a8884)); } }
  // garden: furrows
  { const [cx, cy, rx, ry] = R('garden');
    paint(img, [cx - rx, cy - ry, cx + rx, cy + ry], (x, y) => Math.max(Math.abs(x - cx) - rx * 0.92, Math.abs(y - cy) - ry * 0.92),
      (x, y) => mix(hex(0x4a3020), hex(0x7a5a38), 0.5 + 0.5 * Math.sin((x - cx) / 7)), 4); }
  // tool bench: a flat stone slab on bare earth
  blob('tool', (x, y) => mix(hex(0x7a6a50), hex(0x9a8a6a), fbm(x / 30, y / 30, 10, 3)), 10, 0.08);
  // the near strip: card ledge and building yard, packed earth
  paint(img, [0, wz(1.6), BW, BH], (x, y) => wz(1.75) - y, (x, y) => mix(hex(0x7a6040), hex(0x947a52), fbm(x / 50, y / 50, 11, 4)), 10);
  for (let i = 0; i < 4; i++) { // card spots: a darker hide square each
    const cx = wx(-7.3 + i * 2.0), cy = wz(2.85);
    rbox(img, cx, cy, 0.78 * K, 1.06 * K, 10, hex(0x4a3422, 0.55));
  }
  for (let i = 0; i < 4; i++) { // building yards: flat stones
    const cx = wx(1.5 + i * 2.1), cy = wz(2.85);
    circle(img, cx, cy, 0.95 * K, hex(0x5a5048, 0.6));
    ring(img, cx, cy, 0.95 * K, 5, hex(0x3a3028, 0.8));
  }
  // a painted frame
  paint(img, [0, 0, BW, BH], (x, y) => Math.min(x, y, BW - x, BH - y) - 18, hex(0x3a2414), 3);
  paint(img, [0, 0, BW, BH], (x, y) => Math.abs(Math.min(x, y, BW - x, BH - y) - 26) - 3, hex(0xc8a060), 2);
  grain(img, 0.18, 9, 13);
  writePng('board.png', img);
}

// ── small token textures: wood grain for logs, a meeple fabric weave ──
function grainTex() {
  const img = image(256, 256, [0, 0, 0, 1]);
  for (let y = 0; y < 256; y++)
    for (let x = 0; x < 256; x++) {
      const g = fbm(x / 64, y / 6, 33, 4), r = 0.5 + 0.5 * Math.sin(y / 3 + g * 12);
      const c = mix(hex(0x6a4424), hex(0xa0703c), r * 0.6 + g * 0.4);
      img.px.set(c.slice(0, 3), (y * 256 + x) * 4);
    }
  writePng('grain.png', img);
}

board();
cards();

table();
mat();
grainTex();
