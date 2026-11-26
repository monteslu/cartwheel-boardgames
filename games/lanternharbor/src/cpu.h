/* Lantern Harbor CPU seats: the engine's Monte Carlo tree search over the rules at Easy / Normal / Hard. */
#ifndef HARBOR_CPU_H
#define HARBOR_CPU_H

#include "cartwheel/search.h"
#include "rules.h"

typedef struct hb_cpu hb_cpu;

/* the search options of a level (0 Easy, 1 Normal, 2 Hard) */
void hb_cpu_options(int players, int level, uint64_t seed, cw_search_options *o);
/* a headless CPU (tests): the move it plays at a decision */
hb_cpu *hb_cpu_new(int players, int level, uint64_t seed);
void hb_cpu_free(hb_cpu *c);
bk_move hb_cpu_choose(hb_cpu *c, const hb_state *s);
/* the benchmark (tests/rules_test.c bench): another MCTS reward and scale than the level's */
void hb_cpu_set_reward(hb_cpu *c, cw_search_reward reward, float scale);

#endif
