/* Fever Line rules tests: headless, seeded. Exit 0 = all pass. Each part has a control that must fail. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"

#include "rules.h"

static int fails = 0;
static int test_map = 0; /* the map the hand-made situations use (the world) */
#define CONTROL(c, ...) do { if (c) { printf("CONTROL "); printf(__VA_ARGS__); printf(": caught\n"); } else { fails++; fprintf(stderr, "FAIL CONTROL not caught: "); fprintf(stderr, __VA_ARGS__); fputc(10, stderr); } } while (0)
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* the state's own rules: cubes conserved, at most three of a fever in a city, cards where cards may be, clinics
 * counted, hands within the limit outside a discard step */
static const char *invariant_error(const fv_state *s) {
  for (int c = 0; c < FV_NCOL; c++) {
    int on = 0;
    for (int i = 0; i < FV_N; i++) {
      if (s->cubes[i][c] > 3) return "more than 3 cubes of a fever in a city";
      on += s->cubes[i][c];
    }
    if (on + s->supply[c] != FV_CUBES) return "cubes not conserved";
    if (s->cured[c] == 2 && on) return "a wiped-out fever still on the map";
  }
  int clinics = 0;
  for (int i = 0; i < FV_N; i++) clinics += s->clinic[i] != 0;
  if (clinics != s->clinics || clinics > FV_MAX_CLINICS) return "clinics miscounted";
  for (int c = 0; c < FV_NCARDS; c++) {
    int w = s->pcard[c];
    bool ok = w < s->roles || w == FV_PC_DISCARD || w == FV_PC_OUT || (w >= FV_PC_DECK && w < FV_PC_DECK + FV_MAX_SURGE);
    if (!ok) return "a city card in no place";
    if (c >= FV_SURGE0 && w < s->roles) return "a surge in a hand";
  }
  for (int r = 0; r < s->roles; r++) {
    if (s->loc[r] < 0 || s->loc[r] >= FV_NCITY) return "an agent off the map";
    if (s->phase == FV_PH_ACT && fv_hand_count(s, r) > FV_HAND_MAX) return "a hand over the limit in play";
  }
  if (s->outbreaks > FV_MAX_OUTBREAKS) return "outbreaks past the end";
  if (s->result == FV_PLAYING && s->outbreaks >= FV_MAX_OUTBREAKS) return "8 outbreaks and still playing";
  for (int i = 0; i < FV_N; i++)
    if (s->icard[i] > s->itop) return "a fever card above the top layer";
  return NULL;
}

typedef bk_move (*policy_fn)(const fv_state *s, const bk_move *mv, int n, cw_rng *rng, int *decisions);
static bk_move pol_uniform(const fv_state *s, const bk_move *mv, int n, cw_rng *rng, int *decisions) {
  (void)s, (void)decisions;
  return mv[cw_rng_int(rng, 0, n - 1)];
}
static bk_game TEAM;
static int cpu_level = 0;
static bk_move pol_cpu(const fv_state *s, const bk_move *mv, int n, cw_rng *rng, int *decisions) {
  (void)mv, (void)n, (void)rng;
  cw_search_options o;
  fv_cpu_options(cpu_level, bk_seed_mix((uint64_t)s->turns, (uint64_t)(*decisions)++), &o);
  cw_search *sr = cw_search_new(&TEAM, &o);
  bk_move m = BK_MOVE_NONE;
  if (!cw_search_run(sr, s, &m)) m = BK_MOVE_NONE;
  cw_search_free(sr);
  return m;
}

typedef struct result { int result, reason, cures, turns, moves; uint64_t hash; bool ok; const char *why; } result;

static result play(int roles, int setup, uint64_t seed, policy_fn pol) {
  bk_game g;
  fv_game(&g, roles);
  fv_state s;
  fv_init(&s, roles, setup, test_map, seed);
  cw_rng rng, chance;
  cw_rng_seed(&rng, seed * 31 + 7);
  cw_rng_seed(&chance, bk_seed_mix(seed, 0xc4a));
  result res = { .ok = true };
  int guard = 0, decisions = 0;
  while (!fv_terminal(&s, NULL)) {
    if (++guard > 20000) { res.ok = false, res.why = "the game does not end"; break; }
    const char *e = invariant_error(&s);
    if (e) { res.ok = false, res.why = e; break; }
    if (fv_turn(&s, NULL) == BK_CHANCE) {
      bk_move o[FV_NCARDS];
      uint32_t w[FV_NCARDS];
      int n = fv_outcomes(&s, o, w, FV_NCARDS, NULL);
      if (n <= 0) { res.ok = false, res.why = "a chance step with no outcome"; break; }
      bk_settle(&g, &s, &chance, NULL);
      continue;
    }
    bk_move mv[BK_MAX_MOVES];
    int n = fv_moves(&s, mv, BK_MAX_MOVES, NULL);
    if (n <= 0) { res.ok = false, res.why = "no legal move"; break; }
    bk_move m = pol(&s, mv, n, &rng, &decisions);
    if (!bk_is_legal(&g, &s, m)) { res.ok = false, res.why = "an illegal move chosen"; break; }
    fv_apply(&s, m, NULL);
    res.moves++;
  }
  const char *e = invariant_error(&s);
  if (res.ok && e) res.ok = false, res.why = e;
  if (res.ok && s.result != FV_WON && s.result != FV_LOST) res.ok = false, res.why = "over without a result";
  if (res.ok && s.result == FV_LOST && s.lose_reason == FV_LOSE_NONE) res.ok = false, res.why = "lost for no reason";
  if (res.ok && s.result == FV_WON) {
    for (int c = 0; c < FV_NCOL; c++)
      if (!s.cured[c]) res.ok = false, res.why = "won with a fever uncured";
  }
  res.result = s.result;
  res.reason = s.lose_reason;
  res.turns = s.turns;
  for (int c = 0; c < FV_NCOL; c++) res.cures += s.cured[c] != 0;
  res.hash = fv_hash(&s);
  return res;
}

/* ── parts ──────────────────────────────────────────────────────────── */

static void test_maps(void) {
  for (int m = 0; m < FV_NMAPS; m++) {
  fv_use_map(m);
  int roads = 0;
  for (int c = 0; c < FV_N; c++) {
    roads += FV_CITIES[c].nnb;
    for (int k = 0; k < FV_CITIES[c].nnb; k++) CHECK(fv_adjacent(FV_CITIES[c].nb[k], c), "road %s one way", FV_CITIES[c].name);
    for (int d = 0; d < FV_N; d++) CHECK(fv_dist(c, d) >= 0, "%s cannot reach %s", FV_CITIES[c].name, FV_CITIES[d].name);
  }
  CHECK(roads == 2 * FV_MAPS[m].nroads, "%s: road count %d vs %d", FV_MAPS[m].title, roads, FV_MAPS[m].nroads);
  printf("ok   map %s: %d cities, %d links, every city reaches every other\n", FV_MAPS[m].title, FV_N, FV_MAPS[m].nroads);
  }
  fv_use_map(test_map);
  CONTROL(!fv_adjacent(0, 0), "a city is not its own neighbour");
}

static void test_setup(void) {
  for (int roles = 2; roles <= 4; roles++)
    for (int setup = 0; setup < 3; setup++) {
      fv_state s;
      fv_init(&s, roles, setup, test_map, 99 + (uint64_t)roles * 7 + (uint64_t)setup);
      CHECK(!invariant_error(&s), "setup %d/%d: %s", roles, setup, invariant_error(&s));
      int cubes = 0, infected = 0, three = 0;
      for (int i = 0; i < FV_N; i++) {
        int k = 0;
        for (int c = 0; c < FV_NCOL; c++) k += s.cubes[i][c];
        cubes += k, infected += k > 0, three += k == 3;
      }
      CHECK(cubes == 18 && infected == 9 && three == 3, "nine cities with 18 cubes (got %d cities, %d cubes)", infected, cubes);
      CHECK(fv_cure_need(&s, 0) == (s.role[0] == FV_ANALYST ? 3 : 4), "a cure on the world map (40 cities) takes 4 cards (3 for the Analyst)");
      int each = roles == 2 ? 4 : roles == 3 ? 3 : 2;
      for (int r = 0; r < roles; r++) {
        CHECK(fv_hand_count(&s, r) == each, "a hand of %d", fv_hand_count(&s, r));
        CHECK(s.loc[r] == FV_START_CITY, "an agent not at the start");
        for (int q = 0; q < r; q++) CHECK(s.role[q] != s.role[r], "two agents share a role");
      }
      CHECK(s.clinic[FV_START_CITY] && s.clinics == 1, "the first clinic");
      int surges = fv_surges_for(setup);
      CHECK(fv_deck_left(&s) == FV_N - roles * each + surges + FV_MAPS[test_map].calm, "deck %d", fv_deck_left(&s));
      for (int p = 0; p < surges; p++) {
        int in = 0, cities = 0;
        for (int c = 0; c < FV_NCARDS; c++)
          if (s.pcard[c] == FV_PC_DECK + p) { if (fv_is_surge(c)) in++; else if (c < FV_SURGE0) cities++; }
        CHECK(in == 1 && cities >= 5, "pile %d holds %d surges and %d cities", p, in, cities);
      }
    }
  printf("ok   setup: 9 cities infected (3, 3, 3, 2, 2, 2, 1, 1, 1), hands, a surge in each pile, at 2..4 agents and every setup\n");
  fv_state s;
  fv_init(&s, 2, 0, test_map, 5);
  s.cubes[0][fv_colour(0)] = 4;
  CONTROL(invariant_error(&s) != NULL, "a city with a fourth cube fails the invariants");
}

/* a state with no cubes and every fever card in the deck's one layer, for hand-made situations */
static void blank(fv_state *s, int roles) {
  fv_init(s, roles, FV_INTRO, test_map, 1);
  memset(s->cubes, 0, sizeof s->cubes);
  for (int c = 0; c < FV_NCOL; c++) s->supply[c] = FV_CUBES;
  for (int i = 0; i < FV_N; i++) s->icard[i] = 1;
  s->itop = 1;
  for (int r = 0; r < roles; r++) s->role[r] = (int16_t)(r == 0 ? FV_ANALYST : FV_ENGINEER), s->loc[r] = FV_START_CITY;
}
static void put(fv_state *s, int city, int k) {
  int c = fv_colour(city);
  s->cubes[city][c] = (uint8_t)k;
  s->supply[c] = (uint8_t)(s->supply[c] - k);
}
static void spread_to(fv_state *s, int city) { /* the spread step flips this city's card */
  s->phase = FV_PH_INFECT;
  s->infects = 2;
  fv_apply(s, bk_move_pack(FV_M_FLIP, city, 0, 0, 0, 0, 0, 0), NULL);
}

static void test_outbreaks(void) {
  /* a city at three breaks out: every neighbour gets a cube; a neighbour at three breaks out too, once */
  fv_state s;
  blank(&s, 2);
  int a = 0, b = -1;
  for (int k = 0; k < FV_CITIES[a].nnb; k++)
    if (fv_colour(FV_CITIES[a].nb[k]) == fv_colour(a)) { b = FV_CITIES[a].nb[k]; break; }
  CHECK(b >= 0, "the first city has a neighbour of its own region");
  for (int r = 0; r < 2; r++) s.loc[r] = (int16_t)(FV_N - 1); /* far away: no Warden here anyway */
  put(&s, a, 3);
  put(&s, b, 3);
  spread_to(&s, a);
  CHECK(s.outbreaks == 2, "a chain of two (got %d)", s.outbreaks);
  CHECK(s.ob_n == 2 && s.ob_city[0] == a && s.ob_city[1] == b, "the chain's cities in order");
  CHECK(!invariant_error(&s), "after the chain: %s", invariant_error(&s));
  int col = fv_colour(a);
  for (int k = 0; k < FV_CITIES[a].nnb; k++) {
    int nb = FV_CITIES[a].nb[k];
    if (nb != b) CHECK(s.cubes[nb][col] >= 1, "a neighbour of the first outbreak got a cube");
  }
  printf("ok   outbreaks: a city at three breaks out into its roads, chains, each city once\n");
  fv_state t;
  blank(&t, 2);
  put(&t, a, 2);
  spread_to(&t, a);
  CONTROL(t.outbreaks == 0 && t.cubes[a][col] == 3, "a city at two only gains a cube");
  /* the eighth outbreak ends the game */
  fv_state e;
  blank(&e, 2);
  e.outbreaks = 7;
  put(&e, a, 3);
  spread_to(&e, a);
  CHECK(e.result == FV_LOST && e.lose_reason == FV_LOSE_OUTBREAKS, "the eighth outbreak loses");
  /* a fever out of cubes loses */
  fv_state c;
  blank(&c, 2);
  c.supply[col] = 0;
  spread_to(&c, a);
  CHECK(c.result == FV_LOST && c.lose_reason == FV_LOSE_CUBES, "a cube that cannot be placed loses");
  printf("ok   losing: the eighth outbreak, a fever out of cubes\n");
}

static void test_warden(void) {
  fv_state s;
  blank(&s, 2);
  s.role[1] = FV_WARDEN;
  int x = 10, nb = FV_CITIES[x].nb[0];
  s.loc[1] = (int16_t)x;
  s.loc[0] = (int16_t)(FV_N - 1);
  spread_to(&s, x);
  int c = fv_colour(x);
  CHECK(s.cubes[x][c] == 0, "the Warden's city stays clear");
  s.infects = 2;
  spread_to(&s, nb);
  CHECK(s.cubes[nb][fv_colour(nb)] == 0, "a city on the Warden's roads stays clear");
  printf("ok   Warden: no cube in her city or on its roads\n");
  fv_state t;
  blank(&t, 2);
  t.role[1] = FV_WARDEN;
  t.loc[1] = (int16_t)(FV_N - 1);
  t.loc[0] = (int16_t)(FV_N - 1);
  spread_to(&t, x);
  CONTROL(t.cubes[x][c] == 1, "with the Warden away the cube lands");
}

static void test_surge(void) {
  fv_state s;
  fv_init(&s, 2, FV_INTRO, test_map, 42);
  s.role[0] = FV_ANALYST, s.role[1] = FV_ENGINEER; /* nobody keeps cubes off a city here */
  int disc_before = 0;
  for (int i = 0; i < FV_N; i++) disc_before += s.icard[i] == 0;
  CHECK(disc_before == 9, "nine fever cards in the discards after setup");
  /* draw the top pile's surge */
  s.phase = FV_PH_DRAW;
  s.draws = 2;
  int surge = -1;
  for (int c = FV_SURGE0; c < FV_CALM0; c++)
    if (s.pcard[c] == FV_PC_DECK) surge = c;
  bk_move o[FV_NCARDS];
  uint32_t w[FV_NCARDS];
  int n = fv_outcomes(&s, o, w, FV_NCARDS, NULL), with_surge = 0;
  for (int i = 0; i < n; i++) {
    with_surge += bk_move_field(o[i], 1) == surge;
    CHECK(w[i] == 1, "draws are even");
  }
  CHECK(with_surge == 1, "the top pile's surge can be drawn");
  int rate = fv_rate(&s);
  fv_apply(&s, bk_move_pack(FV_M_DRAW, surge, 0, 0, 0, 0, 0, 0), NULL);
  CHECK(s.phase == FV_PH_SURGE && s.rate_idx == 1, "a surge raises the rate step");
  n = fv_outcomes(&s, o, w, FV_NCARDS, NULL);
  CHECK(n == FV_N - 9, "the bottom city comes from the cards never flipped (%d)", n);
  int city = bk_move_field(o[0], 1), col = fv_colour(city), before = s.cubes[city][col];
  fv_apply(&s, o[0], NULL);
  CHECK(s.cubes[city][col] == 3 || s.outbreaks > 0, "the bottom city gets 3 cubes");
  (void)before;
  int top = 0;
  for (int i = 0; i < FV_N; i++) top += s.icard[i] == s.itop;
  CHECK(top == 10 && s.icard[city] == s.itop, "the ten discards (the surge city with them) are back on top (%d)", top);
  CHECK(fv_rate(&s) >= rate, "the rate never falls");
  printf("ok   surge: rate up, the bottom city takes 3 cubes, the discards back on top\n");
  CONTROL(s.icard[0] != 0 || s.icard[1] != 0, "the discards are not left in the discard pile");
}

static void test_cure(void) {
  fv_state s;
  blank(&s, 2); /* agent 0 the Analyst (4 cards), agent 1 the Engineer (5) */
  for (int c = 0; c < FV_NCARDS; c++)
    if (s.pcard[c] < 2) s.pcard[c] = FV_PC_DECK;
  int col = 1, k = 0;
  for (int c = 0; c < FV_N && k < fv_cure_base() - 1; c++)
    if (fv_colour(c) == col) s.pcard[c] = 0, k++;
  s.phase = FV_PH_ACT, s.cur = 0, s.acts = 4;
  bk_move mv[BK_MAX_MOVES];
  int n = fv_moves(&s, mv, BK_MAX_MOVES, NULL), cure = -1;
  for (int i = 0; i < n; i++)
    if (bk_move_field(mv[i], 0) == FV_M_CURE) cure = i;
  CHECK(cure >= 0, "the Analyst cures with one card fewer at a clinic");
  fv_apply(&s, mv[cure], NULL);
  CHECK(s.cured[col] && fv_hand_count(&s, 0) == 0, "cured, the cards spent");
  CHECK(s.cured[col] == 2, "with no cubes on the map the fever is wiped out at once");
  printf("ok   cures: one card fewer for the Analyst at a clinic, wiped out with no cubes left\n");
  fv_state t;
  blank(&t, 2);
  for (int c = 0; c < FV_NCARDS; c++)
    if (t.pcard[c] < 2) t.pcard[c] = FV_PC_DECK;
  k = 0;
  for (int c = 0; c < FV_N && k < fv_cure_base() - 1; c++)
    if (fv_colour(c) == col) t.pcard[c] = 1, k++;
  t.phase = FV_PH_ACT, t.cur = 1, t.acts = 4;
  n = fv_moves(&t, mv, BK_MAX_MOVES, NULL);
  bool any = false;
  for (int i = 0; i < n; i++) any |= bk_move_field(mv[i], 0) == FV_M_CURE;
  CONTROL(!any, "the Engineer with that many cards cannot cure");
}

static void test_deck_out(void) {
  fv_state s;
  fv_init(&s, 2, FV_INTRO, test_map, 3);
  for (int c = 0; c < FV_NCARDS; c++)
    if (s.pcard[c] >= FV_PC_DECK && s.pcard[c] < FV_PC_DISCARD) s.pcard[c] = FV_PC_DISCARD;
  s.phase = FV_PH_ACT, s.acts = 1;
  fv_apply(&s, bk_move_pack(FV_M_REST, 0, 0, 0, 0, 0, 0, 0), NULL);
  CHECK(s.result == FV_LOST && s.lose_reason == FV_LOSE_DECK, "drawing from an empty deck loses");
  printf("ok   losing: a draw from an empty deck\n");
}

static void test_share_limit(void) {
  /* a share that puts a hand over 7 makes that agent discard at once, then the turn goes on */
  fv_state s;
  blank(&s, 2);
  for (int c = 0; c < FV_NCARDS; c++)
    if (s.pcard[c] < 2) s.pcard[c] = FV_PC_DECK;
  int at = FV_START_CITY, k = 0;
  s.pcard[at] = 0; /* the giver holds the city's card */
  for (int c = 0; c < FV_N && k < 7; c++)
    if (c != at) s.pcard[c] = 1, k++;
  s.phase = FV_PH_ACT, s.cur = 0, s.acts = 4;
  fv_apply(&s, bk_move_pack(FV_M_GIVE, at, 1, 0, 0, 0, 0, 0), NULL);
  CHECK(s.phase == FV_PH_DISCARD && s.disc_role == 1 && fv_turn(&s, NULL) == 1, "the taker of an eighth card discards at once");
  bk_move mv[BK_MAX_MOVES];
  int n = fv_moves(&s, mv, BK_MAX_MOVES, NULL);
  CHECK(n == 8, "any of the eight cards may go (%d)", n);
  fv_apply(&s, mv[0], NULL);
  CHECK(s.phase == FV_PH_ACT && s.cur == 0 && s.acts == 3 && fv_hand_count(&s, 1) == 7, "then the giver's turn goes on with 3 actions");
  printf("ok   sharing over the hand limit: a discard at once, then the turn goes on\n");
  fv_state t;
  blank(&t, 2);
  for (int c = 0; c < FV_NCARDS; c++)
    if (t.pcard[c] < 2) t.pcard[c] = FV_PC_DECK;
  t.pcard[at] = 0;
  t.phase = FV_PH_ACT, t.cur = 0, t.acts = 4;
  fv_apply(&t, bk_move_pack(FV_M_GIVE, at, 1, 0, 0, 0, 0, 0), NULL);
  CONTROL(t.phase == FV_PH_ACT, "a share within the limit asks no discard");
}

static void test_games(void) {
  int ended = 0, won = 0;
  for (int m = 0; m < FV_NMAPS; m++)
    for (int roles = 2; roles <= 4; roles++)
      for (int setup = 0; setup < 3; setup++)
        for (int k = 0; k < 2; k++) {
          test_map = m;
          result r = play(roles, setup, 1000 + (uint64_t)k * 13 + (uint64_t)roles * 101 + (uint64_t)setup + (uint64_t)m * 7, pol_uniform);
          CHECK(r.ok, "random game %s %d agents setup %d seed %d: %s", FV_MAPS[m].title, roles, setup, k, r.why ? r.why : "?");
          ended += r.ok, won += r.result == FV_WON;
        }
  test_map = 0;
  fv_use_map(0);
  printf("ok   random play: %d full games on every map, at 2, 3 and 4 agents, every setup, end with a legal result (%d won)\n", ended, won);
}

static void test_determinism(void) {
  for (int roles = 2; roles <= 4; roles++) {
    result a = play(roles, 1, 77, pol_uniform), b = play(roles, 1, 77, pol_uniform), c = play(roles, 1, 78, pol_uniform);
    CHECK(a.hash == b.hash && a.moves == b.moves, "the same seed gives the same game (%d agents)", roles);
    CONTROL(a.hash != c.hash, "another seed gives another game (%d agents)", roles);
  }
}

static void test_cpu_games(void) {
  fv_search_game(&TEAM);
  int cpu_cures = 0, rnd_cures = 0, games = 0, cpu_won = 0;
  for (int roles = 2; roles <= 4; roles++)
    for (int k = 0; k < 3; k++) {
      uint64_t seed = 500 + (uint64_t)roles * 17 + (uint64_t)k;
      cpu_level = 1;
      result c = play(roles, FV_INTRO, seed, pol_cpu);
      CHECK(c.ok, "CPU game %d agents seed %d: %s", roles, k, c.why ? c.why : "?");
      result r = play(roles, FV_INTRO, seed, pol_uniform);
      cpu_cures += c.cures, rnd_cures += r.cures, cpu_won += c.result == FV_WON, games++;
    }
  printf("ok   CPU teams: %d full games at 2, 3 and 4 agents end with a legal result (%d won); fevers cured: CPU %d, random %d\n", games,
         cpu_won, cpu_cures, rnd_cures);
  CHECK(cpu_cures > rnd_cures * 3 + 3, "the CPU team cures far more than random play (%d vs %d)", cpu_cures, rnd_cures);
  /* CONTROL: random play against random play is no such gap */
  int r1 = 0, r2 = 0;
  for (int k = 0; k < 9; k++) r1 += play(2 + k % 3, FV_INTRO, 900 + (uint64_t)k, pol_uniform).cures, r2 += play(2 + k % 3, FV_INTRO, 1900 + (uint64_t)k, pol_uniform).cures;
  CONTROL(!(r1 > r2 * 3 + 3), "random play does not beat random play by that gap (%d vs %d)", r1, r2);
}

int main(void) {
  test_maps();
  test_setup();
  test_outbreaks();
  test_warden();
  test_surge();
  test_cure();
  test_deck_out();
  test_share_limit();
  test_games();
  test_determinism();
  test_cpu_games();
  if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
  printf("all feverline rules tests passed\n");
  return 0;
}
