/* boardkit: choosing among things on the table (a market row, a factory display, board spaces, a hand) with a
 * pad, a mouse or a finger, built on the engine's cursors (cartwheel/cursor.h).
 *
 * What it adds to cw_cursors:
 *   - targets switched on and off per moment (only legal choices: the cursor never stops on the others, and a
 *     tap on one is ignored), from the rules' legal moves;
 *   - the pointer as recorded input: the engine's pointer channel (cw_input_pointer_channel, switched on by
 *     bk_select_new) records, replays and hashes clicks and taps with the actions, and a tap is resolved to a
 *     target in the tick (bk_select_feed, from cw_input_pointer), so a replay picks what the match picked;
 *   - one event per tick per player: moved, confirm, back, or pointed (a tap on another target moves the
 *     cursor there; a tap on the target already chosen confirms it: tap to look, tap again to play, with the
 *     game's ghost preview between);
 *   - an eased highlight per target for drawing (no flashing).
 *
 *   init():          sel = bk_select_new(&desc);                         // before the match's recording begins
 *   update():        bk_select_pointer(sel, seat_with_pointer, tree);   // per frame: the hover mark
 *   fixed_update():  bk_select_feed(sel, seat_with_pointer);            // before the rules read input: a tap
 *                    bk_sel_event e = bk_select_tick(sel, player);      // the player's event this tick
 *
 * A CPU seat needs none of this: bk_cpu presses the same directional and confirm actions (bk_cpu_press_toward
 * over bk_select_cursors). */
#ifndef BK_SELECT_H
#define BK_SELECT_H

#include <stdbool.h>

#include "cartwheel/cursor.h"
#include "cartwheel/input.h"
#include "cartwheel/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_SELECT_MAX_TARGETS 511

typedef struct bk_select_desc {
  cw_cursor_desc cursor;   /* the cursors' settings (zero fields: the engine's defaults) */
  cw_action left, right, up, down, confirm, back; /* the game's actions (back may be -1: none) */
  float pick_radius;       /* pointer: a tap within this many canvas px of a target's point picks it (56) */
} bk_select_desc;

typedef enum { BK_SEL_NONE, BK_SEL_MOVED, BK_SEL_CONFIRM, BK_SEL_BACK, BK_SEL_POINTED } bk_sel_kind;
typedef struct bk_sel_event {
  bk_sel_kind kind;
  int target;   /* the cursor's target after the event (-1 none) */
  bool pointer; /* the event came from the pointer */
} bk_sel_event;

typedef struct bk_select bk_select;

/* Also switches the engine's pointer channel on (cw_input_pointer_channel): make it in init, before a recording begins. */
bk_select *bk_select_new(const bk_select_desc *d);
void bk_select_free(bk_select *s);
/* the engine cursors underneath: set targets (cw_cursors_set_nodes / _points) and views here */
cw_cursors *bk_select_cursors(bk_select *s);
/* which targets may be chosen now (all on after targets are set); a cursor on a target switched off moves to
 * the nearest one on */
void bk_select_enable_all(bk_select *s, bool on);
void bk_select_set_enabled(bk_select *s, int target, bool on);
bool bk_select_enabled(const bk_select *s, int target);
/* Per rendered frame: the enabled target under the mouse for `seat` (the seat whose turn holds the pointer), for
 * drawing only (bk_select_hover). tree is unused (kept for the callers). */
void bk_select_pointer(bk_select *s, int seat, cw_tree *t);
/* In fixed_update before the rules read input: this tick's click or tap (cw_input_pointer: live, or the recording's
 * in a replay) on an enabled target becomes a pick for the seat, which bk_select_tick(seat) reports. seat < 0: none. */
void bk_select_feed(bk_select *s, int seat);
/* In fixed_update: the player's stick or d-pad hops (cw_cursors_step), confirm, back and a recorded pick. */
bk_sel_event bk_select_tick(bk_select *s, int player);
int bk_select_target(const bk_select *s, int player);
void bk_select_place(bk_select *s, int player, int target);
/* The pointer's hover target (-1 none), for drawing only. */
int bk_select_hover(const bk_select *s);
/* Highlight of a target for a player, eased 0..1 (1 under that player's cursor); call bk_select_update once a
 * frame. */
void bk_select_update(bk_select *s, float dt);
float bk_select_glow(const bk_select *s, int player, int target);

#ifdef __cplusplus
}
#endif
#endif
