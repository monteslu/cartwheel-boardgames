/* Facet: the stones printed on the cards (cardgems.h). */
#include "cardgems.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cuts.h"

typedef struct { float d[3], size, i; float c[3]; } soft_light;

/* the studio the printed stones see (local space: x right, y up the card, z toward the viewer): a warm key high on
 * the upper left, a cool fill on the right, a ring of panels round the viewer and a tight inner ring of small lamps,
 * dark between them: the dark gaps are what make a cut stone's facets read as facets */
static soft_light L[32];
static int NL;
static void studio(void) {
  if (NL) return;
  L[NL++] = (soft_light){{-0.45f, 0.54f, 0.72f}, 0.30f, 6.0f, {1.0f, 0.95f, 0.86f}};
  L[NL++] = (soft_light){{0.70f, 0.10f, 0.70f}, 0.22f, 2.2f, {0.80f, 0.88f, 1.0f}};
  for (int k = 0; k < 6; k++) {
    float a = 0.26f + 1.0472f * (float)k;
    L[NL++] = (soft_light){{0.47f * cosf(a), 0.47f * sinf(a), 0.88f}, 0.17f, k % 2 ? 2.4f : 3.4f, {1.0f, 0.97f, 0.92f}};
  }
  for (int k = 0; k < 8; k++) {
    float a = 0.39f + 0.785f * (float)k;
    L[NL++] = (soft_light){{0.22f * cosf(a), 0.22f * sinf(a), 0.975f}, 0.06f, 3.0f, {1.0f, 0.98f, 0.95f}};
  }
  for (int i = 0; i < NL; i++) {
    float *d = L[i].d, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    d[0] /= l, d[1] /= l, d[2] /= l;
  }
}
static void env(const float dir[3], float out[3]) {
  float up = dir[2];
  float base = up < 0 ? 0.004f : 0.012f + 0.02f * up;
  out[0] = base, out[1] = base * 0.92f, out[2] = base * 0.85f;
  for (int i = 0; i < NL; i++) {
    float c = dir[0] * L[i].d[0] + dir[1] * L[i].d[1] + dir[2] * L[i].d[2];
    float ang = acosf(fminf(fmaxf(c, -1), 1));
    float k = 1 - fminf(fmaxf((ang - L[i].size * 0.7f) / (L[i].size * 0.6f), 0), 1);
    k = k * k * (3 - 2 * k);
    for (int j = 0; j < 3; j++) out[j] += L[i].i * L[i].c[j] * k;
  }
}

typedef struct { float acc[3]; float band[3]; } exit_acc;
static void stone_exit(const float pos[3], const float dir[3], const float w[3], void *user) {
  (void)pos;
  exit_acc *a = user;
  float e[3];
  env(dir, e);
  for (int k = 0; k < 3; k++) a->acc[k] += e[k] * w[k] * a->band[k];
}

static uint8_t *PIX[JW_NCUTS];
static cw_texture TEX[JW_NCUTS];

static float tone(float v) { /* a gentle shoulder, then sRGB */
  v = v / (1 + v * 0.6f) * 1.25f;
  v = fminf(fmaxf(v, 0), 1);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f;
}

const uint8_t *gm_cardgem_pixels(int cut) {
  if (cut < 0 || cut >= JW_NCUTS) return NULL;
  if (PIX[cut]) return PIX[cut];
  studio();
  const jw_cut *c = &jw_cuts[cut];
  const int N = GM_CARDGEM_PX, SS = 2;
  uint8_t *px = calloc((size_t)N * N * 4, 1);
  /* seen from a little in front of overhead: the view tips 20 degrees toward the card's bottom */
  float t = 0.35f, d[3] = {0, sinf(t), -cosf(t)}, up[3] = {0, cosf(t), sinf(t)};
  float ext = c->radius * 1.08f;
  static const float NM[3] = {460, 550, 640};
  float band[3][3], norm[3] = {0, 0, 0};
  for (int b = 0; b < 3; b++) {
    jw_spectrum_rgb(NM[b], band[b]);
    for (int k = 0; k < 3; k++) norm[k] += band[b][k];
  }
  for (int b = 0; b < 3; b++)
    for (int k = 0; k < 3; k++) band[b][k] /= norm[k] > 0 ? norm[k] : 1;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float col[3] = {0, 0, 0}, cover = 0;
      for (int sy = 0; sy < SS; sy++)
        for (int sx = 0; sx < SS; sx++) {
          float u = ((float)x + ((float)sx + 0.5f) / SS) / N * 2 - 1, v = 1 - ((float)y + ((float)sy + 0.5f) / SS) / N * 2;
          float o[3] = {u * ext - d[0] * 4 + up[0] * v * ext, up[1] * v * ext - d[1] * 4, up[2] * v * ext - d[2] * 4};
          exit_acc a = {{0, 0, 0}, {0, 0, 0}};
          bool hit = false;
          for (int b = 0; b < 3; b++) {
            memcpy(a.band, band[b], sizeof a.band);
            hit |= jw_trace_cpu(c, o, d, NM[b], 6, stone_exit, &a);
          }
          if (!hit) continue;
          cover += 1;
          for (int k = 0; k < 3; k++) col[k] += a.acc[k] + c->body[k] * (cut == 5 ? 0.02f : 0.12f);
        }
      if (cover <= 0) continue;
      uint8_t *p = px + ((size_t)y * N + x) * 4;
      for (int k = 0; k < 3; k++) p[k] = (uint8_t)lroundf(tone(col[k] / cover) * 255);
      p[3] = (uint8_t)lroundf(cover / (SS * SS) * 255);
    }
  PIX[cut] = px;
  return px;
}

cw_texture gm_cardgem_texture(int cut) {
  if (cut < 0 || cut >= JW_NCUTS) return (cw_texture){0};
  if (!TEX[cut].id) TEX[cut] = cw_texture_from_pixels(GM_CARDGEM_PX, GM_CARDGEM_PX, gm_cardgem_pixels(cut), NULL);
  return TEX[cut];
}
