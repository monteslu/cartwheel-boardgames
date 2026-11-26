// Writes crokinole's gate input scripts (tests/crokinole-*.json; run-cart's format, tools/run-cart.mjs). Each gate's
// env (tests/crokinole-gates.json) seats a person on pad 0 against a Normal CPU (CRK_SEATS=p,c1) and starts past the
// menus; the person's how-to card closes on its first south press.
//
//   crokinole-pad.json           the pad alone: the how-to card closed, then two shots, each turning the aim on the
//                                shoulders, sliding along the line on the d-pad and charging on south; the second
//                                also watches the replay of the first (north). The control never charges: no shot.
//   crokinole-touch.json         touch alone (the pad only closes the card), through the engine's recorded pointer
//                                channel: a finger slides the disc along the line, then drags back from the disc and
//                                lets go. The control lands the finger on the table beside the board, so nothing is
//                                picked up and no shot is made.
//
//   node tests/crokinole-inputs.mjs
import { writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const out = (name, s) => writeFileSync(join(ROOT, 'tests', name), `[${s.map((e) => JSON.stringify(e)).join(',\n')}]\n`);

/* a pad script: [frame, buttons] holds, each until the next entry */
function pad(holds) {
  return [{ frame: 1 }, ...holds.map(([frame, buttons]) => (buttons.length ? { frame, buttons } : { frame }))];
}
const CLOSE_CARD = [[60, ['south']], [64, []]];
const shot1 = [[90, ['l']], [112, []], [120, ['right']], [132, []], [140, ['south']], [232, []]];
const shot2 = [[1500, ['r']], [1530, []], [1540, ['left']], [1556, []], [1570, ['south']], [1650, []]];
const replay = [[1440, ['north']], [1444, []]];
out('crokinole-pad.json', pad([...CLOSE_CARD, ...shot1, ...replay, ...shot2]));
out('crokinole-pad-control.json', pad([...CLOSE_CARD, ...shot1.filter(([, b]) => !b.includes('south')), ...shot2.filter(([, b]) => !b.includes('south'))]));

/* touch: the disc on seat 0's line sits at the middle of the window's lower edge under the seat-0 camera (the debug
 * fields aim.disc_wx / aim.disc_wy report it: 640, 614 at 1280 x 720) */
const DX = 640, DY = 614;
function finger(path, id = 1) { /* [frame, x, y] points, the finger down from the first to the last */
  const s = path.map(([frame, x, y]) => ({ frame, touches: [{ id, x, y }] }));
  s.push({ frame: path[path.length - 1][0] + 1 });
  return s;
}
const slide = finger([[90, DX + 160, DY - 10], [96, DX + 120, DY - 6], [104, DX + 60, DY - 2], [110, DX + 60, DY - 2]]);
const flick = (x0, y0) => finger([[140, x0, y0], [150, x0 + 10, y0 + 30], [160, x0 + 20, y0 + 60], [170, x0 + 24, y0 + 70]], 2);
const head = [{ frame: 1 }, { frame: 60, buttons: ['south'] }, { frame: 64 }];
out('crokinole-touch.json', [...head, ...slide, ...flick(DX + 60, DY - 2)]);
out('crokinole-touch-control.json', [...head, ...finger([[90, 60, 700], [110, 80, 700]]), ...finger([[140, 60, 700], [170, 90, 710]], 2)]);
console.log('wrote tests/crokinole-pad.json, -pad-control, -touch, -touch-control');
