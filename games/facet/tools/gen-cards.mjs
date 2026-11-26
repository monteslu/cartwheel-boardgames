#!/usr/bin/env node
// Facet: generate the guild's card set (commissions and patrons) and check its balance.
//
// Every commission costs stones and pays a permanent bonus stone (a discount on later buys) plus points. Costs
// are not hand-written: a seeded search picks cost shapes per (tier, bonus colour, points) so that each card's
// effort (a model of how many turns its stones take to gather) sits on its tier's points curve, and every colour
// is asked for about as often as every other. The checks at the end fail the script (exit 1) when a card falls
// off the curve or the demand drifts; `--selftest` breaks a copy of the set on purpose and requires the checks
// to catch it (a check nothing can fail is not a check).
//
//   node tools/gen-cards.mjs            write src/cards_data.h and assets/cards.json
//   node tools/gen-cards.mjs --check    only check (no writes)
//   node tools/gen-cards.mjs --selftest the must-fail controls
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const COLORS = ['ruby', 'sapphire', 'emerald', 'amethyst', 'diamond'];
const NC = COLORS.length;

// ── the effort model ──────────────────────────────────────────────────
// Tokens of one colour are scarce (4 to 7 in the bank, two at a time at best), so a tall stack of one colour
// costs more than its count; a cost spread over many colours needs several different takes. Effort is in
// "stones gathered", the unit the tier curves use.
export function effort(cost) {
  let sum = 0, tall = 0, used = 0;
  for (const c of cost) {
    sum += c;
    if (c > 3) tall += (c - 3) * 0.55;
    if (c > 0) used++;
  }
  const spread = used > 2 ? (used - 2) * 0.45 : 0;
  return sum + tall + spread;
}

// points -> the effort a card of that tier and points should take (the curve), with the band a card may sit in
const CURVE = {
  1: { 0: 3.9, 1: 5.4 },
  2: { 1: 6.9, 2: 8.6, 3: 10.1 },
  3: { 3: 10.8, 4: 12.6, 5: 14.6 },
};
const BAND = 0.75;
// per tier: how many cards of each bonus colour, and the points they carry (same list for every colour)
const PLAN = {
  1: [0, 0, 0, 0, 0, 0, 0, 1],
  2: [1, 1, 2, 2, 2, 3],
  3: [3, 4, 4, 5],
};
// cost limits per tier: total stones, the tallest single colour, and how much of its own colour a card may ask
const LIMITS = {
  1: { min: 3, max: 6, tall: 5, own: 1 },
  2: { min: 5, max: 10, tall: 6, own: 3 },
  3: { min: 7, max: 15, tall: 7, own: 3 },
};

// names: each commission is a piece the guild cuts or sets. Our own word lists.
const PIECES = {
  1: ['Cabochon', 'Bead', 'Chip', 'Rondelle', 'Drop', 'Button', 'Stud', 'Pebble'],
  2: ['Signet', 'Pendant', 'Clasp', 'Brooch', 'Locket', 'Buckle'],
  3: ['Diadem', 'Scepter', 'Chalice', 'Mirror'],
};
const PATRON_FIRST = ['Ada', 'Bram', 'Cora', 'Desmond', 'Elsbeth', 'Florian', 'Greta', 'Hollis', 'Imre', 'Juno'];
const PATRON_TITLE = ['Harbor Warden', 'Glassmaker', 'Archivist', 'Vintner', 'Cartographer', 'Bellfounder',
  'Astronomer', 'Weaver', 'Ferryman', 'Apothecary'];

function rng(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function candidates(tier, color) {
  const L = LIMITS[tier];
  const out = [];
  const cost = [0, 0, 0, 0, 0];
  const rec = (i, sum) => {
    if (i === NC) {
      if (sum >= L.min && sum <= L.max) out.push(cost.slice());
      return;
    }
    const cap = i === color ? L.own : L.tall;
    for (let v = 0; v <= cap && sum + v <= L.max; v++) {
      cost[i] = v;
      rec(i + 1, sum + v);
    }
    cost[i] = 0;
  };
  rec(0, 0);
  return out;
}

export function generate(seed = 0x6a3e7) {
  const R = rng(seed);
  const cards = [];
  const demand = { 1: [0, 0, 0, 0, 0], 2: [0, 0, 0, 0, 0], 3: [0, 0, 0, 0, 0] };
  for (const tier of [1, 2, 3]) {
    const used = new Set();
    // round-robin over colours so demand balancing sees every colour as it goes
    for (let k = 0; k < PLAN[tier].length; k++) {
      for (let color = 0; color < NC; color++) {
        const pts = PLAN[tier][k];
        const target = CURVE[tier][pts];
        const pool = candidates(tier, color).filter(c => Math.abs(effort(c) - target) <= BAND * 0.6 && !used.has(c.join()));
        if (!pool.length) throw new Error(`no cost shape for tier ${tier} colour ${COLORS[color]} points ${pts}`);
        const d = demand[tier];
        const avg = d.reduce((a, b) => a + b, 0) / NC;
        let best = null, bestScore = Infinity;
        for (const c of pool) {
          // stay on the curve, ask for colours that are under-asked, and some seeded variety
          let score = Math.abs(effort(c) - target) * 2;
          for (let i = 0; i < NC; i++) score += c[i] * (d[i] - avg) * 0.08;
          score += R() * 1.2;
          if (score < bestScore) { bestScore = score; best = c; }
        }
        used.add(best.join());
        for (let i = 0; i < NC; i++) d[i] += best[i];
        const name = `${COLORS[color][0].toUpperCase()}${COLORS[color].slice(1)} ${PIECES[tier][k]}`;
        cards.push({ tier, bonus: color, points: pts, cost: best, name });
      }
    }
  }
  // patrons: three points each, met by owning bonuses (not stones). Shapes: two colours at 4, three at 3, or four
  // at 2. Every colour appears in about as many patrons as every other.
  const patrons = [];
  const shapes = [];
  for (let a = 0; a < NC; a++) for (let b = a + 1; b < NC; b++) shapes.push({ need: [[a, 4], [b, 4]] });
  for (let a = 0; a < NC; a++) for (let b = a + 1; b < NC; b++) for (let c = b + 1; c < NC; c++) shapes.push({ need: [[a, 3], [b, 3], [c, 3]] });
  for (let skip = 0; skip < NC; skip++) shapes.push({ need: [0, 1, 2, 3, 4].filter(i => i !== skip).map(i => [i, 2]) });
  const want = [4, 4, 2]; // pairs, triples, quads
  const kinds = [shapes.filter(s => s.need.length === 2), shapes.filter(s => s.need.length === 3), shapes.filter(s => s.need.length === 4)];
  const pdem = [0, 0, 0, 0, 0];
  for (let k = 0; k < 3; k++) {
    const pool = kinds[k].slice();
    for (let n = 0; n < want[k]; n++) {
      let best = -1, bestScore = Infinity;
      for (let i = 0; i < pool.length; i++) {
        let s = R() * 0.5;
        for (const [c] of pool[i].need) s += pdem[c];
        if (s < bestScore) { bestScore = s; best = i; }
      }
      const shape = pool.splice(best, 1)[0];
      for (const [c] of shape.need) pdem[c]++;
      const need = [0, 0, 0, 0, 0];
      for (const [c, v] of shape.need) need[c] = v;
      const idx = patrons.length;
      patrons.push({ need, points: 3, name: `${PATRON_FIRST[idx]} the ${PATRON_TITLE[idx]}` });
    }
  }
  return { cards, patrons };
}

// ── the checks ────────────────────────────────────────────────────────
export function check(set) {
  const errs = [];
  const { cards, patrons } = set;
  if (cards.length !== 90) errs.push(`expected 90 commissions, got ${cards.length}`);
  const seen = new Set();
  const demand = { 1: [0, 0, 0, 0, 0], 2: [0, 0, 0, 0, 0], 3: [0, 0, 0, 0, 0] };
  const perTierColor = {};
  for (const c of cards) {
    const L = LIMITS[c.tier];
    const sum = c.cost.reduce((a, b) => a + b, 0);
    const key = `${c.tier}:${c.cost.join()}:${c.bonus}`;
    if (seen.has(key)) errs.push(`duplicate card ${c.name}`);
    seen.add(key);
    if (sum < L.min || sum > L.max) errs.push(`${c.name}: cost total ${sum} outside ${L.min}..${L.max}`);
    if (Math.max(...c.cost) > L.tall) errs.push(`${c.name}: a colour above ${L.tall}`);
    if (c.cost[c.bonus] > L.own) errs.push(`${c.name}: asks ${c.cost[c.bonus]} of its own colour`);
    const target = CURVE[c.tier][c.points];
    if (target === undefined) errs.push(`${c.name}: no curve point for ${c.points} points in tier ${c.tier}`);
    else if (Math.abs(effort(c.cost) - target) > BAND) errs.push(`${c.name}: effort ${effort(c.cost).toFixed(2)} off the curve (${target} +- ${BAND})`);
    for (let i = 0; i < NC; i++) demand[c.tier][i] += c.cost[i];
    const k = `${c.tier}:${c.bonus}`;
    perTierColor[k] = (perTierColor[k] || 0) + c.points;
  }
  for (const t of [1, 2, 3]) {
    const d = demand[t];
    const avg = d.reduce((a, b) => a + b, 0) / NC;
    for (let i = 0; i < NC; i++)
      if (Math.abs(d[i] - avg) > avg * 0.1) errs.push(`tier ${t}: ${COLORS[i]} asked ${d[i]} times, average ${avg.toFixed(1)} (more than 10% off)`);
    const pts = [0, 1, 2, 3, 4].map(i => perTierColor[`${t}:${i}`] || 0);
    if (new Set(pts).size !== 1) errs.push(`tier ${t}: points per bonus colour differ (${pts.join(',')})`);
  }
  if (patrons.length !== 10) errs.push(`expected 10 patrons, got ${patrons.length}`);
  const pd = [0, 0, 0, 0, 0];
  const pk = new Set();
  for (const p of patrons) {
    const total = p.need.reduce((a, b) => a + b, 0);
    if (total < 8 || total > 9) errs.push(`${p.name}: needs ${total} bonuses (8 or 9)`);
    if (pk.has(p.need.join())) errs.push(`${p.name}: duplicate requirement`);
    pk.add(p.need.join());
    p.need.forEach((v, i) => { if (v) pd[i]++; });
  }
  if (Math.max(...pd) - Math.min(...pd) > 1) errs.push(`patron colours unbalanced (${pd.join(',')})`);
  return errs;
}

function writeOut(set) {
  const { cards, patrons } = set;
  const lines = [];
  lines.push('/* Facet: the commission and patron set. GENERATED by tools/gen-cards.mjs; do not edit by hand. */');
  lines.push('#ifndef FACET_CARDS_DATA_H', '#define FACET_CARDS_DATA_H', '', '#include "rules.h"', '');
  lines.push(`static const gm_card gm_cards[GM_NCARDS] = {`);
  for (const c of cards)
    lines.push(`  {${c.tier - 1}, ${c.bonus}, ${c.points}, {${c.cost.join(', ')}}, "${c.name}"},`);
  lines.push('};', '');
  lines.push(`static const gm_patron gm_patrons[GM_NPATRONS] = {`);
  for (const p of patrons) lines.push(`  {${p.points}, {${p.need.join(', ')}}, "${p.name}"},`);
  lines.push('};', '', '#endif', '');
  writeFileSync(join(ROOT, 'src/cards_data.h'), lines.join('\n'));
  mkdirSync(join(ROOT, 'assets'), { recursive: true });
  writeFileSync(join(ROOT, 'assets/cards.json'), JSON.stringify({ colors: COLORS, cards, patrons }, null, 1) + '\n');
}

function report(set) {
  for (const t of [1, 2, 3]) {
    const cs = set.cards.filter(c => c.tier === t);
    const d = [0, 0, 0, 0, 0];
    for (const c of cs) c.cost.forEach((v, i) => (d[i] += v));
    console.log(`tier ${t}: ${cs.length} cards, demand ${d.join('/')}, points ${cs.reduce((a, c) => a + c.points, 0)}`);
  }
}

const args = process.argv.slice(2);
if (args.includes('--selftest')) {
  const good = generate();
  const fails = [];
  if (check(good).length) fails.push('the real set must pass');
  const mut = (f) => { const s = structuredClone(good); f(s); return check(s).length > 0; };
  if (!mut(s => { s.cards[3].points = 3; })) fails.push('a tier I card at 3 points must fail (off the curve)');
  if (!mut(s => { s.cards[50].cost = [7, 7, 0, 0, 0]; })) fails.push('a cost far off the curve must fail');
  if (!mut(s => { s.cards.pop(); })) fails.push('89 cards must fail');
  if (!mut(s => { s.cards[1] = { ...s.cards[0] }; })) fails.push('a duplicate must fail');
  if (!mut(s => { for (const c of s.cards) if (c.tier === 1 && c.points === 0) { c.cost = [1, 1, 1, 0, 0]; } })) fails.push('all-ruby/sapphire/emerald demand must fail');
  if (!mut(s => { s.patrons[0].need = [6, 6, 0, 0, 0]; })) fails.push('a 12-bonus patron must fail');
  if (fails.length) { console.error('selftest FAILED:\n  ' + fails.join('\n  ')); process.exit(1); }
  console.log('selftest ok: the real set passes and 6 broken sets fail');
} else {
  const set = generate();
  const errs = check(set);
  report(set);
  if (errs.length) { console.error('balance check FAILED:\n  ' + errs.join('\n  ')); process.exit(1); }
  if (!args.includes('--check')) { writeOut(set); console.log('wrote src/cards_data.h and assets/cards.json'); }
  else console.log('balance check ok');
}
