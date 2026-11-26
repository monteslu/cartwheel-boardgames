/* Facet: the rules, pure and headless (bk_game.h). A lapidary guild: players gather loose stones from the
 * velvet tray, spend them on commissions (cards) that each pay a permanent bonus stone and maybe points, and
 * win patrons by owning enough bonuses. 15 points ends the game at the end of that round.
 *
 * Turn, one action:
 *   - take three stones of different colours (fewer only when fewer colours are left), or
 *   - take two stones of one colour when that pile holds at least four, or
 *   - set aside a commission (from the market, or the top of a tier's deck) and take a topaz (the wild stone) if
 *     one is left; at most three set aside. Set-aside cards are always face up for everyone, even from the deck.
 *   - fulfil a commission from the market or from your own set-aside ones: pay its cost less your bonuses, topaz
 *     covering any shortfall.
 * A take or set-aside that lifts a hand past ten names the stones handed back, in the same move; a patron whose bonus needs you meet joins you (choose when two or
 * more do). Market refills from the decks are chance draws (turn() == BK_CHANCE), as is a deck set-aside.
 *
 * End: once a seat has 15 points the round finishes (the last seat in order plays); most points wins, ties to
 * the fewest commissions, then shared. A long stall (GM_MAX_TURNS turns) also ends it the same way. */
#ifndef FACET_RULES_H
#define FACET_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boardkit/bk_game.h"

#define GM_NCOLORS 5   /* ruby, sapphire, emerald, amethyst, diamond */
#define GM_WILD 5      /* topaz */
#define GM_NTOK 6
#define GM_NCARDS 90
#define GM_NPATRONS 10
#define GM_TIERS 3
#define GM_SLOTS 4
#define GM_MAX_RES 3
#define GM_HAND_CAP 10
#define GM_WIN_POINTS 15
#define GM_MAX_TURNS 400
#define GM_NONE 0xff

typedef struct gm_card {
  uint8_t tier;     /* 0..2 */
  uint8_t bonus;    /* colour 0..4 */
  uint8_t points;
  uint8_t cost[GM_NCOLORS];
  const char *name;
} gm_card;

typedef struct gm_patron {
  uint8_t points;
  uint8_t need[GM_NCOLORS];
  const char *name;
} gm_patron;

extern const gm_card *const gm_card_set;     /* GM_NCARDS */
extern const gm_patron *const gm_patron_set; /* GM_NPATRONS */

/* where a card is */
enum { GM_AT_DECK = 0, GM_AT_MARKET, GM_AT_RESERVED, GM_AT_BOUGHT, GM_AT_OUT };
/* phase of the seat's turn */
enum { GM_PH_ACT = 0, GM_PH_PATRON, GM_PH_OVER };
/* the pending draw (chance) */
enum { GM_DRAW_NONE = 0, GM_DRAW_MARKET, GM_DRAW_RESERVE };

typedef struct gm_seat {
  uint8_t tok[GM_NTOK];
  uint8_t bonus[GM_NCOLORS];
  uint8_t points;
  uint8_t nres;
  uint8_t res[GM_MAX_RES];   /* card ids, GM_NONE when empty */
  uint8_t ncards;            /* commissions fulfilled */
  uint8_t npatrons;
  uint8_t pad;
} gm_seat;

typedef struct gm_state {
  cw_rng rng;
  uint8_t players, seat, phase, last_round;
  uint8_t draw_kind, draw_tier, draw_slot, winners; /* winners: seat bitmask once over */
  uint16_t turns;                                    /* turns completed */
  uint16_t pad16;
  uint8_t bank[GM_NTOK];
  uint8_t market[GM_TIERS][GM_SLOTS];                /* card ids or GM_NONE */
  uint8_t where[GM_NCARDS];                          /* GM_AT_* */
  uint8_t owner[GM_NCARDS];                          /* the seat holding a set-aside or fulfilled card */
  uint8_t patron_open[GM_NPATRONS];                  /* 1 on the table, 2 taken, 0 not in this game */
  uint8_t patron_owner[GM_NPATRONS];
  uint8_t last_kind, last_a, last_b, last_seat;      /* the last action (for the view and logs) */
  gm_seat s[BK_MAX_SEATS];
} gm_state;

/* move kinds (field 0) */
enum {
  /* takes and set-asides carry their hand-backs in fields 4..6 (colours 0..5, GM_NONE when unused): exactly
   * the stones past ten, so a move is a whole turn */
  GM_MV_TAKE3 = 1,   /* f1..f3 colours (GM_NONE for fewer) */
  GM_MV_TAKE2,       /* f1 colour */
  GM_MV_BUY,         /* f1 tier, f2 slot */
  GM_MV_BUY_RES,     /* f1 reserve index */
  GM_MV_RESERVE,     /* f1 tier, f2 slot */
  GM_MV_RESERVE_DECK,/* f1 tier */
  GM_MV_PATRON,      /* f1 patron id */
  GM_MV_PASS,
  GM_MV_DRAW,        /* chance outcome: f1 card id */
};

/* a move with no hand-backs, and one with up to three (GM_NONE for unused) */
static inline bk_move gm_move(int kind, int a, int b, int c) { return bk_move_pack(kind, a, b, c, GM_NONE, GM_NONE, GM_NONE, 0); }
static inline bk_move gm_move_ret(int kind, int a, int b, int c, int r0, int r1, int r2) { return bk_move_pack(kind, a, b, c, r0, r1, r2, 0); }

/* deal a new game: players 2..4, seed for the table's order */
void gm_new(gm_state *st, int players, uint64_t seed);
void gm_rules(bk_game *g, int players);
void gm_label(const void *state, bk_move m, char *buf, size_t cap, void *user);

/* query helpers for the view and tests */
int gm_hand(const gm_seat *p);
/* what fulfilling card id costs seat p now: stones per colour and topaz; false when it cannot */
bool gm_price(const gm_seat *p, int card, uint8_t pay[GM_NTOK]);
bool gm_patron_met(const gm_seat *p, int patron);
int gm_deck_left(const gm_state *st, int tier);

/* the bk_game callbacks (exposed for tests) */
int gm_moves(const void *state, bk_move *out, int cap, void *user);
void gm_apply(void *state, bk_move m, void *user);
int gm_turn(const void *state, void *user);
bool gm_terminal(const void *state, void *user);
void gm_evaluate(const void *state, float score[BK_MAX_SEATS], void *user);
void gm_chance(void *state, cw_rng *rng, void *user);
int gm_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user);

#endif
