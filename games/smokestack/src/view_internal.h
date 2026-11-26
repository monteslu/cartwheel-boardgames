/* Smokestack view internals: shared by view.c (the 3D table) and hud.c (faces, the HUD). */
#ifndef RL_VIEW_INTERNAL_H
#define RL_VIEW_INTERNAL_H

#include "boardkit/bk_view.h"
#include "view.h"

#define RL_PATH_MAX 256
#define RL_BEADS 64
#define RL_PUFFS 14

/* card faces */
enum {
  RL_FACE_JOB0 = 0,
  RL_FACE_OPT0 = 10, /* option faces: one per kind of option */
  RL_FACE_DROP = RL_FACE_OPT0, RL_FACE_R1, RL_FACE_R2, RL_FACE_R5, RL_FACE_END, RL_FACE_SHARE, RL_FACE_CHARTER,
  RL_FACE_PASS, RL_FACE_ENGINE, RL_FACE_INCOME, RL_FACE_POINTS, RL_FACE_COLOR0,
  RL_FACE_LAST = RL_FACE_COLOR0 + RL_COLORS,
};

typedef struct settlement_view {
  cw_node root;
  t3_material *roof, *win;
  float glow;
} settlement_view;

typedef struct board_view {
  bool built;
  cw_node root, terrain, decor[RL_MAX_HEX], tile[RL_MAX_HEX];
  int tile_key[RL_MAX_HEX];
  float tile_t[RL_MAX_HEX];
  settlement_view set[RL_MAX_SET][2]; /* [k][RL_CITY], [k][RL_TOWN] */
  cw_node cube[RL_MAX_SET][RL_CITY_CUBES];
  uint8_t cube_shown[RL_MAX_SET][RL_CITY_CUBES];
  float cube_t[RL_MAX_SET][RL_CITY_CUBES];
} board_view;

typedef struct rl_view_state {
  cw_tree *tree;
  cw_node world, cam;
  rl_game *g;
  int map;
  board_view board[RL_MAPS];
  t3_geometry *track_geo[2][36];
  t3_material *ballast[RL_MAX_PLAYERS], *wood, *steel;
  /* the train */
  cw_node loco, wagon, wagon_good;
  t3_material *train_accent;
  t3_vec3 train_path[RL_PATH_MAX];
  float train_acc[RL_PATH_MAX];
  int train_n, train_dest, train_seat, haul_seen;
  float train_t, train_time;
  bool train_on, train_arrived;
  cw_node puff[RL_PUFFS];
  t3_material *puff_mat[RL_PUFFS];
  t3_vec3 puff_at[RL_PUFFS];
  float puff_age[RL_PUFFS], puff_clock;
  int puff_next;
  /* previews and the cursor */
  cw_node ghost, ghost_pair[36], bead[RL_BEADS], marker, pointer;
  /* cards */
  cw_node job[RL_ACTIONS], job_token[RL_ACTIONS], opt[RL_OPTS];
  int opt_face[RL_OPTS];
  /* the HUD's numbers */
  bk_counter money[RL_MAX_PLAYERS], points[RL_MAX_PLAYERS];
  int shown_income[RL_MAX_PLAYERS];
  bk_deltas deltas;
  /* score changes wait their turn per panel spot (money, points): one shows at a time, never drawn over another */
  int dq[RL_MAX_PLAYERS][2][8], dq_n[RL_MAX_PLAYERS][2];
  float dq_wait[RL_MAX_PLAYERS][2];
  int log_at;
  rl_view_stats stats;
} rl_view_state;

extern rl_view_state RV;
extern const uint32_t RL_GOODS_RGB[RL_COLORS];
extern const uint32_t RL_SEAT_RGB[RL_MAX_PLAYERS];

t3_vec3 rl_curve(int e, int d, float t);
t3_geometry *rl_track_geometry(int e, int d, bool bridge);
void rl_paint_face(int face, cw_vec2 px);                 /* hud.c */
int rl_option_face(const rl_state *s, bk_move m);         /* hud.c */
cw_vec2 rl_hud_panel_point(int seat, int which);          /* hud.c: 0 money, 1 points */

#endif
