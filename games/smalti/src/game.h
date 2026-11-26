/* Smalti: the match. The kit's bk_match plays the rules; this file adds the seats (people on pads, hot seats,
 * CPUs through bk_cpu), the cursor every seat drives with the same actions, the pointer (which walks that cursor
 * by pressing the actions, so a recording carries it), and the table as shown: a copy of the state that follows
 * the match one step at a time (a take, each staging row tiled, each strip settled, the refill) with a wait after
 * each, so the view can animate it and nobody moves while it does. All of it runs in fixed_update and reads
 * actions only, so a replay of the input recording plays the same match. */
#ifndef SMALTI_GAME_H
#define SMALTI_GAME_H

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cartwheel/cursor.h"
#include "cpu.h"
#include "layout.h"
#include "rules.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, A_BACK, A_HELP, A_COUNT };
typedef enum { UI_PICK, UI_PLACE } ui_mode;

/* what just happened on the table, for the view's popups and sounds */
typedef enum { EV_TAKE, EV_TILE, EV_FLOOR, EV_REFILL, EV_OVER, EV_TURN } ev_kind;
typedef struct mg_event {
  ev_kind kind;
  int seat, row, col, points, colour, count, line;
  int index; /* running number */
} mg_event;
#define MG_EVENTS 32

#define MG_MAX_TARGETS 64
typedef struct mg_target {
  int src, colour; /* UI_PICK: a group */
  int line;        /* UI_PLACE: a staging row or MS_FLOOR */
  ly_point at;
} mg_target;

/* the kinds of move a turn can be: from a tray or the centre, onto a staging row or the breakage strip */
enum { MG_TRAY_ROW, MG_TRAY_FLOOR, MG_CENTRE_ROW, MG_CENTRE_FLOOR, MG_KINDS };
static inline int mg_kind(int source, int line) { return (source == MS_CENTRE ? 2 : 0) + (line == MS_FLOOR ? 1 : 0); }

typedef struct mg_game {
  int players;
  int cpu_level[BK_MAX_SEATS]; /* -1 a person */
  uint64_t seed;
  bool fast;                   /* self-playing: short waits */
  bk_game rules;
  bk_match *match;
  ms_state shown;              /* the table as shown (follows the match step by step) */
  ly_table lay;
  /* the round end in steps */
  bool staging;
  int stage_seat, stage_row;   /* next step: rows 0..4, then 5 = the strip */
  int wait;                    /* ticks until the next step or input */
  /* the seat to move */
  int turn;
  ui_mode mode;
  int pick_src, pick_colour;
  mg_target target[MG_MAX_TARGETS];
  int ntargets;
  bool targets_dirty;
  bk_select *sel;              /* the cursor every seat drives, and the pointer as recorded picks */
  cw_view3d view;              /* the camera and canvas the cursor hops on (main sets it) */
  int log_seen;
  bk_cpu *cpu[BK_MAX_SEATS];
  bool help;                   /* the how-to card is up */
  bool over;                   /* the match ended and the results show */
  int ticks;
  mg_event ev[MG_EVENTS];
  int ev_next;
  cw_action act[A_COUNT];
  /* debug figures */
  int32_t moves_by[BK_MAX_SEATS], cpu_moves, pointer_picks;
  int32_t help_opens;          /* times a seat opened the how-to card (the idle-seat gate: a pad with no seat opens none) */
  int32_t person_kinds[MG_KINDS]; /* moves people made, by kind (mg_kind): tests drive every kind through the pad and pointer */
} mg_game;

void mg_actions_init(mg_game *g);
/* a new match: seats' CPU levels (-1 a person), the bag seed */
void mg_start(mg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast);
void mg_free(mg_game *g);
/* one tick: CPUs and the pointer press, then the seat to move's actions are read */
void mg_tick(mg_game *g);
/* the camera and canvas rectangle the cursor reads directions and taps on (before mg_start) */
void mg_set_view(mg_game *g, cw_view3d view);
/* once per rendered frame: a tap or click on a target is queued for the seat to move (a person) */
void mg_pointer(mg_game *g, cw_tree *t);
const ms_state *mg_state(const mg_game *g); /* the match's own state */
bool mg_busy(const mg_game *g);
bool mg_seat_is_cpu(const mg_game *g, int seat);
/* the move the cursor shows now (UI_PLACE: the ghost), BK_MOVE_NONE in UI_PICK */
bk_move mg_preview(const mg_game *g);
/* the cursor's target, -1 none */
int mg_cursor(const mg_game *g);
const char *mg_seat_name(int seat);
/* the press that walks seat's cursor toward target (a hop the pad makes), or confirm when it stands there */
bool mg_press_to_target(mg_game *g, int seat, int target, bk_press *out);

#endif
