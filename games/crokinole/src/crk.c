/* Crokinole rules and disc physics (crk.h). Deterministic: + - * / sqrt only; build with -ffp-contract=off. */
#include "crk.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(crk_disc) == 28, "crk_disc has no padding");
_Static_assert(offsetof(crk_state, moved) == 36, "crk_state header has no padding");
_Static_assert(sizeof(crk_state) % 4 == 0, "crk_state is whole words");

/* the felt-free polished board: a disc slides and stops by dry friction, with a little speed-proportional drag */
#define FRIC_A 1.15f      /* m/s^2 */
#define FRIC_LIN 0.10f    /* 1/s */
#define SPIN_A 30.0f      /* rad/s^2: a spinning disc's spin dies on the wood */
#define SPIN_LIN 0.8f
#define E_DISC 0.90f      /* disc on disc: hard, lacquered, it clacks */
#define E_PEG 0.55f       /* a peg's rubber sleeve */
#define MU_DISC 0.10f
#define MU_PEG 0.30f
#define REST_V 0.02f      /* slower approaches do not bounce */
#define STOP_V 0.003f
#define HOLE_V 0.65f      /* a disc slower than this over the hole drops in (a faster one skims across) */
#define SUB_FRAC 0.30f    /* a substep moves a disc at most this many radii */
#define MAX_SUB 64
#define MAX_EVENTS_SUB 48

/* ── deterministic trig ───────────────────────────────────────────── */

#define PI_F 3.14159265358979f
#define TAU_F 6.28318530717959f

void crk_sincos(float a, float *s, float *c) {
  /* reduce to [-pi, pi] */
  float k = (float)(int)(a / TAU_F + (a >= 0 ? 0.5f : -0.5f));
  float x = a - k * TAU_F;
  /* reflect to [-pi/2, pi/2] for sin; cos from the sign of the reflection */
  float cs = 1.0f;
  if (x > PI_F * 0.5f) x = PI_F - x, cs = -1.0f;
  else if (x < -PI_F * 0.5f) x = -PI_F - x, cs = -1.0f;
  float x2 = x * x;
  /* Taylor to x^13 on [-pi/2, pi/2]: error below 1e-7 */
  float sn = x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880 + x2 * (-1.0f / 39916800 + x2 * (1.0f / 6227020800.0f)))))));
  float cn = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320 + x2 * (-1.0f / 3628800 + x2 * (1.0f / 479001600.0f))))));
  *s = sn;
  *c = cn * cs;
}

static float atan_unit(float z) { /* |z| <= 1 */
  /* atan(z) = z - z^3/3 ... converges slowly near 1: halve the angle first: atan z = 2 atan(z / (1 + sqrt(1 + z^2))) */
  float h = z / (1.0f + sqrtf(1.0f + z * z));
  float h2 = h * h;
  float r = h * (1.0f + h2 * (-1.0f / 3 + h2 * (1.0f / 5 + h2 * (-1.0f / 7 + h2 * (1.0f / 9 + h2 * (-1.0f / 11 + h2 * (1.0f / 13 + h2 * (-1.0f / 15))))))));
  return 2.0f * r;
}

float crk_atan2(float y, float x) {
  if (x == 0 && y == 0) return 0;
  float ax = fabsf(x), ay = fabsf(y), a;
  if (ax >= ay) a = atan_unit(ay / ax);
  else a = PI_F * 0.5f - atan_unit(ax / ay);
  if (x < 0) a = PI_F - a;
  return y < 0 ? -a : a;
}

/* ── geometry ─────────────────────────────────────────────────────── */

void crk_peg(int i, float *x, float *y) {
  float s, c;
  crk_sincos(PI_F / 8 + (float)i * (PI_F / 4), &s, &c);
  *x = CRK_R_15 * c, *y = CRK_R_15 * s;
}

static int quadrant_of(int players, int seat) { return players == 2 ? seat * 2 : seat; }

static float seat_angle(int players, int seat) { /* bottom, then clockwise seen from above (left, top, right) */
  return -PI_F * 0.5f - (float)quadrant_of(players, seat) * (PI_F * 0.5f);
}

void crk_seat_axis(int players, int seat, float *ax, float *ay) {
  float s, c;
  crk_sincos(seat_angle(players, seat), &s, &c);
  *ax = c, *ay = s;
}

/* half the arc a seat may shoot from: its quarter, less a disc's width at each quadrant line */
static float arc_half(void) { return PI_F * 0.25f - (CRK_R_DISC * 1.05f) / CRK_R_5; }

void crk_shot_place(const crk_state *s, int seat, float pos, float *x, float *y) {
  /* pos 0 is the seat's left end: seen from the seat the board's angle runs clockwise left to right... the seat
   * faces the centre, so its left is the axis angle minus the half arc (counter-clockwise turn is to the right
   * of a viewer looking in from outside) */
  float a = seat_angle(s->players, seat) - arc_half() + 2.0f * arc_half() * pos;
  float sn, c;
  crk_sincos(a, &sn, &c);
  *x = CRK_R_5 * c, *y = CRK_R_5 * sn;
}

void crk_shot_dir(const crk_state *s, int seat, float pos, float aim, float *dx, float *dy) {
  (void)pos;
  float ax, ay;
  crk_seat_axis(s->players, seat, &ax, &ay);
  float ix = -ax, iy = -ay; /* straight in */
  float sn, c;
  crk_sincos(aim, &sn, &c);
  /* counter-clockwise by aim: the seat's left */
  *dx = ix * c - iy * sn, *dy = ix * sn + iy * c;
}

int crk_side_of(const crk_state *s, int seat) { return seat % s->sides; }

int crk_next_disc(const crk_state *s, int seat) {
  for (int i = 0; i < CRK_NDISC; i++)
    if (s->d[i].owner == seat && s->d[i].st == CRK_HAND) return i;
  return -1;
}

bool crk_opponent_on_board(const crk_state *s, int seat) {
  int side = crk_side_of(s, seat);
  for (int i = 0; i < CRK_NDISC; i++)
    if (s->d[i].st == CRK_BOARD && s->d[i].side != side) return true;
  return false;
}

static float dist0(float x, float y) { return sqrtf(x * x + y * y); }

int crk_disc_points(const crk_disc *d) {
  if (d->st != CRK_BOARD) return 0;
  float r = dist0(d->x, d->y);
  if (r + CRK_R_DISC <= CRK_R_15) return 15;
  if (r + CRK_R_DISC <= CRK_R_10) return 10;
  if (r + CRK_R_DISC < CRK_R_5) return 5;
  return 0;
}

void crk_count(const crk_state *s, int16_t pts[4]) {
  for (int k = 0; k < 4; k++) pts[k] = (int16_t)(s->twenties[k] * 20);
  for (int i = 0; i < CRK_NDISC; i++) pts[s->d[i].side] = (int16_t)(pts[s->d[i].side] + crk_disc_points(&s->d[i]));
}

static bool overlaps_any(const crk_state *s, float x, float y) {
  for (int i = 0; i < CRK_NDISC; i++) {
    const crk_disc *d = &s->d[i];
    if (d->st != CRK_BOARD) continue;
    float dx = d->x - x, dy = d->y - y;
    if (dx * dx + dy * dy < (2 * CRK_R_DISC) * (2 * CRK_R_DISC)) return true;
  }
  return false;
}

int crk_free_pos(const crk_state *s, int seat, int pos) {
  for (int k = 0; k <= CRK_POS_N; k++) {
    for (int sg = 0; sg < 2; sg++) {
      int p = pos + (sg ? -k : k);
      if (p < 0 || p > CRK_POS_N) continue;
      float x, y;
      crk_shot_place(s, seat, crk_pos_f(p), &x, &y);
      if (!overlaps_any(s, x, y)) return p;
      if (k == 0) break;
    }
  }
  return -1;
}

/* ── state ────────────────────────────────────────────────────────── */

static void deal(crk_state *s) {
  for (int i = 0; i < CRK_NDISC; i++) {
    crk_disc *d = &s->d[i];
    memset(d, 0, sizeof *d);
    d->owner = (uint8_t)(i % s->players);
    d->side = (uint8_t)crk_side_of(s, d->owner);
    d->st = CRK_HAND;
  }
  memset(s->twenties, 0, sizeof s->twenties);
  s->shots = 0;
}

void crk_new(crk_state *s, int players, int target) {
  memset(s, 0, sizeof *s);
  if (players < 2) players = 2;
  if (players > 4) players = 4;
  s->players = (uint8_t)players;
  s->sides = (uint8_t)(players == 4 ? 2 : players);
  s->target = (int16_t)(target > 0 ? target : 100);
  s->round = 1;
  s->winner = -1;
  s->last.winner = -1;
  s->phase = CRK_AIM;
  deal(s);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

crk_shot crk_shot_clamp(crk_shot sh) {
  sh.pos = (int16_t)clampi(sh.pos, 0, CRK_POS_N), sh.aim = (int16_t)clampi(sh.aim, -CRK_AIM_N, CRK_AIM_N);
  sh.power = (int16_t)clampi(sh.power, 0, CRK_POW_N), sh.spin = (int16_t)clampi(sh.spin, -CRK_SPIN_N, CRK_SPIN_N);
  return sh;
}

uint64_t crk_move_pack(crk_shot sh) {
  sh = crk_shot_clamp(sh);
  return (uint64_t)sh.pos | (uint64_t)(sh.aim + CRK_AIM_N) << 8 | (uint64_t)sh.power << 24 | (uint64_t)(sh.spin + CRK_SPIN_N) << 32;
}

crk_shot crk_move_unpack(uint64_t m) {
  crk_shot sh = { (int16_t)(m & 0xff), (int16_t)((int)((m >> 8) & 0xffff) - CRK_AIM_N), (int16_t)((m >> 24) & 0xff),
                  (int16_t)((int)((m >> 32) & 0xff) - CRK_SPIN_N) };
  return crk_shot_clamp(sh);
}

/* ── physics ──────────────────────────────────────────────────────── */

static bool moving(const crk_disc *d) { return d->st == CRK_BOARD && (d->vx != 0 || d->vy != 0); }

static void friction(crk_disc *d, float h) {
  float sp = sqrtf(d->vx * d->vx + d->vy * d->vy);
  if (sp > 0) {
    float ns = sp - (FRIC_A + FRIC_LIN * sp) * h;
    if (ns < STOP_V) d->vx = d->vy = 0;
    else d->vx *= ns / sp, d->vy *= ns / sp;
  }
  if (d->w != 0) {
    float aw = fabsf(d->w), nw = aw - (SPIN_A + SPIN_LIN * aw) * h;
    d->w = nw <= 0.02f ? 0 : (d->w > 0 ? nw : -nw);
  }
}

static int substeps_for(float vmax) {
  float n = vmax * CRK_TICK / (SUB_FRAC * CRK_R_DISC);
  int k = (int)n + 1;
  return k < 1 ? 1 : k > MAX_SUB ? MAX_SUB : k;
}

/* time (from now, <= tmax) until the circles at p with relative velocity v reach distance rs; -1 none */
static float toi(float px, float py, float vx, float vy, float rs, float tmax) {
  float b = px * vx + py * vy;
  if (b >= 0) return -1; /* separating */
  float c = px * px + py * py - rs * rs;
  if (c <= 0) return 0;  /* touching and closing */
  float a = vx * vx + vy * vy;
  float disc = b * b - a * c;
  if (disc < 0) return -1;
  float t = (-b - sqrtf(disc)) / a;
  return t <= tmax ? (t < 0 ? 0 : t) : -1;
}

static void ev_add(crk_events *ev, int kind, int a, int b, float speed, float x, float y) {
  if (!ev || ev->n >= CRK_MAXEV) return;
  ev->e[ev->n++] = (crk_event){ (uint8_t)kind, (uint8_t)a, (uint8_t)b, 0, speed, x, y };
}

/* resolve a contact between disc i and disc j (j >= 0) or peg (-1 - j) */
static void resolve(crk_state *s, int i, int j, crk_events *ev) {
  crk_disc *A = &s->d[i];
  float bx, by, bvx = 0, bvy = 0, bw = 0, inv_b = 0;
  crk_disc *B = NULL;
  if (j >= 0) {
    B = &s->d[j];
    bx = B->x, by = B->y, bvx = B->vx, bvy = B->vy, bw = B->w, inv_b = 1.0f;
  } else {
    crk_peg(-1 - j, &bx, &by);
  }
  float nx = A->x - bx, ny = A->y - by, l = sqrtf(nx * nx + ny * ny);
  if (l < 1e-9f) return;
  nx /= l, ny /= l;
  float tx = -ny, ty = nx;
  float rvx = A->vx - bvx, rvy = A->vy - bvy;
  float vn = rvx * nx + rvy * ny;
  if (vn >= 0) return;
  float e = vn < -REST_V ? (B ? E_DISC : E_PEG) : 0.0f;
  float inv_a = 1.0f;
  float jn = -(1.0f + e) * vn / (inv_a + inv_b);
  /* tangential: the contact points' relative slide, spin included (a disc's I = r^2 / 2 per unit mass) */
  float r = CRK_R_DISC;
  float vt = rvx * tx + rvy * ty - r * (A->w + bw);
  float k = inv_a + inv_b + 2.0f * inv_a + 2.0f * inv_b;
  float mu = B ? MU_DISC : MU_PEG;
  float jt = -vt / k, lim = mu * jn;
  jt = jt > lim ? lim : jt < -lim ? -lim : jt;
  A->vx += (jn * nx + jt * tx) * inv_a, A->vy += (jn * ny + jt * ty) * inv_a;
  A->w += -2.0f * jt / r * inv_a;
  if (B) {
    B->vx -= (jn * nx + jt * tx), B->vy -= (jn * ny + jt * ty);
    B->w += -2.0f * jt / r;
  }
  /* the rules' bookkeeping */
  s->contacts++;
  if (B) {
    int shooter = s->d[s->shot_disc].side;
    bool am = (s->moved >> i) & 1u, bm = (s->moved >> j) & 1u;
    if ((am && A->side == shooter && B->side != shooter) || (bm && B->side == shooter && A->side != shooter)) s->hit_opp = 1;
    if (i == s->shot_disc && B->side == shooter) s->touched_own |= 1u << j;
    if (j == s->shot_disc && A->side == shooter) s->touched_own |= 1u << i;
    s->moved |= 1u << i | 1u << j;
  }
  ev_add(ev, B ? CRK_EV_DISC : CRK_EV_PEG, i, B ? j : -1 - j, -vn, A->x - nx * r, A->y - ny * r);
}

static void substep(crk_state *s, float h, crk_events *ev) {
  for (int i = 0; i < CRK_NDISC; i++)
    if (s->d[i].st == CRK_BOARD) friction(&s->d[i], h);
  float t = 0;
  for (int it = 0; it < MAX_EVENTS_SUB && t < h; it++) {
    float best = -1;
    int bi = -1, bj = 0;
    float rem = h - t;
    for (int i = 0; i < CRK_NDISC; i++) {
      const crk_disc *A = &s->d[i];
      if (!moving(A)) continue;
      for (int p = 0; p < CRK_NPEG; p++) {
        float px, py;
        crk_peg(p, &px, &py);
        float tc = toi(A->x - px, A->y - py, A->vx, A->vy, CRK_R_DISC + CRK_R_PEG, rem);
        if (tc >= 0 && (best < 0 || tc < best)) best = tc, bi = i, bj = -1 - p;
      }
      for (int j = 0; j < CRK_NDISC; j++) {
        if (j == i) continue;
        const crk_disc *B = &s->d[j];
        if (B->st != CRK_BOARD) continue;
        if (moving(B) && j < i) continue; /* that pair was tried from j */
        float tc = toi(A->x - B->x, A->y - B->y, A->vx - B->vx, A->vy - B->vy, 2 * CRK_R_DISC, rem);
        if (tc >= 0 && (best < 0 || tc < best)) best = tc, bi = i, bj = j;
      }
    }
    float adv = best < 0 ? rem : best;
    for (int i = 0; i < CRK_NDISC; i++) {
      crk_disc *d = &s->d[i];
      if (!moving(d)) continue;
      d->x += d->vx * adv, d->y += d->vy * adv;
    }
    t += adv;
    if (best < 0) break;
    resolve(s, bi, bj, ev);
  }
  for (int i = 0; i < CRK_NDISC; i++) {
    crk_disc *d = &s->d[i];
    if (d->st != CRK_BOARD) continue;
    d->rot += d->w * h;
    if (d->vx == 0 && d->vy == 0) continue;
    float r = dist0(d->x, d->y);
    if (r > CRK_R_SURF) {
      ev_add(ev, CRK_EV_DITCH, i, 0, dist0(d->vx, d->vy), d->x, d->y);
      d->st = CRK_DITCH;
      continue;
    }
    if (r < CRK_R_HOLE && dist0(d->vx, d->vy) < HOLE_V) {
      ev_add(ev, CRK_EV_HOLE, i, 0, dist0(d->vx, d->vy), d->x, d->y);
      d->st = CRK_TWENTY;
      d->pad = 1; /* new this shot: credited at the shot's end */
      d->vx = d->vy = 0;
    }
  }
}

bool crk_tick(crk_state *s, crk_events *ev) {
  if (ev) ev->n = 0;
  if (s->phase != CRK_MOVING) return true;
  float vmax = 0;
  for (int i = 0; i < CRK_NDISC; i++)
    if (moving(&s->d[i])) {
      float v = dist0(s->d[i].vx, s->d[i].vy);
      if (v > vmax) vmax = v;
    }
  if (vmax > 0) {
    int n = substeps_for(vmax);
    float h = CRK_TICK / (float)n;
    for (int k = 0; k < n; k++) substep(s, h, ev);
  }
  s->ticks++;
  bool any = false;
  for (int i = 0; i < CRK_NDISC; i++)
    if (moving(&s->d[i])) any = true;
  if (!any || s->ticks >= CRK_MAX_TICKS) {
    for (int i = 0; i < CRK_NDISC; i++) s->d[i].vx = s->d[i].vy = 0, s->d[i].w = 0;
    return true;
  }
  return false;
}

float crk_slide_dist(float power) {
  crk_disc d;
  memset(&d, 0, sizeof d);
  d.vx = power * CRK_VMAX;
  d.st = CRK_BOARD;
  float x = 0;
  for (int t = 0; t < CRK_MAX_TICKS && d.vx > 0; t++) {
    int n = substeps_for(d.vx);
    float h = CRK_TICK / (float)n;
    for (int k = 0; k < n; k++) friction(&d, h), x += d.vx * h;
  }
  return x;
}

float crk_power_for(float dist) {
  float lo = 0, hi = 1;
  if (crk_slide_dist(1) < dist) return 1;
  for (int i = 0; i < 24; i++) {
    float m = (lo + hi) * 0.5f;
    if (crk_slide_dist(m) < dist) lo = m;
    else hi = m;
  }
  return hi;
}

/* ── a shot ───────────────────────────────────────────────────────── */

void crk_shot_begin(crk_state *s, crk_shot sh) {
  if (s->phase != CRK_AIM) return;
  int seat = s->to_move;
  int i = crk_next_disc(s, seat);
  if (i < 0) return;
  sh = crk_shot_clamp(sh);
  int ip = crk_free_pos(s, seat, sh.pos);
  float pos = crk_pos_f(ip < 0 ? sh.pos : ip);
  float power = crk_pow_f(sh.power);
  crk_disc *d = &s->d[i];
  crk_shot_place(s, seat, pos, &d->x, &d->y);
  float dx, dy;
  crk_shot_dir(s, seat, pos, crk_aim_f(sh.aim), &dx, &dy);
  d->vx = dx * power * CRK_VMAX, d->vy = dy * power * CRK_VMAX;
  d->w = (float)sh.spin / (float)CRK_SPIN_N * CRK_SPIN_MAX;
  d->rot = 0;
  d->st = CRK_BOARD;
  s->open_board = !crk_opponent_on_board(s, seat);
  s->shot_disc = (uint8_t)i;
  s->moved = 1u << i;
  s->touched_own = 0, s->hit_opp = 0, s->contacts = 0, s->foul = 0;
  s->ticks = 0;
  s->phase = CRK_MOVING;
}

static void end_round(crk_state *s) {
  crk_round_result *R = &s->last;
  memset(R, 0, sizeof *R);
  crk_count(s, R->pts);
  for (int k = 0; k < 4; k++) R->twenties[k] = s->twenties[k];
  int best = -1, second = -1;
  for (int k = 0; k < s->sides; k++) {
    if (best < 0 || R->pts[k] > R->pts[best]) second = best, best = k;
    else if (second < 0 || R->pts[k] > R->pts[second]) second = k;
  }
  int margin = R->pts[best] - R->pts[second];
  R->winner = (int8_t)(margin > 0 ? best : -1);
  R->scored = (int16_t)(margin > 0 ? margin : 0);
  if (margin > 0) s->score[best] = (int16_t)(s->score[best] + margin);
  s->rounds_done++;
  if (margin > 0 && s->score[best] >= s->target) {
    s->winner = (int8_t)best;
    s->phase = CRK_OVER;
    return;
  }
  s->round++;
  s->starter = (uint8_t)((s->starter + 1) % s->players);
  s->to_move = s->starter;
  deal(s);
}

void crk_shot_settle(crk_state *s) {
  if (s->phase != CRK_MOVING) return;
  int i = s->shot_disc;
  crk_disc *sd = &s->d[i];
  s->foul = 0;
  if (!s->open_board && !s->hit_opp) {
    s->foul = 1;
    if (sd->st == CRK_BOARD || sd->st == CRK_TWENTY) sd->st = CRK_DITCH, sd->pad = 0;
    for (int k = 0; k < CRK_NDISC; k++)
      if ((s->touched_own >> k) & 1u) {
        crk_disc *d = &s->d[k];
        if (d->st == CRK_BOARD || (d->st == CRK_TWENTY && d->pad)) d->st = CRK_DITCH, d->pad = 0;
      }
  } else if (s->open_board) {
    bool ok = sd->st == CRK_TWENTY || (sd->st == CRK_BOARD && dist0(sd->x, sd->y) <= CRK_R_15 + CRK_R_DISC);
    if (!ok) {
      s->foul = 2;
      if (sd->st == CRK_BOARD) sd->st = CRK_DITCH;
    }
  }
  for (int k = 0; k < CRK_NDISC; k++) {
    crk_disc *d = &s->d[k];
    if (d->st == CRK_BOARD && dist0(d->x, d->y) + CRK_R_DISC >= CRK_R_5) d->st = CRK_DITCH;
    if (d->st == CRK_TWENTY && d->pad) {
      d->pad = 0;
      s->twenties[d->side]++;
    }
    d->vx = d->vy = 0, d->w = 0;
  }
  s->last_foul = (int8_t)s->foul;
  s->shots++;
  s->phase = CRK_SETTLED;
}

void crk_shot_next(crk_state *s) {
  if (s->phase != CRK_SETTLED) return;
  s->phase = CRK_AIM;
  bool all = true;
  for (int k = 0; k < CRK_NDISC; k++)
    if (s->d[k].st == CRK_HAND) all = false;
  if (all) end_round(s);
  else s->to_move = (uint8_t)((s->to_move + 1) % s->players);
}

void crk_shot_end(crk_state *s) {
  crk_shot_settle(s);
  crk_shot_next(s);
}

void crk_apply(crk_state *s, uint64_t move) {
  crk_shot_begin(s, crk_move_unpack(move));
  while (!crk_tick(s, NULL)) {}
  crk_shot_end(s);
}

uint64_t crk_hash(const crk_state *s) {
  const uint8_t *p = (const uint8_t *)s;
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < sizeof *s; i++) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}
