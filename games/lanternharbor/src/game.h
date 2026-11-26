/* Lantern Harbor: the match. The kit's bk_match plays the rules; this file adds the seats (people on pads, hot
 * seats, CPUs through bk_cpu), the cursor every seat drives with the same actions (the pointer walks it by pressing
 * them, so a recording carries every pick), and the table as shown: a copy of the state that follows the match one
 * decision at a time, with a wait after each so the view can animate it and nobody moves while it does. All of it
 * runs in fixed_update and reads actions only, so a replay of the input recording plays the same match.
 *
 * A decision is one cursor pick, except loading a boat: first the good (a pile on your board), then the boat. */
#ifndef LANTERNHARBOR_GAME_H
#define LANTERNHARBOR_GAME_H

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cartwheel/cursor.h"
#include "cpu.h"
#include "layout.h"
#include "rules.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, A_BACK, A_HELP, A_COUNT };
typedef enum { UI_PICK, UI_PLACE } ui_mode;

typedef enum { EV_MOVE, EV_TURN, EV_OVER, EV_START } ev_kind;
typedef struct mg_event {
  ev_kind kind;
  int seat;
  bk_move move;
  int index;
} mg_event;
#define MG_EVENTS 32

typedef enum { TG_MOVE, TG_GOOD, TG_PUTBACK } tg_kind;
#define MG_MAX_TARGETS 48
typedef struct mg_target {
  tg_kind kind;
  bk_move move; /* TG_MOVE */
  int good;     /* TG_GOOD, TG_PUTBACK */
  ly_point at;
  float y;
} mg_target;

typedef struct mg_game {
  int players;
  int cpu_level[BK_MAX_SEATS]; /* -1 a person */
  uint64_t seed;
  bool fast;                   /* self-playing: short waits */
  bk_game rules;
  bk_match *match;
  hb_state shown;              /* the table as shown (follows the match decision by decision) */
  ly_table lay;
  int wait;                    /* ticks until the next input */
  bool ending;                 /* the match is over; the results show once the last wait ends */
  int turn;
  ui_mode mode;
  int pick_good;
  mg_target target[MG_MAX_TARGETS];
  int ntargets;
  bool targets_dirty;
  bk_select *sel;
  cw_view3d view;
  bk_cpu *cpu[BK_MAX_SEATS];
  bool help;
  bool over;
  int ticks;
  mg_event ev[MG_EVENTS];
  int ev_next;
  cw_action act[A_COUNT];
  int32_t moves_by[BK_MAX_SEATS], cpu_moves, pointer_picks;
  int32_t ui_types[2][16]; /* a person's moves by type (M_PASS..M_JETTY), entered on the pad [0] or by the pointer [1] */
} mg_game;

void mg_actions_init(mg_game *g);
void mg_start(mg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast);
void mg_free(mg_game *g);
void mg_tick(mg_game *g);
void mg_set_view(mg_game *g, cw_view3d view);
void mg_pointer(mg_game *g, cw_tree *t);
const hb_state *mg_state(const mg_game *g);
bool mg_busy(const mg_game *g);
bool mg_seat_is_cpu(const mg_game *g, int seat);
/* the move the cursor would play now (the ghost), BK_MOVE_NONE when it is on a good to pick or nothing */
bk_move mg_preview(const mg_game *g);
int mg_cursor(const mg_game *g);
const mg_target *mg_cursor_target(const mg_game *g);
const char *mg_seat_name(int seat);
/* where a move points on the table (the cursor and the view's ghosts) */
ly_point mg_move_spot(const mg_game *g, const hb_state *s, bk_move m, float *y);

#endif
