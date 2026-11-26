/* Rose Window: the table in 3D and the HUD over it. See view.h. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_shell.h"
#include "boardkit/bk_view.h"
#include "cartwheel/audio.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "three.h"

#define NDICE (RW_COLOURS * RW_PER_COLOUR)
#define TRACK_SCALE 0.72f

/* the glass colours, in the rules' order: ruby, cobalt, amber, verdigris, violet */
static const uint32_t GLASS_HEX[RW_COLOURS] = { 0xd0243cff, 0x2a5ce0ff, 0xf2a41eff, 0x1fae7aff, 0x8c3ad8ff };
static t3_color glass_rgb(int c) {
  cw_color k = cw_hex(GLASS_HEX[c]);
  return (t3_color){ k.r, k.g, k.b };
}
static uint32_t glass_u24(int c) { return GLASS_HEX[c] >> 8; }

/* where a die stands: in the bag, a pool slot, a pane of a seat's window, a place on the round track */
enum { LOC_BAG = 0, LOC_POOL, LOC_PANE, LOC_TRACK };
static uint32_t loc_key(int kind, int a, int b) { return (uint32_t)kind << 16 | (uint32_t)a << 8 | (uint32_t)b; }

typedef struct die_view {
  cw_node node;
  int colour, value;
  uint32_t key;
  bool visible, hide_after, lit;
  int mode;            /* 0 at rest, 1 gliding, 2 thrown */
  t3_vec3 pos, from, to;
  t3_quat rot, qfrom, qto;
  float scale, scale_from, scale_to;
  float t, dur, delay;
  bk_die_throw th;
  int material;        /* 0 glass, 1 lit, 2 hi: what the mesh shows now */
} die_view;

typedef struct win_view {
  cw_node root;
  int frame;           /* the frame built, -1 none */
  cw_node glow[RW_CELLS], spill[RW_CELLS];
  t3_material *glow_mat[RW_CELLS], *spill_mat[RW_CELLS];
  float glow_t[RW_CELLS];
  int glow_colour[RW_CELLS];
} win_view;

static struct {
  cw_tree *tree;
  cw_node world, camera, table_root;
  t3_geometry *die_geo, *quad_geo;
  t3_material *glass[RW_COLOURS], *lit[RW_COLOURS], *hi[RW_COLOURS], *ghost_mat[RW_COLOURS];
  t3_texture *pips_tex, *pips_lit_tex, *panes_tex, *spill_tex, *oak_tex;
  die_view die[NDICE];
  win_view win[LY_WINDOWS];
  cw_node ghost[RW_COLOURS]; /* the preview die of each colour, see-through by mesh opacity */
  int ghosts_shown;
  cw_node bead[BK_MAX_SEATS][8], gem[BK_MAX_SEATS];
  cw_node bell, bag, runner;
  cw_node goal_card[3], tool_card[RW_TOOLS_IN_PLAY];
  ly_table lay;
  int players;
  uint64_t shown_hash;
  float time;
  int dice_moving, lit_panes;
  bk_counter shown_favour[BK_MAX_SEATS];
  bool attract;
  cw_sound snd_clink, snd_roll, snd_chime, snd_bell;
  bool sound_ok;
  int last_turn;
  uint64_t throw_seed;
} V;

/* ── helpers ─────────────────────────────────────────────────────────── */

/* every picture here is colour (wood, cloth, glass, pips: albedo and emissive maps), said before its first load: a PNG
 * (a native build) would load as data, a cart's KTX2 says sRGB itself */
static t3_texture *texture(const char *path) {
  if (!cw_res_texture_set_srgb(path, true)) CW_FATAL("rosewindow: %s was loaded as data before it could be said to be colour", path);
  cw_res r = cw_res_load(path, CW_RES_TEXTURE);
  if (cw_res_wait(r) != CW_RES_STATE_READY) CW_FATAL("rosewindow: texture %s did not load", path);
  t3_texture *t = cw_res_texture(r);
  if (!t) CW_FATAL("rosewindow: %s is not a texture", path);
  return t;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_geometry *geo, t3_material *mat, bool cast, bool receive) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  t3_mesh *m = t3_mesh_new(geo, mat);
  m->base.cast_shadow = cast;
  m->base.receive_shadow = receive;
  if (!cw_mesh3d_set_procedural(n, m)) CW_FATAL("rosewindow: could not attach the mesh of %s", name);
  t3_release(m);
  return n;
}

static void node_material(cw_node n, t3_material *mat) {
  t3_object *o = cw_node3d_object(n);
  if (!o || !o->child_count) return;
  t3_mesh *m = (t3_mesh *)o->children[0];
  if (m->material_count < 1 || m->materials[0] == mat) return;
  t3_retain(mat);
  t3_release(m->materials[0]);
  m->materials[0] = mat;
}

static t3_material *physical(uint32_t rgb, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(rgb);
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static t3_vec3 lerp3(t3_vec3 a, t3_vec3 b, float t) { return (t3_vec3){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
static void show(cw_node n, bool on) { cw_node_set(n, "visible", cw_val_bool(on)); }

/* ── a small geometry builder: quads and boxes into one mesh ─────────── */

typedef struct gb {
  float *pos, *nrm, *uv;
  uint16_t *idx;
  int nv, ni, capv, capi;
} gb;

static void gb_grow(gb *b, int v, int i) {
  if (b->nv + v > b->capv) {
    b->capv = (b->nv + v) * 2;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->capv);
    b->nrm = realloc(b->nrm, sizeof(float) * 3 * (size_t)b->capv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->capv);
  }
  if (b->ni + i > b->capi) {
    b->capi = (b->ni + i) * 2;
    b->idx = realloc(b->idx, sizeof(uint16_t) * (size_t)b->capi);
  }
  if (b->nv + v > 65535) CW_FATAL("rosewindow: a built mesh over 65535 vertices");
}

/* a quad a b c d (counter-clockwise seen from where n points), its texture rectangle u0 v0 u1 v1 (a at u0 v0, c at u1 v1) */
static void gb_quad(gb *b, t3_vec3 a, t3_vec3 bb, t3_vec3 c, t3_vec3 d, t3_vec3 n, float u0, float v0, float u1, float v1) {
  gb_grow(b, 4, 6);
  t3_vec3 p[4] = { a, bb, c, d };
  float uv[4][2] = { { u0, v0 }, { u0, v1 }, { u1, v1 }, { u1, v0 } };
  for (int k = 0; k < 4; k++) {
    int o = b->nv + k;
    b->pos[o * 3] = p[k].x, b->pos[o * 3 + 1] = p[k].y, b->pos[o * 3 + 2] = p[k].z;
    b->nrm[o * 3] = n.x, b->nrm[o * 3 + 1] = n.y, b->nrm[o * 3 + 2] = n.z;
    b->uv[o * 2] = uv[k][0], b->uv[o * 2 + 1] = uv[k][1];
  }
  uint16_t s = (uint16_t)b->nv;
  uint16_t ix[6] = { s, (uint16_t)(s + 1), (uint16_t)(s + 2), s, (uint16_t)(s + 2), (uint16_t)(s + 3) };
  memcpy(b->idx + b->ni, ix, sizeof ix);
  b->nv += 4;
  b->ni += 6;
}

/* a box centred at c, size s, texture tiled by `tile` units */
static void gb_box(gb *b, t3_vec3 c, t3_vec3 s, float tile) {
  static const t3_vec3 N[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  for (int f = 0; f < 6; f++) {
    t3_vec3 n = N[f];
    t3_vec3 u = fabsf(n.y) > 0.5f ? (t3_vec3){ 1, 0, 0 } : (t3_vec3){ 0, 1, 0 };
    t3_vec3 v = { n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x };
    float hn = fabsf(n.x) * s.x + fabsf(n.y) * s.y + fabsf(n.z) * s.z;
    float hu = fabsf(u.x) * s.x + fabsf(u.y) * s.y + fabsf(u.z) * s.z;
    float hv = fabsf(v.x) * s.x + fabsf(v.y) * s.y + fabsf(v.z) * s.z;
    hn *= 0.5f, hu *= 0.5f, hv *= 0.5f;
    t3_vec3 o = { c.x + n.x * hn, c.y + n.y * hn, c.z + n.z * hn };
#define P(su, sv) (t3_vec3){ o.x + u.x * hu * (su) + v.x * hv * (sv), o.y + u.y * hu * (su) + v.y * hv * (sv), o.z + u.z * hu * (su) + v.z * hv * (sv) }
    /* a, b, c, d: (-u,-v) (-u,+v)... wound so (b - a) x (c - a) is along n: u x v = n, so a(-,-) b(+,-) c(+,+) d(-,+) */
    gb_quad(b, P(-1, -1), P(1, -1), P(1, 1), P(-1, 1), n, 0, 0, 2 * hu / tile, 2 * hv / tile);
#undef P
  }
}

static t3_geometry *gb_finish(gb *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nrm, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  a = t3_attribute_new(T3_UINT16, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  t3_geometry_compute_bounding_box(g);
  t3_geometry_compute_bounding_sphere(g);
  free(b->pos), free(b->nrm), free(b->uv), free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

/* a die: a cube with each face's pips from cell f - 1 of the 3 x 2 atlas. Faces as the kit's dice: +Y 1, +X 2, +Z 3,
 * -Z 4, -X 5, -Y 6 (bk_view.h) */
static const t3_vec3 FACE_N[7] = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, -1, 0 } };
static t3_geometry *die_geometry(float size) {
  gb b = { 0 };
  float h = size * 0.5f;
  for (int f = 1; f <= 6; f++) {
    t3_vec3 n = FACE_N[f];
    t3_vec3 u = fabsf(n.y) > 0.5f ? (t3_vec3){ 1, 0, 0 } : (t3_vec3){ 0, 1, 0 };
    t3_vec3 v = { n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x };
    t3_vec3 o = { n.x * h, n.y * h, n.z * h };
#define P(su, sv) (t3_vec3){ o.x + (u.x * (su) + v.x * (sv)) * h, o.y + (u.y * (su) + v.y * (sv)) * h, o.z + (u.z * (su) + v.z * (sv)) * h }
    float u0 = (float)((f - 1) % 3) / 3.0f, v0 = (float)((f - 1) / 3) / 2.0f;
    gb_quad(&b, P(-1, -1), P(1, -1), P(1, 1), P(-1, 1), n, u0, v0, u0 + 1.0f / 3.0f, v0 + 0.5f);
#undef P
  }
  return gb_finish(&b);
}

/* the rotation that brings face v up (+Y) */
static t3_quat face_up(int v) {
  switch (v) {
  case 2: return t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, 1.5707963f);
  case 5: return t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, -1.5707963f);
  case 3: return t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, -1.5707963f);
  case 4: return t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, 1.5707963f);
  case 6: return t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, 3.1415927f);
  default: return (t3_quat){ 0, 0, 0, 1 };
  }
}

/* ── sounds (synthesised: glass, a rolling rattle, a chime, a bell) ──── */

static cw_sound synth(int kind) {
  const int rate = 44100;
  int n = rate * (kind == 2 || kind == 3 ? 12 : 5) / 10;
  float *pcm = calloc((size_t)n, sizeof *pcm);
  uint32_t r = 98765u + (uint32_t)kind * 7919u;
  for (int i = 0; i < n; i++) {
    float t = (float)i / rate;
    r = r * 1664525u + 1013904223u;
    float noise = ((r >> 9) / 4194304.0f) - 1.0f;
    float v = 0;
    switch (kind) {
    case 0: /* clink: a glass die set in lead */
      v = (sinf(6.2832f * 2600 * t) * 0.5f + sinf(6.2832f * 4100 * t) * 0.3f + sinf(6.2832f * 6900 * t) * 0.15f) * expf(-t * 40) +
          noise * expf(-t * 260) * 0.3f;
      break;
    case 1: { /* roll: dice rattling on wood, a few knocks */
      float k = 0;
      for (int j = 0; j < 6; j++) {
        float at = 0.04f + 0.07f * j * (1 + 0.15f * j);
        if (t > at) k += expf(-(t - at) * 90) * (1 - 0.12f * j);
      }
      v = (noise * 0.5f + sinf(6.2832f * 1900 * t) * 0.3f) * k;
      break;
    }
    case 2: /* chime: a round ends */
      v = (sinf(6.2832f * 660 * t) * 0.5f + sinf(6.2832f * 990 * t) * 0.3f + sinf(6.2832f * 1980 * t) * 0.1f) * expf(-t * 3.5f) * fminf(1, t * 200);
      break;
    default: /* bell: a pass */
      v = (sinf(6.2832f * 1250 * t) * 0.5f + sinf(6.2832f * 2980 * t) * 0.25f + sinf(6.2832f * 4310 * t) * 0.12f) * expf(-t * 2.8f) *
          fminf(1, t * 400);
    }
    pcm[i] = v * 0.55f;
  }
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, rate, &(cw_sound_desc){ .max_voices = 8, .bus = cw_shell_sfx_bus() });
  free(pcm);
  return s;
}

static void play(cw_sound s, float vol, float pitch) {
  if (!V.sound_ok) return;
  cw_audio_play(s, &(cw_play_desc){ .volume = vol, .pitch = pitch });
}

/* ── cards: the goals and the tools, painted once ────────────────────── */

enum { FACE_GOAL = 100, FACE_TOOL = 200 };

/* ── icons: what a card does, as dice, colour swatches and a number (cards, the cards panel, the tool panel) ── */

/* a die face: a rounded square (colour < 0: ivory with dark pips, else that glass with light pips) */
static void icon_die(cw_vec2 c, float s, int value, int colour) {
  static const int8_t PIP[7][6][2] = {
    { { 0 } }, { { 0, 0 } }, { { -1, -1 }, { 1, 1 } }, { { -1, -1 }, { 0, 0 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } }, { { -1, -1 }, { 1, -1 }, { 0, 0 }, { -1, 1 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 } },
  };
  cw_color body = colour < 0 ? cw_hex(0xf4eee0ff) : cw_hex(GLASS_HEX[colour]);
  cw_color pip = colour < 0 ? cw_hex(0x2b1d14ff) : cw_hex(0xfff8e8ff);
  cw_r2d_round_rect((cw_rect){ c.x - s * 0.5f, c.y - s * 0.5f, s, s }, s * 0.18f, 6, cw_hex(0x2b1d14ff));
  cw_r2d_round_rect((cw_rect){ c.x - s * 0.44f, c.y - s * 0.44f, s * 0.88f, s * 0.88f }, s * 0.14f, 6, body);
  for (int k = 0; k < value && value <= 6; k++) cw_r2d_circle(cw_v2(c.x + PIP[value][k][0] * s * 0.24f, c.y + PIP[value][k][1] * s * 0.24f), s * 0.09f, 16, pip);
}
static void icon_swatch(cw_vec2 c, float s, int colour) {
  cw_r2d_round_rect((cw_rect){ c.x - s * 0.5f, c.y - s * 0.5f, s, s }, s * 0.12f, 6, cw_hex(0x2b1d14ff));
  cw_r2d_round_rect((cw_rect){ c.x - s * 0.42f, c.y - s * 0.42f, s * 0.84f, s * 0.84f }, s * 0.08f, 6,
                    colour < 0 ? cw_hex(0xe6dfd0ff) : cw_hex(GLASS_HEX[colour]));
}
static void icon_strike(cw_vec2 c, float s) { /* "ignoring this": a bold red diagonal */
  cw_r2d_line(cw_v2(c.x - s * 0.55f, c.y + s * 0.55f), cw_v2(c.x + s * 0.55f, c.y - s * 0.55f), s * 0.14f, cw_hex(0xb3202aff));
}

/* goal g's picture inside r */
static void icon_goal(int g, cw_rect r) {
  float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
  float s = fminf(r.w / 5.6f, r.h / 4.4f);
  switch ((rw_goal_kind)g) {
  case RW_G_ROW_COLOURS:
    for (int i = 0; i < 5; i++) icon_swatch(cw_v2(cx + (i - 2) * s * 1.08f, cy), s, i);
    break;
  case RW_G_COL_COLOURS:
    for (int i = 0; i < 4; i++) icon_swatch(cw_v2(cx, cy + (i - 1.5f) * s * 1.05f), s, i + 1);
    break;
  case RW_G_ROW_VALUES:
    for (int i = 0; i < 5; i++) icon_die(cw_v2(cx + (i - 2) * s * 1.08f, cy), s, i + 1, -1);
    break;
  case RW_G_COL_VALUES:
    for (int i = 0; i < 4; i++) icon_die(cw_v2(cx, cy + (i - 1.5f) * s * 1.05f), s, i + 2, -1);
    break;
  case RW_G_LOW:
  case RW_G_MID:
  case RW_G_HIGH: {
    int a = g == RW_G_LOW ? 1 : g == RW_G_MID ? 3 : 5;
    icon_die(cw_v2(cx - s * 0.95f, cy), s * 1.6f, a, -1);
    icon_die(cw_v2(cx + s * 0.95f, cy), s * 1.6f, a + 1, -1);
    break;
  }
  case RW_G_SCALE:
    for (int i = 0; i < 6; i++) icon_die(cw_v2(cx + (i % 3 - 1) * s * 1.25f, cy + (i / 3 - 0.5f) * s * 1.25f), s * 1.1f, i + 1, -1);
    break;
  case RW_G_PALETTE:
    for (int i = 0; i < 5; i++) {
      float a = -1.5708f + (float)i * 1.2566f;
      icon_swatch(cw_v2(cx + cosf(a) * s * 1.2f, cy + sinf(a) * s * 1.2f), s, i);
    }
    break;
  case RW_G_CHAINS:
    for (int i = 0; i < 4; i++) icon_swatch(cw_v2(cx + (i % 2 - 0.5f) * s * 1.6f, cy + (i / 2 - 0.5f) * s * 1.6f), s * 1.5f, i == 0 || i == 3 ? 0 : -1);
    break;
  }
}

/* tool t's picture inside r */
static void icon_tool(int t, cw_rect r) {
  float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
  float s = fminf(r.w / 3.4f, r.h / 2.6f);
  cw_color ink = cw_hex(0x2b1d14ff);
  switch ((rw_tool_kind)t) {
  case RW_T_NUDGE: /* a die between a minus and a plus */
    icon_die(cw_v2(cx, cy), s * 1.3f, 3, 1);
    cw_r2d_rect((cw_rect){ cx - s * 1.55f, cy - s * 0.09f, s * 0.6f, s * 0.18f }, ink);
    cw_r2d_rect((cw_rect){ cx + s * 0.95f, cy - s * 0.09f, s * 0.6f, s * 0.18f }, ink);
    cw_r2d_rect((cw_rect){ cx + s * 1.16f, cy - s * 0.3f, s * 0.18f, s * 0.6f }, ink);
    break;
  case RW_T_TURN: /* a 2 that becomes a 5 */
    icon_die(cw_v2(cx - s * 0.85f, cy), s * 1.1f, 2, 1);
    icon_die(cw_v2(cx + s * 0.85f, cy), s * 1.1f, 5, 1);
    cw_r2d_line(cw_v2(cx - s * 0.2f, cy), cw_v2(cx + s * 0.2f, cy), s * 0.12f, ink);
    break;
  case RW_T_REFIRE: /* one die rolling: a die and its blurred twin */
    icon_die(cw_v2(cx + s * 0.35f, cy - s * 0.1f), s * 1.2f, 6, 3);
    icon_die(cw_v2(cx - s * 0.55f, cy + s * 0.25f), s * 0.8f, 1, 3);
    break;
  case RW_T_KILN: /* every die rolling */
    for (int i = 0; i < 3; i++) icon_die(cw_v2(cx + (i - 1) * s * 1.05f, cy + (i == 1 ? -s * 0.35f : s * 0.2f)), s * 0.95f, i * 2 + 1, i);
    break;
  case RW_T_LONE: /* a 3 x 3 of panes with one die set apart */
    for (int i = 0; i < 9; i++) icon_swatch(cw_v2(cx + (i % 3 - 1) * s * 0.8f, cy + (i / 3 - 1) * s * 0.8f), s * 0.72f, -1);
    icon_die(cw_v2(cx + s * 0.8f, cy + s * 0.8f), s * 0.72f, 4, 2);
    icon_die(cw_v2(cx - s * 0.8f, cy - s * 0.8f), s * 0.72f, 1, 0);
    break;
  case RW_T_TINT: /* a colour mark, struck out */
    icon_swatch(cw_v2(cx, cy), s * 1.4f, 4);
    icon_strike(cw_v2(cx, cy), s * 1.3f);
    break;
  case RW_T_ETCH: /* a number mark, struck out */
    icon_die(cw_v2(cx, cy), s * 1.4f, 5, -1);
    icon_strike(cw_v2(cx, cy), s * 1.3f);
    break;
  case RW_T_TWIN: /* two dice at once */
    icon_die(cw_v2(cx - s * 0.6f, cy), s * 1.15f, 2, 0);
    icon_die(cw_v2(cx + s * 0.6f, cy), s * 1.15f, 3, 1);
    break;
  case RW_T_REDRAW: /* a die back into the bag */
    cw_r2d_circle(cw_v2(cx - s * 0.55f, cy + s * 0.2f), s * 0.8f, 32, cw_hex(0x6a2a32ff));
    cw_r2d_rect((cw_rect){ cx - s * 0.8f, cy - s * 0.75f, s * 0.5f, s * 0.3f }, cw_hex(0x6a2a32ff));
    icon_die(cw_v2(cx + s * 0.85f, cy - s * 0.25f), s * 0.9f, 4, 2);
    break;
  case RW_T_GLAZE: /* any mark at all, struck out */
    icon_swatch(cw_v2(cx - s * 0.6f, cy), s * 1.05f, 0);
    icon_die(cw_v2(cx + s * 0.6f, cy), s * 1.05f, 3, -1);
    icon_strike(cw_v2(cx, cy), s * 1.6f);
    break;
  }
}

/* what a tool does, in two or three words for its card face */
static const char *const TOOL_WORD[RW_TOOL_KINDS] = { "up or down 1", "flip a die", "reroll one", "reroll all", "set it alone",
                                                       "any colour", "any number", "two dice now", "back to bag", "any mark" };

static void paint_card(int face, cw_vec2 px, void *user) {
  (void)user;
  float w = px.x, h = px.y;
  cw_font f = cw_ui_font_sans();
  bool goal = face >= FACE_GOAL && face < FACE_TOOL;
  int k = goal ? face - FACE_GOAL : face - FACE_TOOL;
  if (face >= BK_FACE_BACK) {
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0x2a1a12ff));
    cw_r2d_rect_lines(cw_rect_make(w * 0.06f, w * 0.06f, w * 0.88f, h - w * 0.12f), w * 0.02f, cw_hex(0xc9a25aff));
    return;
  }
  /* light faces, dark ink: goals a warm cream with a gold edge, tools a cool stone with a slate edge */
  cw_color paper = goal ? cw_hex(0xf6eedbff) : cw_hex(0xe8eaecff);
  cw_color edge = goal ? cw_hex(0xa8741eff) : cw_hex(0x3c4654ff);
  cw_color ink = cw_hex(0x1e140eff);
  cw_r2d_rect(cw_rect_make(0, 0, w, h), paper);
  cw_r2d_rect_lines(cw_rect_make(w * 0.04f, w * 0.04f, w * 0.92f, h - w * 0.08f), w * 0.035f, edge);
  if (goal) icon_goal(k, (cw_rect){ w * 0.08f, h * 0.07f, w * 0.84f, h * 0.55f });
  else icon_tool(k, (cw_rect){ w * 0.1f, h * 0.07f, w * 0.8f, h * 0.52f });
  /* the bottom: a goal's points, big; a tool's effect in a few big words */
  if (goal) {
    char pts[16];
    snprintf(pts, sizeof pts, "+%d", rw_goal_infos[k].points);
    cw_text_style ps = { .size = h * 0.3f, .color = ink, .fixed_size = true, .own_backdrop = true, .wrap_width = w, .align = CW_ALIGN_CENTER };
    cw_r2d_text(f, pts, cw_v2(0, h * 0.6f), &ps);
  } else {
    cw_text_style ts = { .size = h * 0.13f, .color = ink, .fixed_size = true, .own_backdrop = true, .wrap_width = w * 0.9f, .align = CW_ALIGN_CENTER,
                         .line_spacing = 0.95f };
    cw_r2d_text(f, TOOL_WORD[k], cw_v2(w * 0.05f, h * 0.62f), &ts);
  }
}

/* ── the scene ───────────────────────────────────────────────────────── */

void view_init(cw_tree *t) {
  memset(&V, 0, sizeof V);
  V.tree = t;
  V.world = cw_node_get(cw_tree_root(t), "World");
  if (!cw_node_valid(V.world)) V.world = cw_tree_root(t);
  V.camera = cw_node_get(V.world, "Camera");
  if (!cw_node_valid(V.camera)) CW_FATAL("rosewindow: main.cwscene has no Camera");
  V.table_root = cw_node_new("Node3D", "Table");
  cw_node_add_child(V.world, V.table_root);
  V.oak_tex = texture("textures/oak.png");
  V.oak_tex->wrap_s = V.oak_tex->wrap_t = T3_REPEAT;
  V.pips_tex = texture("textures/pips.png");
  V.pips_lit_tex = texture("textures/pips_lit.png");
  V.panes_tex = texture("textures/panes.png");
  V.spill_tex = texture("textures/spill.png");

  /* the table: an oak top, thick, on a dark floor */
  t3_material *wood = physical(0xffffff, 0.6f, 0);
  t3_material_set_map(wood, V.oak_tex);
  wood->clearcoat = 0.25f;
  wood->clearcoat_roughness = 0.45f;
  gb b = { 0 };
  gb_box(&b, (t3_vec3){ 0, -0.25f, 0 }, (t3_vec3){ 34, 0.5f, 22 }, 8.0f);
  t3_geometry *tg = gb_finish(&b);
  mesh_node(V.table_root, "Oak", tg, wood, false, true);
  t3_release(tg);
  /* the runner under the pool */
  t3_texture *rt = texture("textures/runner.png");
  rt->wrap_s = rt->wrap_t = T3_REPEAT;
  t3_material *cloth = physical(0xffffff, 1.0f, 0);
  t3_material_set_map(cloth, rt);
  cloth->sheen = 0.7f;
  cloth->sheen_roughness = 0.6f;
  cloth->sheen_color = (t3_color){ 0.6f, 0.25f, 0.3f };
  gb_box(&b, (t3_vec3){ 0, 0.01f, 0 }, (t3_vec3){ 34, 0.02f, 1.7f }, 1.2f);
  t3_geometry *rg = gb_finish(&b);
  V.runner = mesh_node(V.table_root, "Runner", rg, cloth, false, true);
  t3_release(rg);
  t3_release(cloth);

  /* glass dice: clear coloured glass (transmission) with frosted pips that catch the light */
  V.die_geo = die_geometry(LY_DIE);
  for (int c = 0; c < RW_COLOURS; c++) {
    t3_color g = glass_rgb(c);
    t3_material *m = physical(glass_u24(c), 0.02f, 0);
    m->transmission = 1.0f;
    m->thickness = 0.45f;
    m->attenuation_color = g;
    m->attenuation_distance = 1.4f;
    m->roughness = 0.02f;
    m->ior = 1.5f;
    m->clearcoat = 0.6f;
    m->clearcoat_roughness = 0.05f;
    t3_material_set_texture(m, T3_EMISSIVE_MAP, V.pips_tex);
    m->emissive = (t3_color){ 1.0f, 0.97f, 0.9f };
    m->emissive_intensity = 0.6f;
    m->attenuation_distance = 0.9f;
    V.glass[c] = m;
    /* set in a window: lit from behind, the glass glows its own deep colour and the etched pips stand dark */
    t3_material *l = physical(glass_u24(c), 0.12f, 0);
    l->clearcoat = 0.8f;
    l->clearcoat_roughness = 0.05f;
    l->emissive = (t3_color){ g.r * 0.9f, g.g * 0.9f, g.b * 0.9f };
    l->emissive_intensity = 0.38f;
    t3_material_set_texture(l, T3_EMISSIVE_MAP, V.pips_lit_tex);
    V.lit[c] = l;
    t3_material *h = t3_material_clone(m); /* under the cursor: warmer, gently lit */
    h->emissive = (t3_color){ g.r * 0.6f + 0.4f, g.g * 0.6f + 0.4f, g.b * 0.6f + 0.4f };
    h->emissive_intensity = 1.2f;
    V.hi[c] = h;
    t3_material *gh = t3_mesh_standard_material_new(glass_u24(c)); /* made see-through on its node (bk_ghost_set) */
    gh->emissive = g;
    gh->emissive_intensity = 0.9f;
    t3_material_set_texture(gh, T3_EMISSIVE_MAP, V.pips_tex);
    V.ghost_mat[c] = gh;
  }
  for (int i = 0; i < NDICE; i++) {
    die_view *d = &V.die[i];
    d->colour = i / RW_PER_COLOUR;
    d->value = 1;
    char name[24];
    snprintf(name, sizeof name, "Die%d", i);
    d->node = mesh_node(V.table_root, name, V.die_geo, V.glass[d->colour], true, true);
    d->rot = (t3_quat){ 0, 0, 0, 1 };
    d->scale = 1;
    show(d->node, false);
  }
  for (int c = 0; c < RW_COLOURS; c++) {
    char name[16];
    snprintf(name, sizeof name, "Ghost%d", c);
    V.ghost[c] = mesh_node(V.table_root, name, V.die_geo, V.ghost_mat[c], false, false);
    if (bk_ghost_set(V.ghost[c], 0.5f) != 1) CW_FATAL("rosewindow: the ghost die has not its one mesh");
    show(V.ghost[c], false);
  }
  /* favour beads (clear glass) and the own-colour gems */
  t3_geometry *bg = t3_sphere_geometry_new(0.13f, 20, 12);
  t3_material *bm = physical(0xf4efe6, 0.05f, 0);
  bm->transmission = 0.7f;
  bm->thickness = 0.2f;
  bm->ior = 1.5f;
  bm->emissive = (t3_color){ 1, 0.85f, 0.6f };
  bm->emissive_intensity = 0.15f;
  for (int s = 0; s < BK_MAX_SEATS; s++)
    for (int k = 0; k < 8; k++) {
      V.bead[s][k] = mesh_node(V.table_root, "Bead", bg, bm, true, true);
      cw_node3d_set_scale(V.bead[s][k], (t3_vec3){ 1, 0.7f, 1 });
      show(V.bead[s][k], false);
    }
  t3_release(bg);
  t3_release(bm);
  t3_geometry *gg = t3_octahedron_geometry_new(0.22f, 0);
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    V.gem[s] = mesh_node(V.table_root, "Gem", gg, V.lit[0], true, true);
    cw_node3d_set_scale(V.gem[s], (t3_vec3){ 1, 0.8f, 1 });
    show(V.gem[s], false);
  }
  t3_release(gg);
  /* the bell (pass) */
  t3_material *brass = physical(0xd4a24a, 0.28f, 1.0f);
  V.bell = cw_node_new("Node3D", "Bell");
  cw_node_add_child(V.table_root, V.bell);
  t3_geometry *g1 = t3_cylinder_geometry_new(0.16f, 0.34f, 0.42f, 32, 1, false);
  cw_node n1 = mesh_node(V.bell, "Cup", g1, brass, true, true);
  cw_node3d_set_position(n1, (t3_vec3){ 0, 0.21f, 0 });
  t3_release(g1);
  t3_geometry *g2 = t3_sphere_geometry_new(0.08f, 16, 10);
  cw_node n2 = mesh_node(V.bell, "Knob", g2, brass, true, true);
  cw_node3d_set_position(n2, (t3_vec3){ 0, 0.47f, 0 });
  t3_release(g2);
  t3_geometry *g3 = t3_cylinder_geometry_new(0.42f, 0.44f, 0.05f, 32, 1, false);
  t3_material *dark = physical(0x2a1a10, 0.5f, 0);
  cw_node n3 = mesh_node(V.bell, "Base", g3, dark, true, true);
  cw_node3d_set_position(n3, (t3_vec3){ 0, 0.025f, 0 });
  t3_release(g3);
  t3_release(brass);
  /* the bag the dice come out of: a soft wool pouch */
  t3_material *wool = physical(0x8a4a50, 1.0f, 0);
  t3_material_set_map(wool, rt);
  wool->sheen = 0.8f;
  wool->sheen_color = (t3_color){ 0.7f, 0.3f, 0.35f };
  V.bag = cw_node_new("Node3D", "Bag");
  cw_node_add_child(V.table_root, V.bag);
  t3_geometry *bs = t3_sphere_geometry_new(0.55f, 28, 16);
  cw_node bb = mesh_node(V.bag, "Pouch", bs, wool, true, true);
  cw_node3d_set_position(bb, (t3_vec3){ 0, 0.42f, 0 });
  cw_node3d_set_scale(bb, (t3_vec3){ 1, 0.8f, 1 });
  t3_release(bs);
  t3_geometry *bn = t3_cylinder_geometry_new(0.18f, 0.24f, 0.3f, 20, 1, false);
  cw_node nk = mesh_node(V.bag, "Neck", bn, wool, true, true);
  cw_node3d_set_position(nk, (t3_vec3){ 0, 0.95f, 0 });
  t3_release(bn);
  t3_release(wool);
  t3_release(dark);
  t3_release(wood);
  /* the cards */
  bk_cards_init(t, paint_card, NULL);
  for (int i = 0; i < 3; i++) {
    V.goal_card[i] = bk_card_new(V.table_root, "Goal", &(bk_card_desc){ .width = LY_CARD_W, .height = LY_CARD_H, .thickness = 0.02f, .corner = 0.1f,
                                                                       .front = FACE_GOAL, .back = BK_FACE_BACK, .face_px = 384 });
    V.tool_card[i] = bk_card_new(V.table_root, "Tool", &(bk_card_desc){ .width = LY_CARD_W, .height = LY_CARD_H, .thickness = 0.02f, .corner = 0.1f,
                                                                       .front = FACE_TOOL, .back = BK_FACE_BACK, .face_px = 384 });
    show(V.goal_card[i], false);
    show(V.tool_card[i], false);
  }
  /* a unit quad in the x/z plane facing +y (glows and spills) */
  gb_quad(&b, (t3_vec3){ -0.5f, 0, -0.5f }, (t3_vec3){ -0.5f, 0, 0.5f }, (t3_vec3){ 0.5f, 0, 0.5f }, (t3_vec3){ 0.5f, 0, -0.5f }, (t3_vec3){ 0, 1, 0 }, 0, 0, 1, 1);
  V.quad_geo = gb_finish(&b);
  for (int w = 0; w < LY_WINDOWS; w++) V.win[w].frame = -1;
  V.snd_clink = synth(0);
  V.snd_roll = synth(1);
  V.snd_chime = synth(2);
  V.snd_bell = synth(3);
  V.sound_ok = true;
}

/* ── windows ─────────────────────────────────────────────────────────── */

static void free_window(int w) {
  win_view *wv = &V.win[w];
  if (cw_node_valid(wv->root)) cw_node_free(wv->root);
  for (int i = 0; i < RW_CELLS; i++) {
    if (cw_node_valid(wv->spill[i])) cw_node_free(wv->spill[i]);
    if (wv->glow_mat[i]) t3_release(wv->glow_mat[i]);
    if (wv->spill_mat[i]) t3_release(wv->spill_mat[i]);
  }
  memset(wv, 0, sizeof *wv);
  wv->frame = -1;
}

/* a pane's texture cell in the 4 x 2 atlas */
static void pane_cell(int code, float *u0, float *v0) {
  int k = rw_mark_value(code) ? rw_mark_value(code) : rw_mark_colour(code) >= 0 ? 7 : 0;
  *u0 = (float)(k % 4) / 4.0f;
  *v0 = (float)(k / 4) / 2.0f;
}

/* window w with frame f: lead came, an oak frame and easel, the panes (open panes frosted, colour marks in their
 * glass, number marks etched), each lit from behind; for a seat's window the glows and the light it throws */
static void build_window(int w, int f, bool seat) {
  free_window(w);
  win_view *wv = &V.win[w];
  wv->frame = f;
  const rw_frame *fr = &rw_frames[f];
  wv->root = cw_node_new("Node3D", "Window");
  cw_node_add_child(V.table_root, wv->root);
  cw_node3d_set_position(wv->root, V.lay.win[w]);
  cw_node3d_set_quaternion(wv->root, ly_win_rot());
  float sc = V.lay.win_scale[w];
  cw_node3d_set_scale(wv->root, (t3_vec3){ sc, sc, sc });
  float P = LY_PANE, hw = 0.5f * RW_COLS * P, hh = 0.5f * RW_ROWS * P;
  /* panes: one mesh for the open and number panes, one per colour marked */
  for (int group = -1; group < RW_COLOURS; group++) {
    gb b = { 0 };
    for (int i = 0; i < RW_CELLS; i++) {
      int code = fr->cell[i];
      if (rw_mark_colour(code) != group) continue;
      int r = i / RW_COLS, c = i % RW_COLS;
      float x0 = -hw + c * P, x1 = x0 + P, z0 = -hh + r * P, z1 = z0 + P;
      float u0, v0;
      pane_cell(code, &u0, &v0);
      gb_quad(&b, (t3_vec3){ x0, -0.02f, z0 }, (t3_vec3){ x0, -0.02f, z1 }, (t3_vec3){ x1, -0.02f, z1 }, (t3_vec3){ x1, -0.02f, z0 }, (t3_vec3){ 0, 1, 0 },
              u0, v0, u0 + 0.25f, v0 + 0.5f);
    }
    if (!b.nv) continue;
    t3_geometry *g = gb_finish(&b);
    t3_material *m = t3_mesh_standard_material_new(group < 0 ? 0xe8e2d6 : glass_u24(group));
    t3_material_set_map(m, V.panes_tex);
    t3_material_set_texture(m, T3_EMISSIVE_MAP, V.panes_tex);
    m->roughness = 0.35f;
    if (group < 0) { m->emissive = (t3_color){ 1.0f, 0.86f, 0.62f }; m->emissive_intensity = 0.16f; m->color = (t3_color){ 0.55f, 0.52f, 0.48f }; }
    else {
      t3_color gc = glass_rgb(group);
      m->emissive = gc;
      m->emissive_intensity = 0.22f;
      m->color = (t3_color){ gc.r * 0.45f, gc.g * 0.45f, gc.b * 0.45f };
    }
    mesh_node(wv->root, "Panes", g, m, false, true);
    t3_release(g);
    t3_release(m);
  }
  /* lead came: bars between panes and round the edge */
  gb b = { 0 };
  float bar = 0.07f, depth = 0.09f;
  for (int r = 0; r <= RW_ROWS; r++) gb_box(&b, (t3_vec3){ 0, 0.015f, -hh + r * P }, (t3_vec3){ 2 * hw + bar, depth, bar }, 1);
  for (int c = 0; c <= RW_COLS; c++) gb_box(&b, (t3_vec3){ -hw + c * P, 0.015f, 0 }, (t3_vec3){ bar, depth, 2 * hh + bar }, 1);
  t3_geometry *lg = gb_finish(&b);
  t3_material *lead = physical(0x4a4a4e, 0.42f, 0.85f);
  mesh_node(wv->root, "Lead", lg, lead, true, true);
  t3_release(lg);
  t3_release(lead);
  /* the oak frame and its easel strut behind */
  float bw = LY_BORDER;
  gb_box(&b, (t3_vec3){ 0, 0, -hh - bw * 0.5f }, (t3_vec3){ 2 * hw + 2 * bw, 0.16f, bw }, 2.0f);
  gb_box(&b, (t3_vec3){ 0, 0, hh + bw * 0.5f }, (t3_vec3){ 2 * hw + 2 * bw, 0.16f, bw }, 2.0f);
  gb_box(&b, (t3_vec3){ -hw - bw * 0.5f, 0, 0 }, (t3_vec3){ bw, 0.16f, 2 * hh }, 2.0f);
  gb_box(&b, (t3_vec3){ hw + bw * 0.5f, 0, 0 }, (t3_vec3){ bw, 0.16f, 2 * hh }, 2.0f);
  gb_box(&b, (t3_vec3){ 0, -0.12f, hh + bw + 0.06f }, (t3_vec3){ 2 * hw + 2 * bw, 0.1f, 0.14f }, 2.0f); /* the ledge */
  t3_geometry *fg = gb_finish(&b);
  t3_material *oak = physical(0xd8b48a, 0.5f, 0);
  t3_material_set_map(oak, V.oak_tex);
  oak->clearcoat = 0.3f;
  mesh_node(wv->root, "Frame", fg, oak, true, true);
  t3_release(fg);
  t3_release(oak);
  if (!seat) return;
  /* glows (a pane lit in its die's colour) and spills (the coloured light it throws on the table) */
  t3_vec3 light = ly_win_point(&V.lay, w, 0, -hh * 4.0f, -1.2f); /* high behind the window: its panes fall on the table in front */
  for (int i = 0; i < RW_CELLS; i++) {
    int r = i / RW_COLS, c = i % RW_COLS;
    t3_material *gm = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_map(gm, V.panes_tex);
    t3_material_set_texture(gm, T3_EMISSIVE_MAP, V.panes_tex);
    gm->emissive_intensity = 0;
    wv->glow_mat[i] = gm;
    /* the streaky glass cell, on a quad over the pane */
    gb q = { 0 };
    float x0 = -hw + c * P, x1 = x0 + P, z0 = -hh + r * P, z1 = z0 + P;
    gb_quad(&q, (t3_vec3){ x0, -0.005f, z0 }, (t3_vec3){ x0, -0.005f, z1 }, (t3_vec3){ x1, -0.005f, z1 }, (t3_vec3){ x1, -0.005f, z0 }, (t3_vec3){ 0, 1, 0 },
            0.75f, 0.5f, 1.0f, 1.0f);
    t3_geometry *qg = gb_finish(&q);
    wv->glow[i] = mesh_node(wv->root, "Glow", qg, gm, false, false);
    t3_release(qg);
    show(wv->glow[i], false);
    /* the spill: where the ray from the light through the pane meets the table */
    t3_vec3 p = ly_pane(&V.lay, w, i, 0);
    t3_vec3 d = { p.x - light.x, p.y - light.y, p.z - light.z };
    float k = d.y < -0.01f ? (0.012f - light.y) / d.y : 1.0f;
    t3_vec3 at = { light.x + d.x * k, 0.014f + 0.0005f * (float)i, light.z + d.z * k };
    t3_material *sm = t3_mesh_basic_material_new(0x000000);
    t3_material_set_map(sm, V.spill_tex);
    sm->transparent = true;
    sm->blending = T3_ADDITIVE_BLENDING;
    sm->depth_write = false;
    wv->spill_mat[i] = sm;
    wv->spill[i] = mesh_node(V.table_root, "Spill", V.quad_geo, sm, false, false);
    float stretch = k * 1.05f * V.lay.win_scale[w];
    cw_node3d_set_position(wv->spill[i], at);
    cw_node3d_set_scale(wv->spill[i], (t3_vec3){ P * stretch * 1.5f, 1, P * stretch * 1.6f });
    show(wv->spill[i], false);
  }
}

/* ── following the match ─────────────────────────────────────────────── */

typedef struct want { uint32_t key; int value; t3_vec3 p; t3_quat q; float scale; bool lit; } want;

static t3_quat pool_rot(int slot, int value) {
  float yaw = 0.35f * sinf((float)slot * 2.3f + (float)value);
  return t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, yaw), face_up(value));
}

/* every place colour c shows a die now, the bag last */
static int wanted(const rw_state *s, int c, want *out) {
  int n = 0;
  for (int p = 0; p < s->players; p++)
    for (int i = 0; i < RW_CELLS; i++) {
      uint8_t d = s->p[p].cell[i];
      if (d && rw_die_colour(d) == c)
        out[n++] = (want){ loc_key(LOC_PANE, p, i), rw_die_value(d), ly_pane(&V.lay, p, i, LY_DIE * 0.5f + 0.01f),
                           t3_quat_multiply(ly_win_rot(), face_up(rw_die_value(d))), V.lay.win_scale[p], true };
    }
  for (int i = 0; i < s->pool_n; i++) {
    uint8_t d = s->pool[i];
    if (d && rw_die_colour(d) == c) out[n++] = (want){ loc_key(LOC_POOL, i, 0), rw_die_value(d), V.lay.pool[i], pool_rot(i, rw_die_value(d)), 1, false };
  }
  int per_round[RW_ROUNDS + 1] = { 0 };
  for (int i = 0; i < s->track_n; i++) {
    uint8_t d = s->track[i];
    int r = s->track_round[i], k = per_round[r]++;
    if (rw_die_colour(d) != c) continue;
    t3_vec3 at = ly_track(&V.lay, r, k);
    at.y = LY_DIE * 0.5f * TRACK_SCALE;
    out[n++] = (want){ loc_key(LOC_TRACK, i, 0), rw_die_value(d), at, face_up(rw_die_value(d)), TRACK_SCALE, false };
  }
  for (int k = 0; k < s->bag[c]; k++) out[n++] = (want){ loc_key(LOC_BAG, 0, 0), 1, { 0 }, { 0, 0, 0, 1 }, 1, false };
  if (n != RW_PER_COLOUR) CW_FATAL("rosewindow view: colour %d shows %d dice, not %d", c, n, RW_PER_COLOUR);
  return n;
}

static float dist2(t3_vec3 a, t3_vec3 b) { float dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz); }

static t3_vec3 bag_mouth(void) { return (t3_vec3){ V.lay.bag.x, 1.05f, V.lay.bag.z }; }

static void follow(const rw_state *s) {
  int thrown = 0, moved = 0;
  for (int c = 0; c < RW_COLOURS; c++) {
    want w[RW_PER_COLOUR];
    wanted(s, c, w);
    die_view *dl[RW_PER_COLOUR];
    bool taken[RW_PER_COLOUR] = { 0 }, done[RW_PER_COLOUR] = { 0 };
    for (int i = 0; i < RW_PER_COLOUR; i++) dl[i] = &V.die[c * RW_PER_COLOUR + i];
    /* a die already where a place wants one stays (a face change there is a fresh roll or a turn) */
    for (int k = 0; k < RW_PER_COLOUR; k++) {
      if (w[k].key == loc_key(LOC_BAG, 0, 0)) continue;
      for (int i = 0; i < RW_PER_COLOUR; i++)
        if (!taken[i] && dl[i]->visible && dl[i]->key == w[k].key) {
          taken[i] = done[k] = true;
          die_view *d = dl[i];
          d->lit = w[k].lit;
          if (d->value != w[k].value) { /* rolled again or turned: a throw on the spot */
            d->value = w[k].value;
            d->th = bk_die_throw_make((t3_vec3){ d->pos.x - 0.6f, 0.9f, d->pos.z + 0.3f }, w[k].p, w[k].value, V.throw_seed + (uint64_t)i * 977u);
            d->th.seconds = 0.8f;
            d->th.height = 0.5f;
            d->mode = 2, d->t = 0, d->delay = 0.08f * (float)thrown++;
            d->to = w[k].p, d->qto = w[k].q;
          }
          break;
        }
    }
    /* the rest: each place takes the nearest die on the move, else one from the bag; the bag takes what is left */
    for (int k = 0; k < RW_PER_COLOUR; k++) {
      if (done[k]) continue;
      bool to_bag = w[k].key == loc_key(LOC_BAG, 0, 0);
      int best = -1;
      float bd = 1e9f;
      for (int i = 0; i < RW_PER_COLOUR; i++) {
        if (taken[i]) continue;
        float d = to_bag ? (dl[i]->visible ? 1000.0f : 0) : (dl[i]->visible ? dist2(dl[i]->pos, w[k].p) : 500.0f);
        if (d < bd) bd = d, best = i;
      }
      if (best < 0) CW_FATAL("rosewindow view: no die left for a place");
      taken[best] = true;
      die_view *d = dl[best];
      uint32_t was = d->key;
      bool was_visible = d->visible;
      d->key = w[k].key;
      if (to_bag) {
        if (was_visible) { /* back into the bag */
          d->from = d->pos, d->qfrom = d->rot, d->to = bag_mouth(), d->qto = d->rot;
          d->scale_from = d->scale, d->scale_to = 0.6f;
          d->mode = 1, d->t = 0, d->dur = 0.6f, d->delay = 0, d->hide_after = true;
          d->lit = false;
          moved++;
        }
        continue;
      }
      d->value = w[k].value;
      d->lit = w[k].lit;
      d->to = w[k].p, d->qto = w[k].q;
      d->scale_to = w[k].scale;
      d->hide_after = false;
      if (!was_visible || (was >> 16) == LOC_BAG) { /* out of the bag: thrown onto the table */
        d->visible = true;
        d->scale = 1;
        d->pos = bag_mouth();
        d->th = bk_die_throw_make(bag_mouth(), w[k].p, w[k].value, V.throw_seed + (uint64_t)(c * 64 + k) * 7919u);
        d->mode = 2, d->t = 0, d->delay = 0.11f * (float)thrown++;
      } else { /* lifted and set down elsewhere */
        d->from = d->pos, d->qfrom = d->rot;
        d->scale_from = d->scale;
        float dist = dist2(d->from, d->to);
        d->dur = clampf(0.35f + dist * 0.05f, 0.35f, 0.9f);
        d->mode = 1, d->t = 0, d->delay = 0.05f * (float)moved++;
      }
    }
  }
  if (thrown) play(V.snd_roll, 0.55f, 1.0f);
}

void view_start(const mg_game *g) {
  V.attract = false;
  for (int w = 0; w < LY_WINDOWS; w++) free_window(w);
  V.players = g->players;
  V.lay = g->lay;
  const rw_state *s = mg_state(g);
  for (int i = 0; i < NDICE; i++) {
    die_view *d = &V.die[i];
    d->visible = false, d->mode = 0, d->key = loc_key(LOC_BAG, 0, 0), d->scale = 1, d->lit = false;
    show(d->node, false);
  }
  for (int i = 0; i < s->ngoals && i < 3; i++) {
    bk_card_set_front(V.goal_card[i], FACE_GOAL + s->goals[i]);
    cw_node3d_set_position(V.goal_card[i], (t3_vec3){ V.lay.goal[i].x, 0.012f, V.lay.goal[i].z });
    show(V.goal_card[i], true);
  }
  for (int i = s->ngoals; i < 3; i++) show(V.goal_card[i], false);
  for (int i = 0; i < RW_TOOLS_IN_PLAY; i++) {
    bk_card_set_front(V.tool_card[i], FACE_TOOL + s->tools[i]);
    cw_node3d_set_position(V.tool_card[i], (t3_vec3){ V.lay.tool[i].x, 0.012f, V.lay.tool[i].z });
    show(V.tool_card[i], true);
  }
  cw_node3d_set_position(V.bell, V.lay.pass);
  cw_node3d_set_position(V.bag, V.lay.bag);
  show(V.bell, true);
  show(V.bag, true);
  show(V.runner, true);
  for (int p = 0; p < BK_MAX_SEATS; p++) {
    bk_counter_init(&V.shown_favour[p], 0);
    show(V.gem[p], false);
    for (int k = 0; k < 8; k++) show(V.bead[p][k], false);
  }
  /* the camera frames the table: everything inside the layout's box */
  float w = V.lay.max_x - V.lay.min_x, h = V.lay.max_z - V.lay.min_z;
  float fov = 30.0f * T3_DEG2RAD, tanv = tanf(fov * 0.5f), aspect = 16.0f / 9.0f;
  float dist = fmaxf(w * 0.5f / (tanv * aspect), h * 0.5f / tanv) * 1.16f + 1.5f; /* the near edge (the windows) stands closer than the middle */
  float tilt = 0.52f;
  t3_vec3 look = { 0, 0, 0.5f * (V.lay.min_z + V.lay.max_z) + 0.9f };
  cw_node3d_set_position(V.camera, (t3_vec3){ 0, dist * cosf(tilt), look.z + dist * sinf(tilt) });
  cw_node3d_look_at(V.camera, look, (t3_vec3){ 0, 1, 0 });
  V.shown_hash = 0;
  V.last_turn = -1;
}

static void die_pose(die_view *d, float dt) {
  if (d->mode == 0) return;
  if (d->delay > 0) { d->delay -= dt; return; }
  d->t += dt;
  if (d->mode == 2) {
    bk_die_pose(&d->th, d->t, &d->pos, &d->rot);
    if (d->t >= d->th.seconds) {
      d->pos = d->to;
      d->rot = d->qto;
      d->mode = 0;
      d->scale = d->scale_to > 0 ? d->scale_to : 1;
    }
    return;
  }
  float u = clampf(d->t / d->dur, 0, 1), e = ease(u);
  float arc = sinf(3.14159f * u) * (0.5f + 0.08f * dist2(d->from, d->to));
  d->pos = lerp3(d->from, d->to, e);
  d->pos.y += arc;
  d->rot = t3_quat_slerp(d->qfrom, d->qto, e);
  d->scale = d->scale_from + (d->scale_to - d->scale_from) * e;
  if (u >= 1) {
    d->mode = 0;
    d->pos = d->to, d->rot = d->qto;
    if (d->hide_after) { d->visible = false; d->hide_after = false; }
    else if ((d->key >> 16) == LOC_PANE) play(V.snd_clink, 0.45f, 0.9f + 0.04f * (float)d->value);
  }
}

static bool die_is_target(const mg_game *g, const die_view *d, int id) {
  int k = tg_kind(id);
  if (k == TG_DIE) return d->key == loc_key(LOC_POOL, tg_index(id), 0);
  if (k == TG_PANE && g->plen >= 1 && tg_kind(g->path[0]) == TG_TOOL) return d->key == loc_key(LOC_PANE, g->turn, tg_index(id));
  return false;
}

void view_frame(const mg_game *g, float dt) {
  V.time += dt;
  const rw_state *s = mg_state(g);
  /* windows appear as frames are chosen; the offered pair stands in the middle while someone chooses */
  for (int p = 0; p < g->players; p++)
    if (s->p[p].frame < RW_FRAMES && V.win[p].frame != s->p[p].frame) build_window(p, s->p[p].frame, true);
  for (int i = 0; i < 2; i++) {
    int want = s->phase == RW_PHASE_SETUP ? s->p[s->setup_seat].offer[i] : -1;
    if (want != V.win[LY_OFFER0 + i].frame) {
      if (want < 0) free_window(LY_OFFER0 + i);
      else build_window(LY_OFFER0 + i, want, false);
    }
  }
  uint64_t h = rw_hash(s);
  if (h != V.shown_hash) {
    V.throw_seed = h;
    follow(s);
    V.shown_hash = h;
  }
  if (g->turn != V.last_turn && V.last_turn >= 0 && s->phase == RW_PHASE_DRAFT && s->turn_idx == 0 && s->round > 1)
    play(V.snd_chime, 0.35f, 1.0f);
  V.last_turn = g->turn;
  /* the cursor's target and what the path holds lift a little */
  int cur = (!mg_busy(g) && !g->over && !g->help) ? mg_cursor_id(g) : -1;
  V.dice_moving = 0;
  for (int i = 0; i < NDICE; i++) {
    die_view *d = &V.die[i];
    die_pose(d, dt);
    if (d->mode) V.dice_moving++;
    bool vis = d->visible && !(d->mode == 2 && d->delay > 0);
    show(d->node, vis);
    if (!vis) continue;
    bool hi = cur >= 0 && die_is_target(g, d, cur);
    for (int k = 0; k < g->plen; k++) hi |= die_is_target(g, d, g->path[k]);
    t3_vec3 p = d->pos;
    if (hi && d->mode == 0) {
      if ((d->key >> 16) == LOC_PANE) {
        t3_vec3 n = t3_vec3_apply_quat((t3_vec3){ 0, 0.12f, 0 }, ly_win_rot());
        p.x += n.x, p.y += n.y, p.z += n.z;
      } else p.y += 0.18f + 0.03f * sinf(V.time * 3.0f);
    }
    cw_node3d_set_position(d->node, p);
    cw_node3d_set_quaternion(d->node, d->rot);
    cw_node3d_set_scale(d->node, (t3_vec3){ d->scale, d->scale, d->scale });
    node_material(d->node, hi ? V.hi[d->colour] : (d->lit && d->mode == 0) ? V.lit[d->colour] : V.glass[d->colour]);
  }
  /* panes light up (eased) once their die is set */
  V.lit_panes = 0;
  for (int p = 0; p < g->players; p++) {
    win_view *wv = &V.win[p];
    if (wv->frame < 0) continue;
    for (int i = 0; i < RW_CELLS; i++) {
      uint8_t d = s->p[p].cell[i];
      bool settled = d != 0;
      if (settled) {
        const die_view *dv = NULL;
        for (int k = 0; k < NDICE && !dv; k++)
          if (V.die[k].visible && V.die[k].key == loc_key(LOC_PANE, p, i)) dv = &V.die[k];
        settled = dv && dv->mode == 0;
      }
      float target = settled ? 1.0f : 0.0f;
      wv->glow_t[i] += (target - wv->glow_t[i]) * clampf(dt * 2.5f, 0, 1);
      if (d) wv->glow_colour[i] = rw_die_colour(d);
      float t = wv->glow_t[i];
      bool on = t > 0.01f;
      show(wv->glow[i], on);
      show(wv->spill[i], on);
      if (!on) continue;
      V.lit_panes += t > 0.5f;
      t3_color gc = glass_rgb(wv->glow_colour[i]);
      t3_material *gm = wv->glow_mat[i];
      gm->color = (t3_color){ gc.r * 0.3f, gc.g * 0.3f, gc.b * 0.3f };
      gm->emissive = gc;
      gm->emissive_intensity = 1.3f * t;
      gm->version++;
      t3_material *sm = wv->spill_mat[i];
      sm->color = (t3_color){ gc.r * 0.14f * t, gc.g * 0.14f * t, gc.b * 0.14f * t };
      sm->version++;
    }
  }
  /* favour beads and the own-colour gems */
  for (int p = 0; p < BK_MAX_SEATS; p++) {
    bool here = p < g->players && s->p[p].frame < RW_FRAMES;
    show(V.gem[p], here);
    if (here) {
      t3_vec3 at = ly_gem(&V.lay, p);
      cw_node3d_set_position(V.gem[p], (t3_vec3){ at.x, at.y + 0.04f * sinf(V.time * 1.3f + p), at.z });
      cw_node3d_set_rotation(V.gem[p], (t3_vec3){ 0, V.time * 0.4f, 0 });
      node_material(V.gem[p], V.lit[s->p[p].colour]);
      if (bk_counter_value(&V.shown_favour[p]) != s->p[p].favour && V.shown_favour[p].target != s->p[p].favour)
        bk_counter_set(&V.shown_favour[p], s->p[p].favour);
      bk_counter_update(&V.shown_favour[p], dt);
    }
    int nb = here ? bk_counter_value(&V.shown_favour[p]) : 0;
    for (int k = 0; k < 8; k++) {
      show(V.bead[p][k], k < nb);
      if (k < nb) cw_node3d_set_position(V.bead[p][k], ly_bead(&V.lay, p, k));
    }
  }
  /* the ghost: where the chosen die would go */
  V.ghosts_shown = 0;
  bk_move pv = mg_preview(g);
  bool ghost = false;
  int gc = 0; /* the ghost's colour */
  if (pv != BK_MOVE_NONE && !mg_seat_is_cpu(g, g->turn)) {
    int kind = rw_mv_kind(pv);
    int slot = -1, pane = -1;
    uint8_t die = 0;
    if (kind == RW_MV_PLACE) slot = rw_mv_a(pv), pane = rw_mv_b(pv);
    else if (kind == RW_MV_TOOL) {
      int tk = s->tools[rw_mv_a(pv)];
      if (tk == RW_T_LONE || tk == RW_T_GLAZE) slot = rw_mv_b(pv), pane = rw_mv_c(pv);
      if (tk == RW_T_TINT || tk == RW_T_ETCH) die = s->p[g->turn].cell[rw_mv_b(pv)], pane = rw_mv_c(pv);
      if (tk == RW_T_NUDGE) {
        uint8_t d0 = s->pool[rw_mv_b(pv)];
        int v = rw_die_value(d0) + (rw_mv_c(pv) ? 1 : -1);
        t3_vec3 at = V.lay.pool[rw_mv_b(pv)];
        gc = rw_die_colour(d0);
        cw_node3d_set_position(V.ghost[gc], (t3_vec3){ at.x + (rw_mv_c(pv) ? 0.62f : -0.62f), at.y + 0.1f, at.z + 0.7f });
        cw_node3d_set_quaternion(V.ghost[gc], face_up(v));
        cw_node3d_set_scale(V.ghost[gc], (t3_vec3){ 0.8f, 0.8f, 0.8f });
        ghost = true;
      }
      if (tk == RW_T_TURN) {
        uint8_t d0 = s->pool[rw_mv_b(pv)];
        t3_vec3 at = V.lay.pool[rw_mv_b(pv)];
        gc = rw_die_colour(d0);
        cw_node3d_set_position(V.ghost[gc], (t3_vec3){ at.x, at.y + 0.1f, at.z + 0.75f });
        cw_node3d_set_quaternion(V.ghost[gc], face_up(7 - rw_die_value(d0)));
        cw_node3d_set_scale(V.ghost[gc], (t3_vec3){ 0.8f, 0.8f, 0.8f });
        ghost = true;
      }
    }
    if (slot >= 0) die = s->pool[slot];
    if (die && pane >= 0) {
      gc = rw_die_colour(die);
      cw_node3d_set_position(V.ghost[gc], ly_pane(&V.lay, g->turn, pane, LY_DIE * 0.5f + 0.06f + 0.03f * sinf(V.time * 3)));
      cw_node3d_set_quaternion(V.ghost[gc], t3_quat_multiply(ly_win_rot(), face_up(rw_die_value(die))));
      cw_node3d_set_scale(V.ghost[gc], (t3_vec3){ V.lay.win_scale[g->turn], V.lay.win_scale[g->turn], V.lay.win_scale[g->turn] });
      ghost = true;
    }
  }
  for (int c = 0; c < RW_COLOURS; c++) show(V.ghost[c], ghost && c == gc);
  V.ghosts_shown = ghost;
  /* the cards a tool can use now sit up a little; a used-up one lies back */
  for (int t = 0; t < RW_TOOLS_IN_PLAY; t++) {
    bool ready = s->phase == RW_PHASE_DRAFT && rw_tool_ready(s, t) && !g->over;
    bool under = cur == tg_id(TG_TOOL, t) || (g->plen && g->path[0] == tg_id(TG_TOOL, t));
    float lift = under ? 0.25f : ready ? 0.05f : 0.0f;
    cw_node3d_set_position(V.tool_card[t], (t3_vec3){ V.lay.tool[t].x, 0.012f + lift, V.lay.tool[t].z + (under ? 0.15f : 0) });
  }
  cw_node3d_set_position(V.bell, (t3_vec3){ V.lay.pass.x, cur == tg_id(TG_PASS, 0) ? 0.12f : 0, V.lay.pass.z });
  show(V.bell, s->phase != RW_PHASE_SETUP);
  for (int i = 0; i < 3; i++) { /* the cards wait off the table while windows are chosen (the cards panel shows them) */
    show(V.goal_card[i], s->phase != RW_PHASE_SETUP && i < s->ngoals);
    show(V.tool_card[i], s->phase != RW_PHASE_SETUP);
  }
  show(V.bag, s->phase != RW_PHASE_SETUP);
}

int view_dice_moving(void) { return V.dice_moving; }
int view_ghosts_shown(void) { return V.ghosts_shown; }
int view_lit_panes(void) { return V.lit_panes; }

/* ── the HUD ─────────────────────────────────────────────────────────── */

cw_vec2 view_project(t3_vec3 p) {
  cw_vec2 sz = cw_r2d_canvas_size();
  return cw_camera3d_world_to_screen(V.camera, (cw_rect){ 0, 0, sz.x, sz.y }, p, NULL);
}

static void put_text(const char *s, cw_vec2 at, cw_text_style st) {
  float floor_px = cw_r2d_canvas_size().y * (22.0f / 1080.0f); /* no text under 22 px at 1080 lines (render2d.h asks 1.67%) */
  if (st.size < floor_px) st.size = floor_px;
  if (st.wrap_width <= 0 && st.align != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), s, &st);
    at.x -= st.align == CW_ALIGN_RIGHT ? m.width : m.width * 0.5f;
    st.align = CW_ALIGN_LEFT;
  }
  cw_r2d_text(cw_ui_font_sans(), s, at, &st);
}

static void text(const char *s, cw_vec2 at, float size, cw_color c, cw_text_align align) {
  put_text(s, at, (cw_text_style){ .size = size, .color = c, .align = align, .outline_width = size * 0.08f, .outline_color = cw_rgba(0.06f, 0.03f, 0.02f, 0.85f) });
}

static void card(cw_rect r) {
  /* whole pixels: an edge on a half pixel rasterises differently from host to host */
  r = (cw_rect){ roundf(r.x), roundf(r.y), roundf(r.w), roundf(r.h) };
  cw_r2d_round_rect((cw_rect){ r.x + 6, r.y + 8, r.w, r.h }, 18, 8, cw_rgba(0, 0, 0, 0.35f));
  cw_r2d_round_rect(r, 18, 8, cw_rgba(0.96f, 0.93f, 0.86f, 0.97f));
  cw_r2d_round_rect_lines(r, 18, 4, 8, cw_rgba(0.72f, 0.52f, 0.2f, 1));
}

/* the screen box of a choice */
static bool target_box(const mg_game *g, int id, cw_rect *out) {
  int k = tg_kind(id), i = tg_index(id);
  t3_vec3 c[4];
  int n = 4;
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  if (k == TG_PANE) {
    int r = i / RW_COLS, cc = i % RW_COLS;
    float hw = 0.5f * RW_COLS * LY_PANE, hh = 0.5f * RW_ROWS * LY_PANE;
    float lx0 = -hw + cc * LY_PANE, lz0 = -hh + r * LY_PANE;
    c[0] = ly_win_point(&g->lay, g->turn, lx0, lz0, 0.05f), c[1] = ly_win_point(&g->lay, g->turn, lx0 + LY_PANE, lz0, 0.05f);
    c[2] = ly_win_point(&g->lay, g->turn, lx0, lz0 + LY_PANE, 0.05f), c[3] = ly_win_point(&g->lay, g->turn, lx0 + LY_PANE, lz0 + LY_PANE, 0.05f);
  } else if (k == TG_TOOL || (k == TG_BACK && g->plen == 0)) { /* a tool card, or the one used this turn (take it back) */
    t3_vec3 p = g->lay.tool[k == TG_TOOL ? i : g->undo_slot];
    for (int j = 0; j < 4; j++) c[j] = (t3_vec3){ p.x + (j & 1 ? 0.5f : -0.5f) * LY_CARD_W, 0.05f, p.z + (j & 2 ? 0.5f : -0.5f) * LY_CARD_H };
  } else if (k == TG_FRAME) {
    for (int j = 0; j < 4; j++) c[j] = ly_win_point(&g->lay, LY_OFFER0 + i, (j & 1 ? 0.5f : -0.5f) * LY_WIN_W, (j & 2 ? 0.5f : -0.5f) * LY_WIN_H, 0);
  } else if (k == TG_PASS) {
    t3_vec3 p = g->lay.pass;
    for (int j = 0; j < 4; j++) c[j] = (t3_vec3){ p.x + (j & 1 ? 0.45f : -0.45f), j & 2 ? 0.0f : 0.6f, p.z };
  } else if (k == TG_DIE) {
    t3_vec3 p = g->lay.pool[i];
    for (int j = 0; j < 4; j++) c[j] = (t3_vec3){ p.x + (j & 1 ? 0.4f : -0.4f), j & 2 ? 0.0f : LY_DIE + 0.1f, p.z + (j & 2 ? 0.4f : -0.4f) };
  } else if (k == TG_DIR) {
    t3_vec3 p = mg_target_point(g, id);
    for (int j = 0; j < 4; j++) c[j] = (t3_vec3){ p.x + (j & 1 ? 0.3f : -0.3f), p.y, p.z + (j & 2 ? 0.3f : -0.3f) };
  } else {
    return false;
  }
  for (int j = 0; j < n; j++) {
    cw_vec2 q = view_project(c[j]);
    x0 = fminf(x0, q.x), y0 = fminf(y0, q.y), x1 = fmaxf(x1, q.x), y1 = fmaxf(y1, q.y);
  }
  *out = (cw_rect){ x0, y0, x1 - x0, y1 - y0 };
  return true;
}

/* the how-to-play card: over play with its prompt line, and as the title's page without it */
static void draw_help(cw_vec2 sz, bool prompt) {
  float u = sz.y / 720.0f;
  cw_rect r = { sz.x * 0.5f - 430 * u, sz.y * 0.5f - 290 * u, 860 * u, 580 * u };
  card(r);
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.08f, 1), gold = cw_rgba(0.6f, 0.4f, 0.1f, 1);
  put_text("How to play", (cw_vec2){ r.x + r.w * 0.5f, r.y + 24 * u }, (cw_text_style){ .size = 38 * u, .color = gold, .align = CW_ALIGN_CENTER });
  static const char *const LINES[] = {
    "Each round dice are rolled into the pool. Take turns choosing one die and setting it in your window: everyone "
    "chooses twice a round, first to last and back again. You may pass instead, by ringing the bell.",
    "Your first die goes on an edge or corner pane. Every later die must touch one already set, at a side or a corner. "
    "A pane with a colour takes only that colour, a pane with a number only that number. Dice side by side never share "
    "a colour or a number. Only the panes you can use light up.",
    "Before you set a die you may use one tool card. It costs favour, the glass beads by your window: one bead the "
    "first time anyone uses that tool, two after that.",
    "After ten rounds: the goal cards on the left score, your own colour (the gem by your window) scores the numbers "
    "on its dice, each bead left is a point, and each empty pane costs one. Playing alone, beat the round track.",
  };
  float y = r.y + 82 * u;
  for (int i = 0; i < 4; i++) {
    put_text(LINES[i], (cw_vec2){ r.x + 40 * u, y }, (cw_text_style){ .size = 19 * u, .color = ink, .wrap_width = r.w - 80 * u, .line_spacing = 1.12f });
    y += i == 1 ? 128 * u : 96 * u;
  }
  if (prompt)
    cw_prompt_text(cw_ui_font_sans(), "{choose} play    {how_to_play} this card    {cards} every card, close up", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 44 * u }, 21 * u, ink,
                 CW_ALIGN_CENTER);
}

static void draw_results(const mg_game *g, cw_vec2 sz) {
  float u = sz.y / 720.0f;
  const rw_state *s = mg_state(g);
  float rh = (float)(176 + 52 * s->players + (s->players == 1 ? 40 : 0)) * u;
  cw_rect r = { sz.x * 0.5f - 400 * u, 70 * u, 800 * u, rh }; /* above the windows, which stay in view */
  card(r);
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.08f, 1), gold = cw_rgba(0.6f, 0.4f, 0.1f, 1), soft = cw_rgba(0.42f, 0.33f, 0.25f, 1);
  unsigned w = rw_winners(s);
  char line[96];
  if (s->players == 1) {
    rw_breakdown b;
    rw_score(s, 0, &b);
    snprintf(line, sizeof line, w ? "The window shines: %d beats %d" : "Not this time: %d against %d", b.total, s->track_sum);
  } else {
    int nw = 0, last = 0;
    for (int i = 0; i < s->players; i++)
      if (w >> i & 1) nw++, last = i;
    if (nw == 1) snprintf(line, sizeof line, "%s wins", mg_seat_name(last));
    else snprintf(line, sizeof line, "A shared win");
  }
  put_text(line, (cw_vec2){ r.x + r.w * 0.5f, r.y + 22 * u }, (cw_text_style){ .size = 38 * u, .color = gold, .align = CW_ALIGN_CENTER });
  static const char *const HEAD[5] = { "goals", "colour", "favour", "empty", "total" };
  const float colx[5] = { r.x + r.w - 420 * u, r.x + r.w - 330 * u, r.x + r.w - 240 * u, r.x + r.w - 150 * u, r.x + r.w - 60 * u };
  float y = r.y + 110 * u;
  for (int k = 0; k < 5; k++) put_text(HEAD[k], (cw_vec2){ colx[k], y - 34 * u }, (cw_text_style){ .size = 17 * u, .color = soft, .align = CW_ALIGN_CENTER });
  int order[BK_MAX_SEATS];
  for (int i = 0; i < s->players; i++) order[i] = i;
  for (int i = 0; i < s->players; i++)
    for (int j = i + 1; j < s->players; j++)
      if (s->p[order[j]].score > s->p[order[i]].score) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int k = 0; k < s->players; k++) {
    int p = order[k];
    rw_breakdown b;
    rw_score(s, p, &b);
    cw_r2d_circle((cw_vec2){ r.x + 50 * u, y + 15 * u }, 10 * u, 20, bk_seat_color(p));
    snprintf(line, sizeof line, "%s%s", mg_seat_name(p), mg_seat_is_cpu(g, p) ? " (computer)" : "");
    put_text(line, (cw_vec2){ r.x + 70 * u, y }, (cw_text_style){ .size = 24 * u, .color = ink });
    int goals = b.goal[0] + b.goal[1] + b.goal[2];
    const int cell[4] = { goals, b.colour, b.favour, b.empty };
    for (int c = 0; c < 4; c++) {
      snprintf(line, sizeof line, "%d", cell[c]);
      put_text(line, (cw_vec2){ colx[c], y + 2 * u }, (cw_text_style){ .size = 22 * u, .color = ink, .align = CW_ALIGN_CENTER });
    }
    snprintf(line, sizeof line, "%d", b.total);
    put_text(line, (cw_vec2){ colx[4], y - 2 * u }, (cw_text_style){ .size = 30 * u, .color = gold, .align = CW_ALIGN_CENTER });
    y += 52 * u;
  }
  if (s->players == 1) {
    snprintf(line, sizeof line, "Round track: %d", s->track_sum);
    put_text(line, (cw_vec2){ r.x + 70 * u, y + 6 * u }, (cw_text_style){ .size = 22 * u, .color = soft });
  }
  cw_prompt_text(cw_ui_font_sans(), "{choose} play again    {back} title", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 42 * u }, 21 * u, ink, CW_ALIGN_CENTER);
}

/* the cards panel: every card on the table with its picture, name and full words, and each player's own colour and
 * window, large enough to read from across the room */
static void draw_cards(const mg_game *g, cw_vec2 sz) {
  float u = sz.y / 720.0f;
  const rw_state *s = mg_state(g);
  cw_rect r = { sz.x * 0.5f - 610 * u, 40 * u, 1220 * u, 640 * u };
  card(r);
  cw_color ink = cw_rgba(0.12f, 0.08f, 0.05f, 1), gold = cw_rgba(0.5f, 0.32f, 0.06f, 1), soft = cw_rgba(0.35f, 0.27f, 0.2f, 1);
  char line[160];
  put_text("The cards", (cw_vec2){ r.x + r.w * 0.5f, r.y + 14 * u }, (cw_text_style){ .size = 34 * u, .color = gold, .align = CW_ALIGN_CENTER });
  float x = r.x + 30 * u, y = r.y + 64 * u, row = 84 * u, tw = 560 * u;
  put_text("Shared goals: everyone scores these", (cw_vec2){ x, y }, (cw_text_style){ .size = 22 * u, .color = soft });
  y += 32 * u;
  for (int i = 0; i < s->ngoals; i++, y += row) {
    const rw_goal_info *gi = &rw_goal_infos[s->goals[i]];
    icon_goal(s->goals[i], (cw_rect){ x, y, 100 * u, 70 * u });
    snprintf(line, sizeof line, "%s   +%d", gi->name, gi->points);
    put_text(line, (cw_vec2){ x + 116 * u, y + 2 * u }, (cw_text_style){ .size = 25 * u, .color = gold, .wrap_width = tw });
    put_text(gi->text, (cw_vec2){ x + 116 * u, y + 36 * u }, (cw_text_style){ .size = 22 * u, .color = ink, .wrap_width = tw });
  }
  y += 8 * u;
  put_text("Tools: one a turn, before you set a die", (cw_vec2){ x, y }, (cw_text_style){ .size = 22 * u, .color = soft });
  y += 32 * u;
  for (int i = 0; i < RW_TOOLS_IN_PLAY; i++, y += row) {
    const rw_tool_info *ti = &rw_tool_infos[s->tools[i]];
    icon_tool(s->tools[i], (cw_rect){ x, y, 100 * u, 70 * u });
    snprintf(line, sizeof line, "%s   costs %d", ti->name, rw_tool_cost(s, i));
    put_text(line, (cw_vec2){ x + 116 * u, y + 2 * u }, (cw_text_style){ .size = 25 * u, .color = gold, .wrap_width = tw });
    put_text(ti->text, (cw_vec2){ x + 116 * u, y + 36 * u }, (cw_text_style){ .size = 22 * u, .color = ink, .wrap_width = tw });
  }
  /* the players: own colour (open to all) and window */
  float x2 = r.x + 760 * u, y2 = r.y + 64 * u;
  put_text("Own colours: the numbers on your colour's dice score", (cw_vec2){ x2, y2 }, (cw_text_style){ .size = 22 * u, .color = soft, .wrap_width = 430 * u });
  y2 += 70 * u;
  for (int p = 0; p < s->players; p++, y2 += 104 * u) {
    cw_r2d_round_rect((cw_rect){ x2, y2, 64 * u, 64 * u }, 10 * u, 6, cw_rgba(0.2f, 0.14f, 0.1f, 1));
    cw_r2d_round_rect((cw_rect){ x2 + 6 * u, y2 + 6 * u, 52 * u, 52 * u }, 8 * u, 6, cw_hex(GLASS_HEX[s->p[p].colour]));
    snprintf(line, sizeof line, "%s: %s", mg_seat_name(p), rw_colour_name[s->p[p].colour]);
    put_text(line, (cw_vec2){ x2 + 80 * u, y2 + 2 * u }, (cw_text_style){ .size = 25 * u, .color = ink });
    if (s->p[p].frame < RW_FRAMES) {
      const rw_frame *fr = &rw_frames[s->p[p].frame];
      snprintf(line, sizeof line, "%s window, difficulty %d", fr->name, fr->rating);
      put_text(line, (cw_vec2){ x2 + 80 * u, y2 + 36 * u }, (cw_text_style){ .size = 22 * u, .color = soft, .wrap_width = 350 * u });
    }
  }
  cw_prompt_text(cw_ui_font_sans(), "{cards} or {choose} close", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 40 * u }, 22 * u, ink, CW_ALIGN_CENTER);
}

/* what the seat to move is choosing, in words, with the prompts */
static void prompt_line(const mg_game *g, char *line, int cap) {
  const rw_state *s = mg_state(g);
  int s0 = g->plen ? g->path[0] : -1;
  int cur = mg_cursor_id(g);
  if (s->phase == RW_PHASE_SETUP) {
    snprintf(line, (size_t)cap, "%s: choose your window    {left|right} look    {choose} take this one    {cards} cards", mg_seat_name(g->turn));
    return;
  }
  if (g->plen == 0) {
    const char *what = "pick a die, a tool or the bell";
    if (cur >= 0 && tg_kind(cur) == TG_PASS) what = "ring the bell to pass";
    else if (cur >= 0 && tg_kind(cur) == TG_TOOL) what = "use this tool";
    else if (cur >= 0 && tg_kind(cur) == TG_BACK) what = "take back the tool";
    if (s->tool_now && bk_match_can_undo(g->match)) /* a tool used this turn: back takes it back */
      snprintf(line, (size_t)cap, "%s: %s    {left|right} {up|down} move    {choose} pick    {back} take back the tool", mg_seat_name(g->turn), what);
    else
      snprintf(line, (size_t)cap, "%s: %s    {left|right} {up|down} move    {choose} pick    {cards} cards    {how_to_play} rules", mg_seat_name(g->turn), what);
    return;
  }
  if (tg_kind(s0) == TG_DIE) {
    snprintf(line, (size_t)cap, "Set it on a lit pane    {left|right} {up|down} move    {choose} set it    {back} put it down");
    return;
  }
  int tk = s->tools[tg_index(s0)];
  const char *ask = "choose";
  if (g->plen == 1) {
    if (tk == RW_T_NUDGE || tk == RW_T_TURN || tk == RW_T_REFIRE || tk == RW_T_REDRAW || tk == RW_T_LONE || tk == RW_T_GLAZE) ask = "choose a die for the tool";
    else if (tk == RW_T_TINT || tk == RW_T_ETCH) ask = "choose one of your dice to move";
  } else {
    if (tk == RW_T_NUDGE) ask = "lower or raise it";
    else ask = "choose a pane for it";
  }
  snprintf(line, (size_t)cap, "%s: %s    {choose} pick    {back} step back", rw_tool_infos[tk].name, ask);
}

void view_hud(const mg_game *g, float dt) {
  (void)dt;
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  const rw_state *s = mg_state(g);
  char line[200];
  /* plates under each window, large: name and who plays it; the own colour goal as a swatch and its colour; favour
   * as a bead and a number */
  float px22 = sz.y * (22.0f / 1080.0f); /* the smallest text: 22 px at 1080 lines */
  for (int p = 0; p < g->players; p++) {
    cw_vec2 a = view_project(ly_win_point(&g->lay, p, -0.5f * LY_WIN_W, 0.5f * LY_WIN_H + 0.05f, 0));
    cw_vec2 b = view_project(ly_win_point(&g->lay, p, 0.5f * LY_WIN_W, 0.5f * LY_WIN_H + 0.05f, 0));
    float h = sz.y * 0.082f;
    cw_rect plate = { a.x, fminf(a.y + 4 * u, sz.y - h - 58 * u), b.x - a.x, h };
    bool turn = !g->over && p == g->turn && s->phase == RW_PHASE_DRAFT;
    cw_color sc = bk_seat_color(p);
    cw_r2d_round_rect(plate, h * 0.18f, 6, cw_rgba(0.08f, 0.05f, 0.03f, turn ? 0.92f : 0.82f));
    if (turn) cw_r2d_round_rect_lines(plate, h * 0.18f, 3 * u, 6, sc);
    float big = fmaxf(h * 0.4f, px22 * 1.25f), small = fmaxf(h * 0.3f, px22);
    cw_r2d_circle((cw_vec2){ plate.x + h * 0.26f, plate.y + h * 0.3f }, h * 0.13f, 20, sc);
    text(mg_seat_name(p), (cw_vec2){ plate.x + h * 0.48f, plate.y + h * 0.06f }, big, cw_rgba(1, 0.97f, 0.9f, 1), CW_ALIGN_LEFT);
    const char *who = mg_seat_is_cpu(g, p) ? cw_shell_cpu_level_label((cw_shell_cpu_level)g->cpu_level[p]) : "player";
    text(who, (cw_vec2){ plate.x + h * 0.18f, plate.y + h * 0.56f }, small, cw_rgba(0.95f, 0.88f, 0.76f, 1), CW_ALIGN_LEFT);
    /* the own colour goal: a swatch of its glass and the colour's name */
    /* favour on the right: a glass bead and the number, the word under them (about three ems wide); the swatch sits
     * left of centre but never reaches the favour word or bead, and shrinks on a narrow plate (four seats) */
    float fx = plate.x + plate.w - h * 0.95f;
    cw_text_style ms = { .size = small };
    float word = cw_font_measure(cw_ui_font_sans(), "favour", &ms).width;
    float who_right = plate.x + h * 0.18f + cw_font_measure(cw_ui_font_sans(), who, &ms).width + h * 0.1f;
    float fav_left = fminf(fx - h * 0.16f, fx + h * 0.2f - 0.5f * word) - h * 0.1f;
    float sw = fminf(h * 0.68f, fav_left - who_right); /* between the seat's words and the favour */
    if (sw < h * 0.4f) sw = h * 0.4f;
    float gx = fminf(plate.x + plate.w * 0.5f - h * 0.3f, fav_left - sw);
    if (gx < who_right) gx = who_right;
    float gy = plate.y + (h - sw) * 0.5f, in = sw * 0.09f;
    cw_color gc = cw_hex(GLASS_HEX[s->p[p].colour]);
    cw_r2d_round_rect((cw_rect){ gx, gy, sw, sw }, sw * 0.15f, 6, cw_rgba(0.95f, 0.9f, 0.8f, 1));
    cw_r2d_round_rect((cw_rect){ gx + in, gy + in, sw - 2 * in, sw - 2 * in }, sw * 0.12f, 6, gc);
    /* (its name is on the cards panel: the swatch is the goal here) */
    cw_r2d_circle((cw_vec2){ fx, plate.y + h * 0.32f }, h * 0.16f, 24, cw_rgba(0.98f, 0.92f, 0.78f, 1));
    cw_r2d_circle((cw_vec2){ fx - h * 0.05f, plate.y + h * 0.27f }, h * 0.055f, 16, cw_rgba(1, 1, 1, 1));
    char num[8];
    snprintf(num, sizeof num, "%d", s->p[p].favour);
    text(num, (cw_vec2){ fx + h * 0.24f, plate.y + h * 0.02f }, h * 0.5f, cw_rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_LEFT);
    text("favour", (cw_vec2){ fx + h * 0.2f, plate.y + h * 0.56f }, small, cw_rgba(0.95f, 0.88f, 0.76f, 1), CW_ALIGN_CENTER);
  }
  /* each card's name under it, large; a tool's price beside its name */
  if (s->phase != RW_PHASE_SETUP) {
    for (int k = 0; k < 6; k++) {
      bool tool = k >= 3;
      int i = k % 3;
      if (!tool && i >= s->ngoals) continue;
      t3_vec3 at = tool ? g->lay.tool[i] : g->lay.goal[i];
      cw_vec2 l = view_project((t3_vec3){ at.x - 0.5f * LY_CARD_W, 0.05f, at.z - 0.5f * LY_CARD_H });
      cw_vec2 r = view_project((t3_vec3){ at.x + 0.5f * LY_CARD_W, 0.05f, at.z - 0.5f * LY_CARD_H });
      int lines = tool ? 2 : 1; /* (a long name wraps to one more) */
      float lh = px22 * 1.2f, top = l.y - 6 * u - lh * (float)(lines + 1);
      const char *name = tool ? rw_tool_infos[s->tools[i]].name : rw_goal_infos[s->goals[i]].name;
      float wdt = r.x - l.x;
      cw_r2d_round_rect((cw_rect){ l.x, top - 4 * u, wdt, lh * (float)(lines + 1) + 6 * u }, 8 * u, 6, cw_rgba(0.08f, 0.05f, 0.03f, 0.8f));
      put_text(name, (cw_vec2){ l.x + 3 * u, top },
               (cw_text_style){ .size = px22, .color = cw_rgba(1, 0.96f, 0.86f, 1), .wrap_width = wdt - 6 * u, .align = CW_ALIGN_CENTER, .line_spacing = 0.95f });
      if (tool) {
        snprintf(line, sizeof line, "costs %d", rw_tool_cost(s, i));
        put_text(line, (cw_vec2){ l.x + 3 * u, top + lh * 2.0f },
                 (cw_text_style){ .size = px22, .color = cw_rgba(1, 0.85f, 0.5f, 1), .wrap_width = wdt - 6 * u, .align = CW_ALIGN_CENTER });
      }
    }
    cw_vec2 tq = view_project((t3_vec3){ g->lay.track0.x - 0.6f, 0.05f, g->lay.track0.z });
    text(s->players == 1 ? "round track: your target" : "round track", (cw_vec2){ tq.x, tq.y - 12 * u }, px22, cw_rgba(1, 0.93f, 0.8f, 0.95f), CW_ALIGN_RIGHT);
    if (s->players == 1) {
      snprintf(line, sizeof line, "%d", s->track_sum);
      text(line, (cw_vec2){ tq.x, tq.y + 6 * u }, px22 * 1.5f, cw_rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_RIGHT);
    }
    for (int r = 1; r <= RW_ROUNDS; r++) {
      cw_vec2 rq = view_project((t3_vec3){ g->lay.track0.x + g->lay.track_step * (r - 1), 0.02f, g->lay.track0.z - 0.5f });
      snprintf(line, sizeof line, "%d", r);
      cw_color c = r == s->round ? cw_rgba(1, 0.85f, 0.45f, 1) : cw_rgba(0.9f, 0.82f, 0.68f, 0.85f);
      text(line, (cw_vec2){ rq.x, rq.y - 22 * u }, px22, c, CW_ALIGN_CENTER);
    }
  }
  /* the choices: every one offered a soft frame in the seat's colour, the one under the cursor bold */
  bool person = !mg_seat_is_cpu(g, g->turn);
  if (!mg_busy(g) && !g->over && !g->help && !g->cards) {
    cw_color sc = bk_seat_color(g->turn);
    int cur = mg_cursor_id(g);
    for (int i = 0; i < g->ntargets; i++) {
      cw_rect rb;
      int id = g->target[i];
      if (!target_box(g, id, &rb)) continue;
      bool on = id == cur;
      if (!on && !person) continue;
      float a = on ? 0.8f + 0.2f * sinf(V.time * 3.5f) : 0.45f;
      float pad = (on ? 5 : 2) * u;
      cw_rect fr = { rb.x - pad, rb.y - pad, rb.w + 2 * pad, rb.h + 2 * pad };
      if (on) cw_r2d_round_rect_lines(fr, 8 * u, 6 * u, 8, cw_rgba(0, 0, 0, 0.4f * a));
      else if (tg_kind(id) == TG_PANE) cw_r2d_round_rect(fr, 6 * u, 6, cw_rgba(sc.r * 0.5f + 0.5f, sc.g * 0.5f + 0.5f, sc.b * 0.5f + 0.5f, 0.22f + 0.06f * sinf(V.time * 2.0f)));
      cw_r2d_round_rect_lines(fr, 8 * u, (on ? 3.5f : 2) * u, 8, cw_rgba(sc.r * 0.6f + 0.4f, sc.g * 0.6f + 0.4f, sc.b * 0.6f + 0.4f, a));
      if (tg_kind(id) == TG_DIR) text(tg_index(id) ? "raise" : "lower", (cw_vec2){ rb.x + rb.w * 0.5f, rb.y + rb.h + 2 * u }, 15 * u, CW_WHITE, CW_ALIGN_CENTER);
    }
    if (cur >= 0 && tg_kind(cur) == TG_BACK && g->plen) {
      cw_vec2 q = view_project(mg_target_point(g, cur));
      text("put back", (cw_vec2){ q.x, q.y + 22 * u }, 18 * u, CW_WHITE, CW_ALIGN_CENTER);
    }
    for (int i = 0; i < g->ntargets && person && !g->plen; i++) /* a tool used this turn: its card offers to take it back */
      if (tg_kind(g->target[i]) == TG_BACK) { /* a dark tab over the used card's words */
        t3_vec3 p = g->lay.tool[g->undo_slot];
        cw_vec2 q = view_project((t3_vec3){ p.x, 0.05f, p.z + 0.5f * LY_CARD_H });
        cw_r2d_round_rect((cw_rect){ roundf(q.x - 64 * u), roundf(q.y - 34 * u), roundf(128 * u), roundf(30 * u) }, 8 * u, 6, cw_rgba(0.1f, 0.06f, 0.04f, 0.92f));
        text("take back", (cw_vec2){ q.x, q.y - 32 * u }, 22 * u, CW_WHITE, CW_ALIGN_CENTER);
      }
    /* a tool under the cursor: its card shown large, with its picture, price and full words */
    if (cur >= 0 && tg_kind(cur) == TG_TOOL && person) {
      int t = tg_index(cur);
      const rw_tool_info *ti = &rw_tool_infos[s->tools[t]];
      cw_rect pn = { sz.x * 0.5f - 320 * u, sz.y - 222 * u, 640 * u, 160 * u }; /* above the prompts, clear of the card it explains */
      card(pn);
      icon_tool(s->tools[t], (cw_rect){ pn.x + 18 * u, pn.y + 18 * u, 160 * u, 124 * u });
      cw_color ink = cw_rgba(0.12f, 0.08f, 0.05f, 1);
      snprintf(line, sizeof line, "%s, costs %d", ti->name, rw_tool_cost(s, t));
      put_text(line, (cw_vec2){ pn.x + 196 * u, pn.y + 16 * u }, (cw_text_style){ .size = 28 * u, .color = cw_rgba(0.45f, 0.28f, 0.06f, 1), .wrap_width = 420 * u });
      put_text(ti->text, (cw_vec2){ pn.x + 196 * u, pn.y + 62 * u }, (cw_text_style){ .size = 24 * u, .color = ink, .wrap_width = 420 * u, .line_spacing = 1.05f });
    }
  }
  if (g->cards) draw_cards(g, sz);
  /* setup: each offered window's plate under it: its name, how hard it is, and the favour it starts with */
  if (s->phase == RW_PHASE_SETUP && !g->help && !g->cards) {
    for (int i = 0; i < 2; i++) {
      const rw_frame *fr = &rw_frames[s->p[s->setup_seat].offer[i]];
      cw_vec2 l = view_project(ly_win_point(&g->lay, LY_OFFER0 + i, -0.5f * LY_WIN_W, 0.5f * LY_WIN_H + 0.1f, 0));
      cw_vec2 r = view_project(ly_win_point(&g->lay, LY_OFFER0 + i, 0.5f * LY_WIN_W, 0.5f * LY_WIN_H + 0.1f, 0));
      float h = 92 * u;
      float pw = fmaxf(r.x - l.x, 280 * u);
      cw_rect pl = { 0.5f * (l.x + r.x) - 0.5f * pw, l.y + 10 * u, pw, h };
      cw_r2d_round_rect(pl, 12 * u, 6, cw_rgba(0.08f, 0.05f, 0.03f, 0.88f));
      text(fr->name, (cw_vec2){ pl.x + pl.w * 0.5f, pl.y + 6 * u }, 34 * u, cw_rgba(1, 0.96f, 0.86f, 1), CW_ALIGN_CENTER);
      snprintf(line, sizeof line, "difficulty %d", fr->rating);
      text(line, (cw_vec2){ pl.x + 18 * u, pl.y + 52 * u }, 24 * u, cw_rgba(0.95f, 0.86f, 0.7f, 1), CW_ALIGN_LEFT);
      float bx = pl.x + pl.w - 70 * u, by = pl.y + 66 * u;
      cw_r2d_circle((cw_vec2){ bx, by }, 13 * u, 24, cw_rgba(0.98f, 0.92f, 0.78f, 1));
      snprintf(line, sizeof line, "%d", fr->rating);
      text(line, (cw_vec2){ bx + 20 * u, pl.y + 48 * u }, 30 * u, cw_rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_LEFT);
    }
  }
  /* round and the order of turns, top centre; what just happened under it */
  if (!g->over && s->phase == RW_PHASE_DRAFT) {
    snprintf(line, sizeof line, "Round %d of %d", s->round, RW_ROUNDS);
    text(line, (cw_vec2){ 24 * u, 10 * u }, 24 * u, cw_rgba(0.98f, 0.9f, 0.72f, 0.95f), CW_ALIGN_LEFT);
    int n = 2 * s->players;
    float x0 = 32 * u;
    for (int k = 0; k < n; k++) {
      int seat = rw_seat_of(s, k);
      float r = k == s->turn_idx ? 7 * u : 4.5f * u;
      cw_color c = bk_seat_color(seat);
      if (k < s->turn_idx) c.a = 0.35f;
      cw_r2d_circle((cw_vec2){ x0 + k * 22 * u, 50 * u }, r, 16, c);
    }
  }
  if (g->caption[0] && g->caption_age < 300 && !g->over) {
    float a = g->caption_age < 240 ? 1.0f : (300 - g->caption_age) / 60.0f;
    text(g->caption, (cw_vec2){ sz.x * 0.5f, 10 * u }, 21 * u, cw_rgba(1, 0.95f, 0.85f, 0.9f * a), CW_ALIGN_CENTER);
  }
  /* the prompt line */
  if (!g->over && !g->help && !g->cards && !mg_busy(g)) {
    cw_color ink = cw_rgba(1, 0.96f, 0.88f, 1);
    if (mg_seat_is_cpu(g, g->turn)) {
      snprintf(line, sizeof line, "%s is choosing", mg_seat_name(g->turn));
      text(line, (cw_vec2){ sz.x * 0.5f, sz.y - 40 * u }, 22 * u, ink, CW_ALIGN_CENTER);
    } else {
      prompt_line(g, line, sizeof line);
      cw_r2d_round_rect((cw_rect){ sz.x * 0.5f - 520 * u, sz.y - 48 * u, 1040 * u, 38 * u }, 12 * u, 6, cw_rgba(0.08f, 0.05f, 0.03f, 0.7f));
      cw_prompt_text(cw_ui_font_sans(), line, g->turn, (cw_vec2){ sz.x * 0.5f, sz.y - 40 * u }, 20 * u, ink, CW_ALIGN_CENTER);
    }
  }
  if (g->help) draw_help(sz, true);
  if (g->over) draw_results(g, sz);
}

/* the title screens' table: a finished window glowing in the middle, a few dice beside it */
void view_attract(float dt) {
  V.time += dt;
  if (!V.attract) {
    for (int w = 0; w < LY_WINDOWS; w++) free_window(w);
    ly_table_make(&V.lay, 1);
    V.lay.win[0] = (t3_vec3){ 0, V.lay.win[0].y, 0.6f };
    build_window(0, 3, true);
    /* fill it: a legal-looking arrangement, colours and numbers stepping so no neighbours match */
    int used[RW_COLOURS] = { 0 };
    for (int i = 0; i < NDICE; i++) V.die[i].visible = false, show(V.die[i].node, false);
    for (int pane = 0; pane < RW_CELLS; pane++) {
      int r = pane / RW_COLS, c = pane % RW_COLS;
      int col = (c + 2 * r) % RW_COLOURS, val = (c + 3 * r) % 6 + 1;
      die_view *d = &V.die[col * RW_PER_COLOUR + used[col]++];
      d->visible = true, d->mode = 0, d->lit = true, d->value = val, d->scale = V.lay.win_scale[0];
      d->colour = col;
      d->pos = ly_pane(&V.lay, 0, pane, LY_DIE * 0.5f + 0.01f);
      d->rot = t3_quat_multiply(ly_win_rot(), face_up(val));
      d->key = loc_key(LOC_PANE, 0, pane);
      V.win[0].glow_colour[pane] = col;
      V.win[0].glow_t[pane] = 1;
    }
    for (int k = 0; k < 6; k++) {
      int col = k % RW_COLOURS;
      die_view *d = &V.die[col * RW_PER_COLOUR + used[col]++];
      d->visible = true, d->mode = 0, d->lit = false, d->value = k + 1, d->scale = 1;
      d->pos = (t3_vec3){ -6.0f + 0.9f * k + (k > 2 ? 7.4f : 0), LY_DIE * 0.5f, -1.4f + 0.3f * (k % 2) };
      d->rot = pool_rot(k, k + 1);
    }
    for (int i = 0; i < 3; i++) show(V.goal_card[i], false), show(V.tool_card[i], false);
    show(V.bell, false);
    show(V.bag, false);
    for (int p = 0; p < BK_MAX_SEATS; p++) {
      show(V.gem[p], false);
      for (int k = 0; k < 8; k++) show(V.bead[p][k], false);
    }
    float fov = 30.0f * T3_DEG2RAD, tanv = tanf(fov * 0.5f), w = 15.0f, h = 9.0f;
    float dist = fmaxf(w * 0.5f / (tanv * 16.0f / 9.0f), h * 0.5f / tanv) + 1.0f, tilt = 0.62f;
    cw_node3d_set_position(V.camera, (t3_vec3){ 0, dist * cosf(tilt), 0.4f + dist * sinf(tilt) });
    cw_node3d_look_at(V.camera, (t3_vec3){ 0, 0.6f, 0.4f }, (t3_vec3){ 0, 1, 0 });
    V.attract = true;
  }
  for (int i = 0; i < NDICE; i++) {
    die_view *d = &V.die[i];
    show(d->node, d->visible);
    if (!d->visible) continue;
    cw_node3d_set_position(d->node, d->pos);
    cw_node3d_set_quaternion(d->node, d->rot);
    cw_node3d_set_scale(d->node, (t3_vec3){ d->scale, d->scale, d->scale });
    node_material(d->node, d->lit ? V.lit[d->colour] : V.glass[d->colour]);
  }
  win_view *wv = &V.win[0];
  for (int i = 0; i < RW_CELLS; i++) {
    float t = 0.85f + 0.15f * sinf(V.time * 0.8f + (float)i * 0.7f); /* a slow shimmer, never a flash */
    show(wv->glow[i], true);
    show(wv->spill[i], true);
    t3_color gc = glass_rgb(wv->glow_colour[i]);
    wv->glow_mat[i]->color = gc;
    wv->glow_mat[i]->emissive = gc;
    wv->glow_mat[i]->emissive_intensity = 2.2f * t;
    wv->glow_mat[i]->version++;
    wv->spill_mat[i]->color = (t3_color){ gc.r * 0.3f * t, gc.g * 0.3f * t, gc.b * 0.3f * t };
    wv->spill_mat[i]->version++;
  }
  for (int c = 0; c < RW_COLOURS; c++) show(V.ghost[c], false);
}

void view_help_page(cw_vec2 canvas) { draw_help(canvas, false); }
