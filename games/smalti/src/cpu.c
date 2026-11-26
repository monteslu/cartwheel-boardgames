/* Smalti CPU seats. See cpu.h. */
#include "cpu.h"

#include <stdlib.h>

struct ms_cpu {
  bk_game g;
  uint64_t seed;
  cw_search_options o;
};

void ms_cpu_options(int players, int level, uint64_t seed, cw_search_options *o) {
  if (level < 0 || level > 2) CW_FATAL("smalti cpu: level %d (0..2)", level);
  cw_search_level_options(level, CW_SEARCH_MINIMAX, o);
  o->seed = seed;
  o->tt_entries = 1 << 14;
  /* one decision lists ~20-120 moves: shallow and wide. Easy looks one move ahead and slips often, Normal two,
   * Hard three (alpha-beta; with three or four seats paranoid, me against the best of the rest) */
  static const int depth[3] = { 1, 2, 3 };
  const int64_t nodes[3] = { 4000, 40000, players > 2 ? 600000 : 150000 };
  static const float mistake[3] = { 0.35f, 0.12f, 0 };
  /* with three or four seats Hard looks four moves ahead: among Normals it took 0.46 of the wins at three seats (an
   * even share is 0.33) and 0.38 at four (even 0.25) looking three ahead, 0.54 and 0.40 looking four (30 seeds a seat) */
  o->max_depth = level == 2 && players > 2 ? 4 : depth[level];
  o->max_nodes = nodes[level];
  o->mistake = mistake[level];
  o->mistake_spread = level == 0 ? 3 : 2;
  o->paranoid = players > 2;
}

ms_cpu *ms_cpu_new(int players, int level, uint64_t seed) {
  ms_cpu *c = calloc(1, sizeof *c);
  ms_game(&c->g, players);
  c->seed = seed;
  ms_cpu_options(players, level, seed, &c->o);
  return c;
}

void ms_cpu_free(ms_cpu *c) { free(c); }

bk_move ms_cpu_choose(ms_cpu *c, const ms_state *s) {
  /* a fresh seed per decision, from the state: the answer depends only on (state, level, seed) */
  cw_search_options o = c->o;
  o.seed = c->seed ^ ms_hash(s);
  cw_search *se = cw_search_new(&c->g, &o);
  bk_move m = BK_MOVE_NONE;
  if (!cw_search_run(se, s, &m) || m == BK_MOVE_NONE) CW_FATAL("smalti cpu: the search gave no move");
  cw_search_free(se);
  return m;
}
