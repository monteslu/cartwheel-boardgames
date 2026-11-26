/* boardkit: decks, open market rows, draw bags and dice, as plain data inside a game's flat state.
 *
 * Nothing here holds a pointer: a deck is an array of card ids and a count in the game's own state struct,
 * so the state stays memcpy-able and hashable. Hidden order is never decided early (bk_game.h): a draw takes
 * a card at random at the moment it is drawn, so search cannot peek at the next card. The draw functions
 * take the rng chance() was handed; the outcome helpers list the same draws with weights for outcomes().
 *
 * Card ids are uint16_t; BK_NO_CARD marks an empty slot in a market row. */
#ifndef BK_PIECES_H
#define BK_PIECES_H

#include <stdbool.h>
#include <stdint.h>

#include "bk_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_NO_CARD 0xffffu

/* ── decks (an unordered pile: a draw is a uniform pick) ── */
/* add a card on the pile; false when full (n == cap) */
bool bk_deck_add(uint16_t *cards, int *n, int cap, uint16_t card);
/* draw a random card (removed; the pile's order is not meaningful); BK_NO_CARD when empty */
uint16_t bk_deck_draw(uint16_t *cards, int *n, cw_rng *rng);
/* remove a given card (the first copy); false when absent */
bool bk_deck_remove(uint16_t *cards, int *n, uint16_t card);
/* the distinct cards of the pile with their counts as outcome weights (for outcomes()); `code(card)` packs a
 * card into the move code the game's apply() takes at that chance node; count (<= cap). Cards are listed in
 * ascending id order. */
int bk_deck_outcomes(const uint16_t *cards, int n, bk_move *out, uint32_t *weight, int cap, bk_move (*code)(uint16_t card));
/* an ordered pile shuffled now (an open deck everyone may see, or a setup step): Fisher-Yates with rng */
void bk_shuffle(uint16_t *cards, int n, cw_rng *rng);

/* ── market rows (open cards in slots) ── */
/* fill every empty slot (left to right) by drawing from the deck; returns the cards placed */
int bk_market_refill(uint16_t *slots, int nslots, uint16_t *deck, int *deck_n, cw_rng *rng);
/* the first empty slot, -1 none */
int bk_market_empty_slot(const uint16_t *slots, int nslots);

/* ── draw bags (counts per kind: tiles, cubes, chips) ── */
int bk_bag_total(const uint8_t *counts, int kinds);
/* draw one: a kind picked with weight = its count, the count lowered; -1 when empty */
int bk_bag_draw(uint8_t *counts, int kinds, cw_rng *rng);
/* the kinds with a count as weighted outcomes, codes from code(kind); count (<= cap) */
int bk_bag_outcomes(const uint8_t *counts, int kinds, bk_move *out, uint32_t *weight, int cap, bk_move (*code)(int kind));

/* ── dice ── */
/* a fair roll, 1..sides */
int bk_die_roll(cw_rng *rng, int sides);
/* the faces as outcomes, codes from code(face) (face 1..sides) */
int bk_die_outcomes(int sides, bk_move *out, uint32_t *weight, int cap, bk_move (*code)(int face));

#ifdef __cplusplus
}
#endif
#endif
