/* Smokestack view (view.h): the valley, track, goods, trains, option plaques and job cards. The HUD is in
 * hud.c. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_view.h"
#include "cartwheel/render3d.h"
#include "geom.h"
#include "view_internal.h"

rl_view_state RV;

const uint32_t RL_GOODS_RGB[RL_COLORS] = { 0xc8372d, 0x2f6fd6, 0xf0c419, 0x8e4fc9, 0x2a2a2e };
const uint32_t RL_SEAT_RGB[RL_MAX_PLAYERS] = { 0xe07b2a, 0x1fa39b, 0xe9e3d3, 0x63a83a };

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352d, x ^= x >> 15, x *= 0x846ca68b, x ^= x >> 16;
  return x;
}
static float rnd(uint32_t seed) { return (float)(hash32(seed) & 0xffff) / 65535.0f; }

/* ── track geometry ── */
static t3_vec3 edge_local(int d) {
  cw_hex_layout l = { .orientation = CW_HEX_POINTY, .size = { RL_HEX_SIZE, RL_HEX_SIZE } };
  cw_vec2 v = cw_hex_to_point(&l, cw_hex_direction(d));
  return (t3_vec3){ v.x * 0.5f, 0, v.y * 0.5f };
}
/* the rail's curve through a hex from edge e to edge d: a quadratic through the centre */
t3_vec3 rl_curve(int e, int d, float t) {
  t3_vec3 a = edge_local(e), b = edge_local(d);
  float u = 1 - t;
  return (t3_vec3){ u * u * a.x + t * t * b.x, 0, u * u * a.z + t * t * b.z };
}

#define TRACK_H 0.022f
static t3_geometry *track_geometry(int e, int d, bool bridge) {
  rl_gb *b = rl_gb_new();
  enum { N = 14 };
  t3_vec3 p[N + 1], s[N + 1];
  for (int i = 0; i <= N; i++) p[i] = rl_curve(e, d, (float)i / N);
  for (int i = 0; i <= N; i++) {
    t3_vec3 t = i < N ? (t3_vec3){ p[i + 1].x - p[i].x, 0, p[i + 1].z - p[i].z } : (t3_vec3){ p[i].x - p[i - 1].x, 0, p[i].z - p[i - 1].z };
    float l = sqrtf(t.x * t.x + t.z * t.z);
    s[i] = (t3_vec3){ -t.z / l, 0, t.x / l }; /* sideways */
  }
#define OFF(i, k, y) ((t3_vec3){ p[i].x + s[i].x * (k), (y), p[i].z + s[i].z * (k) })
  /* ballast bed (group 0: the owner's tint) */
  rl_gb_group(b, 0);
  for (int i = 0; i < N; i++) {
    rl_gb_quad(b, OFF(i, -0.14f, 0.002f), OFF(i + 1, -0.14f, 0.002f), OFF(i + 1, -0.105f, TRACK_H), OFF(i, -0.105f, TRACK_H), 0xb8b0a4);
    rl_gb_quad(b, OFF(i, -0.105f, TRACK_H), OFF(i + 1, -0.105f, TRACK_H), OFF(i + 1, 0.105f, TRACK_H), OFF(i, 0.105f, TRACK_H), 0xffffff);
    rl_gb_quad(b, OFF(i, 0.105f, TRACK_H), OFF(i + 1, 0.105f, TRACK_H), OFF(i + 1, 0.14f, 0.002f), OFF(i, 0.14f, 0.002f), 0xb8b0a4);
  }
  /* sleepers (group 1: wood) */
  rl_gb_group(b, 1);
  float len = 0;
  for (int i = 0; i < N; i++) len += hypotf(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
  int ties = (int)(len / 0.072f);
  for (int k = 0; k <= ties; k++) {
    float t = ((float)k + 0.5f) / (float)(ties + 1);
    int i = (int)(t * N);
    if (i >= N) i = N - 1;
    float f = t * N - (float)i;
    t3_vec3 c = { p[i].x + (p[i + 1].x - p[i].x) * f, TRACK_H + 0.007f, p[i].z + (p[i + 1].z - p[i].z) * f };
    float yaw = atan2f(-(s[i].z), s[i].x);
    uint32_t wood = rl_shade(0x5a3d26, 0.85f + 0.3f * rnd((uint32_t)(k * 31 + e * 7 + d)));
    rl_gb_box(b, c, (t3_vec3){ 0.115f, 0.007f, 0.016f }, yaw, wood);
  }
  /* rails (group 2: steel) */
  rl_gb_group(b, 2);
  for (int side = -1; side <= 1; side += 2) {
    float o = 0.052f * (float)side;
    for (int i = 0; i < N; i++) {
      float y0 = TRACK_H + 0.014f, y1 = TRACK_H + 0.03f, w = 0.008f;
      t3_vec3 a0 = OFF(i, o - w, y1), a1 = OFF(i + 1, o - w, y1), b0 = OFF(i, o + w, y1), b1 = OFF(i + 1, o + w, y1);
      rl_gb_quad(b, a0, a1, b1, b0, 0xd8d8dc);
      rl_gb_quad(b, OFF(i, o - w, y0), OFF(i + 1, o - w, y0), a1, a0, 0x8a8a90);
      rl_gb_quad(b, OFF(i, o + w, y0), OFF(i + 1, o + w, y0), b1, b0, 0x8a8a90);
    }
  }
  if (bridge) { /* iron girders on both sides and a deck under the bed */
    for (int side = -1; side <= 1; side += 2) {
      float o = 0.15f * (float)side;
      for (int i = 0; i < N; i++) {
        rl_gb_quad(b, OFF(i, o, -0.01f), OFF(i + 1, o, -0.01f), OFF(i + 1, o, 0.075f), OFF(i, o, 0.075f), 0x3c3f46);
        if (i % 3 == 0) rl_gb_box(b, OFF(i, o, 0.03f), (t3_vec3){ 0.01f, 0.045f, 0.01f }, 0, 0x2d3036);
      }
    }
    for (int i = 0; i < N; i++)
      rl_gb_quad(b, OFF(i, -0.155f, 0.0f), OFF(i + 1, -0.155f, 0.0f), OFF(i + 1, 0.155f, 0.0f), OFF(i, 0.155f, 0.0f), 0x4a3a2c);
  }
#undef OFF
  t3_geometry *g = rl_gb_geometry(b);
  rl_gb_free(b);
  return g;
}

static int pair_key(int e, int d) { return e * 6 + d; }
t3_geometry *rl_track_geometry(int e, int d, bool bridge) {
  t3_geometry **slot = &RV.track_geo[bridge][pair_key(e, d)];
  if (!*slot) *slot = track_geometry(e, d, bridge);
  return *slot;
}

/* ── the valley ── */
static uint32_t ground_rgb(int t, uint32_t seed) {
  float v = 0.92f + 0.16f * rnd(seed);
  switch (t) {
  case RL_T_FOREST: return rl_shade(0x4f7a35, v);
  case RL_T_RIVER: return rl_shade(0x86a556, v);
  case RL_T_HILLS: return rl_shade(0xb4a466, v);
  case RL_T_LAKE: return 0xc9b98a;
  case RL_T_PEAK: return rl_shade(0x8a847a, v);
  default: return rl_mix(rl_shade(0x8fae55, v), 0xb5c06a, 0.35f * rnd(seed * 7u + 3u));
  }
}

static void build_board(int map) {
  board_view *bv = &RV.board[map];
  const rl_map_def *m = rl_map(map);
  bv->root = cw_node_new("Node3D", map ? "BoardLarge" : "BoardSmall");
  cw_node_add_child(RV.world, bv->root);
  cw_vec2 half = rl_board_half(map);
  rl_gb *land = rl_gb_new(), *water = rl_gb_new();
  /* the board's frame: a dark bed and a wooden rim */
  rl_gb_box(land, (t3_vec3){ -0.9f, -0.02f, 0 }, (t3_vec3){ half.x + 0.25f, 0.02f, half.y + 0.25f }, 0, 0x30291f);
  rl_gb_box(land, (t3_vec3){ -0.9f, 0.0f, -half.y - 0.32f }, (t3_vec3){ half.x + 0.4f, 0.04f, 0.07f }, 0, 0x6b4426);
  rl_gb_box(land, (t3_vec3){ -0.9f, 0.0f, half.y + 0.32f }, (t3_vec3){ half.x + 0.4f, 0.04f, 0.07f }, 0, 0x6b4426);
  rl_gb_box(land, (t3_vec3){ -0.9f - half.x - 0.33f, 0.0f, 0 }, (t3_vec3){ 0.07f, 0.04f, half.y + 0.25f }, 0, 0x6b4426);
  rl_gb_box(land, (t3_vec3){ -0.9f + half.x + 0.33f, 0.0f, 0 }, (t3_vec3){ 0.07f, 0.04f, half.y + 0.25f }, 0, 0x6b4426);
  for (int h = 0; h < m->nhex; h++) {
    int t = m->terrain[h];
    t3_vec3 c = rl_hex_at(map, h, 0);
    c.y = 0;
    float top = rl_ground(t);
    uint32_t col = m->hex_set[h] >= 0 ? rl_shade(0xb7a98a, 0.95f + 0.1f * rnd((uint32_t)h)) : ground_rgb(t, (uint32_t)(h * 13 + map));
    rl_gb_prism(land, c, RL_HEX_SIZE * 0.965f, 6, (float)M_PI / 6, 0, top, col, rl_shade(col, 0.62f));
    if (t == RL_T_LAKE) rl_gb_prism(water, (t3_vec3){ c.x, 0, c.z }, RL_HEX_SIZE * 0.8f, 6, (float)M_PI / 6, top, top + 0.012f, 0x5aa2cf, 0x3f86b2);
    if (t == RL_T_PEAK) {
      rl_gb_cone(land, (t3_vec3){ c.x, top, c.z }, 0.38f, 0.5f, 7, 0x7f7a72);
      rl_gb_cone(land, (t3_vec3){ c.x + 0.003f, top + 0.33f, c.z }, 0.13f, 0.18f, 7, 0xf1f0ea);
      rl_gb_cone(land, (t3_vec3){ c.x + 0.2f, top, c.z + 0.12f }, 0.18f, 0.24f, 6, 0x8f897f);
    }
    if (t == RL_T_RIVER) { /* the river: a ribbon from its way in to its way out */
      int ri = m->river_in[h], ro = m->river_out[h];
      enum { RN = 10 };
      for (int i = 0; i < RN; i++) {
        t3_vec3 p0 = rl_curve(ri, ro, (float)i / RN), p1 = rl_curve(ri, ro, (float)(i + 1) / RN);
        float dx = p1.x - p0.x, dz = p1.z - p0.z, l = sqrtf(dx * dx + dz * dz), sx = -dz / l * 0.085f, sz = dx / l * 0.085f;
        float y = top + 0.004f;
        rl_gb_quad(water, (t3_vec3){ c.x + p0.x - sx, y, c.z + p0.z - sz }, (t3_vec3){ c.x + p1.x - sx, y, c.z + p1.z - sz },
                   (t3_vec3){ c.x + p1.x + sx, y, c.z + p1.z + sz }, (t3_vec3){ c.x + p0.x + sx, y, c.z + p0.z + sz }, 0x4a92c6);
      }
    }
    /* decor that a tile clears: trees, hillocks */
    if (t == RL_T_FOREST || t == RL_T_HILLS) {
      rl_gb *dec = rl_gb_new();
      for (int k = 0; k < 6; k++) {
        float a = (float)M_PI / 6 + (float)k * (float)M_PI / 3 + 0.3f * (rnd((uint32_t)(h * 7 + k)) - 0.5f);
        float r = (t == RL_T_FOREST ? 0.27f : 0.24f) + 0.06f * rnd((uint32_t)(h * 11 + k));
        t3_vec3 p = { c.x + r * cosf(a), top, c.z + r * sinf(a) };
        if (t == RL_T_FOREST) {
          float s = 0.75f + 0.5f * rnd((uint32_t)(h * 17 + k));
          rl_gb_cyl(dec, p, (t3_vec3){ p.x, p.y + 0.05f * s, p.z }, 0.014f, 5, 0x5a3c22);
          rl_gb_cone(dec, (t3_vec3){ p.x, p.y + 0.04f * s, p.z }, 0.075f * s, 0.2f * s, 7, rl_shade(0x2f5a2a, 0.85f + 0.3f * rnd((uint32_t)(h * 5 + k))));
          rl_gb_cone(dec, (t3_vec3){ p.x, p.y + 0.12f * s, p.z }, 0.055f * s, 0.15f * s, 7, rl_shade(0x386a30, 0.9f + 0.2f * rnd((uint32_t)(h * 3 + k))));
        } else if (k < 3) { /* a cluster of rounded hills, grass on their shoulders */
          static const float HX[3] = { -0.12f, 0.13f, 0.02f }, HZ[3] = { -0.08f, -0.05f, 0.14f };
          float s = 0.85f + 0.3f * rnd((uint32_t)(h * 41 + k));
          t3_vec3 b0 = { c.x + HX[k], top, c.z + HZ[k] };
          rl_gb_cone(dec, b0, 0.19f * s, 0.2f * s, 9, rl_shade(0x8a8f4c, 0.95f + 0.15f * rnd((uint32_t)(h * 43 + k))));
          rl_gb_cone(dec, (t3_vec3){ b0.x, top + 0.1f * s, b0.z }, 0.095f * s, 0.1f * s, 9, rl_shade(0xb3a46a, 1.0f));
        }
      }
      t3_material *mm = rl_vc_material(0.9f);
      bv->decor[h] = rl_mesh_node(bv->root, "Decor", rl_gb_geometry(dec), &mm, 1, true);
      t3_release(mm);
      rl_gb_free(dec);
    }
  }
  t3_material *lm = rl_vc_material(0.92f);
  bv->terrain = rl_mesh_node(bv->root, "Land", rl_gb_geometry(land), &lm, 1, false);
  t3_release(lm);
  t3_material *wm = rl_vc_material(0.45f);
  wm->metalness = 0.05f;
  rl_mesh_node(bv->root, "Water", rl_gb_geometry(water), &wm, 1, false);
  t3_release(wm);
  rl_gb_free(land), rl_gb_free(water);
  /* settlements: a city model for every settlement (towns' hidden until chartered) and a town model for towns */
  for (int k = 0; k < m->nset; k++) {
    t3_vec3 c = rl_hex_at(map, m->set_hex[k], 0);
    for (int kind = 0; kind < 2; kind++) {
      if (kind == RL_TOWN && m->set_kind[k] != RL_TOWN) continue;
      rl_gb *walls = rl_gb_new(), *roofs = rl_gb_new(), *wins = rl_gb_new();
      int nb = kind == RL_CITY ? 6 : 3;
      static const float BX[6] = { -0.2f, 0.0f, 0.2f, -0.12f, 0.1f, 0.27f }, BZ[6] = { -0.2f, -0.26f, -0.17f, 0.02f, -0.02f, 0.05f };
      static const float TX[3] = { -0.14f, 0.12f, 0.0f }, TZ[3] = { -0.12f, -0.16f, 0.06f };
      for (int i = 0; i < nb; i++) {
        uint32_t sd = (uint32_t)(k * 97 + i * 13 + kind * 5 + map * 1000);
        float x = kind == RL_CITY ? BX[i] : TX[i], z = kind == RL_CITY ? BZ[i] : TZ[i];
        float w = (kind == RL_CITY ? 0.07f : 0.06f) + 0.02f * rnd(sd), dpt = 0.055f + 0.015f * rnd(sd + 1);
        float hgt = (kind == RL_CITY ? 0.12f + 0.12f * rnd(sd + 2) : 0.08f + 0.03f * rnd(sd + 2));
        if (kind == RL_CITY && i == 1) hgt = 0.3f; /* the clock tower */
        float yaw = 0.25f * (rnd(sd + 3) - 0.5f);
        t3_vec3 bc = { c.x + x, c.y + hgt * 0.5f, c.z + z };
        static const uint32_t WALL[4] = { 0xe4d6b8, 0xb9694a, 0xd9c7a3, 0xa8573e };
        rl_gb_box(walls, bc, (t3_vec3){ w, hgt * 0.5f, dpt }, yaw, WALL[hash32(sd) % 4]);
        rl_gb_gable(roofs, (t3_vec3){ c.x + x, 0, c.z + z }, w, dpt, c.y + hgt, c.y + hgt + 0.06f + (i == 1 && kind == RL_CITY ? 0.05f : 0), yaw, 0xffffff);
        /* windows on the side facing the camera */
        int rows = (int)(hgt / 0.065f);
        for (int r = 0; r < rows; r++)
          for (int q = -1; q <= 1; q += 2) {
            float wx = q * w * 0.45f, wy = c.y + 0.035f + 0.065f * (float)r, wz = dpt + 0.002f;
            float cs = cosf(yaw), sn = sinf(yaw);
#define WP(px, py) ((t3_vec3){ c.x + x + (px) * cs + wz * sn, (py), c.z + z - (px) * sn + wz * cs })
            rl_gb_quad(wins, WP(wx - 0.014f, wy), WP(wx + 0.014f, wy), WP(wx + 0.014f, wy + 0.026f), WP(wx - 0.014f, wy + 0.026f), 0xffffff);
#undef WP
          }
      }
      /* the colour ring round the plaza (roof material) */
      rl_gb_ring(roofs, c, RL_HEX_SIZE * 0.8f, RL_HEX_SIZE * 0.93f, 6, (float)M_PI / 6, c.y + 0.004f, 0xffffff);
      settlement_view *sv = &bv->set[k][kind];
      sv->root = cw_node_new("Node3D", kind ? "Town" : "City");
      cw_node_add_child(bv->root, sv->root);
      t3_material *wmat = rl_vc_material(0.85f);
      sv->roof = rl_flat_material(0xffffff, 0.75f, 0);
      sv->win = rl_flat_material(0x3a3024, 0.6f, 0);
      sv->win->emissive = rl_lin(0xffc46a);
      sv->win->emissive_intensity = 0.5f;
      rl_mesh_node(sv->root, "Walls", rl_gb_geometry(walls), &wmat, 1, true);
      rl_mesh_node(sv->root, "Roofs", rl_gb_geometry(roofs), &sv->roof, 1, true);
      rl_mesh_node(sv->root, "Windows", rl_gb_geometry(wins), &sv->win, 1, false);
      t3_release(wmat);
      rl_gb_free(walls), rl_gb_free(roofs), rl_gb_free(wins);
    }
    for (int i = 0; i < RL_CITY_CUBES; i++) {
      cw_node q = cw_node_new("MeshInstance3D", "Good");
      cw_node_add_child(bv->root, q);
      cw_node_set(q, "primitive", cw_val_enum("BOX"));
      cw_node_set(q, "size", cw_val_vec3(0.13f, 0.13f, 0.13f));
      cw_node_set(q, "roughness", cw_val_float(0.55f));
      cw_node_set(q, "cast_shadow", cw_val_bool(true));
      cw_node_set(q, "visible", cw_val_bool(false));
      bv->cube[k][i] = q;
      bv->cube_shown[k][i] = 0;
    }
  }
  cw_node_set(bv->root, "visible", cw_val_bool(false));
  bv->built = true;
}

/* ── models: the train, ghosts, markers ── */
static void build_train(void) {
  rl_gb *b = rl_gb_new();
  rl_gb_group(b, 0);
  /* forward is +X; wheels, boiler, smokebox, chimney, cab, buffer beam */
  for (int i = 0; i < 3; i++)
    for (int s = -1; s <= 1; s += 2) {
      float x = -0.09f + 0.075f * (float)i;
      rl_gb_cyl(b, (t3_vec3){ x, 0.035f, 0.045f * (float)s }, (t3_vec3){ x, 0.035f, 0.058f * (float)s }, 0.032f, 10, 0x26262a);
    }
  rl_gb_box(b, (t3_vec3){ 0, 0.05f, 0 }, (t3_vec3){ 0.15f, 0.012f, 0.048f }, 0, 0x2b2b30);
  rl_gb_cyl(b, (t3_vec3){ -0.06f, 0.1f, 0 }, (t3_vec3){ 0.11f, 0.1f, 0 }, 0.045f, 12, 0x1f2024);
  rl_gb_cyl(b, (t3_vec3){ 0.11f, 0.1f, 0 }, (t3_vec3){ 0.125f, 0.1f, 0 }, 0.047f, 12, 0x3a3a40);
  rl_gb_cyl(b, (t3_vec3){ 0.08f, 0.13f, 0 }, (t3_vec3){ 0.08f, 0.2f, 0 }, 0.017f, 8, 0x1a1a1e);
  rl_gb_cyl(b, (t3_vec3){ 0.08f, 0.195f, 0 }, (t3_vec3){ 0.08f, 0.21f, 0 }, 0.024f, 8, 0x1a1a1e);
  rl_gb_dome(b, (t3_vec3){ 0.02f, 0.14f, 0 }, 0.022f, 0.025f, 8, 0xb08a3a); /* the brass dome */
  rl_gb_box(b, (t3_vec3){ 0.15f, 0.045f, 0 }, (t3_vec3){ 0.008f, 0.02f, 0.055f }, 0, 0x8a1e1e);
  rl_gb_group(b, 1); /* the seat's colour: cab and running boards */
  rl_gb_box(b, (t3_vec3){ -0.105f, 0.115f, 0 }, (t3_vec3){ 0.045f, 0.055f, 0.055f }, 0, 0xffffff);
  rl_gb_box(b, (t3_vec3){ -0.105f, 0.175f, 0 }, (t3_vec3){ 0.055f, 0.007f, 0.065f }, 0, 0xffffff);
  rl_gb_box(b, (t3_vec3){ 0.02f, 0.064f, 0.05f }, (t3_vec3){ 0.12f, 0.004f, 0.006f }, 0, 0xffffff);
  rl_gb_box(b, (t3_vec3){ 0.02f, 0.064f, -0.05f }, (t3_vec3){ 0.12f, 0.004f, 0.006f }, 0, 0xffffff);
  RV.train_accent = rl_flat_material(0xffffff, 0.55f, 0.2f);
  t3_material *body = rl_vc_material(0.5f);
  body->metalness = 0.35f;
  t3_material *mats[2] = { body, RV.train_accent };
  RV.loco = cw_node_new("Node3D", "Train");
  cw_node_add_child(RV.world, RV.loco);
  rl_mesh_node(RV.loco, "Loco", rl_gb_geometry(b), mats, 2, true);
  rl_gb_free(b);
  /* the wagon: a flatcar with the good on it */
  b = rl_gb_new();
  for (int i = 0; i < 2; i++)
    for (int s = -1; s <= 1; s += 2) {
      float x = -0.05f + 0.1f * (float)i;
      rl_gb_cyl(b, (t3_vec3){ x, 0.03f, 0.045f * (float)s }, (t3_vec3){ x, 0.03f, 0.056f * (float)s }, 0.026f, 8, 0x26262a);
    }
  rl_gb_box(b, (t3_vec3){ 0, 0.06f, 0 }, (t3_vec3){ 0.095f, 0.012f, 0.058f }, 0, 0x6b4a2e);
  rl_gb_box(b, (t3_vec3){ 0, 0.08f, 0.055f }, (t3_vec3){ 0.095f, 0.012f, 0.004f }, 0, 0x5a3c24);
  rl_gb_box(b, (t3_vec3){ 0, 0.08f, -0.055f }, (t3_vec3){ 0.095f, 0.012f, 0.004f }, 0, 0x5a3c24);
  RV.wagon = cw_node_new("Node3D", "Wagon");
  cw_node_add_child(RV.world, RV.wagon);
  rl_mesh_node(RV.wagon, "Flatcar", rl_gb_geometry(b), &body, 1, true);
  t3_release(body);
  rl_gb_free(b);
  RV.wagon_good = cw_node_new("MeshInstance3D", "Load");
  cw_node_add_child(RV.wagon, RV.wagon_good);
  cw_node_set(RV.wagon_good, "primitive", cw_val_enum("BOX"));
  cw_node_set(RV.wagon_good, "size", cw_val_vec3(0.09f, 0.09f, 0.09f));
  cw_node_set(RV.wagon_good, "roughness", cw_val_float(0.55f));
  cw_node3d_set_position(RV.wagon_good, (t3_vec3){ 0, 0.118f, 0 });
  cw_node_set(RV.loco, "visible", cw_val_bool(false));
  cw_node_set(RV.wagon, "visible", cw_val_bool(false));
  /* smoke: soft grey puffs that rise, swell and fade */
  for (int i = 0; i < RL_PUFFS; i++) {
    rl_gb *pb = rl_gb_new();
    rl_gb_dome(pb, (t3_vec3){ 0, -0.02f, 0 }, 0.05f, 0.05f, 8, 0xffffff);
    rl_gb_dome(pb, (t3_vec3){ 0, 0.02f, 0 }, 0.05f, -0.05f, 8, 0xffffff);
    RV.puff_mat[i] = rl_flat_material(0xdcdcdc, 1.0f, 0);
    RV.puff_mat[i]->transparent = true;
    RV.puff_mat[i]->depth_write = false;
    RV.puff_mat[i]->opacity = 0;
    RV.puff[i] = rl_mesh_node(RV.world, "Puff", rl_gb_geometry(pb), &RV.puff_mat[i], 1, false);
    rl_gb_free(pb);
    cw_node_set(RV.puff[i], "visible", cw_val_bool(false));
  }
}

static void build_ghost(void) {
  RV.ghost = cw_node_new("Node3D", "GhostTile");
  cw_node_add_child(RV.world, RV.ghost);
  t3_material *bal = rl_flat_material(0xffffff, 0.9f, 0), *wood = rl_flat_material(0x6b4a2e, 0.8f, 0), *steel = rl_flat_material(0xd0d0d6, 0.4f, 0.6f);
  t3_material *mats[3] = { bal, wood, steel };
  for (int e = 0; e < 6; e++)
    for (int d = 0; d < 6; d++) {
      if (e == d) continue;
      RV.ghost_pair[pair_key(e, d)] = rl_mesh_node(RV.ghost, "GhostPair", rl_track_geometry(e, d, false), mats, 3, false);
      cw_node_set(RV.ghost_pair[pair_key(e, d)], "visible", cw_val_bool(false));
    }
  t3_release(bal), t3_release(wood), t3_release(steel);
  /* the preview tile: see-through by mesh opacity, no shadow, from the first frame */
  if (bk_ghost_set(RV.ghost, 0.5f) != 30) CW_FATAL("smokestack: the ghost tile has not its 30 track meshes");
  cw_node_set(RV.ghost, "visible", cw_val_bool(false));
  /* route preview beads */
  for (int i = 0; i < RL_BEADS; i++) {
    RV.bead[i] = cw_node_new("MeshInstance3D", "Bead");
    cw_node_add_child(RV.world, RV.bead[i]);
    cw_node_set(RV.bead[i], "primitive", cw_val_enum("SPHERE"));
    cw_node_set(RV.bead[i], "size", cw_val_vec3(0.055f, 0.055f, 0.055f));
    cw_node_set(RV.bead[i], "visible", cw_val_bool(false));
  }
  /* the cursor: a ring on the table and a pointer above */
  RV.marker = cw_node_new("MeshInstance3D", "Marker");
  cw_node_add_child(RV.world, RV.marker);
  cw_node_set(RV.marker, "primitive", cw_val_enum("CYLINDER"));
  cw_node_set(RV.marker, "size", cw_val_vec3(0.2f, 0.012f, 0.2f));
  cw_node_set(RV.marker, "layers", cw_val_int(2));
  RV.pointer = cw_node_new("MeshInstance3D", "Pointer");
  cw_node_add_child(RV.world, RV.pointer);
  cw_node_set(RV.pointer, "primitive", cw_val_enum("SPHERE"));
  cw_node_set(RV.pointer, "size", cw_val_vec3(0.07f, 0.07f, 0.07f));
  cw_node_set(RV.pointer, "layers", cw_val_int(2));
}

/* ── cards: job cards and option plaques, faces painted once ── */
static void paint(int face, cw_vec2 px, void *user) {
  (void)user;
  rl_paint_face(face, px);
}

static void build_cards(void) {
  bk_cards_init(RV.tree, paint, NULL);
  for (int a = 0; a < RL_ACTIONS; a++) {
    RV.job[a] = bk_card_new(RV.world, "Job", &(bk_card_desc){ .width = 1.55f, .height = 1.12f, .thickness = 0.02f, .corner = 0.05f,
                                                               .front = RL_FACE_JOB0 + a, .back = BK_FACE_BACK, .face_px = 400,
                                                               .edge = { 0.85f, 0.8f, 0.7f, 1 } });
    RV.job_token[a] = cw_node_new("MeshInstance3D", "JobToken");
    cw_node_add_child(RV.world, RV.job_token[a]);
    cw_node_set(RV.job_token[a], "primitive", cw_val_enum("CYLINDER"));
    cw_node_set(RV.job_token[a], "size", cw_val_vec3(0.22f, 0.05f, 0.22f));
    cw_node_set(RV.job_token[a], "roughness", cw_val_float(0.5f));
    cw_node_set(RV.job_token[a], "cast_shadow", cw_val_bool(true));
    cw_node_set(RV.job_token[a], "visible", cw_val_bool(false));
  }
  for (int i = 0; i < RL_OPTS; i++) {
    RV.opt[i] = bk_card_new(RV.world, "Option", &(bk_card_desc){ .width = 1.2f, .height = 0.5f, .thickness = 0.03f, .corner = 0.06f,
                                                                  .front = RL_FACE_OPT0, .back = -1, .face_px = 200,
                                                                  .edge = { 0.35f, 0.25f, 0.16f, 1 } });
    RV.opt_face[i] = RL_FACE_OPT0;
    cw_node_set(RV.opt[i], "visible", cw_val_bool(false));
  }
}

void rl_view_init(cw_tree *tree, cw_node world, cw_node cam, rl_game *g) {
  memset(&RV, 0, sizeof RV);
  RV.tree = tree, RV.world = world, RV.cam = cam, RV.g = g;
  RV.map = -1;
  /* the table under the board */
  rl_gb *b = rl_gb_new();
  rl_gb_box(b, (t3_vec3){ 0, -0.26f, 0.4f }, (t3_vec3){ 9.5f, 0.22f, 6.0f }, 0, 0x4a2d18);
  for (int i = 0; i < 18; i++) /* planks */
    rl_gb_box(b, (t3_vec3){ 0, -0.035f, -5.4f + 0.66f * (float)i }, (t3_vec3){ 9.4f, 0.003f, 0.316f }, 0, rl_shade(0x5b3820, 0.9f + 0.2f * rnd((uint32_t)i)));
  t3_material *tm = rl_vc_material(0.7f);
  rl_mesh_node(world, "Table", rl_gb_geometry(b), &tm, 1, false);
  t3_release(tm);
  rl_gb_free(b);
  for (int m = 0; m < RL_MAPS; m++) build_board(m);
  build_train();
  build_ghost();
  build_cards();
  bk_deltas_init(&RV.deltas);
  RV.deltas.rise = 0.03f; /* a short rise: it stays under its panel */
}

void rl_view_start(void) {
  const rl_state *s = rl_game_state(RV.g);
  if (RV.map != s->map) {
    for (int m = 0; m < RL_MAPS; m++) cw_node_set(RV.board[m].root, "visible", cw_val_bool(m == s->map));
    RV.map = s->map;
    /* the camera frames the board, the job cards and the option row */
    cw_vec2 h = rl_board_half(s->map);
    /* wide enough for the board and the job cards beside it (68.6 degrees across at a 42 degree field) */
    float w = 2 * h.x + 2.3f, d = w * 0.5f / 0.682f * 1.2f;
    t3_vec3 tgt = { 0.1f, 0, 1.05f };
    cw_node3d_set_position(RV.cam, (t3_vec3){ tgt.x, tgt.y + d * 0.83f, tgt.z + d * 0.56f });
    cw_node3d_look_at(RV.cam, tgt, (t3_vec3){ 0, 1, 0 });
  }
  board_view *bv = &RV.board[s->map];
  for (int h = 0; h < RL_MAX_HEX; h++) {
    if (cw_node_valid(bv->tile[h])) cw_node_set(bv->tile[h], "visible", cw_val_bool(false));
    bv->tile_key[h] = -1;
    if (cw_node_valid(bv->decor[h])) cw_node_set(bv->decor[h], "visible", cw_val_bool(true));
  }
  for (int k = 0; k < RL_MAX_SET; k++)
    for (int i = 0; i < RL_CITY_CUBES; i++) bv->cube_shown[k][i] = 0;
  RV.train_on = false;
  RV.haul_seen = 0;
  for (int p = 0; p < RL_MAX_PLAYERS; p++) {
    bk_counter_init(&RV.money[p], s->p[p].money);
    bk_counter_init(&RV.points[p], s->p[p].points);
    RV.shown_income[p] = s->p[p].income;
  }
  RV.log_at = 0;
}

const rl_view_stats *rl_view_stats_get(void) { return &RV.stats; }

/* ── per frame ── */
static float ease_out_back(float t) {
  const float c1 = 1.4f, c3 = c1 + 1;
  float u = t - 1;
  return 1 + c3 * u * u * u + c1 * u * u;
}

static void sync_tiles(float dt) {
  const rl_state *s = rl_game_state(RV.g);
  board_view *bv = &RV.board[s->map];
  const rl_map_def *m = rl_map(s->map);
  for (int h = 0; h < m->nhex; h++) {
    int link = s->tile_link[h];
    int key = link >= 0 ? (s->link[link].owner * 64 + pair_key(s->tile_in[h], s->tile_out[h])) : -1;
    if (key != bv->tile_key[h]) {
      if (cw_node_valid(bv->tile[h])) cw_node_free(bv->tile[h]), bv->tile[h] = (cw_node){ 0 };
      bv->tile_key[h] = key;
      if (key >= 0) {
        int owner = s->link[link].owner;
        if (!RV.ballast[owner]) {
          RV.ballast[owner] = rl_flat_material(rl_mix(0x8c8478, RL_SEAT_RGB[owner], 0.75f), 0.95f, 0);
          RV.wood = rl_flat_material(0x6b4a2e, 0.85f, 0);
          RV.steel = rl_flat_material(0xc9c9cf, 0.35f, 0.75f);
        }
        t3_material *mats[3] = { RV.ballast[owner], RV.wood, RV.steel };
        bv->tile[h] = rl_mesh_node(bv->root, "Tile", rl_track_geometry(s->tile_in[h], s->tile_out[h], m->terrain[h] == RL_T_RIVER), mats, 3, true);
        bv->tile_t[h] = 0;
        RV.stats.tiles_shown++;
      }
      if (cw_node_valid(bv->decor[h])) cw_node_set(bv->decor[h], "visible", cw_val_bool(key < 0));
    }
    if (key >= 0) { /* it drops in, lands with a small bounce and settles */
      float t0 = bv->tile_t[h];
      bv->tile_t[h] += dt;
      float t = bv->tile_t[h] / 0.45f;
      if (t0 / 0.45f < 1 && t >= 1) RV.stats.tiles_landed++;
      if (t > 1) t = 1;
      float y = rl_ground(m->terrain[h]) + 0.55f * (1 - ease_out_back(t)) + (t < 1 ? 0 : 0);
      t3_vec3 c = rl_hex_at(s->map, h, 0);
      cw_node3d_set_position(bv->tile[h], (t3_vec3){ c.x, y, c.z });
      cw_node3d_set_rotation(bv->tile[h], (t3_vec3){ 0, 0.4f * (1 - t) * (1 - t), 0 });
    }
  }
}

static void sync_settlements(float dt) {
  const rl_state *s = rl_game_state(RV.g);
  board_view *bv = &RV.board[s->map];
  const rl_map_def *m = rl_map(s->map);
  int turn = bk_match_turn(RV.g->match);
  int lift_city = s->phase == RL_PH_ROUTE ? s->good_city : -1, lift_slot = s->good_slot;
  for (int k = 0; k < m->nset; k++) {
    bool city = s->set_kind[k] == RL_CITY;
    settlement_view *cv = &bv->set[k][RL_CITY];
    cw_node_set(cv->root, "visible", cw_val_bool(city));
    if (cw_node_valid(bv->set[k][RL_TOWN].root)) cw_node_set(bv->set[k][RL_TOWN].root, "visible", cw_val_bool(!city));
    if (city) cv->roof->color = rl_lin(RL_GOODS_RGB[s->set_color[k]]);
    else bv->set[k][RL_TOWN].roof->color = rl_lin(0x8a7d6e);
    /* windows: a warm glow that swells when a good arrives and fades back */
    for (int kind = 0; kind < 2; kind++) {
      settlement_view *sv = &bv->set[k][kind];
      if (!sv->win) continue;
      if (sv->glow > 0) sv->glow -= dt * 0.7f;
      if (sv->glow < 0) sv->glow = 0;
      float g = sv->glow > 1 ? 1 : sv->glow;
      float swell = g * g * (3 - 2 * g);
      sv->win->emissive_intensity = 0.45f + 3.2f * swell;
    }
    for (int i = 0; i < RL_CITY_CUBES; i++) {
      cw_node q = bv->cube[k][i];
      int c = s->cube[k][i];
      if (c != bv->cube_shown[k][i]) {
        if (c) {
          cw_color col = cw_hex((RL_GOODS_RGB[c - 1] << 8) | 0xff);
          cw_node_set(q, "color", cw_val_color(col.r, col.g, col.b, 1));
          if (!bv->cube_shown[k][i]) bv->cube_t[k][i] = 0;
        }
        bv->cube_shown[k][i] = (uint8_t)c;
        cw_node_set(q, "visible", cw_val_bool(c != 0));
      }
      if (!c) continue;
      bv->cube_t[k][i] += dt;
      float t = bv->cube_t[k][i] / 0.5f;
      if (t > 1) t = 1;
      t3_vec3 p = rl_good_at(s->map, k, i);
      p.y += 0.7f * (1 - t) * (1 - t);
      if (k == lift_city && i == lift_slot) p.y += 0.12f + 0.02f * sinf((float)cw_time() * 2.5f);
      cw_node3d_set_position(q, p);
      int tg = turn >= 0 ? bk_select_target(RV.g->sel, turn) : -1;
      bool hover = tg >= 0 && RV.g->tmove[tg] == rl_mv(RL_MV_GOOD, k, i);
      cw_node3d_set_scale(q, hover ? (t3_vec3){ 1.25f, 1.25f, 1.25f } : (t3_vec3){ 1, 1, 1 });
    }
  }
}

/* the route's path through the world: settlement to settlement over its links' tiles */
static int route_path(const rl_state *s, int src, const rl_route *r, t3_vec3 *out, int cap) {
  const rl_map_def *m = rl_map(s->map);
  int n = 0, at = src;
  t3_vec3 c = rl_hex_at(s->map, m->set_hex[at], TRACK_H + 0.02f);
  out[n++] = c;
  for (int i = 0; i < r->n; i++) {
    const rl_link *l = &s->link[r->link[i]];
    int8_t hx[RL_MAX_HEX];
    int nh = rl_link_hexes(s, r->link[i], hx, RL_MAX_HEX);
    bool fwd = l->from == at;
    for (int j = 0; j < nh; j++) {
      int h = hx[fwd ? j : nh - 1 - j];
      t3_vec3 hc = rl_hex_at(s->map, h, TRACK_H + 0.02f);
      for (int k = 0; k <= 6 && n < cap - 1; k++) {
        float t = (float)k / 6;
        t3_vec3 p = rl_curve(s->tile_in[h], s->tile_out[h], fwd ? t : 1 - t);
        out[n++] = (t3_vec3){ hc.x + p.x, hc.y, hc.z + p.z };
      }
    }
    at = fwd ? l->to : l->from;
    if (n < cap) out[n++] = rl_hex_at(s->map, m->set_hex[at], TRACK_H + 0.02f);
  }
  return n;
}

static t3_vec3 path_at(const t3_vec3 *p, const float *acc, int n, float d, t3_vec3 *dir) {
  int i = 1;
  while (i < n - 1 && acc[i] < d) i++;
  float seg = acc[i] - acc[i - 1], f = seg > 1e-6f ? (d - acc[i - 1]) / seg : 0;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  *dir = (t3_vec3){ p[i].x - p[i - 1].x, 0, p[i].z - p[i - 1].z };
  return (t3_vec3){ p[i - 1].x + (p[i].x - p[i - 1].x) * f, p[i - 1].y + (p[i].y - p[i - 1].y) * f, p[i - 1].z + (p[i].z - p[i - 1].z) * f };
}

static void place_car(cw_node n, t3_vec3 p, t3_vec3 dir) {
  cw_node3d_set_position(n, p);
  cw_node3d_set_scale(n, (t3_vec3){ 1.6f, 1.6f, 1.6f });
  cw_node3d_set_rotation(n, (t3_vec3){ 0, atan2f(-dir.z, dir.x), 0 });
}

static void run_trains(float dt) {
  const rl_state *s = rl_game_state(RV.g);
  if (!RV.train_on && RV.haul_seen < RV.g->haul_n) { /* the next haul: a train along its route */
    const rl_haul_event *e = &RV.g->haul[RV.haul_seen++];
    RV.train_n = route_path(s, e->src, &e->route, RV.train_path, RL_PATH_MAX);
    RV.train_acc[0] = 0;
    for (int i = 1; i < RV.train_n; i++)
      RV.train_acc[i] = RV.train_acc[i - 1] + hypotf(RV.train_path[i].x - RV.train_path[i - 1].x, RV.train_path[i].z - RV.train_path[i - 1].z);
    float len = RV.train_acc[RV.train_n - 1];
    RV.train_time = len / 1.6f;
    if (RV.train_time < 1.8f) RV.train_time = 1.8f;
    if (RV.train_time > 4.5f) RV.train_time = 4.5f;
    RV.train_t = 0;
    RV.train_on = true;
    RV.train_dest = e->route.dest;
    RV.train_seat = e->seat;
    RV.train_arrived = false;
    RV.train_accent->color = rl_lin(RL_SEAT_RGB[e->seat]);
    cw_color col = cw_hex((RL_GOODS_RGB[e->color] << 8) | 0xff);
    cw_node_set(RV.wagon_good, "color", cw_val_color(col.r, col.g, col.b, 1));
    RV.stats.trains_run++;
  }
  if (RV.train_on) {
    RV.train_t += dt;
    float len = RV.train_acc[RV.train_n - 1];
    float u = RV.train_t / RV.train_time;
    if (u > 1) u = 1;
    float e = u * u * (3 - 2 * u); /* pull away, run, ease into the station */
    float d = e * len;
    t3_vec3 dir, dir2;
    t3_vec3 p = path_at(RV.train_path, RV.train_acc, RV.train_n, d, &dir);
    t3_vec3 q = path_at(RV.train_path, RV.train_acc, RV.train_n, d - 0.42f < 0 ? 0 : d - 0.42f, &dir2);
    place_car(RV.loco, p, dir);
    place_car(RV.wagon, q, dir2);
    cw_node_set(RV.loco, "visible", cw_val_bool(true));
    cw_node_set(RV.wagon, "visible", cw_val_bool(true));
    /* smoke from the chimney while running */
    RV.puff_clock += dt;
    if (u < 1 && RV.puff_clock > 0.16f) {
      RV.puff_clock = 0;
      int i = RV.puff_next++ % RL_PUFFS;
      float yaw = atan2f(-dir.z, dir.x);
      RV.puff_at[i] = (t3_vec3){ p.x + 0.13f * cosf(yaw), p.y + 0.34f, p.z - 0.13f * sinf(yaw) };
      RV.puff_age[i] = 0.001f;
    }
    if (u >= 1 && !RV.train_arrived) {
      RV.train_arrived = true;
      board_view *bv = &RV.board[s->map];
      bv->set[RV.train_dest][RL_CITY].glow = 1.6f;
      if (cw_node_valid(bv->set[RV.train_dest][RL_TOWN].root)) bv->set[RV.train_dest][RL_TOWN].glow = 1.6f;
      RV.stats.trains_arrived++;
      RV.stats.glow_peaks++;
    }
    if (RV.train_t > RV.train_time + 0.7f) {
      RV.train_on = false;
      cw_node_set(RV.loco, "visible", cw_val_bool(false));
      cw_node_set(RV.wagon, "visible", cw_val_bool(false));
    }
  }
  for (int i = 0; i < RL_PUFFS; i++) {
    if (RV.puff_age[i] <= 0) { cw_node_set(RV.puff[i], "visible", cw_val_bool(false)); continue; }
    RV.puff_age[i] += dt;
    float a = RV.puff_age[i] / 1.4f;
    if (a >= 1) { RV.puff_age[i] = 0; continue; }
    float sc = 0.5f + 1.6f * a;
    cw_node3d_set_position(RV.puff[i], (t3_vec3){ RV.puff_at[i].x + 0.08f * a, RV.puff_at[i].y + 0.35f * a, RV.puff_at[i].z });
    cw_node3d_set_scale(RV.puff[i], (t3_vec3){ sc, sc, sc });
    RV.puff_mat[i]->opacity = 0.55f * (1 - a) * (a < 0.15f ? a / 0.15f : 1);
    cw_node_set(RV.puff[i], "visible", cw_val_bool(true));
  }
}

static void sync_cards(float dt) {
  (void)dt;
  const rl_state *s = rl_game_state(RV.g);
  int turn = bk_match_turn(RV.g->match);
  bool person = turn >= 0 && !rl_game_is_cpu(RV.g, turn);
  for (int a = 0; a < RL_ACTIONS; a++) {
    t3_vec3 at = rl_act_at(s->map, a);
    float glow = turn >= 0 ? bk_select_glow(RV.g->sel, turn, RL_T_ACT0 + a) : 0;
    bool free_ = s->action_owner[a] < 0;
    at.y += free_ && s->phase == RL_PH_ACTION ? 0.1f * glow : 0;
    float sc = free_ && s->phase == RL_PH_ACTION ? 1 + 0.1f * glow : 1;
    cw_node3d_set_position(RV.job[a], at);
    cw_node3d_set_scale(RV.job[a], (t3_vec3){ sc, sc, sc });
    cw_node3d_set_rotation(RV.job[a], (t3_vec3){ 0, 0, 0 });
    bool owned = !free_;
    cw_node_set(RV.job_token[a], "visible", cw_val_bool(owned));
    if (owned) {
      cw_color c = cw_hex((RL_SEAT_RGB[s->action_owner[a]] << 8) | 0xff);
      cw_node_set(RV.job_token[a], "color", cw_val_color(c.r, c.g, c.b, 1));
      cw_node3d_set_position(RV.job_token[a], (t3_vec3){ at.x - 0.89f, at.y + 0.04f, at.z }); /* beside the card, its words clear */
    }
  }
  for (int i = 0; i < RL_OPTS; i++) {
    bool on = RV.g->tmove[RL_T_OPT0 + i] != BK_MOVE_NONE && !bk_match_over(RV.g->match);
    cw_node_set(RV.opt[i], "visible", cw_val_bool(on));
    if (!on) continue;
    int face = rl_option_face(s, RV.g->tmove[RL_T_OPT0 + i]);
    if (face != RV.opt_face[i]) bk_card_set_front(RV.opt[i], face), RV.opt_face[i] = face;
    float glow = turn >= 0 ? bk_select_glow(RV.g->sel, turn, RL_T_OPT0 + i) : 0;
    t3_vec3 at = rl_opt_at(s->map, i, rl_opt_count(s->phase));
    at.y += 0.08f * glow * (person ? 1.0f : 0.7f);
    float sc = 1 + 0.12f * glow;
    cw_node3d_set_position(RV.opt[i], at);
    cw_node3d_set_scale(RV.opt[i], (t3_vec3){ sc, sc, sc });
    cw_node3d_set_rotation(RV.opt[i], (t3_vec3){ -0.35f, 0, 0 });
  }
}

static void sync_cursor(float dt) {
  (void)dt;
  const rl_state *s = rl_game_state(RV.g);
  int turn = bk_match_turn(RV.g->match);
  int tg = turn >= 0 && !bk_match_over(RV.g->match) ? bk_select_target(RV.g->sel, turn) : -1;
  bool show = tg >= 0 && RV.g->tmove[tg] != BK_MOVE_NONE && !(RV.g->help_open && !rl_game_is_cpu(RV.g, turn));
  cw_node_set(RV.marker, "visible", cw_val_bool(show));
  cw_node_set(RV.pointer, "visible", cw_val_bool(show));
  bool ghost = false, beads = false;
  if (show) {
    t3_vec3 at = RV.g->tpos[tg];
    float bob = 0.03f * sinf((float)cw_time() * 2.2f);
    cw_color c = cw_hex((RL_SEAT_RGB[turn] << 8) | 0xff);
    float ring = tg < RL_T_SITE0 ? 0.0f : 0.2f;
    cw_node3d_set_position(RV.marker, (t3_vec3){ at.x, at.y + 0.005f, at.z });
    cw_node_set(RV.marker, "visible", cw_val_bool(ring > 0));
    cw_node_set(RV.marker, "color", cw_val_color(c.r, c.g, c.b, 1));
    cw_node_set(RV.marker, "emissive", cw_val_color(c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 1));
    cw_node3d_set_position(RV.pointer, (t3_vec3){ at.x, at.y + (tg < RL_T_SITE0 ? 0.42f : 0.32f) + bob, at.z });
    cw_node3d_set_scale(RV.pointer, tg < RL_T_SITE0 ? (t3_vec3){ 1.6f, 1.6f, 1.6f } : (t3_vec3){ 1, 1, 1 });
    cw_node_set(RV.pointer, "color", cw_val_color(c.r, c.g, c.b, 1));
    cw_node_set(RV.pointer, "emissive", cw_val_color(c.r * 0.7f, c.g * 0.7f, c.b * 0.7f, 1));
    bk_move m = RV.g->tmove[tg];
    int k = rl_mv_kind(m);
    /* previews: the tile a build would lay, the route a haul would take */
    if (k == RL_MV_START || k == RL_MV_EXIT) {
      int h, e, d;
      if (k == RL_MV_START) h = rl_mv_a(m), e = rl_mv_b(m), d = (e + 3) % 6;
      else h = rl_next_tile(s, turn, &e, NULL), d = rl_mv_a(m);
      if (h >= 0 && e >= 0) {
        for (int i = 0; i < 36; i++)
          if (cw_node_valid(RV.ghost_pair[i])) cw_node_set(RV.ghost_pair[i], "visible", cw_val_bool(i == pair_key(e, d)));
        t3_vec3 c3 = rl_hex_at(s->map, h, 0.005f);
        cw_node3d_set_position(RV.ghost, c3);
        ghost = true;
      }
    }
    if (k == RL_MV_DEST) {
      rl_route r[RL_MAX_ROUTES];
      int nr = rl_routes(s, s->good_city, s->good_slot, turn, r, RL_MAX_ROUTES);
      for (int i = 0; i < nr; i++)
        if (r[i].dest == rl_mv_a(m)) {
          t3_vec3 path[RL_PATH_MAX];
          float acc[RL_PATH_MAX];
          int n = route_path(s, s->good_city, &r[i], path, RL_PATH_MAX);
          acc[0] = 0;
          for (int j = 1; j < n; j++) acc[j] = acc[j - 1] + hypotf(path[j].x - path[j - 1].x, path[j].z - path[j - 1].z);
          float len = acc[n - 1], step = 0.16f, drift = fmodf((float)cw_time() * 0.25f, step);
          int nb = 0;
          for (float d = drift; d < len && nb < RL_BEADS; d += step, nb++) {
            t3_vec3 dir, p = path_at(path, acc, n, d, &dir);
            int owner = -1;
            (void)owner;
            cw_node3d_set_position(RV.bead[nb], (t3_vec3){ p.x, p.y + 0.06f, p.z });
            cw_node_set(RV.bead[nb], "color", cw_val_color(c.r, c.g, c.b, 1));
            cw_node_set(RV.bead[nb], "emissive", cw_val_color(c.r * 0.8f, c.g * 0.8f, c.b * 0.8f, 1));
            cw_node_set(RV.bead[nb], "visible", cw_val_bool(true));
          }
          for (int j = nb; j < RL_BEADS; j++) cw_node_set(RV.bead[j], "visible", cw_val_bool(false));
          beads = nb > 0;
          RV.stats.route_shown++;
        }
    }
  }
  if (!beads)
    for (int j = 0; j < RL_BEADS; j++) cw_node_set(RV.bead[j], "visible", cw_val_bool(false));
  cw_node_set(RV.ghost, "visible", cw_val_bool(ghost));
  if (ghost) RV.stats.ghost_shown++;
}

/* queue a score change at a panel spot (0 money, 1 points); a full queue folds into its last entry */
static void queue_delta(int p, int which, int d) {
  int *n = &RV.dq_n[p][which];
  if (*n < 8) RV.dq[p][which][(*n)++] = d;
  else RV.dq[p][which][7] += d;
}
#define RL_DELTA_GAP 1.45f /* seconds between two changes at one spot: the first is gone before the next shows */
static void show_deltas(float dt) {
  for (int p = 0; p < RL_MAX_PLAYERS; p++)
    for (int w = 0; w < 2; w++) {
      RV.dq_wait[p][w] += dt;
      if (!RV.dq_n[p][w] || RV.dq_wait[p][w] < RL_DELTA_GAP) continue;
      int d = RV.dq[p][w][0];
      memmove(&RV.dq[p][w][0], &RV.dq[p][w][1], sizeof(int) * 7);
      RV.dq_n[p][w]--;
      RV.dq_wait[p][w] = 0;
      if (d) bk_deltas_add(&RV.deltas, rl_hud_panel_point(p, w), d, w ? cw_hex((RL_SEAT_RGB[p] << 8) | 0xff) : cw_hex(0xf2d27aff));
    }
}
static void sync_scores(void) {
  const rl_state *s = rl_game_state(RV.g);
  for (int p = 0; p < s->players; p++) {
    if (s->p[p].points != RV.points[p].target) {
      queue_delta(p, 1, s->p[p].points - RV.points[p].target);
      bk_counter_set(&RV.points[p], s->p[p].points);
    }
    if (s->p[p].money != RV.money[p].target) {
      queue_delta(p, 0, s->p[p].money - RV.money[p].target);
      bk_counter_set(&RV.money[p], s->p[p].money);
    }
  }
}

void rl_view_update(float dt) {
  if (!RV.g->match) return;
  sync_tiles(dt);
  sync_settlements(dt);
  run_trains(dt);
  sync_cards(dt);
  sync_cursor(dt);
  sync_scores();
  for (int p = 0; p < RL_MAX_PLAYERS; p++) bk_counter_update(&RV.money[p], dt), bk_counter_update(&RV.points[p], dt);
  show_deltas(dt);
  bk_deltas_update(&RV.deltas, dt);
  bk_select_update(RV.g->sel, dt);
}
