/* Smalti CPU seats: the engine's game-tree search over the rules at Easy / Normal / Hard. */
#ifndef SMALTI_CPU_H
#define SMALTI_CPU_H

#include "cartwheel/search.h"
#include "rules.h"

typedef struct ms_cpu ms_cpu;

/* the search options of a level (0 Easy, 1 Normal, 2 Hard) for this many seats */
void ms_cpu_options(int players, int level, uint64_t seed, cw_search_options *o);
/* a headless CPU (tests): the move it plays at a draft turn */
ms_cpu *ms_cpu_new(int players, int level, uint64_t seed);
void ms_cpu_free(ms_cpu *c);
bk_move ms_cpu_choose(ms_cpu *c, const ms_state *s);

#endif
