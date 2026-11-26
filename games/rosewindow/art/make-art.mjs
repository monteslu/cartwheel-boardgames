#!/usr/bin/env node
// Rose Window art: every texture the game loads, drawn here (no inputs, no dependencies beyond Node).
//
//   node games/rosewindow/art/make-art.mjs      writes games/rosewindow/assets/textures/*.png
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'textures');
mkdirSync(OUT, { recursive: true });

// ── PNG writer ──────────────────────────────────────────────────────
const CRC = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}
function writePng(name, w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    for (let i = 0; i < w * 4; i++) raw[y * (w * 4 + 1) + 1 + i] = Math.max(0, Math.min(255, Math.round(rgba[y * w * 4 + i])));
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
  writeFileSync(join(OUT, name), png);
  // the import settings: KTX2 (UASTC), colour images as sRGB, normal maps as linear data
  writeFileSync(join(OUT, `${name}.cwimport`), `texture.format = ktx2\ntexture.mode = uastc\ntexture.color = ${name.endsWith("_n.png") ? "linear" : "srgb"}\n`);
  console.log(`${name} ${w}x${h} ${png.length} bytes`);
}

// ── noise ───────────────────────────────────────────────────────────
function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (s + 0x6d2b79f5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
function hash2(x, y, seed) {
  let h = Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(seed, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
// tileable value noise with period p
function vnoise(x, y, p, seed) {
  const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi;
  const sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  const m = (a) => ((a % p) + p) % p;
  const a = hash2(m(xi), m(yi), seed), b = hash2(m(xi + 1), m(yi), seed);
  const c = hash2(m(xi), m(yi + 1), seed), d = hash2(m(xi + 1), m(yi + 1), seed);
  return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
function fbm(x, y, p, seed, oct = 5) {
  let v = 0, amp = 0.5, f = 1;
  for (let o = 0; o < oct; o++) { v += amp * vnoise(x * f, y * f, p * f, seed + o * 17); amp *= 0.5; f *= 2; }
  return v;
}
const clamp = (v, a = 0, b = 1) => Math.max(a, Math.min(b, v));
const mix = (a, b, t) => a + (b - a) * t;
const smooth = (e0, e1, x) => { const t = clamp((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); };
const hex = (s) => [parseInt(s.slice(1, 3), 16), parseInt(s.slice(3, 5), 16), parseInt(s.slice(5, 7), 16)];

// ── the table: honey oak planks, tileable ───────────────────────────
function oak() {
  const N = 1024, px = new Float32Array(N * N * 4);
  const dark = hex('#4a2c14'), mid = hex('#8a5a2e'), lite = hex('#c08a50');
  const planks = 5, ph = N / planks;
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const p = Math.floor(y / ph), py = (y % ph) / ph;
      const seed = 70 + p * 11;
      const figure = fbm(x / 256, y / 48, 4, seed, 3);
      const streak = fbm(x / 160, y / 1.8 + figure * 7, 7, seed + 1, 4);
      const fine = fbm(x / 36, y / 0.8, 29, seed + 2, 2);
      const ray = Math.max(0, fbm(x / 12, y / 3, 86, seed + 3, 2) - 0.72) * 2.2; /* oak's flecks */
      let t = clamp(0.5 + (streak - 0.5) * 0.85 + (fine - 0.5) * 0.3 + (figure - 0.5) * 0.45 + ray * 0.6);
      const seam = Math.min(py, 1 - py) * ph;
      const c = t < 0.5 ? [0, 1, 2].map((i) => mix(dark[i], mid[i], t * 2)) : [0, 1, 2].map((i) => mix(mid[i], lite[i], (t - 0.5) * 2));
      const k = seam < 2.5 ? 0.5 : 1;
      const o = (y * N + x) * 4;
      px[o] = c[0] * k; px[o + 1] = c[1] * k; px[o + 2] = c[2] * k; px[o + 3] = 255;
    }
  writePng('oak.png', N, N, px);
}

// ── the runner under the pool: a deep wine wool, tileable ────────────
function runner() {
  const N = 256, px = new Float32Array(N * N * 4);
  const base = hex('#5a1622'), lite = hex('#7a2a34'), deep = hex('#350c14');
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const warp = 0.5 + 0.5 * Math.sin(x * Math.PI * 2 / 4), weft = 0.5 + 0.5 * Math.sin(y * Math.PI * 2 / 4);
      const over = ((x >> 2) + (y >> 2)) & 1 ? warp : weft;
      const slub = fbm(x / 8, y / 64, 32, 31, 3), slub2 = fbm(x / 64, y / 8, 4, 32, 3);
      const t = clamp(0.35 + over * 0.3 + (slub - 0.5) * 0.4 + (slub2 - 0.5) * 0.3);
      const c = [0, 1, 2].map((i) => (t < 0.5 ? mix(deep[i], base[i], t * 2) : mix(base[i], lite[i], (t - 0.5) * 2)));
      const o = (y * N + x) * 4;
      px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = 255;
    }
  writePng('runner.png', N, N, px);
}

// pip centres on a face (-1..1), per value
const PIPS = [[], [[0, 0]], [[-1, -1], [1, 1]], [[-1, -1], [0, 0], [1, 1]], [[-1, -1], [1, -1], [-1, 1], [1, 1]],
  [[-1, -1], [1, -1], [0, 0], [-1, 1], [1, 1]], [[-1, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [1, 1]]];

// ── die faces: a 3 x 2 atlas (face f in cell f - 1), frosted white pips on black: the dice's glow map ──
function pips() {
  const C = 256, W = C * 3, H = C * 2, px = new Float32Array(W * H * 4);
  for (let f = 1; f <= 6; f++) {
    const cx0 = ((f - 1) % 3) * C, cy0 = Math.floor((f - 1) / 3) * C;
    for (let y = 0; y < C; y++)
      for (let x = 0; x < C; x++) {
        const u = (x + 0.5) / C * 2 - 1, v = (y + 0.5) / C * 2 - 1;
        let a = 0;
        for (const [px0, py0] of PIPS[f]) {
          const d = Math.hypot(u - px0 * 0.5, v - py0 * 0.5);
          a = Math.max(a, smooth(0.15, 0.12, d));
        }
        const frost = 0.85 + 0.15 * hash2(x, y, f);
        const o = ((cy0 + y) * W + cx0 + x) * 4;
        px[o] = px[o + 1] = px[o + 2] = 255 * a * frost; px[o + 3] = 255;
      }
  }
  writePng('pips.png', W, H, px);
  // the same faces for a die lit from behind: the glass glows, the pips stand dark
  const inv = new Float32Array(px.length);
  for (let i = 0; i < px.length; i += 4) { const v = 255 - px[i] * 0.85; inv[i] = inv[i + 1] = inv[i + 2] = v; inv[i + 3] = 255; }
  writePng('pips_lit.png', W, H, inv);
}

// ── panes: a 4 x 2 atlas. Cell 0 frosted clear glass, 1..6 frosted glass etched with that many pips (a number mark),
// cell 7 streaky cathedral glass (tinted per colour mark by the mesh) ──
function panes() {
  const C = 128, W = C * 4, H = C * 2, px = new Float32Array(W * H * 4);
  for (let k = 0; k < 8; k++) {
    const cx0 = (k % 4) * C, cy0 = Math.floor(k / 4) * C;
    for (let y = 0; y < C; y++)
      for (let x = 0; x < C; x++) {
        const u = (x + 0.5) / C * 2 - 1, v = (y + 0.5) / C * 2 - 1;
        let t;
        if (k === 7) {
          const streak = fbm(x / 40, y / 6, 4, 90, 4), seed = fbm(x / 6, y / 6, 22, 91, 2);
          t = 0.62 + (streak - 0.5) * 0.55 + (seed - 0.5) * 0.15;
        } else {
          t = 0.78 + (fbm(x / 10, y / 10, 13, 80 + k, 3) - 0.5) * 0.25;
          for (const [px0, py0] of (k >= 1 && k <= 6 ? PIPS[k] : [])) {
            const d = Math.hypot(u - px0 * 0.48, v - py0 * 0.48);
            t = mix(t, 0.32, smooth(0.2, 0.16, d)); /* the etched ring */
            t = mix(t, 0.55, smooth(0.14, 0.1, d));
          }
        }
        const edge = Math.max(Math.abs(u), Math.abs(v));
        t *= mix(1, 0.75, smooth(0.86, 1.0, edge)); /* darker where the lead holds it */
        const o = ((cy0 + y) * W + cx0 + x) * 4;
        px[o] = px[o + 1] = px[o + 2] = 255 * clamp(t); px[o + 3] = 255;
      }
  }
  writePng('panes.png', W, H, px);
}

// ── a soft pool of light (the colour a lit pane throws on the table) ──
function spill() {
  const N = 128, px = new Float32Array(N * N * 4);
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const u = (x + 0.5) / N * 2 - 1, v = (y + 0.5) / N * 2 - 1;
      /* a rounded square with a soft rim: the pane's shape, blurred by distance */
      const d = Math.pow(Math.pow(Math.abs(u), 4) + Math.pow(Math.abs(v), 4), 0.25);
      const a = smooth(1.0, 0.45, d);
      const o = (y * N + x) * 4;
      px[o] = px[o + 1] = px[o + 2] = 255 * a; px[o + 3] = 255;
    }
  writePng('spill.png', N, N, px);
}

oak();
runner();
pips();
panes();
spill();
