#!/usr/bin/env node
// Rose Window frames: the window patterns players choose from, generated here and written as a C table.
//
//   node games/rosewindow/art/make-frames.mjs      writes games/rosewindow/src/frames.c
//
// A frame is 4 rows of 5 cells. A cell is open (any die), a colour (only a die of that colour) or a number (only a
// die showing that number). Two cells side by side never ask for the same colour or the same number (no die could
// fill both). Every frame is checked to have at least one complete fill, and rated by how hard it is to fill: the
// share of random legal fills that dead-end (a cell no die can take), from 3 (gentle) to 6 (demanding). The rating
// is also the favour a player who picks it starts with.
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'src', 'frames.c');
const ROWS = 4, COLS = 5, COLOURS = 5;

function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (s + 0x6d2b79f5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

// cell codes as the C side reads them: 0 open, 1..5 colour (code - 1), 11..16 number (code - 10)
const isColour = (c) => c >= 1 && c <= 5;
const isValue = (c) => c >= 11 && c <= 16;

function neighbours(i) {
  const r = Math.floor(i / COLS), c = i % COLS, out = [];
  if (r > 0) out.push(i - COLS);
  if (r < ROWS - 1) out.push(i + COLS);
  if (c > 0) out.push(i - 1);
  if (c < COLS - 1) out.push(i + 1);
  return out;
}

function makeFrame(rand, restricted) {
  for (let tries = 0; tries < 2000; tries++) {
    const cell = new Array(ROWS * COLS).fill(0);
    const order = [...cell.keys()].sort(() => rand() - 0.5);
    let ok = true;
    for (const i of order.slice(0, restricted)) {
      const options = [];
      for (let k = 1; k <= 5; k++) options.push(k);
      for (let k = 11; k <= 16; k++) options.push(k);
      const legal = options.filter((o) => neighbours(i).every((n) => cell[n] !== o));
      if (!legal.length) { ok = false; break; }
      // colours a little more often than numbers: the windows read as glass first
      const pool = legal.flatMap((o) => (isColour(o) ? [o, o] : [o]));
      cell[i] = pool[Math.floor(rand() * pool.length)];
    }
    if (ok) return cell;
  }
  throw new Error('no frame');
}

const allows = (code, colour, value) => code === 0 || (isColour(code) && code - 1 === colour) || (isValue(code) && code - 10 === value);

// one random fill in reading order (dice placed anywhere, so adjacency to placed dice is not the constraint here):
// true when every cell got a die
function randomFill(cell, rand) {
  const die = new Array(ROWS * COLS).fill(null);
  for (let i = 0; i < ROWS * COLS; i++) {
    const legal = [];
    for (let c = 0; c < COLOURS; c++)
      for (let v = 1; v <= 6; v++) {
        if (!allows(cell[i], c, v)) continue;
        if (neighbours(i).some((n) => die[n] && (die[n].c === c || die[n].v === v))) continue;
        legal.push({ c, v });
      }
    if (!legal.length) return false;
    die[i] = legal[Math.floor(rand() * legal.length)];
  }
  return true;
}

// a full fill exists (backtracking)
function solvable(cell) {
  const die = new Array(ROWS * COLS).fill(null);
  const go = (i) => {
    if (i === ROWS * COLS) return true;
    for (let c = 0; c < COLOURS; c++)
      for (let v = 1; v <= 6; v++) {
        if (!allows(cell[i], c, v)) continue;
        if (neighbours(i).some((n) => die[n] && (die[n].c === c || die[n].v === v))) continue;
        die[i] = { c, v };
        if (go(i + 1)) return true;
        die[i] = null;
      }
    return false;
  };
  return go(0);
}

// our own names: birds, plants, lights and quiet places
const NAMES = ['Lantern', 'Orchard', 'Harbour Light', 'Ember', 'Tidewater', 'Meadow', 'Cinder', 'Lattice', 'Foxglove',
  'Cloister', 'Bellframe', 'Kingfisher', 'Thistle', 'Dovecote', 'Saffron', 'Juniper', 'Marigold', 'Nightjar', 'Willow',
  'Hearth', 'Riverstone', 'Copperleaf', 'Starling', 'Heron'];

const rand = rng(20261125);
// candidates with 8 to 14 marked cells, rated by their dead-end share: the 24 kept are spread evenly over the
// range and split into four even bands, 3 the gentlest
const cands = [];
for (let attempt = 0; cands.length < 120 && attempt < 20000; attempt++) {
  const restricted = 8 + Math.floor(rand() * 7);
  const cell = makeFrame(rand, restricted);
  if (!solvable(cell)) continue;
  const fr = rng(attempt * 7919 + 3);
  let dead = 0;
  const N = 400;
  for (let k = 0; k < N; k++) if (!randomFill(cell, fr)) dead++;
  cands.push({ cell, share: dead / N, restricted });
}
cands.sort((x, y) => x.share - y.share);
const frames = [];
for (let i = 0; i < NAMES.length; i++) frames.push(cands[Math.round((i * (cands.length - 1)) / (NAMES.length - 1))]);
const got = { 3: 0, 4: 0, 5: 0, 6: 0 };
frames.forEach((f, i) => { f.rating = 3 + Math.floor((i * 4) / NAMES.length); got[f.rating]++; });
frames.forEach((f, i) => { f.name = NAMES[i]; });

let c = `/* Rose Window frames: generated by games/rosewindow/art/make-frames.mjs (do not edit; run the script).
 * Cells in reading order, 4 rows of 5: 0 open, 1..5 a colour (code - 1), 11..16 a number (code - 10).
 * rating (3..6) is how hard the frame is to fill, and the favour its player starts with. */
#include "rules.h"

const rw_frame rw_frames[RW_FRAMES] = {
`;
for (const f of frames)
  c += `  { "${f.name}", ${f.rating}, { ${f.cell.join(', ')} } }, /* ${f.restricted} marked cells, ${Math.round(f.share * 100)}% of random fills dead-end */\n`;
c += '};\n';
writeFileSync(OUT, c);
console.log(`frames.c: ${frames.length} frames, ratings ${JSON.stringify(got)}`);
