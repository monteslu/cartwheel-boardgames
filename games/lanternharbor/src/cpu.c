/* Lantern Harbor CPU seats. See cpu.h. */
#include "cpu.h"

#include <stdlib.h>

struct hb_cpu {
  bk_game g;
  uint64_t seed;
  cw_search_options o;
};

void hb_cpu_options(int players, int level, uint64_t seed, cw_search_options *o) {
  (void)players;
  if (level < 0 || level > 2) CW_FATAL("harbor cpu: level %d (0..2)", level);
  cw_search_level_options(level, CW_SEARCH_MCTS, o);
  o->seed = seed;
  /* MCTS with playouts that pick plausible moves (rules.c g_playout) for a few rounds' worth of decisions, then the
   * evaluation. Easy thinks little and slips often, Normal thinks more and slips now and then, Hard thinks most */
  static const int64_t nodes[3] = { 6000, 40000, 160000 };
  static const float mistake[3] = { 0.3f, 0.08f, 0 };
  static const int depth[3] = { 16, 30, 40 };
  o->max_nodes = nodes[level];
  o->mistake = mistake[level];
  o->mistake_spread = 3;
  o->playout_depth = depth[level];
  o->uct_c = 1.0f;
  o->tree_nodes = 1 << 15;
  /* the margin reward (engine v0.84.0): a lead of reward_scale points is worth 0.73. Seeded CPU games between the
   * levels end a median 7 points apart (12 at two seats, about 5 at three and four; rules_test bench), so about a
   * fifth of that. Of 1, 2 and 4, scale 2 kept Hard furthest ahead of Normal at three and four seats. */
  o->reward = CW_SEARCH_REWARD_MARGIN;
  o->reward_scale = 2.0f;
}

hb_cpu *hb_cpu_new(int players, int level, uint64_t seed) {
  hb_cpu *c = calloc(1, sizeof *c);
  hb_game(&c->g, players);
  c->seed = seed;
  hb_cpu_options(players, level, seed, &c->o);
  return c;
}

void hb_cpu_free(hb_cpu *c) { free(c); }

void hb_cpu_set_reward(hb_cpu *c, cw_search_reward reward, float scale) {
  c->o.reward = reward;
  c->o.reward_scale = scale;
}

bk_move hb_cpu_choose(hb_cpu *c, const hb_state *s) {
  cw_search_options o = c->o;
  o.seed = c->seed ^ hb_hash(s);
  cw_search *se = cw_search_new(&c->g, &o);
  if (!se) CW_FATAL("harbor cpu: no search");
  bk_move m = BK_MOVE_NONE;
  if (!cw_search_run(se, s, &m) || m == BK_MOVE_NONE) CW_FATAL("harbor cpu: the search gave no move");
  cw_search_free(se);
  return m;
}
