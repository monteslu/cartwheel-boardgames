/* Fever Line: the match on the table. Holds the setup choice, the kit's match (bk_match), the CPU agents (bk_cpu),
 * the cursor (bk_select) and what the table shows: the rules run ahead and the table follows one logged step at a
 * time (a move, a card drawn, a fever card flipped), each held for a beat so people can follow it. Every agent is
 * read through actions alone, so a recording replays the whole match, the CPUs' and the pointer's choices
 * included.
 *
 * Seats and agents: the lobby's seats each play one agent (two to four). One person alone plays two to four agents
 * (chosen on the setup card), every one from the same pad. A CPU agent presses its own seat's actions. */
#ifndef FEVERLINE_GAME_H
#define FEVERLINE_GAME_H

#include <stdbool.h>

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cartwheel/input.h"
#include "rules.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, A_BACK, A_HELP, A_COUNT };
enum { UI_SETUP, UI_MAIN, UI_SHARE, UI_DISCARD };
enum { TG_CITY, TG_PLATE, TG_SETUP };
/* plate kinds (what a plate does beyond its move) */
enum { PL_MOVE, PL_SHARE_OPEN, PL_BACK, PL_SETUP_LEVEL, PL_SETUP_AGENTS, PL_SETUP_GO, PL_SETUP_MAP };
#define TG_MAX_TARGETS 96

typedef struct tg_target {
  int kind;     /* TG_* */
  int city;     /* a city target's city */
  int plate;    /* PL_* */
  int value;    /* a setup plate's value */
  bk_move move; /* the move it plays, BK_MOVE_NONE */
  t3_vec3 at;   /* its point on the table */
} tg_target;

/* one step the table shows: the state before and after a logged decision or chance outcome */
typedef struct tg_beat {
  fv_state before, after;
  bk_move move;
  int seat;   /* the agent who decided (BK_CHANCE for a chance outcome) */
  int serial; /* counts up */
} tg_beat;

typedef struct tg_game {
  bk_game rules;  /* the match's rules: chance through outcomes, so the log carries every draw and flip */
  bk_game search; /* the team's planner (fv_search_game) */
  bk_match *match;
  bk_cpu *cpu[FV_MAX_R];
  int cpu_level[FV_MAX_R]; /* -1 a person */
  int ctl[FV_MAX_R];       /* the input seat that plays each agent */
  int roles, setup;
  int seats;               /* lobby seats */
  int seat_level[FV_MAX_R]; /* each lobby seat: -1 a person, else a CPU level */
  uint64_t seed;
  bool fast;              /* a self-playing cart: short beats */
  bk_select *sel;
  cw_action act[A_COUNT];
  /* the choice in progress */
  int mode, turn;
  tg_target target[TG_MAX_TARGETS];
  int ntargets;
  bool dirty;
  bk_move hint; /* the planner's suggestion the cursor starts on (a person's agent), else BK_MOVE_NONE */
  int setup_level, setup_agents, setup_map; /* the setup card's choices */
  int map; /* the match's map (FV_MAPS) */
  /* the table follows the log */
  fv_state shown;
  int log_seen, wait;
  tg_beat beat;
  bool help, over, started;
  int ticks, cpu_moves, decisions, over_ticks;
  uint64_t final_hash;
} tg_game;

void tg_actions_init(tg_game *g);
/* After the lobby: seats (1..4), each -1 a person or a CPU level. With a person among them the setup card shows
 * first (unless skip_setup: a test switch chose); else the match starts at once with `setup` and (one seat alone)
 * `agents` agents. */
void tg_begin(tg_game *g, int seats, const int *seat_level, int setup, int agents, int map, uint64_t seed, bool fast, bool skip_setup);
void tg_free(tg_game *g);
/* every fixed tick, before anything reads input */
void tg_tick(tg_game *g);
bool tg_busy(const tg_game *g);            /* the table is still showing earlier steps */
bool tg_role_cpu(const tg_game *g, int r); /* a CPU plays this agent */
const fv_state *tg_state(const tg_game *g); /* the match's state (ahead of the table) */
int tg_cursor(const tg_game *g);           /* the target under the cursor of the seat to move (-1) */
bk_move tg_preview(const tg_game *g);      /* the move a confirm would play now (BK_MOVE_NONE) */
int tg_input_seat(const tg_game *g);       /* the seat whose pad drives the choice now */
/* the plate's words */
void tg_plate_label(const tg_game *g, const tg_target *t, char *buf, size_t cap);
const char *tg_setup_name(int setup);

#endif
