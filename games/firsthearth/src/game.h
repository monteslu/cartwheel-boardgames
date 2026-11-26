/* First Hearth: the match on the table. Holds the kit's match (bk_match), the CPU seats (bk_cpu), the cursor
 * (bk_select) and what the table shows: the rules run ahead and the table follows one logged step at a time
 * (a placement, a roll, a purchase), each held for a beat so people can follow it. Every seat is read through
 * actions alone, so a recording replays the whole match, the CPUs' and the pointer's choices included. */
#ifndef FIRSTHEARTH_GAME_H
#define FIRSTHEARTH_GAME_H

#include <stdbool.h>

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "cartwheel/input.h"
#include "rules.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, A_BACK, A_HELP, A_COUNT };
enum { UI_SITES, UI_COUNT, UI_MENU };
enum { TG_T_SITE, TG_T_COUNT, TG_T_OPTION };
#define TG_MAX_TARGETS 32

typedef struct tg_target {
  int kind;     /* TG_T_* */
  int site;     /* the site (a site or a count chip) */
  int value;    /* count, or the option's index */
  bk_move move; /* the move it plays (a count or an option), else BK_MOVE_NONE */
  t3_vec3 at;   /* its point on the table */
} tg_target;

/* one step the table shows: the state before and after a logged decision or chance outcome */
typedef struct tg_beat {
  tr_state before, after;
  bk_move move;
  int seat;      /* who decided (BK_CHANCE for a chance outcome) */
  int serial;    /* counts up */
} tg_beat;

typedef struct tg_game {
  bk_game rules;   /* the match's rules: chance through outcomes, so the log carries every roll and draw */
  bk_game search;  /* the CPUs' rules: chance() too, faster in playouts */
  bk_match *match;
  bk_cpu *cpu[TR_MAX_P];
  int cpu_level[TR_MAX_P]; /* -1 a person */
  int players;
  uint64_t seed;
  bool fast;       /* a self-playing cart: short beats */
  bk_select *sel;
  cw_action act[A_COUNT];
  /* the choice in progress */
  int mode, turn, pick_site;
  tg_target target[TG_MAX_TARGETS];
  int ntargets;
  bool dirty;
  bk_move hint; /* the one-step suggestion the cursor starts on (a person's seat), else BK_MOVE_NONE */
  /* the table follows the log */
  tr_state shown;
  int log_seen, wait;
  tg_beat beat;
  bool help, over;
  int ticks, cpu_moves, decisions, over_ticks;
  uint64_t final_hash;
} tg_game;

void tg_actions_init(tg_game *g);
/* a new match; cpu_level[i] -1 for a person */
void tg_start(tg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast);
void tg_free(tg_game *g);
/* every fixed tick, before anything reads input */
void tg_tick(tg_game *g);
bool tg_busy(const tg_game *g);           /* the table is still showing earlier steps */
bool tg_seat_cpu(const tg_game *g, int seat);
const tr_state *tg_state(const tg_game *g); /* the match's state (ahead of the table) */
/* the target under the seat's cursor (-1 none) and the move a confirm there would play (BK_MOVE_NONE) */
int tg_cursor(const tg_game *g);
bk_move tg_preview(const tg_game *g);
/* the options a menu step offers (tools, buy, feed): their moves, in order */
int tg_menu(const tg_game *g, bk_move *out, int cap);
/* a default count for a site (enough hunters to feed the village, else a few) */
int tg_default_count(const tr_state *s, int seat, int site);
const char *tg_seat_name(int seat);

#endif
