/* Rose Window rules tests: placement and scoring units, the frame table, dice conservation, determinism, random and
 * CPU full games at 1 to 4 seats, and CPU level strength. Every check has a control that must fail.
 *
 *   rosewindow_rules_test              all tests
 *   rosewindow_rules_test strength N   also the Easy/Normal/Hard two-seat win rates over N seeded games per pairing
 *                                      (seats swapped) and the solo win rate of each level
 *   rosewindow_rules_test bench P S N  CPU games at P seats (2: Hard/Normal, Hard/Easy, Normal/Easy, each both ways round;
 *                                      3: Easy, Normal, Hard; 4: Hard, Normal, Normal, Easy; seats rotated) on seeds S..S+N-1,
 *                                      one "game" line each (levels, scores, winners) for tests/rosewindow-strength.mjs.
 *                                      RW_REWARD=place|margin and RW_RSCALE=x give the MCTS seats another reward to compare */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"
#include "cpu.h"
#include "rules.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define CONTROL(caught, name) do { if (caught) printf("CONTROL %s caught\n", name); else { failures++; fprintf(stderr, "FAIL control %s was not caught\n", name); } } while (0)

static bool census_ok(const rw_state *s) {
  int n[RW_COLOURS];
  rw_census(s, n);
  for (int c = 0; c < RW_COLOURS; c++)
    if (n[c] != RW_PER_COLOUR) return false;
  return true;
}

static bool tool_in_play(const rw_state *s, int kind) {
  for (int t = 0; t < s->ntools; t++) if (s->tools[t] == kind) return true;
  return false;
}

/* invariants of a state; a reason or NULL */
static const char *invariants(const rw_state *s) {
  if (!census_ok(s)) return "dice census is not 18 a colour";
  bool marks_bend = tool_in_play(s, RW_T_GLAZE) || tool_in_play(s, RW_T_TINT) || tool_in_play(s, RW_T_ETCH);
  for (int p = 0; p < s->players; p++) {
    const rw_player *pl = &s->p[p];
    int placed = 0;
    for (int i = 0; i < RW_CELLS; i++) {
      uint8_t d = pl->cell[i];
      if (!d) continue;
      placed++;
      if (rw_die_value(d) < 1 || rw_die_value(d) > 6 || rw_die_colour(d) >= RW_COLOURS) return "a malformed die";
      int r = i / RW_COLS, c = i % RW_COLS;
      if (c + 1 < RW_COLS && pl->cell[i + 1]) {
        uint8_t e = pl->cell[i + 1];
        if (rw_die_colour(e) == rw_die_colour(d) || rw_die_value(e) == rw_die_value(d)) return "side-by-side dice share a colour or number";
      }
      if (r + 1 < RW_ROWS && pl->cell[i + RW_COLS]) {
        uint8_t e = pl->cell[i + RW_COLS];
        if (rw_die_colour(e) == rw_die_colour(d) || rw_die_value(e) == rw_die_value(d)) return "stacked dice share a colour or number";
      }
      if (!marks_bend) {
        int code = rw_frames[pl->frame].cell[i];
        int mc = rw_mark_colour(code), mv = rw_mark_value(code);
        if ((mc >= 0 && mc != rw_die_colour(d)) || (mv && mv != rw_die_value(d))) return "a die breaks its pane's mark";
      }
    }
    if (placed != pl->placed) return "placed count is off";
    if (pl->favour > 6) return "favour above six";
  }
  return NULL;
}

static void test_frames(void) {
  int ratings[7] = { 0 };
  for (int f = 0; f < RW_FRAMES; f++) {
    const rw_frame *fr = &rw_frames[f];
    CHECK(fr->rating >= 3 && fr->rating <= 6, "frame %s rating %d", fr->name, fr->rating);
    ratings[fr->rating]++;
    for (int i = 0; i < RW_CELLS; i++) {
      int code = fr->cell[i];
      CHECK(code == 0 || (code >= 1 && code <= 5) || (code >= 11 && code <= 16), "frame %s cell %d code %d", fr->name, i, code);
      if (code && i % RW_COLS + 1 < RW_COLS) CHECK(fr->cell[i + 1] != code, "frame %s cells %d and %d ask the same", fr->name, i, i + 1);
      if (code && i + RW_COLS < RW_CELLS) CHECK(fr->cell[i + RW_COLS] != code, "frame %s cells %d and %d ask the same", fr->name, i, i + RW_COLS);
    }
  }
  for (int r = 3; r <= 6; r++) CHECK(ratings[r] > 0, "no frame rated %d", r);
}

static void test_units(void) {
  rw_state s;
  rw_init(&s, 2, 11);
  CHECK(rw_turn(&s) == 0 && s.phase == RW_PHASE_SETUP, "a game starts with seat 1 choosing a frame");
  rw_apply(&s, rw_move(RW_MV_FRAME, 0, 0, 0));
  rw_apply(&s, rw_move(RW_MV_FRAME, 1, 0, 0));
  CHECK(s.phase == RW_PHASE_DRAFT && s.round == 1 && rw_turn(&s) == BK_CHANCE, "after both choose, the first pool is drawn");
  CHECK(s.p[0].favour == rw_frames[s.p[0].frame].rating, "favour is the frame's rating");
  CHECK(s.p[0].colour != s.p[1].colour, "two players have different colours");
  /* an open frame to test placement on */
  {
    rw_frame open = { "open", 3, { 0 } };
    uint8_t cell[RW_CELLS] = { 0 };
#define CAN(pane, die) rw_can_place_on(cell, placed, &open, pane, die, 0, false)
    int placed = 0;
    CHECK(!CAN(7, RW_DIE(0, 3)), "the first die cannot go in the middle");
    CHECK(CAN(0, RW_DIE(0, 3)), "the first die can go in a corner");
    cell[0] = RW_DIE(0, 3);
    placed = 1;
    CHECK(!CAN(3, RW_DIE(1, 4)), "a die must touch another");
    CHECK(CAN(6, RW_DIE(1, 4)), "a corner touch counts");
    CHECK(!CAN(1, RW_DIE(0, 5)), "same colour side by side is refused");
    CHECK(!CAN(1, RW_DIE(2, 3)), "same number side by side is refused");
    CHECK(CAN(1, RW_DIE(2, 5)), "a different colour and number fits");
    CONTROL(CAN(1, RW_DIE(2, 5)) && !CAN(1, RW_DIE(0, 5)), "the side rule tells a fit from a clash");
    rw_frame marked = open;
    marked.cell[1] = 3; /* amber only */
    marked.cell[5] = 14; /* a 4 only */
    CHECK(!rw_can_place_on(cell, placed, &marked, 1, RW_DIE(1, 5), 0, false) && rw_can_place_on(cell, placed, &marked, 1, RW_DIE(2, 5), 0, false), "a colour mark");
    CHECK(!rw_can_place_on(cell, placed, &marked, 5, RW_DIE(1, 5), 0, false) && rw_can_place_on(cell, placed, &marked, 5, RW_DIE(1, 4), 0, false), "a number mark");
    CHECK(rw_can_place_on(cell, placed, &marked, 1, RW_DIE(1, 5), 1, false), "ignoring colour marks");
#undef CAN
  }
  /* goals on a built window */
  uint8_t w[RW_CELLS] = { 0 };
  for (int c = 0; c < RW_COLS; c++) w[c] = RW_DIE(c, c + 1); /* row 0: five colours, 1..5 */
  CHECK(rw_goal_points(RW_G_ROW_COLOURS, w) == 6, "a row of five colours scores 6, got %d", rw_goal_points(RW_G_ROW_COLOURS, w));
  CHECK(rw_goal_points(RW_G_ROW_VALUES, w) == 5, "a row of five numbers scores 5");
  CHECK(rw_goal_points(RW_G_LOW, w) == 2 && rw_goal_points(RW_G_MID, w) == 2 && rw_goal_points(RW_G_HIGH, w) == 0, "pairs");
  CHECK(rw_goal_points(RW_G_PALETTE, w) == 4, "one palette set");
  CHECK(rw_goal_points(RW_G_SCALE, w) == 0, "no 6, no scale");
  w[5] = RW_DIE(1, 6); /* row 1, column 0: touches row 0 column 1 (colour 1) at a corner */
  CHECK(rw_goal_points(RW_G_SCALE, w) == 5, "a 6 completes a scale");
  CHECK(rw_goal_points(RW_G_CHAINS, w) == 2, "corner chain of two, got %d", rw_goal_points(RW_G_CHAINS, w));
  uint8_t bad[RW_CELLS];
  memcpy(bad, w, sizeof bad);
  bad[4] = RW_DIE(0, 5); /* two ruby in row 0 */
  CONTROL(rw_goal_points(RW_G_ROW_COLOURS, bad) == 0, "a repeated colour spoils a spectrum row");
  for (int r = 0; r < RW_ROWS; r++) w[r * RW_COLS + 4] = RW_DIE(r, r + 1);
  CHECK(rw_goal_points(RW_G_COL_COLOURS, w) == 5 && rw_goal_points(RW_G_COL_VALUES, w) == 4, "a column of four colours and numbers");
  /* a four-seat pool holds nine dice once rolled (a mask too narrow for nine slots once drew eight) */
  for (int players = 1; players <= 4; players++) {
    rw_state f;
    rw_init(&f, players, 5);
    for (int i = 0; i < players; i++) rw_apply(&f, rw_move(RW_MV_FRAME, 0, 0, 0));
    bk_game gg;
    rw_game(&gg, players);
    cw_rng cr;
    cw_rng_seed(&cr, 9);
    bk_settle(&gg, &f, &cr, NULL);
    int dice = 0;
    for (int i = 0; i < RW_POOL_MAX; i++) dice += f.pool[i] != 0;
    CHECK(dice == (players == 1 ? 3 : 2 * players + 1), "%d seats: a pool of %d dice", players, dice);
  }
  /* census control */
  rw_state c = s;
  c.bag[2]--;
  CONTROL(!census_ok(&c), "a lost die breaks the census");
}

typedef bk_move (*chooser)(const rw_state *s, int seat, void *user);

static bk_move choose_random(const rw_state *s, int seat, void *user) {
  (void)seat;
  cw_rng *r = user;
  bk_move mv[BK_MAX_MOVES];
  int n = rw_moves(s, mv, BK_MAX_MOVES);
  /* pass rarely: a random player that passes half its turns plays no game */
  for (int k = 0; k < 4; k++) {
    bk_move m = mv[cw_rng_int(r, 0, n - 1)];
    if (rw_mv_kind(m) != RW_MV_PASS) return m;
  }
  return mv[cw_rng_int(r, 0, n - 1)];
}

static int max_moves_seen;
static int tool_uses[RW_TOOL_KINDS];

static unsigned play(int players, uint64_t seed, chooser choose[BK_MAX_SEATS], void *user[BK_MAX_SEATS], uint64_t *hash_out, int *scores,
                     int *track) {
  rw_state s;
  rw_init(&s, players, seed);
  bk_game g;
  rw_game(&g, players);
  cw_rng chance;
  cw_rng_seed(&chance, seed * 31 + 7);
  int steps = 0;
  while (!rw_terminal(&s)) {
    if (rw_turn(&s) == BK_CHANCE) {
      bk_settle(&g, &s, &chance, NULL);
      const char *why = invariants(&s);
      if (why) { CHECK(0, "after chance: %s", why); break; }
      continue;
    }
    int seat = rw_turn(&s);
    bk_move mv[BK_MAX_MOVES];
    int n = rw_moves(&s, mv, BK_MAX_MOVES);
    if (n > max_moves_seen) max_moves_seen = n;
    bk_move m = choose[seat](&s, seat, user[seat]);
    if (!bk_is_legal(&g, &s, m)) { CHECK(0, "seat %d chose an illegal move %llx", seat, (unsigned long long)m); break; }
    if (rw_mv_kind(m) == RW_MV_TOOL) tool_uses[s.tools[rw_mv_a(m)]]++;
    rw_apply(&s, m);
    const char *why = invariants(&s);
    if (why) { CHECK(0, "players %d seed %llu step %d: %s", players, (unsigned long long)seed, steps, why); break; }
    if (++steps > 4000) { CHECK(0, "game did not end"); break; }
  }
  CHECK(s.round == RW_ROUNDS, "the game ended in round %d", s.round);
  if (hash_out) *hash_out = rw_hash(&s);
  if (scores) for (int i = 0; i < players; i++) scores[i] = s.p[i].score;
  if (track) *track = s.track_sum;
  unsigned w = rw_winners(&s);
  if (players > 1) CHECK(w != 0, "no winner");
  return w;
}

static void test_random_games(void) {
  for (int players = 1; players <= 4; players++)
    for (uint64_t seed = 1; seed <= 150; seed++) {
      cw_rng r;
      chooser ch[4] = { choose_random, choose_random, choose_random, choose_random };
      void *u[4] = { &r, &r, &r, &r };
      uint64_t h1, h2;
      cw_rng_seed(&r, seed * 77);
      play(players, seed, ch, u, &h1, NULL, NULL);
      cw_rng_seed(&r, seed * 77);
      play(players, seed, ch, u, &h2, NULL, NULL);
      CHECK(h1 == h2, "replay of players %d seed %llu differs", players, (unsigned long long)seed);
    }
  int unused = 0;
  for (int t = 0; t < RW_TOOL_KINDS; t++) unused += tool_uses[t] == 0;
  CHECK(unused == 0, "%d tools never used in random play", unused);
  fprintf(stderr, "  random play: at most %d legal moves at one decision\n", max_moves_seen);
  cw_rng r;
  chooser ch[4] = { choose_random, choose_random, choose_random, choose_random };
  void *u[4] = { &r, &r, &r, &r };
  uint64_t a, b;
  cw_rng_seed(&r, 1); play(2, 1, ch, u, &a, NULL, NULL);
  cw_rng_seed(&r, 1); play(2, 2, ch, u, &b, NULL, NULL);
  CONTROL(a != b, "another seed gives another game");
}

typedef struct cpu_seat { rw_cpu *cpu; } cpu_seat;
static bk_move choose_cpu(const rw_state *s, int seat, void *user) {
  (void)seat;
  return rw_cpu_choose(((cpu_seat *)user)->cpu, s);
}

static unsigned cpu_game(int players, uint64_t seed, const int *levels, int *scores, int *track) {
  rw_cpu *cpus[4];
  cpu_seat seats[4];
  chooser ch[4];
  void *u[4];
  const char *rw = getenv("RW_REWARD"), *rs = getenv("RW_RSCALE");
  for (int i = 0; i < players; i++) {
    cpus[i] = rw_cpu_new(players, levels[i], seed * 131 + (uint64_t)i);
    if (rw || rs)
      rw_cpu_set_reward(cpus[i], rw && !strcmp(rw, "place") ? CW_SEARCH_REWARD_PLACE : CW_SEARCH_REWARD_MARGIN, rs ? (float)atof(rs) : 0);
    seats[i].cpu = cpus[i];
    ch[i] = choose_cpu;
    u[i] = &seats[i];
  }
  uint64_t h;
  unsigned w = play(players, seed, ch, u, &h, scores, track);
  for (int i = 0; i < players; i++) rw_cpu_free(cpus[i]);
  return w;
}

static void test_cpu_games(void) {
  for (int players = 1; players <= 4; players++)
    for (int level = 0; level <= 2; level++) {
      int levels[4] = { level, level, level, level };
      int sc[4] = { 0 }, track = 0;
      unsigned w = cpu_game(players, 100 * (uint64_t)players + (uint64_t)level, levels, sc, &track);
      if (players > 1) CHECK(w != 0 && w < (1u << players), "cpu game winners mask %x", w);
      fprintf(stderr, "  cpu %d seats level %d: scores %d %d %d %d%s, winners %x\n", players, level, sc[0], sc[1], sc[2], sc[3],
              players == 1 ? " (solo)" : "", w);
      if (players == 1) fprintf(stderr, "    solo target (round track) %d\n", track);
    }
}

static float duel(int a, int b, int n, float *avg_a, float *avg_b) {
  float wins = 0;
  int sa = 0, sb = 0;
  for (int i = 0; i < n; i++)
    for (int swap = 0; swap < 2; swap++) {
      int levels[2] = { swap ? b : a, swap ? a : b };
      int sc[4];
      unsigned w = cpu_game(2, 5000 + (uint64_t)i, levels, sc, NULL);
      int aseat = swap ? 1 : 0;
      sa += sc[aseat];
      sb += sc[1 - aseat];
      if (w == (1u << aseat)) wins += 1;
      else if (w & (1u << aseat)) wins += 0.5f;
    }
  *avg_a = (float)sa / (float)(2 * n);
  *avg_b = (float)sb / (float)(2 * n);
  return wins / (float)(2 * n);
}

static int bench(int players, uint64_t seed0, int n) {
  static const int L2[6][2] = { { 2, 1 }, { 1, 2 }, { 2, 0 }, { 0, 2 }, { 1, 0 }, { 0, 1 } };
  static const int L3[3] = { 0, 1, 2 }, L4[4] = { 2, 1, 1, 0 };
  int layouts = players == 2 ? 6 : players;
  if (players < 2 || players > 4) CW_FATAL("bench: players %d (2..4)", players);
  for (int k = 0; k < n; k++)
    for (int r = 0; r < layouts; r++) {
      int lv[4], sc[4] = { 0 };
      for (int i = 0; i < players; i++) lv[i] = players == 2 ? L2[r][i] : players == 3 ? L3[(i + r) % 3] : L4[(i + r) % 4];
      uint64_t seed = seed0 + (uint64_t)k;
      unsigned w = cpu_game(players, seed, lv, sc, NULL);
      printf("game %d %llu", players, (unsigned long long)seed);
      for (int i = 0; i < players; i++) printf(" %d:%d", lv[i], sc[i]);
      printf(" w%u\n", w);
      fflush(stdout);
    }
  return failures ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc > 4 && !strcmp(argv[1], "bench")) return bench(atoi(argv[2]), strtoull(argv[3], NULL, 10), atoi(argv[4]));
  test_frames();
  test_units();
  test_random_games();
  test_cpu_games();
  if (argc > 2 && !strcmp(argv[1], "strength")) {
    int n = atoi(argv[2]);
    float s1[6];
    float ne = duel(1, 0, n, &s1[0], &s1[1]);
    printf("strength over %d seeds x2 seats (win rate, mean scores):\n  Normal vs Easy %.2f (%.1f vs %.1f)\n", n, ne, s1[0], s1[1]);
    fflush(stdout);
    float he = duel(2, 0, n, &s1[2], &s1[3]);
    printf("  Hard vs Easy %.2f (%.1f vs %.1f)\n", he, s1[2], s1[3]);
    fflush(stdout);
    float hn = duel(2, 1, n, &s1[4], &s1[5]);
    printf("  Hard vs Normal %.2f (%.1f vs %.1f)\n", hn, s1[4], s1[5]);
    CHECK(ne > 0.55f && he > 0.65f && hn > 0.52f, "levels are not ordered");
    for (int lv = 0; lv <= 2; lv++) {
      int won = 0, tot = 0, tgt = 0;
      for (int i = 0; i < n; i++) {
        int levels[1] = { lv }, sc[4], track;
        won += cpu_game(1, 9000 + (uint64_t)i, levels, sc, &track) & 1u;
        tot += sc[0], tgt += track;
      }
      printf("  solo %s: won %d of %d (mean score %.1f against a mean target %.1f)\n", lv == 0 ? "Easy" : lv == 1 ? "Normal" : "Hard", won, n,
             (float)tot / n, (float)tgt / n);
    }
    float ee = duel(1, 1, n, &s1[0], &s1[1]);
    printf("  Normal vs Normal %.2f (the control: one level against itself is even)\n", ee);
    CONTROL(!(ee > 0.55f), "the ordering check fails a level against itself");
  }
  if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
  printf("rosewindow rules: ok\n");
  return 0;
}
