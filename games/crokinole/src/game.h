/* crokinole's match: turns, aiming from the seat's actions, CPU seats, the shot on screen and its replay.
 *
 * The rules state lives in a bk_match (the kit's sequencer). A shot lands in it at once (bk_match_play runs it to
 * rest); the view plays the same shot tick by tick from the state before it (anim), and checks it ends with the
 * very state the match holds. A replay is that playback again, at half speed.
 *
 * Aiming reads the actions of the seat to move only (cw_action_*), so a pad, a hot seat's shared pad, the keyboard and
 * a CPU (bk_cpu pressing the same actions through cw_input_set_actions) all aim the same way:
 *   slide_left / slide_right   move the disc along the shooting line (a tap is one step, a hold speeds up)
 *   aim_left / aim_right       turn the shot (the same)
 *   spin_up / spin_down        spin, when the Spin setting is on
 *   flick                      hold: the power meter rises one step a tick; let go: the shot
 *   cancel                     drop the charge
 *   replay                     watch the last shot again
 * A pointer (mouse or finger) drags: on the disc, pull back and let go to flick; elsewhere on the seat's arc, slide
 * the disc there. The pointer is the engine's recorded channel (cw_input_pointer in fixed_update), so a recording, its
 * replay and the input hash carry pointer shots exactly as pad presses. */
#ifndef CRK_GAME_H
#define CRK_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "cartwheel/input.h"
#include "rules.h"

enum { GP_AIM, GP_FLIGHT, GP_RESULT, GP_ROUND, GP_OVER };
enum { SEAT_PAD, SEAT_HOT, SEAT_CPU };
enum { CT_SLIDE_L, CT_SLIDE_R, CT_AIM_L, CT_AIM_R, CT_SPIN_U, CT_SPIN_D, CT_FLICK, CT_CANCEL, CT_REPLAY, CT_COUNT };

#define IDLE_TICKS (45 * 60) /* a person's turn with no input this long: the computer plays it for them */
#define PULL_MAX 0.16f       /* a pointer pull this long (metres on the board) is full power */

typedef struct game_events {
  int n;
  crk_event e[CRK_MAXEV];
  bool flick, landed, round_end, game_over, foul, twenty;
  float flick_power;
} game_events;

typedef struct game {
  int players, target;
  bool spin_on;
  int seat_kind[4], seat_level[4];
  crk_rules_user ru_match, ru_cpu[4];
  bk_game rules, cpu_rules[4];
  bk_match *m;
  bk_cpu *cpu[4], *standby; /* standby: plays an idle person's turn */
  cw_action act[CT_COUNT];
  /* the phase */
  int phase, phase_t;
  /* aiming: the seat to move */
  int aim_seat;
  crk_shot aim, seat_aim[4]; /* the seat's settings (kept per seat between its turns) */
  int meter;
  bool charging;
  int held[CT_COUNT];
  float acc[CT_COUNT];
  int idle_t, idle_limit; /* idle_limit: ticks (IDLE_TICKS unless a test sets it; 0 never) */
  bool standby_on, standby_pressing;
  /* the pointer */
  bool drag, slide_drag, ptr_dragging; /* drag / slide_drag: what the pointer holds; ptr_dragging: a pull shown this tick */
  float drag_x, drag_y; /* where the pointer is on the board (crk frame) */
  /* this tick's pointer intent (pointer_plan; aim_tick applies it); tap: pressed this tick (a card's {flick}) */
  struct { bool slide, drag, shot, tap; int pos, aim, power; } pt;
  /* the shot on screen */
  crk_state anim, before;
  uint64_t last_move;
  bool have_last, replaying;
  int last_seat;
  int rounds_seen;
  crk_round_result round;
  /* counters (debug fields) */
  int seat_shots[4], seat_fouls[4];
  int shots, fouls, twenties, contacts, replays, pointer_shots, pad_shots, cpu_shots, standby_turns, checks, cancels, slides, spins;
  game_events ev;
} game;

/* a match: seat kinds and CPU levels per seat, the target score; spin on or off */
void game_new(game *g, int players, const int kind[4], const int level[4], int target, bool spin_on, uint64_t seed);
void game_free(game *g);
const crk_state *game_state(const game *g); /* the rules' state (after the last shot) */
/* what the board shows: the shot in flight (or its replay), else the rules' state */
const crk_state *game_view_state(const game *g);
/* this tick's input made by the game: CPU seats press their actions, and the pointer (this tick's cw_input_pointer:
 * down, pressed, where on the board; valid false: off it) becomes the person's pointer intent. Call in fixed_update
 * before game_tick. */
void game_input(game *g, bool ptr_down, bool ptr_pressed, float px, float py, bool ptr_valid);
/* one tick: reads every seat's actions */
void game_tick(game *g);
/* the seat to move (the aim seat), the shot being aimed, and whether the board is open (no opposing disc) */
int game_turn(const game *g);
bool game_over(const game *g);
/* the aimed disc's place and direction (crk frame) */
void game_aim_disc(const game *g, float *x, float *y, float *dx, float *dy);
/* the steps a held control moves over its first k ticks (shared by people and the CPU) */
int game_hold_rate(int k, int cap);
bool game_seat_is_cpu(const game *g, int seat);
bool game_cpu_thinking(const game *g);

#endif
