/* boardkit: CPU seats. A CPU thinks with the engine's game-tree search (cartwheel/search.h) over ticks, then
 * enters its move by pressing the same actions a person's pad does, one press at a time. The game hands the
 * presses to the engine with cw_input_set_actions(seat, ...) in fixed_update, before its rules read input, so
 * the rules never know a seat is a CPU and the input recording, a replay and the input hash carry its moves.
 *
 *   // init: one bk_cpu per CPU seat (cw_shell_seat_cpu gives the level)
 *   cpu[s] = bk_cpu_new(&RULES, &(bk_cpu_desc){ .level = lv, .next_press = enter_move, .user = &G });
 *   // fixed_update, before the rules read input:
 *   cw_action_value v[4];
 *   int n = bk_cpu_tick(cpu[s], match, s, v, 4);
 *   cw_input_set_actions(s, v, n);           // every tick, also with n == 0 (the seat's actions released)
 *
 * next_press is the game's: given the state and the move the CPU wants, the next press that brings it
 * closer (a cursor hop toward the target, then confirm). bk_cpu_press_toward does that for cursors of the
 * engine's cursor module. The driver presses until the match log moves (bk_match_log_next), i.e. the rules
 * took a decision, then thinks again. A CPU that presses BK_CPU_MAX_PRESSES times without the move landing is
 * a bug in next_press or in the rules: it stops the game (CW_FATAL) instead of pressing forever.
 *
 * Pacing: a CPU thinks at least think_ticks (so a move never lands the tick the turn starts) and waits
 * press_gap ticks between presses, so people can follow the cursor. Both shrink with level by default. Tests
 * and self-playing carts may set them to 1. */
#ifndef BK_CPU_H
#define BK_CPU_H

#include "bk_match.h"
#include "cartwheel/cursor.h"
#include "cartwheel/input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_CPU_MAX_PRESSES 400

/* one press: up to two actions held together (a diagonal) for `hold` ticks (>= 1) */
typedef struct bk_press {
  cw_action_value v[2];
  int count;
  int hold;
} bk_press;

/* the next press toward entering `move`; false when there is nothing to press now (the driver waits a tick) */
typedef bool (*bk_cpu_next_press_fn)(const void *state, bk_move move, int seat, bk_press *out, void *user);

typedef struct bk_cpu_desc {
  int level;                     /* 0 Easy, 1 Normal, 2 Hard (cw_shell_cpu_level) */
  cw_search_kind kind;           /* CW_SEARCH_MINIMAX (default), CW_SEARCH_MCTS or CW_SEARCH_GREEDY */
  const cw_search_options *options; /* NULL: cw_search_level_options(level, kind) */
  int units_per_tick;            /* search nodes per tick (4000); the answer never depends on it */
  int think_ticks;               /* least ticks before the first press (45, 30, 24 by level) */
  int press_gap;                 /* ticks between presses (10, 7, 5 by level) */
  uint64_t seed;                 /* mixed with the state hash and the seat for each decision's search */
  bk_cpu_next_press_fn next_press;
  void *user;
} bk_cpu_desc;

typedef struct bk_cpu bk_cpu;

bk_cpu *bk_cpu_new(const bk_game *g, const bk_cpu_desc *d);
void bk_cpu_free(bk_cpu *c);
/* This tick's actions for the seat (count, <= cap): think, then press. Call it every tick for every CPU
 * seat, its turn or not (off its turn it returns 0 and forgets any plan). */
int bk_cpu_tick(bk_cpu *c, const bk_match *m, int seat, cw_action_value *out, int cap);
/* The move the CPU is entering, BK_MOVE_NONE while thinking or idle (for a ghost preview or a caption). */
bk_move bk_cpu_intent(const bk_cpu *c);
bool bk_cpu_thinking(const bk_cpu *c);
/* the last search's figures (debug fields, tests) */
cw_search_stats bk_cpu_last_stats(const bk_cpu *c);

/* For next_press with the engine's cursors: the press that moves player's cursor toward `target`, or
 * `confirm` when it stands there. Tries the eight stick ways through cw_cursor_hop_select (the hop a pad would
 * make) and picks the one landing nearest the target on that player's screen, so it walks any layout the
 * cursor module can. d: the cursors' desc (NULL = the defaults). False when no way gets nearer (an
 * unreachable target: a bug the caller should report). */
bool bk_cpu_press_toward(const cw_cursors *cur, const cw_cursor_desc *d, int player, int target, cw_action left,
                         cw_action right, cw_action up, cw_action down, cw_action confirm, bk_press *out);
/* bk_cpu_press_toward with the targets a hop may land on: allow(player, target, allow_user) false skips a target,
 * as cw_cursors_set_allow does for a pad (pass the same callback, so the CPU hops exactly as a person can). A hop
 * never lands on a refused target; the target itself must be allowed. NULL allow = every target. */
bool bk_cpu_press_toward_allow(const cw_cursors *cur, const cw_cursor_desc *d, cw_cursor_allow_fn allow, void *allow_user,
                               int player, int target, cw_action left, cw_action right, cw_action up, cw_action down,
                               cw_action confirm, bk_press *out);

#ifdef __cplusplus
}
#endif
#endif
