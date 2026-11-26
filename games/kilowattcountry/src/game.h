/* Kilowatt Country: the match on the table. Holds the kit's match (bk_match), the CPU seats (bk_cpu), the cursor
 * (bk_select) and what the table shows: the rules run ahead and the table follows one logged step at a time (a bid,
 * a draw, a purchase, a house), each held for a beat so people can follow it. Every seat is read through actions
 * alone, so a recording replays the whole match, the CPUs' and the pointer's choices included. */
#ifndef KILOWATTCOUNTRY_GAME_H
#define KILOWATTCOUNTRY_GAME_H

#include <stdbool.h>

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cartwheel/input.h"
#include "rules.h"
#include "three.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, A_BACK, A_HELP, A_COUNT };
enum { UI_MAIN, UI_COUNT };
/* what a target is */
enum { KG_T_PLANT, KG_T_OWN, KG_T_FUEL, KG_T_COUNT, KG_T_CITY, KG_T_OPTION };
#define KG_MAX_TARGETS 48

typedef struct kg_target {
  int kind;
  int value;    /* market slot, own plant index, fuel, count, city, or an option's index */
  bk_move move; /* the move a confirm plays; BK_MOVE_NONE for a fuel row (it opens the count) */
  t3_vec3 at;   /* its point on the table */
} kg_target;

/* one step the table shows: the state before and after a logged decision or chance outcome */
typedef struct kg_beat {
  kc_state before, after;
  bk_move move;
  int seat;   /* who decided (BK_CHANCE for a draw) */
  int serial; /* counts up */
} kg_beat;

typedef struct kg_game {
  bk_game rules;  /* the match's rules: chance through outcomes, so the log names every draw */
  bk_game search; /* the CPUs' rules */
  bk_match *match;
  bk_cpu *cpu[KC_MAX_P];
  int cpu_level[KC_MAX_P]; /* -1 a person */
  int players;
  uint64_t seed;
  bool fast; /* a self-playing cart: short beats */
  bk_select *sel;
  cw_action act[A_COUNT];
  /* the choice in progress */
  int mode, turn, count_fuel;
  kg_target target[KG_MAX_TARGETS];
  int ntargets;
  bool dirty;
  bk_move hint; /* the suggestion the cursor starts on (a person's seat), else BK_MOVE_NONE */
  /* the table follows the log */
  kc_state shown;
  int log_seen, wait;
  kg_beat beat;
  bool help, over;
  int ticks, cpu_moves, decisions, over_ticks, pointer_picks;
  /* the moves people chose, by kind (KC_M_*), through the pad and through the pointer; the last one's kind */
  int pad_kinds[KC_M_DRAW], pointer_kinds[KC_M_DRAW], last_kind;
  int hide_kind; /* a test control: a move kind left off the table (0 none; KILOWATTCOUNTRY_TEST_HIDE) */
  uint64_t final_hash;
} kg_game;

void kg_actions_init(kg_game *g);
/* a new match; cpu_level[i] -1 for a person */
void kg_start(kg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast);
void kg_free(kg_game *g);
/* every fixed tick, before anything reads input */
void kg_tick(kg_game *g);
bool kg_busy(const kg_game *g);
bool kg_seat_cpu(const kg_game *g, int seat);
/* the target under the seat's cursor (-1 none) and the move a confirm there would play (BK_MOVE_NONE) */
int kg_cursor(const kg_game *g);
bk_move kg_preview(const kg_game *g);
const char *kg_seat_name(int seat);
/* the option plates' row (bids, pass, done) */
t3_vec3 kg_option_at(int i, int n);

#endif
