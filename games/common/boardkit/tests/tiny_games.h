/* Two tiny games for the kit tests: three in a row (two seats) and a dice choice (one seat, chance). */
#ifndef BK_TINY_GAMES_H
#define BK_TINY_GAMES_H
#include <string.h>

#include "boardkit/bk_pieces.h"

/* ── three in a row ── */
typedef struct ttt { int8_t cell[9]; int8_t turn, moves, winner, pad; } ttt; /* winner: -1 none, 0/1, 2 draw */
static const int LINES[8][3] = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 }, { 0, 3, 6 }, { 1, 4, 7 }, { 2, 5, 8 }, { 0, 4, 8 }, { 2, 4, 6 } };
static bool blind; /* a control: evaluate cannot see a win */

static int ttt_turn(const void *s, void *u) { (void)u; return ((const ttt *)s)->turn; }
static bool ttt_terminal(const void *s, void *u) { (void)u; return ((const ttt *)s)->winner >= 0; }
static int ttt_moves(const void *s, bk_move *out, int cap, void *u) {
  (void)u;
  const ttt *t = s;
  int n = 0;
  for (int i = 0; i < 9 && n < cap; i++)
    if (t->cell[i] < 0) out[n++] = (bk_move)i;
  return n;
}
static void ttt_apply(void *s, bk_move m, void *u) {
  (void)u;
  ttt *t = s;
  t->cell[m] = t->turn;
  t->moves++;
  for (int l = 0; l < 8; l++)
    if (t->cell[LINES[l][0]] == t->turn && t->cell[LINES[l][1]] == t->turn && t->cell[LINES[l][2]] == t->turn) t->winner = t->turn;
  if (t->winner < 0 && t->moves == 9) t->winner = 2;
  t->turn ^= 1;
}
static void ttt_eval(const void *s, float *sc, void *u) {
  (void)u;
  const ttt *t = s;
  if (blind || t->winner < 0 || t->winner == 2) { sc[0] = sc[1] = 0; return; }
  float w = BK_WIN - (float)t->moves; /* a faster win scores higher */
  sc[t->winner] = w, sc[t->winner ^ 1] = -w;
}
static const bk_game TTT = { .players = 2, .state_size = sizeof(ttt), .moves = ttt_moves, .apply = ttt_apply, .turn = ttt_turn,
                             .terminal = ttt_terminal, .evaluate = ttt_eval };
static ttt ttt_from(const char *b, int turn) { /* "x.o......" */
  ttt t;
  memset(&t, 0, sizeof t);
  t.winner = -1;
  t.turn = (int8_t)turn;
  for (int i = 0; i < 9; i++) {
    t.cell[i] = b[i] == 'x' ? 0 : b[i] == 'o' ? 1 : -1;
    t.moves += t.cell[i] >= 0;
  }
  return t;
}

/* ── the dice choice ── */
typedef struct dice { int32_t pending, banked, done; float bank_tenths; } dice;
static uint32_t die_w[6] = { 1, 1, 1, 1, 1, 1 };
static int d_turn(const void *s, void *u) { (void)u; return ((const dice *)s)->pending ? BK_CHANCE : 0; }
static bool d_terminal(const void *s, void *u) { (void)u; return ((const dice *)s)->done; }
static int d_moves(const void *s, bk_move *out, int cap, void *u) { (void)s, (void)u, (void)cap; out[0] = 0; out[1] = 1; return 2; } /* 0 bank, 1 roll */
static void d_apply(void *s, bk_move m, void *u) {
  (void)u;
  dice *d = s;
  if (d->pending) { d->banked = (int32_t)m * 10; d->pending = 0; d->done = 1; return; } /* an outcome: the face, 1..6 */
  if (m == 0) { d->banked = 34; d->done = 1; }
  else d->pending = 1;
}
static int d_outcomes(const void *s, bk_move *out, uint32_t *w, int cap, void *u) {
  (void)s, (void)u, (void)cap;
  int n = bk_die_outcomes(6, out, w, cap, NULL); /* the kit helper, then the (control's) loading */
  for (int i = 0; i < n; i++) w[i] *= die_w[i];
  return n;
}
static void d_eval(const void *s, float *sc, void *u) { (void)u; sc[0] = (float)((const dice *)s)->banked; }
static const bk_game DICE = { .players = 1, .state_size = sizeof(dice), .moves = d_moves, .apply = d_apply, .turn = d_turn,
                              .terminal = d_terminal, .evaluate = d_eval, .outcomes = d_outcomes };

#endif
