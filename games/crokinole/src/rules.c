/* crokinole's rules as a bk_game (rules.h) */
#include "rules.h"

static int r_turn(const void *s, void *u) { (void)u; return ((const crk_state *)s)->to_move; }
static bool r_terminal(const void *s, void *u) { (void)u; return ((const crk_state *)s)->phase == CRK_OVER; }

static int r_moves(const void *s, bk_move *out, int cap, void *u) {
  const crk_rules_user *ru = u;
  crk_cpu_level L = crk_cpu_level_of(ru && ru->level >= 0 ? ru->level : 1);
  int n = crk_cpu_candidates(s, &L, out, cap);
  if (n == 0) { /* nothing aimed fits (a crowded line): straight in at the middle, gently */
    out[0] = crk_move_pack((crk_shot){ CRK_POS_N / 2, 0, CRK_POW_N / 3, 0 });
    n = 1;
  }
  return n;
}

bool crk_rules_legal(const void *s, bk_move m, void *u) {
  (void)u;
  if (((const crk_state *)s)->phase != CRK_AIM || (m >> 40)) return false;
  crk_shot sh = crk_move_unpack(m);
  return crk_move_pack(sh) == m && sh.power > 0;
}

static void r_apply(void *s, bk_move m, void *u) { (void)u; crk_apply(s, m); }

static void r_eval(const void *s, float score[BK_MAX_SEATS], void *u) {
  (void)u;
  float v[4];
  crk_cpu_evaluate(s, v);
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = v[i];
}

static uint64_t r_hash(const void *s, void *u) { (void)u; return crk_hash(s); }

bk_game crk_rules(crk_rules_user *u, int players) {
  return (bk_game){ .players = players, .state_size = sizeof(crk_state), .moves = r_moves, .apply = r_apply, .turn = r_turn,
                    .terminal = r_terminal, .evaluate = r_eval, .hash = r_hash, .user = u };
}
