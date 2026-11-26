/* Kilowatt Country rules tests: headless, seeded. Exit 0 = all pass. Each part has a control that must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"

#include "rules.h"

static int fails = 0;
#define CONTROL(c, ...) do { if (c) { printf("CONTROL "); printf(__VA_ARGS__); printf(": caught\n"); } else { fails++; fprintf(stderr, "FAIL CONTROL not caught: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* everything the rules must keep true at every step; NULL when it holds */
static const char *invariant_error(const kc_state *s) {
  for (int f = 0; f < KC_NFUEL; f++) {
    if (kc_bank(s, f) < 0) return "more fuel than the game has";
    if (s->fuel_mkt[f] < 0 || s->fuel_mkt[f] > kc_fuel_slots[f]) return "fuel market out of range";
  }
  int owned[KC_MAX_P] = { 0 };
  for (int i = 0; i < KC_NPLANT; i++) {
    int where = s->loc[i], in_market = 0, holders = 0;
    for (int k = 0; k < s->nmarket; k++) in_market += s->market[k] == i;
    for (int j = 0; j < s->players; j++)
      for (int k = 0; k < 3; k++)
        if (s->p[j].plants[k] == i) holders++, owned[j]++;
    if ((where == KC_L_MARKET) != (in_market == 1) || in_market > 1) return "a plant in the market twice or out of place";
    if ((where == KC_L_OWNED) != (holders == 1 || s->disc_new == i) || holders > 1) return "a plant held twice or out of place";
  }
  for (int k = 1; k < s->nmarket; k++)
    if (s->market[k] <= s->market[k - 1]) return "the market is not sorted";
  if (s->nmarket > KC_MARKET || (s->step == 3 && s->nmarket > 6)) return "the market is too big";
  int houses[KC_MAX_P] = { 0 };
  for (int c = 0; c < KC_NCITY; c++) {
    int n = kc_house_slot(s, c);
    if (n > s->step) return "a city holds more houses than the step allows";
    if (n && !kc_region_in(s, kc_cities[c].region)) return "a house in a region out of play";
    for (int k = 0; k < 3; k++) {
      int o = s->occ[c][k];
      if (o >= s->players) return "a house of a seat not in the game";
      if (o >= 0) houses[o]++;
      for (int k2 = k + 1; k2 < 3; k2++)
        if (o >= 0 && s->occ[c][k2] == o) return "two houses of one company in a city";
    }
  }
  for (int i = 0; i < s->players; i++) {
    const kc_player *p = &s->p[i];
    if (p->money < 0) return "negative money";
    if (houses[i] != p->ncity) return "city count is not the houses on the map";
    if (owned[i] > 3) return "more than three plants";
    for (int f = 0; f < KC_NFUEL; f++)
      if (p->fuel[f] < 0 || kc_store_room(p, f) < 0) return "fuel out of range";
    /* stored fuel fits the plants */
    kc_player q = *p;
    for (int f = 0; f < KC_NFUEL; f++) q.fuel[f] = 0;
    int c_room = kc_store_room(&q, KC_COAL), o_room = kc_store_room(&q, KC_OIL);
    if (p->fuel[KC_COAL] > c_room || p->fuel[KC_OIL] > o_room || p->fuel[KC_GARBAGE] > kc_store_room(&q, KC_GARBAGE) ||
        p->fuel[KC_URANIUM] > kc_store_room(&q, KC_URANIUM))
      return "more fuel than the plants store";
  }
  return NULL;
}

typedef bk_move (*policy_fn)(const bk_game *g, const kc_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx);
static bk_move pol_uniform(const bk_game *g, const kc_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx) {
  (void)g, (void)s, (void)ctx;
  return mv[cw_rng_int(rng, 0, n - 1)];
}
static bk_move pol_playout(const bk_game *g, const kc_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx) {
  (void)ctx;
  return g->playout(s, mv, n, rng, g->user);
}
/* seat levels in ctx (int[4], -1 = playout policy) */
static bk_move pol_cpu(const bk_game *g, const kc_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx) {
  const int *lv = ctx;
  int seat = g->turn(s, g->user);
  if (lv[seat] < 0) return pol_playout(g, s, mv, n, rng, NULL);
  if (lv[seat] == 9) return pol_uniform(g, s, mv, n, rng, NULL);
  cw_search_options o;
  kc_cpu_options(lv[seat], kc_hash(s), &o);
  cw_search *sr = cw_search_new(g, &o);
  bk_move m = BK_MOVE_NONE;
  if (!sr || !cw_search_run(sr, s, &m)) CW_FATAL("search failed");
  cw_search_free(sr);
  return m;
}

typedef struct result { int winner, rounds, moves, step; uint64_t hash; bool ok; } result;

static result play(int players, uint64_t seed, policy_fn pol, void *ctx, bool check_inv) {
  bk_game g;
  kc_game(&g, players);
  kc_state s;
  kc_init(&s, players, seed);
  cw_rng rng;
  cw_rng_seed(&rng, seed ^ 0xabcdef);
  result r = { 0 };
  r.ok = true;
  int guard = 0;
  while (!g.terminal(&s, g.user)) {
    if (++guard > 20000) { CHECK(0, "game %d/%llu never ends", players, (unsigned long long)seed); r.ok = false; break; }
    if (g.turn(&s, g.user) == BK_CHANCE) {
      bk_settle(&g, &s, &rng, NULL);
      continue;
    }
    bk_move mv[BK_MAX_MOVES];
    int n = g.moves(&s, mv, BK_MAX_MOVES, g.user);
    if (n <= 0) { CHECK(0, "no moves at a decision (phase %d)", s.phase); r.ok = false; break; }
    bk_move m = pol(&g, &s, mv, n, &rng, ctx);
    if (!bk_is_legal(&g, &s, m)) { CHECK(0, "illegal move from the policy"); r.ok = false; break; }
    g.apply(&s, m, g.user);
    r.moves++;
    if (check_inv) {
      const char *e = invariant_error(&s);
      if (e) { CHECK(0, "%d players seed %llu round %d: %s", players, (unsigned long long)seed, s.round, e); r.ok = false; break; }
    }
  }
  r.winner = kc_winner(&s);
  r.rounds = s.round;
  r.step = s.step;
  r.hash = kc_hash(&s);
  if (r.ok && (r.winner < 0 || r.winner >= players)) { CHECK(0, "no legal winner"); r.ok = false; }
  return r;
}

static void test_setup(void) {
  for (int n = 2; n <= 4; n++) {
    kc_state s;
    kc_init(&s, n, 99 + n);
    int regions = 0;
    for (int r = 0; r < KC_NREGION; r++) regions += kc_region_in(&s, r);
    CHECK(regions == kc_regions_used[n], "%d players: %d regions", n, regions);
    int aside = 0;
    for (int i = 0; i < KC_NPLANT; i++) aside += s.loc[i] == KC_L_ASIDE;
    CHECK(aside == kc_plants_out[n], "%d players: %d set aside", n, aside);
    CHECK(s.nmarket == 8, "market of %d", s.nmarket);
    for (int i = 0; i < 8; i++) CHECK(s.market[i] == i, "market slot %d holds plant %d", i, s.market[i]);
    int seen = 0;
    for (int i = 0; i < n; i++) { seen |= 1 << s.order[i]; CHECK(s.p[i].money == KC_START_MONEY, "money"); }
    CHECK(seen == (1 << n) - 1, "the first order is not every seat once");
    CHECK(s.phase == KC_PH_AUCTION && s.cur == s.order[0], "the first opener is first in order");
    CHECK(!invariant_error(&s), "fresh state: %s", invariant_error(&s));
    bk_move mv[BK_MAX_MOVES];
    int k = kc_moves(&s, mv, BK_MAX_MOVES, NULL);
    CHECK(k == 4, "round 1 opener: %d moves (four plants, no pass)", k);
  }
  printf("ok   setup at 2, 3 and 4 players\n");
  kc_state s;
  kc_init(&s, 3, 5);
  s.p[1].money = -1;
  CONTROL(invariant_error(&s) != NULL, "negative money is an invariant error");
}

static void test_fuel(void) {
  kc_state s;
  kc_init(&s, 2, 1);
  /* coal: 21 in the market, prices 1..8 three each, filled from the top: the cheapest left is 2 */
  CHECK(kc_fuel_price(&s, KC_COAL) == 2, "coal price %d", kc_fuel_price(&s, KC_COAL));
  CHECK(kc_fuel_cost(&s, KC_COAL, 4) == 2 + 2 + 2 + 3, "4 coal cost %d", kc_fuel_cost(&s, KC_COAL, 4));
  CHECK(kc_fuel_price(&s, KC_URANIUM) == 15, "uranium price %d", kc_fuel_price(&s, KC_URANIUM));
  int last = 0;
  for (int k = 1; k <= s.fuel_mkt[KC_OIL]; k++) {
    int c = kc_fuel_cost(&s, KC_OIL, k) - kc_fuel_cost(&s, KC_OIL, k - 1);
    CHECK(c >= last, "oil prices fall as stock runs out");
    last = c;
  }
  CONTROL(kc_fuel_cost(&s, KC_URANIUM, 3) == -1, "buying more uranium than the market holds is refused");
  /* storage: a coal plant 3 (burns 2) and the coal-or-oil plant 6 (burns 2) */
  kc_player p = { 0 };
  p.plants[0] = 0, p.plants[1] = 3, p.plants[2] = -1;
  CHECK(kc_plants[3].kind == KC_K_HYBRID, "plant 6 is coal or oil");
  CHECK(kc_store_room(&p, KC_COAL) == 8 && kc_store_room(&p, KC_OIL) == 4, "rooms %d %d", kc_store_room(&p, KC_COAL), kc_store_room(&p, KC_OIL));
  p.fuel[KC_OIL] = 3;
  CHECK(kc_store_room(&p, KC_COAL) == 5, "oil in the shared store leaves coal room %d", kc_store_room(&p, KC_COAL));
  CHECK(kc_store_room(&p, KC_GARBAGE) == 0, "no garbage room");
  p.ncity = 2;
  p.fuel[KC_COAL] = 1, p.fuel[KC_OIL] = 1;
  CHECK(kc_can_power(&p) == 1, "one hybrid run on one coal and one oil: %d", kc_can_power(&p));
  p.fuel[KC_COAL] = 4;
  CHECK(kc_can_power(&p) == 2, "both run: %d", kc_can_power(&p));
  printf("ok   fuel prices, storage and power\n");
  p.fuel[KC_COAL] = 0, p.fuel[KC_OIL] = 0;
  CONTROL(kc_can_power(&p) == 0, "no fuel lights nothing");
}

/* an independent shortest path (Bellman-Ford over the links in play) */
static int bf(const kc_state *s, int from, int to) {
  int d[KC_NCITY];
  for (int i = 0; i < KC_NCITY; i++) d[i] = 1 << 20;
  d[from] = 0;
  for (int it = 0; it < KC_NCITY; it++)
    for (int l = 0; l < KC_NLINK; l++) {
      int a = kc_links[l].a, b = kc_links[l].b, c = kc_links[l].cost;
      if (!kc_region_in(s, kc_cities[a].region) || !kc_region_in(s, kc_cities[b].region)) continue;
      if (d[a] + c < d[b]) d[b] = d[a] + c;
      if (d[b] + c < d[a]) d[a] = d[b] + c;
    }
  return d[to];
}
static void test_routes(void) {
  int checked = 0;
  for (int n = 2; n <= 4; n++) {
    kc_state s;
    kc_init(&s, n, 7 * n);
    int first = -1;
    for (int c = 0; c < KC_NCITY && first < 0; c++)
      if (kc_region_in(&s, kc_cities[c].region)) first = c;
    CHECK(kc_build_cost(&s, 0, first, NULL) == kc_house_cost[0], "the first city costs only the house");
    s.occ[first][0] = 0, s.p[0].ncity = 1;
    for (int c = 0; c < KC_NCITY; c++) {
      if (c == first) { CHECK(kc_build_cost(&s, 0, c, NULL) == -1, "no second house in one city"); continue; }
      int cost = kc_build_cost(&s, 0, c, NULL);
      if (!kc_region_in(&s, kc_cities[c].region)) { CHECK(cost == -1, "a city out of play"); continue; }
      CHECK(cost == kc_house_cost[0] + bf(&s, first, c), "route to %s %d vs %d", kc_cities[c].name, cost, kc_house_cost[0] + bf(&s, first, c));
      checked++;
    }
    /* step 1: one company per city */
    s.occ[first][0] = 1, s.p[0].ncity = 0, s.p[1].ncity = 1;
    CHECK(kc_build_cost(&s, 0, first, NULL) == -1, "step 1: a taken city is full");
    s.step = 2;
    CHECK(kc_build_cost(&s, 0, first, NULL) == kc_house_cost[1], "step 2: the second room costs %d", kc_house_cost[1]);
  }
  printf("ok   routes: %d build costs match an independent shortest path\n", checked);
  kc_state s;
  kc_init(&s, 4, 3);
  s.occ[0][0] = 0, s.p[0].ncity = 1;
  CONTROL(kc_build_cost(&s, 0, 1, NULL) != kc_house_cost[0], "a second city costs more than the house alone");
}

static void test_auction(void) {
  kc_state s;
  kc_init(&s, 3, 11);
  bk_game g;
  kc_game(&g, 3);
  int opener = s.cur;
  kc_apply(&s, bk_move_pack(KC_M_OPEN, 2, 0, 0, 0, 0, 0, 0), NULL); /* plant 5 */
  CHECK(s.auc_plant == 2 && s.auc_bid == 5, "auction of plant 5 at 5");
  int second = s.cur;
  CHECK(second == (opener + 1) % 3, "the next seat bids");
  kc_apply(&s, bk_move_pack(KC_M_RAISE, 2, 0, 0, 0, 0, 0, 0), NULL); /* +5 = 10 */
  CHECK(s.auc_bid == 10 && s.auc_high == second, "a raise");
  int third = s.cur;
  kc_apply(&s, bk_move_pack(KC_M_DROP, 0, 0, 0, 0, 0, 0, 0), NULL);
  CHECK(s.cur == opener, "back to the opener");
  kc_apply(&s, bk_move_pack(KC_M_DROP, 0, 0, 0, 0, 0, 0, 0), NULL);
  CHECK(s.p[second].plants[0] == 2 && s.p[second].money == 40, "the high bidder pays 10 for plant 5 (%d)", s.p[second].money);
  CHECK(g.turn(&s, NULL) == BK_CHANCE, "a draw refills the market");
  cw_rng r;
  cw_rng_seed(&r, 1);
  bk_settle(&g, &s, &r, NULL);
  CHECK(s.nmarket == 8 && s.cur == opener, "the opener opens again (cur %d)", s.cur);
  (void)third;
  /* a fourth plant replaces one */
  kc_init(&s, 2, 4);
  s.p[s.cur].plants[0] = 8, s.p[s.cur].plants[1] = 9, s.p[s.cur].plants[2] = 10;
  for (int i = 8; i <= 10; i++) s.loc[i] = KC_L_OWNED;
  int me = s.cur;
  s.p[me].fuel[KC_COAL] = 6; /* plant 13 (coal 3) stores 6: taken from the market */
  s.fuel_mkt[KC_COAL] -= 6;
  kc_apply(&s, bk_move_pack(KC_M_OPEN, 0, 0, 0, 0, 0, 0, 0), NULL);
  kc_apply(&s, bk_move_pack(KC_M_DROP, 0, 0, 0, 0, 0, 0, 0), NULL);
  CHECK(s.phase == KC_PH_DISCARD && s.cur == me, "four plants: scrap one");
  int coal_plant = -1;
  for (int i = 0; i < 3; i++)
    if (kc_plants[s.p[me].plants[i]].kind == KC_K_COAL) coal_plant = i;
  kc_apply(&s, bk_move_pack(KC_M_DISCARD, coal_plant, 0, 0, 0, 0, 0, 0), NULL);
  CHECK(kc_nplants(&s.p[me]) == 3, "three plants after scrapping");
  CHECK(!invariant_error(&s), "after scrapping: %s", invariant_error(&s));
  printf("ok   auctions: bids, raises, drops, the opener again, a fourth plant\n");
  s.p[me].fuel[KC_GARBAGE] = 9;
  CONTROL(invariant_error(&s) != NULL, "fuel no plant can store is caught");
}

static void test_games(void) {
  for (int n = 2; n <= 4; n++) {
    int wins[4] = { 0 }, rounds = 0, step3 = 0;
    for (int k = 0; k < 40; k++) {
      result r = play(n, 1000 + (uint64_t)k, k & 1 ? pol_uniform : pol_playout, NULL, true);
      if (!r.ok) return;
      wins[r.winner]++;
      rounds += r.rounds;
      step3 += r.step == 3;
    }
    printf("%d players: 40 random and playout games, wins %d %d %d %d, %.1f rounds, step 3 in %d\n", n, wins[0], wins[1], wins[2], wins[3],
           rounds / 40.0, step3);
  }
  /* determinism */
  result a = play(3, 77, pol_playout, NULL, false), b = play(3, 77, pol_playout, NULL, false), c = play(3, 78, pol_playout, NULL, false);
  CHECK(a.hash == b.hash, "the same seed replays the same game");
  printf("ok   determinism\n");
  CONTROL(a.hash != c.hash, "another seed plays another game");
}

static void test_cpu(void) {
  for (int n = 2; n <= 4; n++) {
    int lv[4] = { 1, 1, 1, 1 };
    result r = play(n, 500 + (uint64_t)n, pol_cpu, lv, true);
    if (r.ok) printf("CPU game: %d Normal CPUs, %d rounds, %d moves, step %d, winner seat %d\n", n, r.rounds, r.moves, r.step, r.winner);
  }
  /* a Normal CPU against random play */
  int won = 0, games = 12;
  for (int k = 0; k < games; k++) {
    int lv[4] = { 9, 9, 9, 9 };
    int me = k % 2;
    lv[me] = 1;
    result r = play(2, 2000 + (uint64_t)k, pol_cpu, lv, false);
    won += r.winner == me;
  }
  CHECK(won >= games - 1, "a Normal CPU beat random play %d of %d", won, games);
  printf("ok   a Normal CPU beats random play %d of %d\n", won, games);
  int lost = 0;
  for (int k = 0; k < 6; k++) {
    int lv[4] = { 9, 9, 9, 9 };
    result r = play(2, 3000 + (uint64_t)k, pol_cpu, lv, false);
    lost += r.winner == 0;
  }
  CONTROL(lost < 6, "random play against random play does not always win for seat 0 (%d of 6)", lost);
}

int main(void) {
  test_setup();
  test_fuel();
  test_routes();
  test_auction();
  test_games();
  test_cpu();
  if (fails) { fprintf(stderr, "%d failures\n", fails); return 1; }
  printf("all passed\n");
  return 0;
}
