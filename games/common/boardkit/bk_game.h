/* boardkit: a game's rules, as the engine's game-tree search takes them (cartwheel/search.h, engine v0.81.0).
 *
 * The rules ARE a cw_search_game: bk_game is the same type, so one struct drives the match (bk_match), the CPU
 * seats (bk_cpu, through cw_search_*), the headless tests and the replay check. Read the top of
 * cartwheel/search.h and the engine's docs/guides/game-search.md for the callbacks; the kit adds only the
 * rules below and a few helpers.
 *
 * Rules the state follows:
 *   - Flat and fixed size (state_size bytes), copied with memcpy unless `copy` is given. memset it to 0
 *     before filling it, so padding bytes are zero and the default hash (cw_hash_bytes over every byte) is
 *     the same on every host.
 *   - Randomness lives in the state (a cw_rng field) or in the match (bk_match's own seeded rng resolves
 *     chance). Rules never read a clock or a global RNG.
 *   - Hidden draws are never decided early: a deck or bag keeps its contents, not a pre-shuffled order search
 *     could peek at. When a die must roll or a card must be drawn, turn() returns BK_CHANCE; chance() resolves
 *     it with the rng it is handed and/or outcomes() lists the results with integer weights (bk_pieces.h has
 *     helpers for both). The match resolves chance before it asks a CPU (a chance root is refused).
 *   - A turn of several decisions (take three gems, then maybe return one) is one decision per move; the same
 *     seat may move several times in a row; turn() says so.
 *   - moves() lists in a fixed order (never pointer or hash-map order): search breaks ties by that order.
 *   - Terminal results: BK_WIN / BK_LOSS bands, a win less the moves played (search never adjusts by depth).
 *
 * Moves are 64-bit codes the game packs and unpacks (bk_move_pack below). Seats are 0..players-1 in the
 * state's own numbering; at most BK_MAX_SEATS (4). */
#ifndef BK_GAME_H
#define BK_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cartwheel/core.h"
#include "cartwheel/search.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef cw_search_game bk_game;
typedef cw_search_move bk_move;
#define BK_MAX_SEATS CW_SEARCH_MAX_PLAYERS
#define BK_CHANCE CW_SEARCH_CHANCE
#define BK_MOVE_NONE CW_SEARCH_MOVE_NONE
#define BK_MAX_MOVES CW_SEARCH_MAX_MOVES
#define BK_WIN CW_SEARCH_WIN
#define BK_LOSS CW_SEARCH_LOSS

/* UI data kept outside the rules struct: a short label for a move ("take 2 blue"), for logs, test failures
 * and a move list a game may show. */
typedef void (*bk_move_label_fn)(const void *state, bk_move move, char *buf, size_t cap, void *user);

/* ── helpers over a bk_game ─────────────────────────────────────────── */

void bk_copy(const bk_game *g, void *dst, const void *src);
/* copy src to dst, then apply move to dst */
void bk_apply_copy(const bk_game *g, const void *src, void *dst, bk_move move);
/* the game's hash, else cw_hash_bytes over state_size bytes, seed 0 (what search uses) */
uint64_t bk_hash(const bk_game *g, const void *state);
bool bk_is_legal(const bk_game *g, const void *state, bk_move move);
/* resolve chance one event at a time with rng (chance(), else a weighted pick from outcomes()) until a seat
 * is to move or the game is over; returns how many it resolved. *last (may be NULL) gets the last outcome code
 * picked from outcomes() (0 when chance() resolved it). */
int bk_settle(const bk_game *g, void *state, cw_rng *rng, bk_move *last);
/* a seed from two values (a match seed and a seat, a state hash): cw_hash_bytes over both */
uint64_t bk_seed_mix(uint64_t a, uint64_t b);

/* Pack up to eight 8-bit fields into a move (field 0 lowest), and read one back. (All eight at 0xff is
 * BK_MOVE_NONE: keep field 7 below 0xff.) */
static inline bk_move bk_move_pack(int f0, int f1, int f2, int f3, int f4, int f5, int f6, int f7) {
  return (bk_move)(uint8_t)f0 | (bk_move)(uint8_t)f1 << 8 | (bk_move)(uint8_t)f2 << 16 |
         (bk_move)(uint8_t)f3 << 24 | (bk_move)(uint8_t)f4 << 32 | (bk_move)(uint8_t)f5 << 40 |
         (bk_move)(uint8_t)f6 << 48 | (bk_move)(uint8_t)f7 << 56;
}
static inline int bk_move_field(bk_move m, int i) { return (int)((m >> (8 * i)) & 0xffu); }

#ifdef __cplusplus
}
#endif
#endif
