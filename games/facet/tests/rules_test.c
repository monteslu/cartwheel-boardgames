/* Facet rules tests: headless, seeded. Each check names what it proves; each has a control that must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rules.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
/* a control that must fail: the check holds when the broken case is refused */
#define CONTROL(cond, ...) do { CHECK(cond, __VA_ARGS__); if (cond) { printf("CONTROL "); printf(__VA_ARGS__); printf(" caught\n"); } } while (0)

static int totals0[GM_NTOK];

/* the conservation law: every stone is in the bank or a hand; every card in exactly one place */
static bool invariants(const gm_state *st, char *why, size_t cap) {
  for (int c = 0; c < GM_NTOK; c++) {
    int t = st->bank[c];
    for (int p = 0; p < st->players; p++) t += st->s[p].tok[c];
    if (t != totals0[c]) { snprintf(why, cap, "colour %d total %d != %d", c, t, totals0[c]); return false; }
  }
  int market = 0, res = 0, bought = 0;
  for (int i = 0; i < GM_NCARDS; i++) {
    if (st->where[i] == GM_AT_MARKET) market++;
    if (st->where[i] == GM_AT_RESERVED) res++;
    if (st->where[i] == GM_AT_BOUGHT) bought++;
  }
  int inmarket = 0, nres = 0, ncards = 0;
  for (int t = 0; t < GM_TIERS; t++)
    for (int s = 0; s < GM_SLOTS; s++)
      if (st->market[t][s] != GM_NONE) {
        inmarket++;
        if (st->where[st->market[t][s]] != GM_AT_MARKET) { snprintf(why, cap, "market card not marked"); return false; }
      }
  for (int p = 0; p < st->players; p++) {
    nres += st->s[p].nres;
    ncards += st->s[p].ncards;
    if (st->s[p].nres > GM_MAX_RES) { snprintf(why, cap, "seat %d holds %d set-asides", p, st->s[p].nres); return false; }
    if (st->phase == GM_PH_ACT && !st->draw_kind && gm_hand(&st->s[p]) > GM_HAND_CAP) { snprintf(why, cap, "seat %d holds %d stones", p, gm_hand(&st->s[p])); return false; }
    int nb = 0;
    for (int c = 0; c < GM_NCOLORS; c++) nb += st->s[p].bonus[c];
    if (nb != st->s[p].ncards) { snprintf(why, cap, "seat %d bonuses %d != cards %d", p, nb, st->s[p].ncards); return false; }
  }
  if (market != inmarket || res != nres || bought != ncards) { snprintf(why, cap, "card places %d/%d %d/%d %d/%d", market, inmarket, res, nres, bought, ncards); return false; }
  return true;
}

typedef enum { PICK_RANDOM, PICK_GREEDY } pick_kind;

static bk_move pick(const bk_game *g, const gm_state *st, pick_kind kind, cw_rng *rng) {
  bk_move mv[BK_MAX_MOVES];
  int n = gm_moves(st, mv, BK_MAX_MOVES, NULL);
  if (n <= 0 || n > BK_MAX_MOVES) { fprintf(stderr, "move count %d\n", n); exit(2); }
  if (kind == PICK_RANDOM) return mv[cw_rng_int(rng, 0, n - 1)];
  int me = st->seat, best = 0;
  float bv = -1e30f;
  for (int i = 0; i < n; i++) {
    gm_state c = *st;
    gm_apply(&c, mv[i], NULL);
    /* average a draw over a few samples */
    float v = 0;
    int samples = c.draw_kind ? 4 : 1;
    for (int k = 0; k < samples; k++) {
      gm_state d = c;
      cw_rng r2;
      cw_rng_seed(&r2, (uint64_t)k * 7919u + 1);
      bk_settle(g, &d, &r2, NULL);
      float sc[BK_MAX_SEATS];
      gm_evaluate(&d, sc, NULL);
      float other = -1e30f;
      for (int p = 0; p < st->players; p++)
        if (p != me && sc[p] > other) other = sc[p];
      v += sc[me] - other;
    }
    v /= samples;
    if (v > bv) { bv = v; best = i; }
  }
  return mv[best];
}

typedef struct { int winners, turns; uint64_t hash; bool ok; } result;

static result play(int players, uint64_t seed, pick_kind kinds[BK_MAX_SEATS], bool check_inv) {
  bk_game g;
  gm_rules(&g, players);
  gm_state st;
  gm_new(&st, players, seed);
  for (int c = 0; c < GM_NTOK; c++) {
    totals0[c] = st.bank[c];
  }
  cw_rng rng;
  cw_rng_seed(&rng, seed ^ 0xabcdefu);
  result r = {0, 0, 0, true};
  char why[160];
  int steps = 0;
  while (!gm_terminal(&st, NULL)) {
    bk_settle(&g, &st, &st.rng, NULL);
    if (gm_terminal(&st, NULL)) break;
    bk_move m = pick(&g, &st, kinds[st.seat], &rng);
    if (!bk_is_legal(&g, &st, m)) { CHECK(0, "picked an illegal move"); r.ok = false; break; }
    gm_apply(&st, m, NULL);
    if (check_inv && !invariants(&st, why, sizeof why)) { CHECK(0, "seed %llu step %d: %s", (unsigned long long)seed, steps, why); r.ok = false; break; }
    if (++steps > 20000) { CHECK(0, "game did not end"); r.ok = false; break; }
  }
  r.winners = st.winners;
  r.turns = st.turns;
  r.hash = bk_hash(&g, &st);
  return r;
}

static void test_setup(void) {
  for (int players = 2; players <= 4; players++) {
    gm_state st;
    gm_new(&st, players, 42);
    int per = players == 2 ? 4 : players == 3 ? 5 : 7;
    CHECK(st.bank[0] == per && st.bank[GM_WILD] == 5, "bank for %d players", players);
    int open = 0;
    for (int i = 0; i < GM_NPATRONS; i++) open += st.patron_open[i] == 1;
    CHECK(open == players + 1, "%d patrons for %d players", open, players);
    for (int t = 0; t < GM_TIERS; t++) CHECK(gm_deck_left(&st, t) == (t == 0 ? 40 : t == 1 ? 30 : 20) - 4, "deck %d", t);
  }
  /* the set itself: 90 commissions, 10 patrons, tier sizes */
  int tiers[3] = {0, 0, 0};
  for (int i = 0; i < GM_NCARDS; i++) tiers[gm_card_set[i].tier]++;
  CHECK(tiers[0] == 40 && tiers[1] == 30 && tiers[2] == 20, "tier sizes %d %d %d", tiers[0], tiers[1], tiers[2]);
}

static int count_kind(const gm_state *st, int kind) {
  bk_move mv[BK_MAX_MOVES];
  int n = gm_moves(st, mv, BK_MAX_MOVES, NULL), k = 0;
  for (int i = 0; i < n; i++) k += bk_move_field(mv[i], 0) == kind;
  return k;
}

static void test_moves(void) {
  gm_state st;
  gm_new(&st, 2, 7);
  /* 2 players: piles of 4, so take-two is legal on every colour; ten three-colour takes */
  CHECK(count_kind(&st, GM_MV_TAKE2) == 5, "take two at 4");
  CHECK(count_kind(&st, GM_MV_TAKE3) == 10, "ten three-colour takes");
  CHECK(count_kind(&st, GM_MV_BUY) == 0, "nothing affordable at the start");
  CHECK(count_kind(&st, GM_MV_RESERVE) == 12 && count_kind(&st, GM_MV_RESERVE_DECK) == 3, "set-asides");
  /* control: a pile of 3 must NOT allow take-two */
  st.bank[0] = 3;
  CONTROL(count_kind(&st, GM_MV_TAKE2) == 4, "a pile of 3 refuses take two");
  /* fewer than three colours left: take what is there */
  st.bank[0] = st.bank[1] = st.bank[2] = 0;
  CHECK(count_kind(&st, GM_MV_TAKE3) == 1, "two colours left: one two-colour take");
}

static void test_hand_cap_and_return(void) {
  gm_state st;
  gm_new(&st, 3, 9);
  gm_seat *p = &st.s[0];
  p->tok[0] = 3; p->tok[1] = 3; p->tok[2] = 2;
  st.bank[0] -= 3; st.bank[1] -= 3; st.bank[2] -= 2;
  for (int c = 0; c < GM_NTOK; c++) totals0[c] = st.bank[c] + p->tok[c];
  /* eight stones: a three-colour take must name one stone to hand back; every listed take ends at ten or
   * fewer, and no two takes end with the same hand */
  bk_move mv[BK_MAX_MOVES];
  int n = gm_moves(&st, mv, BK_MAX_MOVES, NULL), takes = 0, bad = 0, dup = 0;
  uint8_t finals[BK_MAX_MOVES][GM_NTOK];
  for (int i = 0; i < n; i++) {
    int k = bk_move_field(mv[i], 0);
    if (k != GM_MV_TAKE3 && k != GM_MV_TAKE2) continue;
    gm_state c = st;
    gm_apply(&c, mv[i], NULL);
    if (gm_hand(&c.s[0]) > GM_HAND_CAP) bad++;
    for (int j = 0; j < takes; j++)
      if (!memcmp(finals[j], c.s[0].tok, GM_NTOK)) dup++;
    memcpy(finals[takes++], c.s[0].tok, GM_NTOK);
  }
  CHECK(takes > 10 && !bad && !dup, "takes %d, over ten %d, duplicate hands %d", takes, bad, dup);
  bk_game g;
  gm_rules(&g, 3);
  bk_move none = bk_move_pack(GM_MV_TAKE3, 2, 3, 4, GM_NONE, GM_NONE, GM_NONE, GM_NONE) & ~((bk_move)0xff << 56);
  CONTROL(!bk_is_legal(&g, &st, none), "a take to eleven without a hand-back is not legal");
  bk_move one = bk_move_pack(GM_MV_TAKE3, 2, 3, 4, 0, GM_NONE, GM_NONE, 0);
  CHECK(bk_is_legal(&g, &st, one), "take emerald amethyst diamond, hand back ruby is legal");
  gm_apply(&st, one, NULL);
  CHECK(gm_hand(&st.s[0]) == 10 && st.s[0].tok[0] == 2 && st.seat == 1, "back to ten in the same move: turn passes");
}

static void test_buy_and_discount(void) {
  gm_state st;
  gm_new(&st, 2, 11);
  int card = st.market[0][0];
  const gm_card *c = &gm_card_set[card];
  gm_seat *p = &st.s[0];
  /* give exact cost less one, plus one topaz: buying must spend the topaz */
  int short_color = -1;
  for (int i = 0; i < GM_NCOLORS; i++) {
    p->tok[i] = c->cost[i];
    if (c->cost[i] && short_color < 0) short_color = i;
  }
  p->tok[short_color]--;
  p->tok[GM_WILD] = 1;
  uint8_t pay[GM_NTOK];
  CHECK(gm_price(p, card, pay) && pay[GM_WILD] == 1, "topaz covers one missing stone");
  p->tok[GM_WILD] = 0;
  CONTROL(!gm_price(p, card, pay), "without the topaz it cannot be bought");
  p->bonus[short_color] = 1;
  p->ncards = 1;
  CHECK(gm_price(p, card, pay) && pay[GM_WILD] == 0, "a bonus discounts the missing stone");
}

static void test_open_reserve_and_draw(void) {
  bk_game g;
  gm_rules(&g, 2);
  gm_state st;
  gm_new(&st, 2, 5);
  gm_apply(&st, gm_move(GM_MV_RESERVE_DECK, 2, 0, 0), NULL);
  CHECK(gm_turn(&st, NULL) == BK_CHANCE, "a deck set-aside is a chance draw");
  bk_move out[BK_MAX_MOVES];
  uint32_t w[BK_MAX_MOVES];
  CHECK(gm_outcomes(&st, out, w, BK_MAX_MOVES, NULL) == 16, "the draw lists the 16 cards left in deck 3");
  bk_settle(&g, &st, &st.rng, NULL);
  CHECK(st.s[0].nres == 1 && st.where[st.s[0].res[0]] == GM_AT_RESERVED, "the drawn card sits face up in seat 0's set-asides");
  CHECK(st.s[0].tok[GM_WILD] == 1 && st.seat == 1, "a topaz came with it and the turn passed");
  /* reserve cap */
  st.seat = 0;
  st.s[0].nres = 3;
  CHECK(count_kind(&st, GM_MV_RESERVE) == 0, "three set-asides: no more");
}

static void test_patron_and_end(void) {
  gm_state st;
  gm_new(&st, 2, 3);
  int pat = -1;
  for (int i = 0; i < GM_NPATRONS; i++)
    if (st.patron_open[i] == 1) { pat = i; break; }
  gm_seat *p = &st.s[0];
  for (int c = 0; c < GM_NCOLORS; c++) p->bonus[c] = gm_patron_set[pat].need[c];
  gm_apply(&st, gm_move(GM_MV_TAKE3, 0, 1, 2), NULL);
  CHECK(st.patron_open[pat] == 2 && st.patron_owner[pat] == 0 && p->points == 3, "a patron whose needs are met joins");
  /* reaching 15 on seat 0 ends after seat 1 plays */
  st.s[0].points = 15;
  st.seat = 0;
  st.phase = GM_PH_ACT;
  gm_apply(&st, gm_move(GM_MV_TAKE3, 2, 3, 4), NULL);
  CHECK(!gm_terminal(&st, NULL) && st.last_round, "the round finishes first");
  gm_apply(&st, gm_move(GM_MV_TAKE3, 0, 1, 2), NULL);
  CHECK(gm_terminal(&st, NULL) && st.winners == 1, "seat 0 wins at the round's end (winners %d)", st.winners);
  /* tie on points: fewest commissions wins */
  gm_new(&st, 2, 3);
  st.s[0].points = st.s[1].points = 16;
  st.s[0].ncards = 9; st.s[1].ncards = 8;
  st.last_round = 1; st.seat = 1;
  gm_apply(&st, gm_move(GM_MV_TAKE3, 0, 1, 2), NULL);
  CHECK(st.winners == 2, "tie broken by fewer commissions (winners %d)", st.winners);
}

static void test_full_games(void) {
  pick_kind rnd[4] = {PICK_RANDOM, PICK_RANDOM, PICK_RANDOM, PICK_RANDOM};
  pick_kind gr[4] = {PICK_GREEDY, PICK_GREEDY, PICK_GREEDY, PICK_GREEDY};
  for (int players = 2; players <= 4; players++) {
    for (uint64_t seed = 1; seed <= 60; seed++) {
      result r = play(players, seed, rnd, true);
      CHECK(r.ok && r.winners && r.winners < (1 << players), "random %dp seed %llu winners %d", players, (unsigned long long)seed, r.winners);
    }
    int ended = 0, turns = 0;
    for (uint64_t seed = 1; seed <= 20; seed++) {
      result r = play(players, seed, gr, true);
      CHECK(r.ok && r.winners && r.winners < (1 << players), "greedy %dp seed %llu", players, (unsigned long long)seed);
      if (r.turns < GM_MAX_TURNS) ended++;
      turns += r.turns;
    }
    printf("greedy %dp: %d/20 reached 15 points, %.1f turns per game\n", players, ended, turns / 20.0);
    CHECK(ended == 20, "greedy games reach 15 points (not the stall cap)");
  }
  /* greedy beats random: seat 0 greedy vs random */
  int wins = 0;
  for (uint64_t seed = 1; seed <= 40; seed++) {
    pick_kind k[4] = {PICK_GREEDY, PICK_RANDOM};
    if (play(2, seed, k, false).winners & 1) wins++;
  }
  printf("greedy vs random: %d/40\n", wins);
  CHECK(wins >= 36, "greedy beats random");
}

static void test_determinism(void) {
  pick_kind gr[4] = {PICK_GREEDY, PICK_GREEDY, PICK_GREEDY, PICK_GREEDY};
  result a = play(3, 99, gr, false), b = play(3, 99, gr, false), c = play(3, 100, gr, false);
  CHECK(a.hash == b.hash, "same seed, same game");
  CONTROL(a.hash != c.hash, "another seed differs");
}

int main(void) {
  test_setup();
  test_moves();
  test_hand_cap_and_return();
  test_buy_and_discount();
  test_open_reserve_and_draw();
  test_patron_and_end();
  test_full_games();
  test_determinism();
  if (fails) { fprintf(stderr, "%d checks failed\n", fails); return 1; }
  printf("facet rules: all checks pass\n");
  return 0;
}
