/* Smalti rules tests: scoring units, tile conservation, legality, determinism, random and CPU full games at
 * 2, 3 and 4 seats, and CPU level strength. Every check has a control that must fail.
 *
 *   smalti_rules_test            all tests
 *   smalti_rules_test strength N also the Easy/Normal/Hard win rates over N seeded two-seat games per pairing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"
#include "rules.h"
#include "cpu.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
/* a control: a check fed a broken input must catch it (prints "CONTROL <name> caught" for tests/smalti-rules.mjs) */
#define CONTROL(caught, name) do { if (caught) printf("CONTROL %s caught\n", name); else { failures++; fprintf(stderr, "FAIL control %s was not caught\n", name); } } while (0)

static bool census_ok(const ms_state *s) {
  int n[MS_COLOURS];
  ms_tile_census(s, n);
  for (int c = 0; c < MS_COLOURS; c++)
    if (n[c] != MS_TILES_PER_COLOUR) return false;
  return true;
}

/* rules invariants a state must hold; returns a reason or NULL */
static const char *invariants(const ms_state *s) {
  if (!census_ok(s)) return "tile census is not 20 per colour";
  int markers = s->centre_marker;
  for (int i = 0; i < s->players; i++) {
    const ms_player *pl = &s->p[i];
    markers += pl->floor_marker;
    if (pl->floor_tiles + pl->floor_marker > MS_FLOOR_SLOTS) return "breakage strip over seven";
    if (pl->score < 0) return "negative score";
    for (int r = 0; r < MS_SIZE; r++) {
      if (pl->line_count[r] > r + 1) return "staging row over capacity";
      if (pl->line_count[r] && ms_wall_has(pl, r, ms_wall_col(r, pl->line_colour[r]))) return "staging row colour already on the mosaic";
    }
  }
  if (s->phase == MS_PHASE_DRAFT && markers != 1) return "first-player marker count is not one";
  return NULL;
}

static void test_units(void) {
  /* the layout is a Latin square: every row and every column holds each colour once */
  for (int r = 0; r < MS_SIZE; r++) {
    int seen_row = 0, seen_col = 0;
    for (int k = 0; k < MS_SIZE; k++) {
      seen_row |= 1 << ms_wall_colour(r, k);
      seen_col |= 1 << ms_wall_colour(k, r);
      CHECK(ms_wall_colour(r, ms_wall_col(r, k)) == k, "layout round trip row %d colour %d", r, k);
    }
    CHECK(seen_row == 31 && seen_col == 31, "layout row/col %d is not a Latin square", r);
  }
  /* placement scoring */
  CHECK(ms_place_points(0, 2, 2) == 1, "a lone tile scores 1");
  uint32_t w = (1u << (2 * 5 + 1)) | (1u << (2 * 5 + 3)); /* row 2: cols 1 and 3 */
  CHECK(ms_place_points(w, 2, 2) == 3, "joining a run of 3 scores 3, got %d", ms_place_points(w, 2, 2));
  w |= (1u << (1 * 5 + 2)) | (1u << (0 * 5 + 2));          /* col 2: rows 0 and 1 */
  CHECK(ms_place_points(w, 2, 2) == 6, "a cross of 3 and 3 scores 6, got %d", ms_place_points(w, 2, 2));
  CHECK(ms_place_points(w, 4, 4) == 1, "far corner scores 1");
  /* control: a wall that is NOT adjacent must not score the cross */
  CONTROL(ms_place_points(1u << 24, 2, 2) != 6, "isolated tile is not a cross");
  /* floor */
  CHECK(ms_floor_points(0) == 0 && ms_floor_points(1) == -1 && ms_floor_points(3) == -4 && ms_floor_points(7) == -14 &&
            ms_floor_points(9) == -14, "floor penalties");
  /* end bonus */
  int rows, cols, sets;
  CHECK(ms_end_bonus(0x1ffffffu, &rows, &cols, &sets) == 5 * 2 + 5 * 7 + 5 * 10 && rows == 5 && cols == 5 && sets == 5, "full wall bonus");
  CHECK(ms_end_bonus(0x1fu, &rows, &cols, &sets) == 2 && rows == 1 && cols == 0 && sets == 0, "one row bonus");
  uint32_t col0 = 0, set0 = 0;
  for (int r = 0; r < 5; r++) { col0 |= 1u << (r * 5); set0 |= 1u << (r * 5 + ms_wall_col(r, 0)); }
  CHECK(ms_end_bonus(col0, NULL, NULL, NULL) == 7, "one column bonus");
  CHECK(ms_end_bonus(set0, NULL, NULL, NULL) == 10, "one colour set bonus");
}

static void test_take_rules(void) {
  ms_state s;
  ms_init(&s, 2, 7);
  CHECK(ms_turn(&s) == BK_CHANCE, "a new game starts at the bag draw");
  ms_chance(&s, &s.rng);
  CHECK(s.phase == MS_PHASE_DRAFT && s.trays == 5, "draft after the draw, five trays for two");
  int tiles = 0;
  for (int t = 0; t < s.trays; t++) for (int c = 0; c < 5; c++) tiles += s.tray[t][c];
  CHECK(tiles == 20, "five trays of four");
  CHECK(invariants(&s) == NULL, "fresh draft: %s", invariants(&s));
  /* take a colour from tray 0: the rest goes to the centre */
  int c0 = -1;
  for (int c = 0; c < 5; c++) if (s.tray[0][c]) { c0 = c; break; }
  int took = s.tray[0][c0], rest = 4 - took;
  ms_apply(&s, ms_move(0, c0, 4));
  int centre = 0;
  for (int c = 0; c < 5; c++) centre += s.centre[c];
  CHECK(centre == rest, "the rest of the tray goes to the centre (%d vs %d)", centre, rest);
  CHECK(s.p[0].line_count[4] == took && s.p[0].line_colour[4] == c0, "tiles land on the staging row");
  CHECK(s.seat == 1, "turn passes");
  /* seat 1 takes from the centre first: gets the marker */
  if (centre) {
    int cc = -1;
    for (int c = 0; c < 5; c++) if (s.centre[c]) { cc = c; break; }
    ms_apply(&s, ms_move(MS_CENTRE, cc, MS_FLOOR));
    CHECK(s.p[1].floor_marker == 1 && s.centre_marker == 0 && s.first_next == 1, "first from the centre takes the marker");
    CHECK(invariants(&s) == NULL, "after centre take: %s", invariants(&s));
  }
  /* a staging row with another colour does not accept a different colour */
  bk_move mv[BK_MAX_MOVES];
  int n = ms_moves(&s, mv, BK_MAX_MOVES);
  for (int i = 0; i < n; i++) {
    int line = ms_move_line(mv[i]);
    if (line == MS_FLOOR) continue;
    const ms_player *pl = &s.p[s.seat];
    if (pl->line_count[line]) CHECK(pl->line_colour[line] == ms_move_colour(mv[i]), "move onto a row of another colour is listed");
  }
  /* control: the invariant check catches a lost tile */
  ms_state bad = s;
  bad.bag[0]--;
  CONTROL(invariants(&bad) != NULL, "lost tile breaks the census");
}

typedef bk_move (*chooser)(const ms_state *s, int seat, void *user);

static bk_move choose_random(const ms_state *s, int seat, void *user) {
  (void)seat;
  cw_rng *r = user;
  bk_move mv[BK_MAX_MOVES];
  int n = ms_moves(s, mv, BK_MAX_MOVES);
  return mv[cw_rng_int(r, 0, n - 1)];
}

/* play one game; returns the winners mask, fills rounds; checks invariants at every step */
static unsigned play(int players, uint64_t seed, chooser choose[BK_MAX_SEATS], void *user[BK_MAX_SEATS], int *rounds,
                     uint64_t *hash_out, int *scores) {
  ms_state s;
  ms_init(&s, players, seed);
  bk_game g;
  ms_game(&g, players);
  int steps = 0;
  while (!ms_terminal(&s)) {
    if (ms_turn(&s) == BK_CHANCE) {
      bk_settle(&g, &s, &s.rng, NULL);
      continue;
    }
    int seat = ms_turn(&s);
    bk_move m = choose[seat](&s, seat, user[seat]);
    if (!bk_is_legal(&g, &s, m)) { CHECK(0, "seat %d chose an illegal move %llx", seat, (unsigned long long)m); break; }
    ms_state stepwise = s;
    ms_take(&stepwise, m);
    if (ms_draft_empty(&stepwise)) { /* the view shows the round end in steps: they must land where ms_apply does */
      for (int i = 0; i < stepwise.players; i++) {
        for (int r = 0; r < MS_SIZE; r++) ms_tile_row(&stepwise, i, r);
        ms_clear_floor(&stepwise, i);
      }
      ms_round_finish(&stepwise);
    }
    ms_apply(&s, m);
    if (ms_hash(&stepwise) != ms_hash(&s)) { CHECK(0, "the stepwise round end differs from ms_apply"); break; }
    const char *why = invariants(&s);
    if (why) { CHECK(0, "players %d seed %llu step %d: %s", players, (unsigned long long)seed, steps, why); break; }
    if (++steps > 5000) { CHECK(0, "game did not end"); break; }
  }
  if (rounds) *rounds = s.round;
  if (hash_out) *hash_out = ms_hash(&s);
  if (scores) for (int i = 0; i < players; i++) scores[i] = s.p[i].score;
  CHECK(!s.ended_by_cap, "players %d seed %llu ended by the round cap", players, (unsigned long long)seed);
  unsigned w = ms_winners(&s);
  CHECK(w != 0, "no winner");
  return w;
}

static void test_random_games(void) {
  for (int players = 2; players <= 4; players++) {
    int capped = 0;
    for (uint64_t seed = 1; seed <= 200; seed++) {
      cw_rng r;
      cw_rng_seed(&r, seed * 77);
      chooser ch[4] = { choose_random, choose_random, choose_random, choose_random };
      void *u[4] = { &r, &r, &r, &r };
      int rounds;
      uint64_t h1, h2;
      int before = failures;
      play(players, seed, ch, u, &rounds, &h1, NULL);
      if (failures != before) capped++;
      cw_rng_seed(&r, seed * 77);
      play(players, seed, ch, u, NULL, &h2, NULL);
      CHECK(h1 == h2, "replay of players %d seed %llu differs", players, (unsigned long long)seed);
    }
    if (capped) fprintf(stderr, "  random play, %d seats: %d games failed\n", players, capped);
  }
  /* control: a different seed must give a different final state */
  cw_rng r;
  chooser ch[4] = { choose_random, choose_random, choose_random, choose_random };
  void *u[4] = { &r, &r, &r, &r };
  uint64_t a, b;
  cw_rng_seed(&r, 1); play(2, 1, ch, u, NULL, &a, NULL);
  cw_rng_seed(&r, 1); play(2, 2, ch, u, NULL, &b, NULL);
  CONTROL(a != b, "another seed gives another game");
}

typedef struct cpu_seat { ms_cpu *cpu; } cpu_seat;
static bk_move choose_cpu(const ms_state *s, int seat, void *user) {
  (void)seat;
  return ms_cpu_choose(((cpu_seat *)user)->cpu, s);
}

/* levels[seat]; returns winners mask */
static unsigned cpu_game(int players, uint64_t seed, const int *levels, int *rounds, int *scores) {
  ms_cpu *cpus[4];
  cpu_seat seats[4];
  chooser ch[4];
  void *u[4];
  for (int i = 0; i < players; i++) {
    cpus[i] = ms_cpu_new(players, levels[i], seed * 131 + (uint64_t)i);
    seats[i].cpu = cpus[i];
    ch[i] = choose_cpu;
    u[i] = &seats[i];
  }
  uint64_t h;
  unsigned w = play(players, seed, ch, u, rounds, &h, scores);
  for (int i = 0; i < players; i++) ms_cpu_free(cpus[i]);
  return w;
}

static void test_cpu_games(void) {
  for (int players = 2; players <= 4; players++)
    for (int level = 0; level <= 2; level++) {
      int levels[4] = { level, level, level, level };
      for (uint64_t seed = 1; seed <= 2; seed++) {
        int rounds = 0;
        unsigned w = cpu_game(players, seed + 100 * (uint64_t)players, levels, &rounds, NULL);
        CHECK(w != 0 && w < (1u << players), "cpu game winners mask %x", w);
        CHECK(rounds >= 5 && rounds <= 12, "cpu game %d seats level %d took %d rounds", players, level, rounds);
        fprintf(stderr, "  cpu %d seats level %d seed %llu: %d rounds, winners %x\n", players, level, (unsigned long long)seed, rounds, w);
      }
    }
}

/* two-seat strength: a vs b over n seeds, each seed played twice with seats swapped; returns a's wins (ties half) */
static float duel(int a, int b, int n, float *avg_a, float *avg_b) {
  float wins = 0;
  int sa = 0, sb = 0;
  for (int i = 0; i < n; i++)
    for (int swap = 0; swap < 2; swap++) {
      int levels[2] = { swap ? b : a, swap ? a : b };
      int sc[4];
      unsigned w = cpu_game(2, 5000 + (uint64_t)i, levels, NULL, sc);
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

/* three and four seats: one seat at level a among seats at level b, the a seat moved round every place, over n
 * seeds; returns a's share of the wins (ties split), to compare with an even share (1 / players) */
static float crowd(int a, int b, int players, int n, float *avg_a, float *avg_b) {
  float wins = 0;
  int sa = 0, sb = 0, games = 0;
  for (int i = 0; i < n; i++)
    for (int at = 0; at < players; at++) {
      int levels[4];
      for (int k = 0; k < players; k++) levels[k] = k == at ? a : b;
      int sc[4];
      unsigned w = cpu_game(players, 7000 + (uint64_t)i * 4 + (uint64_t)at, levels, NULL, sc);
      int nw = __builtin_popcount(w);
      if (w & (1u << at)) wins += 1.0f / (float)nw;
      sa += sc[at];
      for (int k = 0; k < players; k++) if (k != at) sb += sc[k];
      games++;
    }
  *avg_a = (float)sa / (float)games;
  *avg_b = (float)sb / (float)(games * (players - 1));
  return wins / (float)games;
}

int main(int argc, char **argv) {
  test_units();
  test_take_rules();
  test_random_games();
  test_cpu_games();
  if (argc > 2 && !strcmp(argv[1], "strength")) {
    int n = atoi(argv[2]);
    float s1[6];
    float ne = duel(1, 0, n, &s1[0], &s1[1]), he = duel(2, 0, n, &s1[2], &s1[3]), hn = duel(2, 1, n, &s1[4], &s1[5]);
    printf("strength over %d seeds x2 seats (win rate, mean scores):\n  Normal vs Easy %.2f (%.1f vs %.1f)\n  Hard vs Easy %.2f (%.1f vs %.1f)\n  Hard vs Normal %.2f (%.1f vs %.1f)\n", n, ne, s1[0], s1[1], he, s1[2], s1[3], hn, s1[4], s1[5]);
    CHECK(ne > 0.55f && he > 0.65f && hn > 0.52f, "levels are not ordered");
    float ee = duel(1, 1, n, &s1[0], &s1[1]);
    printf("  Normal vs Normal %.2f (the control: one level against itself is even)\n", ee);
    CONTROL(!(ee > 0.55f), "the ordering check fails a level against itself");
    /* three and four seats: the stronger level among weaker ones takes clearly more than an even share */
    for (int players = 3; players <= 4; players++) {
      float even = 1.0f / (float)players, m[6];
      float hn3 = crowd(2, 1, players, n / 2 > 0 ? n / 2 : 1, &m[0], &m[1]), ne3 = crowd(1, 0, players, n / 2 > 0 ? n / 2 : 1, &m[2], &m[3]);
      float nn3 = crowd(1, 1, players, n / 2 > 0 ? n / 2 : 1, &m[4], &m[5]);
      printf("  %d seats (even share %.2f): Hard among Normals %.2f (%.1f vs %.1f), Normal among Easies %.2f (%.1f vs %.1f), "
             "Normal among Normals %.2f (the control)\n", players, even, hn3, m[0], m[1], ne3, m[2], m[3], nn3);
      CHECK(hn3 > even * 1.25f && ne3 > even * 1.25f, "%d seats: the levels are not ordered", players);
      CONTROL(!(nn3 > even * 1.25f), players == 3 ? "the three-seat ordering check fails a level among its equals" : "the four-seat ordering check fails a level among its equals");
    }
  }
  if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
  printf("smalti rules: ok\n");
  return 0;
}
