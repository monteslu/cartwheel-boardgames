/* boardkit: the match sequencer (bk_match.h). */
#include "bk_match.h"

#include <stdlib.h>
#include <string.h>

struct bk_match {
  bk_game g;
  uint8_t *state;
  cw_rng rng;
  /* undo: snapshots of state and rng before each decision of the current turn */
  uint8_t *snap;            /* BK_MATCH_UNDO_MAX states */
  cw_rng snap_rng[BK_MATCH_UNDO_MAX];
  int nsnap, snap_seat;
  int played;
  bk_log_entry log[BK_MATCH_LOG];
  int log_next;
  bk_legal_fn legal;
  void *legal_user;
};

static void log_add(bk_match *m, bk_log_kind k, int seat, bk_move mv) {
  bk_log_entry *e = &m->log[m->log_next % BK_MATCH_LOG];
  *e = (bk_log_entry){ .kind = k, .seat = seat, .move = mv, .hash = bk_hash(&m->g, m->state), .index = m->log_next };
  m->log_next++;
}

/* resolve chance, one event at a time (each logged); a chance event ends the undo chain */
static void settle(bk_match *m) {
  while (!m->g.terminal(m->state, m->g.user) && m->g.turn(m->state, m->g.user) == BK_CHANCE) {
    bk_move code = 0;
    if (m->g.chance) m->g.chance(m->state, &m->rng, m->g.user);
    else {
      if (!m->g.outcomes) CW_FATAL("bk_match: a chance node, and the game gives neither chance() nor outcomes()");
      bk_move out[BK_MAX_MOVES];
      uint32_t w[BK_MAX_MOVES];
      int k = m->g.outcomes(m->state, out, w, BK_MAX_MOVES, m->g.user);
      uint64_t sum = 0;
      for (int i = 0; i < k; i++) sum += w[i];
      if (k <= 0 || sum == 0) CW_FATAL("bk_match: outcomes() listed nothing with weight at a chance node");
      uint64_t r = (uint64_t)cw_rng_u32(&m->rng) % sum;
      int pick = k - 1;
      for (int i = 0; i < k; i++) {
        if (r < w[i]) { pick = i; break; }
        r -= w[i];
      }
      code = out[pick];
      m->g.apply(m->state, code, m->g.user);
    }
    m->nsnap = 0;
    log_add(m, BK_LOG_CHANCE, BK_CHANCE, code);
  }
}

bk_match *bk_match_new(const bk_game *g, const void *initial, uint64_t chance_seed) {
  if (!g || !g->moves || !g->apply || !g->turn || !g->terminal || !g->state_size)
    CW_FATAL("bk_match_new: the game lacks a required callback or state_size");
  bk_match *m = calloc(1, sizeof *m);
  m->g = *g;
  m->state = calloc(1, g->state_size);
  m->snap = calloc(BK_MATCH_UNDO_MAX, g->state_size);
  bk_copy(g, m->state, initial);
  cw_rng_seed(&m->rng, chance_seed);
  m->snap_seat = -1;
  settle(m);
  return m;
}

void bk_match_free(bk_match *m) {
  if (!m) return;
  free(m->state);
  free(m->snap);
  free(m);
}

const bk_game *bk_match_game(const bk_match *m) { return &m->g; }
const void *bk_match_state(const bk_match *m) { return m->state; }
bool bk_match_over(const bk_match *m) { return m->g.terminal(m->state, m->g.user); }
int bk_match_turn(const bk_match *m) { return bk_match_over(m) ? -1 : m->g.turn(m->state, m->g.user); }
uint64_t bk_match_hash(const bk_match *m) { return bk_hash(&m->g, m->state); }
int bk_match_moves_played(const bk_match *m) { return m->played; }

int bk_match_moves(const bk_match *m, bk_move *out, int cap) {
  if (bk_match_over(m)) return 0;
  return m->g.moves(m->state, out, cap, m->g.user);
}

void bk_match_set_legal(bk_match *m, bk_legal_fn legal, void *user) { m->legal = legal, m->legal_user = user; }

bool bk_match_play(bk_match *m, bk_move move) {
  if (m->legal) {
    if (bk_match_over(m) || m->g.turn(m->state, m->g.user) == BK_CHANCE || !m->legal(m->state, move, m->legal_user)) return false;
  } else if (!bk_is_legal(&m->g, m->state, move)) return false;
  int seat = m->g.turn(m->state, m->g.user);
  if (seat != m->snap_seat) m->nsnap = 0, m->snap_seat = seat;
  if (m->nsnap == BK_MATCH_UNDO_MAX) { /* drop the oldest */
    memmove(m->snap, m->snap + m->g.state_size, (BK_MATCH_UNDO_MAX - 1) * m->g.state_size);
    memmove(m->snap_rng, m->snap_rng + 1, (BK_MATCH_UNDO_MAX - 1) * sizeof m->snap_rng[0]);
    m->nsnap--;
  }
  bk_copy(&m->g, m->snap + (size_t)m->nsnap * m->g.state_size, m->state);
  m->snap_rng[m->nsnap++] = m->rng;
  m->g.apply(m->state, move, m->g.user);
  m->played++;
  log_add(m, BK_LOG_MOVE, seat, move);
  /* the turn passed: the old seat's decisions are final */
  if (bk_match_over(m) || m->g.turn(m->state, m->g.user) != seat) m->nsnap = 0;
  settle(m);
  return true;
}

bool bk_match_can_undo(const bk_match *m) {
  return m->nsnap > 0 && !bk_match_over(m) && m->g.turn(m->state, m->g.user) == m->snap_seat;
}

bool bk_match_undo(bk_match *m) {
  if (!bk_match_can_undo(m)) return false;
  m->nsnap--;
  bk_copy(&m->g, m->state, m->snap + (size_t)m->nsnap * m->g.state_size);
  m->rng = m->snap_rng[m->nsnap];
  m->played--;
  log_add(m, BK_LOG_UNDO, m->snap_seat, 0);
  return true;
}

void bk_match_seal(bk_match *m) { m->nsnap = 0; }

int bk_match_log_next(const bk_match *m) { return m->log_next; }

int bk_match_log(const bk_match *m, int since, bk_log_entry *out, int cap) {
  int first = m->log_next - BK_MATCH_LOG;
  if (first < 0) first = 0;
  if (since < first) since = first;
  int n = 0;
  for (int i = since; i < m->log_next && n < cap; i++) out[n++] = m->log[i % BK_MATCH_LOG];
  return n;
}
