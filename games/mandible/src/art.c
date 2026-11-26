/* Mandible: the pieces' carved geometry, built at start-up from our own insect designs (art.h).
 *
 * A piece is a heavy hexagonal tile with chamfered edges. Its top is a shallow recessed field with the insect
 * standing proud of it in low relief, painted by vertex colour: stone or dark wood for the body, a coloured inlay
 * for the insect (one colour a kind, the same on both sides, so a kind reads at a glance from across the table).
 * The relief is a height field over a triangular lattice that shares vertices across the whole top, so the
 * smooth normals carry the carving's light without seams. Each insect is a few signed-distance shapes (discs,
 * ellipses, capsules) in a unit circle, head toward -z. */
#include "art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"

/* ── shapes ───────────────────────────────────────────────────────────── */

typedef struct { float x, y; } v2;

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float smooth(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}
static float d_circle(v2 p, float cx, float cy, float r) { return hypotf(p.x - cx, p.y - cy) - r; }
static float d_ellipse(v2 p, float cx, float cy, float a, float b, float rot) {
  float c = cosf(rot), s = sinf(rot), x = p.x - cx, y = p.y - cy;
  float u = x * c + y * s, v = -x * s + y * c;
  float k = hypotf(u / a, v / b);
  return (k - 1) * fminf(a, b);
}
static float d_capsule(v2 p, float ax, float ay, float bx, float by, float r) {
  float px = p.x - ax, py = p.y - ay, dx = bx - ax, dy = by - ay;
  float h = clampf((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1);
  return hypotf(px - dx * h, py - dy * h) - r;
}
/* a leg of two segments, mirrored to both sides */
static float d_leg(v2 p, float ax, float ay, float kx, float ky, float fx, float fy, float r) {
  v2 m = { fabsf(p.x), p.y };
  return fminf(d_capsule(m, ax, ay, kx, ky, r), d_capsule(m, kx, ky, fx, fy, r * 0.8f));
}

/* the insect's relief at p (unit circle, +y is the head): height 0..1, and a recess 0..1 (eye spots, the
 * elytra's seam) */
static float insect(int kind, v2 p, float *recess) {
  float d = 1e9f, cut = 1e9f;
  v2 m = { fabsf(p.x), p.y }; /* mirrored: every insect is symmetric */
  switch (kind) {
  case 0: /* Empress: a broad-winged moth with plumed antennae */
    d = fminf(d, d_capsule(p, 0, -0.38f, 0, 0.30f, 0.085f));
    d = fminf(d, d_circle(p, 0, 0.40f, 0.085f));
    d = fminf(d, d_ellipse(m, 0.31f, 0.16f, 0.31f, 0.18f, 0.42f));
    d = fminf(d, d_ellipse(m, 0.24f, -0.24f, 0.21f, 0.13f, -0.55f));
    d = fminf(d, d_capsule(m, 0.03f, 0.46f, 0.20f, 0.70f, 0.022f));
    d = fminf(d, d_capsule(m, 0.12f, 0.58f, 0.18f, 0.55f, 0.018f));
    d = fminf(d, d_capsule(m, 0.16f, 0.64f, 0.23f, 0.61f, 0.018f));
    cut = fminf(cut, d_circle(m, 0.36f, 0.17f, 0.065f));
    cut = fminf(cut, d_circle(m, 0.25f, -0.25f, 0.04f));
    break;
  case 1: /* Ram: a horned beetle */
    d = fminf(d, d_ellipse(p, 0, -0.14f, 0.30f, 0.42f, 0));
    d = fminf(d, d_ellipse(p, 0, 0.32f, 0.25f, 0.16f, 0));
    d = fminf(d, d_capsule(p, 0, 0.40f, 0, 0.72f, 0.055f));
    d = fminf(d, d_capsule(m, 0.03f, 0.66f, 0.12f, 0.74f, 0.03f));
    d = fminf(d, d_leg(p, 0.22f, 0.26f, 0.46f, 0.36f, 0.58f, 0.52f, 0.032f));
    d = fminf(d, d_leg(p, 0.26f, 0.0f, 0.52f, 0.02f, 0.66f, -0.10f, 0.032f));
    d = fminf(d, d_leg(p, 0.24f, -0.28f, 0.46f, -0.40f, 0.52f, -0.62f, 0.032f));
    cut = fminf(cut, d_capsule(p, 0, -0.52f, 0, 0.17f, 0.014f));
    cut = fminf(cut, d_capsule(p, -0.22f, 0.19f, 0.22f, 0.19f, 0.012f));
    break;
  case 2: /* Locust: long body, folded wings, big hind legs */
    d = fminf(d, d_capsule(p, 0, -0.58f, 0, 0.30f, 0.075f));
    d = fminf(d, d_circle(p, 0, 0.42f, 0.10f));
    d = fminf(d, d_ellipse(p, 0, -0.18f, 0.14f, 0.42f, 0));
    d = fminf(d, d_capsule(m, 0.09f, -0.02f, 0.27f, -0.42f, 0.065f));
    d = fminf(d, d_capsule(m, 0.27f, -0.42f, 0.20f, -0.74f, 0.026f));
    d = fminf(d, d_leg(p, 0.07f, 0.24f, 0.24f, 0.30f, 0.30f, 0.44f, 0.026f));
    d = fminf(d, d_leg(p, 0.08f, 0.10f, 0.26f, 0.10f, 0.34f, 0.02f, 0.026f));
    d = fminf(d, d_capsule(m, 0.04f, 0.50f, 0.22f, 0.80f, 0.018f));
    cut = fminf(cut, d_capsule(p, 0, -0.55f, 0, 0.18f, 0.012f));
    cut = fminf(cut, d_circle(m, 0.06f, 0.45f, 0.03f));
    break;
  case 3: /* Weaver: a round-bodied spider, eight jointed legs */
    d = fminf(d, d_circle(p, 0, -0.20f, 0.27f));
    d = fminf(d, d_circle(p, 0, 0.21f, 0.16f));
    d = fminf(d, d_circle(p, 0, 0.38f, 0.07f));
    d = fminf(d, d_leg(p, 0.10f, 0.30f, 0.34f, 0.52f, 0.44f, 0.74f, 0.03f));
    d = fminf(d, d_leg(p, 0.13f, 0.24f, 0.46f, 0.32f, 0.66f, 0.30f, 0.03f));
    d = fminf(d, d_leg(p, 0.14f, 0.16f, 0.46f, 0.04f, 0.66f, -0.14f, 0.03f));
    d = fminf(d, d_leg(p, 0.12f, 0.10f, 0.38f, -0.22f, 0.50f, -0.56f, 0.03f));
    cut = fminf(cut, d_circle(p, 0, -0.20f, 0.10f));
    cut = fminf(cut, d_circle(m, 0.13f, -0.30f, 0.045f));
    break;
  default: /* Runner: an ant, three segments and elbowed antennae */
    d = fminf(d, d_circle(p, 0, 0.44f, 0.13f));
    d = fminf(d, d_ellipse(p, 0, 0.13f, 0.09f, 0.17f, 0));
    d = fminf(d, d_circle(p, 0, -0.09f, 0.055f));
    d = fminf(d, d_ellipse(p, 0, -0.38f, 0.20f, 0.26f, 0));
    d = fminf(d, d_leg(p, 0.05f, 0.22f, 0.30f, 0.34f, 0.46f, 0.56f, 0.026f));
    d = fminf(d, d_leg(p, 0.06f, 0.14f, 0.34f, 0.12f, 0.56f, 0.04f, 0.026f));
    d = fminf(d, d_leg(p, 0.05f, 0.06f, 0.30f, -0.12f, 0.46f, -0.44f, 0.026f));
    d = fminf(d, d_capsule(m, 0.06f, 0.53f, 0.20f, 0.66f, 0.02f));
    d = fminf(d, d_capsule(m, 0.20f, 0.66f, 0.13f, 0.80f, 0.018f));
    cut = fminf(cut, d_capsule(p, -0.14f, -0.36f, 0.14f, -0.36f, 0.012f));
    cut = fminf(cut, d_capsule(p, -0.17f, -0.48f, 0.17f, -0.48f, 0.012f));
    break;
  }
  float edge = 0.022f;
  float body = smooth(edge, -edge, d);
  float dome = smooth(0, -0.10f, d); /* rounded, not flat-topped */
  *recess = smooth(edge * 0.7f, -edge * 0.7f, cut) * body;
  return body * (0.55f + 0.45f * dome);
}

/* ── colours ──────────────────────────────────────────────────────────── */

static const float INLAY[5][3] = {
  { 0.86f, 0.62f, 0.20f }, /* Empress: gold */
  { 0.24f, 0.46f, 0.74f }, /* Ram: steel blue */
  { 0.36f, 0.64f, 0.26f }, /* Locust: leaf green */
  { 0.74f, 0.20f, 0.22f }, /* Weaver: crimson */
  { 0.90f, 0.45f, 0.14f }, /* Runner: amber */
};
const float *art_inlay_color(int kind) { return INLAY[kind < 0 ? 0 : kind > 4 ? 4 : kind]; }

static float hash2(int x, int y, int seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}
static float vnoise(float x, float y, int seed) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - (float)xi, fy = y - (float)yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

/* the body colour at a point of the top or the walls: side 0 a pale limestone with fine speckle, side 1 dark
 * wood with grain along x */
static void body_color(int side, float x, float y, float z, float out[3]) {
  if (side == 0) {
    float n = vnoise(x * 18, z * 18 + y * 9, 3) * 0.6f + vnoise(x * 55, z * 55, 4) * 0.4f;
    float k = 0.94f + 0.10f * (n - 0.5f);
    out[0] = 0.87f * k, out[1] = 0.83f * k, out[2] = 0.75f * k;
  } else {
    float g = vnoise(x * 3 + y * 2, z * 26, 7) * 0.7f + vnoise(x * 9, z * 70, 8) * 0.3f;
    float band = 0.5f + 0.5f * sinf(z * 40 + g * 6);
    float k = 0.80f + 0.28f * band * g;
    out[0] = 0.135f * k, out[1] = 0.098f * k, out[2] = 0.078f * k;
  }
}

/* ── the mesh builder ─────────────────────────────────────────────────── */

typedef struct {
  float *p, *c;
  uint32_t *ix;
  int nv, ni, cv, ci;
} mesh_buf;

static int add_v(mesh_buf *b, float x, float y, float z, const float col[3]) {
  if (b->nv >= b->cv) {
    b->cv = b->cv ? b->cv * 2 : 1024;
    if (b->cv <= 0 || b->cv > (1 << 22)) CW_FATAL("mandible art: too many vertices");
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cv);
    b->c = realloc(b->c, sizeof(float) * 3 * (size_t)b->cv);
  }
  b->p[b->nv * 3] = x, b->p[b->nv * 3 + 1] = y, b->p[b->nv * 3 + 2] = z;
  b->c[b->nv * 3] = col[0], b->c[b->nv * 3 + 1] = col[1], b->c[b->nv * 3 + 2] = col[2];
  return b->nv++;
}
static void add_t(mesh_buf *b, int i, int j, int k) {
  if (b->ni + 3 > b->ci) {
    b->ci = b->ci ? b->ci * 2 : 3072;
    if (b->ci <= 0 || b->ci > (1 << 24)) CW_FATAL("mandible art: too many indices");
    b->ix = realloc(b->ix, sizeof(uint32_t) * (size_t)b->ci);
  }
  b->ix[b->ni++] = (uint32_t)i, b->ix[b->ni++] = (uint32_t)j, b->ix[b->ni++] = (uint32_t)k;
}
/* a quad with its own vertices (a crisp edge), a -> b -> c -> d counterclockwise seen from outside */
static void add_q(mesh_buf *b, const float *pa, const float *pb, const float *pc, const float *pd, int side) {
  float col[3];
  int i[4];
  const float *ps[4] = { pa, pb, pc, pd };
  for (int k = 0; k < 4; k++) {
    body_color(side, ps[k][0], ps[k][1], ps[k][2], col);
    float shade = 0.92f + 0.08f * clampf(ps[k][1] / ART_PIECE_HEIGHT, 0, 1);
    col[0] *= shade, col[1] *= shade, col[2] *= shade;
    i[k] = add_v(b, ps[k][0], ps[k][1], ps[k][2], col);
  }
  add_t(b, i[0], i[1], i[2]);
  add_t(b, i[0], i[2], i[3]);
}

static t3_geometry *finish(mesh_buf *b) {
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, b->c, b->nv, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, b->ix, b->ni, 1));
  t3_geometry_compute_vertex_normals(g);
  t3_geometry_compute_bounding_sphere(g);
  free(b->p), free(b->c), free(b->ix);
  return g;
}

/* corner i of a pointy hexagon of radius r (x, z) */
static void corner(int i, float r, float *x, float *z) {
  float a = (60.0f * (float)i + 30.0f) * 3.14159265f / 180.0f;
  *x = r * cosf(a), *z = r * sinf(a);
}

t3_geometry *art_piece_geometry(int side, int kind) {
  const float R = ART_PIECE_RADIUS, H = ART_PIECE_HEIGHT, B = 0.035f;
  const float rt = R - B; /* the top's corner radius */
  mesh_buf b = { 0 };
  /* walls and chamfers: per side of the hexagon, three bands (bottom chamfer, wall, top chamfer) */
  for (int i = 0; i < 6; i++) {
    float x0, z0, x1, z1, xi0, zi0, xi1, zi1;
    corner(i, R, &x0, &z0), corner(i + 1, R, &x1, &z1);
    corner(i, rt, &xi0, &zi0), corner(i + 1, rt, &xi1, &zi1);
    float a0[3] = { xi0, 0, zi0 }, a1[3] = { xi1, 0, zi1 };
    float w0[3] = { x0, B, z0 }, w1[3] = { x1, B, z1 };
    float u0[3] = { x0, H - B, z0 }, u1[3] = { x1, H - B, z1 };
    float t0[3] = { xi0, H, zi0 }, t1[3] = { xi1, H, zi1 };
    /* counterclockwise from outside: corners go counterclockwise seen from above (+y), so from outside a face
     * the order is bottom i+1, bottom i... keep the winding facing out */
    add_q(&b, a1, a0, w0, w1, side);
    add_q(&b, w1, w0, u0, u1, side);
    add_q(&b, u1, u0, t0, t1, side);
  }
  /* the bottom */
  {
    float col[3];
    body_color(side, 0, 0, 0, col);
    col[0] *= 0.6f, col[1] *= 0.6f, col[2] *= 0.6f;
    int c = add_v(&b, 0, 0, 0, col), k[6];
    for (int i = 0; i < 6; i++) {
      float x, z;
      corner(i, rt, &x, &z);
      k[i] = add_v(&b, x, 0, z, col);
    }
    for (int i = 0; i < 6; i++) add_t(&b, c, k[i], k[(i + 1) % 6]);
  }
  /* the carved top: a triangular lattice in the basis of corners 0 and 1, M steps from the centre to a corner */
  enum { M = 30 };
  const int span = 2 * M + 1;
  int *at = malloc(sizeof(int) * (size_t)span * (size_t)span);
  for (int i = 0; i < span * span; i++) at[i] = -1;
  float c0x, c0z, c1x, c1z;
  corner(0, rt / (float)M, &c0x, &c0z), corner(1, rt / (float)M, &c1x, &c1z);
  const float *inl = INLAY[kind];
  /* white pieces face away from the near edge, black pieces the other way */
  float flip = side == 0 ? 1.0f : -1.0f;
  for (int i = -M; i <= M; i++)
    for (int j = -M; j <= M; j++) {
      /* inside the hexagon: in the lattice basis the hexagon is |i| <= M, |j| <= M, |i + j| <= M */
      if (abs(i + j) > M) continue;
      float x = c0x * (float)i + c1x * (float)j, z = c0z * (float)i + c1z * (float)j;
      /* the insect's frame: unit circle over 0.80 of the top's inner radius, head toward -z (flipped for black) */
      float sc = rt * 0.866f * 0.86f;
      v2 p = { x / sc * flip, -z / sc * flip };
      float rec = 0;
      float relief = insect(kind, p, &rec);
      /* the field: recessed inside a raised border, the floor rising softly to the border */
      float rr = hypotf(x, z) / (rt * 0.866f);
      int ring = abs(i) > abs(j) ? abs(i) : abs(j);
      if (abs(i + j) > ring) ring = abs(i + j);
      float hexr = (float)ring / (float)M; /* 0 centre, 1 rim (hexagonal distance) */
      float field = smooth(0.84f, 0.90f, hexr); /* 1 on the border */
      float y = H - 0.016f * (1 - field) + 0.013f * relief * (1 - field) - 0.006f * rec;
      float col[3];
      body_color(side, x, H, z, col);
      float dark = side == 0 ? 0.80f : 0.70f; /* the field's floor holds a little grime */
      float k = dark + (1 - dark) * field;
      col[0] *= k, col[1] *= k, col[2] *= k;
      /* the inlay: brightest on the crest of the relief, darker toward its foot */
      float t = smooth(0.05f, 0.35f, relief) * (1 - field);
      float lift = 0.55f + 0.45f * relief;
      for (int c = 0; c < 3; c++) col[c] = col[c] * (1 - t) + inl[c] * lift * t;
      if (rec > 0) for (int c = 0; c < 3; c++) col[c] *= 1 - 0.55f * rec;
      (void)rr;
      at[(i + M) * span + (j + M)] = add_v(&b, x, y, z, col);
    }
  for (int i = -M; i < M; i++)
    for (int j = -M; j < M; j++) {
      int a = at[(i + M) * span + (j + M)], bb = at[(i + 1 + M) * span + (j + M)];
      int c = at[(i + M) * span + (j + 1 + M)], d = at[(i + 1 + M) * span + (j + 1 + M)];
      /* two triangles per lattice rhombus, wound to face +y */
      if (a >= 0 && bb >= 0 && c >= 0) add_t(&b, a, c, bb);
      if (bb >= 0 && c >= 0 && d >= 0) add_t(&b, bb, c, d);
    }
  free(at);
  return finish(&b);
}

t3_geometry *art_ring_geometry(float r_out, float r_in) {
  mesh_buf b = { 0 };
  float col[3] = { 1, 1, 1 };
  int o[6], in[6];
  for (int i = 0; i < 6; i++) {
    float x, z;
    corner(i, r_out, &x, &z);
    o[i] = add_v(&b, x, 0, z, col);
    corner(i, r_in, &x, &z);
    in[i] = add_v(&b, x, 0, z, col);
  }
  for (int i = 0; i < 6; i++) {
    int k = (i + 1) % 6;
    add_t(&b, o[i], in[i], o[k]);
    add_t(&b, o[k], in[i], in[k]);
  }
  return finish(&b);
}

t3_geometry *art_hex_geometry(float r) {
  mesh_buf b = { 0 };
  float col[3] = { 1, 1, 1 };
  int c = add_v(&b, 0, 0, 0, col), k[6];
  for (int i = 0; i < 6; i++) {
    float x, z;
    corner(i, r, &x, &z);
    k[i] = add_v(&b, x, 0, z, col);
  }
  for (int i = 0; i < 6; i++) add_t(&b, c, k[(i + 1) % 6], k[i]);
  return finish(&b);
}

/* ── the table ────────────────────────────────────────────────────────── */

static float fbm(float x, float y, int seed, int oct) {
  float v = 0, amp = 0.5f, tot = 0;
  for (int i = 0; i < oct; i++, x *= 2.03f, y *= 2.03f, amp *= 0.5f) v += amp * vnoise(x, y, seed + i * 17), tot += amp;
  return v / tot;
}

/* a flat grid over [x0, x1] x [z0, z1] at height y, coloured by col(x, z) */
static void add_grid(mesh_buf *b, float x0, float x1, float z0, float z1, float y, int nx, int nz,
                     void (*col)(float x, float z, float out[3], const void *u), const void *u) {
  int base = b->nv;
  for (int j = 0; j <= nz; j++)
    for (int i = 0; i <= nx; i++) {
      float x = x0 + (x1 - x0) * (float)i / (float)nx, z = z0 + (z1 - z0) * (float)j / (float)nz, c[3];
      col(x, z, c, u);
      add_v(b, x, y, z, c);
    }
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) {
      int a = base + j * (nx + 1) + i;
      add_t(b, a, a + nx + 1, a + 1); /* facing up */
      add_t(b, a + 1, a + nx + 1, a + nx + 2);
    }
}

/* felt: a deep green with soft mottling, a fine nap, and a vignette that darkens toward the mat's edge */
static void felt_color(float x, float z, float out[3], const void *u) {
  const float *half = u;
  float blot = fbm(x * 0.35f, z * 0.35f, 11, 4), nap = hash2((int)floorf(x * 22), (int)floorf(z * 22), 5);
  float ex = 1 - fabsf(x) / half[0], ez = 1 - fabsf(z) / half[1], edge = fminf(ex, ez);
  float vig = 0.62f + 0.38f * smooth(0.0f, 0.45f, edge);
  float k = (0.86f + 0.28f * blot + 0.07f * (nap - 0.5f)) * vig;
  out[0] = 0.034f * k, out[1] = 0.118f * k, out[2] = 0.082f * k;
}
t3_geometry *art_felt_geometry(float half_x, float half_z) {
  mesh_buf b = { 0 };
  float half[2] = { half_x, half_z };
  add_grid(&b, -half_x, half_x, -half_z, half_z, 0, (int)(half_x * 2 / 0.1f), (int)(half_z * 2 / 0.1f), felt_color, half);
  return finish(&b);
}

/* walnut planks running along x: each plank its own strip (a crisp seam), its grain streaks along the plank, a
 * plank-to-plank tone, darker toward the table's ends */
typedef struct { float z0, z1, tone, seed; float half_x; } plank_u;
static void wood_color(float x, float z, float out[3], const void *u) {
  const plank_u *p = u;
  float t = (z - p->z0) / (p->z1 - p->z0);
  float streak = fbm(x * 0.6f + p->seed, t * 9.0f + p->seed * 3, 23, 4);
  float ring = 0.5f + 0.5f * sinf(t * 18.0f + streak * 7.0f + x * 0.05f);
  float edge = t < 0.04f || t > 0.96f ? 0.55f : 1.0f; /* the seam */
  float k = (0.72f + 0.3f * streak + 0.12f * ring) * p->tone * edge;
  float far = 0.7f + 0.3f * smooth(0.0f, 0.4f, 1 - fabsf(x) / p->half_x);
  k *= far;
  out[0] = 0.235f * k, out[1] = 0.135f * k, out[2] = 0.072f * k;
}
t3_geometry *art_wood_top_geometry(float half_x, float half_z, float plank) {
  mesh_buf b = { 0 };
  int n = (int)ceilf(half_z * 2 / plank);
  for (int i = 0; i < n; i++) {
    plank_u p = { -half_z + plank * (float)i, fminf(half_z, -half_z + plank * (float)(i + 1)), 0.82f + 0.3f * hash2(i, 3, 9),
                  hash2(i, 7, 2) * 40, half_x };
    add_grid(&b, -half_x, half_x, p.z0, p.z1, 0, (int)(half_x * 2 / 0.25f), 6, wood_color, &p);
  }
  return finish(&b);
}
