/* bk_pieces: decks, market rows, bags and dice.
 *
 * Checks: a deck draws every card exactly once and its draws are uniform (chi-square over many seeded draws);
 * outcomes list each distinct card with its count; a market row refills only its empty slots; a bag draws by
 * its counts; a die is fair and its outcomes are its faces; the same seed draws the same.
 *
 * Also: the die helper's outcomes drive the engine's search (expectimax): a dice choice of banking 3.4 or rolling
 * a die and banking its face rolls (3.5 beats 3.4).
 *
 * CONTROLS that must fail: the uniformity test run on a draw biased toward the first card; the dice choice with a
 * die loaded toward 1 still rolling. */
#include <math.h>
#include <string.h>

#include "boardkit/bk_pieces.h"
#include "check.h"
#include "tiny_games.h"

/* chi-square of counts against a uniform expectation */
static double chi2(const int *count, int k, int total) {
  double e = (double)total / k, x = 0;
  for (int i = 0; i < k; i++) x += (count[i] - e) * (count[i] - e) / e;
  return x;
}
/* a broken draw: picks the first card a third of the time */
static uint16_t biased_draw(uint16_t *cards, int *n, cw_rng *rng) {
  if (cw_rng_int(rng, 0, 2) == 0) {
    uint16_t c = cards[0];
    cards[0] = cards[--(*n)];
    return c;
  }
  return bk_deck_draw(cards, n, rng);
}

int main(void) {
  cw_rng r;
  cw_rng_seed(&r, 1);
  uint16_t deck[64];
  int n = 0;
  for (uint16_t i = 0; i < 40; i++) bk_deck_add(deck, &n, 64, i);
  int seen[40] = { 0 };
  for (int i = 0; i < 40; i++) seen[bk_deck_draw(deck, &n, &r)]++;
  bool once = n == 0 && bk_deck_draw(deck, &n, &r) == BK_NO_CARD;
  for (int i = 0; i < 40; i++) once = once && seen[i] == 1;
  CHECK(once, "a deck of 40 draws each card exactly once, then nothing");

  /* the first draw of a fresh 10-card deck, 20000 times: uniform (chi-square, 9 degrees: < 27.9 at p = 0.001) */
  int first[10] = { 0 }, firstb[10] = { 0 };
  for (int t = 0; t < 20000; t++) {
    uint16_t d[10];
    int k = 0;
    for (uint16_t i = 0; i < 10; i++) bk_deck_add(d, &k, 10, i);
    first[bk_deck_draw(d, &k, &r)]++;
    k = 0;
    for (uint16_t i = 0; i < 10; i++) bk_deck_add(d, &k, 10, i);
    firstb[biased_draw(d, &k, &r)]++;
  }
  double x = chi2(first, 10, 20000), xb = chi2(firstb, 10, 20000);
  CHECK(x < 27.9, "deck draws are uniform (chi-square %.1f < 27.9)", x);
  CONTROL(xb >= 27.9, "a draw biased toward the first card fails the uniformity test (chi-square %.1f)", xb);

  uint16_t pile[6] = { 5, 2, 5, 9, 2, 5 };
  bk_move oc[8];
  uint32_t w[8];
  int k = bk_deck_outcomes(pile, 6, oc, w, 8, NULL);
  CHECK(k == 3 && oc[0] == 2 && w[0] == 2 && oc[1] == 5 && w[1] == 3 && oc[2] == 9 && w[2] == 1,
        "outcomes: each distinct card once with its count, ascending");
  CHECK(bk_deck_remove(pile, &(int){ 6 }, 9), "remove a named card");

  uint16_t slots[4] = { 3, BK_NO_CARD, 7, BK_NO_CARD }, src[5] = { 10, 11, 12, 13, 14 };
  int sn = 5;
  int placed = bk_market_refill(slots, 4, src, &sn, &r);
  CHECK(placed == 2 && slots[0] == 3 && slots[2] == 7 && slots[1] >= 10 && slots[3] >= 10 && sn == 3 && bk_market_empty_slot(slots, 4) == -1,
        "a market row refills only its empty slots");

  uint8_t bag[3] = { 30, 10, 0 }, bag2[3];
  int got[3] = { 0 };
  for (int t = 0; t < 4000; t++) {
    memcpy(bag2, bag, 3);
    got[bk_bag_draw(bag2, 3, &r)]++;
  }
  CHECK(got[2] == 0 && fabs(got[0] / 4000.0 - 0.75) < 0.03, "a bag draws by its counts (%d : %d : %d)", got[0], got[1], got[2]);
  k = bk_bag_outcomes(bag, 3, oc, w, 8, NULL);
  CHECK(k == 2 && w[0] == 30 && w[1] == 10, "bag outcomes skip empty kinds");

  int faces[6] = { 0 };
  for (int t = 0; t < 12000; t++) faces[bk_die_roll(&r, 6) - 1]++;
  x = chi2(faces, 6, 12000);
  CHECK(x < 20.5, "a die is fair (chi-square %.1f < 20.5)", x);
  CHECK(bk_die_outcomes(6, oc, w, 8, NULL) == 6 && oc[0] == 1 && oc[5] == 6, "die outcomes are its faces");

  cw_rng a, b;
  cw_rng_seed(&a, 99);
  cw_rng_seed(&b, 99);
  uint16_t da[20], db[20];
  int na = 0, nb = 0;
  for (uint16_t i = 0; i < 20; i++) bk_deck_add(da, &na, 20, i), bk_deck_add(db, &nb, 20, i);
  bk_shuffle(da, na, &a);
  bk_shuffle(db, nb, &b);
  CHECK(!memcmp(da, db, sizeof da) && memcmp(da, (uint16_t[20]){ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 }, sizeof da),
        "the same seed shuffles the same, and a shuffle moves cards");
  /* the dice choice through cw_search with bk_die_outcomes */
  {
    dice d0 = { 0 };
    cw_search *s = cw_search_new(&DICE, &(cw_search_options){ .kind = CW_SEARCH_MINIMAX, .max_depth = 2 });
    bk_move mv = BK_MOVE_NONE;
    CHECK(cw_search_run(s, &d0, &mv) && mv == 1, "the engine's search rolls a fair die over banking 3.4 (move %llu)", (unsigned long long)mv);
    die_w[0] = 10; /* CONTROL: loaded toward 1 */
    cw_search_run(s, &d0, &mv);
    die_w[0] = 1;
    CONTROL(mv == 0, "a die loaded toward 1 is banked instead");
    cw_search_free(s);
    dice d2 = { .pending = 1 };
    cw_rng rr;
    cw_rng_seed(&rr, 4);
    bk_move last = 0;
    CHECK(bk_settle(&DICE, &d2, &rr, &last) == 1 && d2.done && last >= 1 && last <= 6 && d2.banked == (int)last * 10,
          "bk_settle rolls the die through outcomes once (face %llu)", (unsigned long long)last);
  }
  return DONE("pieces");
}
