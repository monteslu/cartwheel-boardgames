/* Facet: the stones' shader sources, GLSL ES 3.00 and WGSL side by side (gemshaders.h). Taken from the jewels game in
 * even-more-cartwheel-examples (same author, MIT): its studio, its spectral fire and its cuts' hulls, with one change:
 * a pixel's ray comes from the table's perspective camera (u_rx, u_ry, u_rz: the direction as a linear function of
 * the pixel) instead of jewels' fixed camera over its tray. Tray coordinates: x right, y away from the players, z up
 * off the table, so the studio's sky stays overhead. */
#include "gemshaders.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* ── a growing string ─────────────────────────────────────────────────── */

typedef struct sb { char *s; size_t n, cap; } sb;
static void put(sb *b, const char *fmt, ...) {
  va_list ap;
  for (;;) {
    va_start(ap, fmt);
    size_t room = b->cap - b->n;
    int k = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? room : 0, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if (b->s && (size_t)k < room) {
      b->n += (size_t)k;
      return;
    }
    b->cap = (b->cap + (size_t)k + 1) * 2;
    b->s = realloc(b->s, b->cap);
    if (!b->n) b->s[0] = 0;
  }
}
static char *done(sb *b) { return b->s; }


/* ── the studio ───────────────────────────────────────────────────────── *
 * A dark room with soft boxes for brilliance, thin strips for the long flashes of the step cuts, and small hot
 * points for fire; straight overhead (where the viewer stands) it is dark, which is what draws the dark half of a
 * cut stone's contrast pattern. World directions: x right, y up the screen, z toward the viewer. */
typedef struct box_light { float a[3], hx, hy, i[3]; } box_light;
typedef struct pin_light { float a[3], r, i[3]; } pin_light;
static const box_light boxes[] = {
  { { -0.45f, 0.54f, 0.72f }, 0.30f, 0.22f, { 5.0f, 4.7f, 4.2f } },   /* the key, upper left, warm */
  { { 0.70f, 0.10f, 0.70f }, 0.16f, 0.36f, { 1.6f, 1.8f, 2.2f } },    /* the fill, right, cool */
  { { 0.00f, -0.80f, 0.60f }, 0.60f, 0.025f, { 2.0f, 1.8f, 1.5f } },  /* a strip below */
  { { -0.88f, -0.15f, 0.45f }, 0.025f, 0.55f, { 2.0f, 1.8f, 1.5f } }, /* a strip to the left */
  { { 0.25f, 0.88f, 0.40f }, 0.50f, 0.025f, { 1.8f, 1.8f, 1.9f } },    /* a strip above */
  /* the ceiling: six panels in a ring round the viewer, with dark gaps between, where the light a well cut stone
   * sends back up comes from (brilliance, and the contrast of the gaps) */
  { { 0.4532f, 0.1214f, 0.8829f }, 0.18f, 0.19f, { 3.4f, 3.2f, 2.9f } },
  { { 0.1214f, 0.4532f, 0.8829f }, 0.18f, 0.19f, { 2.5f, 2.5f, 2.6f } },
  { { -0.3318f, 0.3318f, 0.8829f }, 0.18f, 0.19f, { 3.6f, 3.4f, 3.0f } },
  { { -0.4532f, -0.1214f, 0.8829f }, 0.18f, 0.19f, { 2.4f, 2.4f, 2.5f } },
  { { -0.1214f, -0.4532f, 0.8829f }, 0.18f, 0.19f, { 3.3f, 3.1f, 2.8f } },
  { { 0.3318f, -0.3318f, 0.8829f }, 0.18f, 0.19f, { 2.5f, 2.5f, 2.6f } },
  /* and an inner ring of small lamps close round the viewer: the bright arrows under a brilliant's table */
  { { 0.2078f, 0.0861f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { 0.0861f, 0.2078f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
  { { -0.0861f, 0.2078f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { -0.2078f, 0.0861f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
  { { -0.2078f, -0.0861f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { -0.0861f, -0.2078f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
  { { 0.0861f, -0.2078f, 0.9744f }, 0.06f, 0.05f, { 3.0f, 2.9f, 2.7f } }, { { 0.2078f, -0.0861f, 0.9744f }, 0.06f, 0.05f, { 2.6f, 2.6f, 2.7f } },
};
#define NPIN 320
static pin_light pins[NPIN];
/* small hot lamps on a golden-angle spiral from 6 to 52 degrees off the zenith: as a stone rocks, its facets sweep
 * across them, and each one split by dispersion is a fleck of fire */
static void make_pins(void) {
  for (int i = 0; i < NPIN; i++) {
    float th = (6.0f + 46.0f * sqrtf(((float)i + 0.5f) / (float)NPIN)) * 3.14159265f / 180.0f, ph = (float)i * 2.39996323f + 0.4f;
    pins[i].a[0] = sinf(th) * cosf(ph), pins[i].a[1] = sinf(th) * sinf(ph), pins[i].a[2] = cosf(th);
    pins[i].r = 0.0035f + 0.0025f * (float)((i * 7) % 5) / 4.0f;
    float k = 100.0f + 100.0f * (float)((i * 3) % 4) / 3.0f, warm = (float)(i % 3) * 0.06f;
    pins[i].i[0] = k, pins[i].i[1] = k * (0.97f - warm * 0.5f), pins[i].i[2] = k * (0.93f - warm);
  }
}
#define NBOX ((int)(sizeof boxes / sizeof boxes[0]))

static void norm3(const float *a, float *o) {
  float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  o[0] = a[0] / l, o[1] = a[1] / l, o[2] = a[2] / l;
}

static float sstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static void box_frame(const box_light *bx, float a[3], float t1[3], float t2[3]) {
  norm3(bx->a, a);
  float z[3] = { 0, 0, 1 }, c1[3] = { a[1] * z[2] - a[2] * z[1], a[2] * z[0] - a[0] * z[2], a[0] * z[1] - a[1] * z[0] };
  norm3(c1, t1);
  float c2[3] = { t1[1] * a[2] - t1[2] * a[1], t1[2] * a[0] - t1[0] * a[2], t1[0] * a[1] - t1[1] * a[0] };
  norm3(c2, t2);
}
static void studio_c(const float d[3], float out[3]) {
  float up = d[2];
  if (up < 0) {
    out[0] = 0.004f, out[1] = 0.0042f, out[2] = 0.006f;
    return;
  }
  float m = sstep(-0.05f, 0.6f, up), g = expf(-powf((up - 0.16f) / 0.10f, 2.0f)), z = 1 - sstep(0.984f, 0.9935f, up);
  out[0] = (0.010f + (0.022f - 0.010f) * m + 0.045f * g) * z;
  out[1] = (0.0085f + (0.019f - 0.0085f) * m + 0.036f * g) * z;
  out[2] = (0.0075f + (0.017f - 0.0075f) * m + 0.028f * g) * z;
  for (int i = 0; i < NBOX; i++) {
    float a[3], t1[3], t2[3];
    box_frame(&boxes[i], a, t1, t2);
    float ca = d[0] * a[0] + d[1] * a[1] + d[2] * a[2];
    if (ca <= 0) continue;
    float q0 = (d[0] * t1[0] + d[1] * t1[1] + d[2] * t1[2]) / ca, q1 = (d[0] * t2[0] + d[1] * t2[1] + d[2] * t2[2]) / ca;
    float e = fmaxf(fabsf(q0) - boxes[i].hx, fabsf(q1) - boxes[i].hy);
    float k = 1 - sstep(-0.012f, 0.012f, e);
    if (k <= 0) continue;
    float r0 = q0 / boxes[i].hx, r1 = q1 / boxes[i].hy;
    k *= 1 - 0.15f * fminf(r0 * r0 + r1 * r1, 2.0f);
    for (int j = 0; j < 3; j++) out[j] += boxes[i].i[j] * k * 1.3f; /* the stones see the boxes a little hotter than the tray does */
  }
}
void gm_studio_rows(float *img, int row0, int nrows) {
  const int N = GM_STUDIO_N;
  make_pins();
  for (int row = row0; row < row0 + nrows && row < N + 2; row++) {
    float *px = img + (size_t)row * N * 4;
    if (row >= N) { /* the lamps */
      for (int i = 0; i < N * 4; i++) px[i] = 0;
      for (int i = 0; i < NPIN; i++) {
        float a[3];
        norm3(pins[i].a, a);
        if (row == N) px[i * 4] = a[0], px[i * 4 + 1] = a[1], px[i * 4 + 2] = a[2], px[i * 4 + 3] = pins[i].r;
        else px[i * 4] = pins[i].i[0], px[i * 4 + 1] = pins[i].i[1], px[i * 4 + 2] = pins[i].i[2];
      }
      continue;
    }
    for (int col = 0; col < N; col++) {
      float sx = (float)col / (float)(N - 1) * 2 - 1, sy = (float)row / (float)(N - 1) * 2 - 1;
      float p0 = (sx + sy) * 0.5f, p1 = (sx - sy) * 0.5f, z = fmaxf(1 - fabsf(p0) - fabsf(p1), 0);
      float d[3] = { p0, p1, z };
      norm3(d, d);
      studio_c(d, px + col * 4);
      float best = -2;
      int bi = -1;
      if (d[2] > 0.55f)
        for (int i = 0; i < NPIN; i++) {
          float a[3];
          norm3(pins[i].a, a);
          float c = d[0] * a[0] + d[1] * a[1] + d[2] * a[2];
          if (c > best) best = c, bi = i;
        }
      px[col * 4 + 3] = (float)bi;
    }
  }
}

static void gem_env_fns(sb *b, bool w) {
  if (w)
    put(b, "fn drift(d0: vec3f) -> vec3f { let ca = cos(u.u_cam.w); let sa = sin(u.u_cam.w); return vec3f(ca * d0.x - sa * d0.y, sa * d0.x + ca * d0.y, d0.z); }\n"
           "const SN = %d.0;\nconst SH = %d.0;\n"
           "fn studioT(d: vec3f) -> vec4f {\n"
           "  if (d.z < 0.0) { return vec4f(0.004, 0.0042, 0.006, -1.0); }\n"
           "  let p = d.xy / (abs(d.x) + abs(d.y) + d.z);\n"
           "  let s = (vec2f(p.x + p.y, p.x - p.y) * 0.5 + 0.5) * (SN - 1.0);\n"
           "  let c = textureSampleLevel(u_t0, cw_linear, (s + 0.5) / vec2f(SN, SH), 0.0).rgb;\n"
           "  let id = textureLoad(u_t0, vec2i(s + 0.5), 0).a;\n"
           "  return vec4f(c, id);\n"
           "}\n"
           "fn pinMono(d: vec3f, id: f32, bl: f32) -> vec3f {\n"
           "  if (id < -0.5) { return vec3f(0.0); }\n"
           "  let pa = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0);\n"
           "  let pc = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0).rgb;\n"
           "  let r = pa.w + bl;\n"
           "  return pc * (pa.w * pa.w / (r * r)) * smoothstep(cos(r + 0.006), cos(r * 0.4), dot(d, pa.xyz));\n"
           "}\n"
           "fn lamOf(t: f32) -> f32 { return 1.0 / sqrt(1.0 / (400.0 * 400.0) + t * (1.0 / (700.0 * 700.0) - 1.0 / (400.0 * 400.0))); }\n"
           "fn pinDisp(e0: vec3f, e1: vec3f, id: f32, bl: f32) -> vec3f {\n"
           "  if (id < -0.5) { return vec3f(0.0); }\n"
           "  let pa = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0);\n"
           "  let rr = pa.w + bl; let R = rr + 0.006;\n"
           "  let seg = e1 - e0; let sl2 = max(dot(seg, seg), 1e-14);\n"
           "  let t = clamp(dot(pa.xyz - e0, seg) / sl2, 0.0, 1.0);\n"
           "  let d = length(e0 + seg * t - pa.xyz);\n"
           "  if (d >= R) { return vec3f(0.0); }\n"
           "  let pc = textureLoad(u_t0, vec2i(i32(id + 0.5), %d), 0).rgb;\n"
           "  let fr = min(1.0, 2.0 * rr / sqrt(sl2));\n"
           "  return pc * (pa.w * pa.w / (rr * rr)) * (1.0 - smoothstep(rr * 0.4, R, d)) * fr * mix(spec(lamOf(t)), vec3f(1.0), fr);\n"
           "}\n",
        GM_STUDIO_N, GM_STUDIO_N + 2, GM_STUDIO_N, GM_STUDIO_N + 1, GM_STUDIO_N, GM_STUDIO_N + 1);
  else
    put(b, "vec3 drift(vec3 d0) { float ca = cos(u_cam.w), sa = sin(u_cam.w); return vec3(ca * d0.x - sa * d0.y, sa * d0.x + ca * d0.y, d0.z); }\n"
           "uniform sampler2D u_t0;\n"
           "const float SN = %d.0, SH = %d.0;\n"
           "vec4 studioT(vec3 d) {\n"
           "  if (d.z < 0.0) return vec4(0.004, 0.0042, 0.006, -1.0);\n"
           "  vec2 p = d.xy / (abs(d.x) + abs(d.y) + d.z);\n"
           "  vec2 s = (vec2(p.x + p.y, p.x - p.y) * 0.5 + 0.5) * (SN - 1.0);\n"
           "  vec3 c = texture(u_t0, (s + 0.5) / vec2(SN, SH)).rgb;\n"
           "  float id = texelFetch(u_t0, ivec2(s + 0.5), 0).a;\n"
           "  return vec4(c, id);\n"
           "}\n"
           "vec3 pinMono(vec3 d, float id, float bl) {\n"
           "  if (id < -0.5) return vec3(0.0);\n"
           "  vec4 pa = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0);\n"
           "  vec3 pc = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0).rgb;\n"
           "  float r = pa.w + bl;\n"
           "  return pc * (pa.w * pa.w / (r * r)) * smoothstep(cos(r + 0.006), cos(r * 0.4), dot(d, pa.xyz));\n"
           "}\n"
           "float lamOf(float t) { return 1.0 / sqrt(1.0 / (400.0 * 400.0) + t * (1.0 / (700.0 * 700.0) - 1.0 / (400.0 * 400.0))); }\n"
           "vec3 pinDisp(vec3 e0, vec3 e1, float id, float bl) {\n"
           "  if (id < -0.5) return vec3(0.0);\n"
           "  vec4 pa = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0);\n"
           "  float rr = pa.w + bl, R = rr + 0.006;\n"
           "  vec3 seg = e1 - e0; float sl2 = max(dot(seg, seg), 1e-14);\n"
           "  float t = clamp(dot(pa.xyz - e0, seg) / sl2, 0.0, 1.0);\n"
           "  float d = length(e0 + seg * t - pa.xyz);\n"
           "  if (d >= R) return vec3(0.0);\n"
           "  vec3 pc = texelFetch(u_t0, ivec2(int(id + 0.5), %d), 0).rgb;\n"
           "  float fr = min(1.0, 2.0 * rr / sqrt(sl2));\n"
           "  return pc * (pa.w * pa.w / (rr * rr)) * (1.0 - smoothstep(rr * 0.4, R, d)) * fr * mix(spec(lamOf(t)), vec3(1.0), fr);\n"
           "}\n",
        GM_STUDIO_N, GM_STUDIO_N + 2, GM_STUDIO_N, GM_STUDIO_N + 1, GM_STUDIO_N, GM_STUDIO_N + 1);
}

/* the CIE 1931 observer (an analytic multi-lobe fit) to linear sRGB, scaled so 400-700 nm averages to white */
static void spectrum_fn(sb *b, bool w) {
  float k[3] = { 0 };
  {
    float s[3];
    jw_spectrum_rgb(550, s); /* fills the normalisation */
    float raw[3];
    (void)raw;
  }
  /* the scale: the same as cuts.c (recomputed here so the shader stands alone) */
  {
    double sum[3] = { 0 };
    int n = 0;
    for (float l = 400; l <= 700; l += 0.5f, n++) {
      float s[3];
      jw_spectrum_rgb(l, s);
      for (int i = 0; i < 3; i++) sum[i] += s[i];
    }
    /* jw_spectrum_rgb is already scaled; the shader's raw fit needs the scale itself */
    float raw_sum[3] = { 0 };
    for (float l = 400; l <= 700; l += 0.5f) {
      float lob[8];
      float mu[] = { 599.8f, 442.0f, 501.1f, 568.8f, 530.9f, 437.0f, 459.0f };
      float s1[] = { 37.9f, 16.0f, 20.4f, 46.9f, 16.3f, 11.8f, 26.0f }, s2[] = { 31.0f, 26.7f, 26.2f, 40.5f, 31.1f, 36.0f, 13.8f };
      for (int i = 0; i < 7; i++) {
        float t = (l - mu[i]) / (l < mu[i] ? s1[i] : s2[i]);
        lob[i] = expf(-0.5f * t * t);
      }
      float X = 1.056f * lob[0] + 0.362f * lob[1] - 0.065f * lob[2], Y = 0.821f * lob[3] + 0.286f * lob[4], Z = 1.217f * lob[5] + 0.681f * lob[6];
      raw_sum[0] += fmaxf(0, 3.2406f * X - 1.5372f * Y - 0.4986f * Z);
      raw_sum[1] += fmaxf(0, -0.9689f * X + 1.8758f * Y + 0.0415f * Z);
      raw_sum[2] += fmaxf(0, 0.0557f * X - 0.2040f * Y + 1.0570f * Z);
    }
    for (int i = 0; i < 3; i++) k[i] = (float)n / raw_sum[i];
  }
  if (w)
    put(b, "fn lobe(x: f32, m: f32, s1: f32, s2: f32) -> f32 { let t = (x - m) / select(s2, s1, x < m); return exp(-0.5 * t * t); }\n"
           "fn spec(l: f32) -> vec3f {\n"
           "  let X = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);\n"
           "  let Y = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);\n"
           "  let Z = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);\n"
           "  let c = max(vec3f(3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z), vec3f(0.0));\n"
           "  return c * vec3f(%.6f, %.6f, %.6f);\n}\n",
        k[0], k[1], k[2]);
  else
    put(b, "float lobe(float x, float m, float s1, float s2) { float t = (x - m) / (x < m ? s1 : s2); return exp(-0.5 * t * t); }\n"
           "vec3 spec(float l) {\n"
           "  float X = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);\n"
           "  float Y = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);\n"
           "  float Z = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);\n"
           "  vec3 c = max(vec3(3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z), 0.0);\n"
           "  return c * vec3(%.6f, %.6f, %.6f);\n}\n",
        k[0], k[1], k[2]);
}


/* the canvas and the tray: shared by every program */
static const char *GL_COMMON =
    "uniform vec4 u_view;\n"
    "uniform vec4 u_cam;\n"
    "uniform vec4 u_tint;\n"
    "vec2 canvasOf(vec2 fc) { return vec2(fc.x * 1920.0 * u_view.z, 1080.0 - fc.y * 1080.0 * u_view.w); }\n"
    "vec3 trayPoint(vec2 c) { return vec3(c.x - 960.0, 540.0 - c.y, 0.0); }\n"
    "vec4 canvasClip(vec2 p) { return vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0); }\n"
    "vec2 corner(int c) { return c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0); }\n";
/* WGSL: the uniform block's head (each program appends its own members and closes it) */
static const char *WG_COMMON_FNS =
    "fn canvasOf(fc: vec2f) -> vec2f { return vec2f(fc.x * 1920.0 * u.u_view.z, 1080.0 - fc.y * 1080.0 * u.u_view.w); }\n"
    "fn trayPoint(c: vec2f) -> vec3f { return vec3f(c.x - 960.0, 540.0 - c.y, 0.0); }\n"
    "fn canvasClip(p: vec2f) -> vec4f { return vec4f(p.x / 960.0 - 1.0, (1.0 - p.y / 540.0) * cw_target.flip, 0.5, 1.0); }\n"
    "fn corner(c: i32) -> vec2f { if (c == 1) { return vec2f(1.0, 0.0); } if (c == 2 || c == 4) { return vec2f(1.0); } if (c == 5) { return vec2f(0.0, 1.0); } return vec2f(0.0); }\n";


/* the WGSL (from jewels shaders_wgsl.h) */
/* the uniform block's members every program starts with */
#define WG_U_HEAD "struct U {\n  u_view: vec4f,\n  u_cam: vec4f,\n  u_tint: vec4f,\n"
#define WG_U_TAIL "};\n@group(0) @binding(0) var<uniform> u: U;\n"
static const char *WG_GEM_U = WG_U_HEAD
    "  u_inv: mat4x4f,\n  u_rot: mat4x4f,\n  u_rect: vec4f,\n  u_abs: vec4f,\n  u_ior: vec4f,\n  u_q: vec4f,\n"
    "  u_body: vec4f,\n  u_jit: vec4f,\n  u_gl0: vec4f, u_gl1: vec4f, u_gl2: vec4f, u_gl3: vec4f,\n"
    "  u_gk0: vec4f, u_gk1: vec4f, u_gk2: vec4f, u_gk3: vec4f,\n  u_gx: vec4f,\n  u_rx: vec4f,\n  u_ry: vec4f,\n  u_rz: vec4f,\n" WG_U_TAIL
    "@group(0) @binding(1) var u_t0: texture_2d<f32>;\n";

static const char *WG_GEM =
    "@vertex fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {\n"
    "  return canvasClip(mix(u.u_rect.xy, u.u_rect.zw, corner(i32(vi) % 6)));\n"
    "}\n"
    "fn rot3() -> mat3x3f { return mat3x3f(u.u_rot[0].xyz, u.u_rot[1].xyz, u.u_rot[2].xyz); }\n"
    "fn lampMono(d: vec3f, gl: vec4f, gk: vec4f) -> vec3f {\n"
    "  let c = dot(d, gl.xyz);\n"
    "  let core = smoothstep(cos(gl.w * 1.3), cos(gl.w * 0.5), c);\n"
    "  let glow = smoothstep(cos(0.20), cos(0.08), c);\n"
    "  return gk.xyz * (core * LAMP_CORE + glow * LAMP_GLOW + pow(max(c, 0.0), 4.0) * LAMP_HALO);\n"
    "}\n"
    "fn lampDisp(e0: vec3f, e1: vec3f, em: vec3f, gl: vec4f, gk: vec4f) -> vec3f {\n"
    "  let seg = e1 - e0; let sl2 = max(dot(seg, seg), 1e-14);\n"
    "  let t = clamp(dot(gl.xyz - e0, seg) / sl2, 0.0, 1.0);\n"
    "  let d = length(e0 + seg * t - gl.xyz);\n"
    "  let core = 1.0 - smoothstep(gl.w * 0.5, gl.w * 1.3, d);\n"
    "  let fr = min(1.0, 2.0 * gl.w / sqrt(sl2));\n"
    "  let c = dot(em, gl.xyz);\n"
    "  let glow = smoothstep(cos(0.20), cos(0.08), c);\n"
    "  return gk.xyz * (core * fr * mix(spec(lamOf(t)), vec3f(1.0), fr) * LAMP_CORE + glow * LAMP_GLOW + pow(max(c, 0.0), 4.0) * LAMP_HALO);\n"
    "}\n"
    "fn lampsMono(d: vec3f) -> vec3f {\n"
    "  var c = vec3f(0.0);\n"
    "  if (u.u_gk0.w > 0.5) { c += lampMono(d, u.u_gl0, u.u_gk0); }\n"
    "  if (u.u_gk1.w > 0.5) { c += lampMono(d, u.u_gl1, u.u_gk1); }\n"
    "  if (u.u_gk2.w > 0.5) { c += lampMono(d, u.u_gl2, u.u_gk2); }\n"
    "  if (u.u_gk3.w > 0.5) { c += lampMono(d, u.u_gl3, u.u_gk3); }\n"
    "  return c;\n"
    "}\n"
    "fn lampsDisp(e0: vec3f, e1: vec3f, em: vec3f) -> vec3f {\n"
    "  var c = vec3f(0.0);\n"
    "  if (u.u_gk0.w > 0.5) { c += lampDisp(e0, e1, em, u.u_gl0, u.u_gk0); }\n"
    "  if (u.u_gk1.w > 0.5) { c += lampMono(em, u.u_gl1, u.u_gk1); }\n"
    "  if (u.u_gk2.w > 0.5) { c += lampMono(em, u.u_gl2, u.u_gk2); }\n"
    "  if (u.u_gk3.w > 0.5) { c += lampMono(em, u.u_gl3, u.u_gk3); }\n"
    "  return c;\n"
    "}\n"
    "fn toR(dl: vec3f) -> vec3f { return normalize(rot3() * dl); }\n"
    "fn envMono(dl: vec3f) -> vec3f { let dr = toR(dl); let dw = drift(dr); let s = studioT(dw); return (s.rgb + pinMono(dw, s.a, u.u_ior.w)) * u.u_tint.xyz + lampsMono(dr); }\n"
    "fn envDisp(e0: vec3f, e1: vec3f, em: vec3f) -> vec3f {\n"
    "  let r0 = toR(e0); let r1 = toR(e1); let rm = toR(em); let dm = drift(rm); let s = studioT(dm);\n"
    "  return (s.rgb + pinDisp(drift(r0), drift(r1), s.a, u.u_ior.w)) * u.u_tint.xyz + lampsDisp(r0, r1, rm);\n"
    "}\n"
    "fn fres(ci: f32, n1: f32, n2: f32) -> f32 {\n"
    "  let st = n1 / n2 * sqrt(max(0.0, 1.0 - ci * ci));\n"
    "  if (st >= 1.0) { return 1.0; }\n"
    "  let ct = sqrt(1.0 - st * st);\n"
    "  let rs = (n1 * ci - n2 * ct) / (n1 * ci + n2 * ct); let rp = (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct);\n"
    "  return 0.5 * (rs * rs + rp * rp);\n"
    "}\n"
    "fn shade(p0: vec3f, d: vec3f, nn: vec3f, NB: i32, fire: bool) -> vec3f {\n"
    "  let n = u.u_ior.x + u.u_ior.y / (560.0 * 560.0);\n"
    "  let n0 = u.u_ior.x + u.u_ior.y / (400.0 * 400.0); let n1 = u.u_ior.x + u.u_ior.y / (700.0 * 700.0);\n"
    "  let ci = -dot(nn, d);\n"
    "  let F = fres(ci, 1.0, n);\n"
    "  var col = F * envMono(reflect(d, nn));\n"
    "  var t = refract(d, nn, 1.0 / n);\n"
    "  var p = p0;\n"
    "  var thr = 1.0 - F; var path = 0.0;\n"
    "  for (var b = 0; b < 12; b++) {\n"
    "    if (b >= NB || thr < 0.03) { break; }\n"
    "    let h = hullOut(p, t);\n"
    "    let ne = h.n;\n"
    "    let te = max(h.t, 0.0);\n"
    "    p += t * te; path += te;\n"
    "    let ce = dot(t, ne);\n"
    "    if (n * n * (1.0 - ce * ce) < 1.0) {\n"
    "      let Fe = fres(ce, n, 1.0);\n"
    "      let T = exp(-u.u_abs.xyz * path) * (thr * (1.0 - Fe));\n"
    "      let em = refract(t, -ne, n);\n"
    "      if (fire) {\n"
    "        var e0 = refract(t, -ne, n0); var e1 = refract(t, -ne, n1);\n"
    "        if (dot(e0, e0) < 0.5) { e0 = em; }\n"
    "        if (dot(e1, e1) < 0.5) { e1 = em; }\n"
    "        col += T * envDisp(e0, e1, em);\n"
    "      } else { col += T * envMono(em); }\n"
    "      thr *= Fe;\n"
    "    }\n"
    "    t = reflect(t, ne);\n"
    "  }\n"
    "  if (u.u_q.w > 0.5) { col += vec3f(thr * 3.0, 0.0, thr * 3.0); }\n"
    "  else { col += thr * exp(-u.u_abs.xyz * path) * envMono(t) * 0.5; }\n"
    "  return col;\n"
    "}\n"
    "@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
    "  let fc = cw_frag_coord(pos) + u.u_jit.xy;\n"
    "  let S = i32(u.u_q.x); let NB = i32(u.u_q.y);\n"
    "  let fire = u.u_q.z > 0.5;\n"
    "  var acc = vec3f(0.0); var cover = 0.0;\n"
    "  let ro = u.u_cam.xyz;\n"
    "  let ol = (u.u_inv * vec4f(ro, 1.0)).xyz;\n"
    "  let inv3 = mat3x3f(u.u_inv[0].xyz, u.u_inv[1].xyz, u.u_inv[2].xyz);\n"
    "  for (var k = 0; k < 4; k++) {\n"
    "    if (k >= S) { break; }\n"
    "    var off = vec2f(0.0);\n"
    "    if (S == 2) { off = select(vec2f(0.25, -0.25), vec2f(-0.25, 0.25), k == 0); }\n"
    "    if (S >= 3) { var o4 = array<vec2f, 4>(vec2f(-0.375, 0.125), vec2f(0.125, 0.375), vec2f(0.375, -0.125), vec2f(-0.125, -0.375)); off = o4[k]; }\n"
    "    let q = fc + off;\n"
    "    let rd = normalize(u.u_rx.xyz * q.x + u.u_ry.xyz * q.y + u.u_rz.xyz);\n"
    "    let dl = normalize(inv3 * rd);\n"
    "    let hi = hullIn(ol, dl);\n"
    "    if (!hi.ok) { continue; }\n"
    "    cover += 1.0;\n"
    "    acc += shade(ol + dl * hi.tn, dl, hi.n, NB, fire);\n"
    "  }\n"
    "  if (cover <= 0.0) { discard; }\n"
    "  var col = acc / cover;\n"
    "  col += u.u_body.xyz * u.u_body.w + u.u_gx.xyz;\n"
    "  let g = u.u_ior.z;\n"
    "  col = col * (1.0 + 1.6 * g) + u.u_body.xyz * g * 0.3;\n"
    "  let a = cover / f32(S) * u.u_abs.w;\n"
    "  return vec4f(col * a, a);\n"
    "}\n";



static void hull_fns(sb *b, const jw_cut *c, bool w) {
  if (w)
    put(b, "struct HitIn { ok: bool, tn: f32, n: vec3f };\n"
           "fn hullIn(o: vec3f, d: vec3f) -> HitIn {\n"
           "  var tn = -1e30; var tf = 1e30; var nn = vec3f(0.0, 0.0, 1.0); var den: f32; var t: f32;\n");
  else
    put(b, "bool hullIn(vec3 o, vec3 d, out float tn, out vec3 nn) {\n"
           "  tn = -1e30; float tf = 1e30; nn = vec3(0.0, 0.0, 1.0); float den, t;\n");
  const char *v3 = w ? "vec3f" : "vec3";
  for (int i = 0; i < c->nplanes; i++) {
    const jw_plane *p = &c->p[i];
    put(b, "  den = dot(d, %s(%.7f, %.7f, %.7f)); t = (%.7f - dot(o, %s(%.7f, %.7f, %.7f))) / den;\n", v3, p->n[0], p->n[1], p->n[2], p->d, v3, p->n[0],
        p->n[1], p->n[2]);
    if (w) put(b, "  if (den < 0.0) { if (t > tn) { tn = t; nn = %s(%.7f, %.7f, %.7f); } } else { tf = min(tf, t); }\n", v3, p->n[0], p->n[1], p->n[2]);
    else put(b, "  if (den < 0.0) { if (t > tn) { tn = t; nn = %s(%.7f, %.7f, %.7f); } } else tf = min(tf, t);\n", v3, p->n[0], p->n[1], p->n[2]);
  }
  if (w) put(b, "  var r: HitIn; r.ok = tn <= tf && tf > 0.0 && tn > -1e29; r.tn = tn; r.n = nn; return r;\n}\n");
  else put(b, "  return tn <= tf && tf > 0.0 && tn > -1e29;\n}\n");
  if (w)
    put(b, "struct HitOut { t: f32, n: vec3f };\n"
           "fn hullOut(p: vec3f, d: vec3f) -> HitOut {\n"
           "  var te = 1e30; var n = vec3f(0.0, 0.0, 1.0); var den: f32; var t: f32;\n");
  else
    put(b, "float hullOut(vec3 p, vec3 d, out vec3 n) {\n"
           "  float te = 1e30; n = vec3(0.0, 0.0, 1.0); float den, t;\n");
  for (int i = 0; i < c->nplanes; i++) {
    const jw_plane *p = &c->p[i];
    if (w)
      put(b, "  den = dot(d, %s(%.7f, %.7f, %.7f)); t = select(1e30, (%.7f - dot(p, %s(%.7f, %.7f, %.7f))) / den, den > 1e-7); if (t < te) { te = t; n = %s(%.7f, %.7f, %.7f); }\n",
          v3, p->n[0], p->n[1], p->n[2], p->d, v3, p->n[0], p->n[1], p->n[2], v3, p->n[0], p->n[1], p->n[2]);
    else
      put(b, "  den = dot(d, %s(%.7f, %.7f, %.7f)); t = den > 1e-7 ? (%.7f - dot(p, %s(%.7f, %.7f, %.7f))) / den : 1e30; if (t < te) { te = t; n = %s(%.7f, %.7f, %.7f); }\n",
          v3, p->n[0], p->n[1], p->n[2], p->d, v3, p->n[0], p->n[1], p->n[2], v3, p->n[0], p->n[1], p->n[2]);
  }
  if (w) put(b, "  var r: HitOut; r.t = te; r.n = n; return r;\n}\n");
  else put(b, "  return te;\n}\n");
}

static const char *GEM_VS_GL =
    "uniform vec4 u_rect;\n"
    "vec4 canvasClipV(vec2 p) { return vec4(p.x / 960.0 - 1.0, 1.0 - p.y / 540.0, 0.0, 1.0); }\n"
    "vec2 cornerV(int c) { return c == 1 ? vec2(1.0, 0.0) : (c == 2 || c == 4) ? vec2(1.0) : c == 5 ? vec2(0.0, 1.0) : vec2(0.0); }\n"
    "void main() { gl_Position = canvasClipV(mix(u_rect.xy, u_rect.zw, cornerV(gl_VertexID % 6))); }\n";

static const char *GEM_FS_GL =
    "uniform mat4 u_inv;\n"
    "uniform mat4 u_rot;\n"
    "uniform vec4 u_rect;\n"
    "uniform vec4 u_abs;\n"  /* absorption rgb, opacity */
    "uniform vec4 u_ior;\n"  /* Cauchy A, B; glow */
    "uniform vec4 u_q;\n"    /* subsamples, bounces, fire on, show leaks */
    "uniform vec4 u_body;\n"
    "uniform vec4 u_jit;\n"
    "uniform vec4 u_rx;\nuniform vec4 u_ry;\nuniform vec4 u_rz;\n"
    /* the lights of the bursts round this stone, nearest first (render.c picks them): direction from the stone and
     * the angular radius of the hot core (u_glN), colour and strength, w 1 when the light is on (u_gkN); the light
     * of them all on the stone's body (u_gx) */
    "uniform vec4 u_gl0; uniform vec4 u_gl1; uniform vec4 u_gl2; uniform vec4 u_gl3;\n"
    "uniform vec4 u_gk0; uniform vec4 u_gk1; uniform vec4 u_gk2; uniform vec4 u_gk3;\n"
    "uniform vec4 u_gx;\n"
    "out vec4 o;\n"
    /* a burst's light as the stone sees it: a small hot core (seen in a facet as a flash, split by dispersion into
     * fire) inside a broad glow (the facets that face it brighten), all in the burst's colour */
    "vec3 lampMono(vec3 d, vec4 gl, vec4 gk) {\n"
    "  float c = dot(d, gl.xyz);\n"
    "  float core = smoothstep(cos(gl.w * 1.3), cos(gl.w * 0.5), c);\n"
    "  float glow = smoothstep(cos(0.20), cos(0.08), c);\n"
    "  return gk.xyz * (core * LAMP_CORE + glow * LAMP_GLOW + pow(max(c, 0.0), 4.0) * LAMP_HALO);\n"
    "}\n"
    "vec3 lampDisp(vec3 e0, vec3 e1, vec3 em, vec4 gl, vec4 gk) {\n"
    "  vec3 seg = e1 - e0; float sl2 = max(dot(seg, seg), 1e-14);\n"
    "  float t = clamp(dot(gl.xyz - e0, seg) / sl2, 0.0, 1.0);\n"
    "  float d = length(e0 + seg * t - gl.xyz);\n"
    "  float core = 1.0 - smoothstep(gl.w * 0.5, gl.w * 1.3, d);\n"
    "  float fr = min(1.0, 2.0 * gl.w / sqrt(sl2));\n"
    "  float c = dot(em, gl.xyz);\n"
    "  float glow = smoothstep(cos(0.20), cos(0.08), c);\n"
    "  return gk.xyz * (core * fr * mix(spec(lamOf(t)), vec3(1.0), fr) * LAMP_CORE + glow * LAMP_GLOW + pow(max(c, 0.0), 4.0) * LAMP_HALO);\n"
    "}\n"
    "vec3 lampsMono(vec3 d) {\n"
    "  vec3 c = vec3(0.0);\n"
    "  if (u_gk0.w > 0.5) c += lampMono(d, u_gl0, u_gk0);\n"
    "  if (u_gk1.w > 0.5) c += lampMono(d, u_gl1, u_gk1);\n"
    "  if (u_gk2.w > 0.5) c += lampMono(d, u_gl2, u_gk2);\n"
    "  if (u_gk3.w > 0.5) c += lampMono(d, u_gl3, u_gk3);\n"
    "  return c;\n"
    "}\n"
    /* only the strongest light is split into fire (the cost of the arc is paid once an exit) */
    "vec3 lampsDisp(vec3 e0, vec3 e1, vec3 em) {\n"
    "  vec3 c = vec3(0.0);\n"
    "  if (u_gk0.w > 0.5) c += lampDisp(e0, e1, em, u_gl0, u_gk0);\n"
    "  if (u_gk1.w > 0.5) c += lampMono(em, u_gl1, u_gk1);\n"
    "  if (u_gk2.w > 0.5) c += lampMono(em, u_gl2, u_gk2);\n"
    "  if (u_gk3.w > 0.5) c += lampMono(em, u_gl3, u_gk3);\n"
    "  return c;\n"
    "}\n"
    "vec3 toR(vec3 dl) { return normalize(mat3(u_rot) * dl); }\n" /* the world, as the bursts are in it */
    /* u_ior.w: how far a small lamp spreads in this stone's facets (the polish of a broad flat facet; the emerald cut's
     * steps and table, which the camera sees all at one angle: unspread, one lamp would fill a whole table white) */
    "vec3 envMono(vec3 dl) { vec3 dr = toR(dl); vec3 dw = drift(dr); vec4 s = studioT(dw); return (s.rgb + pinMono(dw, s.a, u_ior.w)) * u_tint.xyz + lampsMono(dr); }\n"
    "vec3 envDisp(vec3 e0, vec3 e1, vec3 em) {\n"
    "  vec3 r0 = toR(e0), r1 = toR(e1), rm = toR(em); vec3 dm = drift(rm); vec4 s = studioT(dm);\n"
    "  return (s.rgb + pinDisp(drift(r0), drift(r1), s.a, u_ior.w)) * u_tint.xyz + lampsDisp(r0, r1, rm);\n"
    "}\n"
    "float fres(float ci, float n1, float n2) {\n"
    "  float st = n1 / n2 * sqrt(max(0.0, 1.0 - ci * ci));\n"
    "  if (st >= 1.0) return 1.0;\n"
    "  float ct = sqrt(1.0 - st * st);\n"
    "  float rs = (n1 * ci - n2 * ct) / (n1 * ci + n2 * ct), rp = (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct);\n"
    "  return 0.5 * (rs * rs + rp * rp);\n"
    "}\n"
    /* one path through the stone: Fresnel at every face, total internal reflection past the critical angle,
     * Beer-Lambert absorption along the way, and at each exit the spectrum fanned out between its 400 and 700 nm
     * directions (pinsDisp) */
    "vec3 shade(vec3 p, vec3 d, vec3 nn, int NB, bool fire) {\n"
    "  float n = u_ior.x + u_ior.y / (560.0 * 560.0);\n"
    "  float n0 = u_ior.x + u_ior.y / (400.0 * 400.0), n1 = u_ior.x + u_ior.y / (700.0 * 700.0);\n"
    "  float ci = -dot(nn, d);\n"
    "  float F = fres(ci, 1.0, n);\n"
    "  vec3 col = F * envMono(reflect(d, nn));\n"
    "  vec3 t = refract(d, nn, 1.0 / n);\n"
    "  float thr = 1.0 - F, path = 0.0;\n"
    "  for (int b = 0; b < 12; b++) {\n"
    "    if (b >= NB || thr < 0.03) break;\n"
    "    vec3 ne;\n"
    "    float te = max(hullOut(p, t, ne), 0.0);\n"
    "    p += t * te; path += te;\n"
    "    float ce = dot(t, ne);\n"
    "    if (n * n * (1.0 - ce * ce) < 1.0) {\n"
    "      float Fe = fres(ce, n, 1.0);\n"
    "      vec3 T = exp(-u_abs.xyz * path) * (thr * (1.0 - Fe));\n"
    "      vec3 em = refract(t, -ne, n);\n"
    "      if (fire) {\n"
    "        vec3 e0 = refract(t, -ne, n0), e1 = refract(t, -ne, n1);\n"
    "        if (dot(e0, e0) < 0.5) e0 = em;\n"
    "        if (dot(e1, e1) < 0.5) e1 = em;\n"
    "        col += T * envDisp(e0, e1, em);\n"
    "      } else col += T * envMono(em);\n"
    "      thr *= Fe;\n"
    "    }\n"
    "    t = reflect(t, ne);\n"
    "  }\n"
    "  if (u_q.w > 0.5) col += vec3(thr * 3.0, 0.0, thr * 3.0);\n" /* the light the bounce budget left inside */
    "  else col += thr * exp(-u_abs.xyz * path) * envMono(t) * 0.5;\n"
    "  return col;\n"
    "}\n"
    "void main() {\n"
    "  vec2 fc = gl_FragCoord.xy + u_jit.xy;\n"
    "  int S = int(u_q.x), NB = int(u_q.y);\n"
    "  bool fire = u_q.z > 0.5;\n"
    "  vec3 acc = vec3(0.0); float cover = 0.0;\n"
    "  vec3 ro = u_cam.xyz;\n"
    "  vec3 ol = (u_inv * vec4(ro, 1.0)).xyz;\n"
    "  for (int k = 0; k < 4; k++) {\n"
    "    if (k >= S) break;\n"
    "    vec2 off = S == 1 ? vec2(0.0) : S == 2 ? (k == 0 ? vec2(-0.25, 0.25) : vec2(0.25, -0.25))\n"
    "             : k == 0 ? vec2(-0.375, 0.125) : k == 1 ? vec2(0.125, 0.375) : k == 2 ? vec2(0.375, -0.125) : vec2(-0.125, -0.375);\n"
    "    vec2 q = fc + off;\n"
    "    vec3 rd = normalize(u_rx.xyz * q.x + u_ry.xyz * q.y + u_rz.xyz);\n"
    "    vec3 dl = normalize(mat3(u_inv) * rd);\n"
    "    float tn; vec3 nn;\n"
    "    if (!hullIn(ol, dl, tn, nn)) continue;\n"
    "    cover += 1.0;\n"
    "    acc += shade(ol + dl * tn, dl, nn, NB, fire);\n"
    "  }\n"
    "  if (cover <= 0.0) discard;\n"
    "  vec3 col = acc / cover;\n"
    "  col += u_body.xyz * u_body.w + u_gx.xyz;\n"
    "  float g = u_ior.z;\n"
    "  col = col * (1.0 + 1.6 * g) + u_body.xyz * g * 0.3;\n"
    "  float a = cover / float(S) * u_abs.w;\n"
    "  o = vec4(col * a, a);\n"
    "}\n";

/* a burst's light as the stones see it: the radiance of its hot core, of the glow round it and of its broad halo,
 * per unit of the light's strength at the stone (render.c) */
#define LAMP_CORE 40.0f
#define LAMP_GLOW 0.9f
#define LAMP_HALO 0.025f
static void lamp_consts(sb *b, bool w) {
  if (w) put(b, "const LAMP_CORE = %.4f;\nconst LAMP_GLOW = %.4f;\nconst LAMP_HALO = %.4f;\n", LAMP_CORE, LAMP_GLOW, LAMP_HALO);
  else put(b, "const float LAMP_CORE = %.4f, LAMP_GLOW = %.4f, LAMP_HALO = %.4f;\n", LAMP_CORE, LAMP_GLOW, LAMP_HALO);
}

char *gm_shader_gem(bool w, const jw_cut *c, char **vs) {
  sb b = { 0 };
  if (w) {
    put(&b, "%s%s", WG_GEM_U, WG_COMMON_FNS);
    lamp_consts(&b, true);
    spectrum_fn(&b, true);
    gem_env_fns(&b, true);
    hull_fns(&b, c, true);
    put(&b, "%s", WG_GEM);
    return done(&b);
  }
  *vs = strdup(GEM_VS_GL);
  put(&b, "%s", GL_COMMON);
  lamp_consts(&b, false);
  spectrum_fn(&b, false);
  gem_env_fns(&b, false);
  hull_fns(&b, c, false);
  put(&b, "%s", GEM_FS_GL);
  return done(&b);
}

/* ── under each stone: shadow, caustic, the held one's glow ──────────── */
