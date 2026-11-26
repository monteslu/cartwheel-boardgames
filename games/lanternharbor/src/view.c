/* Lantern Harbor: the harbour in 3D and the HUD over it. See view.h. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_shell.h"
#include "boardkit/bk_view.h"
#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "cartwheel/water.h"
#include "three.h"

#define BOARD_TOP 0.16f
#define CARD_T 0.06f
#define SMALL_W 1.34f
#define GRAND_W 2.84f
#define ROLE_W 2.5f
#define MARKET_W 2.0f   /* market tiles: larger than the town's, the same faces */
#define MARKET_GW 2.9f
#define ROLE_H 1.7f
#define FACE_ROLE 100
#define FACE_PLOT 200
#define FACE_PLOT_BACK BK_FACE_BACK

/* colours of the goods (barrels, HUD) in the rules' order: rice, indigo, glass, tea, spice */
static const uint32_t GOOD_HEX[HB_GOODS] = { 0xe9dfc2ff, 0x3a3f9eff, 0x4fc3b8ff, 0x5d8a3aff, 0xd2632aff };
static const uint32_t GOOD_HOOP[HB_GOODS] = { 0x8a7a55ff, 0x1d1f55ff, 0x237a72ff, 0x34521eff, 0x7a3210ff };

/* ── pieces that move between places ────────────────────────────────── */

enum { K_HIDDEN, K_GOODS, K_HOLD, K_STALL, K_PLOT, K_ROW, K_TOWN, K_FERRY, K_DOCK, K_PLACE_PLOT, K_PLACE_BLD };
static uint32_t key(int kind, int a, int b, int c) { return (uint32_t)kind << 24 | (uint32_t)(a & 0xff) << 16 | (uint32_t)(b & 0xff) << 8 | (uint32_t)(c & 0xff); }
static int key_kind(uint32_t k) { return (int)(k >> 24); }

typedef struct piece {
  cw_node node;
  uint32_t key;
  int boat;              /* the boat (0..2) or the ferry (3) it rides, else -1: the boat's sailing offset moves it */
  t3_vec3 pos, prev, from, to;
  float yaw, prev_yaw;
  int t, dur, delay, settle;
  bool moving, visible, hide_on_arrival;
} piece;

typedef struct want { uint32_t key; t3_vec3 p; int boat; } want;

#define MAX_FOLK 80
#define MAX_PLOTS 14
#define MAX_BARRELS 12
#define MAX_TILES 4

typedef struct popup { t3_vec3 at; char text[24]; uint32_t colour; int age, life; } popup;
#define POPUPS 24

static struct {
  cw_tree *tree;
  cw_node world, camera, root, water;
  cw_node lights[4];
  /* materials and shapes */
  t3_material *barrel_mat[HB_GOODS], *ghost_mat, *pawn_mat[4], *coin_mat, *lantern_lit[LY_LANTERNS], *paper_dark;
  t3_geometry *barrel_geo, *pawn_geo, *coin_geo;
  /* pieces */
  piece barrel[HB_GOODS][MAX_BARRELS];
  int nbarrel[HB_GOODS];
  piece plot[HB_PLOT_KINDS][MAX_PLOTS];
  int nplot[HB_PLOT_KINDS];
  piece tile[HB_BUILDINGS][MAX_TILES];
  int ntile[HB_BUILDINGS];
  piece folk[MAX_FOLK];
  int nfolk;
  /* fixed things */
  cw_node role_card[HB_ROLES], role_coins[HB_ROLES][6];
  cw_node market_card[HB_BUILDINGS], market_stack[HB_BUILDINGS];
  cw_node pile_card, stone_card;
  cw_node boards[BK_MAX_SEATS], glow[BK_MAX_SEATS], coins[BK_MAX_SEATS];
  cw_node boat[HB_SHIPS + 1], cargo[HB_SHIPS][7], boat_lamp[HB_SHIPS + 1];
  cw_node lantern[LY_LANTERNS];
  cw_node pass_sign;
  cw_node ghost_card_small, ghost_card_grand, ghost_plot, ghost_pawn, ghost_barrel[7];
  /* state followed */
  int players, nships;
  ly_table lay;
  hb_state seen;
  bool have_seen;
  int ev_seen;
  /* boats sailing: offset along x, the trip's phase */
  float boat_x[HB_SHIPS + 1], boat_prev[HB_SHIPS + 1];
  int boat_t[HB_SHIPS + 1], boat_trip[HB_SHIPS + 1]; /* trip: 0 moored, 1 leaving, 2 arriving */
  int cargo_good[HB_SHIPS], cargo_n[HB_SHIPS];
  float lit[LY_LANTERNS];
  int lit_want;
  float role_lift[HB_ROLES];
  float seat_glow[BK_MAX_SEATS];
  float shown_coins[BK_MAX_SEATS];
  popup pop[POPUPS];
  int ticks;
  cw_sound snd_knock, snd_chime, snd_coin, snd_horn, snd_turn;
  bool attract;
  int warm; /* frames left of the warm-up: one of every piece drawn out of sight, so every draw state is surveyed and made */
} V;

/* ── helpers ────────────────────────────────────────────────────────── */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 at2(ly_point p, float y) { return (t3_vec3){ p.x, y, p.z }; }

/* a picture is colour (sRGB), said before its first load: a PNG would load as data */
static t3_texture *texture(const char *path) {
  if (!cw_res_texture_set_srgb(path, true)) CW_FATAL("lanternharbor: %s was loaded as data before it could be said to be colour", path);
  cw_res r = cw_res_load(path, CW_RES_TEXTURE);
  if (cw_res_wait(r) != CW_RES_STATE_READY) CW_FATAL("lanternharbor: texture %s did not load", path);
  t3_texture *t = cw_res_texture(r);
  if (!t) CW_FATAL("lanternharbor: %s is not a texture", path);
  if (t->color_space != T3_SRGB_COLOR_SPACE) CW_FATAL("lanternharbor: %s did not load as a colour picture", path);
  return t;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_geometry *geo, t3_material *mat, bool cast, bool receive) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  t3_mesh *m = t3_mesh_new(geo, mat);
  m->base.cast_shadow = cast;
  m->base.receive_shadow = receive;
  if (!cw_mesh3d_set_procedural(n, m)) CW_FATAL("lanternharbor: could not attach the mesh of %s", name);
  t3_release(m);
  return n;
}

static t3_material *physical(uint32_t rgb, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(rgb);
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

static void set_visible(cw_node n, bool on) { cw_node_set(n, "visible", cw_val_bool(on)); }

static t3_material *node_material(cw_node n) {
  t3_object *o = cw_node3d_object(n);
  if (!o || !o->child_count) return NULL;
  t3_mesh *m = (t3_mesh *)o->children[0];
  return m->material_count ? m->materials[0] : NULL;
}

/* a lantern's paper from dark to glowing (k 0..1) */
static void set_glow(cw_node n, float k) {
  t3_material *m = node_material(n);
  if (!m) return;
  t3_color c = { 0.1f + 0.9f * k, 0.013f + 0.25f * k, 0.006f + 0.02f * k }; /* linear: dark red paper to a deep orange glow */
  if (fabsf(m->color.r - c.r) > 0.002f) { m->color = c; m->version++; }
}

static void set_emissive(cw_node n, float k) {
  t3_material *m = node_material(n);
  if (m && fabsf(m->emissive_intensity - k) > 0.002f) { m->emissive_intensity = k; m->version++; }
}

static t3_geometry *lathe(const float *xy, int n, int seg) {
  t3_vec2 pts[32];
  for (int i = 0; i < n && i < 32; i++) pts[i] = (t3_vec2){ xy[2 * i], xy[2 * i + 1] };
  return t3_lathe_geometry_new(pts, n, seg, 0, 6.2831853f);
}

/* a boat's hull: stations along x, each a rounded U from one gunwale over the keel to the other, pointed at both
 * ends (the bow sharper). Length len, beam w, depth d; the deck at y 0. */
static t3_geometry *hull_geometry(float len, float w, float d) {
  enum { ST = 18, RIM = 9 };
  static float pos[(ST + 1) * RIM * 3], nrm[(ST + 1) * RIM * 3], uv[(ST + 1) * RIM * 2];
  static uint16_t idx[ST * (RIM - 1) * 6 + 64];
  int nv = 0, ni = 0;
  for (int i = 0; i <= ST; i++) {
    float u = (float)i / ST, x = (u - 0.5f) * len;
    float shape = powf(sinf(3.14159f * u), u > 0.5f ? 0.75f : 0.5f);
    float half = 0.5f * w * fmaxf(shape, 0.04f), depth = d * (0.35f + 0.65f * fmaxf(shape, 0.0f));
    for (int k = 0; k < RIM; k++) {
      float a = 3.14159f * (float)k / (RIM - 1); /* 0 one gunwale, pi the other */
      float z = cosf(a) * half, y = -sinf(a) * depth + 0.12f * fabsf(u - 0.5f) * d;
      pos[nv * 3] = x, pos[nv * 3 + 1] = y, pos[nv * 3 + 2] = z;
      float nz = cosf(a), ny = -sinf(a);
      nrm[nv * 3] = 0, nrm[nv * 3 + 1] = ny, nrm[nv * 3 + 2] = nz;
      uv[nv * 2] = u, uv[nv * 2 + 1] = (float)k / (RIM - 1);
      nv++;
    }
  }
  for (int i = 0; i < ST; i++)
    for (int k = 0; k < RIM - 1; k++) {
      int a = i * RIM + k, b = a + RIM;
      idx[ni++] = (uint16_t)a, idx[ni++] = (uint16_t)(a + 1), idx[ni++] = (uint16_t)b;
      idx[ni++] = (uint16_t)b, idx[ni++] = (uint16_t)(a + 1), idx[ni++] = (uint16_t)(b + 1);
    }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at);
  t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, nrm, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, at);
  t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, at);
  t3_release(at);
  at = t3_attribute_new(T3_UINT16, idx, ni, 1);
  t3_geometry_set_index(g, at);
  t3_release(at);
  t3_geometry_compute_bounding_box(g);
  t3_geometry_compute_bounding_sphere(g);
  return g;
}

/* ── sounds (synthesised) ───────────────────────────────────────────── */

static cw_sound synth(int kind) {
  const int rate = 44100;
  int n = rate * (kind == 1 ? 12 : kind == 3 ? 14 : 4) / 10;
  float *pcm = calloc((size_t)n, sizeof *pcm);
  uint32_t r = 2246822519u + (uint32_t)kind * 7919u;
  for (int i = 0; i < n; i++) {
    float t = (float)i / rate, v = 0;
    r = r * 1664525u + 1013904223u;
    float noise = ((r >> 9) / 4194304.0f) - 1.0f;
    switch (kind) {
    case 0: /* knock: a piece set down on wood */
      v = sinf(6.2832f * (210 - 80 * t) * t) * expf(-t * 30) * 0.7f + noise * expf(-t * 120) * 0.3f;
      break;
    case 1: /* chime: a soft bell as a lantern lights */
      v = (sinf(6.2832f * 784 * t) * 0.5f + sinf(6.2832f * 1176 * t) * 0.25f + sinf(6.2832f * 1568 * t) * 0.12f) * expf(-t * 3.2f) * fminf(1, t * 150);
      break;
    case 2: /* coin: two bright clinks */
      v = (sinf(6.2832f * 3300 * t) * 0.5f + sinf(6.2832f * 4650 * t) * 0.3f) * expf(-t * 40) +
          (t > 0.07f ? (sinf(6.2832f * 3900 * t) * 0.4f) * expf(-(t - 0.07f) * 45) : 0);
      break;
    case 3: /* horn: a low boat horn, swelling and fading */
      v = (sinf(6.2832f * 110 * t) * 0.5f + sinf(6.2832f * 165 * t) * 0.3f + sinf(6.2832f * 220 * t) * 0.15f) * sinf(3.14159f * clampf(t / 1.3f, 0, 1)) * 0.6f;
      break;
    default: /* turn: a low wooden tock */
      v = sinf(6.2832f * 400 * t) * expf(-t * 40) * 0.6f + sinf(6.2832f * 600 * t) * expf(-t * 60) * 0.3f;
    }
    pcm[i] = v * 0.55f;
  }
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, rate, &(cw_sound_desc){ .max_voices = 6, .bus = cw_shell_sfx_bus() });
  free(pcm);
  return s;
}

static void play(cw_sound s, float vol, float pitch) { cw_audio_play(s, &(cw_play_desc){ .volume = vol, .pitch = pitch }); }

/* ── card faces (drawn once each into a texture) ────────────────────── */

static void face_text(const char *s, float x, float y, float size, cw_color c, float wrap, cw_text_align al) {
  cw_text_style st = { .size = size, .color = c, .fixed_size = true, .own_backdrop = true, .wrap_width = wrap, .align = al, .line_spacing = 0.95f };
  if (wrap <= 0 && al != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), s, &st);
    x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width;
    st.align = CW_ALIGN_LEFT;
  }
  cw_r2d_text(cw_ui_font_sans(), s, cw_v2(x, y), &st);
}

/* a small lantern glyph (value marks) */
static void glyph_lantern(float x, float y, float r, cw_color c) {
  cw_color iron = cw_hex(0x3a2a1aff);
  cw_r2d_circle_lines(cw_v2(x, y - r * 1.28f), r * 0.22f, r * 0.09f, 16, iron);               /* the ring it hangs by */
  cw_r2d_round_rect(cw_rect_make(x - r * 0.62f, y - r * 1.1f, r * 1.24f, r * 0.3f), r * 0.1f, 4, iron); /* the cap */
  cw_r2d_round_rect(cw_rect_make(x - r * 0.8f, y - r * 0.88f, r * 1.6f, r * 1.76f), r * 0.5f, 8, c); /* the glowing body, taller than wide */
  cw_r2d_round_rect(cw_rect_make(x - r * 0.62f, y + r * 0.82f, r * 1.24f, r * 0.3f), r * 0.1f, 4, iron); /* the base */
}

static void glyph_coin(float x, float y, float r) {
  cw_r2d_circle(cw_v2(x, y), r, 28, cw_hex(0xc9952fff));
  cw_r2d_circle(cw_v2(x, y), r * 0.78f, 28, cw_hex(0xf0c660ff));
}

static void glyph_house(float cx, float cy, float s, cw_color wall, cw_color roof) {
  cw_r2d_rect(cw_rect_make(cx - s * 0.5f, cy - s * 0.1f, s, s * 0.6f), wall);
  cw_vec2 tri[3] = { { cx - s * 0.62f, cy - s * 0.08f }, { cx, cy - s * 0.6f }, { cx + s * 0.62f, cy - s * 0.08f } };
  cw_r2d_polygon(tri, 3, roof);
  cw_r2d_rect(cw_rect_make(cx - s * 0.1f, cy + s * 0.18f, s * 0.2f, s * 0.32f), cw_hex(0x2a1c12ff));
}

/* ── big pictograms (every tile reads by its picture from across the room; words live in the focus panel) ── */

static cw_color hexc(uint32_t h) { return cw_hex(h); }

/* a barrel of a good, standing: s its height */
static void glyph_barrel(float cx, float cy, float s, int g) {
  float w = s * 0.62f;
  cw_r2d_round_rect(cw_rect_make(cx - w * 0.5f, cy - s * 0.5f, w, s), w * 0.3f, 8, hexc(GOOD_HEX[g]));
  cw_r2d_round_rect_lines(cw_rect_make(cx - w * 0.5f, cy - s * 0.5f, w, s), w * 0.3f, s * 0.05f, 8, hexc(0x1a120cff));
  for (int k = -1; k <= 1; k += 2) cw_r2d_line(cw_v2(cx - w * 0.5f, cy + k * s * 0.22f), cw_v2(cx + w * 0.5f, cy + k * s * 0.22f), s * 0.07f, hexc(GOOD_HOOP[g]));
}

static void glyph_pawn(float cx, float cy, float s, cw_color c) {
  cw_r2d_circle(cw_v2(cx, cy - s * 0.3f), s * 0.18f, 18, c);
  cw_vec2 body[4] = { { cx - s * 0.28f, cy + s * 0.42f }, { cx - s * 0.1f, cy - s * 0.12f }, { cx + s * 0.1f, cy - s * 0.12f }, { cx + s * 0.28f, cy + s * 0.42f } };
  cw_r2d_polygon(body, 4, c);
}

static void glyph_crate(float cx, float cy, float s) {
  cw_r2d_rect(cw_rect_make(cx - s * 0.5f, cy - s * 0.5f, s, s), hexc(0xb88a50ff));
  cw_r2d_rect_lines(cw_rect_make(cx - s * 0.5f, cy - s * 0.5f, s, s), s * 0.08f, hexc(0x5a3a1aff));
  cw_r2d_line(cw_v2(cx - s * 0.45f, cy - s * 0.45f), cw_v2(cx + s * 0.45f, cy + s * 0.45f), s * 0.08f, hexc(0x5a3a1aff));
}

static void glyph_boat(float cx, float cy, float s) {
  cw_vec2 hull[4] = { { cx - s * 0.6f, cy }, { cx + s * 0.6f, cy }, { cx + s * 0.4f, cy + s * 0.25f }, { cx - s * 0.4f, cy + s * 0.25f } };
  cw_r2d_polygon(hull, 4, hexc(0x8a5a32ff));
  cw_r2d_line(cw_v2(cx, cy), cw_v2(cx, cy - s * 0.7f), s * 0.05f, hexc(0x3a2a1aff));
  cw_vec2 sail[3] = { { cx + s * 0.04f, cy - s * 0.66f }, { cx + s * 0.45f, cy - s * 0.08f }, { cx + s * 0.04f, cy - s * 0.08f } };
  cw_r2d_polygon(sail, 3, hexc(0xf2e6c8ff));
}

static void glyph_bell(float cx, float cy, float s) {
  cw_vec2 bell[5] = { { cx - s * 0.12f, cy - s * 0.4f }, { cx + s * 0.12f, cy - s * 0.4f }, { cx + s * 0.32f, cy + s * 0.25f }, { cx - s * 0.32f, cy + s * 0.25f }, { cx - s * 0.12f, cy - s * 0.4f } };
  cw_r2d_polygon(bell, 4, hexc(0xe6b84aff));
  cw_r2d_circle(cw_v2(cx, cy + s * 0.34f), s * 0.09f, 14, hexc(0xe6b84aff));
}

static void glyph_sprout(float cx, float cy, float s) {
  cw_r2d_line(cw_v2(cx, cy + s * 0.45f), cw_v2(cx, cy - s * 0.1f), s * 0.08f, hexc(0x4f9a3aff));
  cw_r2d_circle(cw_v2(cx - s * 0.18f, cy - s * 0.15f), s * 0.17f, 16, hexc(0x6fc04aff));
  cw_r2d_circle(cw_v2(cx + s * 0.18f, cy - s * 0.25f), s * 0.17f, 16, hexc(0x6fc04aff));
}

static void big_number(const char *n, float cx, float cy, float size, cw_color c) {
  cw_text_style st = { .size = size, .color = c, .fixed_size = true, .own_backdrop = true, .outline_width = size * 0.1f, .outline_color = hexc(0x140c06ff) };
  cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), n, &st);
  cw_r2d_text(cw_ui_font_sans(), n, cw_v2(cx - m.width * 0.5f, cy - size * 0.6f), &st);
}

/* a building's pictogram, centred, s about its size */
static void glyph_building(int b, float cx, float cy, float s) {
  cw_color ink = hexc(0xfff3d6ff);
  int g = hb_bld[b].good;
  if (g >= 0) { /* a workshop: its good; one barrel small, three large (two for the spice mill) */
    int n = hb_bld[b].slots;
    for (int k = 0; k < n; k++) glyph_barrel(cx + ((float)k - (float)(n - 1) * 0.5f) * s * 0.42f, cy, s * (n == 1 ? 0.85f : 0.6f), g);
    return;
  }
  switch (b) {
  case B_STALL: glyph_coin(cx, cy, s * 0.36f); big_number("+1", cx, cy + s * 0.04f, s * 0.36f, ink); break;
  case B_NIGHT_MARKET: glyph_coin(cx - s * 0.16f, cy, s * 0.3f); glyph_coin(cx + s * 0.16f, cy, s * 0.3f); big_number("+2", cx, cy + s * 0.04f, s * 0.36f, ink); break;
  case B_SEED_SHED: glyph_sprout(cx, cy, s * 0.9f); break;
  case B_STONEYARD:
    for (int k = 0; k < 3; k++) cw_r2d_circle(cw_v2(cx + (k - 1) * s * 0.26f, cy + (k == 1 ? -s * 0.18f : s * 0.08f)), s * 0.2f, 16, hexc(k == 1 ? 0xb5afa4ff : 0x8f897fff));
    break;
  case B_STOREHOUSE: glyph_crate(cx, cy, s * 0.6f); break;
  case B_WAREHOUSE: glyph_crate(cx - s * 0.22f, cy + s * 0.08f, s * 0.46f); glyph_crate(cx + s * 0.22f, cy - s * 0.08f, s * 0.46f); break;
  case B_HOSTEL:
    cw_r2d_rect(cw_rect_make(cx - s * 0.42f, cy + s * 0.12f, s * 0.84f, s * 0.26f), hexc(0x5f9a45ff));
    glyph_pawn(cx, cy - s * 0.08f, s * 0.6f, ink);
    break;
  case B_COUNTING_HOUSE: glyph_barrel(cx - s * 0.2f, cy, s * 0.6f, G_TEA); glyph_barrel(cx + s * 0.2f, cy, s * 0.6f, G_TEA); break;
  case B_COOPERATIVE: for (int k = 0; k < 5; k++) cw_r2d_circle(cw_v2(cx + (k - 2) * s * 0.2f, cy), s * 0.11f, 14, hexc(GOOD_HEX[k])); break;
  case B_ACADEMY:
    glyph_house(cx, cy + s * 0.12f, s * 0.7f, hexc(0x8fb8b0ff), hexc(0x6a2f22ff));
    glyph_pawn(cx, cy - s * 0.12f, s * 0.42f, ink);
    break;
  case B_BEACON: glyph_lantern(cx - s * 0.1f, cy, s * 0.26f, hexc(0xffb050ff)); big_number("+1", cx + s * 0.28f, cy, s * 0.32f, ink); break;
  case B_JETTY: glyph_boat(cx, cy + s * 0.1f, s * 0.8f); break;
  case B_GUILDHALL: for (int k = 0; k < 3; k++) glyph_barrel(cx + (k - 1) * s * 0.34f, cy, s * 0.55f, k + 1); break;
  case B_GARDENS:
    for (int k = 0; k < 6; k++) cw_r2d_rect(cw_rect_make(cx - s * 0.48f + (k % 3) * s * 0.33f, cy - s * 0.3f + (k / 3) * s * 0.33f, s * 0.29f, s * 0.29f), hexc(0x5f9a45ff));
    break;
  case B_BELL_TOWER: glyph_bell(cx, cy, s * 0.9f); break;
  case B_HARBOUR_OFFICE: glyph_lantern(cx - s * 0.2f, cy, s * 0.24f, hexc(0xffb050ff)); glyph_lantern(cx + s * 0.2f, cy, s * 0.24f, hexc(0xffb050ff)); break;
  case B_ASSEMBLY:
    cw_r2d_rect(cw_rect_make(cx - s * 0.45f, cy + s * 0.3f, s * 0.9f, s * 0.08f), ink);
    for (int k = 0; k < 4; k++) cw_r2d_rect(cw_rect_make(cx - s * 0.38f + k * s * 0.24f, cy - s * 0.18f, s * 0.08f, s * 0.48f), ink);
    { cw_vec2 tri[3] = { { cx - s * 0.5f, cy - s * 0.2f }, { cx, cy - s * 0.48f }, { cx + s * 0.5f, cy - s * 0.2f } }; cw_r2d_polygon(tri, 3, ink); }
    break;
  default: break;
  }
}

static float rnd(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return (float)(*r >> 8) / 16777216.0f; }

static void paint_plot(int k, float w, float h) {
  static const uint32_t BASE[HB_PLOT_KINDS] = { 0x6d8f4aff, 0x4a6a3aff, 0xd9c08aff, 0x3f6a33ff, 0x41522eff, 0x7d776cff };
  cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(BASE[k]));
  uint32_t r = 1234u + (uint32_t)k * 977u;
  switch (k) {
  case G_RICE:
    for (int i = 0; i < 6; i++) cw_r2d_rect(cw_rect_make(0, h * (0.08f + i * 0.16f), w, h * 0.07f), cw_hex(0x8fb7b9ff));
    break;
  case G_INDIGO:
    for (int i = 0; i < 60; i++) { float x = rnd(&r) * w, y = rnd(&r) * h; cw_r2d_circle(cw_v2(x, y), w * 0.02f, 8, cw_hex(i % 3 ? 0x5a56c9ff : 0x8b86e8ff)); }
    break;
  case G_GLASS:
    for (int i = 0; i < 7; i++) {
      float y = h * (0.1f + i * 0.13f);
      for (int k2 = 0; k2 < 12; k2++) cw_r2d_line(cw_v2(w * k2 / 12.0f, y + sinf(k2 * 0.9f + i) * h * 0.02f), cw_v2(w * (k2 + 1) / 12.0f, y + sinf((k2 + 1) * 0.9f + i) * h * 0.02f), w * 0.012f, cw_hex(0xb89b62ff));
    }
    break;
  case G_TEA:
    for (int i = 0; i < 5; i++)
      for (int k2 = 0; k2 < 9; k2++) cw_r2d_circle(cw_v2(w * (0.06f + k2 * 0.11f), h * (0.12f + i * 0.19f)), w * 0.05f, 12, cw_hex(0x6fa64aff));
    break;
  case G_SPICE:
    for (int i = 0; i < 14; i++) { float x = rnd(&r) * w; float y = rnd(&r) * h; cw_r2d_circle(cw_v2(x, y), w * 0.07f, 14, cw_hex(0x2f4a24ff)); cw_r2d_circle(cw_v2(x, y), w * 0.025f, 8, cw_hex(0xe0683aff)); }
    break;
  default:
    for (int i = 0; i < 22; i++) { float x = rnd(&r) * w, y = rnd(&r) * h, rr = rnd(&r); cw_r2d_circle(cw_v2(x, y), w * (0.04f + rr * 0.05f), 10, cw_hex(i % 2 ? 0xa39e94ff : 0x8c877dff)); }
  }
  /* the big mark: the good it grows (a barrel), or a heap of stone */
  cw_r2d_circle(cw_v2(w * 0.5f, h * 0.5f), w * 0.34f, 32, cw_rgba(0.08f, 0.06f, 0.04f, 0.55f));
  if (k == HB_STONE)
    for (int i = 0; i < 3; i++) cw_r2d_circle(cw_v2(w * (0.36f + i * 0.14f), h * (i == 1 ? 0.44f : 0.56f)), w * 0.12f, 16, cw_hex(i == 1 ? 0xd8d2c6ff : 0xaaa498ff));
  else glyph_barrel(w * 0.5f, h * 0.5f, h * 0.5f, k);
  cw_r2d_rect_lines(cw_rect_make(w * 0.03f, h * 0.03f, w * 0.94f, h * 0.94f), w * 0.025f, cw_rgba(0.1f, 0.07f, 0.04f, 0.6f));
}

/* the jobs' pictograms */
static void glyph_role(int r, float cx, float cy, float s) {
  cw_color ink = hexc(0xfff3d6ff);
  switch (r) {
  case R_SURVEYOR:
    cw_r2d_rect(cw_rect_make(cx - s * 0.45f, cy - s * 0.1f, s * 0.9f, s * 0.5f), hexc(0x5f9a45ff));
    cw_r2d_line(cw_v2(cx, cy + s * 0.1f), cw_v2(cx, cy - s * 0.5f), s * 0.06f, ink);
    { cw_vec2 fl[3] = { { cx, cy - s * 0.5f }, { cx + s * 0.32f, cy - s * 0.38f }, { cx, cy - s * 0.26f } }; cw_r2d_polygon(fl, 3, hexc(0xe0583aff)); }
    break;
  case R_FERRYMASTER: glyph_boat(cx - s * 0.1f, cy + s * 0.05f, s * 0.8f); glyph_pawn(cx - s * 0.25f, cy - s * 0.08f, s * 0.36f, ink); break;
  case R_MASON: glyph_house(cx - s * 0.1f, cy + s * 0.1f, s * 0.8f, hexc(0xc79a6aff), hexc(0x6a2f22ff)); big_number("+", cx + s * 0.42f, cy - s * 0.2f, s * 0.6f, ink); break;
  case R_ARTISAN: for (int k = 0; k < 3; k++) glyph_barrel(cx + (k - 1) * s * 0.34f, cy, s * 0.6f, k + 1); break;
  case R_MERCHANT: glyph_barrel(cx - s * 0.28f, cy, s * 0.6f, G_SPICE); glyph_coin(cx + s * 0.25f, cy, s * 0.26f); break;
  case R_SKIPPER: glyph_boat(cx, cy + s * 0.1f, s * 0.9f); glyph_lantern(cx + s * 0.4f, cy - s * 0.25f, s * 0.14f, hexc(0xffb050ff)); break;
  default: cw_r2d_circle(cw_v2(cx, cy + s * 0.05f), s * 0.34f, 28, hexc(0x2f6a78ff)); cw_r2d_circle(cw_v2(cx, cy + s * 0.05f), s * 0.16f, 20, hexc(0xf4f0e8ff)); break;
  }
}

static void paint(int face, cw_vec2 px, void *user) {
  (void)user;
  float w = px.x, h = px.y;
  if (face == FACE_PLOT_BACK) {
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0x4a3220ff));
    cw_r2d_rect_lines(cw_rect_make(w * 0.08f, h * 0.08f, w * 0.84f, h * 0.84f), w * 0.03f, cw_hex(0xc99a4aff));
    glyph_lantern(w * 0.5f, h * 0.52f, w * 0.2f, cw_hex(0xe8a845ff));
    return;
  }
  if (face >= FACE_PLOT) { paint_plot(face - FACE_PLOT, w, h); return; }
  cw_color cream = cw_hex(0xfff3d6ff);
  if (face >= FACE_ROLE) { /* a job: its name big across the top, its picture below */
    int r = face - FACE_ROLE;
    static const uint32_t ROLE_BG[HB_ROLES] = { 0x2f4a2cff, 0x22405cff, 0x5a3a26ff, 0x6a4a1aff, 0x5a2232ff, 0x1c3650ff, 0x22505aff };
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(ROLE_BG[r]));
    cw_r2d_rect(cw_rect_make(0, 0, w, h * 0.4f), cw_rgba(0, 0, 0, 0.3f));
    cw_r2d_rect_lines(cw_rect_make(w * 0.025f, h * 0.035f, w * 0.95f, h * 0.93f), h * 0.03f, cw_hex(0xd8b46aff));
    cw_text_style st = { .size = h * 0.34f, .color = cream, .fixed_size = true, .own_backdrop = true };
    cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), hb_role_name[r], &st);
    if (m.width > w * 0.92f) { st.size *= w * 0.92f / m.width; m = cw_font_measure(cw_ui_font_sans(), hb_role_name[r], &st); }
    cw_r2d_text(cw_ui_font_sans(), hb_role_name[r], cw_v2((w - m.width) * 0.5f, h * 0.02f), &st);
    glyph_role(r, w * 0.5f, h * 0.7f, h * 0.5f);
    return;
  }
  /* a building: its picture big, its cost (a coin) and lanterns big in the top corners */
  int b = face;
  bool grand = hb_bld[b].lots == 2;
  int g = hb_bld[b].good;
  cw_color bg = g >= 0 ? cw_hex(0x7a4a2aff) : grand ? cw_hex(0x6e5424ff) : cw_hex(0x24555aff);
  cw_r2d_rect(cw_rect_make(0, 0, w, h), bg);
  cw_r2d_rect_lines(cw_rect_make(h * 0.03f, h * 0.03f, w - h * 0.06f, h * 0.94f), h * 0.03f, cw_hex(0xd8b46aff));
  /* the picture large on the left, the lanterns it is worth as a big number on the right (its cost is the coin
   * badge beside it in the market) */
  char num[8];
  glyph_building(b, grand ? w * 0.34f : w * 0.3f, h * 0.42f, h * 0.56f);
  /* the lanterns it is worth: the number in dark ink on a big lit lantern, read as "lanterns" the way the market's
   * gold coin badge reads as the cost */
  float lx = w - h * 0.22f, ly = h * 0.46f, lr = h * 0.2f;
  glyph_lantern(lx, ly, lr, cw_hex(0xffbc4cff));
  snprintf(num, sizeof num, "%d", hb_bld[b].value);
  cw_text_style vs = { .size = h * 0.32f, .color = cw_hex(0x2a1608ff), .fixed_size = true, .own_backdrop = true };
  cw_text_metrics vm = cw_font_measure(cw_ui_font_sans(), num, &vs);
  cw_r2d_text(cw_ui_font_sans(), num, cw_v2(lx - vm.width * 0.5f, ly - h * 0.32f * 0.6f), &vs);
  (void)cream;
  float unit = w / (grand ? GRAND_W : SMALL_W);
  for (int k = 0; k < hb_bld[b].slots; k++) {
    float cx = w * 0.5f + ((float)k - (float)(hb_bld[b].slots - 1) * 0.5f) * 0.42f * unit, cy = h * 0.5f + 0.36f * unit;
    cw_r2d_circle(cw_v2(cx, cy), 0.15f * unit, 24, cw_rgba(0, 0, 0, 0.4f));
    cw_r2d_circle_lines(cw_v2(cx, cy), 0.15f * unit, 0.03f * unit, 24, cw_hex(0xf2dca8ff));
  }
}

/* ── the scene ──────────────────────────────────────────────────────── */

static cw_node card(const char *name, float w, float h, int front, int back) {
  return bk_card_new(V.root, name, &(bk_card_desc){ .width = w, .height = h, .thickness = CARD_T, .corner = 0.08f, .front = front, .back = back,
                                                    .edge = cw_hex(0x2a1d12ff), .face_px = 256 });
}

static void make_boat(int k, bool ferry) {
  char name[24];
  snprintf(name, sizeof name, ferry ? "Ferry" : "Boat%d", k);
  cw_node b = cw_node_new("Node3D", name);
  cw_node_add_child(V.root, b);
  float len = ferry ? 6.0f : 7.6f;
  t3_geometry *hg = hull_geometry(len, 1.9f, 0.9f);
  t3_material *wood = physical(ferry ? 0x5a3a28 : 0x6b4026, 0.62f, 0);
  mesh_node(b, "Hull", hg, wood, true, true);
  t3_release(hg);
  t3_release(wood);
  t3_geometry *deck = t3_box_geometry_new(len * 0.86f, 0.06f, 1.55f, 1, 1, 1);
  t3_material *dm = physical(0xa07a52, 0.7f, 0);
  cw_node dn = mesh_node(b, "Deck", deck, dm, false, true);
  cw_node3d_set_position(dn, v3(0, -0.03f, 0));
  t3_release(deck);
  t3_release(dm);
  /* mast and a furled-down sail, a lamp at the stern */
  t3_geometry *mg = t3_cylinder_geometry_new(0.06f, 0.08f, 3.4f, 8, 1, false);
  t3_material *mm = physical(0x4a3020, 0.6f, 0);
  cw_node mast = mesh_node(b, "Mast", mg, mm, true, false);
  cw_node3d_set_position(mast, v3(ferry ? 0.6f : -0.4f, 1.7f, 0));
  t3_release(mg);
  t3_release(mm);
  t3_geometry *sg = t3_plane_geometry_new(ferry ? 1.5f : 2.2f, 2.4f, 1, 1);
  t3_material *sm = physical(ferry ? 0xb8794f : 0xc9b48a, 0.95f, 0);
  sm->side = T3_DOUBLE_SIDE;
  sm->sheen = 0.4f;
  sm->sheen_roughness = 0.8f;
  cw_node sail = mesh_node(b, "Sail", sg, sm, true, false);
  cw_node3d_set_position(sail, v3((ferry ? 0.6f : -0.4f) + (ferry ? 0.8f : 1.15f), 1.95f, 0.04f));
  cw_node3d_set_rotation(sail, v3(0, 0.18f, 0));
  t3_release(sg);
  t3_release(sm);
  if (ferry) { /* an awning over the benches */
    t3_geometry *ag = t3_box_geometry_new(2.2f, 0.05f, 1.3f, 1, 1, 1);
    t3_material *am = physical(0x5e2a20, 0.9f, 0);
    cw_node aw = mesh_node(b, "Awning", ag, am, true, false);
    cw_node3d_set_position(aw, v3(-0.6f, 1.15f, 0));
    t3_release(ag);
    t3_release(am);
  }
  float lamp_geo[] = { 0, -0.12f, 0.09f, -0.1f, 0.13f, 0, 0.09f, 0.1f, 0, 0.12f };
  t3_geometry *lg = lathe(lamp_geo, 5, 12);
  t3_material *lm = physical(0xffd9a0, 0.6f, 0);
  lm->emissive = (t3_color){ 1.0f, 0.62f, 0.25f };
  lm->emissive_intensity = 2.2f;
  cw_node lamp = mesh_node(b, "Lamp", lg, lm, false, false);
  cw_node3d_set_position(lamp, v3(-len * 0.42f, 0.75f, 0));
  V.boat_lamp[ferry ? HB_SHIPS : k] = lamp;
  t3_release(lg);
  t3_release(lm);
  V.boat[ferry ? HB_SHIPS : k] = b;
  if (!ferry)
    for (int i = 0; i < 7; i++) {
      V.cargo[k][i] = mesh_node(b, "Cargo", V.barrel_geo, V.barrel_mat[0], true, false);
      set_visible(V.cargo[k][i], false);
    }
}

void view_init(cw_tree *t) {
  memset(&V, 0, sizeof V);
  V.tree = t;
  V.world = cw_node_get(cw_tree_root(t), "World");
  if (!cw_node_valid(V.world)) CW_FATAL("lanternharbor: main.cwscene has no World");
  V.camera = cw_node_get(V.world, "Camera");
  V.water = cw_node_get(V.world, "Water");
  if (!cw_node_valid(V.camera) || !cw_node_valid(V.water)) CW_FATAL("lanternharbor: main.cwscene needs a Camera and a Water");
  for (int i = 0; i < 4; i++) {
    char n[16];
    snprintf(n, sizeof n, "Lamp%d", i);
    V.lights[i] = cw_node_get(V.world, n);
    if (!cw_node_valid(V.lights[i])) CW_FATAL("lanternharbor: main.cwscene has no %s", n);
  }
  V.root = cw_node_new("Node3D", "Harbour");
  cw_node_add_child(V.world, V.root);
  bk_cards_init(t, paint, NULL);

  /* the quay: a long teak deck over the water's edge, its stone face toward the water */
  t3_texture *wood_tex = texture("textures/teak.png");
  wood_tex->wrap_s = wood_tex->wrap_t = T3_REPEAT;
  t3_material *wood = physical(0xffffff, 0.6f, 0);
  t3_material_set_map(wood, wood_tex);
  t3_geometry *tg = t3_box_geometry_new(130, 0.6f, 30, 1, 1, 1);
  {
    t3_attribute *uv = tg->attributes[T3_ATTR_UV];
    float *f = (float *)uv->array;
    for (int i = 0; i < uv->count * 2; i += 2) f[i] *= 10, f[i + 1] *= 3;
  }
  cw_node table = mesh_node(V.root, "Deck", tg, wood, false, true);
  cw_node3d_set_position(table, v3(0, -0.3f, 7.4f));
  t3_release(tg);
  t3_release(wood);
  t3_texture *stone_tex = texture("textures/quay.png");
  stone_tex->wrap_s = T3_REPEAT;
  t3_material *stone = physical(0xffffff, 0.85f, 0);
  t3_material_set_map(stone, stone_tex);
  t3_geometry *qg = t3_box_geometry_new(130, 1.6f, 1.1f, 1, 1, 1);
  {
    t3_attribute *uv = qg->attributes[T3_ATTR_UV];
    float *f = (float *)uv->array;
    for (int i = 0; i < uv->count * 2; i += 2) f[i] *= 40;
  }
  cw_node quay = mesh_node(V.root, "QuayWall", qg, stone, true, true);
  cw_node3d_set_position(quay, v3(0, -0.72f, -7.95f));
  t3_release(qg);
  t3_release(stone);

  /* the far shore: the old town's houses across the water, their windows lit for the evening */
  {
    t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
    t3_geometry *roof = t3_cone_geometry_new(0.75f, 1.0f, 4, 1, false);
    t3_geometry *win = t3_box_geometry_new(0.3f, 0.36f, 0.04f, 1, 1, 1);
    t3_material *wall = physical(0x3a3240, 0.9f, 0), *tile = physical(0x4a2622, 0.85f, 0), *lamp = physical(0x40261a, 0.9f, 0);
    lamp->emissive = (t3_color){ 1.0f, 0.36f, 0.08f };
    lamp->emissive_intensity = 1.1f;
    t3_geometry *hill = t3_box_geometry_new(140, 2.2f, 6, 1, 1, 1);
    t3_material *dark = physical(0x1c1a24, 1.0f, 0);
    cw_node hn = mesh_node(V.root, "Hill", hill, dark, false, false);
    cw_node3d_set_position(hn, v3(0, 0.4f, -18.5f));
    t3_release(hill);
    t3_release(dark);
    uint32_t r = 4242u;
    float x = -38.0f;
    for (int i = 0; x < 38.0f; i++) {
      float w = 1.6f + 1.2f * rnd(&r), h = 1.4f + 2.4f * rnd(&r), d = 1.6f, z = -14.9f - 0.7f * rnd(&r);
      cw_node hs = mesh_node(V.root, "House", box, wall, false, false);
      cw_node3d_set_position(hs, v3(x + w * 0.5f, h * 0.5f - 0.2f, z));
      cw_node3d_set_scale(hs, v3(w, h, d));
      cw_node rf = mesh_node(V.root, "Roof", roof, tile, false, false);
      cw_node3d_set_position(rf, v3(x + w * 0.5f, h - 0.2f + 0.45f, z));
      cw_node3d_set_rotation(rf, v3(0, 0.785f, 0));
      cw_node3d_set_scale(rf, v3(w * 0.95f, 0.9f, d * 0.95f));
      int nw = 1 + (int)(rnd(&r) * 3.0f);
      for (int k = 0; k < nw; k++) {
        if (rnd(&r) < 0.3f) continue;
        cw_node wn = mesh_node(V.root, "Window", win, lamp, false, false);
        cw_node3d_set_position(wn, v3(x + w * (0.25f + 0.5f * (float)k / (float)(nw > 1 ? nw - 1 : 1)) , 0.3f + (h - 0.8f) * rnd(&r), z + d * 0.5f + 0.02f));
      }
      x += w + 0.15f + 0.6f * rnd(&r);
    }
    t3_release(box);
    t3_release(roof);
    t3_release(win);
    t3_release(wall);
    t3_release(tile);
    t3_release(lamp);
  }

  /* barrels, pawns, coins */
  float barrel[] = { 0, 0, 0.15f, 0, 0.19f, 0.06f, 0.21f, 0.19f, 0.19f, 0.32f, 0.15f, 0.38f, 0, 0.38f };
  V.barrel_geo = lathe(barrel, 7, 14);
  for (int g = 0; g < HB_GOODS; g++) {
    V.barrel_mat[g] = physical(GOOD_HEX[g] >> 8, g == G_GLASS ? 0.25f : 0.7f, 0);
    if (g == G_GLASS) V.barrel_mat[g]->clearcoat = 0.8f;
  }
  float pawn[] = { 0, 0, 0.21f, 0, 0.22f, 0.06f, 0.15f, 0.15f, 0.11f, 0.38f, 0.09f, 0.45f, 0.14f, 0.53f, 0.14f, 0.63f, 0.09f, 0.7f, 0, 0.73f };
  V.pawn_geo = lathe(pawn, 10, 14);
  for (int i = 0; i < 4; i++) {
    static const uint32_t SHIRT[4] = { 0xe0b070, 0xc0583a, 0x4f8a86, 0x8e6aa8 };
    V.pawn_mat[i] = physical(SHIRT[i], 0.85f, 0);
  }
  V.coin_geo = t3_cylinder_geometry_new(0.3f, 0.3f, 1.0f, 24, 1, false);
  V.coin_mat = physical(0xd6a640, 0.32f, 1.0f);
  V.ghost_mat = t3_mesh_standard_material_new(0xfff0c0);
  V.ghost_mat->transparent = true;
  V.ghost_mat->opacity = 0.55f;
  V.ghost_mat->depth_write = false;
  V.ghost_mat->emissive = (t3_color){ 1.0f, 0.8f, 0.45f };
  V.ghost_mat->emissive_intensity = 0.5f;

  for (int k = 0; k < HB_SHIPS; k++) make_boat(k, false);
  make_boat(0, true);

  /* the quay's lanterns on posts */
  float lantern[] = { 0, -0.34f, 0.2f, -0.3f, 0.36f, -0.1f, 0.36f, 0.1f, 0.2f, 0.3f, 0, 0.34f };
  t3_geometry *lg = lathe(lantern, 6, 16);
  t3_geometry *pg = t3_cylinder_geometry_new(0.05f, 0.07f, 1.5f, 8, 1, false);
  t3_material *post = physical(0x3a2618, 0.6f, 0);
  for (int i = 0; i < LY_LANTERNS; i++) {
    ly_point p = ly_lantern(i);
    cw_node pn = mesh_node(V.root, "Post", pg, post, true, false);
    cw_node3d_set_position(pn, v3(p.x, 0.75f, p.z));
    cw_node3d_set_rotation(pn, v3(0, 0, 0));
    t3_material *m = t3_mesh_basic_material_new(0x1a0402); /* paper with a flame inside: its own light, not the lamps' */
    m->tone_mapped = false;
    V.lantern[i] = mesh_node(V.root, "Lantern", lg, m, false, false);
    cw_node3d_set_position(V.lantern[i], v3(p.x, 1.85f, p.z));
    t3_release(m);
  }
  t3_release(lg);
  t3_release(pg);
  t3_release(post);

  /* the job cards, their coins, the market, the piles, the pass sign */
  for (int r = 0; r < HB_ROLES; r++) {
    char n[24];
    snprintf(n, sizeof n, "Role%d", r);
    V.role_card[r] = card(n, ROLE_W, ROLE_H, FACE_ROLE + r, -1);
    for (int c = 0; c < 6; c++) {
      V.role_coins[r][c] = mesh_node(V.root, "Coin", V.coin_geo, V.coin_mat, true, true);
      cw_node3d_set_scale(V.role_coins[r][c], v3(0.8f, 0.07f, 0.8f));
      set_visible(V.role_coins[r][c], false);
    }
  }
  t3_geometry *stack_geo = t3_box_geometry_new(1.0f, 1.0f, 1.0f, 1, 1, 1);
  t3_material *stack_mat = physical(0x2a1d12, 0.8f, 0);
  for (int b = 0; b < HB_BUILDINGS; b++) {
    char n[24];
    snprintf(n, sizeof n, "Market%d", b);
    float w = hb_bld[b].lots == 2 ? GRAND_W : SMALL_W;
    V.market_card[b] = card(n, hb_bld[b].lots == 2 ? MARKET_GW : MARKET_W, hb_bld[b].lots == 2 ? MARKET_GW * SMALL_W / GRAND_W : MARKET_W, b, -1);
    V.market_stack[b] = mesh_node(V.root, "Stack", stack_geo, stack_mat, true, true);
    cw_node3d_set_scale(V.market_stack[b], v3(w * 0.98f, 0.01f, SMALL_W * 0.98f));
  }
  t3_release(stack_geo);
  t3_release(stack_mat);
  V.pile_card = card("Pile", SMALL_W, SMALL_W, FACE_PLOT_BACK, -1);
  V.stone_card = card("Stones", SMALL_W, SMALL_W, FACE_PLOT + HB_STONE, -1);
  float sign[] = { 0, 0, 0.75f, 0, 0.78f, 0.06f, 0.7f, 0.12f, 0, 0.12f };
  t3_geometry *sg = lathe(sign, 5, 32);
  t3_material *sm = physical(0x6b4a2a, 0.55f, 0);
  sm->clearcoat = 0.4f;
  V.pass_sign = mesh_node(V.root, "Pass", sg, sm, true, true);
  t3_release(sg);
  t3_release(sm);

  /* ghosts */
  V.ghost_card_small = card("GhostSmall", SMALL_W, SMALL_W, 0, -1);
  V.ghost_card_grand = card("GhostGrand", GRAND_W, SMALL_W, B_GUILDHALL, -1);
  V.ghost_plot = card("GhostPlot", SMALL_W, SMALL_W, FACE_PLOT, -1);
  /* the card ghosts: see-through by mesh opacity, no shadow, from the first frame */
  cw_node ghost_cards[3] = { V.ghost_card_small, V.ghost_card_grand, V.ghost_plot };
  for (int i = 0; i < 3; i++)
    if (bk_ghost_set(ghost_cards[i], 0.55f) <= 0) CW_FATAL("lanternharbor: ghost card %d has no meshes", i);
  V.ghost_pawn = mesh_node(V.root, "GhostPawn", V.pawn_geo, V.ghost_mat, false, false);
  for (int i = 0; i < 7; i++) V.ghost_barrel[i] = mesh_node(V.root, "GhostBarrel", V.barrel_geo, V.ghost_mat, false, false);

  V.snd_knock = synth(0);
  V.snd_chime = synth(1);
  V.snd_coin = synth(2);
  V.snd_horn = synth(3);
  V.snd_turn = synth(4);
}

static void clear_match_nodes(void) {
  for (int g = 0; g < HB_GOODS; g++)
    for (int i = 0; i < V.nbarrel[g]; i++) cw_node_free(V.barrel[g][i].node);
  for (int k = 0; k < HB_PLOT_KINDS; k++)
    for (int i = 0; i < V.nplot[k]; i++) cw_node_free(V.plot[k][i].node);
  for (int b = 0; b < HB_BUILDINGS; b++)
    for (int i = 0; i < V.ntile[b]; i++) cw_node_free(V.tile[b][i].node);
  for (int i = 0; i < V.nfolk; i++) cw_node_free(V.folk[i].node);
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    if (cw_node_valid(V.boards[s])) cw_node_free(V.boards[s]);
    if (cw_node_valid(V.glow[s])) cw_node_free(V.glow[s]);
    if (cw_node_valid(V.coins[s])) cw_node_free(V.coins[s]);
    V.boards[s] = V.glow[s] = V.coins[s] = (cw_node){ 0 };
  }
  memset(V.nbarrel, 0, sizeof V.nbarrel);
  memset(V.nplot, 0, sizeof V.nplot);
  memset(V.ntile, 0, sizeof V.ntile);
  V.nfolk = 0;
}

static void piece_init(piece *p, cw_node n, t3_vec3 at) {
  memset(p, 0, sizeof *p);
  p->node = n;
  p->key = key(K_HIDDEN, 0, 0, 0);
  p->boat = -1;
  p->pos = p->prev = p->from = p->to = at;
  set_visible(n, false);
}

/* the camera frames the whole table, a strip left at the bottom for the prompts */
static void frame_camera(const ly_table *l) {
  float w = l->max_x - l->min_x, h = l->max_z - l->min_z;
  float fov = 30.0f * T3_DEG2RAD, tanv = tanf(fov * 0.5f), aspect = 16.0f / 9.0f;
  float dist = fmaxf(w * 0.5f / (tanv * aspect), h * 0.5f / tanv) * 1.02f + 1.0f;
  float tilt = 0.36f, cz = (l->min_z + l->max_z) * 0.5f + 0.9f;
  cw_node3d_set_position(V.camera, v3(0, dist * cosf(tilt), cz + dist * sinf(tilt)));
  cw_node3d_look_at(V.camera, v3(0, 0, cz), v3(0, 1, 0));
}

static void place_fixed(const hb_state *s) {
  for (int r = 0; r < HB_ROLES; r++) {
    bool in = r < s->roles_in_play;
    set_visible(V.role_card[r], in);
    cw_node3d_set_position(V.role_card[r], at2(ly_role(r), 0.05f));
  }
  for (int b = 0; b < HB_BUILDINGS; b++) cw_node3d_set_position(V.market_card[b], at2(ly_market(b), 0.1f));
  cw_node3d_set_position(V.pile_card, at2(ly_plot_pile(), 0.3f));
  cw_node3d_set_position(V.stone_card, at2(ly_stone_pile(), 0.3f));
  cw_node3d_set_position(V.pass_sign, at2(ly_pass(), 0.0f));
  for (int k = 0; k < HB_SHIPS; k++) {
    set_visible(V.boat[k], k < s->nships);
    if (k < s->nships) cw_node3d_set_position(V.boat[k], at2(ly_ship(s->nships, k), 0.05f));
  }
  cw_node3d_set_position(V.boat[HB_SHIPS], at2(ly_ferry(), 0.05f));
}

void view_start(const mg_game *g) {
  V.attract = false;
  clear_match_nodes();
  const hb_state *s = &g->shown;
  V.players = g->players;
  V.lay = g->lay;
  V.nships = s->nships;
  frame_camera(&V.lay);
  place_fixed(s);
  /* the boards */
  t3_texture *bt = texture("textures/board.png");
  t3_material *bm = physical(0xffffff, 0.8f, 0);
  t3_material_set_map(bm, bt);
  t3_geometry *bg = t3_box_geometry_new(LY_BOARD_W, BOARD_TOP, LY_BOARD_H, 1, 1, 1);
  { /* the top face reads the texture with v toward -z (the texture's row 0 is v 0) */
    t3_attribute *uv = bg->attributes[T3_ATTR_UV];
    float *f = (float *)uv->array;
    for (int i = 0; i < uv->count * 2; i += 2) f[i + 1] = 1 - f[i + 1];
  }
  t3_material *rim = physical(0x24160d, 0.6f, 0);
  for (int i = 0; i < g->players; i++) {
    ly_point o = V.lay.board[i];
    V.boards[i] = mesh_node(V.root, "Board", bg, bm, true, true);
    cw_node3d_set_position(V.boards[i], v3(o.x + LY_BOARD_W * 0.5f, BOARD_TOP * 0.5f, o.z + LY_BOARD_H * 0.5f));
    t3_geometry *fg = t3_box_geometry_new(LY_BOARD_W + 0.36f, 0.05f, LY_BOARD_H + 0.36f, 1, 1, 1);
    t3_material *own = t3_material_clone(rim);
    cw_color sc = bk_seat_color(i);
    own->emissive = (t3_color){ sc.r, sc.g, sc.b };
    own->emissive_intensity = 0;
    V.glow[i] = mesh_node(V.root, "Glow", fg, own, false, false);
    cw_node3d_set_position(V.glow[i], v3(o.x + LY_BOARD_W * 0.5f, 0.02f, o.z + LY_BOARD_H * 0.5f));
    t3_release(own);
    t3_release(fg);
    V.coins[i] = mesh_node(V.root, "Coins", V.coin_geo, V.coin_mat, true, true);
    V.shown_coins[i] = s->p[i].coins;
  }
  t3_release(bg);
  t3_release(bm);
  t3_release(rim);
  /* pools sized from the totals the game holds */
  for (int gd = 0; gd < HB_GOODS; gd++) {
    V.nbarrel[gd] = hb_goods_total(s, gd);
    if (V.nbarrel[gd] > MAX_BARRELS) CW_FATAL("lanternharbor view: %d barrels of a good", V.nbarrel[gd]);
    for (int i = 0; i < V.nbarrel[gd]; i++) piece_init(&V.barrel[gd][i], mesh_node(V.root, "Barrel", V.barrel_geo, V.barrel_mat[gd], true, true), v3(0, -1, 0));
  }
  for (int k = 0; k < HB_PLOT_KINDS; k++) {
    int n = k == HB_STONE ? s->stones : s->plot_pile[k];
    for (int i = 0; i < s->players; i++)
      for (int j = 0; j < s->p[i].nplots; j++) n += s->p[i].plot[j] == k;
    for (int j = 0; j < HB_ROW; j++) n += s->row[j] == k;
    if (n > MAX_PLOTS) CW_FATAL("lanternharbor view: %d plots of a kind", n);
    V.nplot[k] = n;
    ly_point from = k == HB_STONE ? ly_stone_pile() : ly_plot_pile();
    for (int i = 0; i < n; i++) piece_init(&V.plot[k][i], card("Plot", SMALL_W, SMALL_W, FACE_PLOT + k, -1), at2(from, 0.3f));
  }
  for (int b = 0; b < HB_BUILDINGS; b++) {
    V.ntile[b] = s->bld_supply[b];
    float w = hb_bld[b].lots == 2 ? GRAND_W : SMALL_W;
    for (int i = 0; i < V.ntile[b]; i++) piece_init(&V.tile[b][i], card("Building", w, SMALL_W, b, -1), at2(ly_market(b), 0.1f));
  }
  V.nfolk = hb_folk_total(s);
  if (V.nfolk > MAX_FOLK) CW_FATAL("lanternharbor view: %d townsfolk", V.nfolk);
  for (int i = 0; i < V.nfolk; i++) piece_init(&V.folk[i], mesh_node(V.root, "Folk", V.pawn_geo, V.pawn_mat[i % 4], true, true), v3(-40, 0, LY_BOAT_Z));
  /* the boats come in */
  for (int k = 0; k <= HB_SHIPS; k++) {
    V.boat_x[k] = V.boat_prev[k] = -46.0f - (float)k * 6.0f;
    V.boat_trip[k] = 2;
    V.boat_t[k] = 0;
  }
  for (int i = 0; i < LY_LANTERNS; i++) V.lit[i] = 0;
  V.have_seen = false;
  V.warm = 45;
  V.ev_seen = g->ev_next;
  memset(V.pop, 0, sizeof V.pop);
  for (int i = 0; i < BK_MAX_SEATS; i++) V.seat_glow[i] = 0;
}

/* ── following the table ────────────────────────────────────────────── */

static float dist2d(t3_vec3 a, t3_vec3 b) { float dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz); }

/* rise: a piece leaving the hidden supply comes up out of the table where it lands, and one going back sinks where
 * it stands; otherwise it comes from and goes to `home` */
static void follow(piece *pc, int n, const want *w, int nw, bool rise, t3_vec3 home, const bool *instant_boat) {
  if (nw != n) CW_FATAL("lanternharbor view: %d places for %d pieces", nw, n);
  bool taken[MAX_FOLK] = { 0 }, done[MAX_FOLK] = { 0 };
  for (int k = 0; k < nw; k++) {
    if (key_kind(w[k].key) == K_HIDDEN) continue;
    for (int i = 0; i < n; i++)
      if (!taken[i] && pc[i].key == w[k].key) {
        taken[i] = done[k] = true;
        pc[i].boat = w[k].boat;
        if (!pc[i].moving && dist2d(pc[i].to, w[k].p) > 0.01f) { /* the same place moved (a building's places) */
          pc[i].from = pc[i].pos, pc[i].to = w[k].p, pc[i].t = 0, pc[i].dur = 18, pc[i].moving = true, pc[i].delay = 0;
        }
        break;
      }
  }
  int moved = 0;
  for (int k = 0; k < nw; k++) {
    if (done[k]) continue;
    bool hide = key_kind(w[k].key) == K_HIDDEN;
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
      if (taken[i]) continue;
      float d = dist2d(pc[i].to, w[k].p);
      if (hide) d = key_kind(pc[i].key) == K_HIDDEN ? 0 : 1000 - d;
      else if (key_kind(pc[i].key) == K_HIDDEN) d += rise ? 500 : 0; /* pieces on the table move before new ones come */
      if (d < bd) bd = d, best = i;
    }
    if (best < 0) CW_FATAL("lanternharbor view: no piece left for a place");
    taken[best] = true;
    piece *p = &pc[best];
    bool was_hidden = key_kind(p->key) == K_HIDDEN;
    int old_boat = p->boat;
    p->key = w[k].key;
    p->boat = w[k].boat;
    if (hide && was_hidden) continue;
    if (hide && old_boat >= 0 && instant_boat && instant_boat[old_boat]) { /* it sailed away with its boat */
      p->visible = false, p->moving = false;
      continue;
    }
    if (!hide && was_hidden) {
      p->visible = true;
      p->pos = p->prev = p->from = rise ? v3(w[k].p.x, w[k].p.y - 0.7f, w[k].p.z) : home;
      if (w[k].boat >= 0 && instant_boat && instant_boat[w[k].boat]) p->pos = p->prev = p->from = w[k].p; /* boards out of sight */
    } else {
      p->from = p->pos;
    }
    p->to = hide ? (rise ? v3(p->pos.x, p->pos.y - 0.8f, p->pos.z) : home) : w[k].p;
    float d = dist2d(p->from, p->to);
    p->dur = (int)clampf(16 + d * 1.6f, 16, 50);
    p->t = 0;
    p->delay = moved < 20 ? moved * 2 : 40;
    p->moving = true;
    p->hide_on_arrival = hide;
    moved++;
  }
}

static t3_vec3 pile_pos(ly_point base, int i) {
  /* a pile of barrels: two by two, then a second and a third layer */
  int layer = i / 4, k = i % 4;
  float dx = (k & 1) ? 0.22f : -0.22f, dz = (k & 2) ? 0.2f : -0.2f;
  return v3(base.x + dx * (layer == 1 ? 0.6f : 1) , BOARD_TOP + 0.02f + layer * 0.38f, base.z + dz * (layer == 1 ? 0.6f : 1));
}

static void follow_goods(const hb_state *s, const bool *instant) {
  for (int g = 0; g < HB_GOODS; g++) {
    want w[MAX_BARRELS];
    int n = 0;
    for (int i = 0; i < s->players; i++)
      for (int k = 0; k < s->p[i].goods[g] && n < MAX_BARRELS; k++)
        w[n++] = (want){ key(K_GOODS, i, k, 0), pile_pos(ly_goods(&V.lay, i, g), k), -1 };
    for (int sh = 0; sh < s->nships; sh++)
      if (s->ship_good[sh] == g)
        for (int k = 0; k < s->ship_load[sh] && n < MAX_BARRELS; k++)
          w[n++] = (want){ key(K_HOLD, sh, k, 0), at2(ly_ship_hold(s->nships, sh, s->ship_cap[sh], k), 0.0f), sh };
    for (int k = 0; k < s->nmarket && n < MAX_BARRELS; k++)
      if (s->market[k] == g) w[n++] = (want){ key(K_STALL, k, 0, 0), at2(ly_stall(k), 0.1f), -1 };
    while (n < V.nbarrel[g]) w[n++] = (want){ key(K_HIDDEN, 0, 0, 0), v3(0, -1, 0), -1 };
    follow(V.barrel[g], V.nbarrel[g], w, n, true, v3(0, -1, 0), instant);
  }
}

static void follow_plots(const hb_state *s) {
  for (int kd = 0; kd < HB_PLOT_KINDS; kd++) {
    want w[MAX_PLOTS];
    int n = 0;
    for (int i = 0; i < s->players; i++)
      for (int j = 0; j < s->p[i].nplots; j++)
        if (s->p[i].plot[j] == kd) w[n++] = (want){ key(K_PLOT, i, j, 0), at2(ly_plot(&V.lay, i, j), BOARD_TOP + 0.04f), -1 };
    for (int j = 0; j < HB_ROW; j++)
      if (s->row[j] == kd) w[n++] = (want){ key(K_ROW, j, 0, 0), at2(ly_row(j), 0.05f), -1 };
    while (n < V.nplot[kd]) w[n++] = (want){ key(K_HIDDEN, 0, 0, 0), v3(0, -1, 0), -1 };
    t3_vec3 home = at2(kd == HB_STONE ? ly_stone_pile() : ly_plot_pile(), 0.3f);
    follow(V.plot[kd], V.nplot[kd], w, n, false, home, NULL);
  }
}

static void follow_town(const hb_state *s) {
  for (int b = 0; b < HB_BUILDINGS; b++) {
    want w[MAX_TILES];
    int n = 0;
    for (int i = 0; i < s->players; i++)
      if (hb_owns(&s->p[i], b)) {
        ly_point town[HB_BUILDINGS];
        ly_town(&V.lay, i, &s->p[i], town);
        w[n++] = (want){ key(K_TOWN, i, b, 0), at2(town[b], BOARD_TOP + 0.04f), -1 };
      }
    while (n < V.ntile[b]) w[n++] = (want){ key(K_HIDDEN, 0, 0, 0), v3(0, -1, 0), -1 };
    follow(V.tile[b], V.ntile[b], w, n, false, at2(ly_market(b), 0.1f + 0.06f * s->bld_supply[b]), NULL);
  }
}

static void follow_folk(const hb_state *s, const bool *instant) {
  static want w[MAX_FOLK];
  int n = 0;
  for (int k = 0; k < s->ferry && n < V.nfolk; k++) w[n++] = (want){ key(K_FERRY, k, 0, 0), at2(ly_ferry_seat(k), 0.05f), HB_SHIPS };
  for (int i = 0; i < s->players; i++) {
    const hb_player *pl = &s->p[i];
    for (int k = 0; k < pl->dock && n < V.nfolk; k++) w[n++] = (want){ key(K_DOCK, i, k, 0), at2(ly_dock(&V.lay, i, k), BOARD_TOP), -1 };
    for (int j = 0; j < pl->nplots; j++)
      if (pl->plot_staffed[j]) {
        ly_point p = ly_plot(&V.lay, i, j);
        w[n++] = (want){ key(K_PLACE_PLOT, i, j, 0), v3(p.x + 0.38f, BOARD_TOP + 0.1f, p.z + 0.36f), -1 };
      }
    ly_point town[HB_BUILDINGS];
    ly_town(&V.lay, i, pl, town);
    for (int j = 0; j < pl->nbld; j++) {
      int b = pl->bld[j];
      for (int k = 0; k < pl->staff[b]; k++) w[n++] = (want){ key(K_PLACE_BLD, i, b, k), at2(ly_bld_place(town[b], b, k), BOARD_TOP + 0.1f), -1 };
    }
  }
  if (n > V.nfolk) CW_FATAL("lanternharbor view: more places than townsfolk");
  while (n < V.nfolk) w[n++] = (want){ key(K_HIDDEN, 0, 0, 0), v3(-50, 0, LY_BOAT_Z), -1 };
  follow(V.folk, V.nfolk, w, n, false, v3(-50, 0, LY_BOAT_Z), instant);
}

/* a change rising from where it happened for about a second; one still showing at the same spot moves the new one
 * beside it, never over it */
static void popup_add(t3_vec3 at, const char *text, uint32_t colour) {
  for (int tries = 0; tries < 4; tries++) {
    bool taken = false;
    for (int i = 0; i < POPUPS; i++)
      if (V.pop[i].age < V.pop[i].life && fabsf(V.pop[i].at.x - at.x) < 0.6f && fabsf(V.pop[i].at.z - at.z) < 0.6f) taken = true;
    if (!taken) break;
    at.x += 0.9f;
  }
  for (int i = 0; i < POPUPS; i++)
    if (V.pop[i].age >= V.pop[i].life) {
      V.pop[i] = (popup){ at, "", colour, 0, 60 };
      snprintf(V.pop[i].text, sizeof V.pop[i].text, "%s", text);
      return;
    }
}

static int lanterns_shipped(const hb_state *s) {
  int n = 0;
  for (int i = 0; i < s->players; i++) n += s->p[i].lanterns;
  return n;
}

/* what changed between two shown tables: boats sailing, popups, sounds */
static void diff(const hb_state *a, const hb_state *b, bool *instant) {
  for (int k = 0; k < b->nships; k++)
    if (a->ship_load[k] > 0 && b->ship_load[k] == 0) { /* full: it sails off with its cargo and an empty one comes in */
      V.boat_trip[k] = 1;
      V.boat_t[k] = 0;
      V.cargo_good[k] = a->ship_good[k];
      V.cargo_n[k] = a->ship_load[k];
      instant[k] = true;
      play(V.snd_horn, 0.5f, 0.9f + 0.08f * (float)k);
    }
  if (b->ferry > a->ferry || (a->ferry == 0 && b->ferry > 0)) { /* the ferry went for more townsfolk */
    V.boat_trip[HB_SHIPS] = 2;
    V.boat_t[HB_SHIPS] = 0;
    V.boat_x[HB_SHIPS] = V.boat_prev[HB_SHIPS] = -34.0f;
    instant[HB_SHIPS] = true;
  }
  for (int i = 0; i < b->players; i++) {
    int dl = b->p[i].lanterns - a->p[i].lanterns, dc = b->p[i].coins - a->p[i].coins;
    char buf[24];
    ly_point pl = ly_coins(&V.lay, i);
    if (dl > 0) {
      snprintf(buf, sizeof buf, "+%d", dl);
      ly_point pp = ly_plate(&V.lay, i);
      popup_add(v3(pp.x + 1.4f, 0.8f, pp.z - 0.35f), buf, 0xffd27aff); /* above the plate, clear of its name */
      play(V.snd_chime, 0.45f, 1.0f + 0.03f * (float)(dl > 8 ? 8 : dl));
    }
    if (dc != 0) {
      snprintf(buf, sizeof buf, "%+d", dc);
      popup_add(v3(pl.x, 0.8f, pl.z), buf, dc > 0 ? 0xfff0b0ff : 0xffa080ff);
      play(V.snd_coin, 0.35f, dc > 0 ? 1.0f : 0.85f);
    }
  }
  V.lit_want = (int)floorf((float)LY_LANTERNS * (float)lanterns_shipped(b) / (float)(lanterns_shipped(b) + (b->lantern_supply > 0 ? b->lantern_supply : 0)) + 0.5f);
}

int view_lanterns_lit(void) {
  int n = 0;
  for (int i = 0; i < LY_LANTERNS; i++) n += V.lit[i] > 0.5f;
  return n;
}

static void tick_piece(piece *p, int i) {
  p->prev = p->pos;
  p->prev_yaw = p->yaw;
  if (!p->moving) {
    if (p->settle > 0) p->settle--;
    return;
  }
  if (p->delay > 0) { p->delay--; return; }
  p->t++;
  float u = (float)p->t / (float)p->dur, e = ease(u);
  float d = dist2d(p->from, p->to);
  float arc = sinf(3.14159f * clampf(u, 0, 1)) * (0.3f + d * 0.08f);
  p->pos = v3(p->from.x + (p->to.x - p->from.x) * e, p->from.y + (p->to.y - p->from.y) * e + arc, p->from.z + (p->to.z - p->from.z) * e);
  p->yaw = sinf(3.14159f * u) * 0.3f * ((i & 1) ? 1 : -1);
  if (p->t >= p->dur) {
    p->pos = p->to;
    p->yaw = 0;
    p->moving = false;
    p->settle = 12;
    if (p->hide_on_arrival) p->visible = false;
    else if ((i & 3) == 0) play(V.snd_knock, 0.25f, 0.9f + 0.04f * (float)(i % 5));
  }
}

void view_tick(const mg_game *g) {
  V.ticks++;
  const hb_state *s = &g->shown;
  uint64_t h = hb_hash(s);
  if (!V.have_seen || h != hb_hash(&V.seen)) {
    bool instant[HB_SHIPS + 1] = { 0 };
    if (V.have_seen) diff(&V.seen, s, instant);
    else {
      instant[HB_SHIPS] = true;
      V.lit_want = 0;
    }
    follow_goods(s, instant);
    follow_plots(s);
    follow_town(s);
    follow_folk(s, instant);
    V.seen = *s;
    V.have_seen = true;
  }
  for (; V.ev_seen < g->ev_next; V.ev_seen++) {
    const mg_event *e = &g->ev[V.ev_seen % MG_EVENTS];
    if (e->kind == EV_TURN) play(V.snd_turn, 0.3f, 1.0f);
    else if (e->kind == EV_OVER) play(V.snd_chime, 0.6f, 0.7f);
    else if (e->kind == EV_MOVE && hb_move_type(e->move) == M_ROLE) play(V.snd_knock, 0.5f, 0.8f);
  }
  for (int i = 0; i < POPUPS; i++)
    if (V.pop[i].age < V.pop[i].life) V.pop[i].age++;
  for (int gd = 0; gd < HB_GOODS; gd++)
    for (int i = 0; i < V.nbarrel[gd]; i++) tick_piece(&V.barrel[gd][i], i);
  for (int k = 0; k < HB_PLOT_KINDS; k++)
    for (int i = 0; i < V.nplot[k]; i++) tick_piece(&V.plot[k][i], i);
  for (int b = 0; b < HB_BUILDINGS; b++)
    for (int i = 0; i < V.ntile[b]; i++) tick_piece(&V.tile[b][i], i);
  for (int i = 0; i < V.nfolk; i++) tick_piece(&V.folk[i], i);
  /* boats: leave to the right, come in from the left, slowing as they moor; a wake behind them */
  for (int k = 0; k <= HB_SHIPS; k++) {
    V.boat_prev[k] = V.boat_x[k];
    if (V.boat_trip[k] == 0) continue;
    V.boat_t[k]++;
    float speed;
    if (V.boat_trip[k] == 1) {
      float u = (float)V.boat_t[k] / 80.0f;
      V.boat_x[k] = 46.0f * u * u;
      speed = 92.0f * u / 80.0f;
      if (V.boat_t[k] >= 80) { V.boat_trip[k] = 2; V.boat_t[k] = 0; V.boat_x[k] = V.boat_prev[k] = -46.0f; }
    } else {
      float u = clampf((float)V.boat_t[k] / 100.0f, 0, 1), e = 1 - (1 - u) * (1 - u);
      float start = V.boat_x[k] < -30 && V.boat_t[k] == 1 ? V.boat_x[k] : -46.0f;
      (void)start;
      V.boat_x[k] = -46.0f + 46.0f * e;
      if (k == HB_SHIPS) V.boat_x[k] = -34.0f + 34.0f * e;
      speed = 0.92f * (1 - u);
      if (V.boat_t[k] >= 100) { V.boat_trip[k] = 0; V.boat_x[k] = 0; }
    }
    ly_point c = k == HB_SHIPS ? ly_ferry() : ly_ship(V.nships, k);
    if (speed > 0.05f && cw_node_valid(V.water)) cw_water_wake(V.water, c.x + V.boat_x[k] - 3.2f, c.z, speed * 60.0f, 0, 0.7f, 0.04f, 0.5f);
  }
  /* lanterns swell on one after another (never a flash): about a second each */
  for (int i = 0; i < LY_LANTERNS; i++) {
    int order = (i * 7) % LY_LANTERNS; /* they light in a scattered order along the quay */
    float want_l = order < V.lit_want ? 1.0f : 0.0f;
    float before = V.lit[i];
    V.lit[i] += clampf(want_l - V.lit[i], -0.02f, 0.02f);
    if (before < 0.5f && V.lit[i] >= 0.5f) play(V.snd_chime, 0.2f, 1.4f + 0.02f * (float)(i % 6));
  }
  for (int r = 0; r < HB_ROLES; r++) {
    bool hot = false;
    const mg_target *t = mg_cursor_target(g);
    if (!mg_busy(g) && t && t->kind == TG_MOVE && hb_move_type(t->move) == M_ROLE && hb_move_a(t->move) == r) hot = true;
    V.role_lift[r] += ((hot ? 0.35f : 0) - V.role_lift[r]) * 0.2f;
  }
  for (int i = 0; i < V.players; i++) {
    float want_g = (!g->over && i == g->turn && !mg_busy(g)) ? 1.0f : 0.0f;
    V.seat_glow[i] += (want_g - V.seat_glow[i]) * 0.06f;
    V.shown_coins[i] += ((float)s->p[i].coins - V.shown_coins[i]) * 0.15f;
  }
}

/* ── the frame ─────────────────────────────────────────────────────── */

static float boat_offset(int boat, float alpha) {
  if (boat < 0) return 0;
  return V.boat_prev[boat] + (V.boat_x[boat] - V.boat_prev[boat]) * alpha;
}

static void draw_piece(const piece *p, float alpha, bool flat) {
  set_visible(p->node, p->visible);
  if (!p->visible) return;
  t3_vec3 q = v3(p->prev.x + (p->pos.x - p->prev.x) * alpha, p->prev.y + (p->pos.y - p->prev.y) * alpha, p->prev.z + (p->pos.z - p->prev.z) * alpha);
  q.x += boat_offset(p->boat, alpha);
  if (p->boat >= 0) q.y += 0.04f * sinf((float)V.ticks * 0.05f + (float)p->boat);
  cw_node3d_set_position(p->node, q);
  cw_node3d_set_rotation(p->node, v3(0, flat ? 0 : p->prev_yaw + (p->yaw - p->prev_yaw) * alpha, 0));
  float sq = 0;
  if (p->settle > 0) {
    float k = (12 - p->settle + alpha) / 12.0f;
    sq = sinf(k * 6.2832f) * 0.06f * (1 - k);
  }
  cw_node3d_set_scale(p->node, v3(1 + sq * 0.5f, 1 - sq, 1 + sq * 0.5f));
}

static void show_ghost_barrels(int good, const t3_vec3 *at, int n) {
  for (int i = 0; i < 7; i++) {
    set_visible(V.ghost_barrel[i], i < n);
    if (i < n) cw_node3d_set_position(V.ghost_barrel[i], v3(at[i].x, at[i].y + 0.05f * sinf((float)V.ticks * 0.08f), at[i].z));
  }
  (void)good;
}

static void ghosts(const mg_game *g) {
  set_visible(V.ghost_card_small, false);
  set_visible(V.ghost_card_grand, false);
  set_visible(V.ghost_plot, false);
  set_visible(V.ghost_pawn, false);
  show_ghost_barrels(0, NULL, 0);
  bk_move m = mg_preview(g);
  if (m == BK_MOVE_NONE || mg_busy(g) || g->help) return;
  const hb_state *s = &g->shown;
  int seat = s->actor, a = hb_move_a(m), b = hb_move_b(m);
  const hb_player *pl = &s->p[seat];
  float bob = 0.06f * sinf((float)V.ticks * 0.08f);
  switch (hb_move_type(m)) {
  case M_TAKE: case M_STONE:
    if (pl->nplots < HB_ISLAND) {
      bk_card_set_front(V.ghost_plot, FACE_PLOT + (hb_move_type(m) == M_STONE ? HB_STONE : a));
      cw_node3d_set_position(V.ghost_plot, at2(ly_plot(&V.lay, seat, pl->nplots), BOARD_TOP + 0.12f + bob));
      set_visible(V.ghost_plot, true);
    }
    break;
  case M_BUILD: {
    cw_node gc = hb_bld[a].lots == 2 ? V.ghost_card_grand : V.ghost_card_small;
    bk_card_set_front(gc, a);
    cw_node3d_set_position(gc, at2(ly_next_lot(&V.lay, seat, pl, a), BOARD_TOP + 0.12f + bob));
    set_visible(gc, true);
    break;
  }
  case M_PLACE_PLOT: {
    for (int i = 0; i < pl->nplots; i++)
      if (pl->plot[i] == a && !pl->plot_staffed[i]) {
        ly_point p = ly_plot(&V.lay, seat, i);
        cw_node3d_set_position(V.ghost_pawn, v3(p.x + 0.38f, BOARD_TOP + 0.12f + bob, p.z + 0.36f));
        set_visible(V.ghost_pawn, true);
        break;
      }
    break;
  }
  case M_PLACE_BLD: {
    ly_point town[HB_BUILDINGS];
    ly_town(&V.lay, seat, pl, town);
    cw_node3d_set_position(V.ghost_pawn, at2(ly_bld_place(town[a], a, pl->staff[a]), BOARD_TOP + 0.12f + bob));
    set_visible(V.ghost_pawn, true);
    break;
  }
  case M_SELL: {
    t3_vec3 p = at2(ly_stall(s->nmarket < HB_MARKET ? s->nmarket : HB_MARKET - 1), 0.1f);
    show_ghost_barrels(a, &p, 1);
    break;
  }
  case M_EXTRA: {
    t3_vec3 p = pile_pos(ly_goods(&V.lay, seat, a), pl->goods[a]);
    show_ghost_barrels(a, &p, 1);
    break;
  }
  case M_LOAD: {
    int n = hb_load_amount(s, seat, a, b);
    t3_vec3 p[7];
    for (int i = 0; i < n && i < 7; i++) p[i] = at2(ly_ship_hold(s->nships, b, s->ship_cap[b], s->ship_load[b] + i), 0.0f);
    show_ghost_barrels(a, p, n < 7 ? n : 7);
    break;
  }
  default: break;
  }
}

void view_frame(const mg_game *g, float alpha) {
  const hb_state *s = &g->shown;
  for (int gd = 0; gd < HB_GOODS; gd++)
    for (int i = 0; i < V.nbarrel[gd]; i++) draw_piece(&V.barrel[gd][i], alpha, false);
  for (int k = 0; k < HB_PLOT_KINDS; k++)
    for (int i = 0; i < V.nplot[k]; i++) draw_piece(&V.plot[k][i], alpha, true);
  for (int b = 0; b < HB_BUILDINGS; b++)
    for (int i = 0; i < V.ntile[b]; i++) draw_piece(&V.tile[b][i], alpha, true);
  for (int i = 0; i < V.nfolk; i++) draw_piece(&V.folk[i], alpha, false);
  /* boats and their cargo on the way out */
  for (int k = 0; k <= HB_SHIPS; k++) {
    if (k < HB_SHIPS && k >= V.nships) continue;
    ly_point c = k == HB_SHIPS ? ly_ferry() : ly_ship(V.nships, k);
    float x = boat_offset(k, alpha);
    float bob = 0.05f * sinf((float)V.ticks * 0.05f + (float)k);
    cw_node3d_set_position(V.boat[k], v3(c.x + x, 0.05f + bob, c.z));
    cw_node3d_set_rotation(V.boat[k], v3(0.02f * sinf((float)V.ticks * 0.04f + k), 0, 0.015f * sinf((float)V.ticks * 0.033f + k)));
    if (k < HB_SHIPS) {
      bool show = V.boat_trip[k] == 1;
      for (int i = 0; i < 7; i++) {
        bool on = show && i < V.cargo_n[k];
        set_visible(V.cargo[k][i], on);
        if (!on) continue;
        float hx = ((float)i - (float)(V.cargo_n[k] - 1) * 0.5f) * 0.92f;
        cw_node3d_set_position(V.cargo[k][i], v3(hx, 0.0f, 0.05f));
        t3_object *o = cw_node3d_object(V.cargo[k][i]);
        if (o && o->child_count) {
          t3_mesh *mm = (t3_mesh *)o->children[0];
          if (mm->material_count && mm->materials[0] != V.barrel_mat[V.cargo_good[k]]) {
            t3_retain(V.barrel_mat[V.cargo_good[k]]);
            t3_release(mm->materials[0]);
            mm->materials[0] = V.barrel_mat[V.cargo_good[k]];
          }
        }
      }
    }
  }
  /* role cards lift under the cursor; taken ones dim; coins on the ones nobody took */
  for (int r = 0; r < s->roles_in_play; r++) {
    ly_point p = ly_role(r);
    float lift = V.role_lift[r];
    cw_node3d_set_position(V.role_card[r], v3(p.x, 0.05f + lift + (s->role_taken[r] ? -0.02f : 0), p.z + (s->role_taken[r] ? 0.25f : 0)));
    cw_node3d_set_rotation(V.role_card[r], v3(s->role_taken[r] ? 0.0f : -lift * 0.5f, s->role_taken[r] ? 0.12f : 0, 0));
    for (int c = 0; c < 6; c++) {
      bool on = c < s->role_coins[r] && !s->role_taken[r];
      set_visible(V.role_coins[r][c], on);
      if (on) cw_node3d_set_position(V.role_coins[r][c], v3(p.x + 0.75f, 0.12f + lift + c * 0.075f, p.z + 0.35f));
    }
  }
  /* the market stacks: thickness for the copies left */
  for (int b = 0; b < HB_BUILDINGS; b++) {
    int left = s->bld_supply[b];
    ly_point p = ly_market(b);
    float hgt = 0.02f + 0.06f * (float)left;
    cw_node3d_set_scale(V.market_stack[b], v3(hb_bld[b].lots == 2 ? MARKET_GW * 0.98f : MARKET_W * 0.98f, hgt, (hb_bld[b].lots == 2 ? MARKET_GW * SMALL_W / GRAND_W : MARKET_W) * 0.98f));
    set_visible(V.market_stack[b], left > 0);
    cw_node3d_set_position(V.market_stack[b], v3(p.x, hgt * 0.5f, p.z));
    set_visible(V.market_card[b], left > 0);
    cw_node3d_set_position(V.market_card[b], v3(p.x, hgt + 0.01f, p.z));
  }
  int pile = 0;
  for (int k = 0; k < HB_GOODS; k++) pile += s->plot_pile[k] + s->plot_discard[k];
  set_visible(V.pile_card, pile > 0);
  set_visible(V.stone_card, s->stones > 0);
  cw_node3d_set_position(V.pile_card, v3(ly_plot_pile().x, 0.02f + 0.02f * (float)(pile > 40 ? 40 : pile), ly_plot_pile().z));
  cw_node3d_set_position(V.stone_card, v3(ly_stone_pile().x, 0.02f + 0.06f * (float)s->stones, ly_stone_pile().z));
  /* coin stacks and the turn glow */
  for (int i = 0; i < V.players; i++) {
    ly_point c = ly_coins(&V.lay, i);
    float n = fmaxf(V.shown_coins[i], 0.2f), hgt = 0.06f * fminf(n, 20.0f);
    cw_node3d_set_scale(V.coins[i], v3(1.0f, hgt, 1.0f));
    cw_node3d_set_position(V.coins[i], v3(c.x, BOARD_TOP + hgt * 0.5f, c.z));
    set_visible(V.coins[i], s->p[i].coins > 0);
    set_emissive(V.glow[i], 1.2f * V.seat_glow[i]);
  }
  /* lanterns and the lamps by the water */
  int lit = 0;
  float total = 0;
  for (int i = 0; i < LY_LANTERNS; i++) {
    set_glow(V.lantern[i], ease(V.lit[i]));
    total += V.lit[i];
    lit += V.lit[i] > 0.5f;
  }
  for (int i = 0; i < 4; i++) {
    float share = 0;
    for (int k = i * 6; k < i * 6 + 6; k++) share += V.lit[k];
    cw_node_set(V.lights[i], "energy", cw_val_float(0.2f + 3.2f * share / 6.0f));
  }
  (void)lit;
  (void)total;
  ghosts(g);
  if (V.warm > 0) { /* under the deck: drawn (their programs made now, not mid-game) and never seen */
    V.warm--;
    for (int gd = 0; gd < HB_GOODS; gd++)
      if (V.nbarrel[gd]) {
        set_visible(V.barrel[gd][0].node, true);
        cw_node3d_set_position(V.barrel[gd][0].node, v3((float)gd, -2.0f, 4));
      }
    cw_node ghost_nodes[5] = { V.ghost_card_small, V.ghost_card_grand, V.ghost_plot, V.ghost_pawn, V.ghost_barrel[0] };
    for (int i = 0; i < 5; i++) {
      set_visible(ghost_nodes[i], true);
      cw_node3d_set_position(ghost_nodes[i], v3((float)i * 3, -2.0f, 6));
    }
    for (int k = 0; k < HB_SHIPS; k++) set_visible(V.cargo[k][0], true);
  }
}

/* ── the HUD ───────────────────────────────────────────────────────── */

static cw_rect view_rect(void) { cw_vec2 sz = cw_r2d_canvas_size(); return (cw_rect){ 0, 0, sz.x, sz.y }; }
static cw_vec2 project(t3_vec3 p) { return cw_camera3d_world_to_screen(V.camera, view_rect(), p, NULL); }

/* every HUD line stays above the engine's text floor (render2d.h: 1.67% of the canvas height) */
static float floor_size(float size) { float m = cw_r2d_canvas_size().y * 0.0206f; /* 22 px at 1080 lines: read from the couch */ return size < m ? m : size; }

static void put_text(const char *s, cw_vec2 at, cw_text_style st) {
  if (!st.fixed_size) st.size = floor_size(st.size);
  if (st.wrap_width <= 0 && st.align != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), s, &st);
    at.x -= st.align == CW_ALIGN_RIGHT ? m.width : m.width * 0.5f;
    st.align = CW_ALIGN_LEFT;
  }
  cw_r2d_text(cw_ui_font_sans(), s, at, &st);
}

static void text(const char *s, cw_vec2 at, float size, cw_color c, cw_text_align align) {
  put_text(s, at, (cw_text_style){ .size = size, .color = c, .align = align, .outline_width = size * 0.1f, .outline_color = cw_rgba(0.04f, 0.03f, 0.05f, 0.9f) });
}

static float text_w(const char *s, float size) {
  cw_text_style st = { .size = size };
  return cw_font_measure(cw_ui_font_sans(), s, &st).width;
}

static void panel(cw_rect r, float u) {
  r = (cw_rect){ floorf(r.x) + 0.5f, floorf(r.y) + 0.5f, floorf(r.w), floorf(r.h) }; /* on pixel centres: hosts agree on its edges */
  u = floorf(u * 4.0f + 0.5f) / 4.0f;
  cw_r2d_round_rect((cw_rect){ r.x + 5 * u, r.y + 7 * u, r.w, r.h }, 16 * u, 8, cw_rgba(0, 0, 0, 0.35f));
  cw_r2d_round_rect(r, 16 * u, 8, cw_rgba(0.98f, 0.93f, 0.84f, 0.97f));
  cw_r2d_round_rect_lines(r, 16 * u, 3 * u, 8, cw_rgba(0.74f, 0.52f, 0.2f, 1));
}

static const char *const HELP[] = {
  "Each round every player takes one job from the row, in turn. Everyone then does that job, the taker first and "
  "with a bonus. A job nobody takes gathers a coin for later.",
  "Surveyor: claim a plot. Ferryman: the ferry lands townsfolk; place them on plots and buildings. Mason: raise "
  "a building; staffed stone pits make it cheaper. Artisan: staffed plots with staffed workshops make goods "
  "(rice needs no workshop).",
  "Merchant: sell one good to the stall for coins. Skipper: load goods onto the boats for lanterns. Anyone who can "
  "load must, and goods left over spoil, but you keep one. Diver: dives for a pearl, one coin.",
  "Lanterns are points. The game ends after the round when townsfolk or lanterns run out, or a town fills its "
  "twelve lots. Lanterns, building values and staffed grand buildings' bonuses win.",
};

/* the how-to-play card: over play (with its prompt line) and as the title page (the shell draws the way back) */
static void draw_help(cw_vec2 sz, bool prompt) {
  float u = sz.y / 720.0f;
  cw_rect r = { sz.x * 0.5f - 430 * u, sz.y * 0.5f - 300 * u, 860 * u, 600 * u };
  panel(r, u);
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.08f, 1), gold = cw_rgba(0.62f, 0.4f, 0.1f, 1);
  put_text("How to play", (cw_vec2){ r.x + r.w * 0.5f, r.y + 24 * u }, (cw_text_style){ .size = 38 * u, .color = gold, .align = CW_ALIGN_CENTER });
  float y = r.y + 86 * u;
  for (int i = 0; i < 4; i++) { /* each paragraph under the last, an even gap between */
    cw_text_style hs = { .size = 20 * u, .color = ink, .wrap_width = r.w - 80 * u, .line_spacing = 1.12f };
    put_text(HELP[i], (cw_vec2){ r.x + 40 * u, y }, hs);
    hs.size = floor_size(hs.size);
    y += cw_font_measure(cw_ui_font_sans(), HELP[i], &hs).height + 26 * u;
  }
  if (prompt) cw_prompt_text(cw_ui_font_sans(), "{choose} play    {how_to_play} this card any time", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 44 * u }, 22 * u, ink,
                 CW_ALIGN_CENTER);
}

static void draw_results(const mg_game *g, cw_vec2 sz) {
  float u = sz.y / 720.0f;
  const hb_state *s = mg_state(g);
  cw_rect r = { sz.x * 0.5f - 360 * u, sz.y * 0.5f - 210 * u, 720 * u, 420 * u };
  panel(r, u);
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.08f, 1), gold = cw_rgba(0.62f, 0.4f, 0.1f, 1), soft = cw_rgba(0.42f, 0.33f, 0.25f, 1);
  unsigned w = hb_winners(s);
  int nw = 0, last = 0;
  for (int i = 0; i < s->players; i++)
    if (w >> i & 1) nw++, last = i;
  char line[96];
  if (nw == 1) snprintf(line, sizeof line, "%s lights the harbour", mg_seat_name(last));
  else snprintf(line, sizeof line, "A shared win");
  put_text(line, (cw_vec2){ r.x + r.w * 0.5f, r.y + 24 * u }, (cw_text_style){ .size = 38 * u, .color = gold, .align = CW_ALIGN_CENTER });
  int order[BK_MAX_SEATS];
  for (int i = 0; i < s->players; i++) order[i] = i;
  for (int i = 0; i < s->players; i++)
    for (int j = i + 1; j < s->players; j++)
      if (hb_score(&s->p[order[j]]) > hb_score(&s->p[order[i]])) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  static const char *const HEAD[4] = { "lanterns", "buildings", "bonuses", "total" };
  const float colx[4] = { r.x + r.w - 380 * u, r.x + r.w - 270 * u, r.x + r.w - 160 * u, r.x + r.w - 60 * u };
  float y = r.y + 110 * u;
  for (int k = 0; k < 4; k++) put_text(HEAD[k], (cw_vec2){ colx[k], y - 30 * u }, (cw_text_style){ .size = 17 * u, .color = soft, .align = CW_ALIGN_CENTER });
  for (int k = 0; k < s->players; k++) {
    int i = order[k];
    const hb_player *pl = &s->p[i];
    int bv = 0, bonus = 0;
    for (int j = 0; j < pl->nbld; j++) bv += hb_bld[pl->bld[j]].value, bonus += hb_bonus(pl, pl->bld[j]);
    cw_r2d_circle((cw_vec2){ r.x + 50 * u, y + 16 * u }, 11 * u, 20, bk_seat_color(i));
    snprintf(line, sizeof line, "%s%s", mg_seat_name(i), mg_seat_is_cpu(g, i) ? " (computer)" : "");
    put_text(line, (cw_vec2){ r.x + 72 * u, y }, (cw_text_style){ .size = 25 * u, .color = ink });
    const int cell[3] = { pl->lanterns, bv, bonus };
    for (int c = 0; c < 3; c++) {
      snprintf(line, sizeof line, "%d", cell[c]);
      put_text(line, (cw_vec2){ colx[c], y + 2 * u }, (cw_text_style){ .size = 22 * u, .color = ink, .align = CW_ALIGN_CENTER });
    }
    snprintf(line, sizeof line, "%d", hb_score(pl));
    put_text(line, (cw_vec2){ colx[3], y - 2 * u }, (cw_text_style){ .size = 30 * u, .color = gold, .align = CW_ALIGN_CENTER });
    y += 54 * u;
  }
  cw_prompt_text(cw_ui_font_sans(), "{choose} play again    {back} title", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 44 * u }, 22 * u, ink,
                 CW_ALIGN_CENTER);
}

/* the card about what the cursor is on: its name, what it does, what it costs */
static void draw_info(const mg_game *g, cw_vec2 sz) {
  const mg_target *t = mg_cursor_target(g);
  if (!t || mg_busy(g) || g->over || mg_seat_is_cpu(g, g->turn)) return;
  const hb_state *s = &g->shown;
  float u = sz.y / 720.0f;
  char title[64] = "", body[160] = "", foot[96] = "";
  if (t->kind == TG_GOOD) {
    snprintf(title, sizeof title, "Load %s", hb_good_name[t->good]);
    snprintf(body, sizeof body, "You have %d. Choose it, then the boat. A boat holds one good, and a good sails on one boat.", s->p[s->actor].goods[t->good]);
  } else if (t->kind == TG_PUTBACK) {
    snprintf(title, sizeof title, "Put it back");
    snprintf(body, sizeof body, "Choose another good to load.");
  } else {
    bk_move m = t->move;
    int a = hb_move_a(m), b = hb_move_b(m);
    switch (hb_move_type(m)) {
    case M_ROLE:
      snprintf(title, sizeof title, "%s", hb_role_name[a]);
      snprintf(body, sizeof body, "%s", hb_role_effect[a]);
      if (s->role_coins[a]) snprintf(foot, sizeof foot, "%d coin%s waiting on it", s->role_coins[a], s->role_coins[a] == 1 ? "" : "s");
      break;
    case M_BUILD: case M_PLACE_BLD: case M_JETTY: {
      int bb = hb_move_type(m) == M_JETTY ? B_JETTY : a;
      snprintf(title, sizeof title, "%s", hb_bld[bb].name);
      snprintf(body, sizeof body, "%s", hb_bld_effect[bb]);
      if (hb_move_type(m) == M_BUILD)
        snprintf(foot, sizeof foot, "you pay %d of your %d coins    worth %d lantern%s    %d worker%s", hb_build_cost(s, s->actor, bb), s->p[s->actor].coins,
                 hb_bld[bb].value, hb_bld[bb].value == 1 ? "" : "s", hb_bld[bb].slots, hb_bld[bb].slots == 1 ? "" : "s");
      else if (hb_move_type(m) == M_PLACE_BLD) snprintf(foot, sizeof foot, "a worker goes here (%d waiting)", s->p[s->actor].dock);
      else snprintf(foot, sizeof foot, "ship every %s you have off your own jetty", hb_good_name[a]);
      break;
    }
    case M_TAKE: snprintf(title, sizeof title, "Claim a %s", hb_plot_name[a]); snprintf(body, sizeof body, a == G_RICE ? "Grows rice with a worker on it; no workshop needed." : "Grows a crop with a worker on it; a staffed workshop turns it into goods."); break;
    case M_STONE: snprintf(title, sizeof title, "Open a stone pit"); snprintf(body, sizeof body, "A staffed stone pit takes a coin off each building (more for bigger ones)."); break;
    case M_SEED: snprintf(title, sizeof title, "Draw a hidden plot"); snprintf(body, sizeof body, "Your seed shed: a plot from the face-down pile, then claim as usual."); break;
    case M_PASS: snprintf(title, sizeof title, "Pass"); snprintf(body, sizeof body, "Do nothing this time."); break;
    case M_REARRANGE: snprintf(title, sizeof title, "Call everyone back"); snprintf(body, sizeof body, "Take every worker off your plots and buildings to place them all again."); break;
    case M_PLACE_PLOT: snprintf(title, sizeof title, "Work a %s", hb_plot_name[a]); snprintf(body, sizeof body, "A worker goes to this plot (%d waiting).", s->p[s->actor].dock); break;
    case M_EXTRA: snprintf(title, sizeof title, "One extra %s", hb_good_name[a]); snprintf(body, sizeof body, "The artisan's bonus: one more of a good you made."); break;
    case M_SELL: snprintf(title, sizeof title, "Sell %s", hb_good_name[a]); snprintf(body, sizeof body, "The stall pays %d coin%s for it.", hb_sale_price(s, s->actor, a), hb_sale_price(s, s->actor, a) == 1 ? "" : "s"); break;
    case M_LOAD: snprintf(title, sizeof title, "Boat %d", b + 1); snprintf(body, sizeof body, "Load %d %s here: a lantern each.", hb_load_amount(s, s->actor, a, b), hb_good_name[a]); break;
    default: break;
    }
  }
  if (!title[0]) return;
  /* the focus panel: large, away from what it describes (over the water for the lower table, at the bottom for the
   * upper rows) */
  float w = 600 * u, h = 132 * u;
  cw_vec2 at = project(v3(t->at.x, t->y, t->at.z));
  float top = at.y > sz.y * 0.42f ? 48 * u : sz.y - h - 52 * u;
  cw_rect r = { sz.x * 0.5f - w * 0.5f, top, w, h };
  cw_r2d_round_rect(r, 12 * u, 8, cw_rgba(0.08f, 0.07f, 0.11f, 0.9f));
  cw_r2d_round_rect_lines(r, 12 * u, 2 * u, 8, cw_rgba(0.95f, 0.72f, 0.36f, 0.9f));
  put_text(title, (cw_vec2){ r.x + 18 * u, r.y + 8 * u }, (cw_text_style){ .size = 28 * u, .color = cw_rgba(1, 0.86f, 0.55f, 1) });
  put_text(body, (cw_vec2){ r.x + 18 * u, r.y + 46 * u }, (cw_text_style){ .size = 20 * u, .color = cw_rgba(0.98f, 0.95f, 0.9f, 1), .wrap_width = w - 36 * u, .line_spacing = 1.0f });
  if (foot[0]) put_text(foot, (cw_vec2){ r.x + 18 * u, r.y + h - 30 * u }, (cw_text_style){ .size = 17 * u, .color = cw_rgba(1, 0.8f, 0.5f, 1) });
}

static const char *phase_prompt(const hb_state *s, ui_mode mode) {
  switch (s->phase) {
  case PH_PICK: return "take a job";
  case PH_SETTLE: return "claim a plot";
  case PH_ARRANGE: return "place your townsfolk";
  case PH_BUILD: return "raise a building, or pass";
  case PH_EXTRA: return "choose your extra good";
  case PH_TRADE: return "sell a good, or pass";
  case PH_SHIP: return mode == UI_PICK ? "choose a good to load" : "choose its boat";
  default: return "";
  }
}

void view_hud(const mg_game *g) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  const hb_state *s = &g->shown;
  cw_color cream = cw_rgba(1, 0.95f, 0.86f, 1);
  char line[192];
  /* plates: name and score, then who plays it and the lanterns; counts by the piles in big figures */
  for (int i = 0; i < g->players; i++) {
    const hb_player *pl = &s->p[i];
    ly_point o = ly_plate(&V.lay, i);
    cw_vec2 a = project(v3(o.x, BOARD_TOP, o.z));
    cw_vec2 b = project(v3(o.x + 3.9f, BOARD_TOP, o.z + 2.2f));
    cw_rect plate = { a.x, a.y, b.x - a.x, 66 * u };
    cw_color sc = bk_seat_color(i);
    float glow = V.seat_glow[i];
    cw_r2d_round_rect(plate, 9 * u, 6, cw_rgba(0.07f, 0.06f, 0.09f, 0.82f + 0.15f * glow));
    if (glow > 0.02f) cw_r2d_round_rect_lines(plate, 9 * u, 3 * u, 6, cw_rgba(sc.r, sc.g, sc.b, glow));
    cw_r2d_circle((cw_vec2){ plate.x + 13 * u, plate.y + 14 * u }, 7 * u, 20, sc);
    if (i == s->governor) cw_r2d_circle_lines((cw_vec2){ plate.x + 13 * u, plate.y + 14 * u }, 10 * u, 2.5f * u, 20, cw_rgba(1, 0.8f, 0.35f, 1));
    /* row one: the name; row two: who plays it; the bottom row: the lanterns won. Right: the score, big, with the word under it */
    text(mg_seat_name(i), (cw_vec2){ plate.x + 26 * u, plate.y + 0 * u }, 17 * u, cream, CW_ALIGN_LEFT);
    snprintf(line, sizeof line, "%d", hb_score(pl));
    text(line, (cw_vec2){ plate.x + plate.w - 7 * u, plate.y + 9 * u }, 25 * u, cw_rgba(1, 0.84f, 0.5f, 1), CW_ALIGN_RIGHT);
    text("points", (cw_vec2){ plate.x + plate.w - 7 * u, plate.y + 34 * u }, 15 * u, cw_rgba(0.95f, 0.85f, 0.65f, 1), CW_ALIGN_RIGHT);
    if (mg_seat_is_cpu(g, i)) snprintf(line, sizeof line, "%s", cw_shell_cpu_level_label((cw_shell_cpu_level)g->cpu_level[i]));
    else snprintf(line, sizeof line, "player");
    text(line, (cw_vec2){ plate.x + 7 * u, plate.y + 18 * u }, 15 * u, cw_rgba(0.88f, 0.84f, 0.78f, 1), CW_ALIGN_LEFT);
    glyph_lantern(plate.x + 14 * u, plate.y + 59 * u, 5.5f * u, cw_rgba(1, 0.6f, 0.2f, 1));
    snprintf(line, sizeof line, "%d lanterns", pl->lanterns);
    text(line, (cw_vec2){ plate.x + 23 * u, plate.y + 48 * u }, 15 * u, cw_rgba(1, 0.75f, 0.35f, 1), CW_ALIGN_LEFT);
    for (int gd = 0; gd < HB_GOODS; gd++) {
      ly_point p = ly_goods(&V.lay, i, gd);
      cw_vec2 q = project(v3(p.x, BOARD_TOP, p.z + 0.62f));
      /* the count big on a barrel of the good's colour (dim when empty), ink by the barrel's lightness */
      static const bool LIGHT[HB_GOODS] = { true, false, true, false, true };
      bool any = pl->goods[gd] > 0;
      cw_rect br = { q.x - 14 * u, q.y - 12 * u, 28 * u, 26 * u };
      cw_color bc = hexc(GOOD_HEX[gd]);
      bc.a = any ? 1 : 0.4f;
      cw_r2d_round_rect(br, 8 * u, 8, bc);
      cw_r2d_round_rect_lines(br, 8 * u, 1.6f * u, 8, cw_rgba(0.07f, 0.05f, 0.03f, any ? 1 : 0.5f));
      snprintf(line, sizeof line, "%d", pl->goods[gd]);
      cw_color ink = LIGHT[gd] ? cw_rgba(0.12f, 0.07f, 0.03f, 1) : cw_rgba(1, 0.97f, 0.9f, 1);
      if (!any) ink.a = 0.7f;
      put_text(line, (cw_vec2){ q.x, q.y - 12 * u }, (cw_text_style){ .size = 21 * u, .color = ink, .align = CW_ALIGN_CENTER });
    }
    /* the coins: the count on a gold coin, as the market's cost badges */
    ly_point cp = ly_coins(&V.lay, i);
    cw_vec2 q = project(v3(cp.x, BOARD_TOP, cp.z + 0.62f));
    snprintf(line, sizeof line, "%d", pl->coins);
    cw_r2d_circle((cw_vec2){ q.x, q.y + 1 * u }, 15 * u, 24, cw_rgba(0.45f, 0.3f, 0.06f, 1));
    cw_r2d_circle((cw_vec2){ q.x, q.y + 1 * u }, 13 * u, 24, cw_rgba(0.95f, 0.72f, 0.26f, 1));
    put_text(line, (cw_vec2){ q.x, q.y - 11 * u }, (cw_text_style){ .size = 20 * u, .color = cw_rgba(0.15f, 0.08f, 0.02f, 1), .align = CW_ALIGN_CENTER });
    if (pl->dock) {
      ly_point dp = ly_dock(&V.lay, i, 7);
      cw_vec2 dq = project(v3(dp.x, BOARD_TOP, dp.z + 0.4f));
      snprintf(line, sizeof line, "%d waiting", pl->dock);
      text(line, (cw_vec2){ dq.x, dq.y }, 15 * u, cream, CW_ALIGN_CENTER);
    }
  }
  /* coins waiting on the jobs nobody took: a coin with the count */
  for (int r = 0; r < s->roles_in_play; r++) {
    if (!s->role_coins[r] || s->role_taken[r]) continue;
    ly_point p = ly_role(r);
    cw_vec2 q = project(v3(p.x + ROLE_W * 0.5f - 0.2f, 0.3f, p.z + ROLE_H * 0.5f - 0.15f)); /* the lower corner, clear of the name */
    cw_r2d_circle(q, 15 * u, 24, cw_rgba(0.62f, 0.42f, 0.1f, 1));
    cw_r2d_circle(q, 12.5f * u, 24, cw_rgba(0.98f, 0.78f, 0.3f, 1));
    snprintf(line, sizeof line, "%d", s->role_coins[r]);
    put_text(line, (cw_vec2){ q.x, q.y - 11 * u }, (cw_text_style){ .size = 18 * u, .color = cw_rgba(0.2f, 0.1f, 0.02f, 1), .align = CW_ALIGN_CENTER });
  }
  /* the market's costs (names too while the mason works) */
  bool building = s->phase == PH_BUILD && !mg_busy(g);
  for (int b = 0; b < HB_BUILDINGS; b++) {
    ly_point p = ly_market(b);
    cw_vec2 q = project(v3(p.x, 0.1f, p.z + SMALL_W * 0.5f + 0.02f));
    int c = building ? hb_build_cost(s, s->actor, b) : hb_bld[b].cost;
    bool can = !building || (s->bld_supply[b] && c <= s->p[s->actor].coins && !hb_owns(&s->p[s->actor], b));
    if (!s->bld_supply[b]) continue;
    snprintf(line, sizeof line, "%d", c);
    /* the cost coin: left of a small building's tile, on the corner of a grand one */
    bool grand_b = hb_bld[b].lots == 2;
    cw_vec2 bq = grand_b ? project(v3(p.x - MARKET_GW * 0.5f + 0.25f, 0.1f, p.z - MARKET_GW * SMALL_W / GRAND_W * 0.5f + 0.25f))
                         : project(v3(p.x - MARKET_W * 0.5f - 0.5f, 0.1f, p.z));
    cw_r2d_circle(bq, 15 * u, 22, cw_rgba(0.45f, 0.3f, 0.06f, 1));
    cw_r2d_circle(bq, 13 * u, 22, can ? cw_rgba(0.95f, 0.72f, 0.26f, 1) : cw_rgba(0.35f, 0.32f, 0.3f, 1));
    put_text(line, (cw_vec2){ bq.x, bq.y - 11 * u }, (cw_text_style){ .size = 18 * u, .color = can ? cw_rgba(0.15f, 0.08f, 0.02f, 1) : cw_rgba(0.85f, 0.8f, 0.72f, 1), .align = CW_ALIGN_CENTER });
    (void)q;
  }
  /* labels by the piles, the pass sign, the stall and the boats */
  {
    cw_vec2 q = project(v3(ly_pass().x, 0.2f, ly_pass().z));
    text("pass", (cw_vec2){ q.x, q.y - 10 * u }, 17 * u, cream, CW_ALIGN_CENTER);
    q = project(v3(ly_plot_pile().x + 0.8f, 0.3f, ly_plot_pile().z));
    text("hidden", (cw_vec2){ q.x + 4 * u, q.y - 9 * u }, 13 * u, cw_rgba(0.9f, 0.86f, 0.8f, 0.9f), CW_ALIGN_LEFT);
    q = project(v3(ly_stone_pile().x + 0.8f, 0.3f, ly_stone_pile().z));
    snprintf(line, sizeof line, "x%d", s->stones);
    text(line, (cw_vec2){ q.x + 4 * u, q.y - 9 * u }, 13 * u, cw_rgba(0.9f, 0.86f, 0.8f, 0.9f), CW_ALIGN_LEFT);
    q = project(v3(ly_stall(0).x - 0.5f, 0.3f, ly_stall(0).z - 1.0f));
    text("market stall", (cw_vec2){ q.x, q.y }, 14 * u, cream, CW_ALIGN_LEFT);
    q = project(v3(ly_ferry().x, 0.3f, ly_ferry().z - 1.4f));
    snprintf(line, sizeof line, "ferry: %d aboard", s->ferry);
    text(line, (cw_vec2){ q.x, q.y }, 14 * u, cream, CW_ALIGN_CENTER);
    for (int k = 0; k < s->nships; k++) {
      ly_point c = ly_ship(s->nships, k);
      q = project(v3(c.x + V.boat_x[k], 0.3f, c.z + 1.25f));
      if (s->ship_load[k]) snprintf(line, sizeof line, "%d of %d %s", s->ship_load[k], s->ship_cap[k], hb_good_name[s->ship_good[k]]);
      else snprintf(line, sizeof line, "empty, holds %d", s->ship_cap[k]);
      text(line, (cw_vec2){ q.x, q.y }, 14 * u, cream, CW_ALIGN_CENTER);
    }
  }
  /* cursor frame: round what it is on, in the seat's colour, breathing slowly */
  const mg_target *t = mg_cursor_target(g);
  if (t && !mg_busy(g) && !g->over && !g->help) {
    float rad = 0.8f;
    if (t->kind == TG_MOVE) {
      int ty = hb_move_type(t->move);
      if (ty == M_ROLE) rad = 1.25f;
      else if (ty == M_BUILD) rad = hb_bld[hb_move_a(t->move)].lots == 2 ? 1.55f : 1.1f;
      else if (ty == M_LOAD) rad = 2.2f;
      else if (ty == M_PLACE_PLOT || ty == M_PLACE_BLD || ty == M_SELL || ty == M_EXTRA) rad = 0.55f;
    } else rad = 0.55f;
    cw_vec2 a = project(v3(t->at.x - rad, t->y, t->at.z - rad * (rad > 1.2f ? 0.55f : 0.9f)));
    cw_vec2 b = project(v3(t->at.x + rad, t->y, t->at.z + rad * (rad > 1.2f ? 0.55f : 0.9f)));
    cw_color sc = bk_seat_color(g->turn);
    float al = 0.75f + 0.25f * sinf((float)V.ticks * 0.07f);
    cw_rect fr = { a.x, a.y, b.x - a.x, b.y - a.y };
    cw_r2d_round_rect_lines(fr, 8 * u, 5 * u, 8, cw_rgba(0, 0, 0, 0.4f * al));
    cw_r2d_round_rect_lines(fr, 8 * u, 3 * u, 8, cw_rgba(sc.r * 0.6f + 0.4f, sc.g * 0.6f + 0.4f, sc.b * 0.6f + 0.4f, al));
  }
  /* popups rise and fade */
  for (int i = 0; i < POPUPS; i++) {
    popup *pp = &V.pop[i];
    if (pp->age >= pp->life) continue;
    float k = (float)pp->age / pp->life;
    cw_vec2 at = project(v3(pp->at.x, pp->at.y + k * 1.4f, pp->at.z));
    cw_color c = cw_hex(pp->colour);
    c.a = k < 0.15f ? k / 0.15f : k > 0.7f ? (1 - k) / 0.3f : 1;
    /* on a dark pill of its own, so it reads over the boards and never smears the plate's words */
    float tw = text_w(pp->text, 28 * u) + 22 * u;
    cw_r2d_round_rect((cw_rect){ at.x - tw * 0.5f, at.y - 17 * u, tw, 36 * u }, 12 * u, 6, cw_rgba(0.06f, 0.05f, 0.09f, 0.8f * c.a));
    text(pp->text, (cw_vec2){ at.x, at.y - 15 * u }, 28 * u, c, CW_ALIGN_CENTER);
  }
  /* the top line: the round, the supplies */
  if (!g->over) {
    snprintf(line, sizeof line, "Round %d    lanterns left %d    townsfolk left %d", s->round, s->lantern_supply > 0 ? s->lantern_supply : 0, s->folk_supply);
    float w = text_w(line, 18 * u) + 30 * u;
    cw_r2d_round_rect((cw_rect){ sz.x * 0.5f - w * 0.5f, 4 * u, w, 30 * u }, 10 * u, 6, cw_rgba(0.06f, 0.05f, 0.09f, 0.7f));
    text(line, (cw_vec2){ sz.x * 0.5f, 8 * u }, 18 * u, cw_rgba(1, 0.9f, 0.72f, 1), CW_ALIGN_CENTER);
  }
  /* the prompt line */
  if (!g->over && !g->help) {
    int seat = g->turn;
    cw_color ink = cw_rgba(1, 0.96f, 0.88f, 1);
    float y = sz.y - 40 * u;
    if (!mg_busy(g)) {
      const char *what = phase_prompt(&g->shown, g->mode);
      const char *job = s->phase != PH_PICK ? hb_role_name[s->role] : "";
      if (mg_seat_is_cpu(g, seat)) snprintf(line, sizeof line, "%s%s%s is thinking: %s", job, *job ? ". " : "", mg_seat_name(seat), what);
      else if (g->mode == UI_PLACE) snprintf(line, sizeof line, "%s: %s    {left|right} {up|down} move    {choose} load    {back} another good", mg_seat_name(seat), what);
      else snprintf(line, sizeof line, "%s%s%s: %s    {left|right} {up|down} move    {choose} choose    {how_to_play} how to play", job, *job ? ". " : "",
                    mg_seat_name(seat), what);
      float w = text_w(line, 21 * u) + 40 * u;
      if (w > sz.x - 20 * u) w = sz.x - 20 * u;
      cw_r2d_round_rect((cw_rect){ sz.x * 0.5f - w * 0.5f, y - 6 * u, w, 36 * u }, 12 * u, 6, cw_rgba(0.06f, 0.05f, 0.09f, 0.78f));
      cw_prompt_text(cw_ui_font_sans(), line, mg_seat_is_cpu(g, seat) ? -1 : seat, (cw_vec2){ sz.x * 0.5f, y }, 21 * u, ink, CW_ALIGN_CENTER);
    }
  }
  draw_info(g, sz);
  if (g->help) draw_help(sz, true);
  if (g->over) draw_results(g, sz);
}

/* the title screens: the harbour at dusk with every lantern lit and the boats moored */
void view_attract(void) {
  if (!V.attract) {
    clear_match_nodes();
    hb_state s;
    hb_init(&s, 3, 1);
    V.nships = 3;
    V.players = 0;
    ly_table l;
    ly_table_make(&l, 4);
    V.lay = l;
    place_fixed(&s);
    frame_camera(&l);
    for (int k = 0; k <= HB_SHIPS; k++) V.boat_x[k] = V.boat_prev[k] = 0, V.boat_trip[k] = 0;
    for (int i = 0; i < LY_LANTERNS; i++) V.lit[i] = 1;
    V.attract = true;
  }
  V.ticks++;
  for (int k = 0; k <= HB_SHIPS; k++) {
    if (k < HB_SHIPS && k >= V.nships) continue;
    ly_point c = k == HB_SHIPS ? ly_ferry() : ly_ship(V.nships, k);
    cw_node3d_set_position(V.boat[k], v3(c.x, 0.05f + 0.05f * sinf((float)V.ticks * 0.05f + k), c.z));
  }
  for (int i = 0; i < LY_LANTERNS; i++) set_glow(V.lantern[i], 1.0f);
  for (int i = 0; i < 4; i++) cw_node_set(V.lights[i], "energy", cw_val_float(3.4f));
  set_visible(V.ghost_card_small, false);
  set_visible(V.ghost_card_grand, false);
  set_visible(V.ghost_plot, false);
  set_visible(V.ghost_pawn, false);
  show_ghost_barrels(0, NULL, 0);
}

void view_help_page(cw_vec2 canvas) { draw_help(canvas, false); }
