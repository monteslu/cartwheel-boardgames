/* Rose Window: the match. The kit's bk_match plays the rules; this file adds the seats (people on pads, hot seats,
 * CPUs through bk_cpu), the one cursor every seat drives with the same actions, and the pointer (recorded picks).
 *
 * A decision is entered as a short path of choices on the table: a die then a pane (set it), the bell (pass), or a
 * tool card then what the tool asks for (a die, a die and a pane, one of your dice and where it goes, lower or
 * raise). Each step offers only the choices some legal move continues with, so every path ends in a legal move.
 * Back steps out of a choice (and with nothing chosen takes back a tool used this turn). The CPU walks the same
 * path by pressing the same actions. A tool used this turn can be taken back: Back with nothing chosen, or the "take
 * back" choice on its card (so a pointer can too). Everything runs in fixed_update and reads actions only, so a replay of the
 * input recording plays the same match. */
#ifndef ROSEWINDOW_GAME_H
#define ROSEWINDOW_GAME_H

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cpu.h"
#include "layout.h"
#include "rules.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, A_BACK, A_HELP, A_CARDS, A_COUNT };

/* what a choice on the table is */
enum { TG_DIE = 1, TG_PANE, TG_TOOL, TG_PASS, TG_DIR, TG_FRAME, TG_BACK };
static inline int tg_id(int kind, int index) { return kind << 8 | index; }
static inline int tg_kind(int id) { return id >> 8; }
static inline int tg_index(int id) { return id & 0xff; }

#define MG_MAX_TARGETS 64
#define MG_STEPS 3

typedef struct mg_game {
  int players;
  int cpu_level[BK_MAX_SEATS]; /* -1 a person */
  uint64_t seed;
  bool fast;
  bk_game rules;
  bk_match *match;
  ly_table lay;
  int turn;                    /* the seat to move */
  int path[MG_STEPS], plen;    /* the choices made toward this decision */
  int target[MG_MAX_TARGETS];  /* the choices offered now (tg ids) */
  t3_vec3 target_at[MG_MAX_TARGETS];
  int ntargets;
  bool targets_dirty;
  bk_select *sel;
  cw_view3d view;
  int log_seen;
  int wait;                    /* ticks before anyone may choose (a throw or a die landing plays out) */
  int undo_slot;               /* the tool card used this turn (where "take back" sits on the table) */
  bk_cpu *cpu[BK_MAX_SEATS];
  bool help;
  bool cards;                  /* the cards panel is up: every card's full words, large */
  bool over;
  int ticks;
  cw_action act[A_COUNT];
  char caption[96];            /* what just happened, in words */
  int caption_age;
  int32_t moves_by[BK_MAX_SEATS], cpu_moves, pointer_picks, undos, tools_used;
  int32_t person_kinds[4];     /* moves people made, by kind (RW_MV_PLACE, _PASS, _TOOL, _FRAME) */
  int32_t person_tools;        /* tool kinds people used (bit per rw_tool_kind) */
} mg_game;

void mg_actions_init(mg_game *g);
/* a new match: players 1..4, each seat's CPU level (-1 a person), the game seed */
void mg_start(mg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast);
void mg_free(mg_game *g);
void mg_tick(mg_game *g);
void mg_set_view(mg_game *g, cw_view3d view);
/* once per rendered frame: a tap on a choice is queued for the seat to move (a person) */
void mg_pointer(mg_game *g, cw_tree *t);
const rw_state *mg_state(const mg_game *g);
bool mg_busy(const mg_game *g);
bool mg_seat_is_cpu(const mg_game *g, int seat);
/* the choice under the cursor (tg id), -1 none */
int mg_cursor_id(const mg_game *g);
/* the move the current path and cursor would make if confirmed (the ghost), BK_MOVE_NONE when not one */
bk_move mg_preview(const mg_game *g);
const char *mg_seat_name(int seat);
/* the steps of a move as choices (count) */
int mg_move_steps(const rw_state *s, bk_move m, int out[MG_STEPS]);
/* where a choice stands on the table */
t3_vec3 mg_target_point(const mg_game *g, int id);

#endif
