#!/usr/bin/env node
// Smalti art: every texture the game loads, drawn here (no inputs, no dependencies beyond Node).
//
//   node games/smalti/art/make-art.mjs      writes games/smalti/assets/textures/*.png
//
// The board layout numbers (BOARD below) are the same ones src/smalti_view.c places pieces with.
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

// ── the glass colours (src/smalti_view.c keeps the same order: cobalt, amber, coral, jade, pearl) ──
export const COLOURS = [
  { name: 'cobalt', base: hex('#1d56d6'), deep: hex('#0a2a7a'), lite: hex('#8cc0ff') },
  { name: 'amber', base: hex('#f2a810'), deep: hex('#b86a04'), lite: hex('#ffe6a0') },
  { name: 'coral', base: hex('#e03a2c'), deep: hex('#8c1410'), lite: hex('#ffb09a') },
  { name: 'jade', base: hex('#10a874'), deep: hex('#05553a'), lite: hex('#94f2cc') },
  { name: 'pearl', base: hex('#e9e4da'), deep: hex('#9f988d'), lite: hex('#ffffff') },
];

// motif masks in tile space u,v in [-1,1]: 1 inside the raised gold-free relief, 0 outside. Each colour has its own
// shape so the colours read without colour too.
function motif(k, u, v) {
  const r = Math.hypot(u, v);
  const line = (d, w) => smooth(w, w * 0.45, Math.abs(d));
  switch (k) {
    case 0: { // cobalt: three waves
      let m = 0;
      for (const o of [-0.42, 0, 0.42]) m = Math.max(m, line(v - o - 0.11 * Math.sin(u * 5.2), 0.075));
      return m * smooth(0.82, 0.7, Math.abs(u));
    }
    case 1: { // amber: a sun, a disc and eight rays
      const a = Math.atan2(v, u);
      const disc = smooth(0.3, 0.26, r);
      const ray = smooth(0.12, 0.05, Math.abs(Math.sin(a * 4)) * r) * smooth(0.4, 0.44, r) * smooth(0.74, 0.68, r);
      return Math.max(disc, ray);
    }
    case 2: { // coral: a diamond with a diamond inside
      const d = Math.abs(u) + Math.abs(v);
      return Math.max(line(d - 0.62, 0.08), smooth(0.2, 0.15, d));
    }
    case 3: { // jade: a leaf with a midrib
      const uu = (u - v) * 0.7071, vv = (u + v) * 0.7071;
      const leaf = Math.hypot(uu, vv * 1.0) < 2 && Math.hypot(uu, Math.abs(vv) + 0.45) < 0.88 ? 1 : 0;
      const edge = Math.hypot(uu, Math.abs(vv) + 0.45);
      const shape = smooth(0.9, 0.84, edge);
      const rib = line(vv, 0.05) * smooth(0.7, 0.6, Math.abs(uu));
      return Math.max(shape * 0.55, rib) * (leaf || shape);
    }
    default: { // pearl: two rings
      return Math.max(line(r - 0.6, 0.06), line(r - 0.32, 0.06));
    }
  }
}

// ── tiles: albedo + height (normal) per colour, 128 px ─────────────
function tile(k) {
  const N = 128, c = COLOURS[k];
  const px = new Float32Array(N * N * 4), nrm = new Float32Array(N * N * 4);
  const height = new Float32Array(N * N);
  const R = rng(1000 + k);
  const bubbles = Array.from({ length: 9 }, () => [R() * 2 - 1, R() * 2 - 1, 0.025 + R() * 0.05]);
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const u = (x + 0.5) / N * 2 - 1, v = (y + 0.5) / N * 2 - 1;
      const n = fbm(x / 16, y / 16, 8, 40 + k * 7, 4);
      const streak = fbm(x / 40 + n * 0.8, y / 6, 4, 90 + k, 3); // pulled glass: long streaks
      let t = clamp(0.42 + (n - 0.5) * 0.55 + (streak - 0.5) * 0.3 - 0.18 * (u * u + v * v));
      const m = motif(k, u, v);
      let col = [0, 1, 2].map((i) => mix(c.deep[i], c.base[i], smooth(0.15, 0.75, t)));
      // light through the glass: the middle glows toward the light colour, the rim stays deep (a thick edge)
      const edgeD = Math.max(Math.abs(u), Math.abs(v));
      const glow = smooth(0.95, 0.15, edgeD) * 0.24;
      // and a thin bright line just inside the edge, where a polished bevel catches the light
      const bevel = smooth(0.86, 0.92, edgeD) * smooth(1.0, 0.95, edgeD) * 0.2;
      col = col.map((ch, i) => mix(ch, c.lite[i], glow + bevel));
      col = col.map((ch, i) => mix(ch, c.lite[i], 0.3 * m)); // the relief catches more light
      let h = n * 0.18 + m * 0.8;
      for (const [bx, by, br] of bubbles) {
        const d = Math.hypot(u - bx, v - by) / br;
        if (d < 1) { col = col.map((ch, i) => mix(ch, c.lite[i], 0.35 * (1 - d) * (1 - d))); h -= 0.25 * (1 - d * d); }
      }
      if (k === 4) { // pearl: a faint mother-of-pearl sheen
        const s = fbm(x / 22, y / 22, 6, 777, 3);
        col = [col[0] + 5 * Math.sin(s * 6), col[1] + 4 * Math.sin(s * 6 + 2), col[2] + 6 * Math.sin(s * 6 + 4)];
      }
      const o = (y * N + x) * 4;
      px[o] = col[0]; px[o + 1] = col[1]; px[o + 2] = col[2]; px[o + 3] = 255;
      height[y * N + x] = h;
    }
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const H = (xx, yy) => height[((yy + N) % N) * N + ((xx + N) % N)];
      const dx = (H(x + 1, y) - H(x - 1, y)) * 3.0, dy = (H(x, y + 1) - H(x, y - 1)) * 3.0;
      const l = Math.hypot(dx, dy, 1);
      const o = (y * N + x) * 4;
      nrm[o] = (-dx / l * 0.5 + 0.5) * 255; nrm[o + 1] = (-dy / l * 0.5 + 0.5) * 255; nrm[o + 2] = (1 / l * 0.5 + 0.5) * 255; nrm[o + 3] = 255;
    }
  writePng(`tile_${c.name}.png`, N, N, px);
  writePng(`tile_${c.name}_n.png`, N, N, nrm);
}

// ── the player board ────────────────────────────────────────────────
// Board units: a cell is 1. Staging rows on the left, right-aligned staircase; the mosaic on the right; the
// breakage strip along the bottom. src/smalti_view.c uses the same numbers.
export const BOARD = { W: 11.6, H: 7.4, margin: 0.45, gap: 0.7, stripY: 5.85, cell: 1 };
// the mosaic: column = (colour + 2 * row) % 5
const wallColour = (row, col) => (((col - 2 * row) % 5) + 10) % 5;

function board() {
  const PPU = 96, W = Math.round(BOARD.W * PPU), H = Math.round(BOARD.H * PPU);
  const px = new Float32Array(W * H * 4);
  const plaster = hex('#cdbfa2'), plasterDeep = hex('#a8977a'), gold = hex('#c99a3a'), goldLite = hex('#f3d27a');
  const slate = hex('#3c3a3f');
  const m = BOARD.margin, wallX = m + 5 + BOARD.gap;
  const set = (x, y, c, a = 1) => { const o = (y * W + x) * 4; for (let i = 0; i < 3; i++) px[o + i] = mix(px[o + i], c[i], a); };
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const n = fbm(x / 40, y / 40, 64, 5, 5), fine = fbm(x / 6, y / 6, 256, 9, 3);
      const t = clamp(0.55 + (n - 0.5) * 0.9 + (fine - 0.5) * 0.25);
      const o = (y * W + x) * 4;
      for (let i = 0; i < 3; i++) px[o + i] = mix(plasterDeep[i], plaster[i], t);
      px[o + 3] = 255;
    }
  // gold-leaf frame and a slim inner rule
  const rect = (x0, y0, x1, y1, w, c, a) => {
    for (let y = Math.round(y0 * PPU); y < Math.round(y1 * PPU); y++)
      for (let x = Math.round(x0 * PPU); x < Math.round(x1 * PPU); x++) {
        const dx = Math.min(x - x0 * PPU, x1 * PPU - 1 - x), dy = Math.min(y - y0 * PPU, y1 * PPU - 1 - y);
        const d = Math.min(dx, dy);
        if (d < w) set(x, y, c, a * smooth(w, w - 1.5, d));
      }
  };
  const fill = (x0, y0, x1, y1, f) => {
    for (let y = Math.max(0, Math.round(y0 * PPU)); y < Math.min(H, Math.round(y1 * PPU)); y++)
      for (let x = Math.max(0, Math.round(x0 * PPU)); x < Math.min(W, Math.round(x1 * PPU)); x++) f(x, y, (x / PPU - x0) / (x1 - x0), (y / PPU - y0) / (y1 - y0));
  };
  const goldAt = (x, y) => { const g = fbm(x / 5, y / 5, 512, 33, 2); return [0, 1, 2].map((i) => mix(gold[i], goldLite[i], g)); };
  // outer frame
  fill(0, 0, BOARD.W, BOARD.H, (x, y) => {
    const d = Math.min(x, y, W - 1 - x, H - 1 - y);
    if (d < 14) set(x, y, goldAt(x, y), smooth(14, 11, d) * 0.95);
    else if (d < 18) set(x, y, slate, 0.25);
  });
  // a cell: recessed, a soft shadow at the top-left edge
  const cellBox = (cx, cy, tint, alpha, motifK) => {
    const s = 0.9, x0 = cx + 0.05, y0 = cy + 0.05;
    fill(x0, y0, x0 + s, y0 + s, (x, y, fu, fv) => {
      const u = fu * 2 - 1, v = fv * 2 - 1;
      const edge = Math.max(Math.abs(u), Math.abs(v));
      let c = [0, 1, 2].map((i) => mix(plasterDeep[i] * 0.86, tint ? tint[i] : plasterDeep[i] * 0.8, tint ? alpha : 0));
      set(x, y, c, 0.8);
      if (motifK !== undefined) {
        const mm = motif(motifK, u * 1.15, v * 1.15);
        if (mm > 0) set(x, y, tint, 0.5 * mm);
      }
      if (edge > 0.9) set(x, y, goldAt(x, y), smooth(0.9, 0.96, edge) * 0.9);
      if (u < -0.8 || v < -0.8) set(x, y, [40, 32, 24], 0.18 * smooth(-0.8, -0.95, Math.min(u, v)));
    });
  };
  // staging rows: row r has r+1 cells, right-aligned against the gap
  for (let r = 0; r < 5; r++)
    for (let k = 0; k <= r; k++) cellBox(m + 4 - k, m + r, null, 0);
  // the arrows from each staging row to the mosaic: three small gold dots
  for (let r = 0; r < 5; r++)
    for (let d = 0; d < 3; d++) {
      const cx = (m + 5 + 0.15 + d * 0.2) * PPU, cy = (m + r + 0.5) * PPU, rad = 4 - d * 0.6;
      fill(cx / PPU - 0.06, cy / PPU - 0.06, cx / PPU + 0.06, cy / PPU + 0.06, (x, y) => {
        const dd = Math.hypot(x - cx, y - cy);
        if (dd < rad) set(x, y, goldAt(x, y), smooth(rad, rad - 1.2, dd));
      });
    }
  // the mosaic: each cell tinted faintly with its colour and its motif
  for (let r = 0; r < 5; r++)
    for (let c = 0; c < 5; c++) {
      const k = wallColour(r, c);
      cellBox(wallX + c, m + r, COLOURS[k].base, 0.4, k);
    }
  // breakage strip: seven cells (the game writes each cell's penalty under it)
  for (let i = 0; i < 7; i++) {
    const x0 = m + i * 1.02, y0 = BOARD.stripY;
    cellBox(x0, y0, hex('#6b2a22'), 0.22);
  }
  // a slim gold rule between the staging area and the strip
  fill(m, BOARD.stripY - 0.22, BOARD.W - m, BOARD.stripY - 0.18, (x, y) => set(x, y, goldAt(x, y), 0.8));
  writePng('board.png', W, H, px);
}

// ── the table: walnut, tileable ────────────────────────────────────
function walnut() {
  // four planks across, the grain running along them: long fine streaks, a slow figure, a dark seam between planks
  const N = 1024, px = new Float32Array(N * N * 4);
  const dark = hex("#24140c"), mid = hex("#4e2e1a"), lite = hex("#7a4e2e");
  const planks = 4, ph = N / planks;
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const p = Math.floor(y / ph), py = (y % ph) / ph;
      const seed = 50 + p * 13;
      const figure = fbm(x / 256, y / 64, 4, seed, 3);
      const streak = fbm(x / 128, y / 1.6 + figure * 6, 8, seed + 1, 4);
      const fine = fbm(x / 40, y / 0.9, 26, seed + 2, 2);
      let t = clamp(0.45 + (streak - 0.5) * 0.9 + (fine - 0.5) * 0.35 + (figure - 0.5) * 0.5);
      const seam = Math.min(py, 1 - py) * ph;
      const c = t < 0.5 ? [0, 1, 2].map((i) => mix(dark[i], mid[i], t * 2)) : [0, 1, 2].map((i) => mix(mid[i], lite[i], (t - 0.5) * 2));
      const k = seam < 2 ? 0.45 : 1;
      const o = (y * N + x) * 4;
      px[o] = c[0] * k; px[o + 1] = c[1] * k; px[o + 2] = c[2] * k; px[o + 3] = 255;
    }
  writePng("walnut.png", N, N, px);
}

// ── the runner: a matte woven linen, tileable ──────────────────────
function linen() {
  const N = 256, px = new Float32Array(N * N * 4);
  const base = hex('#1d4a4f'), lite = hex('#2f6a6e'), deep = hex('#123236');
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const warp = 0.5 + 0.5 * Math.sin(x * Math.PI * 2 / 4), weft = 0.5 + 0.5 * Math.sin(y * Math.PI * 2 / 4);
      const over = ((x >> 2) + (y >> 2)) & 1 ? warp : weft;
      const slub = fbm(x / 8, y / 64, 32, 21, 3), slub2 = fbm(x / 64, y / 8, 4, 22, 3);
      const t = clamp(0.35 + over * 0.35 + (slub - 0.5) * 0.4 + (slub2 - 0.5) * 0.3);
      const c = [0, 1, 2].map((i) => (t < 0.5 ? mix(deep[i], base[i], t * 2) : mix(base[i], lite[i], (t - 0.5) * 2)));
      const o = (y * N + x) * 4;
      px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = 255;
    }
  writePng('linen.png', N, N, px);
}

// ── a tray: a glazed terracotta coaster seen from above ─────────────
function tray() {
  const N = 256, px = new Float32Array(N * N * 4);
  const clay = hex('#a8552e'), clayDeep = hex('#6e321a'), glaze = hex('#e9dcc0'), glazeBlue = hex('#2a5a9a');
  for (let y = 0; y < N; y++)
    for (let x = 0; x < N; x++) {
      const u = (x + 0.5) / N * 2 - 1, v = (y + 0.5) / N * 2 - 1, r = Math.hypot(u, v), a = Math.atan2(v, u);
      const n = fbm(x / 12, y / 12, 21, 31, 4);
      let c = [0, 1, 2].map((i) => mix(clayDeep[i], clay[i], clamp(0.4 + (n - 0.5) * 1.2)));
      // a cream glaze ring with a blue scallop band
      const ring = smooth(0.66, 0.68, r) * smooth(0.95, 0.93, r);
      c = c.map((ch, i) => mix(ch, glaze[i], ring * 0.9));
      const scallop = 0.8 + 0.04 * Math.cos(a * 16);
      const band = smooth(0.025, 0.012, Math.abs(r - scallop));
      c = c.map((ch, i) => mix(ch, glazeBlue[i], band * 0.9));
      const dots = smooth(0.03, 0.018, Math.hypot(r - 0.885, Math.sin(a * 8) * 0.885 / 8 * 2)) * (Math.cos(a * 8) > 0 ? 1 : 0);
      c = c.map((ch, i) => mix(ch, glazeBlue[i], dots * 0.85));
      const o = (y * N + x) * 4;
      px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = 255;
    }
  writePng('tray.png', N, N, px);
}

for (let k = 0; k < 5; k++) tile(k);
board();
walnut();
linen();
tray();
