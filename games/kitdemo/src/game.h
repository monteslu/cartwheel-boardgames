/* kitdemo match: seats, the match sequencer, CPU seats and the input that turns presses into moves. Everything
 * here runs in fixed_update and reads actions only, so a recording replays the match. */
#ifndef KD_GAME_H
#define KD_GAME_H

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "boardkit/bk_select.h"
#include "boardkit/bk_shell.h"
#include "rules.h"

/* the select targets: the three market slots, then bank and roll */
enum { KD_T_SLOT0 = 0, KD_T_BANK = 3, KD_T_ROLL = 4, KD_TARGETS = 5 };

typedef struct kd_game {
  bk_match *match;
  bk_cpu *cpu[2];
  bk_seat seat[2];
  bk_select *sel;
  cw_action left, right, up, down, ok, back;
  uint64_t seed;
  int32_t undos, pointer_picks, pad_moves, cpu_moves, matches, wins[3], over_ticks;
} kd_game;

void kd_game_init(kd_game *g);               /* actions and the select cursor (targets set by the view) */
void kd_game_start(kd_game *g, uint64_t seed); /* a match with the shell's seats */
void kd_game_tick(kd_game *g);               /* one fixed tick of play */
const kd_state *kd_game_state(const kd_game *g);
bool kd_game_is_cpu(const kd_game *g, int seat);

#endif
