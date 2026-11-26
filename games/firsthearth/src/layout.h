/* First Hearth: where things sit on the table (world units, y up, the camera looks from +z). The village board's
 * painted regions in tools/make-textures.mjs use the same numbers. */
#ifndef FIRSTHEARTH_LAYOUT_H
#define FIRSTHEARTH_LAYOUT_H

#include <math.h>

#include "rules.h"
#include "three.h"

#define LY_BOARD_X0 (-9.0f)
#define LY_BOARD_X1 9.0f
#define LY_BOARD_Z0 (-6.0f)
#define LY_BOARD_Z1 4.0f
#define LY_BOARD_Y 0.22f   /* the board's top */
#define LY_MAT_Z 6.9f      /* the player mats' centre line */
#define LY_MAT_W 4.3f
#define LY_MAT_D 3.7f
#define LY_MAT_Y 0.03f
#define LY_CARD_W 1.44f
#define LY_CARD_D 2.0f

/* each site's centre and half extents on the board (x, z, hx, hz) */
static const float LY_SITE[TR_NSITES][4] = {
  { -5.6f, 0.0f, 3.0f, 1.55f },  /* hunting grounds */
  { -6.3f, -3.7f, 2.4f, 1.6f },  /* forest */
  { -2.1f, -3.8f, 1.8f, 1.45f }, /* clay bank */
  { 2.1f, -3.8f, 1.8f, 1.45f },  /* quarry */
  { 6.3f, -3.7f, 2.4f, 1.6f },   /* river */
  { -1.55f, 0.0f, 1.25f, 1.25f },/* hearth */
  { 1.55f, 0.0f, 1.25f, 1.25f }, /* garden */
  { 5.5f, 0.0f, 1.9f, 1.3f },    /* tool bench */
  { -7.3f, 2.85f, 0.8f, 1.1f },  /* cards */
  { -5.3f, 2.85f, 0.8f, 1.1f },
  { -3.3f, 2.85f, 0.8f, 1.1f },
  { -1.3f, 2.85f, 0.8f, 1.1f },
  { 1.5f, 2.85f, 0.95f, 0.95f }, /* building piles */
  { 3.6f, 2.85f, 0.95f, 0.95f },
  { 5.7f, 2.85f, 0.95f, 0.95f },
  { 7.8f, 2.85f, 0.95f, 0.95f },
};

static inline t3_vec3 ly_site(int site) { return (t3_vec3){ LY_SITE[site][0], LY_BOARD_Y, LY_SITE[site][1] }; }

/* where kin number k (0..) of a seat stands at a site: a row per seat at the gathering sites, a little group
 * elsewhere (cards and piles: beside the card or the hut) */
static inline t3_vec3 ly_kin_at_site(int site, int seat, int k, int players) {
  const float *r = LY_SITE[site];
  if (site <= TR_S_GOLD) {
    float rows = (float)players, dz = fminf(0.42f, r[3] * 1.5f / rows);
    float z = r[1] + ((float)seat - (rows - 1) * 0.5f) * dz;
    int per = site == TR_S_HUNT ? 10 : 7;
    float dx = fminf(0.34f, r[2] * 1.6f / (float)per);
    float x = r[0] + ((float)k - (float)(per - 1) * 0.5f) * dx;
    return (t3_vec3){ x, LY_BOARD_Y, z + (k & 1) * 0.06f };
  }
  if (site <= TR_S_TOOL) return (t3_vec3){ r[0] - 0.55f + (float)k * 0.38f, LY_BOARD_Y, r[1] + 0.55f };
  if (site <= TR_S_CARD3) return (t3_vec3){ r[0] + 0.55f, LY_BOARD_Y, r[1] - 0.95f };
  return (t3_vec3){ r[0] + 0.7f, LY_BOARD_Y, r[1] - 0.7f };
}

static inline float ly_mat_x(int seat, int players) { return ((float)seat - (float)(players - 1) * 0.5f) * (LY_MAT_W + 0.35f); }
static inline t3_vec3 ly_mat(int seat, int players) { return (t3_vec3){ ly_mat_x(seat, players), LY_MAT_Y, LY_MAT_Z }; }
/* a kin at home: a row along the back of the mat */
static inline t3_vec3 ly_kin_home(int seat, int k, int players) {
  t3_vec3 m = ly_mat(seat, players);
  return (t3_vec3){ m.x - 1.75f + (float)k * 0.39f, LY_MAT_Y + 0.02f, m.z - 1.3f };
}
/* goods piles on the mat: food, wood, clay, stone, gold left to right */
static inline t3_vec3 ly_goods(int seat, int res, int players) {
  t3_vec3 m = ly_mat(seat, players);
  return (t3_vec3){ m.x - 1.6f + (float)res * 0.8f, LY_MAT_Y + 0.02f, m.z - 0.25f };
}
static inline t3_vec3 ly_tool(int seat, int i, int players) {
  t3_vec3 m = ly_mat(seat, players);
  return (t3_vec3){ m.x - 1.65f + (float)i * 0.5f, LY_MAT_Y + 0.02f, m.z + 0.75f };
}
/* the field track: 0..10 along the front edge */
static inline t3_vec3 ly_field(int seat, int level, int players) {
  t3_vec3 m = ly_mat(seat, players);
  return (t3_vec3){ m.x - 1.9f + (float)level * 0.38f, LY_MAT_Y + 0.02f, m.z + 1.45f };
}
/* built huts: a block at the mat's right */
static inline t3_vec3 ly_hut(int seat, int i, int players) {
  t3_vec3 m = ly_mat(seat, players);
  return (t3_vec3){ m.x + 0.2f + (float)(i % 5) * 0.4f, LY_MAT_Y + 0.02f, m.z + 0.78f + (float)(i / 5) * 0.4f };
}
/* cards a seat holds: a fanned row just past the mat's front edge */
static inline t3_vec3 ly_held_card(int seat, int i, int players) {
  t3_vec3 m = ly_mat(seat, players);
  return (t3_vec3){ m.x - 1.75f + (float)i * 0.26f, LY_MAT_Y + 0.01f + (float)i * 0.004f, m.z + 2.25f };
}

#endif
