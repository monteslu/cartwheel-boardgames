/* boardkit: helpers over a bk_game (bk_game.h). */
#include "bk_game.h"

#include <string.h>

void bk_copy(const bk_game *g, void *dst, const void *src) {
  if (g->copy) g->copy(dst, src, g->user);
  else memcpy(dst, src, g->state_size);
}

void bk_apply_copy(const bk_game *g, const void *src, void *dst, bk_move move) {
  bk_copy(g, dst, src);
  g->apply(dst, move, g->user);
}

uint64_t bk_hash(const bk_game *g, const void *state) {
  if (g->hash) return g->hash(state, g->user);
  return cw_hash_bytes(state, g->state_size, 0);
}

bool bk_is_legal(const bk_game *g, const void *state, bk_move move) {
  if (g->terminal(state, g->user) || g->turn(state, g->user) == BK_CHANCE) return false;
  bk_move list[BK_MAX_MOVES];
  int n = g->moves(state, list, BK_MAX_MOVES, g->user);
  for (int i = 0; i < n; i++)
    if (list[i] == move) return true;
  return false;
}

int bk_settle(const bk_game *g, void *state, cw_rng *rng, bk_move *last) {
  int n = 0;
  if (last) *last = 0;
  while (!g->terminal(state, g->user) && g->turn(state, g->user) == BK_CHANCE) {
    if (g->chance) {
      g->chance(state, rng, g->user);
      if (last) *last = 0;
    } else {
      if (!g->outcomes) CW_FATAL("bk_settle: a chance node, and the game gives neither chance() nor outcomes()");
      bk_move out[BK_MAX_MOVES];
      uint32_t w[BK_MAX_MOVES];
      int k = g->outcomes(state, out, w, BK_MAX_MOVES, g->user);
      uint64_t sum = 0;
      for (int i = 0; i < k; i++) sum += w[i];
      if (k <= 0 || sum == 0) CW_FATAL("bk_settle: outcomes() listed nothing with weight at a chance node");
      uint64_t r = (uint64_t)cw_rng_u32(rng) % sum;
      int pick = k - 1;
      for (int i = 0; i < k; i++) {
        if (r < w[i]) { pick = i; break; }
        r -= w[i];
      }
      g->apply(state, out[pick], g->user);
      if (last) *last = out[pick];
    }
    n++;
  }
  return n;
}

uint64_t bk_seed_mix(uint64_t a, uint64_t b) {
  uint64_t v[2] = { a, b };
  return cw_hash_bytes(v, sizeof v, 0x6b6974u);
}
