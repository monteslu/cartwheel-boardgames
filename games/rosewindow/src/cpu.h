/* Rose Window CPU seats: the search options per level (the kit's bk_cpu runs them in the cart), and a whole-decision
 * chooser for the headless tests. */
#ifndef ROSEWINDOW_CPU_H
#define ROSEWINDOW_CPU_H

#include "rules.h"

typedef struct rw_cpu rw_cpu;

/* level 0 Easy, 1 Normal, 2 Hard */
void rw_cpu_options(int players, int level, uint64_t seed, cw_search_options *o);
rw_cpu *rw_cpu_new(int players, int level, uint64_t seed);
void rw_cpu_free(rw_cpu *c);
bk_move rw_cpu_choose(rw_cpu *c, const rw_state *s);
/* the headless bench only: another MCTS reward and scale for this CPU (a level that is not MCTS ignores them) */
void rw_cpu_set_reward(rw_cpu *c, cw_search_reward reward, float scale);

#endif
