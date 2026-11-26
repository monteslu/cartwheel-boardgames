/* Facet CPU levels: full CPU-vs-CPU games at every player count end with a legal winner, and the levels are
 * ordered (Hard beats Normal beats Easy over seeded games, seats swapped). Prints the win rates. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"
#include "cpu.h"
#include "rules.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
/* a control that must fail: the check holds when the broken case is refused */
#define CONTROL(cond, ...) do { CHECK(cond, __VA_ARGS__); if (cond) { printf("CONTROL "); printf(__VA_ARGS__); printf(" caught\n"); } } while (0)

typedef struct { int winners, turns; uint64_t hash; long long nodes; } result;

static result play(int players, uint64_t seed, const int level[BK_MAX_SEATS]) {
  bk_game g;
  gm_rules(&g, players);
  gm_state st;
  gm_new(&st, players, seed);
  result r = {0, 0, 0, 0};
  int steps = 0;
  while (!gm_terminal(&st, NULL)) {
    bk_settle(&g, &st, &st.rng, NULL);
    if (gm_terminal(&st, NULL)) break;
    cw_search_stats stats;
    bk_move m = gm_cpu_move(&g, &st, level[st.seat], seed * 1000003u + (uint64_t)steps, &stats);
    r.nodes += stats.nodes;
    if (!bk_is_legal(&g, &st, m)) { CHECK(0, "CPU picked an illegal move"); break; }
    gm_apply(&st, m, NULL);
    if (++steps > 5000) { CHECK(0, "game did not end"); break; }
  }
  r.winners = st.winners;
  r.turns = st.turns;
  r.hash = bk_hash(&g, &st);
  return r;
}

int main(int argc, char **argv) {
  int n = argc > 1 ? atoi(argv[1]) : 20;
  /* every player count, every level: a full game ends with a legal winner, on the real end condition */
  for (int players = 2; players <= 4; players++)
    for (int lv = 0; lv < 3; lv++) {
      int levels[4] = {lv, lv, lv, lv};
      result r = play(players, 1000 + players * 10 + lv, levels);
      CHECK(r.winners && r.winners < (1 << players) && r.turns < GM_MAX_TURNS, "%dp level %d: winners %d turns %d", players, lv, r.winners, r.turns);
      printf("%dp all level %d: winners %d, %d turns, %lld nodes\n", players, lv, r.winners, r.turns, r.nodes);
    }
  /* head to head, seats swapped every game; a shared win counts half */
  static const int pairs[3][2] = {{1, 0}, {2, 1}, {2, 0}};
  static const char *names[3] = {"Easy", "Normal", "Hard"};
  for (int k = 0; k < 3 && n > 0; k++) { /* n 0: the full games and determinism only */
    float wins = 0;
    for (int i = 0; i < n; i++) {
      int a = i & 1; /* the stronger level's seat */
      int levels[4];
      levels[a] = pairs[k][0];
      levels[1 - a] = pairs[k][1];
      result r = play(2, 5000 + (uint64_t)i, levels);
      if (r.winners == (1 << a)) wins += 1;
      else if (r.winners == 3) wins += 0.5f;
    }
    printf("%s vs %s: %.1f/%d (%.0f%%)\n", names[pairs[k][0]], names[pairs[k][1]], wins, n, 100.0f * wins / n);
    CHECK(wins > n * 0.55f, "%s should beat %s", names[pairs[k][0]], names[pairs[k][1]]);
  }
  /* determinism, with a control that must differ */
  int lv[4] = {1, 1, 1, 1};
  result a = play(3, 77, lv), b = play(3, 77, lv), c = play(3, 78, lv);
  CHECK(a.hash == b.hash, "same seed, same CPU game");
  CONTROL(a.hash != c.hash, "another seed differs");
  if (fails) { fprintf(stderr, "%d checks failed\n", fails); return 1; }
  printf("facet cpu: all checks pass\n");
  return 0;
}
