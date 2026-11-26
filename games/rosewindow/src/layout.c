/* Rose Window: the table layout. See layout.h. */
#include "layout.h"

#include <math.h>

#define BASE_Y 0.32f /* the windows' bottom edge stands this high, on the easel's ledge */

void ly_table_make(ly_table *t, int players) {
  *t = (ly_table){ 0 };
  t->players = players;
  /* fewer seats, bigger windows */
  float sc = players == 1 ? 1.4f : players == 2 ? 1.15f : 1.0f;
  float spacing = LY_WIN_W * sc + 0.75f;
  float cy = BASE_Y + 0.5f * LY_WIN_H * sc * sinf(LY_TILT);
  float wz = 3.3f + 0.5f * LY_WIN_H * (sc - 1.0f) * cosf(LY_TILT);
  float cz = wz;
  for (int i = 0; i < BK_MAX_SEATS; i++) {
    float x = ((float)i - 0.5f * (float)(players - 1)) * spacing;
    t->win[i] = (t3_vec3){ x, cy, cz };
    t->win_scale[i] = sc;
  }
  for (int i = 0; i < 2; i++) {
    t->win[LY_OFFER0 + i] = (t3_vec3){ (i ? 1 : -1) * (0.5f * LY_WIN_W + 1.3f), BASE_Y + 0.5f * LY_WIN_H * sinf(LY_TILT), -0.2f };
    t->win_scale[LY_OFFER0 + i] = 1.0f;
  }
  t->pool_n = players == 1 ? 3 : 2 * players + 1;
  for (int k = 0; k < RW_POOL_MAX; k++) t->pool[k] = (t3_vec3){ ((float)k - 0.5f * (float)(t->pool_n - 1)) * 0.98f, LY_DIE * 0.5f, -0.9f };
  float half = 0.5f * (float)(t->pool_n - 1) * 0.98f;
  t->pass = (t3_vec3){ half + 1.5f, 0.0f, -0.9f };
  t->bag = (t3_vec3){ -half - 1.6f, 0.0f, -0.9f };
  for (int i = 0; i < 3; i++) {
    t->goal[i] = (t3_vec3){ -8.6f + 2.35f * (float)i, 0.0f, -3.5f };
    t->tool[i] = (t3_vec3){ 3.9f + 2.35f * (float)i, 0.0f, -3.5f };
  }
  t->track0 = (t3_vec3){ -4.5f, LY_DIE * 0.5f, -7.3f };
  t->track_step = 1.0f;
  float wx = 0.5f * (float)(players - 1) * spacing + 0.5f * LY_WIN_W * sc;
  t->min_x = fminf(-wx, -9.8f);
  t->max_x = fmaxf(wx, 9.8f);
  t->min_z = -8.0f;
  t->max_z = wz + 0.5f * LY_WIN_H * sc * cosf(LY_TILT) + 0.9f;
}

t3_quat ly_win_rot(void) { return t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, LY_TILT); }

t3_vec3 ly_win_point(const ly_table *t, int w, float lx, float lz, float ly) {
  float s = t->win_scale[w];
  t3_vec3 v = t3_vec3_apply_quat((t3_vec3){ lx * s, ly * s, lz * s }, ly_win_rot());
  return (t3_vec3){ t->win[w].x + v.x, t->win[w].y + v.y, t->win[w].z + v.z };
}

t3_vec3 ly_pane(const ly_table *t, int w, int pane, float up) {
  int r = pane / RW_COLS, c = pane % RW_COLS;
  float lx = ((float)c - 0.5f * (RW_COLS - 1)) * LY_PANE, lz = ((float)r - 0.5f * (RW_ROWS - 1)) * LY_PANE;
  return ly_win_point(t, w, lx, lz, up);
}

t3_vec3 ly_track(const ly_table *t, int round, int k) {
  /* a round's dice stand in a short column; more than three start a second column */
  return (t3_vec3){ t->track0.x + t->track_step * (float)(round - 1) + 0.3f * (float)(k / 3), t->track0.y, t->track0.z + 0.45f * (float)(k % 3) };
}

t3_vec3 ly_bead(const ly_table *t, int w, int i) {
  float front = t->win[w].z + 0.5f * LY_WIN_H * t->win_scale[w] * cosf(LY_TILT) + 0.42f;
  return (t3_vec3){ t->win[w].x - 0.5f * LY_WIN_W * t->win_scale[w] + 0.3f + 0.34f * (float)i, 0.1f, front };
}

t3_vec3 ly_gem(const ly_table *t, int w) {
  float front = t->win[w].z + 0.5f * LY_WIN_H * t->win_scale[w] * cosf(LY_TILT) + 0.42f;
  return (t3_vec3){ t->win[w].x + 0.5f * LY_WIN_W * t->win_scale[w] - 0.35f, 0.12f, front };
}
