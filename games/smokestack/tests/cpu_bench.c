/* Smokestack CPU levels against each other over seeded games, as the game runs them (rl_cpu_options, the
 * search seeded per decision from the state hash). Seats swap each game so no level keeps the first seat.
 *
 *   smokestack_cpu_bench <levelA> <levelB> <games> [players]
 * prints the wins of A and B (a draw-free game: ties break on money, then turn order). */
#include <stdio.h>
#include <stdlib.h>

#include "boardkit/bk_game.h"
#include "boardkit/bk_match.h"
#include "rules.h"

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: smokestack_cpu_bench <levelA> <levelB> <games> [players]\n"); return 2; }
  int la = atoi(argv[1]), lb = atoi(argv[2]), games = atoi(argv[3]), players = argc > 4 ? atoi(argv[4]) : 2;
  int wa = 0, wb = 0, sa = 0, sb = 0;
  for (int gi = 0; gi < games; gi++) {
    int lv[4];
    for (int i = 0; i < players; i++) lv[i] = ((i + gi) % 2 == 0) ? la : lb; /* A on even seats, swapping */
    rl_state s0;
    rl_setup(&s0, players);
    const bk_game *g = &RL_CPU_RULES[players];
    bk_match *m = bk_match_new(g, &s0, 5000 + (uint64_t)gi);
    while (!bk_match_over(m)) {
      int seat = bk_match_turn(m);
      cw_search_options o;
      rl_cpu_options(lv[seat], &o);
      o.seed = bk_seed_mix(0xbe5cu ^ (uint64_t)seat ^ (uint64_t)gi << 8, bk_match_hash(m));
      cw_search *se = cw_search_new(g, &o);
      bk_move mv;
      if (!cw_search_run(se, bk_match_state(m), &mv)) CW_FATAL("bench: search refused");
      cw_search_free(se);
      if (!bk_match_play(m, mv)) CW_FATAL("bench: the search's move was refused");
    }
    const rl_state *st = bk_match_state(m);
    int w = rl_winner(st);
    if (lv[w] == la && la != lb) wa++;
    else if (lv[w] == lb && la != lb) wb++;
    else if ((w + gi) % 2 == 0) wa++;
    else wb++;
    for (int i = 0; i < players; i++) {
      if (((i + gi) % 2) == 0) sa += rl_final_score(st, i);
      else sb += rl_final_score(st, i);
    }
    fprintf(stderr, "game %d: winner seat %d (level %d)", gi, w, lv[w]);
    for (int i = 0; i < players; i++)
      fprintf(stderr, " | L%d pts %d links %d shares %d $%d inc %d eng %d", lv[i], st->p[i].points, rl_links_done(st, i), st->p[i].shares, st->p[i].money, st->p[i].income, st->p[i].engine);
    fprintf(stderr, "\n");
    bk_match_free(m);
  }
  printf("level %d vs level %d, %d players, %d games: %d to %d (mean seat score %.1f vs %.1f)\n", la, lb, players, games, wa, wb,
         (double)sa / games / (players / 2.0), (double)sb / games / ((players + 1) / 2 / 1.0));
  return 0;
}
