/* kitdemo rules (rules.h): pure, a bk_game (cw_search_game). */
#include "rules.h"

#include <string.h>

void kd_setup(kd_state *s) {
  memset(s, 0, sizeof *s);
  for (int c = 0; c < KD_CARDS; c++) s->deck[s->deck_n++] = (uint16_t)c;
  for (int i = 0; i < KD_SLOTS; i++) s->market[i] = BK_NO_CARD;
  s->phase = KD_REFILL; /* the opening market: three chance draws */
  s->slot = -1;
  s->card = -1;
  memset(s->owner, 0xff, sizeof s->owner);
}

static int empty_slot(const kd_state *s) { return bk_market_empty_slot(s->market, KD_SLOTS); }
static bool market_empty(const kd_state *s) {
  for (int i = 0; i < KD_SLOTS; i++)
    if (s->market[i] != BK_NO_CARD) return false;
  return true;
}

static int turn(const void *st, void *u) {
  (void)u;
  const kd_state *s = st;
  return s->phase == KD_ROLL || s->phase == KD_REFILL ? BK_CHANCE : s->turn;
}
static bool terminal(const void *st, void *u) { (void)u; return ((const kd_state *)st)->phase == KD_OVER; }

static int moves(const void *st, bk_move *out, int cap, void *u) {
  (void)u, (void)cap;
  const kd_state *s = st;
  int n = 0;
  if (s->phase == KD_PICK)
    for (int i = 0; i < KD_SLOTS; i++)
      if (s->market[i] != BK_NO_CARD) out[n++] = KD_MV_SLOT(i);
  if (s->phase == KD_CHOOSE) out[n++] = KD_MV_BANK, out[n++] = KD_MV_ROLL;
  return n;
}

static bk_move card_code(uint16_t c) { return KD_MV_CARD(c); }
static bk_move face_code(int f) { return KD_MV_FACE(f); }

static int outcomes(const void *st, bk_move *out, uint32_t *w, int cap, void *u) {
  (void)u;
  const kd_state *s = st;
  if (s->phase == KD_ROLL) return bk_die_outcomes(6, out, w, cap, face_code);
  if (s->phase == KD_REFILL) return bk_deck_outcomes(s->deck, s->deck_n, out, w, cap, card_code);
  return 0;
}

/* after a card is settled: refill its slot (chance) or pass the turn */
static void next(kd_state *s) {
  s->owner[s->card] = (uint8_t)s->turn;
  s->card = -1;
  if (s->deck_n > 0 && empty_slot(s) >= 0) { s->phase = KD_REFILL; return; }
  if (market_empty(s)) { s->phase = KD_OVER; return; }
  s->phase = KD_PICK;
  s->turn ^= 1;
  s->slot = -1;
}

static void apply(void *st, bk_move m, void *u) {
  (void)u;
  kd_state *s = st;
  switch (s->phase) {
  case KD_PICK:
    s->slot = (int32_t)m;
    s->card = s->market[m];
    s->market[m] = BK_NO_CARD;
    s->phase = KD_CHOOSE;
    s->moves++;
    break;
  case KD_CHOOSE:
    s->moves++;
    if (m == KD_MV_BANK) {
      s->last_gain = kd_value(s->card);
      s->score[s->turn] += s->last_gain;
      s->last_die = 0;
      next(s);
    } else s->phase = KD_ROLL;
    break;
  case KD_ROLL: {
    int face = (int)(m - KD_MV_FACE(0));
    s->last_die = face;
    s->last_gain = face >= kd_value(s->card) ? 2 * kd_value(s->card) : 0;
    s->score[s->turn] += s->last_gain;
    next(s);
    break;
  }
  case KD_REFILL: {
    uint16_t c = (uint16_t)(m - KD_MV_CARD(0));
    bk_deck_remove(s->deck, &s->deck_n, c);
    s->market[empty_slot(s)] = c;
    if (empty_slot(s) >= 0 && s->deck_n > 0) break; /* the opening market: more to draw */
    s->phase = KD_PICK;
    if (s->moves > 0) s->turn ^= 1; /* after a turn the next seat; the opening market leaves seat 0 to start */
    s->slot = -1;
    break;
  }
  default: CW_FATAL("kitdemo: a move in phase %d", s->phase);
  }
}

int kd_winner(const kd_state *s) {
  if (s->phase != KD_OVER) return -1;
  return s->score[0] > s->score[1] ? 0 : s->score[1] > s->score[0] ? 1 : 2;
}

static void evaluate(const void *st, float *score, void *u) {
  (void)u;
  const kd_state *s = st;
  float d = (float)(s->score[0] - s->score[1]);
  if (s->phase == KD_OVER) {
    int w = kd_winner(s);
    float band = BK_WIN - (float)s->moves;
    score[0] = w == 0 ? band : w == 1 ? -band : 0;
    score[1] = -score[0];
    return;
  }
  /* a card in hand is worth about its bank value to the seat holding it */
  if (s->card >= 0) d += (s->turn == 0 ? 1.0f : -1.0f) * (float)kd_value(s->card);
  score[0] = d, score[1] = -d;
}

const bk_game KD_RULES = { .players = 2, .state_size = sizeof(kd_state), .moves = moves, .apply = apply, .turn = turn,
                           .terminal = terminal, .evaluate = evaluate, .outcomes = outcomes };
