#!/usr/bin/env node
// Kilowatt Country art: every texture the game loads, drawn here from shapes and noise (Node only). The night map
// uses the same coastline and regions tools/gen-data.mjs placed the cities by.
//
//   node games/kilowattcountry/tools/make-textures.mjs      writes games/kilowattcountry/assets/textures/*.png
//
// map.png      the country at night: dark land with relief, a faint glow along the coast, dashed region borders,
//              a navy sea with a chart grid (2048 x 1366, the board's 3:2 top face; the image's top is the north edge)
// region-N.png one per region: where it lies (alpha), laid dark over a region out of play
// table.png    dark oiled wood planks (tiles)
// glow.png     a soft round light (the glow under a powered city)
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { land, regionOf, MAP_W } from './gen-data.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'textures');
mkdirSync(OUT, { recursive: true });
const MAP = JSON.parse((await import('node:fs')).readFileSync(join(HERE, '..', 'data', 'map.json'), 'utf8'));

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
// colours are painted in linear light and stored as sRGB
const srgb2lin = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
const lin2srgb = (c) => { c = Math.max(0, Math.min(1, c)); return c <= 0.0031308 ? c * 12.92 : 1.055 * c ** (1 / 2.4) - 0.055; };
const hex = (h) => [srgb2lin(((h >> 16) & 255) / 255), srgb2lin(((h >> 8) & 255) / 255), srgb2lin((h & 255) / 255)];
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
function writePng(name, w, h, px) { // px: Float32Array rgba, linear rgb, straight alpha
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    for (let i = 0; i < w * 4; i++) {
      const v = px[y * w * 4 + i];
      raw[y * (w * 4 + 1) + 1 + i] = Math.round(clamp((i & 3) === 3 ? v : lin2srgb(v), 0, 1) * 255);
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
  writeFileSync(join(OUT, name), png);
  writeFileSync(join(OUT, `${name}.cwimport`), 'texture.format = ktx2\ntexture.mode = uastc\ntexture.color = srgb\n');
  console.log(`${name} ${w}x${h} ${png.length} bytes`);
}

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

// ── the night map ───────────────────────────────────────────────────
const REGION_TINT = [0x2a2838, 0x33291f, 0x1c2e33, 0x1f3024, 0x332e1c].map(hex);
// the bisector distance to the nearest other region (map units): how far inside its region a point lies
function regionDepth(X, Z) {
  const seeds = MAP.regions.map((r) => r.seed);
  const r = regionOf(X, Z);
  let best = 1e9;
  seeds.forEach((s, i) => {
    if (i === r) return;
    const a = seeds[r], dx = s[0] - a[0], dz = s[1] - a[1], len = Math.hypot(dx, dz);
    const d = ((X - (a[0] + s[0]) / 2) * dx + (Z - (a[1] + s[1]) / 2) * dz) / len;
    best = Math.min(best, -d);
  });
  return { r, d: best };
}
function nightMap() {
  const W = 2048, H = 1366, px = new Float32Array(W * H * 4);
  const sea = hex(0x06101d), seaNear = hex(0x0c2134), grid = hex(0x13294a), landLo = hex(0x111a18), landHi = hex(0x26352e);
  const coast = hex(0x4f7d8c), border = hex(0x6a7c8c);
  const upx = MAP_W / W; // map units per pixel
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const X = (x + 0.5) * upx, Z = (y + 0.5) / H;
      const L = land(X, Z);
      const gx = (land(X + upx, Z) - land(X - upx, Z)) / 2, gz = (land(X, Z + upx) - land(X, Z - upx)) / 2;
      const dpx = L / Math.max(1e-6, Math.hypot(gx, gz)); // signed distance to the coast in pixels (land > 0)
      let c;
      if (dpx < 0) {
        const near = Math.exp(dpx / 40);
        c = mix(sea, seaNear, near * 0.8);
        c = c.map((v) => v * (0.92 + fbm(X * 40, Z * 40, 3, 3) * 0.16));
        // a chart grid: thin lines every 1/12 of the width, fading toward the coast
        const gxv = Math.abs(((X / MAP_W) * 12) % 1 - 0.5) * (W / 12), gzv = Math.abs(((Z) * 8) % 1 - 0.5) * (H / 8);
        const gl = Math.max(smooth(W / 24 - 1.2, W / 24, gxv), smooth(H / 16 - 1.2, H / 16, gzv));
        c = mix(c, grid, gl * 0.55 * (1 - near));
        c = mix(c, coast, Math.exp(dpx / 5) * 0.35); // the coast's faint glow
      } else {
        const h = fbm(X * 7, Z * 7, 11, 5), hx = fbm(X * 7 + 0.01, Z * 7, 11, 5) - h, hz = fbm(X * 7, Z * 7 + 0.01, 11, 5) - h;
        const lit = clamp(0.8 + (-hx * 0.7 - hz * 0.5) * 60, 0.45, 1.35); // relief lit from the north west
        const { r, d } = regionDepth(X, Z);
        c = mix(landLo, landHi, smooth(0.3, 0.75, h)).map((v) => v * lit);
        c = mix(c, REGION_TINT[r], 0.38);
        c = c.map((v) => v * (0.9 + fbm(X * 60, Z * 60, 5, 2) * 0.2));
        // region borders: a dashed line on the bisector
        const bpx = d / upx;
        const dash = (Math.floor((x + y) / 9) & 1) === 0;
        if (dash) c = mix(c, border, (1 - smooth(0.6, 1.8, Math.abs(bpx))) * 0.8);
        c = mix(c, coast, (1 - smooth(0.5, 2.5, dpx)) * 0.9); // the coastline
      }
      const i = (y * W + x) * 4;
      px[i] = c[0], px[i + 1] = c[1], px[i + 2] = c[2], px[i + 3] = 1;
    }
  rivers(px, W, H);
  writePng('map.png', W, H, px);
}


// rivers: from inland springs downhill (toward the coast) with a meander, drawn over the land
function rivers(px, W, H) {
  const upx = MAP_W / W, river = hex(0x123a5a), glowc = hex(0x2a5a7a);
  const springs = [[0.6, 0.42], [0.98, 0.46], [0.5, 0.62], [0.86, 0.6]];
  springs.forEach(([sx, sz], ri) => {
    const pts = [[sx, sz]];
    let x = sx, z = sz, a = 0;
    const out = Math.atan2(sz - 0.5, sx - 0.75);
    for (let i = 0; i < 400 && land(x, z) > -0.02; i++) {
      const ang = out + (vnoise(i * 0.09, ri * 7, 31) - 0.5) * 1.6 + Math.sin(i * 0.35 + ri) * 0.35;
      x += Math.cos(ang) * 0.006, z += Math.sin(ang) * 0.006;
      pts.push([x, z]);
    }
    for (let k = 1; k < pts.length; k++) {
      const [ax, az] = pts[k - 1], [bx, bz] = pts[k];
      const w = 1.0 + 2.0 * (k / pts.length); // widens toward the sea
      const x0 = Math.floor(Math.min(ax, bx) / upx - 8), x1 = Math.ceil(Math.max(ax, bx) / upx + 8);
      const y0 = Math.floor(Math.min(az, bz) * H - 8), y1 = Math.ceil(Math.max(az, bz) * H + 8);
      for (let y = Math.max(0, y0); y < Math.min(H, y1); y++)
        for (let xx = Math.max(0, x0); xx < Math.min(W, x1); xx++) {
          const X = (xx + 0.5) * upx, Z = (y + 0.5) / H;
          const vx = bx - ax, vz = bz - az, t = clamp(((X - ax) * vx + (Z - az) * vz) / (vx * vx + vz * vz || 1), 0, 1);
          const d = Math.hypot(X - ax - vx * t, Z - az - vz * t) / upx;
          const i = (y * W + xx) * 4;
          const k1 = 1 - smooth(w * 0.5, w * 0.5 + 1, d), k2 = (1 - smooth(w, w + 5, d)) * 0.25;
          for (let c = 0; c < 3; c++) px[i + c] = px[i + c] + (glowc[c] - px[i + c]) * k2;
          for (let c = 0; c < 3; c++) px[i + c] = px[i + c] + (river[c] - px[i + c]) * k1;
        }
    }
  });
}

// one mask per region: dark, hatched, soft at the border; drawn over a region out of play
function regionMasks() {
  const W = 512, H = 342;
  for (let r = 0; r < MAP.regions.length; r++) {
    const px = new Float32Array(W * H * 4);
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const X = (x + 0.5) / W * MAP_W, Z = (y + 0.5) / H;
        const rd = regionDepth(X, Z);
        let a = rd.r === r ? smooth(-0.004, 0.012, rd.d) : 0;
        a *= smooth(-0.04, 0.02, land(X, Z) / 3); // the land, and a little of the sea beside it
        const hatch = ((x - y) % 12 + 12) % 12 < 2 ? 0.08 : 0;
        const i = (y * W + x) * 4;
        px[i] = 0.004, px[i + 1] = 0.006, px[i + 2] = 0.012, px[i + 3] = a * (0.8 - hatch);
      }
    writePng(`region-${r}.png`, W, H, px);
  }
}

function table() {
  const S = 512, px = new Float32Array(S * S * 4);
  for (let y = 0; y < S; y++)
    for (let x = 0; x < S; x++) {
      const plank = Math.floor(x / 128), u = x / S, v = y / S;
      const g = fbm(u * 3 + plank * 7.1, v * 40, 5, 5), ring = Math.sin(v * 60 + g * 9 + plank * 3) * 0.5 + 0.5;
      let c = mix(hex(0x1e140d), hex(0x3d2818), 0.3 + ring * 0.35 + (g - 0.5) * 0.6);
      if (x % 128 < 2 || x % 128 > 125) c = c.map((t) => t * 0.45);
      const i = (y * S + x) * 4;
      px[i] = c[0], px[i + 1] = c[1], px[i + 2] = c[2], px[i + 3] = 1;
    }
  writePng('table.png', S, S, px);
}

function glow() {
  const S = 128, px = new Float32Array(S * S * 4);
  for (let y = 0; y < S; y++)
    for (let x = 0; x < S; x++) {
      const r = Math.hypot(x + 0.5 - S / 2, y + 0.5 - S / 2) / (S / 2);
      const a = Math.max(0, 1 - r) ** 2.2;
      const i = (y * S + x) * 4;
      px[i] = 1, px[i + 1] = 1, px[i + 2] = 1, px[i + 3] = a;
    }
  writePng('glow.png', S, S, px);
}

nightMap();
regionMasks();
table();
glow();
