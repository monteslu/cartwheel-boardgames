/* Mandible rules tests: perft, each movement rule with a control that must come out the other way, random
 * games with undo round trips, seeded CPU games, and the level ladder (argument: games per pair). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rules.h"
#include "cartwheel/search.h"

static int fails = 0;
#define CHECK(cond, ...)                                                                                       \
  do {                                                                                                         \
    if (!(cond)) {                                                                                             \
      fails++;                                                                                                 \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                     \
      fprintf(stderr, __VA_ARGS__);                                                                            \
      fprintf(stderr, "\n");                                                                                   \
    }                                                                                                          \
  } while (0)

static uint64_t perft(sw_state *s, int depth) {
  if (depth == 0) return 1;
  sw_move m[SW_MAX_MOVES];
  int n = sw_moves(s, m, SW_MAX_MOVES);
  if (depth == 1) return (uint64_t)n;
  uint64_t t = 0;
  for (int i = 0; i < n; i++) {
    sw_state before = *s;
    sw_apply(s, m[i]);
    t += perft(s, depth - 1);
    sw_undo(s, m[i]);
    if (memcmp(&before, s, sizeof before) != 0) {
      fails++;
      fprintf(stderr, "FAIL undo did not restore the state\n");
      return 0;
    }
  }
  return t;
}

/* piece ids by side and kind: the n-th piece of that kind */
static int pid(int side, int kind, int n) {
  static const int base[SW_KINDS] = { 0, 1, 3, 6, 8 };
  return side * SW_SIDE_PIECES + base[kind] + n;
}

/* count the moves of one piece to one hex */
static int moves_to(const sw_state *s, int id, cw_axial to) {
  sw_move m[SW_MAX_MOVES];
  int n = sw_moves(s, m, SW_MAX_MOVES), c = 0;
  for (int i = 0; i < n; i++)
    if (sw_move_kind(m[i]) == SW_MOVE_STEP && sw_move_id(m[i]) == id && sw_move_to(m[i]).q == to.q &&
        sw_move_to(m[i]).r == to.r)
      c++;
  return c;
}
static int moves_of(const sw_state *s, int id) {
  sw_move m[SW_MAX_MOVES];
  int n = sw_moves(s, m, SW_MAX_MOVES), c = 0;
  for (int i = 0; i < n; i++)
    if (sw_move_kind(m[i]) == SW_MOVE_STEP && sw_move_id(m[i]) == id) c++;
  return c;
}
static int place_moves(const sw_state *s) {
  sw_move m[SW_MAX_MOVES];
  int n = sw_moves(s, m, SW_MAX_MOVES), c = 0;
  for (int i = 0; i < n; i++)
    if (sw_move_kind(m[i]) == SW_MOVE_PLACE) c++;
  return c;
}

static cw_axial H(int q, int r) { return (cw_axial){ q, r }; }

static void test_perft(void) {
  sw_state s;
  sw_init(&s);
  CHECK(perft(&s, 1) == 5, "perft 1");
  CHECK(perft(&s, 2) == 150, "perft 2 = %llu", (unsigned long long)perft(&s, 2));
  /* 2220: an Empress placed first may already step on her second turn */
  CHECK(perft(&s, 3) == 2220, "perft 3 = %llu", (unsigned long long)perft(&s, 3));
  uint64_t p4 = perft(&s, 4);
  printf("perft 4 = %llu\n", (unsigned long long)p4);
}

/* a ring of six around an empty hole with one gap: a Runner may not squeeze in; with the gap two wide it may */
static void test_gate(void) {
  sw_state s;
  sw_init(&s);
  /* black pieces around (0,0) except direction 0, white Empress + Runner outside */
  cw_axial hole = H(0, 0);
  int ids[5] = { pid(1, SW_LOCUST, 0), pid(1, SW_LOCUST, 1), pid(1, SW_LOCUST, 2), pid(1, SW_WEAVER, 0), pid(1, SW_WEAVER, 1) };
  for (int d = 1; d < 6; d++) sw_put(&s, ids[d - 1], cw_hex_neighbor(hole, d));
  sw_put(&s, pid(1, SW_EMPRESS, 0), H(3, -2));
  sw_put(&s, pid(1, SW_RAM, 0), H(2, -1)); /* joins the black Empress to the ring */
  sw_put(&s, pid(0, SW_EMPRESS, 0), H(0, -2));
  sw_put(&s, pid(0, SW_RUNNER, 0), H(-1, -1));
  s.turns[0] = s.turns[1] = 5;
  /* direction 0 of the hole is (1,0): its sides are (1,-1) (occupied) and (0,1) (occupied) -> blocked */
  CHECK(moves_to(&s, pid(0, SW_RUNNER, 0), hole) == 0, "runner squeezed through a closed gap");
  /* control: remove one side of the gap, the Runner gets in */
  sw_state t = s;
  /* lift the piece at (0,1) (direction 5) and move it next to the black Empress instead */
  int idb = sw_top_at(&t, H(0, 1));
  t.top[sw_cell(H(0, 1))] = 0;
  t.p[idb].placed = 0;
  sw_put(&t, idb, H(4, -2));
  if (moves_to(&t, pid(0, SW_RUNNER, 0), hole) == 1) printf("CONTROL the opened gap lets the runner in: caught\n");
  else { fails++; fprintf(stderr, "FAIL control: runner should reach an open hole\n"); }
}

static void test_one_group(void) {
  sw_state s;
  sw_init(&s);
  sw_put(&s, pid(0, SW_EMPRESS, 0), H(0, 0));
  sw_put(&s, pid(0, SW_RUNNER, 0), H(1, 0));   /* the bridge */
  sw_put(&s, pid(1, SW_EMPRESS, 0), H(2, 0));
  sw_put(&s, pid(0, SW_LOCUST, 0), H(-1, 0));
  s.turns[0] = s.turns[1] = 4;
  CHECK(moves_of(&s, pid(0, SW_RUNNER, 0)) == 0, "bridge piece moved and split the group");
  if (moves_of(&s, pid(0, SW_LOCUST, 0)) > 0) printf("CONTROL an end piece of the line moves: caught\n");
  else { fails++; fprintf(stderr, "FAIL control: an end piece must move\n"); }
  uint8_t pin[SW_PIECES];
  sw_pinned(&s, pin);
  CHECK(pin[pid(0, SW_RUNNER, 0)] && pin[pid(0, SW_EMPRESS, 0)] && !pin[pid(0, SW_LOCUST, 0)], "pinned flags");
}

static void test_locust(void) {
  sw_state s;
  sw_init(&s);
  sw_put(&s, pid(0, SW_EMPRESS, 0), H(0, 0));
  sw_put(&s, pid(0, SW_LOCUST, 0), H(-1, 0));
  sw_put(&s, pid(1, SW_EMPRESS, 0), H(1, 0));
  sw_put(&s, pid(1, SW_RAM, 0), H(2, 0));
  s.turns[0] = s.turns[1] = 4;
  CHECK(moves_to(&s, pid(0, SW_LOCUST, 0), H(3, 0)) == 1, "locust jumps the whole line");
  CHECK(moves_to(&s, pid(0, SW_LOCUST, 0), H(1, -1)) == 0, "control: locust may not slide like a walker");
  CHECK(moves_of(&s, pid(0, SW_LOCUST, 0)) == 1, "locust has exactly one jump, got %d", moves_of(&s, pid(0, SW_LOCUST, 0)));
}

static void test_weaver_and_ram(void) {
  sw_state s;
  sw_init(&s);
  /* a straight line of four, weaver at one end */
  sw_put(&s, pid(0, SW_WEAVER, 0), H(-1, 0));
  sw_put(&s, pid(0, SW_EMPRESS, 0), H(0, 0));
  sw_put(&s, pid(1, SW_EMPRESS, 0), H(1, 0));
  sw_put(&s, pid(1, SW_RAM, 0), H(2, 0));
  s.turns[0] = s.turns[1] = 4;
  /* three slides along the top: (-1,0) -> (0,-1) -> (1,-1) -> (2,-1); along the bottom (-1,1) -> (0,1) -> (1,1) */
  CHECK(moves_to(&s, pid(0, SW_WEAVER, 0), H(2, -1)) == 1, "weaver three steps along the top");
  CHECK(moves_to(&s, pid(0, SW_WEAVER, 0), H(1, 1)) == 1, "weaver three steps along the bottom");
  CHECK(moves_to(&s, pid(0, SW_WEAVER, 0), H(1, -1)) == 0, "control: two steps is not a weaver move");
  CHECK(moves_of(&s, pid(0, SW_WEAVER, 0)) == 2, "weaver moves = %d", moves_of(&s, pid(0, SW_WEAVER, 0)));
  /* Ram: climbs onto the Empress next to it; control, the Empress cannot */
  sw_state t;
  sw_init(&t);
  sw_put(&t, pid(0, SW_RAM, 0), H(-1, 0));
  sw_put(&t, pid(0, SW_EMPRESS, 0), H(0, 0));
  sw_put(&t, pid(1, SW_EMPRESS, 0), H(1, 0));
  t.turns[0] = t.turns[1] = 4;
  CHECK(moves_to(&t, pid(0, SW_RAM, 0), H(0, 0)) == 1, "ram climbs");
  CHECK(moves_of(&t, pid(0, SW_RAM, 0)) == 3, "ram has 3 moves (climb + two slides), got %d", moves_of(&t, pid(0, SW_RAM, 0)));
  sw_move up = sw_move_make(SW_MOVE_STEP, pid(0, SW_RAM, 0), H(-1, 0), H(0, 0));
  sw_apply(&t, up);
  CHECK(sw_height_at(&t, H(0, 0)) == 2, "stack of two");
  CHECK(sw_top_at(&t, H(0, 0)) == pid(0, SW_RAM, 0), "ram on top");
  sw_apply(&t, sw_move_make(SW_MOVE_PASS, 0, H(0, 0), H(0, 0))); /* (black's turn: not legal-checked here) */
  CHECK(moves_of(&t, pid(0, SW_EMPRESS, 0)) == 0, "covered empress cannot move");
  CHECK(moves_of(&t, pid(0, SW_RAM, 0)) == 6, "ram on top steps in all six directions, got %d", moves_of(&t, pid(0, SW_RAM, 0)));
}

static void test_placement_and_empress_rule(void) {
  sw_state s;
  sw_init(&s);
  /* play three turns each without the Empress: on the 4th, white may only place the Empress */
  int order[6][2] = { { 0, SW_LOCUST }, { 1, SW_LOCUST }, { 0, SW_LOCUST }, { 1, SW_LOCUST }, { 0, SW_RUNNER }, { 1, SW_RUNNER } };
  for (int t = 0; t < 6; t++) {
    sw_move m[SW_MAX_MOVES];
    int n = sw_moves(&s, m, SW_MAX_MOVES);
    sw_move pick = 0;
    for (int i = 0; i < n && !pick; i++)
      if (sw_move_kind(m[i]) == SW_MOVE_PLACE && s.p[sw_move_id(m[i])].kind == order[t][1]) pick = m[i];
    CHECK(pick != 0, "no placement for turn %d", t);
    /* placements never touch the opponent after the first two */
    if (t >= 2) {
      cw_axial to = sw_move_to(pick);
      for (int d = 0; d < 6; d++) {
        int id = sw_top_at(&s, cw_hex_neighbor(to, d));
        CHECK(id < 0 || s.p[id].side == s.to_move, "placement touches the opponent");
      }
    }
    sw_apply(&s, pick);
  }
  sw_move m[SW_MAX_MOVES];
  int n = sw_moves(&s, m, SW_MAX_MOVES);
  bool only_empress = n > 0;
  for (int i = 0; i < n; i++)
    if (sw_move_kind(m[i]) != SW_MOVE_PLACE || s.p[sw_move_id(m[i])].kind != SW_EMPRESS) only_empress = false;
  CHECK(only_empress, "fourth turn must place the Empress");
  /* control: on the third turn other kinds were allowed (checked above by placing a Runner) and no piece could
   * move before the Empress: */
  CHECK(place_moves(&s) == n, "no steps before the Empress is down");
}

static void test_win(void) {
  sw_state s;
  sw_init(&s);
  cw_axial c = H(0, 0);
  sw_put(&s, pid(1, SW_EMPRESS, 0), c);
  int ring[6] = { pid(0, SW_EMPRESS, 0), pid(0, SW_RAM, 0), pid(0, SW_LOCUST, 0), pid(0, SW_LOCUST, 1), pid(0, SW_WEAVER, 0), pid(1, SW_RUNNER, 0) };
  for (int d = 0; d < 5; d++) sw_put(&s, ring[d], cw_hex_neighbor(c, d));
  /* white Runner at the far side walks into the last hex */
  sw_put(&s, pid(0, SW_RUNNER, 0), H(1, 1));
  s.turns[0] = s.turns[1] = 6;
  CHECK(s.result == SW_ONGOING, "not over yet");
  sw_move w = sw_move_make(SW_MOVE_STEP, pid(0, SW_RUNNER, 0), H(1, 1), cw_hex_neighbor(c, 5));
  CHECK(sw_is_legal(&s, w), "the closing move is legal");
  sw_apply(&s, w);
  CHECK(s.result == SW_WHITE_WINS, "surrounded empress loses, result %d", s.result);
  float sc[2];
  sw_evaluate(&s, sc);
  CHECK(sc[0] > SW_WIN / 2 && sc[1] < -SW_WIN / 2, "terminal bands");
  sw_undo(&s, w);
  CHECK(s.result == SW_ONGOING, "control: undo clears the result");
}

/* threefold repetition: two Rams stepping out and back bring the start position up a third time after eight
 * plies, a draw; CONTROL: the same eight plies with Ebony's Ram walking on (no position comes up three times)
 * must not end the game. Undo takes the draw back. */
static int shuffle(bool walk_on) {
  sw_state s;
  sw_init(&s);
  sw_put(&s, pid(0, SW_EMPRESS, 0), H(0, 0));
  sw_put(&s, pid(1, SW_EMPRESS, 0), H(1, 0));
  sw_put(&s, pid(0, SW_RAM, 0), H(-1, 0));
  sw_put(&s, pid(1, SW_RAM, 0), H(2, 0));
  s.turns[0] = s.turns[1] = 6;
  s.hist[0] = (uint32_t)s.key; /* a hand-made start: its key is the first position */
  cw_axial wa = H(-1, 0), wb = H(-1, 1);
  /* Ebony's Ram: out and back, or (walk_on) round the group's far end */
  cw_axial bpath[5] = { H(2, 0), H(2, -1), H(2, 0), H(2, -1), H(2, 0) };
  cw_axial bwalk[5] = { H(2, 0), H(2, -1), H(1, -1), H(0, -1), H(-1, -1) };
  cw_axial *bp = walk_on ? bwalk : bpath;
  sw_move last = 0;
  for (int i = 0; i < 4; i++) {
    sw_move w = sw_move_make(SW_MOVE_STEP, pid(0, SW_RAM, 0), i % 2 ? wb : wa, i % 2 ? wa : wb);
    if (!sw_is_legal(&s, w)) { fails++; fprintf(stderr, "FAIL repetition: Ivory's Ram step %d is not legal\n", i); return -1; }
    sw_apply(&s, w);
    if (s.result != SW_ONGOING) return s.ply;
    sw_move b = sw_move_make(SW_MOVE_STEP, pid(1, SW_RAM, 0), bp[i], bp[i + 1]);
    if (!sw_is_legal(&s, b)) { fails++; fprintf(stderr, "FAIL repetition: Ebony's Ram step %d is not legal\n", i); return -1; }
    sw_apply(&s, b);
    last = b;
    if (s.result != SW_ONGOING) break;
  }
  if (s.result == SW_DRAW) {
    CHECK(sw_drawn_by_repetition(&s), "the draw is the repetition draw (reps %d)", s.reps);
    sw_undo(&s, last);
    CHECK(s.result == SW_ONGOING && s.reps == 2, "undo takes the repetition draw back (result %d, reps %d)", s.result, s.reps);
    return 8;
  }
  return 0;
}
static void test_repetition(void) {
  int at = shuffle(false);
  CHECK(at == 8, "the start position a third time is a draw after 8 plies (%d)", at);
  int ctl = shuffle(true);
  if (ctl == 0) printf("CONTROL a Ram walking on never repeats a position three times: caught\n");
  else { fails++; fprintf(stderr, "FAIL control: the walk ended at ply %d\n", ctl); }
}

static uint64_t rng(uint64_t *x) {
  uint64_t z = (*x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

static void test_random_games(int games) {
  int results[4] = { 0 };
  for (int g = 0; g < games; g++) {
    sw_state s;
    sw_init(&s);
    uint64_t seed = 1234 + (uint64_t)g;
    while (!sw_terminal(&s)) {
      sw_move m[SW_MAX_MOVES];
      int n = sw_moves(&s, m, SW_MAX_MOVES);
      if (n <= 0) { fails++; fprintf(stderr, "FAIL no moves in a live game\n"); break; }
      sw_move pick = m[rng(&seed) % (uint64_t)n];
      sw_state before = s;
      sw_apply(&s, pick);
      sw_state after = s;
      sw_undo(&s, pick);
      if (memcmp(&before, &s, sizeof s) != 0) { fails++; fprintf(stderr, "FAIL undo mismatch game %d ply %d\n", g, s.ply); break; }
      s = after;
      uint8_t pin[SW_PIECES];
      sw_pinned(&s, pin); /* aborts if the table split */
    }
    results[s.result]++;
  }
  printf("random games: %d white, %d black, %d draws\n", results[1], results[2], results[3]);
}

/* ── the CPU through the kit's search ─────────────────────────────────── */

static int draws_by_surround = 0;
static int64_t total_nodes = 0, total_searches = 0;

/* one full CPU-vs-CPU game, white at level lw, black at lb; returns the result */
static int play(int lw, int lb, uint64_t seed, int *plies, uint64_t *final_key) {
  const bk_game *g = mandible_bk_game();
  sw_state s;
  sw_init(&s);
  while (!sw_terminal(&s)) {
    /* a fresh search per move, its seed mixed with the position (as bk_cpu does) */
    cw_search_options o;
    mandible_level_options(s.to_move == 0 ? lw : lb, &o);
    o.seed = seed * 0x9e3779b97f4a7c15ull ^ s.key;
    cw_search *ss = cw_search_new(g, &o);
    bk_move m = BK_MOVE_NONE;
    if (!ss || !cw_search_run(ss, &s, &m)) m = BK_MOVE_NONE;
    cw_search_stats st;
    cw_search_get_stats(ss, &st);
    cw_search_free(ss);
    total_nodes += st.nodes;
    total_searches++;
    if (m == BK_MOVE_NONE || !sw_is_legal(&s, m)) {
      fails++;
      fprintf(stderr, "FAIL illegal cpu move at ply %d\n", s.ply);
      break;
    }
    sw_apply(&s, m);
  }
  if (plies) *plies = s.ply;
  if (final_key) *final_key = s.key;
  if (s.result == SW_DRAW && s.ply < SW_MAX_PLY) draws_by_surround++;
  return s.result;
}

/* a CPU game ends with a legal winner, and the same seed plays the same game */
static void test_cpu_game(void) {
  int plies = 0, plies2 = 0;
  uint64_t k1 = 0, k2 = 0;
  int r = play(1, 0, 7, &plies, &k1);
  CHECK(r == SW_WHITE_WINS || r == SW_BLACK_WINS || r == SW_DRAW, "cpu game ended with result %d", r);
  int r2 = play(1, 0, 7, &plies2, &k2);
  CHECK(r == r2 && plies == plies2 && k1 == k2, "the same seed played a different game");
  /* control: another seed plays a different game */
  uint64_t k3 = 0;
  play(1, 0, 8, NULL, &k3);
  if (k3 != k1) printf("CONTROL another seed played another game: caught\n");
  else { fails++; fprintf(stderr, "FAIL control: seeds 7 and 8 played the same game\n"); }
  printf("cpu game: result %d after %d plies\n", r, plies);
}

/* the level ladder: each pair over n seeded games, colours alternating */
static void test_levels(int n) {
  const char *name[3] = { "Easy", "Normal", "Hard" };
  int pairs[3][2] = { { 1, 0 }, { 2, 1 }, { 2, 0 } };
  for (int p = 0; p < 3; p++) {
    int a = pairs[p][0], b = pairs[p][1], win = 0, loss = 0, draw = 0, plysum = 0;
    for (int g = 0; g < n; g++) {
      int ply = 0;
      int r = g % 2 == 0 ? play(a, b, 100 + (uint64_t)g, &ply, NULL) : play(b, a, 100 + (uint64_t)g, &ply, NULL);
      plysum += ply;
      int awins = g % 2 == 0 ? SW_WHITE_WINS : SW_BLACK_WINS;
      if (r == SW_DRAW) draw++;
      else if (r == awins) win++;
      else loss++;
    }
    printf("LEVELS %s vs %s over %d games: %d wins, %d losses, %d draws (avg %d plies, %d draws by double surround, %lld nodes per search)\n",
           name[a], name[b], n, win, loss, draw, plysum / n, draws_by_surround,
           (long long)(total_nodes / (total_searches ? total_searches : 1)));
    /* the stronger level must win clearly more than it loses */
    CHECK(win > 2 * loss, "%s did not beat %s clearly", name[a], name[b]);
    draws_by_surround = 0;
    total_nodes = total_searches = 0;
  }
}

/* controls for the rule checks: a broken rule set must fail the same checks */
static void test_controls(void) {
  /* perft 3 must not be the count without the early Empress step (the rule that makes it 2220) */
  sw_state s;
  sw_init(&s);
  if (perft(&s, 3) != 2160) printf("CONTROL perft differs from the count without the Empress step: caught\n");
  else { fails++; fprintf(stderr, "FAIL control perft\n"); }
}

int main(int argc, char **argv) {
  test_perft();
  test_gate();
  test_one_group();
  test_locust();
  test_weaver_and_ram();
  test_placement_and_empress_rule();
  test_win();
  test_repetition();
  test_controls();
  test_random_games(200);
  test_cpu_game();
  int lv = argc > 1 ? atoi(argv[1]) : 0;
  if (lv > 0) test_levels(lv);
  if (fails) {
    fprintf(stderr, "%d failures\n", fails);
    return 1;
  }
  printf("all mandible rules tests passed\n");
  return 0;
}
