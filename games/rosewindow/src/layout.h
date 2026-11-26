/* Rose Window: where everything stands on the table (pure math, no engine calls beyond the vector types).
 *
 * Table units: a pane is LY_PANE across. The table is the x/z plane, y up, x right, z toward the camera. The windows
 * stand in a row nearest the camera, each leaning back on its easel at LY_TILT so it faces the players, lit from
 * behind; the dice pool runs across the middle, the shared goal cards on the left and the tool cards on the right
 * above it, the round track along the far edge. During setup two offered frames stand in the middle. */
#ifndef ROSEWINDOW_LAYOUT_H
#define ROSEWINDOW_LAYOUT_H

#include "rules.h"
#include "three.h"

#define LY_PANE 0.8f
#define LY_DIE 0.56f
#define LY_BORDER 0.28f
#define LY_WIN_W (RW_COLS * LY_PANE + 2 * LY_BORDER)
#define LY_WIN_H (RW_ROWS * LY_PANE + 2 * LY_BORDER)
#define LY_TILT 0.65f          /* radians each window is raised from lying flat (its top edge up and away) */
#define LY_CARD_W 2.0f
#define LY_CARD_H 2.8f
#define LY_WINDOWS 6           /* the seats' windows, then the two offered frames */
#define LY_OFFER0 4

typedef struct ly_table {
  int players;
  t3_vec3 win[LY_WINDOWS];   /* each window's centre (in the plane of its panes) */
  float win_scale[LY_WINDOWS];
  t3_vec3 pool[RW_POOL_MAX];
  int pool_n;
  t3_vec3 pass;              /* the pass bell */
  t3_vec3 bag;
  t3_vec3 goal[3], tool[RW_TOOLS_IN_PLAY];
  t3_vec3 track0;            /* round 1's track slot */
  float track_step;
  float min_x, max_x, min_z, max_z; /* what the camera must show */
} ly_table;

void ly_table_make(ly_table *t, int players);
/* a window's lean: the rotation from its flat local frame (x right, z down the window, y out of the glass) */
t3_quat ly_win_rot(void);
/* a point in window w's local frame (x across, z down, y out of the glass) in the world */
t3_vec3 ly_win_point(const ly_table *t, int w, float lx, float lz, float ly);
/* the centre of a pane of window w, lifted `up` out of the glass */
t3_vec3 ly_pane(const ly_table *t, int w, int pane, float up);
/* a die on the round track: round r (1..10), the k-th die of that round */
t3_vec3 ly_track(const ly_table *t, int round, int k);
/* favour beads and the own-colour gem in front of window w */
t3_vec3 ly_bead(const ly_table *t, int w, int i);
t3_vec3 ly_gem(const ly_table *t, int w);

#endif
