/* Rose Window CPU seats. See cpu.h. */
#include "cpu.h"

#include <stdlib.h>

struct rw_cpu {
  bk_game g;
  uint64_t seed;
  cw_search_options o;
};

void rw_cpu_options(int players, int level, uint64_t seed, cw_search_options *o) {
  if (level < 0 || level > 2) CW_FATAL("rosewindow cpu: level %d (0..2)", level);
  cw_search_level_options(level, CW_SEARCH_MINIMAX, o);
  o->seed = seed;
  o->tt_entries = 1 << 14;
  /* a decision lists up to ~150 moves, and the dice to come are averaged over their outcomes (expectimax). Easy looks
   * at its own move only and slips often, Normal sees its move and a tool before it, Hard looks a decision further
   * (with three or four seats paranoid: me against the best of the rest; Hard with others at the table: below) */
  static const int depth[3] = { 1, 2, 4 };
  static const int64_t nodes[3] = { 3000, 30000, 300000 };
  static const float mistake[3] = { 0.4f, 0.1f, 0 };
  o->max_depth = depth[level];
  o->max_nodes = nodes[level];
  o->mistake = mistake[level];
  o->mistake_spread = level == 0 ? 4 : 2;
  o->paranoid = players > 2;
  /* Hard at a shared table plays the dice forward instead (Monte Carlo tree search: playouts six decisions deep, then
   * the evaluation): over 40 seeded two-seat games it beat Normal 0.69 (the expectimax above: 0.56). Alone it keeps
   * the expectimax, which beats the round track far more often (playouts read the solo target badly) */
  if (level == 2 && players > 1) {
    o->kind = CW_SEARCH_MCTS;
    o->max_nodes = 300000;
    o->max_depth = 64;
    o->playout_depth = 6;
    /* the margin reward (engine v0.84.0): a lead of reward_scale points is worth 0.73. Seeded CPU games end a median
     * 10 to 12 points apart at two seats, 5 to 8 at three, 4 to 6 at four (tests/rosewindow-strength.mjs), about 9 in
     * all, so a fifth of that. Scale 2 and 4 kept Hard as far ahead of Normal over 40 seeds; 1.5 was weaker */
    o->reward = CW_SEARCH_REWARD_MARGIN;
    o->reward_scale = 2.0f;
  }
}

rw_cpu *rw_cpu_new(int players, int level, uint64_t seed) {
  rw_cpu *c = calloc(1, sizeof *c);
  rw_game(&c->g, players);
  c->seed = seed;
  rw_cpu_options(players, level, seed, &c->o);
  return c;
}

void rw_cpu_free(rw_cpu *c) { free(c); }

void rw_cpu_set_reward(rw_cpu *c, cw_search_reward reward, float scale) {
  c->o.reward = reward;
  if (scale > 0) c->o.reward_scale = scale;
}

bk_move rw_cpu_choose(rw_cpu *c, const rw_state *s) {
  cw_search_options o = c->o;
  o.seed = c->seed ^ rw_hash(s);
  cw_search *se = cw_search_new(&c->g, &o);
  bk_move m = BK_MOVE_NONE;
  if (!cw_search_run(se, s, &m) || m == BK_MOVE_NONE) CW_FATAL("rosewindow cpu: the search gave no move");
  cw_search_free(se);
  return m;
}
