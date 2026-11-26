/* Facet: the match (game.c). Holds the rules match (boardkit bk_match), the table's selection model (what the seat
 * to move has picked so far, which targets the cursor may stand on), the cursors and the CPU seats. Rules read
 * actions only: people and CPUs alike press the actions below for their seat (CPUs through cw_input_set_actions),
 * so a recording carries every move.
 *
 * A turn on the pad: hop the cursor over the table and press pick.
 *   - On a stone pile: the stone joins the pick (three colours, or two of one colour from a pile of four or more).
 *     Once the pick is a whole take the Take plaque lights and the cursor goes to it; pick on it takes them.
 *   - On a card (market, a set-aside of your own, or a deck): the Fulfil and Set aside plaques light for what is
 *     possible; pick on one does it.
 *   - Ending past ten stones: pick the stones to hand back from your mat, then the Hand back plaque.
 *   - Two patrons at once: pick the one who joins you, then Welcome.
 *   - back (or a right click) steps back out of any of these: the last stone picked goes back to its stack, a chosen
 *     card is let go, a hand-back is taken back. A tap on a picked stone puts that one back. Nothing is played
 *     until the plaque, so once a move lands the turn is sealed and back has nothing to undo. */
#ifndef FACET_GAME_H
#define FACET_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cartwheel/cursor.h"
#include "cartwheel/input.h"
#include "cpu.h"
#include "rules.h"

/* targets on the table */
enum {
  TG_BANK = 0,                       /* + colour 0..5 (5 topaz: shown, never a target) */
  TG_CARD = TG_BANK + GM_NTOK,       /* + tier * 4 + slot */
  TG_DECK = TG_CARD + GM_TIERS * GM_SLOTS, /* + tier */
  TG_RES = TG_DECK + GM_TIERS,       /* + set-aside index of the seat to move */
  TG_HAND = TG_RES + GM_MAX_RES,     /* + colour, the seat to move's stones */
  TG_PATRON = TG_HAND + GM_NTOK,     /* + patron id */
  TG_OK = TG_PATRON + GM_NPATRONS,   /* the main plaque: Take, Fulfil, Hand back, Welcome, Pass */
  TG_ALT,                            /* the second plaque: Set aside */
  TG_COUNT
};

typedef enum { UI_BROWSE, UI_CARD, UI_HANDBACK, UI_PATRON } gm_ui_mode;

typedef struct gm_ui {
  gm_ui_mode mode;
  int pick[3], npick;      /* stones picked from the piles */
  int card;                /* UI_CARD: the target chosen (TG_CARD/TG_DECK/TG_RES + i) */
  bk_move staged;          /* UI_HANDBACK: the action (fields 0..3) waiting for its hand-backs */
  int ret[3], nret, excess;
  int patron;              /* UI_PATRON: the patron chosen, -1 none */
  int message;             /* a hint the view shows (GM_MSG_*), 0 none */
} gm_ui;

enum { GM_MSG_NONE = 0, GM_MSG_CANNOT_TAKE, GM_MSG_NOTHING_TO_DO, GM_MSG_PILE_LOW };

/* the actions every seat presses */
typedef struct gm_actions { cw_action left, right, up, down, pick, back; } gm_actions;

typedef struct gm_game {
  bk_game rules;
  bk_match *match;
  int players;
  int cpu_level[BK_MAX_SEATS]; /* -1 a person */
  bk_cpu *cpu[BK_MAX_SEATS];
  gm_ui ui;
  gm_actions act;
  bk_select *sel;                /* the cursors and recorded pointer picks (boardkit) */
  int active[TG_COUNT], nactive; /* the targets the cursor may stand on now, in cursor index order */
  uint64_t active_key;
  int cursor_target;             /* the target the cursor of the seat to move stands on, -1 none */
  int seat_shown;                /* the seat the cursor belongs to */
  cw_view3d view;                /* the camera the cursor hops by (set by the view) */
  bool have_view;
  bool secondary_was;            /* the right button was held at the last tick (a right click steps back) */
  int log_seen;
  uint64_t seed;
} gm_game;

/* where a target sits on the table (world, y up, table top at y = 0) */
t3_vec3 gm_target_pos(const gm_game *g, int target);
/* seat mats: centre of seat s's mat, and its size */
t3_vec3 gm_mat_pos(int players, int seat);
#define GM_MAT_W 4.6f
#define GM_MAT_H 1.8f
/* a mat's depth for this many seats: four mats are a little shallower so their name bars fit between them */
static inline float gm_mat_h(int players) { return players >= 4 ? 1.62f : GM_MAT_H; }
/* market and pile spots */
t3_vec3 gm_bank_pos(int colour);
t3_vec3 gm_card_pos(int tier, int slot); /* slot -1: the deck */
t3_vec3 gm_patron_pos(const gm_state *st, int patron);
t3_vec3 gm_res_pos(int players, int seat, int r);
t3_vec3 gm_hand_pos(int players, int seat, int colour);
#define GM_CARD_W 1.25f
#define GM_CARD_H 1.75f

void gm_game_init(gm_game *g, const gm_actions *act);
/* a new match: players 2..4, cpu_level per seat (-1 a person) */
void gm_game_start(gm_game *g, int players, const int *cpu_level, uint64_t seed);
void gm_game_end(gm_game *g);
/* one tick: CPU seats press (cw_input_set_actions), then the seat to move's actions (and a recorded pointer pick:
 * the view calls bk_select_pointer on g->sel each frame) drive the selection. True when a move landed. */
bool gm_game_tick(gm_game *g);
const gm_state *gm_game_state(const gm_game *g);
bool gm_game_over(const gm_game *g);
int gm_game_turn(const gm_game *g);
bool gm_target_active(const gm_game *g, int target);
/* where the view draws picked stone i (canvas units; a tap there puts it back) */
cw_vec2 gm_pick_icon_at(const gm_game *g, int i);
/* the move the selection would play now if OK were picked (for the ghost preview), BK_MOVE_NONE when none */
bk_move gm_game_preview(const gm_game *g);
/* what the plaques say now ("" when off) */
const char *gm_ok_label(const gm_game *g);
const char *gm_alt_label(const gm_game *g);
/* the hand of the seat to move as the staged hand-back would leave it (for the mat's ghost) */
void gm_game_staged_hand(const gm_game *g, uint8_t hand[GM_NTOK]);
/* the CPU seat entering a move, and the move (BK_MOVE_NONE when none) */
bk_move gm_game_cpu_intent(const gm_game *g);
bool gm_game_cpu_thinking(const gm_game *g);

/* the move as the rules list it, for a move the selection built (takes that end in the same hand are one move) */
bk_move gm_canonical(const gm_state *st, bk_move m);

#endif
