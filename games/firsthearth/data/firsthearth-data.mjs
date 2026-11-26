// First Hearth: the village's own cards and buildings. tools/gen-data.mjs checks their balance and writes
// src/firsthearth_data.h from this file; edit here, never the header.
//
// Effects: food, wood, clay, stone, gold, points, field (+1 field level), tool (+1 tool strength).
// Lore marks (the eight craft symbols): collecting different marks scores the count squared at the end.
// Kin roles (end multipliers): planters x field level, knappers x tool strength, builders x buildings built,
// elders x kin in the village.

export const MARKS = ['Ember', 'Flint', 'Ochre', 'Flute', 'Drum', 'Cord', 'Basket', 'Starlore'];
export const ROLES = ['Planters', 'Knappers', 'Builders', 'Elders'];
export const RESOURCES = ['food', 'wood', 'clay', 'stone', 'gold'];
// what one unit of each resource is worth in points: wood, clay, stone, gold score their value when built
export const VALUE = { food: 2, wood: 3, clay: 4, stone: 5, gold: 6 };

// a card is either a lore mark (mark: index) or a kin role (role: index, mult), plus up to two effects
export const CARDS = [
  { name: 'Keeper of Embers', mark: 0, fx: [['food', 4]] },
  { name: 'Night Fire', mark: 0, fx: [['points', 5]] },
  { name: 'Flint Knapping', mark: 1, fx: [['tool', 1], ['food', 2]] },
  { name: 'Sharp Edges', mark: 1, fx: [['stone', 1]] },
  { name: 'Red Ochre', mark: 2, fx: [['clay', 1], ['points', 2]] },
  { name: 'Painted Wall', mark: 2, fx: [['gold', 1]] },
  { name: 'Bone Flute', mark: 3, fx: [['points', 4]] },
  { name: 'Evening Song', mark: 3, fx: [['food', 3], ['points', 2]] },
  { name: 'Hide Drum', mark: 4, fx: [['wood', 2]] },
  { name: 'Harvest Beat', mark: 4, fx: [['field', 1]] },
  { name: 'Twisted Cord', mark: 5, fx: [['wood', 1], ['clay', 1]] },
  { name: 'Snare Line', mark: 5, fx: [['tool', 1], ['points', 2]] },
  { name: 'Reed Basket', mark: 6, fx: [['food', 5]] },
  { name: 'Seed Basket', mark: 6, fx: [['field', 1], ['food', 1]] },
  { name: 'Star Reading', mark: 7, fx: [['points', 6]] },
  { name: 'Moon Count', mark: 7, fx: [['stone', 1], ['points', 1]] },
  { name: 'Planters', role: 0, mult: 1, fx: [['field', 1]] },
  { name: 'Planters', role: 0, mult: 1, fx: [['food', 4]] },
  { name: 'Planters', role: 0, mult: 2, fx: [['points', 3]] },
  { name: 'Planters', role: 0, mult: 2, fx: [['food', 2]] },
  { name: 'Knappers', role: 1, mult: 1, fx: [['tool', 1], ['points', 2]] },
  { name: 'Knappers', role: 1, mult: 1, fx: [['stone', 1]] },
  { name: 'Knappers', role: 1, mult: 2, fx: [['points', 2]] },
  { name: 'Knappers', role: 1, mult: 2, fx: [['tool', 1]] },
  { name: 'Builders', role: 2, mult: 1, fx: [['clay', 1], ['points', 2]] },
  { name: 'Builders', role: 2, mult: 1, fx: [['wood', 2]] },
  { name: 'Builders', role: 2, mult: 2, fx: [['points', 1]] },
  { name: 'Builders', role: 2, mult: 2, fx: [['gold', 1]] },
  { name: 'Elders', role: 3, mult: 1, fx: [['food', 3]] },
  { name: 'Elders', role: 3, mult: 1, fx: [['points', 3]] },
  { name: 'Elders', role: 3, mult: 1, fx: [['food', 2]] },
  { name: 'Elders', role: 3, mult: 2, fx: [] },
];

// buildings: an exact cost in wood, clay, stone, gold; points are the cost's value, plus 2 for a
// four-piece longhouse
const B = (name, w, c, s, g) => ({ name, cost: [w, c, s, g] });
export const BUILDINGS = [
  B('Lean-to', 2, 1, 0, 0), B('Brush Shelter', 2, 1, 0, 0),
  B('Stone Hearth', 2, 0, 1, 0), B('Fire Ring', 2, 0, 1, 0),
  B('Bright Lodge', 2, 0, 0, 1),
  B('Mud Hut', 1, 2, 0, 0), B('Daub House', 1, 2, 0, 0),
  B('Kiln Shed', 1, 1, 1, 0), B('Pot Store', 1, 1, 1, 0),
  B('Shell Hall', 1, 1, 0, 1), B('Bead Hut', 1, 1, 0, 1),
  B('Rock Shelter', 1, 0, 2, 0), B('Cairn House', 1, 0, 2, 0),
  B('Sun Hut', 1, 0, 1, 1), B('Amber Hut', 1, 0, 1, 1),
  B('Shining House', 1, 0, 0, 2),
  B('Clay Mound', 0, 2, 1, 0), B('Oven House', 0, 2, 1, 0),
  B('Glaze Hall', 0, 2, 0, 1),
  B('Pillar Hut', 0, 1, 2, 0), B('Standing Stones', 0, 1, 2, 0),
  B('Ochre Shrine', 0, 1, 1, 1), B('Totem House', 0, 1, 1, 1),
  B('Stone Circle', 0, 0, 2, 1),
  B('Longhouse', 2, 1, 1, 0), B('Elder Lodge', 1, 1, 1, 1),
  B('Granary', 1, 2, 1, 0), B('Gathering Hall', 2, 1, 0, 1),
];
export const LONGHOUSE_BONUS = 2;
export const buildingPoints = (b) => {
  const v = b.cost[0] * 3 + b.cost[1] * 4 + b.cost[2] * 5 + b.cost[3] * 6;
  const n = b.cost.reduce((a, x) => a + x, 0);
  return v + (n >= 4 ? LONGHOUSE_BONUS : 0);
};
