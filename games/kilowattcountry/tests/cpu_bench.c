/* Kilowatt Country CPU bench: seeded CPU-vs-CPU games between levels, seats rotated every game so the first
 * seat's edge cancels out.
 *
 *   kilowattcountry_cpu_bench <games> <level seat 0> <level seat 1> [<level> <level>]   (0 Easy, 1 Normal, 2 Hard,
 *                                                                                         9 the playout policy alone)
 * Tuning: LV<level>="nodes,playout depth,mistake,reward scale" overrides a level (reward scale 0: the place reward); SEED=<n> the first game's seed.
 * MARGINS=1 prints each game's winning margin. MAP=<n> plays on that map (kc_maps order, default 0).
 *
 * Prints wins per level, average cities, plants bought, rounds, the step reached, the search time per decision and the
 * winning margin in evaluate units (what reward_scale in kc_cpu_options is set from).
 * Exit 1 when a game breaks (no legal winner, an illegal CPU move). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rules.h"

static int cmp_float(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return (x > y) - (x < y);
}

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: kilowattcountry_cpu_bench <games> <level> <level> [<level> <level>]\n"); return 2; }
  int games = atoi(argv[1]), players = argc - 2;
  if (players > 4) { fprintf(stderr, "at most 4 seats\n"); return 2; }
  int lv[4];
  for (int i = 0; i < players; i++) lv[i] = atoi(argv[2 + i]);
  uint64_t seed0 = getenv("SEED") ? strtoull(getenv("SEED"), NULL, 10) : 1000;
  int map = getenv("MAP") ? atoi(getenv("MAP")) : 0;
  if (map < 0 || map >= KC_NMAPS) { fprintf(stderr, "MAP=%d: no such map (0..%d)\n", map, KC_NMAPS - 1); return 2; }
  bk_game g;
  kc_game(&g, players);
  int wins_by_level[10] = { 0 }, seats_by_level[10] = { 0 };
  long cities_by_level[10] = { 0 }, powered_by_level[10] = { 0 };
  int rounds = 0, decisions = 0, step3 = 0, step2_round = 0, step3_round = 0;
  double think = 0;
  float margins[4096];
  int nmargin = 0;
  for (int k = 0; k < games; k++) {
    kc_state s;
    kc_init(&s, players, map, seed0 + (uint64_t)k);
    cw_rng rng;
    cw_rng_seed(&rng, seed0 * 31 + (uint64_t)k);
    int seat_level[4];
    for (int i = 0; i < players; i++) seat_level[i] = lv[(i + k) % players]; /* rotate */
    int s2 = 0, s3 = 0;
    kc_state last = s; /* the last state before the end: evaluate's heuristic, not the terminal bands */
    while (!g.terminal(&s, g.user)) {
      last = s;
      if (g.turn(&s, g.user) == BK_CHANCE) { bk_settle(&g, &s, &rng, NULL); continue; }
      if (s.step >= 2 && !s2) s2 = s.round;
      if (s.step >= 3 && !s3) s3 = s.round;
      int seat = g.turn(&s, g.user);
      bk_move m = BK_MOVE_NONE;
      double t0 = now_ms();
      if (seat_level[seat] == 9) {
        bk_move mv[BK_MAX_MOVES];
        int n = g.moves(&s, mv, BK_MAX_MOVES, g.user);
        m = g.playout(&s, mv, n, &rng, g.user);
      } else {
        cw_search_options o;
        kc_cpu_options(seat_level[seat], kc_hash(&s), &o);
        char key[8];
        snprintf(key, sizeof key, "LV%d", seat_level[seat]);
        if (getenv(key)) {
          char buf[64];
          snprintf(buf, sizeof buf, "%s", getenv(key));
          char *a = strtok(buf, ","), *b = strtok(NULL, ","), *c = strtok(NULL, ","), *d = strtok(NULL, ",");
          if (a && *a) o.max_nodes = atoll(a);
          if (b && *b) o.playout_depth = atoi(b);
          if (c && *c) o.mistake = (float)atof(c);
          if (d && *d) o.reward_scale = (float)atof(d); /* 0: the place reward */
          if (d && *d && atof(d) == 0) o.reward = CW_SEARCH_REWARD_PLACE;
        }
        cw_search *sr = cw_search_new(&g, &o);
        if (!sr || !cw_search_run(sr, &s, &m)) { fprintf(stderr, "FAIL search\n"); return 1; }
        cw_search_free(sr);
      }
      think += now_ms() - t0;
      decisions++;
      if (!bk_is_legal(&g, &s, m)) { fprintf(stderr, "FAIL illegal CPU move in game %d\n", k); return 1; }
      if (getenv("TRACE")) {
        char lb[64];
        kc_label(&s, m, lb, sizeof lb, NULL);
        printf("  r%d st%d seat %d $%d cities %d cap %d: %s\n", s.round, s.step, seat, s.p[seat].money, s.p[seat].ncity, kc_capacity(&s.p[seat]), lb);
      }
      g.apply(&s, m, g.user);
    }
    int w = kc_winner(&s);
    if (w < 0 || w >= players) { fprintf(stderr, "FAIL no legal winner in game %d\n", k); return 1; }
    {
      float sc[BK_MAX_SEATS];
      g.evaluate(&last, sc, g.user);
      float other = -1e30f;
      for (int i = 0; i < players; i++)
        if (i != w && sc[i] > other) other = sc[i];
      if (nmargin < 4096) margins[nmargin++] = sc[w] - other;
      if (getenv("MARGINS")) printf("margin %.1f\n", sc[w] - other);
    }
    wins_by_level[seat_level[w]]++;
    for (int i = 0; i < players; i++) {
      seats_by_level[seat_level[i]]++;
      cities_by_level[seat_level[i]] += s.p[i].ncity;
      powered_by_level[seat_level[i]] += s.p[i].powered;
    }
    if (getenv("TRACE")) {
      printf("game %d over round %d:", k, s.round);
      for (int i = 0; i < players; i++) printf("  seat %d L%d cities %d powered %d $%d", i, seat_level[i], s.p[i].ncity, s.p[i].powered, s.p[i].money);
      printf("\n");
    }
    rounds += s.round;
    step3 += s.step == 3;
    step2_round += s2;
    step3_round += s3;
  }
  static const char *names[10] = { "Easy", "Normal", "Hard", "", "", "", "", "", "", "Playout" };
  printf("%s, %d players, %d games:", kc_maps[map].title, players, games);
  for (int l = 0; l < 10; l++)
    if (seats_by_level[l])
      printf("  %s wins %d/%d seats (%.0f%%), %.1f cities %.1f lit", names[l], wins_by_level[l], seats_by_level[l],
             100.0 * wins_by_level[l] / seats_by_level[l], (double)cities_by_level[l] / seats_by_level[l],
             (double)powered_by_level[l] / seats_by_level[l]);
  printf("\n  %.1f rounds, step 2 from round %.1f, step 3 in %d games (from round %.1f), %.1f ms per decision\n", (double)rounds / games,
         (double)step2_round / games, step3, step3 ? (double)step3_round / step3 : 0.0, think / decisions);
  if (nmargin) {
    /* the winner's lead over the best other seat in evaluate's units, on the last state before the end: the margin
     * the MCTS reward_scale is set from (about a fifth of a typical one) */
    qsort(margins, (size_t)nmargin, sizeof margins[0], cmp_float);
    double sum = 0;
    for (int i = 0; i < nmargin; i++) sum += margins[i];
    printf("  winning margin in evaluate units: median %.1f, quartiles %.1f to %.1f, mean %.1f\n", margins[nmargin / 2],
           margins[nmargin / 4], margins[3 * nmargin / 4], sum / nmargin);
  }
  return 0;
}
