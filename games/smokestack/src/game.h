/* Smokestack match: seats, the match sequencer, CPU seats and the input that turns presses into moves. Every
 * choice is a cursor target: the option plaques, the job cards, or a site on the board (an edge a link starts
 * from, the edge a tile leaves by, a good, a city). Everything here runs in fixed_update and reads actions only,
 * so a recording replays the match. */
#ifndef RL_GAME_H
#define RL_GAME_H

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "boardkit/bk_shell.h"
#include "layout.h"
#include "rules.h"

/* a good hauled: the view runs a train along its route */
typedef struct rl_haul_event { int seat, src, color; rl_route route; } rl_haul_event;
#define RL_HAUL_EVENTS 8

typedef struct rl_game {
  bk_match *match;
  bk_cpu *cpu[RL_MAX_PLAYERS];
  bk_seat seat[RL_MAX_PLAYERS];
  int players;
  bk_select *sel;
  cw_node target[RL_TARGETS];
  bk_move tmove[RL_TARGETS]; /* the move each target stands for now (BK_MOVE_NONE: off) */
  t3_vec3 tpos[RL_TARGETS];
  int targets_for;           /* the log index the targets were laid out for */
  cw_action left, right, up, down, ok, back;
  uint64_t seed;
  bool fast;                 /* CPUs think and press without pauses (the self-playing cart, tests) */
  bool help_open;            /* the how-to-play card (people's presses close it) */
  rl_haul_event haul[RL_HAUL_EVENTS]; /* for the view, oldest first */
  int haul_n;
  int32_t undos, pointer_picks, pad_moves, cpu_moves, decisions, matches, games_done, wins[RL_MAX_PLAYERS], over_ticks;
} rl_game;

void rl_game_init(rl_game *g, cw_node parent);           /* actions, the select cursor and its target nodes */
void rl_game_start(rl_game *g, uint64_t seed, bool fast); /* a match with the shell's seats */
void rl_game_tick(rl_game *g);                           /* one fixed tick of play */
const rl_state *rl_game_state(const rl_game *g);
bool rl_game_is_cpu(const rl_game *g, int seat);
bool rl_game_any_person(const rl_game *g);
/* the target a move stands on now, -1 none */
int rl_game_target_of(const rl_game *g, bk_move m);

#endif
