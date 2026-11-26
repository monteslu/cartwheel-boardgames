/* Crokinole's computer player (crk_cpu.h). */
#include "crk_cpu.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

crk_cpu_level crk_cpu_level_of(int level) {
  switch (level) {
  case 0: return (crk_cpu_level){ 5, 0.026f, 0.065f };
  case 1: return (crk_cpu_level){ 5, 0.018f, 0.045f };
  default: return (crk_cpu_level){ 9, 0.006f, 0.015f };
  }
}

static float wrap(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

typedef struct cand_buf { uint64_t *out; int n, cap; } cand_buf;

static int iround(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }

static void add(cand_buf *b, const crk_state *s, int seat, int pos, float tx, float ty, float lateral, float power) {
  if (b->n >= b->cap) return;
  float x, y, ax, ay;
  crk_shot_place(s, seat, crk_pos_f(pos), &x, &y);
  crk_seat_axis(s->players, seat, &ax, &ay);
  float dx = tx - x, dy = ty - y, L = sqrtf(dx * dx + dy * dy);
  if (L < 1e-4f) return;
  float dir = crk_atan2(dy, dx);
  if (lateral != 0) {
    float q = lateral / L;
    if (q > 0.95f || q < -0.95f) return;
    dir += q + q * q * q / 6; /* asin, close enough for a small offset */
  }
  int aim = iround(wrap(dir - crk_atan2(-ay, -ax)) / CRK_AIM_STEP);
  if (aim > CRK_AIM_N || aim < -CRK_AIM_N) return;
  int pw = iround(power * (float)CRK_POW_N);
  if (pw > CRK_POW_N) pw = CRK_POW_N;
  if (pw < 1) return;
  uint64_t m = crk_move_pack((crk_shot){ (int16_t)pos, (int16_t)aim, (int16_t)pw, 0 });
  for (int i = 0; i < b->n; i++)
    if (b->out[i] == m) return; /* the same shot on the grid already */
  b->out[b->n++] = m;
}

int crk_cpu_candidates(const crk_state *s, const crk_cpu_level *L, uint64_t *out, int cap) {
  cand_buf b = { out, 0, cap };
  int seat = s->to_move;
  int side = crk_side_of(s, seat);
  bool opp = crk_opponent_on_board(s, seat);
  int np = L->positions;
  for (int pi = 0; pi < np; pi++) {
    int want = np == 1 ? CRK_POS_N / 2 : iround((0.06f + 0.88f * (float)pi / (float)(np - 1)) * (float)CRK_POS_N);
    int pos = crk_free_pos(s, seat, want);
    if (pos < 0) continue;
    float sx, sy;
    crk_shot_place(s, seat, crk_pos_f(pos), &sx, &sy);
    if (opp) {
      for (int i = 0; i < CRK_NDISC; i++) {
        const crk_disc *d = &s->d[i];
        if (d->st != CRK_BOARD || d->side == side) continue;
        float dx = d->x - sx, dy = d->y - sy, dist = sqrtf(dx * dx + dy * dy);
        float reach = crk_power_for(dist);
        static const float lat[5] = { -1.2f, -0.6f, 0, 0.6f, 1.2f };
        for (int k = 0; k < 5; k++) {
          float off = lat[k] * CRK_R_DISC;
          add(&b, s, seat, pos, d->x, d->y, off, reach * 1.12f + 0.04f); /* a tap that stays near */
          add(&b, s, seat, pos, d->x, d->y, off, 0.62f);
          add(&b, s, seat, pos, d->x, d->y, off, 0.85f);
          if (k == 2) add(&b, s, seat, pos, d->x, d->y, off, 1.0f);
        }
      }
    } else {
      float dist = sqrtf(sx * sx + sy * sy);
      float p20 = crk_power_for(dist);
      for (int k = -1; k <= 1; k++) add(&b, s, seat, pos, 0, 0, 0, p20 + (float)k / (float)CRK_POW_N);
      /* spots in the 15 region: some behind the pegs */
      for (int k = 0; k < 8; k++) {
        float a = PI_F / 8 + (float)k * (PI_F / 4), sn, c;
        crk_sincos(a, &sn, &c);
        for (int rr = 0; rr < 2; rr++) {
          float r = rr ? 0.072f : 0.045f;
          float tx = r * c, ty = r * sn;
          float ddx = tx - sx, ddy = ty - sy;
          add(&b, s, seat, pos, tx, ty, 0, crk_power_for(sqrtf(ddx * ddx + ddy * ddy)));
        }
      }
    }
  }
  return b.n;
}

void crk_cpu_evaluate(const crk_state *s, float v[4]) {
  for (int k = 0; k < 4; k++) v[k] = 0;
  if (s->phase == CRK_OVER) {
    for (int k = 0; k < s->sides; k++) v[k] = k == s->winner ? 1.0e6f : -1.0e6f;
  } else {
    int16_t pts[4];
    crk_count(s, pts);
    float val[4];
    for (int k = 0; k < s->sides; k++) {
      val[k] = (float)s->score[k] + (float)pts[k];
      /* a disc behind a peg (seen from the other sides' arcs) is worth a little more: it is hard to take out */
      for (int i = 0; i < CRK_NDISC; i++) {
        const crk_disc *d = &s->d[i];
        if (d->st != CRK_BOARD || d->side != k) continue;
        float r = sqrtf(d->x * d->x + d->y * d->y);
        if (r < CRK_R_15 + CRK_R_DISC) val[k] += 1.5f;
      }
    }
    for (int k = 0; k < s->sides; k++) {
      float best = -1e9f;
      for (int o = 0; o < s->sides; o++)
        if (o != k && val[o] > best) best = val[o];
      v[k] = val[k] - best;
    }
  }
  /* seats read their side's value */
  float side_v[4];
  memcpy(side_v, v, sizeof side_v);
  for (int seat = 0; seat < s->players; seat++) v[seat] = side_v[crk_side_of(s, seat)];
}

static uint64_t mix(uint64_t x) {
  x ^= x >> 33, x *= 0xff51afd7ed558ccdull, x ^= x >> 33, x *= 0xc4ceb9fe1a85ec53ull, x ^= x >> 33;
  return x;
}

static float gauss(uint64_t *r) { /* sum of four uniforms, unit variance */
  float sum = 0;
  for (int i = 0; i < 4; i++) {
    *r = mix(*r + 0x9e3779b97f4a7c15ull);
    sum += (float)(*r >> 40) / 16777216.0f;
  }
  return (sum - 2.0f) * 1.7320508f;
}

uint64_t crk_cpu_perturb(const crk_state *s, uint64_t move, const crk_cpu_level *L, uint64_t seed) {
  uint64_t r = mix(crk_hash(s) ^ seed);
  crk_shot sh = crk_move_unpack(move);
  sh.aim = (int16_t)(sh.aim + iround(gauss(&r) * L->aim_err / CRK_AIM_STEP));
  sh.power = (int16_t)(sh.power + iround((float)sh.power * gauss(&r) * L->power_err));
  return crk_move_pack(sh);
}

uint64_t crk_cpu_choose(const crk_state *s, int level, uint64_t seed, int *evals) {
  crk_cpu_level L = crk_cpu_level_of(level);
  static uint64_t cand[CRK_CPU_MAXCAND];
  int n = crk_cpu_candidates(s, &L, cand, CRK_CPU_MAXCAND);
  int side = crk_side_of(s, s->to_move);
  float best = -1e30f;
  uint64_t pick = crk_move_pack((crk_shot){ CRK_POS_N / 2, 0, (int16_t)iround(crk_power_for(CRK_R_5) * CRK_POW_N), 0 });
  static crk_state t;
  for (int i = 0; i < n; i++) {
    t = *s;
    crk_apply(&t, cand[i]);
    float v[4];
    crk_cpu_evaluate(&t, v);
    float val = v[0];
    for (int seat = 0; seat < s->players; seat++)
      if (crk_side_of(s, seat) == side) { val = v[seat]; break; }
    if (val > best) best = val, pick = cand[i];
  }
  if (evals) *evals = n;
  return crk_cpu_perturb(s, pick, &L, seed);
}
