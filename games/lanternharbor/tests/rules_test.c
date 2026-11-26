/* Lantern Harbor rules tests: production, sales, loading and spoilage, end bonuses, conservation of every piece,
 * determinism, random and CPU full games at 2, 3 and 4 seats, and the CPU levels' strength. Every check has a
 * control that must fail.
 *
 *   harbor_rules_test             all tests
 *   harbor_rules_test strength N  also Easy / Normal / Hard win rates over N seeded games per pairing
 *   harbor_rules_test bench P N S [place | margin X]
 *                                 N seeded CPU games at P seats from seed S, the levels seated in turn, one line each
 *                                 (levels, scores, winners, winning margin); the reward and scale may be overridden */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"
#include "cpu.h"
#include "rules.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define CONTROL(caught, name) do { if (caught) printf("CONTROL %s caught\n", name); else { failures++; fprintf(stderr, "FAIL control %s was not caught\n", name); } } while (0)

/* the totals a fresh game holds, to compare every later state with */
typedef struct totals { int folk, goods[HB_GOODS], plots[HB_GOODS], stones, bld[HB_BUILDINGS], lanterns; } totals;

static void count(const hb_state *s, totals *t) {
  memset(t, 0, sizeof *t);
  t->folk = hb_folk_total(s);
  t->stones = s->stones;
  t->lanterns = s->lantern_supply;
  for (int g = 0; g < HB_GOODS; g++) {
    t->goods[g] = hb_goods_total(s, g);
    t->plots[g] = s->plot_pile[g] + s->plot_discard[g];
    for (int i = 0; i < HB_ROW; i++) t->plots[g] += s->row[i] == g;
  }
  for (int b = 0; b < HB_BUILDINGS; b++) t->bld[b] = s->bld_supply[b];
  for (int i = 0; i < s->players; i++) {
    const hb_player *pl = &s->p[i];
    t->lanterns += pl->lanterns;
    for (int k = 0; k < pl->nplots; k++) {
      if (pl->plot[k] == HB_STONE) t->stones++;
      else t->plots[pl->plot[k]]++;
    }
    for (int b = 0; b < HB_BUILDINGS; b++) t->bld[b] += hb_owns(pl, b);
  }
}

static const char *invariants(const hb_state *s, const totals *want) {
  totals t;
  count(s, &t);
  if (t.folk != want->folk) return "townsfolk are not all accounted for";
  if (t.stones != want->stones) return "stone pits are not all accounted for";
  if (t.lanterns != want->lanterns) return "lanterns are not all accounted for";
  for (int g = 0; g < HB_GOODS; g++) {
    if (t.goods[g] != want->goods[g]) return "goods are not all accounted for";
    if (t.plots[g] != want->plots[g]) return "plots are not all accounted for";
  }
  for (int b = 0; b < HB_BUILDINGS; b++)
    if (t.bld[b] != want->bld[b]) return "buildings are not all accounted for";
  for (int i = 0; i < s->players; i++) {
    const hb_player *pl = &s->p[i];
    if (pl->nplots > HB_ISLAND) return "hillside over twelve plots";
    if (hb_town_lots(pl) > HB_TOWN) return "town over twelve lots";
    for (int k = 0; k < pl->nbld; k++)
      if (pl->staff[pl->bld[k]] > hb_bld[pl->bld[k]].slots) return "building over its places";
    for (int b = 0; b < HB_BUILDINGS; b++)
      if (!hb_owns(pl, b) && pl->staff[b]) return "workers in a building not raised";
  }
  for (int k = 0; k < s->nships; k++) {
    if (s->ship_load[k] > s->ship_cap[k]) return "boat over its hold";
    for (int j = k + 1; j < s->nships; j++)
      if (s->ship_load[k] && s->ship_load[j] && s->ship_good[k] == s->ship_good[j]) return "two boats carry one good";
  }
  if (s->nmarket > HB_MARKET) return "market over four";
  return NULL;
}

static bool listed(const hb_state *s, bk_move m) {
  bk_move mv[BK_MAX_MOVES];
  int n = hb_moves(s, mv, BK_MAX_MOVES);
  for (int i = 0; i < n; i++)
    if (mv[i] == m) return true;
  return false;
}

/* a state at the start of a skipper turn for seat 0 */
static void ship_state(hb_state *s) {
  hb_init(s, 3, 1);
  cw_rng r;
  cw_rng_seed(&r, 1);
  hb_chance(s, &r);
  s->phase = PH_SHIP;
  s->role = R_SKIPPER;
  s->picker = 0;
  s->actor = 0;
  s->role_taken[R_SKIPPER] = 1;
  s->picks = 0;
}

static void test_units(void) {
  /* production: crops need a staffed workshop, rice does not */
  hb_player pl;
  memset(&pl, 0, sizeof pl);
  pl.nplots = 3;
  pl.plot[0] = G_INDIGO, pl.plot[1] = G_INDIGO, pl.plot[2] = G_RICE;
  pl.plot_staffed[0] = pl.plot_staffed[1] = pl.plot_staffed[2] = 1;
  CHECK(hb_production(&pl, G_INDIGO) == 0 && hb_production(&pl, G_RICE) == 1, "no workshop, no dye; rice grows");
  pl.bld[pl.nbld++] = B_DYE_SHED;
  pl.built |= 1u << B_DYE_SHED;
  pl.staff[B_DYE_SHED] = 1;
  CHECK(hb_production(&pl, G_INDIGO) == 1, "a dye shed with one worker makes 1 of 2 crops");
  pl.plot_staffed[1] = 0;
  pl.bld[pl.nbld++] = B_DYE_WORKS;
  pl.built |= 1u << B_DYE_WORKS;
  pl.staff[B_DYE_WORKS] = 3;
  CHECK(hb_production(&pl, G_INDIGO) == 1, "one staffed crop limits production");
  CONTROL(hb_production(&pl, G_INDIGO) != 2, "an unstaffed plot does not produce");

  /* the guildhall's bonus counts small and large workshops, only when staffed */
  pl.bld[pl.nbld++] = B_GUILDHALL;
  pl.built |= 1u << B_GUILDHALL;
  CHECK(hb_bonus(&pl, B_GUILDHALL) == 0, "an empty guildhall scores nothing");
  pl.staff[B_GUILDHALL] = 1;
  CHECK(hb_bonus(&pl, B_GUILDHALL) == 3, "guildhall: one small and one large workshop is 3 (got %d)", hb_bonus(&pl, B_GUILDHALL));
  CHECK(hb_score(&pl) == 1 + 2 + 4 + 3, "score: values 1 + 2 + 4 and the bonus 3 (got %d)", hb_score(&pl));

  /* loading: the boat taking the most, never a good another boat carries, and a player who can load must */
  hb_state s;
  ship_state(&s);
  s.p[0].goods[G_TEA] = 3;
  s.ship_good[1] = G_INDIGO, s.ship_load[1] = 2;
  CHECK(hb_load_amount(&s, 0, G_TEA, 0) == 3 && hb_load_amount(&s, 0, G_TEA, 2) == 3, "3 tea fit either empty boat");
  CHECK(hb_load_amount(&s, 0, G_TEA, 1) == 0, "tea cannot join indigo");
  CHECK(!listed(&s, hb_move(M_PASS, 0, 0)), "a player who can load may not pass");
  s.p[0].goods[G_TEA] = 5;
  CHECK(hb_load_amount(&s, 0, G_TEA, 0) == 0 && hb_load_amount(&s, 0, G_TEA, 2) == 5, "5 tea: only the boat taking all 5");
  s.p[0].goods[G_INDIGO] = 4;
  CHECK(hb_load_amount(&s, 0, G_INDIGO, 1) == 3 && hb_load_amount(&s, 0, G_INDIGO, 0) == 0, "indigo goes where indigo is");
  CONTROL(!listed(&s, hb_move(M_LOAD, G_TEA, 1)), "a load onto another good's boat is not listed");

  /* shipping and spoilage: lanterns per good plus the skipper's one; a player keeps one good of the dearest kind */
  ship_state(&s);
  for (int i = 0; i < 3; i++) memset(s.p[i].goods, 0, sizeof s.p[i].goods);
  for (int g = 0; g < HB_GOODS; g++) s.goods_supply[g] = 5;
  s.p[0].goods[G_RICE] = 3;
  s.p[0].goods[G_TEA] = 2;
  int before = s.p[0].lanterns, sup = s.lantern_supply;
  hb_apply(&s, hb_move(M_LOAD, G_RICE, 0));
  CHECK(s.p[0].lanterns == before + 4 && s.lantern_supply == sup - 4, "3 rice shipped by the skipper: 4 lanterns");
  /* next turn seat 0 again (others have nothing): the tea */
  CHECK(s.phase == PH_SHIP && s.actor == 0, "the skipper loads again while able");
  hb_apply(&s, hb_move(M_LOAD, G_TEA, 1));
  CHECK(s.p[0].lanterns == before + 6, "the extra lantern once a job");
  CHECK(s.phase == PH_PICK && s.p[0].goods[G_TEA] == 0 && s.p[0].goods[G_RICE] == 0, "all shipped, nothing to spoil");
  ship_state(&s);
  for (int i = 0; i < 3; i++) memset(s.p[i].goods, 0, sizeof s.p[i].goods);
  for (int g = 0; g < HB_GOODS; g++) s.goods_supply[g] = 5;
  s.ship_good[0] = G_RICE, s.ship_load[0] = 3;
  s.ship_good[1] = G_INDIGO, s.ship_load[1] = 4;
  s.ship_good[2] = G_GLASS, s.ship_load[2] = 5;
  s.p[1].goods[G_TEA] = 2;
  s.p[1].goods[G_SPICE] = 1;
  s.p[1].goods[G_RICE] = 2;
  CHECK(listed(&s, hb_move(M_PASS, 0, 0)), "a skipper with nothing to load passes");
  hb_apply(&s, hb_move(M_PASS, 0, 0));
  CHECK(s.actor == 1 && hb_load_amount(&s, 1, G_RICE, 0) == 1, "seat 2 must top up the rice boat");
  hb_apply(&s, hb_move(M_LOAD, G_RICE, 0));
  CHECK(s.phase == PH_PICK, "the skipper phase ends when nobody can load");
  CHECK(s.p[1].goods[G_SPICE] == 1 && s.p[1].goods[G_TEA] == 0 && s.p[1].goods[G_RICE] == 0,
        "spoilage keeps one spice (got rice %d tea %d spice %d)", s.p[1].goods[G_RICE], s.p[1].goods[G_TEA], s.p[1].goods[G_SPICE]);
  CHECK(s.ship_load[0] == 0 && s.ship_load[1] == 4 && s.ship_load[2] == 5, "only the full boat unloads");
  CONTROL(s.p[1].goods[G_TEA] != 2, "spoilage takes the tea");
}

typedef bk_move (*chooser)(const hb_state *s, int seat, void *user);

static bk_move choose_random(const hb_state *s, int seat, void *user) {
  (void)seat;
  bk_move mv[BK_MAX_MOVES];
  int n = hb_moves(s, mv, BK_MAX_MOVES);
  return mv[cw_rng_int(user, 0, n - 1)];
}

static int game_moves;

static unsigned play(int players, uint64_t seed, chooser choose[BK_MAX_SEATS], void *user[BK_MAX_SEATS], int *rounds, uint64_t *hash,
                     int *scores) {
  hb_state s;
  hb_init(&s, players, seed);
  bk_game g;
  hb_game(&g, players);
  totals want;
  count(&s, &want);
  int steps = 0;
  while (!hb_terminal(&s)) {
    if (hb_turn(&s) == BK_CHANCE) { bk_settle(&g, &s, &s.rng, NULL); continue; }
    int seat = hb_turn(&s);
    bk_move m = choose[seat](&s, seat, user[seat]);
    if (!bk_is_legal(&g, &s, m)) { CHECK(0, "seat %d chose an illegal move %llx", seat, (unsigned long long)m); break; }
    hb_apply(&s, m);
    const char *why = invariants(&s, &want);
    if (why) { CHECK(0, "players %d seed %llu step %d: %s", players, (unsigned long long)seed, steps, why); break; }
    if (++steps > 20000) { CHECK(0, "game did not end"); break; }
  }
  game_moves = steps;
  if (rounds) *rounds = s.round;
  if (hash) *hash = hb_hash(&s);
  if (scores) for (int i = 0; i < players; i++) scores[i] = hb_score(&s.p[i]);
  CHECK(!s.ended_by_cap, "players %d seed %llu ended by the round cap", players, (unsigned long long)seed);
  unsigned w = hb_winners(&s);
  CHECK(w != 0 && w < (1u << players), "no legal winner");
  return w;
}

static void test_random_games(void) {
  for (int players = 2; players <= 4; players++) {
    int total_rounds = 0;
    for (uint64_t seed = 1; seed <= 150; seed++) {
      cw_rng r;
      chooser ch[4] = { choose_random, choose_random, choose_random, choose_random };
      void *u[4] = { &r, &r, &r, &r };
      int rounds;
      uint64_t h1, h2;
      cw_rng_seed(&r, seed * 77);
      play(players, seed, ch, u, &rounds, &h1, NULL);
      total_rounds += rounds;
      cw_rng_seed(&r, seed * 77);
      play(players, seed, ch, u, NULL, &h2, NULL);
      CHECK(h1 == h2, "replay of players %d seed %llu differs", players, (unsigned long long)seed);
    }
    fprintf(stderr, "  random play, %d seats: %.1f rounds a game\n", players, total_rounds / 150.0);
  }
  cw_rng r;
  chooser ch[4] = { choose_random, choose_random, choose_random, choose_random };
  void *u[4] = { &r, &r, &r, &r };
  uint64_t a, b;
  cw_rng_seed(&r, 1); play(3, 1, ch, u, NULL, &a, NULL);
  cw_rng_seed(&r, 1); play(3, 2, ch, u, NULL, &b, NULL);
  CONTROL(a != b, "another seed gives another game");
  /* the conservation check catches a lost townsperson */
  hb_state s;
  hb_init(&s, 4, 3);
  totals want;
  count(&s, &want);
  s.folk_supply--;
  CONTROL(invariants(&s, &want) != NULL, "a lost townsperson breaks the count");
}

static bk_move choose_cpu(const hb_state *s, int seat, void *user) {
  (void)seat;
  return hb_cpu_choose(user, s);
}

static unsigned cpu_game(int players, uint64_t seed, const int *levels, int *rounds, int *scores) {
  hb_cpu *cpus[4];
  chooser ch[4];
  void *u[4];
  for (int i = 0; i < players; i++) {
    cpus[i] = hb_cpu_new(players, levels[i], seed * 131 + (uint64_t)i);
    ch[i] = choose_cpu;
    u[i] = cpus[i];
  }
  unsigned w = play(players, seed, ch, u, rounds, NULL, scores);
  for (int i = 0; i < players; i++) hb_cpu_free(cpus[i]);
  return w;
}

static void test_cpu_games(void) {
  for (int players = 2; players <= 4; players++)
    for (int level = 0; level <= 2; level += 2) {
      int levels[4] = { level, level, level, level };
      int rounds = 0, sc[4] = { 0 };
      unsigned w = cpu_game(players, 10 + (uint64_t)players, levels, &rounds, sc);
      CHECK(rounds >= 8 && rounds <= 30, "cpu game %d seats level %d took %d rounds", players, level, rounds);
      fprintf(stderr, "  cpu %d seats level %d: %d rounds, %d moves, winners %x, scores %d %d %d %d\n", players, level, rounds, game_moves, w,
              sc[0], sc[1], sc[2], sc[3]);
    }
}

/* a vs b over n seeds, each seed twice with seats swapped, in games of `players` seats (the others play level b);
 * returns a's wins (shared wins count as a share) */
static float duel(int players, int a, int b, int n, float *avg_a, float *avg_b) {
  float wins = 0;
  int sa = 0, sb = 0, games = 0;
  for (int i = 0; i < n; i++)
    for (int swap = 0; swap < players; swap++) {
      int levels[4] = { b, b, b, b };
      levels[swap] = a;
      int sc[4];
      unsigned w = cpu_game(players, 5000 + (uint64_t)i, levels, NULL, sc);
      sa += sc[swap];
      for (int k = 0; k < players; k++)
        if (k != swap) sb += sc[k];
      int nw = 0;
      for (int k = 0; k < players; k++) nw += (w >> k) & 1u;
      if ((w >> swap) & 1u) wins += 1.0f / (float)nw;
      games++;
      if (swap == 1 && players > 2) break; /* two seats of the others' order are enough */
    }
  *avg_a = (float)sa / (float)games;
  *avg_b = (float)sb / (float)(games * (players - 1));
  return wins / (float)games;
}

/* bench <players> <games> <first seed> [place|margin <scale>]: Easy, Normal and Hard seated in turn (two seats: each
 * pairing both ways round; three: one of each, rotated; four: Hard, Normal, Normal, Easy, rotated), one line per
 * game with each seat's level and score, and the winning margin over the next seat */
static int bench(int players, int n, uint64_t seed0, int argc, char **argv) {
  cw_search_reward rw = CW_SEARCH_REWARD_DEFAULT;
  float scale = 0;
  if (argc > 0) {
    if (!strcmp(argv[0], "place")) rw = CW_SEARCH_REWARD_PLACE;
    else if (!strcmp(argv[0], "margin") && argc > 1) rw = CW_SEARCH_REWARD_MARGIN, scale = (float)atof(argv[1]);
    else { fprintf(stderr, "bench: reward is place or margin <scale>\n"); return 2; }
  }
  static const int PAIRS[6][2] = { { 0, 1 }, { 1, 0 }, { 0, 2 }, { 2, 0 }, { 1, 2 }, { 2, 1 } };
  static const int THREE[6][3] = { { 0, 1, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 0, 2, 1 }, { 2, 1, 0 }, { 1, 0, 2 } };
  static const int FOUR[4] = { 2, 1, 1, 0 };
  for (int i = 0; i < n; i++) {
    int lv[4];
    for (int k = 0; k < players; k++)
      lv[k] = players == 2 ? PAIRS[i % 6][k] : players == 3 ? THREE[i % 6][k] : FOUR[(k + i) % 4];
    uint64_t seed = seed0 + (uint64_t)i;
    hb_cpu *cpus[4];
    chooser ch[4];
    void *u[4];
    for (int k = 0; k < players; k++) {
      cpus[k] = hb_cpu_new(players, lv[k], seed * 131 + (uint64_t)k);
      if (argc > 0) hb_cpu_set_reward(cpus[k], rw, scale > 0 ? scale : 4);
      ch[k] = choose_cpu, u[k] = cpus[k];
    }
    int sc[4] = { 0 }, rounds = 0;
    unsigned w = play(players, seed, ch, u, &rounds, NULL, sc);
    for (int k = 0; k < players; k++) hb_cpu_free(cpus[k]);
    int best = -1000, next = -1000;
    for (int k = 0; k < players; k++) {
      if (sc[k] > best) next = best, best = sc[k];
      else if (sc[k] > next) next = sc[k];
    }
    printf("game seed %llu players %d levels", (unsigned long long)seed, players);
    for (int k = 0; k < players; k++) printf(" %d", lv[k]);
    printf(" scores");
    for (int k = 0; k < players; k++) printf(" %d", sc[k]);
    printf(" winners %u margin %d rounds %d\n", w, best - next, rounds);
    fflush(stdout);
  }
  return failures ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc > 4 && !strcmp(argv[1], "bench")) return bench(atoi(argv[2]), atoi(argv[3]), strtoull(argv[4], NULL, 10), argc - 5, argv + 5);
  test_units();
  test_random_games();
  test_cpu_games();
  if (argc > 2 && !strcmp(argv[1], "strength")) {
    int n = atoi(argv[2]);
    float x[6];
    float ne = duel(2, 1, 0, n, &x[0], &x[1]), he = duel(2, 2, 0, n, &x[2], &x[3]), hn = duel(2, 2, 1, n, &x[4], &x[5]);
    printf("strength, two seats, %d seeds x2 seats (win rate, mean scores):\n  Normal vs Easy %.2f (%.1f vs %.1f)\n  Hard vs Easy %.2f (%.1f vs %.1f)\n  Hard vs Normal %.2f (%.1f vs %.1f)\n",
           n, ne, x[0], x[1], he, x[2], x[3], hn, x[4], x[5]);
    CHECK(ne > 0.55f && he > 0.65f && hn > 0.52f, "levels are not ordered");
    float h4 = duel(4, 2, 0, (n + 1) / 2, &x[0], &x[1]);
    printf("  four seats, Hard against three Easy: Hard wins %.2f (an even share is 0.25; %.1f vs %.1f)\n", h4, x[0], x[1]);
    CHECK(h4 > 0.4f, "Hard does not lead a four-seat table of Easy");
    float ee = duel(2, 1, 1, n, &x[0], &x[1]);
    printf("  Normal vs Normal %.2f (the control: one level against itself is even)\n", ee);
    CONTROL(!(ee > 0.55f), "the ordering check fails a level against itself");
  }
  if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
  printf("lanternharbor rules: ok\n");
  return 0;
}
