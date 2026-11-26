/* Smokestack geometry (geom.h). */
#include "geom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct rl_gb {
  float *pos, *nor, *col;
  uint32_t *idx;
  int nv, ni, cv, ci;
  int gstart[16], gmat[16], ng;
};

rl_gb *rl_gb_new(void) { return calloc(1, sizeof(rl_gb)); }
void rl_gb_free(rl_gb *b) {
  if (!b) return;
  free(b->pos), free(b->nor), free(b->col), free(b->idx), free(b);
}
int rl_gb_verts(const rl_gb *b) { return b->nv; }
void rl_gb_group(rl_gb *b, int m) {
  if (b->ng == 16) CW_FATAL("rl_gb: more than 16 groups");
  b->gstart[b->ng] = b->ni, b->gmat[b->ng++] = m;
}

t3_color rl_lin(uint32_t rgb) {
  float c[3] = { (float)((rgb >> 16) & 255) / 255.0f, (float)((rgb >> 8) & 255) / 255.0f, (float)(rgb & 255) / 255.0f };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}
uint32_t rl_shade(uint32_t rgb, float k) {
  int r = (int)lroundf((float)((rgb >> 16) & 255) * k), g = (int)lroundf((float)((rgb >> 8) & 255) * k), bl = (int)lroundf((float)(rgb & 255) * k);
  r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, bl = bl > 255 ? 255 : bl;
  return (uint32_t)(r << 16 | g << 8 | bl);
}
uint32_t rl_mix(uint32_t a, uint32_t b, float t) {
  uint32_t o = 0;
  for (int s = 0; s < 24; s += 8) {
    float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
    o |= (uint32_t)lroundf(x + (y - x) * t) << s;
  }
  return o;
}

static void grow(rl_gb *b, int nv, int ni) {
  if (b->nv + nv > b->cv) {
    while (b->nv + nv > b->cv) b->cv = b->cv ? b->cv * 2 : 1024;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->cv);
  }
  if (b->ni + ni > b->ci) {
    while (b->ni + ni > b->ci) b->ci = b->ci ? b->ci * 2 : 2048;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->ci);
  }
}

static t3_vec3 sub(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static t3_vec3 cross(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static t3_vec3 norm(t3_vec3 a) {
  float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 1e-9f ? (t3_vec3){ a.x / l, a.y / l, a.z / l } : (t3_vec3){ 0, 1, 0 };
}

static void vert(rl_gb *b, t3_vec3 p, t3_vec3 n, t3_color c) {
  float *P = b->pos + 3 * b->nv, *N = b->nor + 3 * b->nv, *C = b->col + 3 * b->nv;
  P[0] = p.x, P[1] = p.y, P[2] = p.z, N[0] = n.x, N[1] = n.y, N[2] = n.z, C[0] = c.r, C[1] = c.g, C[2] = c.b;
  b->nv++;
}

/* faces that look up or toward the camera (+Z) keep that way, so the flat normals light from outside */
static t3_vec3 face_normal(t3_vec3 a, t3_vec3 c1, t3_vec3 c2) { return norm(cross(sub(c1, a), sub(c2, a))); }

void rl_gb_tri(rl_gb *b, t3_vec3 a, t3_vec3 c1, t3_vec3 c2, uint32_t rgb) {
  grow(b, 3, 3);
  t3_vec3 n = face_normal(a, c1, c2);
  if (n.y < -0.2f) n = (t3_vec3){ -n.x, -n.y, -n.z };
  t3_color c = rl_lin(rgb);
  uint32_t s = (uint32_t)b->nv;
  vert(b, a, n, c), vert(b, c1, n, c), vert(b, c2, n, c);
  b->idx[b->ni++] = s, b->idx[b->ni++] = s + 1, b->idx[b->ni++] = s + 2;
}

static void quad_n(rl_gb *b, t3_vec3 a, t3_vec3 c1, t3_vec3 c2, t3_vec3 d, t3_vec3 n, uint32_t rgb) {
  grow(b, 4, 6);
  t3_color c = rl_lin(rgb);
  uint32_t s = (uint32_t)b->nv;
  vert(b, a, n, c), vert(b, c1, n, c), vert(b, c2, n, c), vert(b, d, n, c);
  b->idx[b->ni++] = s, b->idx[b->ni++] = s + 1, b->idx[b->ni++] = s + 2;
  b->idx[b->ni++] = s, b->idx[b->ni++] = s + 2, b->idx[b->ni++] = s + 3;
}
void rl_gb_quad(rl_gb *b, t3_vec3 a, t3_vec3 c1, t3_vec3 c2, t3_vec3 d, uint32_t rgb) {
  t3_vec3 n = face_normal(a, c1, c2);
  if (n.y < -0.2f) n = (t3_vec3){ -n.x, -n.y, -n.z };
  quad_n(b, a, c1, c2, d, n, rgb);
}

/* a quad whose outward side is known (the direction from the solid's centre) */
static void quad_out(rl_gb *b, t3_vec3 a, t3_vec3 c1, t3_vec3 c2, t3_vec3 d, t3_vec3 centre, uint32_t rgb) {
  t3_vec3 n = face_normal(a, c1, c2);
  t3_vec3 mid = { (a.x + c2.x) * 0.5f - centre.x, (a.y + c2.y) * 0.5f - centre.y, (a.z + c2.z) * 0.5f - centre.z };
  if (n.x * mid.x + n.y * mid.y + n.z * mid.z < 0) n = (t3_vec3){ -n.x, -n.y, -n.z };
  quad_n(b, a, c1, c2, d, n, rgb);
}

void rl_gb_box(rl_gb *b, t3_vec3 c, t3_vec3 h, float yaw, uint32_t rgb) {
  float cs = cosf(yaw), sn = sinf(yaw);
  t3_vec3 p[8];
  for (int i = 0; i < 8; i++) {
    float x = (i & 1) ? h.x : -h.x, y = (i & 2) ? h.y : -h.y, z = (i & 4) ? h.z : -h.z;
    p[i] = (t3_vec3){ c.x + x * cs + z * sn, c.y + y, c.z - x * sn + z * cs };
  }
  uint32_t top = rgb, side = rl_shade(rgb, 0.86f), front = rl_shade(rgb, 0.95f);
  quad_out(b, p[2], p[3], p[7], p[6], c, top);    /* +Y */
  quad_out(b, p[0], p[1], p[5], p[4], c, rl_shade(rgb, 0.6f)); /* -Y */
  quad_out(b, p[1], p[3], p[7], p[5], c, side);   /* +X */
  quad_out(b, p[0], p[2], p[6], p[4], c, side);   /* -X */
  quad_out(b, p[4], p[5], p[7], p[6], c, front);  /* +Z */
  quad_out(b, p[0], p[1], p[3], p[2], c, side);   /* -Z */
}

void rl_gb_cyl(rl_gb *b, t3_vec3 a, t3_vec3 e, float r, int seg, uint32_t rgb) {
  t3_vec3 ax = norm(sub(e, a));
  t3_vec3 u = fabsf(ax.y) < 0.9f ? norm(cross(ax, (t3_vec3){ 0, 1, 0 })) : norm(cross(ax, (t3_vec3){ 1, 0, 0 }));
  t3_vec3 v = cross(ax, u);
  t3_vec3 mid = { (a.x + e.x) * 0.5f, (a.y + e.y) * 0.5f, (a.z + e.z) * 0.5f };
  for (int i = 0; i < seg; i++) {
    float a0 = 2 * (float)M_PI * (float)i / (float)seg, a1 = 2 * (float)M_PI * (float)(i + 1) / (float)seg;
    t3_vec3 o0 = { (u.x * cosf(a0) + v.x * sinf(a0)) * r, (u.y * cosf(a0) + v.y * sinf(a0)) * r, (u.z * cosf(a0) + v.z * sinf(a0)) * r };
    t3_vec3 o1 = { (u.x * cosf(a1) + v.x * sinf(a1)) * r, (u.y * cosf(a1) + v.y * sinf(a1)) * r, (u.z * cosf(a1) + v.z * sinf(a1)) * r };
    t3_vec3 p0 = { a.x + o0.x, a.y + o0.y, a.z + o0.z }, p1 = { a.x + o1.x, a.y + o1.y, a.z + o1.z };
    t3_vec3 q0 = { e.x + o0.x, e.y + o0.y, e.z + o0.z }, q1 = { e.x + o1.x, e.y + o1.y, e.z + o1.z };
    /* smooth side normals */
    grow(b, 4, 6);
    t3_color c = rl_lin(rgb);
    uint32_t s = (uint32_t)b->nv;
    t3_vec3 n0 = norm(o0), n1 = norm(o1);
    vert(b, p0, n0, c), vert(b, p1, n1, c), vert(b, q1, n1, c), vert(b, q0, n0, c);
    b->idx[b->ni++] = s, b->idx[b->ni++] = s + 1, b->idx[b->ni++] = s + 2;
    b->idx[b->ni++] = s, b->idx[b->ni++] = s + 2, b->idx[b->ni++] = s + 3;
    rl_gb_tri(b, a, p0, p1, rl_shade(rgb, 0.9f));
    rl_gb_tri(b, e, q1, q0, rl_shade(rgb, 0.9f));
    (void)mid;
  }
}

void rl_gb_cone(rl_gb *b, t3_vec3 base, float r, float h, int seg, uint32_t rgb) {
  t3_vec3 tip = { base.x, base.y + h, base.z };
  for (int i = 0; i < seg; i++) {
    float a0 = 2 * (float)M_PI * (float)i / (float)seg, a1 = 2 * (float)M_PI * (float)(i + 1) / (float)seg;
    t3_vec3 p0 = { base.x + r * cosf(a0), base.y, base.z + r * sinf(a0) }, p1 = { base.x + r * cosf(a1), base.y, base.z + r * sinf(a1) };
    uint32_t c = rl_shade(rgb, 0.85f + 0.2f * (0.5f + 0.5f * sinf(a0 + 1.0f)));
    rl_gb_tri(b, p0, p1, tip, c);
  }
}

void rl_gb_prism(rl_gb *b, t3_vec3 c, float radius, int sides, float rot, float y0, float y1, uint32_t top, uint32_t side) {
  t3_vec3 ct = { c.x, y1, c.z };
  for (int i = 0; i < sides; i++) {
    float a0 = rot + 2 * (float)M_PI * (float)i / (float)sides, a1 = rot + 2 * (float)M_PI * (float)(i + 1) / (float)sides;
    t3_vec3 t0 = { c.x + radius * cosf(a0), y1, c.z + radius * sinf(a0) }, t1 = { c.x + radius * cosf(a1), y1, c.z + radius * sinf(a1) };
    t3_vec3 b0 = { t0.x, y0, t0.z }, b1 = { t1.x, y0, t1.z };
    rl_gb_tri(b, ct, t0, t1, top);
    quad_out(b, b0, b1, t1, t0, (t3_vec3){ c.x, (y0 + y1) * 0.5f, c.z }, side);
  }
}

void rl_gb_ring(rl_gb *b, t3_vec3 c, float r0, float r1, int sides, float rot, float y, uint32_t rgb) {
  for (int i = 0; i < sides; i++) {
    float a0 = rot + 2 * (float)M_PI * (float)i / (float)sides, a1 = rot + 2 * (float)M_PI * (float)(i + 1) / (float)sides;
    t3_vec3 p0 = { c.x + r0 * cosf(a0), y, c.z + r0 * sinf(a0) }, p1 = { c.x + r0 * cosf(a1), y, c.z + r0 * sinf(a1) };
    t3_vec3 q0 = { c.x + r1 * cosf(a0), y, c.z + r1 * sinf(a0) }, q1 = { c.x + r1 * cosf(a1), y, c.z + r1 * sinf(a1) };
    quad_n(b, p0, p1, q1, q0, (t3_vec3){ 0, 1, 0 }, rgb);
  }
}

void rl_gb_gable(rl_gb *b, t3_vec3 c, float hx, float hz, float y0, float y1, float yaw, uint32_t rgb) {
  float cs = cosf(yaw), sn = sinf(yaw);
#define P(px_, py_, pz_) ((t3_vec3){ c.x + (px_) * cs + (pz_) * sn, (py_), c.z - (px_) * sn + (pz_) * cs })
  float o = 0.012f; /* eaves overhang */
  t3_vec3 a = P(-hx - o, y0, -hz - o), bb = P(hx + o, y0, -hz - o), cc = P(hx + o, y0, hz + o), d = P(-hx - o, y0, hz + o);
  t3_vec3 r0 = P(-hx - o, y1, 0), r1 = P(hx + o, y1, 0);
  t3_vec3 mid = { c.x, y0, c.z };
  quad_out(b, a, bb, r1, r0, mid, rl_shade(rgb, 0.82f));
  quad_out(b, d, cc, r1, r0, mid, rgb);
  t3_vec3 g0 = P(-hx, y0, -hz), g1 = P(-hx, y0, hz), g2 = P(-hx, y1, 0), g3 = P(hx, y0, -hz), g4 = P(hx, y0, hz), g5 = P(hx, y1, 0);
  rl_gb_tri(b, g0, g1, g2, rl_shade(rgb, 0.7f));
  rl_gb_tri(b, g3, g4, g5, rl_shade(rgb, 0.7f));
#undef P
}

void rl_gb_dome(rl_gb *b, t3_vec3 c, float r, float h, int seg, uint32_t rgb) {
  int rings = 4;
  for (int j = 0; j < rings; j++) {
    float e0 = (float)M_PI * 0.5f * (float)j / (float)rings, e1 = (float)M_PI * 0.5f * (float)(j + 1) / (float)rings;
    for (int i = 0; i < seg; i++) {
      float a0 = 2 * (float)M_PI * (float)i / (float)seg, a1 = 2 * (float)M_PI * (float)(i + 1) / (float)seg;
      t3_vec3 p00 = { c.x + r * cosf(e0) * cosf(a0), c.y + h * sinf(e0), c.z + r * cosf(e0) * sinf(a0) };
      t3_vec3 p01 = { c.x + r * cosf(e0) * cosf(a1), c.y + h * sinf(e0), c.z + r * cosf(e0) * sinf(a1) };
      t3_vec3 p11 = { c.x + r * cosf(e1) * cosf(a1), c.y + h * sinf(e1), c.z + r * cosf(e1) * sinf(a1) };
      t3_vec3 p10 = { c.x + r * cosf(e1) * cosf(a0), c.y + h * sinf(e1), c.z + r * cosf(e1) * sinf(a0) };
      quad_out(b, p00, p01, p11, p10, c, rl_shade(rgb, 0.85f + 0.15f * (float)j / (float)rings));
    }
  }
}

t3_geometry *rl_gb_geometry(rl_gb *b) {
  if (b->nv == 0) CW_FATAL("rl_gb: an empty geometry");
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, b->idx, b->ni, 1));
  for (int i = 0; i < b->ng; i++) {
    int end = i + 1 < b->ng ? b->gstart[i + 1] : b->ni;
    if (end > b->gstart[i]) t3_geometry_add_group(g, b->gstart[i], end - b->gstart[i], b->gmat[i]);
  }
  t3_geometry_compute_bounding_sphere(g);
  t3_geometry_compute_bounding_box(g);
  return g;
}

t3_material *rl_vc_material(float roughness) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true;
  m->roughness = roughness;
  m->metalness = 0;
  m->side = T3_DOUBLE_SIDE;
  return m;
}
t3_material *rl_flat_material(uint32_t rgb, float roughness, float metalness) {
  t3_material *m = t3_mesh_standard_material_new(rgb);
  m->roughness = roughness;
  m->metalness = metalness;
  m->side = T3_DOUBLE_SIDE;
  return m;
}

cw_node rl_mesh_node(cw_node parent, const char *name, t3_geometry *g, t3_material **mats, int n, bool shadow) {
  t3_mesh *mesh = n == 1 ? t3_mesh_new(g, mats[0]) : t3_mesh_new_multi(g, mats, n);
  mesh->base.cast_shadow = shadow;
  mesh->base.receive_shadow = true;
  cw_node node = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, node);
  if (!cw_mesh3d_set_procedural(node, mesh)) CW_FATAL("smokestack: the engine refused mesh %s", name);
  t3_release(mesh);
  return node;
}
