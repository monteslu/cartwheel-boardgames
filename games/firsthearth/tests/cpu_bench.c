/* First Hearth CPU bench: seeded CPU-vs-CPU games between levels, seats rotated every game so the first
 * player's edge cancels out.
 *
 *   firsthearth_cpu_bench <games> <level seat 0> <level seat 1> [<level> <level>]   (0 Easy, 1 Normal, 2 Hard)
 *
 * Prints wins per level, average final scores and rounds, and the search time per decision. Exit 1 when a
 * game breaks (no legal winner). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rules.h"

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: firsthearth_cpu_bench <games> <level> <level> [<level> <level>]\n"); return 2; }
  int games = atoi(argv[1]), players = argc - 2;
  if (players > 4) { fprintf(stderr, "at most 4 seats\n"); return 2; }
  int lv[4];
  for (int i = 0; i < players; i++) lv[i] = atoi(argv[2 + i]);
  bk_game g;
  tr_game(&g, players);
  cw_search *srch[3];
  for (int l = 0; l < 3; l++) {
    cw_search_options o;
    tr_cpu_options(l, 1, &o);
    srch[l] = cw_search_new(&g, &o);
  }
  int wins_by_level[3] = {0}, seats_by_level[3] = {0};
  long score_by_level[3] = {0};
  int rounds = 0, hungry = 0, decisions = 0, cards = 0, builds = 0;
  double think = 0;
  int ends[4] = {0};
  long margin = 0; /* the winner's final score over the best other seat's, summed */
  int margins[200], nm = 0;
  for (int k = 0; k < games; k++) {
    tr_state s;
    tr_init(&s, players, 1000 + (uint64_t)k);
    int seat_level[4];
    for (int i = 0; i < players; i++) seat_level[i] = lv[(i + k) % players]; /* rotate */
    while (!g.terminal(&s, g.user)) {
      if (g.turn(&s, g.user) == BK_CHANCE) { bk_settle(&g, &s, &s.rng, NULL); continue; }
      int seat = g.turn(&s, g.user);
      cw_search *sr = srch[seat_level[seat]];
      double t0 = now_ms();
      /* the search's own seed varies per decision, from the state */
      cw_search_options o;
      tr_cpu_options(seat_level[seat], tr_hash(&s), &o);
      { /* tuning: LV<level>="nodes,playout depth,mistake" overrides that level (empty fields keep it) */
        char key[8], buf[64];
        snprintf(key, sizeof key, "LV%d", seat_level[seat]);
        snprintf(key, sizeof key, "KIND%d", seat_level[seat]);
        if (getenv(key)) o.kind = !strcmp(getenv(key), "minimax") ? CW_SEARCH_MINIMAX : !strcmp(getenv(key), "greedy") ? CW_SEARCH_GREEDY : CW_SEARCH_MCTS;
        snprintf(key, sizeof key, "LV%d", seat_level[seat]);
        if (getenv("RSCALE")) o.reward_scale = (float)atof(getenv("RSCALE")); /* the MCTS margin reward's scale */
        if (getenv(key)) {
          snprintf(buf, sizeof buf, "%s", getenv(key));
          char *a = strtok(buf, ","), *b = strtok(NULL, ","), *c = strtok(NULL, ",");
          if (a && *a) o.max_nodes = atoll(a);
          if (b && *b) o.playout_depth = o.max_depth = atoi(b);
          if (c && *c) o.mistake = (float)atof(c);
        }
      }
      cw_search_free(sr);
      sr = srch[seat_level[seat]] = cw_search_new(&g, &o);
      bk_move m = BK_MOVE_NONE;
      cw_search_run(sr, &s, &m);
      think += now_ms() - t0;
      decisions++;
      if (!bk_is_legal(&g, &s, m)) { fprintf(stderr, "FAIL illegal CPU move in game %d\n", k); return 1; }
      int hb[4];
      for (int i = 0; i < players; i++) hb[i] = s.p[i].hungry;
      if (getenv("TRACE") && bk_move_field(m, 0) == TR_M_PLACE) {
        char lb[64];
        tr_label(&s, m, lb, sizeof lb, NULL);
        printf("  r%d seat %d (kin %d field %d food %d goods %d): %s\n", s.round, seat, s.p[seat].kin, s.p[seat].field,
               s.p[seat].res[0], s.p[seat].res[1] + s.p[seat].res[2] + s.p[seat].res[3] + s.p[seat].res[4], lb);
      }
      g.apply(&s, m, g.user);
      if (getenv("TRACE"))
        for (int i = 0; i < players; i++)
          if (s.p[i].hungry > hb[i]) printf("  HUNGRY seat %d round %d kin %d field %d\n", i, s.round, s.p[i].kin, s.p[i].field);
    }
    int w = tr_winner(&s);
    if (w < 0 || w >= players) { fprintf(stderr, "FAIL no legal winner in game %d\n", k); return 1; }
    wins_by_level[seat_level[w]]++;
    {
      int best = -1000000;
      for (int i = 0; i < players; i++)
        if (i != w && tr_final_score(&s, i) > best) best = tr_final_score(&s, i);
      margin += tr_final_score(&s, w) - best;
      if (nm < 200) margins[nm++] = tr_final_score(&s, w) - best;
    }
    for (int i = 0; i < players; i++) {
      seats_by_level[seat_level[i]]++;
      score_by_level[seat_level[i]] += tr_final_score(&s, i);
      hungry += s.p[i].hungry;
      cards += s.p[i].ncards;
      builds += s.p[i].nbuild;
    }
    rounds += s.round;
    ends[s.end_reason]++;
  }
  static const char *names[3] = {"Easy", "Normal", "Hard"};
  printf("%d players, %d games:", players, games);
  for (int l = 0; l < 3; l++)
    if (seats_by_level[l])
      printf("  %s wins %d/%d seats (%.0f%%), avg %.1f", names[l], wins_by_level[l], seats_by_level[l],
             100.0 * wins_by_level[l] / seats_by_level[l], (double)score_by_level[l] / seats_by_level[l]);
  printf("\n  avg %.1f rounds, %.2f cards and %.2f buildings per seat, hungry %.2f per seat, %.1f ms per decision\n",
         (double)rounds / games, (double)cards / (games * players), (double)builds / (games * players),
         (double)hungry / (games * players), think / decisions);
  printf("  ended by: a pile %d, the cards %d, the moons %d\n", ends[1], ends[2], ends[3]);
  /* the winning margin (the reward_scale in tr_cpu_options is about a fifth of a typical one) */
  for (int i = 1; i < nm; i++)
    for (int j = i; j > 0 && margins[j - 1] > margins[j]; j--) { int t = margins[j]; margins[j] = margins[j - 1]; margins[j - 1] = t; }
  printf("  winning margin over the next seat: mean %.1f, median %d\n", (double)margin / games, nm ? margins[nm / 2] : 0);
  for (int l = 0; l < 3; l++) cw_search_free(srch[l]);
  return 0;
}
