/* bk_match: the sequencer on a dice race.
 *
 * The race: two seats, each turn a seat moves a token 1, 2 or 3 (several decisions: it may move twice in a
 * turn, then ends), and after each turn a die rolls a bonus for the next seat. Checks: illegal moves are
 * refused; undo takes back the seat's own decisions in its turn only, never past the die or into the other
 * seat's turn; undo restores the exact state (hash); the log says what happened in order; two matches with
 * the same seed and moves end with the same hash, another seed differs.
 *
 * A kit-side legal() check (a continuous move no list holds) lets the match play a move moves() does not list,
 * and still refuses what it calls illegal.
 *
 * CONTROLS that must fail: a match replayed with one move changed ends with another hash; undo after the turn
 * passed is refused. */
#include <string.h>

#include "boardkit/bk_match.h"
#include "check.h"

typedef struct race {
  int32_t pos[2], bonus[2], turn, steps_this_turn, pending_die, done;
} race;
enum { MV_END = 0 }; /* MV 1..3: advance */
static int r_turn(const void *s, void *u) { (void)u; const race *r = s; return r->pending_die ? BK_CHANCE : r->turn; }
static bool r_terminal(const void *s, void *u) { (void)u; return ((const race *)s)->done; }
static int r_moves(const void *s, bk_move *out, int cap, void *u) {
  (void)u, (void)cap;
  const race *r = s;
  int n = 0;
  if (r->steps_this_turn < 2)
    for (int k = 1; k <= 3; k++) out[n++] = (bk_move)k;
  if (r->steps_this_turn > 0) out[n++] = MV_END;
  return n;
}
static void r_apply(void *s, bk_move m, void *u) {
  (void)u;
  race *r = s;
  if (r->pending_die) { /* the die: a bonus for the seat to move next */
    r->bonus[r->turn] += (int32_t)m;
    r->pending_die = 0;
    return;
  }
  if (m == MV_END) {
    r->steps_this_turn = 0;
    r->turn ^= 1;
    r->pending_die = 1;
    return;
  }
  r->pos[r->turn] += (int32_t)m + r->bonus[r->turn];
  r->bonus[r->turn] = 0;
  r->steps_this_turn++;
  if (r->pos[r->turn] >= 30) r->done = 1;
}
static int r_outcomes(const void *s, bk_move *out, uint32_t *w, int cap, void *u) {
  (void)s, (void)u, (void)cap;
  for (int i = 0; i < 3; i++) out[i] = (bk_move)i, w[i] = 1; /* bonus 0, 1 or 2 */
  return 3;
}
static void r_eval(const void *s, float *sc, void *u) { (void)u; const race *r = s; sc[0] = (float)(r->pos[0] - r->pos[1]); sc[1] = -sc[0]; }
static const bk_game RACE = { .players = 2, .state_size = sizeof(race), .moves = r_moves, .apply = r_apply, .turn = r_turn,
                              .terminal = r_terminal, .evaluate = r_eval, .outcomes = r_outcomes };

static bool wide_legal(const void *s, bk_move m, void *u) {
  (void)u;
  const race *r = s;
  return (m >= 1 && m <= 9 && r->steps_this_turn < 2) || (m == MV_END && r->steps_this_turn > 0);
}

/* a fixed script: each turn move 2 then 1 then end */
static uint64_t play_script(uint64_t seed, int change_at) {
  race r0;
  memset(&r0, 0, sizeof r0);
  bk_match *m = bk_match_new(&RACE, &r0, seed);
  static const bk_move SCRIPT[3] = { 2, 1, MV_END };
  for (int i = 0; !bk_match_over(m) && i < 300; i++) {
    bk_move mv = SCRIPT[i % 3];
    if (i == change_at) mv = 3;
    if (!bk_match_play(m, mv)) { bk_match_free(m); return 0; }
  }
  uint64_t h = bk_match_hash(m);
  bk_match_free(m);
  return h;
}

int main(void) {
  race r0;
  memset(&r0, 0, sizeof r0);
  bk_match *m = bk_match_new(&RACE, &r0, 42);
  CHECK(bk_match_turn(m) == 0 && !bk_match_can_undo(m), "a new match: seat 0 to move, nothing to undo");
  CHECK(!bk_match_play(m, MV_END), "an illegal move (end before any step) is refused");
  uint64_t h0 = bk_match_hash(m);
  CHECK(bk_match_play(m, 2), "seat 0 steps 2");
  uint64_t h1 = bk_match_hash(m);
  CHECK(bk_match_play(m, 3) && bk_match_can_undo(m), "seat 0 steps 3 and may undo");
  CHECK(bk_match_undo(m) && bk_match_hash(m) == h1, "undo restores the state after the first step exactly");
  CHECK(bk_match_undo(m) && bk_match_hash(m) == h0, "a second undo restores the turn's start");
  CHECK(!bk_match_undo(m), "nothing more to undo at the turn's start");
  CHECK(bk_match_play(m, 1) && bk_match_play(m, MV_END), "seat 0 steps 1 and ends its turn");
  CHECK(bk_match_turn(m) == 1, "seat 1 to move (the die rolled between)");
  bool refused = !bk_match_can_undo(m) && !bk_match_undo(m);
  CHECK(refused, "seat 1 cannot undo seat 0's turn or the die");
  CONTROL(refused, "undo after the turn passed");
  bk_log_entry log[16];
  int n = bk_match_log(m, 0, log, 16);
  /* moves 2, 3, undo, undo, 1, end, chance */
  bool order = n == 7 && log[0].kind == BK_LOG_MOVE && log[0].move == 2 && log[2].kind == BK_LOG_UNDO && log[3].kind == BK_LOG_UNDO &&
               log[5].kind == BK_LOG_MOVE && log[5].move == MV_END && log[6].kind == BK_LOG_CHANCE && log[6].move <= 2 &&
               log[6].hash == bk_match_hash(m);
  CHECK(order, "the log: 2, 3, undo, undo, 1, end, die %llu (%d entries)", n > 6 ? (unsigned long long)log[6].move : 99ull, n);
  CHECK(bk_match_log(m, 5, log, 16) == 2 && log[0].index == 5, "the log from an index");
  CHECK(bk_match_moves_played(m) == 2, "moves played count only kept decisions (%d)", bk_match_moves_played(m));
  bk_match_free(m);

  /* a kit-side legal check: steps of 1..9 are legal (moves() lists 1..3 only) */
  {
    race r1;
    memset(&r1, 0, sizeof r1);
    bk_match *lm = bk_match_new(&RACE, &r1, 3);
    bool listed_only = !bk_match_play(lm, 7);
    bk_match_set_legal(lm, wide_legal, NULL);
    CHECK(listed_only && bk_match_play(lm, 7) && ((const race *)bk_match_state(lm))->pos[0] == 7, "with legal() set, a move outside moves() plays (step 7)");
    CHECK(!bk_match_play(lm, 12), "legal() still refuses a move it calls illegal (step 12)");
    CONTROL(listed_only, "without legal() the same unlisted move is refused");
    bk_match_free(lm);
  }

  uint64_t a = play_script(7, -1), b = play_script(7, -1), c = play_script(8, -1), d = play_script(7, 4);
  CHECK(a && a == b, "the same seed and moves give the same final hash (%016llx)", (unsigned long long)a);
  CHECK(c && c != a, "another seed gives another match");
  CONTROL(d && d != a, "one move changed in the script changes the final hash");
  return DONE("match");
}
