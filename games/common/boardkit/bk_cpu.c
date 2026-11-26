/* boardkit: CPU seats (bk_cpu.h). */
#include "bk_cpu.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum { CPU_IDLE, CPU_THINK, CPU_PRESS } cpu_phase;

struct bk_cpu {
  bk_game g;
  bk_cpu_desc d;
  cw_search_options o;
  cw_search *search;
  cpu_phase phase;
  int ticks, gap, presses, log_at;
  bk_move move;
  bk_press press;
  cw_search_stats stats;
};

bk_cpu *bk_cpu_new(const bk_game *g, const bk_cpu_desc *d) {
  if (!d || !d->next_press) CW_FATAL("bk_cpu_new: a CPU needs next_press (how it enters a move with the pad's actions)");
  bk_cpu *c = calloc(1, sizeof *c);
  c->g = *g;
  c->d = *d;
  int lv = d->level < 0 ? 0 : d->level > 2 ? 2 : d->level;
  if (d->options) c->o = *d->options;
  else cw_search_level_options(lv, d->kind, &c->o);
  static const int THINK[3] = { 45, 30, 24 }, GAP[3] = { 10, 7, 5 };
  if (c->d.units_per_tick <= 0) c->d.units_per_tick = 4000;
  if (c->d.think_ticks <= 0) c->d.think_ticks = THINK[lv];
  if (c->d.press_gap <= 0) c->d.press_gap = GAP[lv];
  c->search = cw_search_new(g, &c->o);
  if (!c->search) CW_FATAL("bk_cpu_new: cw_search_new refused the game (a required callback missing, or players / state_size out of range)");
  c->move = BK_MOVE_NONE;
  return c;
}

void bk_cpu_free(bk_cpu *c) {
  if (!c) return;
  cw_search_free(c->search);
  free(c);
}

bk_move bk_cpu_intent(const bk_cpu *c) { return c->phase == CPU_PRESS ? c->move : BK_MOVE_NONE; }
bool bk_cpu_thinking(const bk_cpu *c) { return c->phase == CPU_THINK; }
cw_search_stats bk_cpu_last_stats(const bk_cpu *c) { return c->stats; }

int bk_cpu_tick(bk_cpu *c, const bk_match *m, int seat, cw_action_value *out, int cap) {
  if (bk_match_over(m) || bk_match_turn(m) != seat) {
    c->phase = CPU_IDLE;
    return 0;
  }
  if (c->phase == CPU_PRESS && bk_match_log_next(m) != c->log_at) c->phase = CPU_IDLE; /* the move landed */
  if (c->phase == CPU_IDLE) {
    const void *st = bk_match_state(m);
    c->o.seed = bk_seed_mix(c->d.seed ^ (uint64_t)seat, bk_match_hash(m));
    cw_search_free(c->search); /* the seed is per decision: a new search with it */
    c->search = cw_search_new(&c->g, &c->o);
    if (!cw_search_start(c->search, st)) CW_FATAL("bk_cpu: the search refused the seat's own turn (terminal or chance)");
    c->phase = CPU_THINK;
    c->ticks = 0;
    c->move = BK_MOVE_NONE;
  }
  if (c->phase == CPU_THINK) {
    c->ticks++;
    bool done = cw_search_step(c->search, 0, c->d.units_per_tick);
    if (!done || c->ticks < c->d.think_ticks) return 0;
    if (!cw_search_result(c->search, &c->move)) CW_FATAL("bk_cpu: the search said done without a result");
    cw_search_get_stats(c->search, &c->stats);
    c->phase = CPU_PRESS;
    c->log_at = bk_match_log_next(m);
    c->presses = 0;
    c->gap = 0;
    c->press.hold = 0;
  }
  /* pressing */
  if (c->press.hold > 0) {
    int n = 0;
    for (int i = 0; i < c->press.count && n < cap; i++) out[n++] = c->press.v[i];
    if (--c->press.hold == 0) c->gap = c->d.press_gap;
    return n;
  }
  if (c->gap > 0) {
    c->gap--;
    return 0;
  }
  bk_press p = { 0 };
  if (!c->d.next_press(bk_match_state(m), c->move, seat, &p, c->d.user)) return 0;
  if (++c->presses > BK_CPU_MAX_PRESSES)
    CW_FATAL("bk_cpu: seat %d pressed %d times and move 0x%llx never landed (next_press or the rules are wrong)", seat,
             BK_CPU_MAX_PRESSES, (unsigned long long)c->move);
  if (p.hold < 1) p.hold = 1;
  c->press = p;
  int n = 0;
  for (int i = 0; i < p.count && n < cap; i++) out[n++] = p.v[i];
  if (--c->press.hold == 0) c->gap = c->d.press_gap;
  return n;
}

bool bk_cpu_press_toward(const cw_cursors *cur, const cw_cursor_desc *d, int player, int target, cw_action left,
                         cw_action right, cw_action up, cw_action down, cw_action confirm, bk_press *out) {
  return bk_cpu_press_toward_allow(cur, d, NULL, NULL, player, target, left, right, up, down, confirm, out);
}

bool bk_cpu_press_toward_allow(const cw_cursors *cur, const cw_cursor_desc *d, cw_cursor_allow_fn allow, void *allow_user,
                               int player, int target, cw_action left, cw_action right, cw_action up, cw_action down,
                               cw_action confirm, bk_press *out) {
  memset(out, 0, sizeof *out);
  out->hold = 1;
  int from = cw_cursors_target(cur, player);
  if (from == target) {
    out->v[0] = (cw_action_value){ confirm, 1 };
    out->count = 1;
    return true;
  }
  int n = cw_cursors_count(cur);
  if (n <= 0 || target < 0 || target >= n) return false;
  cw_vec2 *pts = malloc(sizeof *pts * (size_t)n);
  for (int i = 0; i < n; i++) pts[i] = cw_cursors_screen(cur, player, i);
  cw_vec2 goal = pts[target];
  float here = from >= 0 ? hypotf(pts[from].x - goal.x, pts[from].y - goal.y) : INFINITY;
  static const int DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
  int best = -1;
  float best_d = here;
  for (int k = 0; k < 8; k++) {
    float len = (DX[k] && DY[k]) ? 0.70710678f : 1.0f;
    int to = cw_cursor_hop_select(pts, n, from, (cw_vec2){ (float)DX[k] * len, (float)DY[k] * len }, d, allow, player, allow_user);
    if (to < 0) continue;
    float dist = hypotf(pts[to].x - goal.x, pts[to].y - goal.y);
    if (to == target) dist = -1; /* a hop straight there wins */
    if (dist < best_d) best_d = dist, best = k;
  }
  free(pts);
  if (best < 0) return false;
  if (DX[best]) out->v[out->count++] = (cw_action_value){ DX[best] > 0 ? right : left, 1 };
  if (DY[best]) out->v[out->count++] = (cw_action_value){ DY[best] > 0 ? down : up, 1 };
  return true;
}
