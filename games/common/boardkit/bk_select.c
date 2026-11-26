/* boardkit: choosing among targets (bk_select.h). */
#include "bk_select.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"

struct bk_select {
  bk_select_desc d;
  cw_cursors *cur;
  bool enabled[BK_SELECT_MAX_TARGETS];
  int pick_seat, pick_target; /* this tick's tap, for bk_select_tick */
  int hover;
  float glow[CW_CURSOR_MAX_PLAYERS][64]; /* glows of the first 64 targets (drawing only) */
};

static bool allow(int player, int target, void *user) {
  (void)player;
  const bk_select *s = user;
  return target >= 0 && target < BK_SELECT_MAX_TARGETS && s->enabled[target];
}

bk_select *bk_select_new(const bk_select_desc *d) {
  cw_input_pointer_channel(true); /* clicks and taps go into the recording, the replay and the input hash */
  bk_select *s = calloc(1, sizeof *s);
  s->d = *d;
  if (s->d.pick_radius <= 0) s->d.pick_radius = 56;
  s->cur = cw_cursors_new(&s->d.cursor);
  cw_cursors_set_allow(s->cur, allow, s);
  for (int i = 0; i < BK_SELECT_MAX_TARGETS; i++) s->enabled[i] = true;
  s->pick_seat = -1;
  s->hover = -1;
  return s;
}

void bk_select_free(bk_select *s) {
  if (!s) return;
  cw_cursors_free(s->cur);
  free(s);
}

cw_cursors *bk_select_cursors(bk_select *s) { return s->cur; }

static void keep_on_enabled(bk_select *s) {
  int n = cw_cursors_count(s->cur);
  for (int p = 0; p < CW_CURSOR_MAX_PLAYERS; p++) {
    int t = cw_cursors_target(s->cur, p);
    if (t < 0 || (t < n && s->enabled[t])) continue;
    /* the nearest enabled target on this player's screen */
    cw_vec2 at = cw_cursors_screen(s->cur, p, t);
    int best = -1;
    float bd = INFINITY;
    for (int i = 0; i < n && i < BK_SELECT_MAX_TARGETS; i++) {
      if (!s->enabled[i]) continue;
      cw_vec2 q = cw_cursors_screen(s->cur, p, i);
      float dd = (q.x - at.x) * (q.x - at.x) + (q.y - at.y) * (q.y - at.y);
      if (dd < bd) bd = dd, best = i;
    }
    if (best >= 0) cw_cursors_place(s->cur, p, best);
  }
}

void bk_select_enable_all(bk_select *s, bool on) {
  for (int i = 0; i < BK_SELECT_MAX_TARGETS; i++) s->enabled[i] = on;
  keep_on_enabled(s);
}
void bk_select_set_enabled(bk_select *s, int target, bool on) {
  if (target < 0 || target >= BK_SELECT_MAX_TARGETS) CW_FATAL("bk_select_set_enabled: target %d (0..%d)", target, BK_SELECT_MAX_TARGETS - 1);
  s->enabled[target] = on;
  if (!on) keep_on_enabled(s);
}
bool bk_select_enabled(const bk_select *s, int target) { return target >= 0 && target < BK_SELECT_MAX_TARGETS && s->enabled[target]; }

static int nearest_enabled(const bk_select *s, int seat, cw_vec2 at) {
  int n = cw_cursors_count(s->cur), best = -1;
  float bd = s->d.pick_radius * s->d.pick_radius;
  for (int i = 0; i < n && i < BK_SELECT_MAX_TARGETS; i++) {
    if (!s->enabled[i]) continue;
    cw_vec2 q = cw_cursors_screen(s->cur, seat, i);
    float dd = (q.x - at.x) * (q.x - at.x) + (q.y - at.y) * (q.y - at.y);
    if (dd <= bd) bd = dd, best = i;
  }
  return best;
}

void bk_select_pointer(bk_select *s, int seat, cw_tree *t) {
  (void)t;
  cw_pointer pt = cw_input_pointer(); /* this frame's */
  s->hover = seat >= 0 && pt.present && !pt.touch ? nearest_enabled(s, seat, cw_r2d_window_to_canvas(pt.pos)) : -1;
}

void bk_select_feed(bk_select *s, int seat) {
  s->pick_seat = -1;
  if (seat < 0) return;
  cw_pointer pt = cw_input_pointer(); /* this tick's: live, or the recording's during a replay */
  if (!pt.pressed) return;
  int target = nearest_enabled(s, seat, cw_r2d_window_to_canvas(pt.pos));
  if (target >= 0) s->pick_seat = seat, s->pick_target = target;
}

bk_sel_event bk_select_tick(bk_select *s, int player) {
  bk_sel_event e = { BK_SEL_NONE, cw_cursors_target(s->cur, player), false };
  if (s->pick_seat == player) { /* this tick's tap (bk_select_feed) */
    int v = s->pick_target;
    s->pick_seat = -1;
    if (v < cw_cursors_count(s->cur) && s->enabled[v]) {
      e.pointer = true;
      if (v == e.target) e.kind = BK_SEL_CONFIRM;
      else {
        cw_cursors_place(s->cur, player, v);
        e.kind = BK_SEL_POINTED, e.target = v;
      }
      return e;
    }
  }
  cw_vec2 stick = cw_input_vector(player, s->d.left, s->d.right, s->d.up, s->d.down, CW_DEADZONE_RADIAL, 0.2f);
  int to = cw_cursors_step(s->cur, player, stick);
  if (to >= 0) e.kind = BK_SEL_MOVED, e.target = to;
  if (cw_action_just_pressed(player, s->d.confirm) && e.target >= 0 && s->enabled[e.target]) e.kind = BK_SEL_CONFIRM;
  else if (s->d.back >= 0 && cw_action_just_pressed(player, s->d.back)) e.kind = BK_SEL_BACK;
  return e;
}

int bk_select_target(const bk_select *s, int player) { return cw_cursors_target(s->cur, player); }
void bk_select_place(bk_select *s, int player, int target) { cw_cursors_place(s->cur, player, target); }
int bk_select_hover(const bk_select *s) { return s->hover; }

void bk_select_update(bk_select *s, float dt) {
  float k = 1 - expf(-12 * dt);
  for (int p = 0; p < CW_CURSOR_MAX_PLAYERS; p++) {
    int t = cw_cursors_target(s->cur, p);
    for (int i = 0; i < 64; i++) s->glow[p][i] += ((i == t ? 1.0f : 0.0f) - s->glow[p][i]) * k;
  }
}
float bk_select_glow(const bk_select *s, int player, int target) {
  if (player < 0 || player >= CW_CURSOR_MAX_PLAYERS || target < 0 || target >= 64) return 0;
  return s->glow[player][target];
}
