/* Mandible: the match. Seats, turns, what the cursor can point at, the CPU seats, the result.
 *
 * Every seat is read through actions only (cartwheel/input.h): a person's pad, keyboard or pointer, or a CPU that
 * presses the same actions with cw_input_set_actions. So the recording, a replay and the input hash carry every
 * move, the CPUs' included.
 *
 * A turn is two picks: SELECT a piece (one of yours on the table that can move, or a kind still in your hand),
 * then TARGET where it goes (the legal hexes light up, a ghost shows the piece there). Confirm takes the pick,
 * back returns to SELECT. The d-pad hops to the nearest pick in that direction on the table; the shoulders step
 * through the picks in order. A pointer click on a pick takes it straight away (action `point`, whose strength
 * carries the pick's index so it is recorded like any press). */
#ifndef MANDIBLE_MATCH_H
#define MANDIBLE_MATCH_H

#include "rules.h"

#include "cartwheel/input.h"

enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_CONFIRM, A_BACK, A_NEXT, A_PREV, A_HELP, A_POINT, A_COUNT };
enum { PH_SELECT = 1, PH_TARGET, PH_OVER };
#define SRC_HAND 100 /* a source 100 + kind is a piece of that kind from the hand */
#define SRC_PASS 200 /* the pass move */
#define MAX_PICKS 160

/* one thing the cursor can point at */
typedef struct sw_pick {
  int32_t src;   /* SELECT: piece id, SRC_HAND + kind or SRC_PASS; TARGET: the source */
  cw_axial hex;  /* TARGET: the destination; SELECT: where the piece stands (hand: unused) */
  float x, z;    /* where it is on the table (world metres), for the d-pad */
  sw_move move;  /* TARGET: the move it makes */
} sw_pick;

/* everything the rules and the seats change: flat, hashed for the replay and determinism checks */
typedef struct sw_match {
  sw_state st;
  int32_t seats;       /* people and CPUs in the match (1 or 2; a lone player gets a CPU opponent) */
  int32_t cpu[2];      /* -1 a person, else the CPU's level (0 Easy, 1 Normal, 2 Hard) */
  int32_t phase;
  int32_t help;        /* the how-to-play card is open: a person's presses close it; a CPU keeps playing */
  int32_t cursor;      /* index into the current picks */
  int32_t src;         /* TARGET: the picked source */
  int32_t over_ticks;
  int32_t wins[3];     /* white, black, draws over the session */
  int32_t games;
  int32_t cpu_moves, human_moves, stray;
  int32_t has_last;
  sw_move last_move;
  int32_t last_mover;
  uint64_t seed;
  int32_t auto_rematch;
} sw_match;

typedef struct sw_match_events {
  /* a move was played this tick: the state before it (the view animates from it) */
  bool moved;
  sw_move move;
  sw_state before;
  bool game_over;
  bool started;
} sw_match_events;

void match_bind_actions(cw_action act[A_COUNT]);
/* the CPU of a side is thinking (for a caption) */
bool match_cpu_thinking(int side);
/* a new match: seats 1 or 2, cpu[side] as in sw_match */
void match_start(sw_match *m, int seats, const int32_t cpu[2], uint64_t seed, bool autoplay);
/* a new game in the same match (scores kept) */
void match_new_game(sw_match *m);
/* one tick: CPUs press first, then every seat is read the same way */
void match_tick(sw_match *m, const cw_action act[A_COUNT], sw_match_events *ev);
/* the picks for the current phase, in a fixed order; count */
int match_picks(const sw_match *m, sw_pick *out, int cap);
/* where a hex or a hand piece sits on the table (world metres): the layout the view uses too */
void match_hex_world(cw_axial h, float *x, float *z);
void match_hand_world(const sw_state *s, int side, int kind, float *x, float *z);
/* the side whose turn it is is played by a CPU */
bool match_cpu_turn(const sw_match *m);
/* the seat (player index) that controls this side */
int match_seat_of_side(const sw_match *m, int side);
/* the pointer: encode a pick index into the `point` action's strength and back */
float match_point_strength(int pick);
int match_point_pick(float strength);
uint32_t match_hash(const sw_match *m);
/* the kinds of move a turn can be, for tests that drive each through the real input */
enum { MT_PLACE, MT_STEP, MT_CLIMB, MT_PASS, MT_COUNT };
/* what kind of move mv is in the state before it */
int match_move_type(const sw_state *before, sw_move mv);
/* a test position (the debug command `setup`): the game goes on from s, the side to move choosing; false for an
 * unknown name. "moves": Ivory may place, step or climb; "pass": Ivory has no legal move but the pass */
bool match_setup(sw_match *m, const char *name);

#endif
