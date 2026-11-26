/* crokinole's painted textures, made in code at load (no image files): the playing surface (glued maple planks with
 * their grain, the stained rings, the inlaid lines and the walnut collar round the hole), the rail's walnut, the
 * table top and the ditch's felt. Deterministic (integer hashing), so every host paints the same board. */
#include "art.h"

#include <math.h>
#include <stdlib.h>

#include "crk.h"

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float rnd2(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static float vnoise(float x, float y, uint32_t seed) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = smooth(x - (float)xi), fy = smooth(y - (float)yi);
  float a = rnd2(xi, yi, seed), b = rnd2(xi + 1, yi, seed), c = rnd2(xi, yi + 1, seed), d = rnd2(xi + 1, yi + 1, seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y, uint32_t seed, int oct) {
  float s = 0, amp = 0.5f, f = 1;
  for (int i = 0; i < oct; i++) s += vnoise(x * f, y * f, seed + (uint32_t)i * 17u) * amp, amp *= 0.5f, f *= 2.03f;
  return s;
}
static uint8_t srgb(float v) {
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  return (uint8_t)(powf(v, 1.0f / 2.2f) * 255.0f + 0.5f);
}
static void put(uint8_t *o, float r, float g, float b) { o[0] = srgb(r), o[1] = srgb(g), o[2] = srgb(b), o[3] = 255; }

/* wood: long grain along u, with figure; base colour, how strong the grain lines are */
static void wood(float u, float v, uint32_t seed, const float base[3], float lines, float *out) {
  float warp = fbm(u * 1.5f, v * 9.0f, seed, 4) * 2.2f;
  float ring = v * 38.0f + warp * 3.0f + fbm(u * 0.4f, v * 3.0f, seed + 5, 3) * 6.0f;
  float g = 0.5f + 0.5f * sinf(ring * 6.2831853f);
  g = powf(g, 6.0f);                                          /* thin dark late-wood lines */
  float pores = fbm(u * 60.0f, v * 600.0f, seed + 9, 2);      /* fine streaks */
  float fig = fbm(u * 3.0f, v * 3.0f, seed + 3, 3);           /* broad figure */
  float k = 1.0f - lines * g - 0.10f * (pores - 0.5f) + 0.12f * (fig - 0.5f);
  out[0] = base[0] * k, out[1] = base[1] * k * 0.98f, out[2] = base[2] * k * 0.95f;
}

uint8_t *art_board(int n) {
  uint8_t *px = malloc((size_t)n * (size_t)n * 4);
  const float R = CRK_R_SURF;
  const float maple[3] = { 0.72f, 0.47f, 0.25f }; /* amber lacquer over maple */
  const float line_col[3] = { 0.05f, 0.03f, 0.02f };
  const float px_m = 2 * R / (float)n; /* metres per pixel */
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      float x = ((float)i + 0.5f) / (float)n * 2 * R - R, y = R - ((float)j + 0.5f) / (float)n * 2 * R;
      float r = sqrtf(x * x + y * y);
      /* planks 7 cm wide, each its own tint and grain offset */
      int plank = (int)floorf((x + R) / 0.07f);
      float tint = 0.92f + 0.14f * rnd2(plank, 3, 77u);
      float base[3] = { maple[0] * tint, maple[1] * tint, maple[2] * (0.95f + 0.1f * rnd2(plank, 5, 78u)) };
      float c[3];
      wood(y * 1.3f + (float)plank * 3.1f, x * 1.1f + (float)plank * 0.37f, 100u + (uint32_t)plank, base, 0.22f, c);
      /* the rings: the 10 band a touch lighter, the 5 band stained a little deeper, the 15 natural */
      if (r > CRK_R_15 && r < CRK_R_10) c[0] *= 1.05f, c[1] *= 1.05f, c[2] *= 1.06f;
      else if (r >= CRK_R_10 && r < CRK_R_5) c[0] *= 0.86f, c[1] *= 0.80f, c[2] *= 0.74f;
      else if (r >= CRK_R_5) c[0] *= 0.80f, c[1] *= 0.72f, c[2] *= 0.64f;
      /* the walnut collar round the hole */
      if (r < CRK_R_HOLE + 0.010f) {
        float w[3];
        const float walnut[3] = { 0.22f, 0.12f, 0.06f };
        wood(r * 20.0f, crk_atan2(y, x) * 0.2f, 31u, walnut, 0.25f, w);
        c[0] = w[0], c[1] = w[1], c[2] = w[2];
      }
      /* the inlaid lines: the 15, 10 and shooting lines, and the quarter lines between the 15 and the shooting line */
      float lw = 0.0016f;
      float on = 0;
      const float rings[3] = { CRK_R_15, CRK_R_10, CRK_R_5 };
      for (int k = 0; k < 3; k++) {
        float d = fabsf(r - rings[k]) - lw * (k == 2 ? 1.25f : 1.0f) * 0.5f;
        float a = 1.0f - d / px_m;
        if (a > on) on = a;
      }
      if (r > CRK_R_15 && r < CRK_R_5) {
        float ax = fabsf(x), ay = fabsf(y);
        float d = fabsf(ax - ay) * 0.70710678f - lw * 0.5f; /* distance to the diagonals */
        float a = 1.0f - d / px_m;
        if (a > on) on = a;
      }
      on = on < 0 ? 0 : on > 1 ? 1 : on;
      for (int k = 0; k < 3; k++) c[k] = c[k] * (1 - on) + line_col[k] * on;
      /* the hole itself is dark (the geometry cuts it; this is what shows at its rim) */
      if (r < CRK_R_HOLE) c[0] = c[1] = c[2] = 0.02f;
      put(px + ((size_t)j * (size_t)n + (size_t)i) * 4, c[0], c[1], c[2]);
    }
  return px;
}

uint8_t *art_walnut(int n) {
  uint8_t *px = malloc((size_t)n * (size_t)n * 4);
  const float base[3] = { 0.20f, 0.10f, 0.05f };
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      float c[3];
      wood((float)i / (float)n * 4.0f, (float)j / (float)n * 1.0f, 55u, base, 0.35f, c);
      put(px + ((size_t)j * (size_t)n + (size_t)i) * 4, c[0], c[1], c[2]);
    }
  return px;
}

uint8_t *art_table(int n) {
  uint8_t *px = malloc((size_t)n * (size_t)n * 4);
  const float base[3] = { 0.10f, 0.055f, 0.032f };
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      float c[3];
      int plank = j * 6 / n;
      wood((float)i / (float)n * 3.0f + (float)plank * 1.7f, (float)j / (float)n * 1.5f, 200u + (uint32_t)plank, base, 0.3f, c);
      float seam = fabsf(fmodf((float)j * 6.0f / (float)n, 1.0f) - 0.5f);
      if (seam > 0.495f) c[0] *= 0.4f, c[1] *= 0.4f, c[2] *= 0.4f;
      put(px + ((size_t)j * (size_t)n + (size_t)i) * 4, c[0], c[1], c[2]);
    }
  return px;
}

uint8_t *art_felt(int n) {
  uint8_t *px = malloc((size_t)n * (size_t)n * 4);
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      float f = 0.85f + 0.3f * (fbm((float)i * 0.35f, (float)j * 0.35f, 401u, 3) - 0.5f) + 0.12f * (rnd2(i, j, 402u) - 0.5f);
      put(px + ((size_t)j * (size_t)n + (size_t)i) * 4, 0.025f * f, 0.075f * f, 0.05f * f);
    }
  return px;
}

/* the disc's top: a faint turned groove ring and end grain, light enough to tint by the disc's colour */
uint8_t *art_disc_top(int n) {
  uint8_t *px = malloc((size_t)n * (size_t)n * 4);
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      float x = ((float)i + 0.5f) / (float)n * 2 - 1, y = ((float)j + 0.5f) / (float)n * 2 - 1;
      float r = sqrtf(x * x + y * y);
      float k = 0.92f + 0.08f * sinf(r * 70.0f + fbm(x * 3, y * 3, 900u, 3) * 8.0f);
      if (fabsf(r - 0.66f) < 0.035f) k *= 0.55f;             /* the turned groove */
      if (fabsf(r - 0.66f) < 0.06f && r > 0.66f) k *= 1.08f; /* its lit lip */
      if (r > 0.93f) k *= 0.85f;                             /* the bevel */
      put(px + ((size_t)j * (size_t)n + (size_t)i) * 4, k, k, k);
    }
  return px;
}
