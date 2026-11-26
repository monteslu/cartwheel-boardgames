#!/usr/bin/env node
// Kilowatt Country art: every texture the game loads, drawn here from the maps (data/maps.json, made by
// tools/gen-data.mjs), Natural Earth's land, borders, lakes, rivers and state lines (public domain), shapes and noise
// (Node only).
//
//   node games/kilowattcountry/tools/make-textures.mjs      writes games/kilowattcountry/assets/textures/*.png
//
// map-<key>.png        each map at night (2048 x 1366, the board's 3:2 top face; the image's top is the north edge):
//                      the map's own countries lit by region, the neighbours dark, the sea navy with a faint
//                      graticule, a glow along the coast, country borders, state lines where the map has them, rivers
//                      and lakes, and dashed borders between the game's regions
// region-<key>-N.png   one per region: where it lies (alpha), laid dark over a region out of play
// table.png            dark oiled wood planks (tiles)
// glow.png             a soft round light (the glow under a powered city)
import { deflateSync } from 'node:zlib';
import { mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { edgeDistance, fillPolygons, lineDistance, lines, loadData, seedDistance } from '../../common/geo/geo.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'textures');
mkdirSync(OUT, { recursive: true });
const DATA = JSON.parse(readFileSync(join(HERE, '..', 'data', 'maps.json'), 'utf8'));
const MAP_W = DATA.aspect;
const ONLY = process.argv[2]; /* one map's key: draw just that map (a quicker look) */
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

// ── the night maps ──────────────────────────────────────────────────
const REGION_TINT = [0x2a2838, 0x33291f, 0x1c2e33, 0x1f3024, 0x332e1c].map(hex);
const COUNTRIES = loadData('countries50'), LAKES = loadData('lakes50'), RIVERS = loadData('rivers50'), STATES = loadData('states50');
const iso = (f) => f.properties.ISO_A2_EH;

/* a map's rasters at W x H: which country (index into COUNTRIES, -1 sea), lakes, the region of each home pixel (its
 * nearest city's), and distances to the coast, the borders and the region borders */
function rasters(m, W, H) {
  const { lonm, latm, k, scale } = m.proj;
  const toPx = (lon, lat) => [((MAP_W / 2 + (lon - lonm) * k * scale) / MAP_W) * W, (0.5 - (lat - latm) * scale) * H];
  const toLL = (x, y) => [lonm + ((x / W) * MAP_W - MAP_W / 2) / scale / k, latm - (y / H - 0.5) / scale];
  const country = fillPolygons(W, H, COUNTRIES, toPx, (f) => COUNTRIES.features.indexOf(f));
  const lake = fillPolygons(W, H, LAKES, toPx, (f) => (f.properties.scalerank <= 5 ? 1 : -1));
  const N = W * H, land = new Int8Array(N), home = new Int8Array(N), region = new Int8Array(N).fill(-1);
  const homeIds = new Set(COUNTRIES.features.map((f, i) => (m.cc.includes(iso(f)) ? i : -1)).filter((i) => i >= 0));
  const cities = m.cities.map((c) => [(c.x / MAP_W) * W, c.z * H, c.region]);
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const i = y * W + x;
      land[i] = country[i] >= 0 && lake[i] < 0 ? 1 : 0;
      home[i] = land[i] && homeIds.has(country[i]) ? 1 : 0;
      if (!home[i]) continue;
      let bd = 1e18, br = -1;
      for (const [cx, cy, r] of cities) { const d = (cx - x - 0.5) ** 2 + (cy - y - 0.5) ** 2; if (d < bd) bd = d, br = r; }
      region[i] = br;
    }
  const coast = edgeDistance(W, H, land, 48);
  const landId = new Int16Array(N);
  for (let i = 0; i < N; i++) landId[i] = land[i] ? country[i] : -1;
  const border = edgeDistance(W, H, landId, 16, (p, q) => p >= 0 && q >= 0);
  const regionEdge = edgeDistance(W, H, region, 16, (p, q) => p >= 0 && q >= 0);
  return { W, H, toPx, toLL, land, home, region, coast, border, regionEdge, lake };
}

function nightMap(m) {
  const W = 2048, H = 1366, R = rasters(m, W, H), px = new Float32Array(W * H * 4);
  const sea = hex(0x06101d), seaNear = hex(0x0c2134), grid = hex(0x13294a), landLo = hex(0x111a18), landHi = hex(0x26352e);
  const foreign = hex(0x0d1316), coastc = hex(0x4f7d8c), borderc = hex(0x8696a4), regionc = hex(0x6a7c8c), lakec = hex(0x0a1a2c);
  const statec = hex(0x3a4c58), riverc = hex(0x1a4664);
  const pxPerDeg = (m.proj.scale / MAP_W) * W; /* pixels per degree of latitude */
  const states = m.states ? lineDistance(W, H, lines(STATES, (f) => f.properties.ADM0_A3 === m.states || (m.cc.includes('CA') && f.properties.ADM0_A3 === 'CAN')).map((l) => l.map(([lon, lat]) => R.toPx(lon, lat))), 3) : null;
  const rivers = lineDistance(W, H, lines(RIVERS, (f) => f.properties.scalerank <= 6).map((l) => l.map(([lon, lat]) => R.toPx(lon, lat))), 4);
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const i = y * W + x, X = ((x + 0.5) / W) * MAP_W, Z = (y + 0.5) / H;
      const [lon, lat] = R.toLL(x + 0.5, y + 0.5);
      const dc = R.coast[i];
      let c;
      if (!R.land[i]) {
        const near = Math.exp(-dc / 40);
        c = mix(R.lake[i] >= 0 ? lakec : sea, seaNear, near * 0.8);
        c = c.map((v) => v * (0.92 + fbm(X * 40, Z * 40, 3, 3) * 0.16));
        /* the graticule: a thin line every five degrees, fading toward the coast */
        const glon = Math.abs(lon / 5 - Math.round(lon / 5)) * 5 * pxPerDeg * m.proj.k, glat = Math.abs(lat / 5 - Math.round(lat / 5)) * 5 * pxPerDeg;
        const gl = 1 - smooth(0.4, 1.4, Math.min(glon, glat));
        c = mix(c, grid, gl * 0.55 * (1 - near));
        c = mix(c, coastc, Math.exp(-dc / 5) * 0.35); /* the coast's faint glow out to sea */
      } else if (R.home[i]) {
        const h = fbm(X * 7, Z * 7, 11, 5), hx = fbm(X * 7 + 0.01, Z * 7, 11, 5) - h, hz = fbm(X * 7, Z * 7 + 0.01, 11, 5) - h;
        const lit = clamp(0.85 + (-hx * 0.7 - hz * 0.5) * 40, 0.6, 1.2); /* a soft grain, lit from the north west */
        c = mix(landLo, landHi, smooth(0.3, 0.75, h)).map((v) => v * lit);
        c = mix(c, REGION_TINT[R.region[i]], 0.38);
        c = c.map((v) => v * (0.9 + fbm(X * 60, Z * 60, 5, 2) * 0.2));
        if (states) c = mix(c, statec, (1 - smooth(0.4, 1.3, states[i])) * 0.7);
        const dash = (Math.floor((x + y) / 9) & 1) === 0; /* the game's regions: a dashed line */
        if (dash) c = mix(c, regionc, (1 - smooth(0.6, 1.8, R.regionEdge[i])) * 0.85);
      } else {
        c = foreign.map((v) => v * (0.9 + fbm(X * 60, Z * 60, 5, 2) * 0.2));
      }
      if (R.land[i]) {
        c = mix(c, riverc, (1 - smooth(0.5, 1.4, rivers[i])) * (R.home[i] ? 0.8 : 0.45));
        c = mix(c, borderc, (1 - smooth(0.6, 1.7, R.border[i])) * (R.home[i] ? 0.85 : 0.5)); /* country borders */
        c = mix(c, coastc, (1 - smooth(0.5, 2.5, dc)) * (R.home[i] ? 0.9 : 0.55)); /* the coastline */
      }
      px[i * 4] = c[0], px[i * 4 + 1] = c[1], px[i * 4 + 2] = c[2], px[i * 4 + 3] = 1;
    }
  writePng(`map-${m.key}.png`, W, H, px);
}

/* one mask per region: dark, hatched, soft at its border; drawn over a region out of play */
function regionMasks(m) {
  const W = 512, H = 342, R = rasters(m, W, H);
  const near = seedDistance(W, H, (i) => R.home[i] === 1, 24); /* a little of the sea beside the land goes dark too */
  for (let r = 0; r < 5; r++) {
    /* each pixel near the home land takes the region of the nearest home pixel */
    const mine = seedDistance(W, H, (i) => R.region[i] === r, 24), other = seedDistance(W, H, (i) => R.region[i] >= 0 && R.region[i] !== r, 24);
    const px = new Float32Array(W * H * 4);
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const i = y * W + x;
        let a = smooth(-1.5, 1.5, other[i] - mine[i]) * (1 - smooth(3, 8, near[i]));
        const hatch = ((x - y) % 12 + 12) % 12 < 2 ? 0.08 : 0;
        px[i * 4] = 0.004, px[i * 4 + 1] = 0.006, px[i * 4 + 2] = 0.012, px[i * 4 + 3] = a * (0.8 - hatch);
      }
    writePng(`region-${m.key}-${r}.png`, W, H, px);
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

/* the old single-map files and the maps no longer made go */
for (const f of readdirSync(OUT))
  if (/^(map|region)[-.]/.test(f) && !DATA.maps.some((m) => f.startsWith(`map-${m.key}.`) || f.startsWith(`region-${m.key}-`))) if (!ONLY) rmSync(join(OUT, f));
for (const m of DATA.maps) {
  if (ONLY && m.key !== ONLY) continue;
  nightMap(m);
  regionMasks(m);
}
if (!ONLY) {
  table();
  glow();
}
