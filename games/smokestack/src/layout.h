/* Smokestack table layout: where hexes, edges, goods, option plaques and job cards sit in the world. Shared by
 * the match (cursor targets: game.c) and the view (view.c), and a pure function of the map, so a CPU's cursor
 * walk and a person's are the same on every host. */
#ifndef RL_LAYOUT_H
#define RL_LAYOUT_H

#include "cartwheel/hex.h"
#include "rules.h"

#define RL_HEX_SIZE 0.5f      /* centre to corner, metres */
#define RL_OPTS 6             /* option plaques along the near edge */
#define RL_SITES 220          /* movable targets on the board */
enum { RL_T_OPT0 = 0, RL_T_ACT0 = RL_OPTS, RL_T_SITE0 = RL_OPTS + RL_ACTIONS, RL_TARGETS = RL_T_SITE0 + RL_SITES };

static inline cw_hex_layout rl_layout(int map) {
  const rl_map_def *m = rl_map(map);
  cw_hex_layout l = { .orientation = CW_HEX_POINTY, .size = { RL_HEX_SIZE, RL_HEX_SIZE } };
  /* the board's middle at x = -0.9 (room for the job cards on the right), z = 0 */
  float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
  for (int i = 0; i < m->nhex; i++) {
    cw_vec2 p = cw_hex_to_point(&l, cw_hex_make(m->q[i], m->r[i]));
    if (p.x < minx) minx = p.x;
    if (p.x > maxx) maxx = p.x;
    if (p.y < minz) minz = p.y;
    if (p.y > maxz) maxz = p.y;
  }
  l.origin = (cw_vec2){ -0.9f - (minx + maxx) * 0.5f, -(minz + maxz) * 0.5f };
  return l;
}

/* the board's half extents (x, z) */
static inline cw_vec2 rl_board_half(int map) {
  const rl_map_def *m = rl_map(map);
  return (cw_vec2){ 0.866f * RL_HEX_SIZE * ((float)m->cols + 0.5f), 0.75f * RL_HEX_SIZE * (float)m->rows + 0.25f * RL_HEX_SIZE };
}

static inline float rl_ground(int terrain) {
  switch (terrain) {
  case RL_T_HILLS: return 0.16f;
  case RL_T_FOREST: return 0.07f;
  case RL_T_RIVER: return 0.05f;
  case RL_T_LAKE: return 0.02f;
  case RL_T_PEAK: return 0.2f;
  default: return 0.06f;
  }
}

static inline t3_vec3 rl_hex_at(int map, int hex, float lift) {
  cw_hex_layout l = rl_layout(map);
  const rl_map_def *m = rl_map(map);
  return cw_hex_to_world(&l, cw_hex_make(m->q[hex], m->r[hex]), rl_ground(m->terrain[hex]) + lift);
}

/* the middle of a hex's edge toward direction d, pulled in by `in` (0 on the edge, 1 at the centre) */
static inline t3_vec3 rl_edge_at(int map, int hex, int d, float in, float lift) {
  t3_vec3 c = rl_hex_at(map, hex, lift);
  cw_axial step = cw_hex_direction(d);
  cw_hex_layout l = rl_layout(map);
  l.origin = (cw_vec2){ 0, 0 };
  cw_vec2 v = cw_hex_to_point(&l, step); /* centre to neighbour centre */
  float k = 0.5f * (1 - in);
  return (t3_vec3){ c.x + v.x * k, c.y, c.z + v.y * k };
}

/* a good's place in a city: a row of four on the city's near side */
static inline t3_vec3 rl_good_at(int map, int set, int slot) {
  t3_vec3 c = rl_hex_at(map, rl_map(map)->set_hex[set], 0.0f);
  return (t3_vec3){ c.x - 0.255f + 0.17f * (float)slot, c.y + 0.08f, c.z + 0.29f };
}

/* option plaque i of n along the near edge, centred under the board */
static inline t3_vec3 rl_opt_at(int map, int i, int n) {
  cw_vec2 h = rl_board_half(map);
  return (t3_vec3){ -0.9f + ((float)i - 0.5f * (float)(n - 1)) * 1.38f, 0.04f, h.y + 0.55f };
}
/* how many option plaques a phase lays out */
static inline int rl_opt_count(int phase) {
  switch (phase) {
  case RL_PH_BID: return 4;
  case RL_PH_BUILD: return 3;
  case RL_PH_COLOR: return RL_COLORS;
  default: return 2;
  }
}
static inline t3_vec3 rl_act_at(int map, int a) {
  cw_vec2 h = rl_board_half(map);
  float x = -0.9f + h.x + 1.05f;
  return (t3_vec3){ x, 0.03f, -h.y + 0.4f + (2 * h.y - 0.3f) * (float)a / (RL_ACTIONS - 1) };
}

#endif
