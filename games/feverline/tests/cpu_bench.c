/* Fever Line CPU bench: all-CPU teams over seeded games, the win rate per setup and team size.
 *
 *   feverline_cpu_bench <games> <agents 2..4> <setup 0 Intro, 1 Normal, 2 Heroic> [level 0..2 (1)] [first seed (1000)]
 *                       [map (0 the world; FV_MAPS order)]
 *
 * Every agent is a CPU at the level, planning for the team (fv_search_game), as the cart's CPU seats do: the search
 * runs on node budgets only, so a run is the same on every host. Prints wins, the ways games were lost, average
 * cures, outbreaks and turns, and the search time per decision. Exit 1 when a game breaks. */
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
  if (argc < 4) { fprintf(stderr, "usage: feverline_cpu_bench <games> <agents> <setup> [level] [seed]\n"); return 2; }
  int games = atoi(argv[1]), roles = atoi(argv[2]), setup = atoi(argv[3]), level = argc > 4 ? atoi(argv[4]) : 1;
  uint64_t seed0 = argc > 5 ? strtoull(argv[5], NULL, 10) : 1000;
  int map = argc > 6 ? atoi(argv[6]) : 0;
  if (map < 0 || map >= FV_NMAPS) { fprintf(stderr, "no map %d\n", map); return 2; }
  if (roles < 2 || roles > 4 || setup < 0 || setup > 2 || level < 0 || level > 2) { fprintf(stderr, "out of range\n"); return 2; }
  bk_game rules, team;
  fv_game(&rules, roles);
  fv_search_game(&team);
  int missed_cures = 0;
  int wins = 0, lost[4] = { 0 }, cures = 0, outbreaks = 0, turns = 0, decisions = 0;
  double think = 0;
  for (int k = 0; k < games; k++) {
    fv_state s;
    fv_init(&s, roles, setup, map, seed0 + (uint64_t)k);
    cw_rng chance;
    cw_rng_seed(&chance, bk_seed_mix(seed0 + (uint64_t)k, 0x5eed));
    int guard = 0;
    while (!fv_terminal(&s, NULL)) {
      if (++guard > 5000) { fprintf(stderr, "game %d does not end\n", k); return 1; }
      if (fv_turn(&s, NULL) == BK_CHANCE) {
        int ob = s.outbreaks;
        bk_move last;
        last = 0;
        fv_chance(&s, &chance, NULL);
        if (getenv("FV_BENCH_TRACE") && s.ev[0] == FV_EV_INFECT) printf("    fever: %s +%d (now %d)\n", FV_CITIES[s.ev[1]].name, s.ev[3], s.cubes[s.ev[1]][s.ev[2]]);
        (void)last;
        if (getenv("FV_BENCH_TRACE") && s.ob_n) {
          printf("    OUTBREAK x%d (now %d) from %s:", s.outbreaks - ob, s.outbreaks, s.ev[0] == FV_EV_SURGE ? "surge" : "spread");
          for (int i = 0; i < s.ob_n; i++) printf(" %s", FV_CITIES[s.ob_city[i]].name);
          printf("\n");
        }
        if (getenv("FV_BENCH_TRACE") && s.ev[0] == FV_EV_SURGE && s.ev[1] >= 0) printf("    SURGE at %s\n", FV_CITIES[s.ev[1]].name);
        continue;
      }
      cw_search_options o;
      fv_cpu_options(level, bk_seed_mix(seed0 + (uint64_t)k, (uint64_t)decisions), &o);
      cw_search *sr = cw_search_new(&team, &o);
      bk_move m;
      double t0 = now_ms();
      if (!cw_search_run(sr, &s, &m)) { fprintf(stderr, "search refused\n"); return 1; }
      think += now_ms() - t0;
      if (!bk_is_legal(&rules, &s, m)) {
        char lab[96];
        fv_label(&s, m, lab, sizeof lab, NULL);
        fprintf(stderr, "game %d: the CPU chose an illegal move (%s)\n", k, lab);
        return 1;
      }
      {
        bk_move all[BK_MAX_MOVES];
        int na = fv_moves(&s, all, BK_MAX_MOVES, NULL);
        bool cure = false;
        for (int i = 0; i < na; i++) cure |= bk_move_field(all[i], 0) == FV_M_CURE;
        if (cure && bk_move_field(m, 0) != FV_M_CURE) missed_cures++;
      }
      if (getenv("FV_BENCH_TRACE")) {
        char lab[96];
        fv_label(&s, m, lab, sizeof lab, NULL);
        int cubes = 0;
        for (int c = 0; c < FV_NCOL; c++) cubes += fv_cubes_on_map(&s, c);
        printf("  t%d %s(%d) at %s: %s   [ob %d cubes %d deck %d hand %d]\n", s.turns, fv_role_name(s.role[s.cur]), s.cur, FV_CITIES[s.loc[s.cur]].name, lab, s.outbreaks, cubes, fv_deck_left(&s), fv_hand_count(&s, s.cur));
      }
      if (getenv("FV_BENCH_ROOT")) {
        cw_search_root_move rm[6];
        int nr = cw_search_root_moves(sr, rm, 6);
        for (int i = 0; i < nr; i++) { char l2[96]; fv_label(&s, rm[i].move, l2, sizeof l2, NULL); printf("       %.4f %5d %s\n", rm[i].value, rm[i].visits, l2); }
      }
      cw_search_free(sr);
      fv_apply(&s, m, NULL);
      decisions++;
    }
    if (getenv("FV_BENCH_TRACE")) {
      for (int r = 0; r < roles; r++) { printf("  end hand %s:", fv_role_name(s.role[r])); for (int c = 0; c < FV_N; c++) if (s.pcard[c] == r) printf(" %c", "ETMD"[fv_colour(c)]); printf("\n"); }
      int disc[4] = {0}; for (int c = 0; c < FV_N; c++) if (s.pcard[c] == FV_PC_DISCARD) disc[fv_colour(c)]++;
      printf("  discarded per colour: %d %d %d %d  cured %d %d %d %d\n", disc[0], disc[1], disc[2], disc[3], s.cured[0], s.cured[1], s.cured[2], s.cured[3]);
    }
    int c = 0;
    for (int i = 0; i < FV_NCOL; i++) c += s.cured[i] != 0;
    if (s.result == FV_WON) wins++;
    else lost[s.lose_reason]++;
    cures += c;
    outbreaks += s.outbreaks;
    turns += s.turns;
    if (getenv("FV_BENCH_VERBOSE"))
      printf("game %d: %s cures %d outbreaks %d turns %d\n", k, s.result == FV_WON ? "won " : "lost", c, s.outbreaks, s.turns);
  }
  static const char *const SETUP[3] = { "Intro", "Normal", "Heroic" };
  printf("%s agents %d %s level %d: won %d of %d (%.0f%%)  lost by outbreaks %d, cubes %d, deck %d  avg cures %.2f outbreaks %.2f turns %.1f  %.1f ms a decision\n",
         FV_MAPS[map].key, roles, SETUP[setup], level, wins, games, 100.0 * wins / games, lost[FV_LOSE_OUTBREAKS], lost[FV_LOSE_CUBES], lost[FV_LOSE_DECK],
         (double)cures / games, (double)outbreaks / games, (double)turns / games, think / (decisions ? decisions : 1));
  if (missed_cures) printf("missed cures: %d\n", missed_cures);
  return 0;
}
