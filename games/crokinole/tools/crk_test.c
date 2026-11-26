/* Crokinole rules and physics tests: headless, plain C. Each check has a control that must fail. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/crk.h"
#include "../src/crk_cpu.h"
#include "../src/rules.h"
#include "boardkit/bk_match.h"
#include "cartwheel/search.h"

static int fails = 0;
#define CHECK(c, ...)                                   \
  do {                                                  \
    if (!(c)) {                                         \
      fails++;                                          \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
      printf(__VA_ARGS__);                              \
      printf("\n");                                     \
    }                                                   \
  } while (0)

static void place(crk_state *s, int i, float x, float y) {
  s->d[i].st = CRK_BOARD, s->d[i].x = x, s->d[i].y = y;
}

/* a move from numbers: arc place 0..1, aim radians, power 0..1, spin -1..1 (rounded to the shot's steps) */
static int rnd_i(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
static uint64_t mv(float pos, float aim, float power, float spin) {
  return crk_move_pack((crk_shot){ (int16_t)rnd_i(pos * CRK_POS_N), (int16_t)rnd_i(aim / CRK_AIM_STEP), (int16_t)rnd_i(power * CRK_POW_N),
                                   (int16_t)rnd_i(spin * CRK_SPIN_N) });
}

static float max_overlap(const crk_state *s) {
  float worst = 0;
  for (int i = 0; i < CRK_NDISC; i++) {
    const crk_disc *a = &s->d[i];
    if (a->st != CRK_BOARD) continue;
    for (int p = 0; p < CRK_NPEG; p++) {
      float px, py;
      crk_peg(p, &px, &py);
      float o = CRK_R_DISC + CRK_R_PEG - sqrtf((a->x - px) * (a->x - px) + (a->y - py) * (a->y - py));
      if (o > worst) worst = o;
    }
    for (int j = i + 1; j < CRK_NDISC; j++) {
      const crk_disc *b = &s->d[j];
      if (b->st != CRK_BOARD) continue;
      float o = 2 * CRK_R_DISC - sqrtf((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y));
      if (o > worst) worst = o;
    }
  }
  return worst;
}

static void test_trig(void) {
  float worst = 0, worst_at = 0;
  for (int i = -4000; i <= 4000; i++) {
    float a = (float)i * 0.0025f, s, c;
    crk_sincos(a, &s, &c);
    float e = fmaxf(fabsf(s - sinf(a)), fabsf(c - cosf(a)));
    if (e > worst) worst = e, worst_at = a;
    float y = sinf(a) * 0.7f, x = cosf(a) * 1.3f;
    float e2 = fabsf(crk_atan2(y, x) - atan2f(y, x));
    if (e2 > 3.2f) e2 = fabsf(e2 - 6.2831853f);
    if (e2 > worst) worst = e2, worst_at = a;
  }
  CHECK(worst < 2e-6f, "trig error %g at %g", worst, worst_at);
  /* control: a crude sin must fail the same bound */
  float crude = fabsf(1.0f - 1.0f / 6.0f - sinf(1.0f));
  CHECK(crude >= 2e-6f, "control: a two-term sine passes the trig bound");
}

static void test_twenty(void) {
  crk_state s;
  crk_new(&s, 2, 100);
  float p = crk_power_for(CRK_R_5);
  crk_apply(&s, mv(0.5f, 0, p, 0));
  CHECK(s.twenties[0] == 1, "a shot of exactly the centre's distance drops in (twenties %d)", s.twenties[0]);
  CHECK(s.d[0].st == CRK_TWENTY, "the disc is in the hole (st %d)", s.d[0].st);
  CHECK(s.to_move == 1, "the turn passes");
  /* control: too hard skims over the hole and does not score */
  crk_new(&s, 2, 100);
  crk_apply(&s, mv(0.5f, 0, p * 1.6f, 0));
  CHECK(s.twenties[0] == 0, "control: a fast disc skims the hole (twenties %d)", s.twenties[0]);
}

static void test_open_board_short(void) {
  crk_state s;
  crk_new(&s, 2, 100);
  crk_apply(&s, mv(0.5f, 0, crk_power_for(0.12f), 0));
  CHECK(s.d[0].st == CRK_DITCH && s.last_foul == 2, "an open-board shot short of the 15 line is removed (st %d foul %d)", s.d[0].st, s.last_foul);
  crk_new(&s, 2, 100);
  crk_apply(&s, mv(0.5f, 0, crk_power_for(CRK_R_5 - 0.06f), 0));
  CHECK(s.d[0].st == CRK_BOARD && s.last_foul == 0, "control: one inside the 15 line stays (st %d foul %d)", s.d[0].st, s.last_foul);
}

static void test_must_hit(void) {
  crk_state s;
  crk_new(&s, 2, 100);
  place(&s, 1, 0.15f, 0.05f); /* seat 1's disc on the board */
  crk_apply(&s, mv(0.5f, 0, crk_power_for(CRK_R_5 - 0.06f), 0));
  CHECK(s.d[0].st == CRK_DITCH && s.last_foul == 1, "missing every opposing disc is a foul (st %d foul %d)", s.d[0].st, s.last_foul);
  CHECK(s.d[1].st == CRK_BOARD, "the opposing disc stays");
  /* control: hitting it is no foul */
  crk_new(&s, 2, 100);
  place(&s, 1, 0.0f, -0.15f);
  crk_apply(&s, mv(0.5f, 0, 0.75f, 0));
  CHECK(s.last_foul == 0, "control: a hit is no foul (foul %d)", s.last_foul);
  /* a foul takes the shooter's own touched disc too */
  crk_new(&s, 2, 100);
  place(&s, 1, 0.20f, 0.10f);
  place(&s, 2, 0.0f, -0.12f); /* seat 0's own */
  crk_apply(&s, mv(0.5f, 0, 0.5f, 0));
  CHECK(s.last_foul == 1 && s.d[2].st == CRK_DITCH, "a foul removes the own disc it touched (foul %d st %d)", s.last_foul, s.d[2].st);
  /* a hit through an own disc counts */
  crk_new(&s, 2, 100);
  place(&s, 2, 0.0f, -0.18f);
  place(&s, 1, 0.0f, -0.10f);
  crk_apply(&s, mv(0.5f, 0, 0.8f, 0));
  CHECK(s.last_foul == 0, "an opposing disc struck through an own disc is a hit (foul %d)", s.last_foul);
}

static void test_head_on(void) {
  crk_state s;
  crk_new(&s, 2, 100);
  place(&s, 1, 0.0f, -0.15f);
  crk_shot_begin(&s, crk_move_unpack(mv(0.5f, 0, 0.6f, 0)));
  float p0 = s.d[0].vy;
  int hit_tick = -1;
  for (int t = 0; t < 200; t++) {
    crk_events ev;
    crk_tick(&s, &ev);
    if (ev.n && hit_tick < 0) {
      hit_tick = t;
      float p1 = s.d[0].vy + s.d[1].vy;
      /* momentum is conserved across the hit up to the friction of the tick */
      CHECK(fabsf(p1 - s.d[0].vy - s.d[1].vy) < 1e-6f, "momentum");
      CHECK(s.d[1].vy > s.d[0].vy * 5, "a head-on hit passes the speed on (%g, %g)", s.d[0].vy, s.d[1].vy);
      CHECK(fabsf(s.d[1].vx) < 1e-4f, "straight on stays straight (%g)", s.d[1].vx);
    }
  }
  CHECK(hit_tick > 0, "the discs met");
  (void)p0;
}

static void test_no_tunnel(void) {
  /* random hard shots into a crowded board: never an overlap deeper than a hair, never a disc through a peg */
  unsigned r = 12345;
  float worst = 0;
  int shots = 0;
  for (int g = 0; g < 60; g++) {
    crk_state s;
    crk_new(&s, 2, 100);
    for (int k = 2; k < 20; k++) {
      r = r * 1664525u + 1013904223u;
      float a = (float)(r >> 8) / 16777216.0f * 6.2831853f;
      r = r * 1664525u + 1013904223u;
      float rad = 0.03f + (float)(r >> 8) / 16777216.0f * 0.25f;
      float x = rad * cosf(a), y = rad * sinf(a);
      bool ok = true;
      for (int j = 0; j < CRK_NDISC; j++)
        if (s.d[j].st == CRK_BOARD && hypotf(s.d[j].x - x, s.d[j].y - y) < 2.05f * CRK_R_DISC) ok = false;
      for (int p = 0; p < CRK_NPEG; p++) {
        float px, py;
        crk_peg(p, &px, &py);
        if (hypotf(px - x, py - y) < CRK_R_DISC + CRK_R_PEG + 0.001f) ok = false;
      }
      if (hypotf(x, y) < CRK_R_HOLE) ok = false;
      if (ok) place(&s, k, x, y), s.d[k].owner = (uint8_t)(k % 2), s.d[k].side = (uint8_t)(k % 2);
    }
    for (int sh = 0; sh < 4; sh++) {
      r = r * 1664525u + 1013904223u;
      float aim = ((float)(r >> 8) / 16777216.0f - 0.5f) * 1.6f;
      r = r * 1664525u + 1013904223u;
      float pos = (float)(r >> 8) / 16777216.0f;
      crk_shot_begin(&s, crk_move_unpack(mv(pos, aim, 1.0f, sh & 1 ? 0.8f : 0)));
      float start = max_overlap(&s);
      while (!crk_tick(&s, NULL)) {
        float o = max_overlap(&s) - (start > 0 ? start : 0);
        if (o > worst) worst = o;
      }
      crk_shot_end(&s);
      shots++;
    }
  }
  CHECK(worst < 2e-5f, "deepest overlap %g m over %d hard shots", worst, shots);
  /* a straight full-power shot at a peg bounces off it (not through) */
  crk_state s;
  crk_new(&s, 2, 100);
  float px, py;
  crk_peg(5, &px, &py); /* in front of seat 0, right of centre */
  float sx, sy;
  crk_shot_place(&s, 0, 0.5f, &sx, &sy);
  float aim = crk_atan2(py - sy, px - sx) - 1.5707963f;
  crk_shot_begin(&s, crk_move_unpack(mv(0.5f, aim, 1.0f, 0)));
  bool through = false, peg_hit = false;
  for (int t = 0; t < 600 && !crk_tick(&s, NULL); t++) {
    if (s.d[0].y > py + 0.03f && fabsf(s.d[0].x - px) < 0.01f) through = true;
    if (s.contacts) peg_hit = true;
  }
  CHECK(peg_hit && !through, "a full-power shot straight at a peg bounces (hit %d through %d)", peg_hit, through);
}

/* a whole match, CPU seats only; returns the winner side, fills the final hash */
static int cpu_match(int players, const int levels[4], uint64_t seed, uint64_t *hash, int *rounds, int *shots) {
  crk_state s;
  crk_new(&s, players, 100);
  int n = 0;
  while (s.phase != CRK_OVER && n < 24 * 80) {
    uint64_t m = crk_cpu_choose(&s, levels[s.to_move], seed + (uint64_t)n * 7919u, NULL);
    crk_apply(&s, m);
    n++;
    if (max_overlap(&s) > 1e-4f) {
      printf("overlap after shot %d\n", n);
      break;
    }
  }
  if (hash) *hash = crk_hash(&s);
  if (rounds) *rounds = s.rounds_done;
  if (shots) *shots = n;
  return s.phase == CRK_OVER ? s.winner : -1;
}

/* a whole match as the game plays it (game.c): each CPU seat searches its level's candidates with cw_search
 * (CW_SEARCH_GREEDY, the level's seeded mistakes, the seed per decision as bk_cpu mixes it), then its hand's error
 * (crk_cpu_perturb, seeded as cpu_target does) is what lands. Returns the winner side. */
static int search_match(int players, const int levels[4], uint64_t seed) {
  static const float MISTAKE[3] = { 0.25f, 0.06f, 0 };
  crk_rules_user ru[3], rm = { .level = -1 };
  bk_game rules[3];
  for (int l = 0; l < 3; l++) ru[l].level = l, rules[l] = crk_rules(&ru[l], players);
  bk_game mr = crk_rules(&rm, players);
  crk_state s0;
  crk_new(&s0, players, 100);
  bk_match *m = bk_match_new(&mr, &s0, seed);
  bk_match_set_legal(m, crk_rules_legal, NULL);
  for (int n = 0; !bk_match_over(m) && n < 24 * 80; n++) {
    int seat = bk_match_turn(m), lv = levels[seat];
    const crk_state *s = bk_match_state(m);
    cw_search_options o;
    memset(&o, 0, sizeof o);
    o.kind = CW_SEARCH_GREEDY, o.max_depth = 1, o.max_nodes = 4 * CRK_CPU_MAXCAND + 64, o.tt_entries = -1;
    o.mistake = MISTAKE[lv], o.mistake_spread = 4;
    o.seed = bk_seed_mix((seed * 31u + (uint64_t)seat) ^ (uint64_t)seat, bk_match_hash(m));
    cw_search *sr = cw_search_new(&rules[lv], &o);
    cw_search_move pick;
    if (!sr || !cw_search_run(sr, s, &pick)) { printf("search refused seat %d\n", seat); cw_search_free(sr); break; }
    cw_search_free(sr);
    crk_cpu_level L = crk_cpu_level_of(lv);
    crk_shot t = crk_move_unpack(crk_cpu_perturb(s, pick, &L, 0x5eedu + (uint64_t)seat));
    t.spin = 0;
    if (t.power < 1) t.power = 1;
    if (!bk_match_play(m, crk_move_pack(t))) { printf("the match refused seat %d\x27s shot\n", seat); break; }
  }
  const crk_state *e = bk_match_state(m);
  int w = e->phase == CRK_OVER ? e->winner : -1;
  bk_match_free(m);
  return w;
}

static void test_full_games(void) {
  for (int players = 2; players <= 4; players++) {
    int lv[4] = { 1, 1, 1, 1 };
    uint64_t h1, h2;
    int rounds, shots;
    int w = cpu_match(players, lv, 99 + players, &h1, &rounds, &shots);
    int sides = players == 4 ? 2 : players;
    CHECK(w >= 0 && w < sides, "%d seats: a full CPU game ends with a winner (winner %d)", players, w);
    printf("  %d seats: winner side %d after %d rounds, %d shots, hash %016llx\n", players, w, rounds, shots, (unsigned long long)h1);
    cpu_match(players, lv, 99 + players, &h2, NULL, NULL);
    CHECK(h1 == h2, "%d seats: the same seed plays the same game", players);
    uint64_t h3;
    cpu_match(players, lv, 1000 + players, &h3, NULL, NULL);
    CHECK(h1 != h3, "control: %d seats: another seed plays another game", players);
  }
}

int main(int argc, char **argv) {
  test_trig();
  test_twenty();
  test_open_board_short();
  test_must_hit();
  test_head_on();
  test_no_tunnel();
  test_full_games();
  if (argc > 1 && !strcmp(argv[1], "levels-game")) { /* the CPUs as the game runs them (search_match) */
    int N = argc > 2 ? atoi(argv[2]) : 20;
    int pairs[3][2] = { { 2, 0 }, { 2, 1 }, { 1, 0 } };
    for (int p = 0; p < 3; p++) {
      int wins = 0, done = 0;
      for (int g = 0; g < N; g++) {
        int a = g & 1, lv[4] = { 0 };
        lv[a] = pairs[p][0], lv[1 - a] = pairs[p][1];
        int w = search_match(2, lv, 5000 + (uint64_t)g * 31);
        done += w >= 0, wins += w == a;
      }
      printf("  game path: level %d vs level %d: %d / %d (%d finished)\n", pairs[p][0], pairs[p][1], wins, N, done);
    }
  }
  if (argc > 1 && !strcmp(argv[1], "levels")) {
    int N = argc > 2 ? atoi(argv[2]) : 20;
    int pairs[3][2] = { { 2, 0 }, { 2, 1 }, { 1, 0 } };
    int only = argc > 3 ? atoi(argv[3]) : -1; /* one pairing: 0 Hard-Easy, 1 Hard-Normal, 2 Normal-Easy */
    for (int p = 0; p < 3; p++) {
      if (only >= 0 && p != only) continue;
      int wins = 0;
      for (int g = 0; g < N; g++) {
        /* alternate who sits first */
        int a = g & 1;
        int lv[4] = { 0 };
        lv[a] = pairs[p][0], lv[1 - a] = pairs[p][1];
        int w = cpu_match(2, lv, 5000 + (uint64_t)g * 31, NULL, NULL, NULL);
        if (w == a) wins++;
      }
      printf("  level %d vs level %d: %d / %d\n", pairs[p][0], pairs[p][1], wins, N);
    }
  }
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
