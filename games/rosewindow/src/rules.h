/* Rose Window (codename rosewindow): the rules, pure and headless.
 *
 * Each player builds a stained-glass window of 4 rows by 5 panes from coloured glass dice (five colours, faces
 * 1 to 6). Every round a pool of dice comes out of the bag and is rolled (two per player plus one; three in a solo
 * game). Players take turns in snake order (first to last, then last to first), so everyone drafts twice a round.
 * On a turn a player may use one tool (paid with favour), then take one die from the pool and set it in their
 * window, or pass. Placing:
 *   - the first die goes on an edge or corner pane; every later die touches a die already set (sides or corners);
 *   - a pane marked with a colour takes only that colour, a pane marked with a number only that number;
 *   - two dice side by side never share a colour or a number.
 * After ten rounds windows score: the shared goals on the table, each player's own colour goal (the face values of
 * their colour's dice; every player's colour is open), one point per favour left, and minus one per empty pane. A
 * solo player scores the same with two off per empty pane and must beat the round track (the sum of the dice left
 * over each round).
 *
 * The state is flat and fixed size. The only randomness is a die coming out of the bag (its colour, weighted by
 * what is left, and its face) and a die rerolled by a tool: chance nodes (turn() == BK_CHANCE) with outcomes(), one
 * die at a time. Frames offered, tools and goals are dealt from the game seed at setup, face up.
 *
 * Moves (bk_move_pack fields): kind, then arguments.
 *   RW_MV_PLACE  pool slot, pane
 *   RW_MV_PASS
 *   RW_MV_TOOL   tool slot (0..2 on the table), a, b (per tool, see rw_tool_kind)
 *   RW_MV_FRAME  which of the two offered frames (setup)
 *   RW_MV_DIE    (chance) pool slot, colour, face */
#ifndef ROSEWINDOW_RULES_H
#define ROSEWINDOW_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "boardkit/bk_game.h"

#define RW_COLOURS 5
#define RW_ROWS 4
#define RW_COLS 5
#define RW_CELLS (RW_ROWS * RW_COLS)
#define RW_ROUNDS 10
#define RW_PER_COLOUR 18
#define RW_POOL_MAX 9
#define RW_TOOLS_IN_PLAY 3
#define RW_TOOL_KINDS 10
#define RW_GOAL_KINDS 10
#define RW_FRAMES 24
#define RW_TRACK_MAX 64

/* a die in one byte: 0 none, else colour << 3 | face (face 1..6) */
#define RW_DIE(c, v) ((uint8_t)(((c) << 3) | (v)))
static inline int rw_die_colour(uint8_t d) { return d >> 3; }
static inline int rw_die_value(uint8_t d) { return d & 7; }

enum { RW_PHASE_SETUP = 0, RW_PHASE_DRAFT = 1, RW_PHASE_OVER = 2 };
enum { RW_MV_PLACE = 0, RW_MV_PASS = 1, RW_MV_TOOL = 2, RW_MV_FRAME = 3, RW_MV_DIE = 9 };

/* the tools (our own): what a/b mean in a tool move */
typedef enum {
  RW_T_NUDGE = 0,     /* a slot, b 0 lower / 1 raise: one pool die's face by one (never past 1 or 6) */
  RW_T_TURN = 1,      /* a slot: one pool die turned to its opposite face (the faces add to seven) */
  RW_T_REFIRE = 2,    /* a slot: one pool die rolled again */
  RW_T_KILN = 3,      /* every pool die rolled again */
  RW_T_LONE = 4,      /* a slot, b pane: take and set a die on a pane touching no other die (ends the placing) */
  RW_T_TINT = 5,      /* a pane, b pane: move one of your dice, ignoring colour marks */
  RW_T_ETCH = 6,      /* a pane, b pane: move one of your dice, ignoring number marks */
  RW_T_TWIN = 7,      /* on your first turn of a round: set two dice this turn, then sit out your second turn */
  RW_T_REDRAW = 8,    /* a slot: a pool die goes back in the bag and a new one comes out */
  RW_T_GLAZE = 9,     /* a slot, b pane: take and set a die ignoring the pane's mark (ends the placing) */
} rw_tool_kind;

typedef enum {
  RW_G_ROW_COLOURS = 0, /* each full row of five colours: 6 */
  RW_G_COL_COLOURS,     /* each full column of four colours: 5 */
  RW_G_ROW_VALUES,      /* each full row of five different numbers: 5 */
  RW_G_COL_VALUES,      /* each full column of four different numbers: 4 */
  RW_G_LOW,             /* each pair of a 1 and a 2: 2 */
  RW_G_MID,             /* each pair of a 3 and a 4: 2 */
  RW_G_HIGH,            /* each pair of a 5 and a 6: 2 */
  RW_G_SCALE,           /* each set of 1 to 6: 5 */
  RW_G_PALETTE,         /* each set of all five colours: 4 */
  RW_G_CHAINS,          /* each die touching a die of its colour at a corner: 1 */
} rw_goal_kind;

typedef struct rw_frame {
  const char *name;
  uint8_t rating; /* 3..6: how hard it is, and the favour it starts with */
  uint8_t cell[RW_CELLS]; /* 0 open, 1..5 colour (code - 1), 11..16 number (code - 10) */
} rw_frame;
extern const rw_frame rw_frames[RW_FRAMES];

typedef struct rw_tool_info { const char *name, *text; } rw_tool_info;
typedef struct rw_goal_info { const char *name, *text; int points; } rw_goal_info;
extern const rw_tool_info rw_tool_infos[RW_TOOL_KINDS];
extern const rw_goal_info rw_goal_infos[RW_GOAL_KINDS];
extern const char *const rw_colour_name[RW_COLOURS];

typedef struct rw_player {
  uint8_t cell[RW_CELLS]; /* dice set, 0 empty */
  uint8_t frame;          /* the frame chosen (0xff while choosing) */
  uint8_t offer[2];       /* the two frames offered at setup */
  uint8_t favour;
  uint8_t colour;         /* the player's own colour goal (open) */
  uint8_t placed;         /* dice set */
  uint8_t skip;           /* sits out its second turn this round (the twin tool) */
  uint8_t pad0;
  int16_t score;          /* the final score (filled when the game ends) */
} rw_player;

typedef struct rw_state {
  uint8_t players;        /* 1..4 */
  uint8_t phase;
  uint8_t round;          /* rounds started (1..10 in play) */
  uint8_t turn_idx;       /* 0 .. 2 * players - 1: the snake */
  uint8_t first;          /* who starts this round */
  uint8_t setup_seat;     /* who chooses a frame next */
  uint8_t pool_n;         /* pool slots this round (dice drafted leave a 0) */
  uint16_t draw_mask;     /* pool slots waiting for a die out of the bag (chance) */
  uint16_t reroll_mask;   /* pool slots waiting for a roll (chance) */
  uint8_t tool_now;       /* a tool was used this turn */
  uint8_t places_left;    /* dice the player to move may still set this turn */
  uint8_t ntools, ngoals;
  uint8_t pool[RW_POOL_MAX];
  uint8_t bag[RW_COLOURS];
  uint8_t tools[RW_TOOLS_IN_PLAY];
  uint8_t tool_used[RW_TOOLS_IN_PLAY]; /* times each was used: the first use costs 1 favour, later ones 2 */
  uint8_t goals[3];
  uint8_t track_n;
  uint8_t track[RW_TRACK_MAX]; /* the dice left over each round, in order */
  uint8_t track_round[RW_TRACK_MAX];
  uint16_t track_sum;
  uint16_t moves_played;
  rw_player p[BK_MAX_SEATS];
} rw_state;

/* a frame's cell mark */
static inline int rw_mark_colour(int code) { return code >= 1 && code <= 5 ? code - 1 : -1; }
static inline int rw_mark_value(int code) { return code >= 11 && code <= 16 ? code - 10 : 0; }

static inline bk_move rw_move(int kind, int a, int b, int c) { return bk_move_pack(kind, a, b, c, 0, 0, 0, 0); }
static inline int rw_mv_kind(bk_move m) { return bk_move_field(m, 0); }
static inline int rw_mv_a(bk_move m) { return bk_move_field(m, 1); }
static inline int rw_mv_b(bk_move m) { return bk_move_field(m, 2); }
static inline int rw_mv_c(bk_move m) { return bk_move_field(m, 3); }

/* a fresh game, players 1..4: frames offered, tools, goals and colours dealt from seed; phase SETUP */
void rw_init(rw_state *s, int players, uint64_t seed);
void rw_game(bk_game *g, int players);

int rw_moves(const rw_state *s, bk_move *out, int cap);
void rw_apply(rw_state *s, bk_move m);
int rw_turn(const rw_state *s);
bool rw_terminal(const rw_state *s);
int rw_outcomes(const rw_state *s, bk_move *out, uint32_t *weight, int cap);
void rw_evaluate(const rw_state *s, float score[BK_MAX_SEATS]);
uint64_t rw_hash(const rw_state *s);

/* the seat to move in the draft (turn_idx in the snake) */
int rw_seat_of(const rw_state *s, int turn_idx);
/* can this die go on this pane of seat's window; ignore: bit 0 colour marks, bit 1 number marks; lone: it must
 * touch no other die */
bool rw_can_place(const rw_state *s, int seat, int pane, uint8_t die, int ignore, bool lone);
/* the same on any frame and window cells (placed: dice already set there) */
bool rw_can_place_on(const uint8_t *cell, int placed, const rw_frame *fr, int pane, uint8_t die, int ignore, bool lone);
/* the cost in favour of tool slot t now */
int rw_tool_cost(const rw_state *s, int t);
bool rw_tool_ready(const rw_state *s, int t); /* affordable and not used this turn */

typedef struct rw_breakdown {
  int goal[3];   /* each shared goal's points */
  int colour;    /* the own colour goal */
  int favour;
  int empty;     /* the empty-pane penalty (<= 0) */
  int total;
} rw_breakdown;
void rw_score(const rw_state *s, int seat, rw_breakdown *out);
int rw_goal_points(int goal, const uint8_t cell[RW_CELLS]);
/* winners: bit i per winning seat; solo: bit 0 when the score beats the track */
unsigned rw_winners(const rw_state *s);
/* panes of seat's window that no die can ever fill now (marks and the dice beside them) */
int rw_dead_panes(const rw_state *s, int seat);
/* every die accounted for (bag, pool, windows, track): 18 a colour */
void rw_census(const rw_state *s, int out[RW_COLOURS]);
void rw_move_label(const rw_state *s, bk_move m, char *buf, int cap);

#endif
