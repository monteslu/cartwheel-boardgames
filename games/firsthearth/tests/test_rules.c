/* First Hearth rules tests: headless, seeded. Exit 0 = all pass. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"

#include "rules.h"

static int fails = 0;
#define CONTROL(c, ...) do { if (c) { printf("CONTROL "); printf(__VA_ARGS__); printf(": caught\n"); } else { fails++; fprintf(stderr, "FAIL CONTROL not caught: "); fprintf(stderr, __VA_ARGS__); fputc(10, stderr); } } while (0)
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static const int START[TR_NRES] = {0, 28, 18, 12, 10};

/* goods are conserved: every wood, clay, stone, gold is in the supply or with a player. Returns 0 or the
 * first resource that is off. */
static int conservation_error(const tr_state *s) {
  for (int r = TR_WOOD; r <= TR_GOLD; r++) {
    int t = s->supply[r];
    for (int i = 0; i < s->players; i++) t += s->p[i].res[r];
    if (t != START[r]) return r;
  }
  return 0;
}

static const char *invariant_error(const tr_state *s) {
  if (conservation_error(s)) return "goods not conserved";
  for (int i = 0; i < s->players; i++) {
    const tr_player *p = &s->p[i];
    if (p->kin < TR_START_KIN || p->kin > TR_MAX_KIN) return "kin out of range";
    if (p->field < 0 || p->field > TR_MAX_FIELD) return "field out of range";
    if (p->ntools > 3) return "too many tools";
    for (int t = 0; t < p->ntools; t++)
      if (p->tools[t] < 1 || p->tools[t] > 4) return "tool strength out of range";
    for (int r = 0; r < TR_NRES; r++)
      if (p->res[r] < 0) return "negative goods";
    if (p->avail < 0 || p->avail > p->kin) return "available kin out of range";
  }
  for (int r = 0; r < TR_NRES; r++)
    if (s->supply[r] < 0) return "negative supply";
  int built = 0;
  for (int i = 0; i < s->players; i++) built += s->p[i].nbuild;
  int pile_left = 0;
  for (int i = 0; i < s->players; i++) pile_left += s->bld_count[i];
  if (built + pile_left != TR_PILE * s->players) return "buildings lost";
  return NULL;
}

typedef bk_move (*policy_fn)(const bk_game *g, const tr_state *s, const bk_move *mv, int n, cw_rng *rng);
static bk_move pol_uniform(const bk_game *g, const tr_state *s, const bk_move *mv, int n, cw_rng *rng) {
  (void)g, (void)s;
  return mv[cw_rng_int(rng, 0, n - 1)];
}
static bk_move pol_playout(const bk_game *g, const tr_state *s, const bk_move *mv, int n, cw_rng *rng) {
  return g->playout(s, mv, n, rng, g->user);
}

typedef struct result { int winner, rounds, moves; uint64_t hash; int scores[4]; bool ok; } result;

static result play(int players, uint64_t seed, policy_fn pol) {
  bk_game g;
  tr_game(&g, players);
  tr_state s;
  tr_init(&s, players, seed);
  cw_rng rng;
  cw_rng_seed(&rng, seed ^ 0xabcdef);
  result r = {0};
  r.ok = true;
  int guard = 0;
  while (!g.terminal(&s, g.user)) {
    if (++guard > 20000) { CHECK(0, "game %d/%llu never ends", players, (unsigned long long)seed); r.ok = false; break; }
    if (g.turn(&s, g.user) == BK_CHANCE) {
      bk_settle(&g, &s, &s.rng, NULL);
      continue;
    }
    bk_move mv[BK_MAX_MOVES];
    int n = g.moves(&s, mv, BK_MAX_MOVES, g.user);
    if (n <= 0) { CHECK(0, "no moves at a decision (phase %d need %d)", s.phase, s.need); r.ok = false; break; }
    bk_move m = pol(&g, &s, mv, n, &rng);
    if (!bk_is_legal(&g, &s, m)) { CHECK(0, "policy picked an illegal move"); r.ok = false; break; }
    g.apply(&s, m, g.user);
    const char *e = invariant_error(&s);
    if (e) { CHECK(0, "invariant: %s (seed %llu)", e, (unsigned long long)seed); r.ok = false; break; }
  }
  r.winner = tr_winner(&s);
  r.rounds = s.round;
  r.moves = s.moves_played;
  r.hash = tr_hash(&s);
  for (int i = 0; i < players; i++) r.scores[i] = tr_final_score(&s, i);
  return r;
}

static void test_setup(void) {
  tr_state s;
  tr_init(&s, 3, 7);
  bk_game g;
  tr_game(&g, 3);
  CHECK(g.turn(&s, NULL) == BK_CHANCE, "a new game deals first");
  bk_settle(&g, &s, &s.rng, NULL);
  CHECK(s.phase == TR_PH_PLACE && s.cur == 0, "after the deal the first player places");
  for (int i = 0; i < 4; i++) CHECK(s.card_slot[i] >= 0, "card slot %d filled", i);
  for (int i = 0; i < 3; i++) CHECK(s.bld_top[i] >= 0, "pile %d has a top", i);
  CHECK(s.bld_top[3] < 0, "pile 4 unused with 3 players");
  CHECK(!tr_site_open(&s, 0, TR_S_BLD3), "pile 4 closed with 3 players");
  /* 3 players: a gathering site takes at most 2 players */
  bk_move m = bk_move_pack(TR_M_PLACE, TR_S_WOOD, 1, 0, 0, 0, 0, 0);
  g.apply(&s, m, NULL);
  g.apply(&s, bk_move_pack(TR_M_PLACE, TR_S_WOOD, 2, 0, 0, 0, 0, 0), NULL);
  CHECK(!tr_site_open(&s, 2, TR_S_WOOD), "third player kept out of the forest with 3 players");
  CHECK(tr_site_open(&s, 2, TR_S_CLAY), "clay bank open");
  /* village: only two of hearth/garden/tool bench */
  g.apply(&s, bk_move_pack(TR_M_PLACE, TR_S_FIELD, 1, 0, 0, 0, 0, 0), NULL); /* seat 2 */
  g.apply(&s, bk_move_pack(TR_M_PLACE, TR_S_TOOL, 1, 0, 0, 0, 0, 0), NULL);  /* seat 0 */
  CHECK(!tr_site_open(&s, 1, TR_S_HUT), "hearth closed once garden and tool bench are taken (3 players)");
  /* control that must fail: 4 players keep the hearth open */
  tr_state s4;
  tr_init(&s4, 4, 7);
  bk_game g4;
  tr_game(&g4, 4);
  bk_settle(&g4, &s4, &s4.rng, NULL);
  g4.apply(&s4, bk_move_pack(TR_M_PLACE, TR_S_FIELD, 1, 0, 0, 0, 0, 0), NULL);
  g4.apply(&s4, bk_move_pack(TR_M_PLACE, TR_S_TOOL, 1, 0, 0, 0, 0, 0), NULL);
  CONTROL(tr_site_open(&s4, 2, TR_S_HUT), "with 4 players the hearth stays open where 3 players close it");
}

static void test_roll_outcomes(void) {
  tr_state s;
  tr_init(&s, 2, 3);
  bk_game g;
  tr_game(&g, 2);
  bk_settle(&g, &s, &s.rng, NULL);
  /* seat 0: 4 kin hunting, seat 1 everything at the quarry etc: drive to the first roll */
  g.apply(&s, bk_move_pack(TR_M_PLACE, TR_S_HUNT, 4, 0, 0, 0, 0, 0), NULL);
  while (g.turn(&s, NULL) != BK_CHANCE) {
    bk_move mv[BK_MAX_MOVES];
    int n = g.moves(&s, mv, BK_MAX_MOVES, NULL);
    g.apply(&s, mv[n - 1], NULL);
  }
  bk_move out[BK_MAX_MOVES];
  uint32_t w[BK_MAX_MOVES];
  int n = g.outcomes(&s, out, w, BK_MAX_MOVES, NULL);
  CHECK(n == 21, "4 dice give 21 sums, got %d", n);
  double sum = 0;
  for (int i = 0; i < n; i++) sum += w[i];
  CHECK(fabs(sum - 1296.0) < 0.5, "4 dice weigh 6^4 = 1296, got %f", sum);
  CHECK(w[10] == 146, "sum 14 has 146 ways, got %u", w[10]);
  /* hunting 14 with no tools gives 7 food */
  g.apply(&s, out[10], NULL);
  CHECK(s.last_event[0] == 1 && s.last_event[1] == TR_S_HUNT && s.last_event[2] == 7 && s.last_event[3] == 0,
        "14 at the hunting grounds is 7 food (event %d %d %d %d)", s.last_event[0], s.last_event[1], s.last_event[2], s.last_event[3]);
}

static void test_tools(void) {
  tr_state s;
  tr_init(&s, 2, 11);
  bk_game g;
  tr_game(&g, 2);
  bk_settle(&g, &s, &s.rng, NULL);
  s.p[0].ntools = 2, s.p[0].tools[0] = 2, s.p[0].tools[1] = 1;
  s.p[0].avail = 2; /* every kin of seat 0 goes to the forest */
  g.apply(&s, bk_move_pack(TR_M_PLACE, TR_S_WOOD, 2, 0, 0, 0, 0, 0), NULL);
  int n;
  while (g.turn(&s, NULL) != BK_CHANCE) {
    bk_move mv[BK_MAX_MOVES];
    n = g.moves(&s, mv, BK_MAX_MOVES, NULL);
    g.apply(&s, mv[0], NULL);
  }
  CHECK(s.step == TR_S_WOOD && s.cur == 0, "seat 0 rolls for the forest first");
  /* roll 8: 2 wood; +1 tool -> 9 = 3 wood; +3 tools -> 11 = 3 (dominated); so options: keep, +1 */
  g.apply(&s, bk_move_pack(TR_M_ROLL, 8, 0, 0, 0, 0, 0, 0), NULL);
  bk_move mv[BK_MAX_MOVES];
  n = g.moves(&s, mv, BK_MAX_MOVES, NULL);
  CHECK(n == 2 && bk_move_field(mv[1], 1) == 2, "roll 8 at the forest offers keep and the 1-tool (got %d moves)", n);
  g.apply(&s, mv[1], NULL);
  CHECK(s.p[0].res[TR_WOOD] == 3 && s.p[0].tool_used == 2, "the 1-tool made 3 wood");
}

static void test_games(void) {
  policy_fn pols[2] = {pol_uniform, pol_playout};
  const char *names[2] = {"uniform", "playout"};
  for (int pl = 2; pl <= 4; pl++)
    for (int k = 0; k < 2; k++) {
      int wins[4] = {0}, rounds = 0, n = 60, hungry = 0;
      long total = 0;
      for (int seed = 1; seed <= n; seed++) {
        result r = play(pl, (uint64_t)seed * 7919 + pl, pols[k]);
        CHECK(r.ok, "game ok");
        CHECK(r.winner >= 0 && r.winner < pl, "legal winner");
        wins[r.winner]++;
        rounds += r.rounds;
        total += r.scores[r.winner];
        (void)hungry;
      }
      printf("%d players, %-7s: %d games, avg %.1f rounds, avg winning score %.1f, wins %d %d %d %d\n", pl, names[k], n,
             rounds / (double)n, total / (double)n, wins[0], wins[1], wins[2], wins[3]);
    }
}

static void test_determinism(void) {
  for (int pl = 2; pl <= 4; pl++) {
    result a = play(pl, 99, pol_playout), b = play(pl, 99, pol_playout), c = play(pl, 100, pol_playout);
    CHECK(a.hash == b.hash, "same seed, same final state (%d players)", pl);
    CONTROL(a.hash != c.hash, "another seed gives another final state (%d players)", pl);
  }
}

static void test_invariant_control(void) {
  tr_state s;
  tr_init(&s, 2, 5);
  CHECK(invariant_error(&s) == NULL, "fresh state passes the invariants");
  s.p[0].res[TR_WOOD] = 1; /* a wood from nowhere: the checker must catch it */
  CONTROL(invariant_error(&s) != NULL, "a forged wood fails the invariants");
}

static void test_scoring(void) {
  tr_state s;
  tr_init(&s, 2, 1);
  tr_player *p = &s.p[0];
  p->marks[0] = 2, p->marks[1] = 1, p->marks[2] = 1; /* sets of 3 and 1: 9 + 1 */
  p->roles[0] = 2, p->field = 3;                       /* planters 2 x 3 */
  p->roles[3] = 1;                                     /* elders 1 x 5 kin */
  p->res[TR_WOOD] = 2, p->res[TR_FOOD] = 9;            /* 2 goods; food scores nothing */
  int parts[4];
  int b = tr_end_bonus(&s, 0, parts);
  CHECK(parts[0] == 2 && parts[1] == 10 && parts[2] == 11 && b == 23, "end bonus 2 + 10 + 11 (got %d %d %d)", parts[0], parts[1], parts[2]);
}

/* CPU against CPU, every player count, every level on some seat: full games end with a legal winner, every
 * CPU move legal, the invariants hold, and the CPU (Normal) beats uniform random play. */
static bk_move pol_cpu(const bk_game *g, const tr_state *s, const bk_move *mv, int n, cw_rng *rng) {
  (void)mv, (void)n;
  static int level = 1;
  cw_search_options o;
  tr_cpu_options(level, cw_rng_u32(rng), &o);
  cw_search *sr = cw_search_new(g, &o);
  bk_move m = BK_MOVE_NONE;
  cw_search_run(sr, s, &m);
  cw_search_free(sr);
  level = (level + 1) % 3;
  return m;
}
static int cpu_vs_uniform_seat;
static bk_move pol_cpu_vs_uniform(const bk_game *g, const tr_state *s, const bk_move *mv, int n, cw_rng *rng) {
  if (s->cur == cpu_vs_uniform_seat) return pol_cpu(g, s, mv, n, rng);
  return pol_uniform(g, s, mv, n, rng);
}

static void test_cpu_games(void) {
  for (int pl = 2; pl <= 4; pl++)
    for (int seed = 1; seed <= 2; seed++) {
      result r = play(pl, 500 + (uint64_t)seed * 31 + pl, pol_cpu);
      CHECK(r.ok && r.winner >= 0 && r.winner < pl, "CPU game %d players seed %d ends with a legal winner", pl, seed);
      printf("CPU game, %d players: winner seat %d, scores %d %d %d %d, %d rounds\n", pl, r.winner, r.scores[0], r.scores[1],
             r.scores[2], r.scores[3], r.rounds);
    }
  int cpu_wins = 0;
  for (int k = 0; k < 6; k++) {
    cpu_vs_uniform_seat = k % 2;
    result r = play(2, 900 + (uint64_t)k, pol_cpu_vs_uniform);
    cpu_wins += r.winner == cpu_vs_uniform_seat;
  }
  CHECK(cpu_wins == 6, "the CPU beats random play every game (%d of 6)", cpu_wins);
  /* CONTROL: random play does not beat random play every game */
  int same = 0;
  for (int k = 0; k < 6; k++) same += play(2, 900 + (uint64_t)k, pol_uniform).winner == k % 2;
  CONTROL(same < 6, "random against random does not win every game for the same seat (%d of 6)", same);
}

int main(void) {
  test_setup();
  test_roll_outcomes();
  test_tools();
  test_scoring();
  test_invariant_control();
  test_determinism();
  test_games();
  test_cpu_games();
  if (fails) { fprintf(stderr, "%d failures\n", fails); return 1; }
  printf("firsthearth rules: all pass\n");
  return 0;
}
