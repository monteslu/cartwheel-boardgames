/* kitdemo rules: a two-player push-your-luck card game, small enough to read in one sitting, using every rules
 * part of the kit (a bk_game with chance, a deck and a market row, two decisions in a turn).
 *
 * Fifteen cards: values 1 to 5 in three colours. Three lie face up in a market row. On your turn:
 *   1. take a card from the market (undo allowed until step 2);
 *   2. bank it (score its value) or roll for it (a die: at or over its value scores double, under scores
 *      nothing).
 * The emptied slot refills from the deck. When the market is empty, the higher score wins (a tie is a draw).
 *
 * The state is flat and memcpy-able; the deck's order is never decided early (a refill is a chance event drawing
 * any card left in the deck). */
#ifndef KD_RULES_H
#define KD_RULES_H

#include <stdint.h>

#include "boardkit/bk_pieces.h"

#define KD_CARDS 15
#define KD_SLOTS 3

enum { KD_PICK, KD_CHOOSE, KD_ROLL, KD_REFILL, KD_OVER }; /* phases */
/* moves */
#define KD_MV_SLOT(s) ((bk_move)(s))       /* take the card in slot s */
#define KD_MV_BANK ((bk_move)10)
#define KD_MV_ROLL ((bk_move)11)
#define KD_MV_FACE(f) ((bk_move)(20 + (f))) /* chance: the die shows f */
#define KD_MV_CARD(c) ((bk_move)(100 + (c))) /* chance: card c refills the slot */

typedef struct kd_state {
  uint16_t deck[KD_CARDS];
  uint16_t market[KD_SLOTS];
  int32_t deck_n;
  int32_t score[2];
  int32_t turn, phase, slot, card; /* the slot taken this turn and its card */
  int32_t last_die, last_gain, moves;
  uint8_t owner[KD_CARDS]; /* whose pile a played card is in (0xff: none yet) */
} kd_state;

static inline int kd_value(int card) { return card % 5 + 1; }
static inline int kd_colour(int card) { return card / 5; }

extern const bk_game KD_RULES;
void kd_setup(kd_state *s);
/* 0 or 1 the winner, 2 a draw, -1 not over */
int kd_winner(const kd_state *s);

#endif
