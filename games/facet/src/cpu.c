/* Facet CPU (cpu.h). Levels measured by tests/cpu_test.c (seeded head-to-head games, seats swapped). */
#include "cpu.h"

void gm_cpu_options(int level, int players, cw_search_options *o) {
  cw_search_level_options(level, CW_SEARCH_MINIMAX, o);
  o->paranoid = players > 2; /* three or four seats: me against the best of the others */
  switch (level) {
  case 0: /* Easy: one turn ahead, often settles for a lesser move */
    o->max_depth = 1; o->mistake = 0.35f; o->mistake_spread = 4; o->max_nodes = 20000; break;
  case 1: /* Normal: sees the next seat's answer, sometimes slips */
    o->max_depth = 2; o->mistake = 0.10f; o->mistake_spread = 2; o->max_nodes = 60000; break;
  default: /* Hard: its own next turn too, no slips */
    o->max_depth = 3; o->mistake = 0.0f; o->max_nodes = 1000000; break;
  }
}

bk_move gm_cpu_move(const bk_game *g, const gm_state *st, int level, uint64_t seed, cw_search_stats *stats) {
  cw_search_options o;
  gm_cpu_options(level, st->players, &o);
  o.seed = seed;
  cw_search *s = cw_search_new(g, &o);
  bk_move m = BK_MOVE_NONE;
  if (!cw_search_run(s, st, &m)) CW_FATAL("facet cpu: search refused the state");
  if (stats) cw_search_get_stats(s, stats);
  cw_search_free(s);
  return m;
}
