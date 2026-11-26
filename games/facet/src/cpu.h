/* Facet CPU: the three levels as search options, tuned for this game (tests/cpu_test.c measures them). */
#ifndef FACET_CPU_H
#define FACET_CPU_H
#include "cartwheel/search.h"
#include "rules.h"
void gm_cpu_options(int level, int players, cw_search_options *o);
/* one search from a settled state (not terminal, not chance); stats optional */
bk_move gm_cpu_move(const bk_game *g, const gm_state *st, int level, uint64_t seed, cw_search_stats *stats);
#endif
