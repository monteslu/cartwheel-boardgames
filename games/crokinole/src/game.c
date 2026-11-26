/* crokinole's match (game.h) */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/project.h"

static const char *const ACTION_NAMES[CT_COUNT] = { "slide_left", "slide_right", "aim_left", "aim_right", "spin_up",
                                                    "spin_down",  "flick",       "cancel",   "replay" };

int game_hold_rate(int k, int cap) {
  if (k <= 0) return 0;
  if (k == 1) return 1;
  if (k < 12) return 0;
  int r = 1 + (k - 12) / 5;
  return r > cap ? cap : r;
}

/* the steps a press held for h ticks moves in all (people and the CPU alike: strength 1) */
static int hold_total(int h, int cap) {
  int s = 0;
  for (int k = 1; k <= h; k++) s += game_hold_rate(k, cap);
  return s;
}

enum { CAP_SLIDE = 3, CAP_AIM = 6, CAP_SPIN = 1 };

static int seat_level(const game *g, int seat) { return g->seat_kind[seat] == SEAT_CPU ? g->seat_level[seat] : 1; }

/* the shot a CPU enters: its search's choice with its hand's error (seeded by the state, so the same every run) */
static crk_shot cpu_target(const game *g, const crk_state *s, bk_move move, int seat) {
  crk_cpu_level L = crk_cpu_level_of(seat_level(g, seat));
  crk_shot t = crk_move_unpack(crk_cpu_perturb(s, move, &L, 0x5eedu + (uint64_t)seat));
  if (!g->spin_on) t.spin = 0;
  if (t.power < 1) t.power = 1;
  return t;
}

static bool next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  game *g = user;
  memset(out, 0, sizeof *out);
  if (g->phase != GP_AIM || g->aim_seat != seat || g->replaying) return false;
  crk_shot t = cpu_target(g, state, move, seat);
  const crk_shot *a = &g->aim;
  int ctl = -1, diff = 0, cap = 1;
  if (g->charging) return false; /* a charge in progress (a person's, before the standby took over): wait */
  if (a->pos != t.pos) ctl = t.pos > a->pos ? CT_SLIDE_R : CT_SLIDE_L, diff = abs(t.pos - a->pos), cap = CAP_SLIDE;
  else if (a->aim != t.aim) ctl = t.aim > a->aim ? CT_AIM_L : CT_AIM_R, diff = abs(t.aim - a->aim), cap = CAP_AIM;
  else if (g->spin_on && a->spin != t.spin) ctl = t.spin > a->spin ? CT_SPIN_U : CT_SPIN_D, diff = abs(t.spin - a->spin), cap = CAP_SPIN;
  if (ctl >= 0) {
    int h = 1;
    while (hold_total(h + 1, cap) <= diff) h++;
    out->v[0] = (cw_action_value){ g->act[ctl], 1 };
    out->count = 1;
    out->hold = h;
    return true;
  }
  /* the flick: held exactly as many ticks as the power steps */
  out->v[0] = (cw_action_value){ g->act[CT_FLICK], 1 };
  out->count = 1;
  out->hold = t.power;
  return true;
}

static bk_cpu *new_cpu(game *g, int level, uint64_t seed) {
  cw_search_options o;
  memset(&o, 0, sizeof o);
  o.kind = CW_SEARCH_GREEDY; /* every candidate shot simulated to rest once and judged */
  o.max_depth = 1;
  o.max_nodes = 4 * CRK_CPU_MAXCAND + 64;
  o.tt_entries = -1;
  static const float MISTAKE[3] = { 0.25f, 0.06f, 0 };
  o.mistake = MISTAKE[level < 0 ? 0 : level > 2 ? 2 : level];
  o.mistake_spread = 4;
  g->ru_cpu[level < 0 ? 0 : level > 2 ? 2 : level].level = level;
  return bk_cpu_new(&g->cpu_rules[level < 0 ? 0 : level > 2 ? 2 : level],
                    &(bk_cpu_desc){ .level = level, .kind = CW_SEARCH_GREEDY, .options = &o, .units_per_tick = 60, .think_ticks = 40, .press_gap = 4,
                                    .seed = seed, .next_press = next_press, .user = g });
}

static void start_turn(game *g) {
  const crk_state *s = game_state(g);
  g->aim_seat = s->to_move;
  g->aim = g->seat_aim[g->aim_seat];
  g->aim.power = 0;
  if (!g->spin_on) g->aim.spin = 0;
  g->meter = 0, g->charging = false, g->drag = g->slide_drag = g->ptr_dragging = false;
  memset(g->held, 0, sizeof g->held), memset(g->acc, 0, sizeof g->acc);
  g->idle_t = 0, g->standby_on = false;
  g->phase = GP_AIM, g->phase_t = 0;
}

void game_new(game *g, int players, const int kind[4], const int level[4], int target, bool spin_on, uint64_t seed) {
  game_free(g);
  memset(g, 0, sizeof *g);
  g->players = players < 2 ? 2 : players > 4 ? 4 : players;
  g->target = target;
  g->spin_on = spin_on;
  for (int i = 0; i < CT_COUNT; i++) {
    g->act[i] = cw_action_find(ACTION_NAMES[i]);
    if (g->act[i] < 0) CW_FATAL("crokinole: the project defines no action %s", ACTION_NAMES[i]);
  }
  g->ru_match.level = -1;
  g->rules = crk_rules(&g->ru_match, g->players);
  for (int l = 0; l < 3; l++) g->ru_cpu[l].level = l, g->cpu_rules[l] = crk_rules(&g->ru_cpu[l], g->players);
  crk_state s0;
  crk_new(&s0, g->players, target);
  g->m = bk_match_new(&g->rules, &s0, seed);
  bk_match_set_legal(g->m, crk_rules_legal, NULL); /* a shot is any place, aim and power, not one of a list */
  for (int i = 0; i < 4; i++) {
    g->seat_kind[i] = kind ? kind[i] : SEAT_PAD;
    g->seat_level[i] = level ? level[i] : 1;
    g->seat_aim[i] = (crk_shot){ CRK_POS_N / 2, 0, 0, 0 };
    if (i < g->players && g->seat_kind[i] == SEAT_CPU) g->cpu[i] = new_cpu(g, g->seat_level[i], seed * 31u + (uint64_t)i);
  }
  g->standby = new_cpu(g, 1, seed * 37u + 99u);
  g->idle_limit = IDLE_TICKS;
  start_turn(g);
}

void game_free(game *g) {
  if (g->m) bk_match_free(g->m);
  for (int i = 0; i < 4; i++)
    if (g->cpu[i]) bk_cpu_free(g->cpu[i]);
  if (g->standby) bk_cpu_free(g->standby);
  g->m = NULL, g->standby = NULL;
  memset(g->cpu, 0, sizeof g->cpu);
}

const crk_state *game_state(const game *g) { return bk_match_state(g->m); }

const crk_state *game_view_state(const game *g) {
  if (g->phase == GP_FLIGHT) return &g->anim;
  if (g->phase == GP_RESULT || g->phase == GP_ROUND || g->phase == GP_OVER) return &g->anim; /* settled, before the next deal */
  return game_state(g);
}

int game_turn(const game *g) { return g->aim_seat; }
bool game_over(const game *g) { return g->phase == GP_OVER; }
bool game_seat_is_cpu(const game *g, int seat) { return seat >= 0 && seat < 4 && g->seat_kind[seat] == SEAT_CPU; }
bool game_cpu_thinking(const game *g) {
  int s = g->aim_seat;
  bk_cpu *c = g->cpu[s] ? g->cpu[s] : g->standby_on ? g->standby : NULL;
  return c && bk_cpu_thinking(c);
}

void game_aim_disc(const game *g, float *x, float *y, float *dx, float *dy) {
  const crk_state *s = game_state(g);
  int p = crk_free_pos(s, g->aim_seat, g->aim.pos);
  float pos = crk_pos_f(p < 0 ? g->aim.pos : p);
  crk_shot_place(s, g->aim_seat, pos, x, y);
  crk_shot_dir(s, g->aim_seat, pos, crk_aim_f(g->aim.aim), dx, dy);
}

static void pointer_plan(game *g, bool down, bool pressed, float px, float py, bool valid);

void game_input(game *g, bool ptr_down, bool ptr_pressed, float px, float py, bool ptr_valid) {
  if (!g->m) return;
  pointer_plan(g, ptr_down, ptr_pressed, px, py, ptr_valid);
  g->pt.tap = ptr_pressed;
  for (int s = 0; s < g->players; s++) {
    if (!g->cpu[s]) continue;
    cw_action_value v[4];
    int n = bk_cpu_tick(g->cpu[s], g->m, s, v, 4);
    cw_input_set_actions(s, v, n);
  }
  /* an idle person's turn: the standby plays it (their own presses take it back) */
  int s = g->aim_seat;
  if (g->standby_on && !g->cpu[s] && g->phase == GP_AIM) {
    cw_action_value v[4];
    int n = bk_cpu_tick(g->standby, g->m, s, v, 4);
    if (n) cw_input_set_actions(s, v, n);
    g->standby_pressing = n > 0;
  }
}

static int step(game *g, int c, float strength, int cap) {
  int k = g->held[c];
  if (k <= 0) return 0;
  if (k == 1) return 1;
  float s = strength >= 0.99f ? 1.0f : strength;
  g->acc[c] += (float)game_hold_rate(k, cap) * s;
  int n = (int)g->acc[c];
  g->acc[c] -= (float)n;
  return n;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void fire(game *g, int power, bool by_pointer) {
  crk_shot sh = g->aim;
  sh.power = (int16_t)clampi(power, 1, CRK_POW_N);
  if (!g->spin_on) sh.spin = 0;
  uint64_t mv = crk_move_pack(sh);
  g->before = *game_state(g);
  if (!bk_match_play(g->m, mv)) CW_FATAL("crokinole: the match refused shot 0x%llx of seat %d", (unsigned long long)mv, g->aim_seat);
  g->anim = g->before;
  crk_shot_begin(&g->anim, sh);
  g->last_move = mv, g->have_last = true, g->last_seat = g->aim_seat;
  g->seat_aim[g->aim_seat] = sh;
  g->seat_aim[g->aim_seat].power = 0;
  g->shots++;
  if (g->cpu[g->aim_seat]) g->cpu_shots++;
  else if (g->standby_on) g->standby_turns++;
  else if (by_pointer) g->pointer_shots++;
  else g->pad_shots++;
  g->ev.flick = true, g->ev.flick_power = crk_pow_f(sh.power);
  g->charging = false, g->meter = 0, g->drag = g->ptr_dragging = false;
  g->phase = GP_FLIGHT, g->phase_t = 0;
  g->replaying = false;
}

static void start_replay(game *g) {
  g->anim = g->before;
  crk_shot_begin(&g->anim, crk_move_unpack(g->last_move));
  g->replaying = true;
  g->replays++;
  g->phase = GP_FLIGHT, g->phase_t = 0;
}

/* A pointer on the board (a finger or the mouse): slide along the arc, or pull back from the disc and let go. It comes
 * from the engine's recorded pointer channel (main.c reads cw_input_pointer in fixed_update), so the input recording, a
 * replay and the input hash carry pointer shots exactly as pad presses; what it means this tick is g->pt. */
static void pointer_plan(game *g, bool down, bool pressed, float px, float py, bool valid) {
  int seat = g->aim_seat;
  memset(&g->pt, 0, sizeof g->pt);
  if (g->phase != GP_AIM || g->replaying || g->cpu[seat] || g->standby_on) { g->drag = g->slide_drag = false; return; }
  float x, y, dx, dy;
  game_aim_disc(g, &x, &y, &dx, &dy);
  if (pressed && valid) {
    float d = hypotf(px - x, py - y);
    if (d < CRK_R_DISC * 2.6f) g->drag = true;
    else {
      float r = hypotf(px, py);
      if (r > CRK_R_10 && r < CRK_R_SURF + 0.06f) g->slide_drag = true;
    }
  }
  if (valid) g->drag_x = px, g->drag_y = py;
  float ax, ay;
  crk_seat_axis(g->players, seat, &ax, &ay);
  if (g->slide_drag) {
    if (!down) { g->slide_drag = false; return; }
    if (!valid) return;
    /* the arc place nearest the pointer's angle */
    float ang = crk_atan2(py, px) - crk_atan2(ay, ax);
    while (ang > 3.14159265f) ang -= 6.2831853f;
    while (ang < -3.14159265f) ang += 6.2831853f;
    float half = 3.14159265f * 0.25f - (CRK_R_DISC * 1.05f) / CRK_R_5;
    int pos = clampi((int)((ang + half) / (2 * half) * CRK_POS_N + 0.5f), 0, CRK_POS_N);
    g->pt.slide = true, g->pt.pos = pos;
    return;
  }
  if (!g->drag) return;
  /* the pull: from the disc back to the pointer; the shot goes the other way */
  float vx = x - g->drag_x, vy = y - g->drag_y, len = hypotf(vx, vy);
  int aim = g->aim.aim;
  if (len > 0.004f) {
    float a = crk_atan2(vy, vx) - crk_atan2(-ay, -ax);
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    aim = clampi((int)lroundf(a / CRK_AIM_STEP), -CRK_AIM_N, CRK_AIM_N);
  }
  int power = clampi((int)(len / PULL_MAX * CRK_POW_N + 0.5f), 0, CRK_POW_N);
  if (down) g->pt.drag = true;
  else {
    g->drag = false;
    if (power < 3) return; /* a touch that did not pull: nothing */
    g->pt.shot = true;
  }
  g->pt.aim = aim, g->pt.power = power;
}

static void aim_tick(game *g) {
  int seat = g->aim_seat;
  bool any = false;
  float strength[CT_COUNT];
  for (int c = 0; c < CT_COUNT; c++) {
    strength[c] = cw_action_strength(seat, g->act[c]);
    if (cw_action_pressed(seat, g->act[c])) g->held[c]++, any = true;
    else g->held[c] = 0, g->acc[c] = 0;
  }
  bool cpu_seat = g->cpu[seat] != NULL;
  if (!cpu_seat) {
    if (g->standby_on && any && !g->standby_pressing) g->standby_on = false, g->idle_t = 0; /* they are back */
    if (any) g->idle_t = 0;
    else if (!g->standby_on && g->idle_limit > 0 && ++g->idle_t >= g->idle_limit) g->standby_on = true;
  }
  g->standby_pressing = false;
  if (g->held[CT_REPLAY] == 1 && cw_action_just_pressed(seat, g->act[CT_REPLAY]) && g->have_last && !g->charging && !g->ptr_dragging) {
    start_replay(g);
    return;
  }
  /* the pointer's intent (pointer_plan read it from this tick's recorded pointer) */
  g->ptr_dragging = false;
  if (g->pt.slide) g->aim.pos = (int16_t)clampi(g->pt.pos, 0, CRK_POS_N), g->idle_t = 0;
  if (g->pt.drag || g->pt.shot) {
    g->aim.aim = (int16_t)clampi(g->pt.aim, -CRK_AIM_N, CRK_AIM_N);
    g->meter = clampi(g->pt.power, 0, CRK_POW_N);
    g->charging = false, g->ptr_dragging = true, g->idle_t = 0;
    if (g->pt.shot) fire(g, g->meter, true);
    return;
  }
  if (g->phase != GP_AIM) return;
  if (g->charging) {
    if (g->held[CT_CANCEL] == 1) g->charging = false, g->meter = 0, g->cancels++;
    else if (g->held[CT_FLICK] > 0) g->meter = g->meter < CRK_POW_N ? g->meter + 1 : CRK_POW_N;
    else fire(g, g->meter, false);
    return;
  }
  int dpos = step(g, CT_SLIDE_R, strength[CT_SLIDE_R], CAP_SLIDE) - step(g, CT_SLIDE_L, strength[CT_SLIDE_L], CAP_SLIDE);
  int daim = step(g, CT_AIM_L, strength[CT_AIM_L], CAP_AIM) - step(g, CT_AIM_R, strength[CT_AIM_R], CAP_AIM);
  if (dpos && !cpu_seat) g->slides++;
  g->aim.pos = (int16_t)clampi(g->aim.pos + dpos, 0, CRK_POS_N);
  g->aim.aim = (int16_t)clampi(g->aim.aim + daim, -CRK_AIM_N, CRK_AIM_N);
  if (g->spin_on) {
    int dsp = step(g, CT_SPIN_U, strength[CT_SPIN_U], CAP_SPIN) - step(g, CT_SPIN_D, strength[CT_SPIN_D], CAP_SPIN);
    if (dsp && !cpu_seat) g->spins++;
    g->aim.spin = (int16_t)clampi(g->aim.spin + dsp, -CRK_SPIN_N, CRK_SPIN_N);
  }
  if (g->held[CT_FLICK] == 1 && cw_action_just_pressed(seat, g->act[CT_FLICK])) g->charging = true, g->meter = 1; /* a fresh press, not one held over */
}

static int twenties_of(const crk_state *s) {
  int n = 0;
  for (int k = 0; k < 4; k++) n += s->twenties[k];
  return n;
}

void game_tick(game *g) {
  memset(&g->ev, 0, sizeof g->ev);
  g->phase_t++;
  switch (g->phase) {
  case GP_AIM:
    aim_tick(g);
    break;
  case GP_FLIGHT: {
    if (g->replaying && (g->phase_t & 1)) break; /* the replay at half speed */
    crk_events ev;
    bool done = crk_tick(&g->anim, &ev);
    g->ev.n = ev.n;
    memcpy(g->ev.e, ev.e, sizeof(crk_event) * (size_t)ev.n);
    for (int i = 0; i < ev.n; i++)
      if (ev.e[i].kind == CRK_EV_DISC || ev.e[i].kind == CRK_EV_PEG) g->contacts++;
    if (!done) break;
    if (g->replaying) {
      g->replaying = false;
      g->phase = GP_AIM, g->phase_t = 0;
      memset(g->held, 0, sizeof g->held);
      break;
    }
    int tw = twenties_of(&g->anim);
    crk_shot_settle(&g->anim);
    if (twenties_of(&g->anim) > tw) g->twenties++, g->ev.twenty = true;
    g->seat_shots[g->last_seat]++;
    if (g->anim.last_foul) g->fouls++, g->seat_fouls[g->last_seat]++, g->ev.foul = true;
    /* the board as the shot left it stays on screen; the match must hold exactly what follows from it */
    crk_state next = g->anim;
    crk_shot_next(&next);
    if (crk_hash(&next) != bk_match_hash(g->m))
      CW_FATAL("crokinole: the shot on screen ended in another state than the match (%016llx vs %016llx)",
               (unsigned long long)crk_hash(&next), (unsigned long long)bk_match_hash(g->m));
    g->checks++;
    g->ev.landed = true;
    g->phase = GP_RESULT, g->phase_t = 0;
    break;
  }
  case GP_RESULT:
    if (g->phase_t < 50) break;
    {
      const crk_state *s = game_state(g);
      if (s->rounds_done != g->rounds_seen) {
        g->rounds_seen = s->rounds_done;
        g->round = s->last;
        g->ev.round_end = true;
        if (s->phase == CRK_OVER) g->phase = GP_OVER, g->ev.game_over = true;
        else g->phase = GP_ROUND;
        g->phase_t = 0;
      } else start_turn(g);
    }
    break;
  case GP_ROUND: {
    bool go = g->phase_t > 600;
    if (g->phase_t > 45)
      for (int s = 0; s < g->players; s++)
        if (!g->cpu[s] && (cw_action_just_pressed(s, g->act[CT_FLICK]) || g->pt.tap)) go = true;
    bool all_cpu = true;
    for (int s = 0; s < g->players; s++)
      if (!g->cpu[s]) all_cpu = false;
    if (all_cpu && g->phase_t > 240) go = true;
    if (go) start_turn(g);
    break;
  }
  case GP_OVER:
  default:
    break;
  }
}
