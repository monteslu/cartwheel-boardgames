#!/usr/bin/env node
// Lantern Harbor art: every texture the game loads, drawn here (no inputs, no dependencies beyond Node).
//
//   node games/lanternharbor/art/make-art.mjs      writes games/lanternharbor/assets/textures/*.png
//
// The board layout numbers (BOARD below) are the ones src/layout.h places pieces with.
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

// ── teak deck: planks running along x, tileable ──────────────────────
{
  const W = 512, H = 512, px = new Float32Array(W * H * 4);
  const planks = 8;
  for (let y = 0; y < H; y++) {
    const row = Math.floor(y / (H / planks)), v = (y % (H / planks)) / (H / planks);
    const seam = smooth(0.0, 0.04, v) * smooth(1.0, 0.96, v);
    for (let x = 0; x < W; x++) {
      const shift = (row * 173) % W, xs = (x + shift) % W;
      const end = (xs % 256) / 256;
      const joint = smooth(0, 0.01, end) * smooth(1, 0.99, end);
      const grain = fbm(x / 64, y / 6 + row * 3, 8, 11 + row, 4);
      const fine = vnoise(x / 3, y / 1.5, W / 3, 5);
      const tone = 0.55 + 0.25 * grain + 0.08 * fine + 0.08 * (hash2(row, Math.floor(xs / 256), 3) - 0.5);
      const k = tone * (0.35 + 0.65 * seam * joint);
      const i = (y * W + x) * 4;
      px[i] = 150 * k; px[i + 1] = 92 * k; px[i + 2] = 52 * k; px[i + 3] = 255;
    }
  }
  writePng('teak.png', W, H, px);
}

// ── quay wall: dressed stone blocks, tiles along x ───────────────────
{
  const W = 512, H = 128, px = new Float32Array(W * H * 4);
  for (let y = 0; y < H; y++) {
    const course = Math.floor(y / 32), v = (y % 32) / 32;
    for (let x = 0; x < W; x++) {
      const off = course % 2 ? 32 : 0, u = ((x + off) % 64) / 64, block = Math.floor((x + off) / 64);
      const mortar = smooth(0, 0.06, v) * smooth(1, 0.94, v) * smooth(0, 0.04, u) * smooth(1, 0.96, u);
      const n = fbm(x / 24, y / 24, W / 24, 31, 4);
      const tone = 0.45 + 0.3 * n + 0.12 * (hash2(block, course, 9) - 0.5);
      const k = mix(0.18, tone, mortar);
      const i = (y * W + x) * 4;
      px[i] = 160 * k; px[i + 1] = 150 * k; px[i + 2] = 138 * k; px[i + 3] = 255;
    }
  }
  writePng('quay.png', W, H, px);
}

// ── a player's board: the hillside (left), the town (right), the band of goods, coins and quay on top ──
// BOARD: the numbers of src/layout.h, in table units; S pixels a unit
{
  const BOARD = { w: 13.0, h: 7.7, cell: 1.5, gridZ: 2.8, hillX: 0.35, townX: 6.65 };
  const S = 80, W = Math.round(BOARD.w * S), H = Math.round(BOARD.h * S);
  const px = new Float32Array(W * H * 4);
  const GOODS = ['#e9dfc2', '#3a3f9e', '#4fc3b8', '#5d8a3a', '#d2632a'];
  const inRect = (x, y, x0, y0, w, h) => x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
  const rr = (x, y, x0, y0, w, h, r) => { // signed distance-ish to a rounded rect, < 0 inside
    const cx = Math.max(Math.abs(x - (x0 + w / 2)) - (w / 2 - r), 0), cy = Math.max(Math.abs(y - (y0 + h / 2)) - (h / 2 - r), 0);
    return Math.hypot(cx, cy) - r;
  };
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const ux = x / S, uz = y / S;
      // the frame: dark stained wood
      let c = [62, 40, 26].map((v) => v * (0.85 + 0.3 * fbm(x / 40, y / 5, W / 40, 3, 3)));
      // the top band: lighter oiled wood
      if (inRect(ux, uz, 0.2, 0.2, BOARD.w - 0.4, 2.4)) c = [110, 74, 46].map((v) => v * (0.8 + 0.35 * fbm(x / 60, y / 4, W / 60, 7, 3)));
      // hillside: green and earth terraces
      if (inRect(ux, uz, BOARD.hillX - 0.12, BOARD.gridZ - 0.12, 4 * BOARD.cell + 0.24, 3 * BOARD.cell + 0.24)) {
        const n = fbm(x / 30, y / 30, W / 30, 13, 4);
        c = [mix(58, 96, n), mix(78, 112, n), mix(40, 56, n)];
      }
      // town: cobbles
      if (inRect(ux, uz, BOARD.townX - 0.12, BOARD.gridZ - 0.12, 4 * BOARD.cell + 0.24, 3 * BOARD.cell + 0.24)) {
        const cx = Math.floor(x / 14), cy = Math.floor(y / 11), fx = (x % 14) / 14, fy = (y % 11) / 11;
        const edge = smooth(0, 0.15, fx) * smooth(1, 0.85, fx) * smooth(0, 0.18, fy) * smooth(1, 0.82, fy);
        const t = 0.6 + 0.3 * hash2(cx, cy, 21);
        c = [mix(60, 150 * t, edge), mix(55, 138 * t, edge), mix(52, 124 * t, edge)];
      }
      // sunken cells (plots and lots)
      for (const x0 of [BOARD.hillX, BOARD.townX])
        for (let r = 0; r < 3; r++)
          for (let k = 0; k < 4; k++) {
            const d = rr(ux, uz, x0 + k * BOARD.cell + 0.06, BOARD.gridZ + r * BOARD.cell + 0.06, BOARD.cell - 0.12, BOARD.cell - 0.12, 0.12);
            if (d < 0) c = c.map((v) => v * 0.72);
            else if (d < 0.035) c = [196, 160, 96];
          }
      // goods places: a ring in each good's colour
      for (let g = 0; g < 5; g++) {
        const d = Math.hypot(ux - (4.75 + g * 1.05), uz - 1.45);
        if (d < 0.46) c = c.map((v) => v * 0.7);
        if (d > 0.4 && d < 0.47) c = hex(GOODS[g]);
      }
      // the coin place and the quay corner where townsfolk wait
      const dc = Math.hypot(ux - 10.25, uz - 1.45);
      if (dc < 0.42) c = c.map((v) => v * 0.7);
      if (dc > 0.36 && dc < 0.43) c = [214, 166, 64];
      if (inRect(ux, uz, 11.0, 0.5, 1.65, 1.95)) {
        const plank = Math.floor(uz / 0.22);
        c = [128, 96, 64].map((v) => v * (0.75 + 0.25 * hash2(plank, 1, 4)) * ((uz % 0.22) < 0.02 ? 0.5 : 1));
      }
      // the name plate's place (the HUD draws over it): darker
      if (rr(ux, uz, 0.35, 0.3, 3.9, 2.2, 0.2) < 0) c = c.map((v) => v * 0.6);
      const i = (y * W + x) * 4;
      px[i] = c[0]; px[i + 1] = c[1]; px[i + 2] = c[2]; px[i + 3] = 255;
    }
  }
  writePng('board.png', W, H, px);
}
