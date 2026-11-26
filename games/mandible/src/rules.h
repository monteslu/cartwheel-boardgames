/* Mandible: the rules, pure and headless.
 *
 * Two players, white (seat 0) and black (seat 1), each with eleven hexagonal insect pieces and no board: pieces
 * are placed touching the group already on the table and then crawl around it. Surround the other side's Empress
 * on all six sides to win.
 *
 * The pieces (our names; the movement roles follow the classic base set):
 *   Empress (1)  one step, sliding.
 *   Ram     (2)  one step in any direction, and may climb onto other pieces and walk on top of them.
 *   Locust  (3)  jumps in a straight line over one or more pieces to the first empty space.
 *   Weaver  (2)  exactly three sliding steps around the group, never stepping back onto a space it used.
 *   Runner  (3)  any number of sliding steps around the group.
 *
 * Laws of the table:
 *   - The first piece goes anywhere (the centre); the second touches it. After that a new piece must touch
 *     your own pieces and must not touch any of the opponent's (only the top piece of a stack counts).
 *   - Your Empress must be on the table by your fourth turn. Until she is, none of your pieces may move.
 *   - One group: a move may never split the pieces into two groups, not even for a moment.
 *   - Sliding needs room: a piece cannot squeeze between two pieces that block the gap, and a slide must keep
 *     touching the group the whole way (Ram: the gap rule compares stack heights).
 *   - A covered piece cannot move. A player with no legal move passes.
 *   - Both Empresses surrounded at once is a draw; so is reaching SW_MAX_PLY plies, and so is the same position
 *     (the same pieces in the same places, the same side to move) coming up a third time, so a side that is
 *     ahead must make progress instead of shuffling.
 *
 * Coordinates are axial (q, r) with the engine's hex grid (cartwheel/hex.h, cw_axial: 0 (+1,0), 1 (+1,-1),
 * 2 (0,-1), 3 (-1,0), 4 (-1,+1), 5 (0,+1)). The state is flat and fixed size; a 32 by 32 wrapped index finds
 * the piece at a hex (the group never spans more than 23 hexes, so no two live hexes share an index). */
#ifndef MANDIBLE_RULES_H
#define MANDIBLE_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cartwheel/core.h"
#include "cartwheel/hex.h"
#include "boardkit/bk_game.h"
#include "cartwheel/search.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SW_EMPRESS, SW_RAM, SW_LOCUST, SW_WEAVER, SW_RUNNER, SW_KINDS };
#define SW_SIDE_PIECES 11
#define SW_PIECES 22
#define SW_GRID 32
#define SW_CELLS (SW_GRID * SW_GRID)
#define SW_MAX_PLY 400
#define SW_MAX_MOVES 512

enum { SW_ONGOING, SW_WHITE_WINS, SW_BLACK_WINS, SW_DRAW };
enum { SW_MOVE_PLACE = 1, SW_MOVE_STEP = 2, SW_MOVE_PASS = 3 };

typedef struct sw_piece {
  int16_t q, r;     /* where it is (valid when placed) */
  int8_t h;         /* 0 on the table, 1+ on top of others */
  int8_t below;     /* the piece directly under it, -1 none */
  uint8_t kind;     /* SW_EMPRESS.. */
  uint8_t side;     /* 0 white, 1 black */
  uint8_t placed;
  uint8_t pad[3];
} sw_piece;

typedef struct sw_state {
  sw_piece p[SW_PIECES];   /* ids 0..10 white, 11..21 black, in kind order */
  int8_t top[SW_CELLS];    /* id + 1 of the top piece at a wrapped hex, 0 empty */
  uint64_t key;            /* position hash (pieces by kind and side, side to move) */
  uint16_t ply;
  uint8_t to_move;         /* 0 or 1 */
  uint8_t result;          /* SW_ONGOING.. */
  uint8_t turns[2];        /* turns each side has taken (passes count) */
  uint8_t reps;            /* how many times this position has come up (1 = new); 3 ends the game */
  uint8_t pad;
  uint32_t hist[SW_MAX_PLY + 1]; /* the position key (low 32 bits) after each ply, hist[0] the start; 0 past ply */
} sw_state;

/* A move: kind (4 bits), piece id (8), from q, from r, to q, to r (12 bits each, two's complement). The top byte
 * stays zero, so a move is never BK_MOVE_NONE. */
typedef uint64_t sw_move;
static inline sw_move sw_move_make(int kind, int id, cw_axial from, cw_axial to) {
  return (uint64_t)(kind & 15) | (uint64_t)(id & 255) << 4 | (uint64_t)(from.q & 0xfff) << 12 |
         (uint64_t)(from.r & 0xfff) << 24 | (uint64_t)(to.q & 0xfff) << 36 | (uint64_t)(to.r & 0xfff) << 48;
}
static inline int sw_s12(uint64_t v) { return (int)((v & 0x800) ? (int)(v | ~(uint64_t)0xfff) : (int)v); }
static inline int sw_move_kind(sw_move m) { return (int)(m & 15); }
static inline int sw_move_id(sw_move m) { return (int)((m >> 4) & 255); }
static inline cw_axial sw_move_from(sw_move m) { return (cw_axial){ sw_s12((m >> 12) & 0xfff), sw_s12((m >> 24) & 0xfff) }; }
static inline cw_axial sw_move_to(sw_move m) { return (cw_axial){ sw_s12((m >> 36) & 0xfff), sw_s12((m >> 48) & 0xfff) }; }

static inline int sw_cell(cw_axial h) { return ((h.q & (SW_GRID - 1)) << 5) | (h.r & (SW_GRID - 1)); }

void sw_init(sw_state *s);
/* set a piece down on top of whatever is at `at` (setups for tests and the how-to-play scenes; no legality) */
void sw_put(sw_state *s, int id, cw_axial at);
/* legal moves of the side to move, fixed order; returns the count (0 only when the game is over) */
int sw_moves(const sw_state *s, sw_move *out, int cap);
void sw_apply(sw_state *s, sw_move m);
void sw_undo(sw_state *s, sw_move m);
bool sw_is_legal(const sw_state *s, sw_move m);
static inline bool sw_terminal(const sw_state *s) { return s->result != SW_ONGOING; }
/* true when the game ended as a draw because a position came up a third time */
static inline bool sw_drawn_by_repetition(const sw_state *s) { return s->result == SW_DRAW && s->reps >= 3; }

/* queries for the view and the evaluation */
int sw_top_at(const sw_state *s, cw_axial h);          /* top piece id at a hex, -1 none */
int sw_height_at(const sw_state *s, cw_axial h);       /* stack height (0 empty) */
int sw_in_hand(const sw_state *s, int side, int kind);
bool sw_empress_placed(const sw_state *s, int side);
int sw_empress_pressure(const sw_state *s, int side); /* occupied hexes around that side's Empress, 0..6 */
/* pieces that may not move because lifting them would split the group: out[id] = 1 (others 0) */
void sw_pinned(const sw_state *s, uint8_t out[SW_PIECES]);

/* the evaluation each seat sees (score[0] white, score[1] black, larger is better for that seat); terminal states
 * give the win/loss bands minus the plies played */
#define SW_WIN 1.0e6f
void sw_evaluate(const sw_state *s, float score[2]);
/* move ordering for the search: higher first (moves toward the other Empress, moves that free your own) */
float sw_priority(const sw_state *s, sw_move m);

/* names and labels (our names) */
const char *sw_kind_name(int kind);

/* the rules as the kit's bk_game (two seats, undo, the position key as hash, sw_priority ordering) */
const bk_game *mandible_bk_game(void);
/* the CPU levels (0 Easy, 1 Normal, 2 Hard) as search options; the caller sets the seed */
void mandible_level_options(int level, cw_search_options *o);
void sw_move_label(const sw_state *s, sw_move m, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
