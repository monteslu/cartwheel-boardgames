/* Fever Line: where things sit on the table (world units, y up, the players sit toward +z). The map board's
 * painting in tools/make-textures.mjs uses the same numbers (x -11..11, z -6.2..6.2). */
#ifndef FEVERLINE_LAYOUT_H
#define FEVERLINE_LAYOUT_H

#include <math.h>

#include "rules.h"
#include "three.h"

#define LY_BOARD_W 22.0f
#define LY_BOARD_D 12.4f
#define LY_BOARD_Y 0.16f /* the board's top */
#define LY_CUBE 0.2f     /* a fever cube's edge */

/* the left wing: the outbreak track and the spread-rate track side by side at the far end, the two decks (and
 * their discards) nearer the players */
#define LY_OB_X (-14.1f)
#define LY_RATE_X (-12.5f)
#define LY_TRACK_Z0 (-5.5f)
#define LY_TRACK_DZ 0.62f
#define LY_PDECK_X (-14.2f)
#define LY_FDECK_X (-12.2f)
#define LY_DECK_Z 1.3f
#define LY_DISCARD_Z 3.6f
#define LY_CARD_W 1.3f
#define LY_CARD_H 1.82f

static inline t3_vec3 ly_city(int c) { return (t3_vec3){ FV_CITIES[c].x, LY_BOARD_Y, FV_CITIES[c].z }; }

/* a tower of one fever's cubes beside a city: its own fever due north, the others round the north side */
static inline t3_vec3 ly_cube(int city, int colour, int k) {
  int slot = (colour - fv_colour(city) + 4) % 4;
  static const float DX[4] = { 0.0f, 0.4f, -0.4f, 0.52f }, DZ[4] = { -0.46f, -0.3f, -0.3f, 0.08f };
  t3_vec3 p = ly_city(city);
  return (t3_vec3){ p.x + DX[slot], p.y + LY_CUBE * 0.5f + (float)k * (LY_CUBE + 0.012f), p.z + DZ[slot] };
}
/* where an agent's pawn stands in a city (four places on the south side) */
static inline t3_vec3 ly_pawn(int city, int role_index) {
  t3_vec3 p = ly_city(city);
  static const float DX[4] = { 0.0f, 0.44f, -0.44f, 0.0f }, DZ[4] = { 0.5f, 0.4f, 0.4f, 0.92f };
  return (t3_vec3){ p.x + DX[role_index & 3], p.y, p.z + DZ[role_index & 3] };
}
static inline t3_vec3 ly_clinic(int city) {
  t3_vec3 p = ly_city(city);
  return (t3_vec3){ p.x - 0.56f, p.y, p.z + 0.02f };
}
static inline t3_vec3 ly_ob_slot(int k) { return (t3_vec3){ LY_OB_X, 0.06f, LY_TRACK_Z0 + (float)k * LY_TRACK_DZ }; }
static inline t3_vec3 ly_rate_slot(int k) { return (t3_vec3){ LY_RATE_X, 0.06f, LY_TRACK_Z0 + (float)(k + 1) * LY_TRACK_DZ }; }

/* the action plates: a column (two when many) on the right of the board */
static inline t3_vec3 ly_plate(int i, int n) {
  int col = n > 8 ? i / ((n + 1) / 2) : 0, row = n > 8 ? i % ((n + 1) / 2) : i, rows = n > 8 ? (n + 1) / 2 : n;
  float x = n > 8 ? 12.3f + (float)col * 2.6f : 13.4f;
  return (t3_vec3){ x, 0.6f, ((float)row - (float)(rows - 1) * 0.5f) * 1.0f + 0.4f };
}

#endif
