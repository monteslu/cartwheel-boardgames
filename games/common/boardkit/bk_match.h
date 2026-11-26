/* boardkit: the match sequencer. Holds the state of a match of a bk_game, plays moves, resolves chance with
 * the match's own seeded rng, keeps snapshots for undo and a log of what happened for the view.
 *
 * Phases are the game's: a phase is a field of its state (draft, then score, then refill), and moves() and
 * turn() follow it. The sequencer only sees decisions and chance events, so a phase change needs nothing
 * here; the log tells the view what changed (each entry carries the seat, the move or chance outcome, and the
 * state hash after it), and a game reads its phase from bk_match_state.
 *
 * Undo: a person may take back their own decisions within their current turn (consecutive decisions of one
 * seat), never past a chance event (a roll seen cannot be unseen) and never into another seat's turn. A
 * game may forbid undo after a decision that revealed something (bk_match_seal). CPUs never undo.
 *
 * Determinism: the match is a pure function of (initial state, chance seed, moves played). bk_match_hash is
 * the state hash for tests, gates and replays.
 *
 *   bk_match *m = bk_match_new(&RULES, &initial, seed);   // resolves any opening chance
 *   while (!bk_match_over(m)) { int seat = bk_match_turn(m); ... bk_match_play(m, move); }
 */
#ifndef BK_MATCH_H
#define BK_MATCH_H

#include "bk_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_MATCH_UNDO_MAX 64 /* decisions one turn can take back */

typedef enum { BK_LOG_MOVE, BK_LOG_CHANCE, BK_LOG_UNDO } bk_log_kind;
typedef struct bk_log_entry {
  bk_log_kind kind;
  int seat;          /* who moved (BK_CHANCE for a chance outcome) */
  bk_move move;      /* the decision; for BK_LOG_CHANCE the outcome when the game resolves chance through
                      * outcomes() (0 when through chance(): read the result from the state) */
  uint64_t hash;     /* the state hash after it */
  int index;         /* running number, from 0 */
} bk_log_entry;

typedef struct bk_match bk_match;

/* A match from `initial` (copied); chance is resolved with an rng seeded by `chance_seed`. */
bk_match *bk_match_new(const bk_game *g, const void *initial, uint64_t chance_seed);
void bk_match_free(bk_match *m);
const bk_game *bk_match_game(const bk_match *m);
const void *bk_match_state(const bk_match *m);
int bk_match_turn(const bk_match *m);       /* the seat to move; BK_CHANCE never shows (resolved at once) */
bool bk_match_over(const bk_match *m);
uint64_t bk_match_hash(const bk_match *m);
int bk_match_moves_played(const bk_match *m); /* decisions, not counting undone ones */
/* A kit-side legality check for moves no list can hold (a continuous flick: an angle and a power packed in the
 * move code). When set, bk_match_play accepts a move this says is legal instead of scanning moves(); the CPU's
 * search still plays from moves() (a game lists a sample of its continuous moves there). Not part of
 * cw_search_game. NULL restores the list check. */
typedef bool (*bk_legal_fn)(const void *state, bk_move move, void *user);
void bk_match_set_legal(bk_match *m, bk_legal_fn legal, void *user);
/* The legal moves now (count; <= cap). */
int bk_match_moves(const bk_match *m, bk_move *out, int cap);
/* Play a decision of the seat to move. False (nothing changes) when it is not legal. Chance that follows is
 * resolved at once. */
bool bk_match_play(bk_match *m, bk_move move);
/* Can the seat to move take back a decision now? */
bool bk_match_can_undo(const bk_match *m);
/* Take back the last decision of the seat to move (within its turn). False when not allowed. */
bool bk_match_undo(bk_match *m);
/* No undo past this point (the last decision revealed something). */
void bk_match_seal(bk_match *m);
/* The log: entries with index >= since, oldest first, up to cap; returns the count. The view keeps the last
 * index it showed. The log keeps the last BK_MATCH_LOG entries. */
#define BK_MATCH_LOG 256
int bk_match_log(const bk_match *m, int since, bk_log_entry *out, int cap);
int bk_match_log_next(const bk_match *m);   /* the index the next entry will get */

#ifdef __cplusplus
}
#endif
#endif
