/* boardkit: decks, market rows, bags and dice (bk_pieces.h). */
#include "bk_pieces.h"

bool bk_deck_add(uint16_t *cards, int *n, int cap, uint16_t card) {
  if (*n >= cap) return false;
  cards[(*n)++] = card;
  return true;
}

uint16_t bk_deck_draw(uint16_t *cards, int *n, cw_rng *rng) {
  if (*n <= 0) return BK_NO_CARD;
  int i = cw_rng_int(rng, 0, *n - 1);
  uint16_t c = cards[i];
  cards[i] = cards[--(*n)];
  return c;
}

bool bk_deck_remove(uint16_t *cards, int *n, uint16_t card) {
  for (int i = 0; i < *n; i++)
    if (cards[i] == card) {
      for (int j = i + 1; j < *n; j++) cards[j - 1] = cards[j];
      (*n)--;
      return true;
    }
  return false;
}

int bk_deck_outcomes(const uint16_t *cards, int n, bk_move *out, uint32_t *weight, int cap, bk_move (*code)(uint16_t card)) {
  int k = 0;
  uint32_t last = 0x10000u; /* ascending ids without sorting the pile: repeated min scans */
  for (;;) {
    uint32_t next = 0x10000u;
    int count = 0;
    for (int i = 0; i < n; i++) {
      uint32_t c = cards[i];
      if ((last == 0x10000u || c > last) && c < next) next = c, count = 0;
      if (c == next) count++;
    }
    if (next == 0x10000u || k >= cap) break;
    out[k] = code ? code((uint16_t)next) : (bk_move)next;
    weight[k++] = (uint32_t)count;
    last = next;
  }
  return k;
}

void bk_shuffle(uint16_t *cards, int n, cw_rng *rng) {
  for (int i = n - 1; i > 0; i--) {
    int j = cw_rng_int(rng, 0, i);
    uint16_t t = cards[i];
    cards[i] = cards[j];
    cards[j] = t;
  }
}

int bk_market_refill(uint16_t *slots, int nslots, uint16_t *deck, int *deck_n, cw_rng *rng) {
  int placed = 0;
  for (int i = 0; i < nslots && *deck_n > 0; i++)
    if (slots[i] == BK_NO_CARD) slots[i] = bk_deck_draw(deck, deck_n, rng), placed++;
  return placed;
}

int bk_market_empty_slot(const uint16_t *slots, int nslots) {
  for (int i = 0; i < nslots; i++)
    if (slots[i] == BK_NO_CARD) return i;
  return -1;
}

int bk_bag_total(const uint8_t *counts, int kinds) {
  int t = 0;
  for (int i = 0; i < kinds; i++) t += counts[i];
  return t;
}

int bk_bag_draw(uint8_t *counts, int kinds, cw_rng *rng) {
  int t = bk_bag_total(counts, kinds);
  if (t <= 0) return -1;
  int r = cw_rng_int(rng, 0, t - 1);
  for (int i = 0; i < kinds; i++) {
    if (r < counts[i]) { counts[i]--; return i; }
    r -= counts[i];
  }
  CW_FATAL("bk_bag_draw: unreachable (counts changed under the draw)");
}

int bk_bag_outcomes(const uint8_t *counts, int kinds, bk_move *out, uint32_t *weight, int cap, bk_move (*code)(int kind)) {
  int k = 0;
  for (int i = 0; i < kinds && k < cap; i++)
    if (counts[i]) out[k] = code ? code(i) : (bk_move)i, weight[k++] = counts[i];
  return k;
}

int bk_die_roll(cw_rng *rng, int sides) { return cw_rng_int(rng, 1, sides); }

int bk_die_outcomes(int sides, bk_move *out, uint32_t *weight, int cap, bk_move (*code)(int face)) {
  int k = 0;
  for (int f = 1; f <= sides && k < cap; f++) out[k] = code ? code(f) : (bk_move)f, weight[k++] = 1;
  return k;
}
