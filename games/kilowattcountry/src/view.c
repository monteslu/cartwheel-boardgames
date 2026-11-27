/* Kilowatt Country: the table in 3D and the HUD over it. See view.h. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_view.h"
#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "layout.h"
#include "three.h"

#define STORE_SHOW 12 /* fuel pieces shown per company per fuel */
#define FLYERS 16

static const uint32_t SEAT_HEX[KC_MAX_P] = { 0xe09a2eu, 0x3f78d8u, 0xd85a78u, 0x3fae7au };
static const uint32_t SEAT_RGBA[KC_MAX_P] = { 0xf4b04cffu, 0x6a9cf0ffu, 0xf07c98ffu, 0x5cd29affu };
/* a plant's card colour by kind, and each fuel's piece colour */
static const uint32_t KIND_HEX[6] = { 0x5c4d44u, 0x2d5664u, 0x9a8228u, 0x3c7d3cu, 0x4c4f58u, 0x3d84b0u };
static const uint32_t FUEL_HEX[KC_NFUEL] = { 0x7a685cu, 0x2a7a94u, 0xd8b23cu, 0x5cd85cu };
static const uint32_t FUEL_UI[KC_NFUEL] = { 0x9a8a80ffu, 0x5cb4ccffu, 0xe8c850ffu, 0x7cf07cffu };
static const char *const FUEL_TITLE[KC_NFUEL] = { "Coal", "Oil", "Garbage", "Uranium" };

typedef struct glide {
  t3_vec3 pos, from, to;
  float t, dur, arc;
  bool moving, placed;
} glide;

typedef struct flyer {
  bool live;
  int seat, fuel;
  glide g;
} flyer;

int view_stage;
static struct {
  cw_tree *tree;
  cw_node world, camera, root;
  int players;
  /* the map */
  cw_node overlay[KC_NREGION];
  int map_shown, nlink_built; /* the map the board shows (kc_maps index), the links built for it */
  t3_texture *map_tex[KC_NMAPS], *region_tex[KC_NMAPS][KC_NREGION];
  t3_material *paint_mat, *overlay_mat[KC_NREGION];
  cw_node link[KC_MAX_LINK];
  t3_material *link_mat, *link_hot;
  bool link_lit[KC_MAX_LINK];
  cw_node city[KC_NCITY], plinth[KC_NCITY], glow[KC_NCITY];
  t3_material *glow_mat[KC_NCITY];
  cw_node room[KC_NCITY][3];
  cw_node house[KC_NCITY][3];
  t3_material *house_mat[KC_NCITY][3], *window_mat[KC_NCITY][3];
  int house_owner[KC_NCITY][3];
  float house_drop[KC_NCITY][3]; /* seconds since it was built (< 0: settled) */
  float lit[KC_NCITY][3], lit_to[KC_NCITY][3], lit_wait[KC_NCITY][3];
  cw_node ghost;
  /* plants */
  cw_node card[KC_NPLANT];
  glide card_g[KC_NPLANT];
  bool card_gone[KC_NPLANT];
  cw_node deck_top, deck_slab;
  /* fuel */
  t3_geometry *fuel_geo[KC_NFUEL];
  t3_material *fuel_mat[KC_NFUEL];
  cw_node cube[KC_NFUEL][24];
  cw_node store[KC_MAX_P][KC_NFUEL][STORE_SHOW];
  cw_node fly[FLYERS];
  flyer fl[FLYERS];
  /* trays */
  cw_node tray[KC_MAX_P];
  /* the cursor */
  cw_node marker;
  float marker_bob;
  /* HUD */
  bk_counter money[KC_MAX_P];
  bk_deltas deltas;
  float delta_t[KC_MAX_P]; /* when each company's last money change showed */
  int delta_k[KC_MAX_P];
  int beat_seen;
  char banner[160];
  float banner_t;
  float payday_t;
  kc_state payday; /* the state the payday panel reports */
  int moving, lit_now;
  float time;
  cw_sound snd_tock, snd_thump, snd_chime, snd_clink;
  bool sound_ok;
} V;

/* ── helpers ─────────────────────────────────────────────────────────── */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 lerp3(t3_vec3 a, t3_vec3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static void show(cw_node n, bool on) { cw_node_set(n, "visible", cw_val_bool(on)); }
static cw_color rgba(uint32_t h) { return cw_hex(h); }

/* a flat plane's uv v runs from the near edge (0) to the far edge (1) once laid on the table; texture rows run
 * from the image's top, which the art draws as the far side: flip v so the picture lies as drawn */
static void flip_v(t3_geometry *g) {
  t3_attribute *uv = g->attributes[T3_ATTR_UV];
  float *f = (float *)uv->array;
  for (int i = 0; i < uv->count; i++) f[i * 2 + 1] = 1.0f - f[i * 2 + 1];
}

static t3_texture *texture(const char *path) {
  /* a PNG loads as data unless the game says it is a colour picture before the first load */
  if (!cw_res_texture_set_srgb(path, true)) CW_FATAL("kilowattcountry: %s was loaded as data before it could be marked as colour", path);
  cw_res r = cw_res_load(path, CW_RES_TEXTURE);
  if (cw_res_wait(r) != CW_RES_STATE_READY) CW_FATAL("kilowattcountry: texture %s did not load", path);
  t3_texture *t = cw_res_texture(r);
  if (!t) CW_FATAL("kilowattcountry: %s is not a texture", path);
  if (t->color_space != T3_SRGB_COLOR_SPACE) CW_FATAL("kilowattcountry: %s did not load as a colour picture", path);
  return t;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_geometry *geo, t3_material *mat, bool cast, bool receive) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  t3_mesh *m = t3_mesh_new(geo, mat);
  m->base.cast_shadow = cast;
  m->base.receive_shadow = receive;
  if (!cw_mesh3d_set_procedural(n, m)) CW_FATAL("kilowattcountry: could not attach the mesh of %s", name);
  t3_release(m);
  return n;
}
static cw_node group(cw_node parent, const char *name) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  return n;
}
static t3_material *matte(uint32_t rgb, float rough) {
  t3_material *m = t3_mesh_physical_material_new(rgb);
  m->roughness = rough;
  m->metalness = 0;
  return m;
}
static t3_color tcol(uint32_t rgb) {
  cw_color c = cw_hex((rgb << 8) | 0xffu);
  return (t3_color){ c.r, c.g, c.b };
}

static void glide_to(glide *g, t3_vec3 to, float dur, float arc, float delay) {
  if (!g->placed) {
    g->pos = g->from = g->to = to;
    g->placed = true;
    g->moving = false;
    return;
  }
  float d = fabsf(to.x - g->to.x) + fabsf(to.y - g->to.y) + fabsf(to.z - g->to.z);
  if (d < 1e-4f) return;
  g->from = g->pos;
  g->to = to;
  g->t = -delay;
  g->dur = dur;
  g->arc = arc;
  g->moving = true;
}
static bool glide_step(glide *g, float dt) {
  if (!g->moving) return false;
  g->t += dt;
  float u = clampf(g->t / g->dur, 0, 1);
  g->pos = lerp3(g->from, g->to, ease(u));
  g->pos.y += g->arc * sinf(u * 3.14159f);
  if (u >= 1) {
    g->moving = false;
    g->pos = g->to;
    return true;
  }
  V.moving++;
  return false;
}

/* ── sounds (synthesised) ────────────────────────────────────────────── */

static cw_sound synth(int kind) {
  const int rate = 44100;
  int n = rate * (kind == 2 ? 12 : 4) / 10;
  float *pcm = calloc((size_t)n, sizeof *pcm);
  uint32_t r = 77711u + (uint32_t)kind * 7919u;
  for (int i = 0; i < n; i++) {
    float t = (float)i / rate;
    r = r * 1664525u + 1013904223u;
    float noise = ((r >> 9) / 4194304.0f) - 1.0f, v;
    switch (kind) {
    case 0: v = sinf(6.2832f * 420 * t) * expf(-t * 40) * 0.6f + noise * expf(-t * 160) * 0.2f; break; /* tock: a card or a bid */
    case 1: v = sinf(6.2832f * (150 - 70 * t) * t) * expf(-t * 20) * 0.8f + noise * expf(-t * 80) * 0.25f; break; /* thump: a house lands */
    case 2: /* chime: the grid hums up, soft attack, slow fade */
      v = (sinf(6.2832f * 392 * t) * 0.45f + sinf(6.2832f * 588 * t) * 0.3f + sinf(6.2832f * 784 * t) * 0.15f) * expf(-t * 2.6f) * fminf(1, t * 30);
      break;
    default: v = (sinf(6.2832f * 1900 * t) * 0.4f + sinf(6.2832f * 2700 * t) * 0.3f) * expf(-t * 45) + noise * expf(-t * 200) * 0.2f; /* clink: fuel */
    }
    pcm[i] = v * 0.5f;
  }
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, rate, &(cw_sound_desc){ .max_voices = 6, .bus = cw_shell_sfx_bus() });
  free(pcm);
  return s;
}
static void sfx(cw_sound s, float vol, float pitch) {
  if (V.sound_ok) cw_audio_play(s, &(cw_play_desc){ .volume = vol, .pitch = pitch });
}

/* ── the card painter: faces drawn once into textures ────────────────── */

static cw_font font(void) { return cw_ui_font_sans(); }
static void ptext(const char *s, float x, float y, float size, cw_color c, int align) { /* align: 0 left, 1 centre, 2 right */
  cw_text_style st = { .size = size, .color = c, .fixed_size = true, .own_backdrop = true };
  cw_text_metrics m = cw_font_measure(font(), s, &st);
  cw_r2d_text(font(), s, cw_v2(x - (align == 1 ? m.width * 0.5f : align == 2 ? m.width : 0), y), &st);
}
static void house_icon(float cx, float cy, float s, cw_color c) {
  cw_r2d_rect(cw_rect_make(cx - s * 0.5f, cy - s * 0.1f, s, s * 0.6f), c);
  cw_vec2 roof[3] = { { cx - s * 0.62f, cy - s * 0.08f }, { cx, cy - s * 0.62f }, { cx + s * 0.62f, cy - s * 0.08f } };
  cw_r2d_polygon(roof, 3, c);
}
static void fuel_icon(int fuel, float cx, float cy, float s) {
  cw_color c = rgba(FUEL_UI[fuel]), dark = cw_rgba(0, 0, 0, 0.45f);
  switch (fuel) {
  case KC_COAL: cw_r2d_rect(cw_rect_make(cx - s / 2, cy - s / 2, s, s), c); cw_r2d_rect_lines(cw_rect_make(cx - s / 2, cy - s / 2, s, s), s * 0.08f, dark); break;
  case KC_OIL:
    cw_r2d_round_rect(cw_rect_make(cx - s * 0.4f, cy - s / 2, s * 0.8f, s), s * 0.2f, 6, c);
    cw_r2d_rect(cw_rect_make(cx - s * 0.4f, cy - s * 0.08f, s * 0.8f, s * 0.16f), dark);
    break;
  case KC_GARBAGE: {
    cw_vec2 bin[4] = { { cx - s * 0.5f, cy - s * 0.45f }, { cx + s * 0.5f, cy - s * 0.45f }, { cx + s * 0.38f, cy + s * 0.5f }, { cx - s * 0.38f, cy + s * 0.5f } };
    cw_r2d_polygon(bin, 4, c);
    cw_r2d_rect(cw_rect_make(cx - s * 0.56f, cy - s * 0.55f, s * 1.12f, s * 0.14f), dark);
    break;
  }
  default:
    cw_r2d_round_rect(cw_rect_make(cx - s * 0.22f, cy - s * 0.55f, s * 0.44f, s * 1.1f), s * 0.2f, 6, c);
    cw_r2d_circle(cw_v2(cx, cy), s * 0.12f, 12, dark);
  }
}
static void sun_icon(float cx, float cy, float s) {
  cw_color c = rgba(0xffe07aff);
  for (int i = 0; i < 8; i++) {
    float a = (float)i * 0.785f;
    cw_r2d_line(cw_v2(cx + cosf(a) * s * 0.45f, cy + sinf(a) * s * 0.45f), cw_v2(cx + cosf(a) * s * 0.72f, cy + sinf(a) * s * 0.72f), s * 0.1f, c);
  }
  cw_r2d_circle(cw_v2(cx, cy), s * 0.34f, 24, c);
}

static void paint(int face, cw_vec2 px, void *user) {
  (void)user;
  float w = px.x, h = px.y;
  if (face >= BK_FACE_BACK) { /* the back: night blue with a bolt */
    cw_r2d_rect(cw_rect_make(0, 0, w, h), rgba(0x101a30ff));
    for (int i = 0; i < 12; i++) cw_r2d_line(cw_v2(0, (float)i * h / 11), cw_v2(w, (float)i * h / 11 + h * 0.08f), 2, rgba(0x18284aff));
    cw_r2d_rect_lines(cw_rect_make(w * 0.06f, w * 0.06f, w * 0.88f, h - w * 0.12f), w * 0.025f, rgba(0xe0a040ff));
    cw_vec2 bolt[6] = { { w * 0.58f, h * 0.18f }, { w * 0.3f, h * 0.55f }, { w * 0.5f, h * 0.55f },
                        { w * 0.4f, h * 0.84f }, { w * 0.72f, h * 0.44f }, { w * 0.52f, h * 0.44f } };
    cw_r2d_polygon(bolt, 3, rgba(0xf2b84aff));
    cw_vec2 b2[3] = { bolt[0], bolt[2], bolt[5] };
    cw_r2d_polygon(b2, 3, rgba(0xf2b84aff));
    cw_vec2 b3[3] = { bolt[3], bolt[4], bolt[5] };
    cw_r2d_polygon(b3, 3, rgba(0xf2b84aff));
    cw_vec2 b4[3] = { bolt[2], bolt[3], bolt[5] };
    cw_r2d_polygon(b4, 3, rgba(0xf2b84aff));
    return;
  }
  const kc_plant_def *d = &kc_plants[face];
  cw_color body = rgba((KIND_HEX[d->kind] << 8) | 0xffu);
  cw_r2d_rect(cw_rect_make(0, 0, w, h), rgba(0xeee6d4ff));
  if (d->kind == KC_K_HYBRID) {
    cw_r2d_rect(cw_rect_make(w * 0.05f, w * 0.05f, w * 0.45f, h - w * 0.1f), rgba((KIND_HEX[KC_K_COAL] << 8) | 0xffu));
    cw_r2d_rect(cw_rect_make(w * 0.5f, w * 0.05f, w * 0.45f, h - w * 0.1f), rgba((KIND_HEX[KC_K_OIL] << 8) | 0xffu));
  } else {
    cw_r2d_rect(cw_rect_make(w * 0.05f, w * 0.05f, w * 0.9f, h - w * 0.1f), body);
  }
  /* icon first, three big things (the focus panel says it in words): the lowest bid, the fuel, the cities */
  char num[8];
  snprintf(num, sizeof num, "%d", d->number);
  cw_r2d_round_rect(cw_rect_make(w * 0.1f, w * 0.1f, w * 0.8f, h * 0.3f), w * 0.08f, 8, rgba(0xf6eedcff));
  ptext(num, w * 0.5f, w * 0.1f + h * 0.005f, h * 0.27f, rgba(0x14100cff), 1);
  float iy = h * 0.56f, s = w * 0.27f; /* the fuel icons as large as three fit across: read at the table camera */
  if (d->kind == KC_K_GREEN) {
    sun_icon(w * 0.5f, iy, w * 0.46f);
  } else {
    int fuel = d->kind == KC_K_HYBRID ? -1 : d->kind;
    for (int i = 0; i < d->need; i++) {
      float x = w * 0.5f + ((float)i - (float)(d->need - 1) * 0.5f) * s * 1.18f;
      cw_r2d_circle(cw_v2(x, iy), s * 0.6f, 24, cw_rgba(0.04f, 0.03f, 0.02f, 0.55f)); /* a dark disc: the icon stands off the card */
      if (fuel >= 0) fuel_icon(fuel, x, iy, s);
      else { fuel_icon(KC_COAL, x - s * 0.22f, iy, s * 0.7f); fuel_icon(KC_OIL, x + s * 0.22f, iy, s * 0.7f); }
    }
  }
  /* the cities it lights: a house and a big number on a light band */
  cw_r2d_round_rect(cw_rect_make(w * 0.1f, h * 0.71f, w * 0.8f, h * 0.22f), w * 0.08f, 8, rgba(0xf6eedcff));
  house_icon(w * 0.32f, h * 0.83f, w * 0.2f, rgba(0x14100cff));
  char cn[8];
  snprintf(cn, sizeof cn, "%d", d->cities);
  ptext(cn, w * 0.66f, h * 0.705f, h * 0.2f, rgba(0x14100cff), 1);
}

/* ── pieces ──────────────────────────────────────────────────────────── */

/* a house: a box, a pyramid roof and a lit window on the front */
static cw_node make_house(cw_node parent, const char *name, t3_material *body, t3_material *window) {
  static t3_geometry *walls, *roof, *win;
  static t3_material *roof_m;
  if (!walls) {
    walls = t3_box_geometry_new(0.24f, 0.18f, 0.21f, 1, 1, 1);
    roof = t3_cone_geometry_new(0.21f, 0.14f, 4, 1, false);
    win = t3_box_geometry_new(0.15f, 0.07f, 0.01f, 1, 1, 1);
    roof_m = matte(0x2a2422, 0.8f);
  }
  cw_node g = group(parent, name);
  cw_node3d_set_scale(g, v3(LY_CITY_S, LY_CITY_S, LY_CITY_S));
  cw_node w = mesh_node(g, "Walls", walls, body, true, true);
  cw_node3d_set_position(w, v3(0, 0.09f, 0));
  cw_node r = mesh_node(g, "Roof", roof, window ? body : roof_m, true, true); /* the roof in the company's colour: the camera sees it most */
  cw_node3d_set_position(r, v3(0, 0.25f, 0));
  cw_node3d_set_rotation(r, v3(0, 0.785f, 0));
  if (window) {
    cw_node wn = mesh_node(g, "Window", win, window, false, false);
    cw_node3d_set_position(wn, v3(0, 0.1f, 0.107f));
  }
  return g;
}

static t3_geometry *link_geo(int i) {
  t3_vec3 a = ly_city(kc_links[i].a), b = ly_city(kc_links[i].b);
  float len = hypotf(b.x - a.x, b.z - a.z) - 0.5f;
  return t3_box_geometry_new(len > 0.05f ? len : 0.05f, 0.012f, 0.05f, 1, 1, 1);
}

/* the cables between the map's cities: one per link of the map in use (rebuilt when the map changes) */
static void build_links(void) {
  for (int i = 0; i < V.nlink_built; i++) cw_node_free(V.link[i]);
  for (int i = 0; i < KC_NLINK; i++) {
    t3_vec3 a = ly_city(kc_links[i].a), b = ly_city(kc_links[i].b);
    t3_geometry *g = link_geo(i);
    V.link[i] = group(V.root, "Link");
    cw_node3d_set_position(V.link[i], v3((a.x + b.x) * 0.5f, LY_BOARD_Y + 0.006f, (a.z + b.z) * 0.5f));
    cw_node3d_set_rotation(V.link[i], v3(0, -atan2f(b.z - a.z, b.x - a.x), 0));
    mesh_node(V.link[i], "Cable", g, V.link_mat, false, true);
    cw_node n1 = mesh_node(V.link[i], "CableLit", g, V.link_hot, false, false);
    show(n1, false);
    V.link_lit[i] = false;
    t3_release(g);
  }
  V.nlink_built = KC_NLINK;
}

/* the board shows the map in use: its night picture and region masks, its cables, the cities on their spots */
static void apply_map(void) {
  if (V.map_shown == kc_map_active) return;
  V.map_shown = kc_map_active;
  t3_material_set_map(V.paint_mat, V.map_tex[V.map_shown]);
  V.paint_mat->version++;
  for (int r = 0; r < KC_NREGION; r++) {
    t3_material_set_map(V.overlay_mat[r], V.region_tex[V.map_shown][r]);
    V.overlay_mat[r]->version++;
  }
  build_links();
  for (int c = 0; c < KC_NCITY; c++) {
    t3_vec3 p = ly_city(c);
    cw_node3d_set_position(V.plinth[c], v3(p.x, p.y + 0.025f, p.z));
    for (int k = 0; k < 3; k++) {
      t3_vec3 r = ly_room(c, k);
      cw_node3d_set_position(V.room[c][k], v3(r.x, p.y + 0.055f, r.z));
    }
    cw_node3d_set_position(V.glow[c], v3(p.x, p.y + 0.03f, p.z));
  }
}

/* what is in play on the board: the regions dealt (the rest under their dark masks), their cities and links */
static void show_regions(const kc_state *s) {
  for (int r = 0; r < KC_NREGION; r++) show(V.overlay[r], !kc_region_in(s, r));
  for (int i = 0; i < KC_NLINK; i++) {
    bool in = kc_region_in(s, kc_cities[kc_links[i].a].region) && kc_region_in(s, kc_cities[kc_links[i].b].region);
    show(V.link[i], in);
  }
  for (int c = 0; c < KC_NCITY; c++) show(V.city[c], kc_region_in(s, kc_cities[c].region));
}

cw_node view_camera(void) { return V.camera; }

void view_init(cw_tree *t) {
  memset(&V, 0, sizeof V);
  V.tree = t;
  V.world = cw_node_get(cw_tree_root(t), "World");
  if (!cw_node_valid(V.world)) V.world = cw_tree_root(t);
  V.camera = cw_node_get(V.world, "Camera");
  if (!cw_node_valid(V.camera)) CW_FATAL("kilowattcountry: main.cwscene has no Camera");
  V.root = group(V.world, "Table");
  /* the table */
  t3_texture *tt = texture("textures/table.png");
  tt->wrap_s = tt->wrap_t = T3_REPEAT;
  t3_material *wood = matte(0xffffff, 0.62f);
  t3_material_set_map(wood, tt);
  t3_geometry *tg = t3_plane_geometry_new(80, 50, 1, 1);
  {
    float *f = (float *)tg->attributes[T3_ATTR_UV]->array;
    for (int i = 0; i < tg->attributes[T3_ATTR_UV]->count * 2; i++) f[i] *= 8;
  }
  cw_node table = mesh_node(V.root, "TableTop", tg, wood, false, true);
  cw_node3d_set_rotation(table, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(table, v3(0, 0, 0));
  t3_release(tg);
  t3_release(wood);
  /* the map board: a slab with a brass-coloured rim, the night map on top */
  float cx = LY_MAP_X0 + LY_MAP_W * 0.5f, cz = LY_MAP_Z0 + LY_MAP_D * 0.5f;
  t3_material *rim = matte(0x1a1612, 0.7f);
  t3_geometry *bg = t3_box_geometry_new(LY_MAP_W + 0.3f, LY_BOARD_Y - 0.01f, LY_MAP_D + 0.3f, 1, 1, 1);
  cw_node slab = mesh_node(V.root, "BoardSlab", bg, rim, true, true);
  cw_node3d_set_position(slab, v3(cx, (LY_BOARD_Y - 0.01f) * 0.5f, cz));
  t3_release(bg);
  t3_release(rim);
  /* every map's night picture and region masks load now: picking a map swaps textures, never waits on a load */
  for (int m = 0; m < KC_NMAPS; m++) {
    char path[64];
    snprintf(path, sizeof path, "textures/map-%s.png", kc_maps[m].key);
    V.map_tex[m] = texture(path);
    for (int r = 0; r < KC_NREGION; r++) {
      snprintf(path, sizeof path, "textures/region-%s-%d.png", kc_maps[m].key, r);
      V.region_tex[m][r] = texture(path);
    }
  }
  V.map_shown = kc_map_active;
  t3_material *paint_m = V.paint_mat = matte(0xffffff, 0.9f);
  t3_material_set_map(paint_m, V.map_tex[V.map_shown]);
  t3_geometry *pg = t3_plane_geometry_new(LY_MAP_W, LY_MAP_D, 1, 1);
  flip_v(pg);
  cw_node face = mesh_node(V.root, "MapFace", pg, paint_m, false, true);
  cw_node3d_set_rotation(face, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(face, v3(cx, LY_BOARD_Y, cz));
  /* a region out of play: its mask laid dark over it */
  for (int r = 0; r < KC_NREGION; r++) {
    t3_material *m = V.overlay_mat[r] = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(m, V.region_tex[V.map_shown][r]);
    m->transparent = true;
    m->depth_write = false;
    V.overlay[r] = mesh_node(V.root, "OutOfPlay", pg, m, false, false);
    cw_node3d_set_rotation(V.overlay[r], v3(-1.5708f, 0, 0));
    cw_node3d_set_position(V.overlay[r], v3(cx, LY_BOARD_Y + 0.004f, cz));
    show(V.overlay[r], false);
  }
  t3_release(pg);
  /* links: thin cables between cities */
  V.link_mat = t3_mesh_standard_material_new(0x30405a);
  V.link_mat->emissive = (t3_color){ 0.12f, 0.2f, 0.34f };
  V.link_mat->emissive_intensity = 0.9f;
  V.link_hot = t3_mesh_standard_material_new(0x806020);
  V.link_hot->emissive = (t3_color){ 1.0f, 0.62f, 0.22f };
  V.link_hot->emissive_intensity = 1.6f;
  build_links();
  /* cities: a dark plinth with three rooms, a glow beneath for the lights */
  t3_geometry *plinth = t3_cylinder_geometry_new(0.36f * LY_CITY_S, 0.4f * LY_CITY_S, 0.05f, 28, 1, false);
  t3_geometry *roomg = t3_cylinder_geometry_new(0.1f * LY_CITY_S, 0.1f * LY_CITY_S, 0.012f, 16, 1, false);
  t3_geometry *glowg = t3_plane_geometry_new(1.6f * LY_CITY_S, 1.6f * LY_CITY_S, 1, 1);
  t3_material *plinth_m = matte(0x3a4250, 0.6f), *room_m = t3_mesh_standard_material_new(0x404850);
  room_m->emissive = (t3_color){ 0.3f, 0.36f, 0.44f };
  room_m->emissive_intensity = 0.35f;
  t3_texture *glow_tex = texture("textures/glow.png");
  for (int c = 0; c < KC_NCITY; c++) {
    t3_vec3 p = ly_city(c);
    V.city[c] = group(V.root, "City");
    cw_node pl = V.plinth[c] = mesh_node(V.city[c], "Plinth", plinth, plinth_m, true, true);
    cw_node3d_set_position(pl, v3(p.x, p.y + 0.025f, p.z));
    for (int k = 0; k < 3; k++) {
      t3_vec3 r = ly_room(c, k);
      V.room[c][k] = mesh_node(V.city[c], "Room", roomg, room_m, false, true);
      cw_node3d_set_position(V.room[c][k], v3(r.x, p.y + 0.055f, r.z));
      V.house_mat[c][k] = matte(0xffffff, 0.7f);
      V.window_mat[c][k] = t3_mesh_standard_material_new(0x201810);
      V.window_mat[c][k]->emissive = (t3_color){ 1.0f, 0.72f, 0.38f };
      V.window_mat[c][k]->emissive_intensity = 0;
      char nm[24];
      snprintf(nm, sizeof nm, "House%d_%d", c, k);
      V.house[c][k] = make_house(V.root, nm, V.house_mat[c][k], V.window_mat[c][k]);
      show(V.house[c][k], false);
      V.house_owner[c][k] = -1;
    }
    t3_material *gm = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(gm, glow_tex);
    gm->color = (t3_color){ 1.0f, 0.62f, 0.3f };
    gm->transparent = true;
    gm->blending = T3_ADDITIVE_BLENDING;
    gm->depth_write = false;
    gm->opacity = 0;
    V.glow_mat[c] = gm;
    V.glow[c] = mesh_node(V.root, "Glow", glowg, gm, false, false);
    cw_node3d_set_rotation(V.glow[c], v3(-1.5708f, 0, 0));
    cw_node3d_set_position(V.glow[c], v3(p.x, p.y + 0.03f, p.z));
    show(V.glow[c], false);
  }
  t3_release(plinth);
  t3_release(roomg);
  t3_release(glowg);
  t3_release(plinth_m);
  t3_release(room_m);
  /* the ghost house: where a build would go */
  t3_material *gb = matte(0xfff4d8, 0.7f); /* a warm white: the see-through house where a build would go */
  V.ghost = make_house(V.root, "GhostHouse", gb, NULL);
  t3_release(gb);
  if (!bk_ghost_set(V.ghost, 0.5f)) CW_FATAL("kilowattcountry: the ghost house has no mesh");
  show(V.ghost, false);
  /* plant cards */
  bk_cards_init(V.tree, paint, NULL);
  for (int p = 0; p < KC_NPLANT; p++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Plant%d", p);
    V.card[p] = bk_card_new(V.root, nm, &(bk_card_desc){ .width = LY_CARD_W, .height = LY_CARD_D, .thickness = 0.02f, .corner = 0.07f,
                                                          .front = p, .back = BK_FACE_BACK, .face_px = 320 });
    show(V.card[p], false);
  }
  V.deck_top = bk_card_new(V.root, "DeckTop", &(bk_card_desc){ .width = LY_CARD_W, .height = LY_CARD_D, .thickness = 0.02f, .corner = 0.07f,
                                                                .front = BK_FACE_BACK, .back = BK_FACE_BACK, .face_px = 320 });
  t3_material *deck_m = matte(0x18223a, 0.8f);
  t3_geometry *dg = t3_box_geometry_new(LY_CARD_W - 0.04f, 1.0f, LY_CARD_D - 0.04f, 1, 1, 1);
  V.deck_slab = mesh_node(V.root, "DeckSlab", dg, deck_m, true, true);
  t3_release(dg);
  t3_release(deck_m);
  /* fuel: coal lumps, oil barrels, garbage bins, uranium rods */
  V.fuel_geo[KC_COAL] = t3_dodecahedron_geometry_new(0.12f, 0);
  V.fuel_geo[KC_OIL] = t3_cylinder_geometry_new(0.08f, 0.08f, 0.2f, 14, 1, false);
  V.fuel_geo[KC_GARBAGE] = t3_cylinder_geometry_new(0.1f, 0.08f, 0.17f, 4, 1, false);
  V.fuel_geo[KC_URANIUM] = t3_cylinder_geometry_new(0.055f, 0.055f, 0.24f, 12, 1, false);
  for (int f = 0; f < KC_NFUEL; f++) V.fuel_mat[f] = matte(FUEL_HEX[f], f == KC_OIL ? 0.45f : 0.8f);
  V.fuel_mat[KC_URANIUM]->emissive = (t3_color){ 0.25f, 0.95f, 0.3f };
  V.fuel_mat[KC_URANIUM]->emissive_intensity = 0.8f;
  /* the fuel market's tracks: a dark wooden strip per fuel with a cell per price */
  t3_material *strip_m = matte(0x2a1e16, 0.75f), *cell_m = matte(0x3a2c22, 0.85f);
  for (int f = 0; f < KC_NFUEL; f++) {
    t3_geometry *sg = t3_box_geometry_new(LY_FUEL_W + 0.2f, 0.04f, 1.0f, 1, 1, 1);
    cw_node sn = mesh_node(V.root, "FuelTrack", sg, strip_m, true, true);
    cw_node3d_set_position(sn, v3(LY_FUEL_X0 + LY_FUEL_W * 0.5f, 0.02f, ly_fuel_row_z(f)));
    t3_release(sg);
    int cells = f == KC_URANIUM ? 12 : 8;
    float cw_ = LY_FUEL_W / (float)cells;
    t3_geometry *cg = t3_box_geometry_new(cw_ - 0.06f, 0.01f, 0.86f, 1, 1, 1);
    for (int k = 0; k < cells; k++) {
      t3_vec3 at = ly_fuel_cell(f, k);
      cw_node cn = mesh_node(V.root, "Cell", cg, cell_m, false, true);
      cw_node3d_set_position(cn, v3(at.x, 0.045f, at.z));
    }
    t3_release(cg);
    for (int j = 0; j < kc_fuel_slots[f]; j++) {
      V.cube[f][j] = mesh_node(V.root, "Fuel", V.fuel_geo[f], V.fuel_mat[f], true, true);
      t3_vec3 p = ly_fuel_slot(f, j);
      cw_node3d_set_position(V.cube[f][j], v3(p.x, p.y + 0.08f, p.z));
      if (f == KC_URANIUM) cw_node3d_set_rotation(V.cube[f][j], v3(0, 0, 0));
      else cw_node3d_set_rotation(V.cube[f][j], v3(0, 0.25f * (float)(j % 3) - 0.25f, 0));
    }
  }
  t3_release(strip_m);
  t3_release(cell_m);
  for (int i = 0; i < FLYERS; i++) {
    V.fly[i] = mesh_node(V.root, "Flyer", V.fuel_geo[0], V.fuel_mat[0], true, true);
    show(V.fly[i], false);
  }
  /* the cursor marker: a hovering ring */
  t3_material *mk = t3_mesh_standard_material_new(0xffe8a0);
  mk->emissive = (t3_color){ 1.0f, 0.85f, 0.45f };
  mk->emissive_intensity = 1.1f;
  t3_geometry *tor = t3_torus_geometry_new(0.5f, 0.04f, 10, 40, 6.2832f);
  V.marker = mesh_node(V.root, "Marker", tor, mk, false, false);
  cw_node3d_set_rotation(V.marker, v3(1.5708f, 0, 0));
  show(V.marker, false);
  t3_release(tor);
  t3_release(mk);
  V.snd_tock = synth(0);
  V.snd_thump = synth(1);
  V.snd_chime = synth(2);
  V.snd_clink = synth(3);
  V.sound_ok = true;
  bk_deltas_init(&V.deltas);
}

static void free_seat_nodes(void) {
  for (int s = 0; s < KC_MAX_P; s++) {
    if (cw_node_valid(V.tray[s])) cw_node_free(V.tray[s]);
    for (int f = 0; f < KC_NFUEL; f++)
      for (int k = 0; k < STORE_SHOW; k++)
        if (cw_node_valid(V.store[s][f][k])) cw_node_free(V.store[s][f][k]);
  }
  memset(V.tray, 0, sizeof V.tray);
  memset(V.store, 0, sizeof V.store);
}

void view_start(const kg_game *g) {
  free_seat_nodes();
  V.players = g->players;
  int n = g->players;
  const kc_state *s = &g->shown;
  t3_material *felt = matte(0x14181c, 1.0f);
  t3_geometry *tray_g = t3_box_geometry_new(LY_TRAY_W, 0.04f, LY_TRAY_D, 1, 1, 1);
  t3_geometry *band_g = t3_box_geometry_new(LY_TRAY_W - 0.2f, 0.05f, 0.12f, 1, 1, 1);
  for (int i = 0; i < n; i++) {
    V.tray[i] = group(V.root, "Tray");
    t3_vec3 at = ly_tray(i, n);
    cw_node tn = mesh_node(V.tray[i], "Felt", tray_g, felt, true, true);
    cw_node3d_set_position(tn, v3(at.x, 0.02f, at.z));
    t3_material *bm = matte(SEAT_HEX[i], 0.6f);
    cw_node bn = mesh_node(V.tray[i], "Band", band_g, bm, false, true);
    cw_node3d_set_position(bn, v3(at.x, 0.03f, at.z - LY_TRAY_D * 0.5f + 0.1f));
    t3_release(bm);
    for (int f = 0; f < KC_NFUEL; f++)
      for (int k = 0; k < STORE_SHOW; k++) {
        V.store[i][f][k] = mesh_node(V.root, "Stored", V.fuel_geo[f], V.fuel_mat[f], true, true);
        show(V.store[i][f][k], false);
      }
    bk_counter_init(&V.money[i], s->p[i].money);
  }
  t3_release(felt);
  t3_release(tray_g);
  t3_release(band_g);
  apply_map();
  show_regions(s);
  for (int c = 0; c < KC_NCITY; c++) {
    for (int k = 0; k < 3; k++) {
      V.house_owner[c][k] = -1;
      V.lit[c][k] = V.lit_to[c][k] = 0;
      V.lit_wait[c][k] = 0;
      V.house_drop[c][k] = -1;
      show(V.house[c][k], false);
    }
    show(V.glow[c], false);
  }
  for (int p = 0; p < KC_NPLANT; p++) V.card_g[p] = (glide){ 0 }, V.card_gone[p] = false, show(V.card[p], false);
  for (int i = 0; i < FLYERS; i++) V.fl[i].live = false, show(V.fly[i], false);
  V.beat_seen = g->beat.serial;
  V.banner[0] = 0;
  V.payday_t = 99;
  bk_deltas_init(&V.deltas);
  V.deltas.rise = 0; /* the badges hold still on their panel's edge and fade */
  for (int i = 0; i < KC_MAX_P; i++) V.delta_t[i] = -99, V.delta_k[i] = 0;
  cw_node3d_set_position(V.camera, v3(0, 21.4f, 13.9f));
  cw_node3d_look_at(V.camera, v3(0, 0, -0.75f), v3(0, 1, 0));
}

/* ── following the table ─────────────────────────────────────────────── */

static cw_vec2 to_screen(t3_vec3 p) {
  cw_vec2 sz = cw_r2d_canvas_size();
  return cw_camera3d_world_to_screen(V.camera, (cw_rect){ 0, 0, sz.x, sz.y }, p, NULL);
}
/* a company's panel along the top of the HUD (canvas units) */
static cw_rect hud_panel(int seat, int players, cw_vec2 sz) {
  float u = sz.y / 720.0f, w = 290 * u, h = 62 * u, gap = 12 * u, x0 = sz.x * 0.5f - (players * w + (players - 1) * gap) * 0.5f;
  x0 = 2 * floorf(x0 * 0.5f);
  return (cw_rect){ x0 + seat * (w + gap), 8 * u, w, h };
}
/* where a company's money change shows: a badge on its panel's lower right corner, under the money; a second change
 * while the first still shows sits beside it, never on top */
static cw_vec2 panel_point(int seat) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  cw_rect r = hud_panel(seat, V.players, sz);
  int k = V.time - V.delta_t[seat] < 1.0f ? V.delta_k[seat] + 1 : 0;
  V.delta_t[seat] = V.time, V.delta_k[seat] = k % 3;
  return cw_v2(r.x + r.w - 26 * u - (float)(k % 3) * 48 * u, r.y + r.h - 1 * u);
}

/* which of a company's cities its lights reach: the first `powered` of them in map order */
static bool city_lit(const kc_state *s, int seat, int city) {
  int k = 0;
  for (int c = 0; c < KC_NCITY; c++)
    if (kc_has_house(s, seat, c)) {
      if (c == city) return k < s->p[seat].powered;
      k++;
    }
  return false;
}

static int flyer_new(void) {
  for (int i = 0; i < FLYERS; i++)
    if (!V.fl[i].live) return i;
  return -1;
}

/* where company seat's stored fuel pieces sit: per fuel, a spot on the plant card that stores it */
static int store_spots(const kc_state *s, int seat, int fuel, t3_vec3 *out, int cap) {
  const kc_player *p = &s->p[seat];
  int caps[3] = { 0 }, used[3] = { 0 }, n = 0;
  for (int k = 0; k < 3; k++)
    if (p->plants[k] >= 0) caps[k] = 2 * kc_plants[p->plants[k]].need;
  /* fill in a fixed order: coal and oil plants, then the coal-or-oil plants, then garbage and uranium */
  for (int f = 0; f < KC_NFUEL; f++) {
    int left = p->fuel[f];
    for (int pass = 0; pass < 2 && left > 0; pass++)
      for (int k = 0; k < 3 && left > 0; k++) {
        if (p->plants[k] < 0) continue;
        int kind = kc_plants[p->plants[k]].kind;
        bool fits = pass == 0 ? kind == f : (kind == KC_K_HYBRID && (f == KC_COAL || f == KC_OIL));
        while (fits && left > 0 && used[k] < caps[k]) {
          if (f == fuel && n < cap) out[n++] = ly_card_fuel(ly_tray_card(seat, k, s->players), used[k]);
          used[k]++, left--;
        }
      }
  }
  return n;
}

static void on_beat(const kg_game *g) {
  const kg_beat *b = &g->beat;
  const kc_state *a = &b->before, *z = &b->after;
  int kind = bk_move_field(b->move, 0);
  for (int s = 0; s < g->players; s++) {
    int dm = z->p[s].money - a->p[s].money;
    if (dm) bk_deltas_add(&V.deltas, panel_point(s), dm, dm > 0 ? rgba(0xffe080ff) : rgba(0xff9a80ff));
  }
  if (b->seat == BK_CHANCE) sfx(V.snd_tock, 0.4f, 1.25f);
  switch (kind) {
  case KC_M_OPEN: case KC_M_RAISE: sfx(V.snd_tock, 0.55f, 1.0f + 0.03f * (float)(z->auc_bid % 7)); break;
  case KC_M_FUEL: {
    int seat = a->cur, f = bk_move_field(b->move, 1), k = bk_move_field(b->move, 2), m0 = a->fuel_mkt[f], S = kc_fuel_slots[f];
    t3_vec3 spots[24];
    int ns = store_spots(z, seat, f, spots, 24);
    for (int i = 0; i < k; i++) {
      int fi = flyer_new();
      if (fi < 0) break;
      flyer *fl = &V.fl[fi];
      fl->live = true, fl->seat = seat, fl->fuel = f;
      fl->g = (glide){ 0 };
      t3_vec3 from = ly_fuel_slot(f, S - m0 + i);
      from.y += 0.08f;
      glide_to(&fl->g, from, 0, 0, 0);
      int si = ns - k + i;
      t3_vec3 to = si >= 0 && si < ns ? spots[si] : ly_tray_card(seat, 0, g->players);
      glide_to(&fl->g, to, g->fast ? 0.35f : 0.7f, 1.4f, (float)i * 0.08f);
      t3_mesh *fm = t3_mesh_new(V.fuel_geo[f], V.fuel_mat[f]);
      fm->base.cast_shadow = true;
      if (!cw_mesh3d_set_procedural(V.fly[fi], fm)) CW_FATAL("kilowattcountry: a flying fuel piece lost its mesh");
      t3_release(fm);
    }
    sfx(V.snd_clink, 0.5f, 1.0f);
    break;
  }
  case KC_M_BUILD: {
    int c = bk_move_field(b->move, 1);
    for (int k = 0; k < 3; k++)
      if (a->occ[c][k] < 0 && z->occ[c][k] >= 0) V.house_drop[c][k] = 0;
    break;
  }
  default: break;
  }
  if (z->bought != a->bought) sfx(V.snd_thump, 0.5f, 1.4f);
  if (z->step != a->step && z->phase != KC_PH_OVER) {
    snprintf(V.banner, sizeof V.banner, z->step == 2 ? "Step 2: a city now has room for two companies; the lowest plant leaves"
                                                     : "Step 3: the deck ran out, six plants for sale, three companies per city");
    V.banner_t = 0;
  }
  /* power: the cities light one by one, company by company in turn order */
  if (z->round != a->round || (z->phase == KC_PH_OVER && a->phase != KC_PH_OVER)) {
    V.payday = *z;
    V.payday_t = 0;
    float t = 0.3f, step = g->fast ? 0.07f : 0.22f;
    for (int oi = 0; oi < g->players; oi++) {
      int seat = a->order[oi];
      for (int c = 0; c < KC_NCITY; c++)
        for (int k = 0; k < 3; k++) {
          if (z->occ[c][k] != seat) continue;
          bool on = city_lit(z, seat, c);
          if (on && V.lit_to[c][k] < 1) V.lit_wait[c][k] = t, t += step;
          else if (!on) V.lit_wait[c][k] = 0.1f;
          V.lit_to[c][k] = on ? 1.0f : 0.0f;
        }
    }
    sfx(V.snd_chime, 0.45f, 1.0f);
  }
  if (z->phase == KC_PH_OVER && a->phase != KC_PH_OVER) {
    snprintf(V.banner, sizeof V.banner, "The last round: the most cities lit wins");
    V.banner_t = 0;
  }
}

int view_moving(void) { return V.moving; }
int view_lit(void) { return V.lit_now; }

/* the cheapest route's links from a company's cities to a city (for the build preview) */
static void route_links(const kc_state *s, int seat, int city, bool *on) {
  memset(on, 0, sizeof(bool) * KC_NLINK);
  if (s->p[seat].ncity == 0) return;
  int dist[KC_NCITY], via[KC_NCITY];
  bool done[KC_NCITY] = { false };
  for (int c = 0; c < KC_NCITY; c++) dist[c] = kc_has_house(s, seat, c) ? 0 : 1 << 20, via[c] = -1;
  for (int it = 0; it < KC_NCITY; it++) {
    int u = -1;
    for (int c = 0; c < KC_NCITY; c++)
      if (!done[c] && kc_region_in(s, kc_cities[c].region) && (u < 0 || dist[c] < dist[u])) u = c;
    if (u < 0 || dist[u] >= 1 << 20) break;
    done[u] = true;
    for (int l = 0; l < KC_NLINK; l++) {
      int a = kc_links[l].a, b = kc_links[l].b, w = a == u ? b : b == u ? a : -1;
      if (w < 0 || !kc_region_in(s, kc_cities[w].region)) continue;
      if (dist[u] + kc_links[l].cost < dist[w]) dist[w] = dist[u] + kc_links[l].cost, via[w] = l;
    }
  }
  for (int c = city, guard = 0; via[c] >= 0 && guard < KC_NCITY; guard++) {
    int l = via[c];
    on[l] = true;
    c = kc_links[l].a == c ? kc_links[l].b : kc_links[l].a;
  }
}

void view_update(const kg_game *g, float dt) {
  V.time += dt;
  V.moving = 0;
  const kc_state *s = &g->shown;
  if (g->setup) { /* the setup card: the board shows the map picked, dealt as it would be; nothing else moves */
    apply_map();
    show_regions(s);
    show(V.marker, false);
    show(V.ghost, false);
    show(V.deck_top, false); /* (the deck is placed by play; it waits off the table) */
    show(V.deck_slab, false);
    return;
  }
  apply_map();
  if (!g->match) return;
  int n = g->players;
  if (g->beat.serial != V.beat_seen) {
    V.beat_seen = g->beat.serial;
    on_beat(g);
  }
  float speed = g->fast ? 0.3f : 0.65f;
  /* plant cards */
  int aside = 0;
  for (int p = 0; p < KC_NPLANT; p++) {
    int where = s->loc[p];
    bool vis = true;
    float scale = 1.0f;
    t3_vec3 to = ly_deck();
    if (where == KC_L_MARKET) {
      int slot = 0;
      for (int i = 0; i < s->nmarket; i++)
        if (s->market[i] == p) slot = i;
      to = ly_market(slot);
      to.y = LY_BOARD_Y - 0.13f;
      if (s->auc_plant == p) to.y += 0.45f; /* up for auction: lifted (its bob is added after the glide) */
    } else if (where == KC_L_OWNED) {
      int seat = -1, k = 0;
      for (int i = 0; i < n; i++)
        for (int j = 0; j < 3; j++)
          if (s->p[i].plants[j] == p) seat = i, k = j;
      if (seat < 0 && s->disc_new == p) seat = s->cur, k = 3;
      to = k < 3 ? ly_tray_card(seat, k, n) : v3(ly_tray_x(seat, n), 1.2f, LY_TRAY_Z - 1.9f);
    } else if (where == KC_L_ASIDE) {
      to = ly_aside(aside++);
      scale = LY_ASIDE_SCALE;
    } else if (where == KC_L_OUT) { /* out of the game: off the table */
      vis = false;
    } else { /* in the deck or under it: hidden at the deck */
      vis = false;
      V.card_g[p] = (glide){ .pos = to, .from = to, .to = to, .placed = true };
    }
    if (where != KC_L_OUT) {
      V.card_gone[p] = false;
      glide_to(&V.card_g[p], to, speed, where == KC_L_OWNED ? 1.2f : 0.5f, 0);
    }
    glide_step(&V.card_g[p], dt);
    show(V.card[p], vis);
    if (vis) {
      t3_vec3 at = V.card_g[p].pos;
      if (where == KC_L_MARKET && s->auc_plant == p && !V.card_g[p].moving) at.y += 0.04f * sinf(V.time * 3);
      cw_node3d_set_position(V.card[p], at);
      cw_node3d_set_scale(V.card[p], v3(scale, 1, scale));
    }
  }
  int deck = kc_deck_count(s) + (s->step3_drawn ? 0 : 1);
  show(V.deck_top, deck > 0);
  show(V.deck_slab, deck > 1);
  float dh = 0.02f * (float)deck;
  t3_vec3 dp = ly_deck();
  cw_node3d_set_position(V.deck_slab, v3(dp.x, dh * 0.5f, dp.z));
  cw_node3d_set_scale(V.deck_slab, v3(1, fmaxf(0.01f, dh), 1));
  cw_node3d_set_position(V.deck_top, v3(dp.x, dh + 0.012f, dp.z));
  /* the fuel market */
  for (int f = 0; f < KC_NFUEL; f++) {
    int S = kc_fuel_slots[f], m = s->fuel_mkt[f];
    for (int j = 0; j < S; j++) show(V.cube[f][j], j >= S - m);
  }
  /* flyers, and the stored pieces (less those still in the air) */
  int flying[KC_MAX_P][KC_NFUEL] = { { 0 } };
  for (int i = 0; i < FLYERS; i++) {
    flyer *fl = &V.fl[i];
    if (!fl->live) { show(V.fly[i], false); continue; }
    if (glide_step(&fl->g, dt)) {
      fl->live = false;
      show(V.fly[i], false);
      sfx(V.snd_clink, 0.3f, 1.2f);
      continue;
    }
    flying[fl->seat][fl->fuel]++;
    show(V.fly[i], fl->g.t >= 0);
    cw_node3d_set_position(V.fly[i], fl->g.pos);
  }
  for (int seat = 0; seat < n; seat++)
    for (int f = 0; f < KC_NFUEL; f++) {
      t3_vec3 spots[STORE_SHOW];
      int ns = store_spots(s, seat, f, spots, STORE_SHOW) - flying[seat][f];
      for (int k = 0; k < STORE_SHOW; k++) {
        show(V.store[seat][f][k], k < ns);
        if (k < ns) cw_node3d_set_position(V.store[seat][f][k], spots[k]);
      }
    }
  /* houses: drop in when built, glow when powered */
  V.lit_now = 0;
  for (int c = 0; c < KC_NCITY; c++) {
    float glow = 0;
    for (int k = 0; k < 3; k++) {
      int o = s->occ[c][k];
      if (o != V.house_owner[c][k]) {
        V.house_owner[c][k] = o;
        if (o >= 0) {
          V.house_mat[c][k]->color = tcol(SEAT_HEX[o]);
          V.house_mat[c][k]->version++;
        }
        V.lit[c][k] = V.lit_to[c][k] = 0;
      }
      show(V.house[c][k], o >= 0);
      show(V.room[c][k], o < 0 && k < s->step);
      if (o < 0) continue;
      t3_vec3 at = ly_room(c, k);
      float y = 0, sq = 1;
      if (V.house_drop[c][k] >= 0) {
        float t = V.house_drop[c][k] += dt;
        float u = clampf(t / 0.42f, 0, 1);
        y = (1 - u * u) * 1.4f;
        if (u >= 1) {
          float after = t - 0.42f;
          sq = 1 - 0.18f * expf(-after * 14) * cosf(after * 30);
          if (after > 0.5f) V.house_drop[c][k] = -1;
          if (after < dt + 1e-4f) sfx(V.snd_thump, 0.6f, 1.0f);
        }
        V.moving++;
      }
      cw_node3d_set_position(V.house[c][k], v3(at.x, at.y + y, at.z));
      cw_node3d_set_scale(V.house[c][k], v3(1.25f * LY_CITY_S / sq, 1.25f * LY_CITY_S * sq, 1.25f * LY_CITY_S / sq));
      if (V.lit_wait[c][k] > 0) V.lit_wait[c][k] -= dt;
      else {
        float to = V.lit_to[c][k];
        float l = V.lit[c][k];
        l += (to > l ? 1.6f : -1.0f) * dt; /* swells in over about two thirds of a second, fades slower: never a flash */
        V.lit[c][k] = clampf(l, fminf(to, l), fmaxf(to, l));
        if (fabsf(V.lit[c][k] - to) < 0.002f) V.lit[c][k] = to;
      }
      float e = ease(V.lit[c][k]);
      if (fabsf(V.window_mat[c][k]->emissive_intensity - e * 4.0f) > 0.002f) {
        V.window_mat[c][k]->emissive_intensity = e * 4.0f;
        V.window_mat[c][k]->version++;
      }
      t3_color base = tcol(SEAT_HEX[o]);
      V.house_mat[c][k]->emissive = (t3_color){ base.r * 0.6f + 0.1f * e, base.g * 0.6f + 0.06f * e, base.b * 0.6f };
      float hi = 0.35f + 0.65f * e;
      if (fabsf(V.house_mat[c][k]->emissive_intensity - hi) > 0.002f) {
        V.house_mat[c][k]->emissive_intensity = hi;
        V.house_mat[c][k]->version++;
      }
      glow += e;
      V.lit_now += e > 0.5f;
    }
    show(V.glow[c], glow > 0.01f);
    float op = fminf(1.0f, glow * 0.55f);
    if (fabsf(V.glow_mat[c]->opacity - op) > 0.002f) {
      V.glow_mat[c]->opacity = op;
      V.glow_mat[c]->version++;
    }
  }
  /* the cursor's marker, the ghost house and the route a build would pay for */
  int t = kg_cursor(g);
  bool human = g->turn >= 0 && !kg_seat_cpu(g, g->turn);
  bool ready = !g->over && !kg_busy(g) && !g->dirty && t >= 0 && t < g->ntargets;
  int tk = ready ? g->target[t].kind : -1;
  bool marker = ready && (tk == KG_T_PLANT || tk == KG_T_OWN || tk == KG_T_CITY || tk == KG_T_FUEL);
  show(V.marker, marker);
  if (marker) {
    V.marker_bob += dt;
    t3_vec3 at = g->target[t].at;
    float r = tk == KG_T_CITY ? 0.95f * LY_CITY_S : tk == KG_T_FUEL ? 0.45f : 1.75f;
    if (tk == KG_T_PLANT || tk == KG_T_OWN) at.y = (tk == KG_T_PLANT ? LY_BOARD_Y : LY_TRAY_Y) + 0.06f;
    cw_node3d_set_position(V.marker, v3(at.x, at.y + 0.08f + 0.04f * sinf(V.marker_bob * 4), at.z));
    cw_node3d_set_scale(V.marker, v3(r * (1 + 0.03f * sinf(V.marker_bob * 4)), r, r * (tk == KG_T_PLANT || tk == KG_T_OWN ? 1.3f : 1.0f)));
  }
  bk_move pv = human || g->fast ? kg_preview(g) : BK_MOVE_NONE;
  bool ghost = false;
  static bool route[KC_MAX_LINK];
  memset(route, 0, sizeof route);
  if (pv != BK_MOVE_NONE && bk_move_field(pv, 0) == KC_M_BUILD && g->turn >= 0) {
    int c = bk_move_field(pv, 1), k = kc_house_slot(s, c);
    if (k < 3) {
      t3_vec3 at = ly_room(c, k);
      cw_node3d_set_position(V.ghost, v3(at.x, at.y + 0.02f + 0.03f * sinf(V.time * 3), at.z));
      ghost = true;
    }
    route_links(s, g->turn, c, route);
  }
  show(V.ghost, ghost);
  for (int l = 0; l < KC_NLINK; l++)
    if (route[l] != V.link_lit[l]) {
      V.link_lit[l] = route[l];
      cw_node lit = cw_node_get(V.link[l], "CableLit");
      if (cw_node_valid(lit)) show(lit, route[l]);
    }
  /* a fuel count's cubes lift in the market before they are bought */
  if (pv != BK_MOVE_NONE && bk_move_field(pv, 0) == KC_M_FUEL) {
    int f = bk_move_field(pv, 1), k = bk_move_field(pv, 2), S = kc_fuel_slots[f], m = s->fuel_mkt[f];
    for (int j = S - m; j < S - m + k && j < S; j++) {
      t3_vec3 p = ly_fuel_slot(f, j);
      cw_node3d_set_position(V.cube[f][j], v3(p.x, p.y + 0.08f + 0.18f + 0.03f * sinf(V.time * 4 + (float)j), p.z));
    }
    for (int j = S - m + k; j < S; j++) {
      t3_vec3 p = ly_fuel_slot(f, j);
      cw_node3d_set_position(V.cube[f][j], v3(p.x, p.y + 0.08f, p.z));
    }
  } else {
    for (int f = 0; f < KC_NFUEL; f++)
      for (int j = 0; j < kc_fuel_slots[f]; j++) {
        t3_vec3 p = ly_fuel_slot(f, j);
        cw_node3d_set_position(V.cube[f][j], v3(p.x, p.y + 0.08f, p.z));
      }
  }
  for (int i = 0; i < n; i++) {
    bk_counter_set(&V.money[i], s->p[i].money);
    bk_counter_update(&V.money[i], dt);
  }
  bk_deltas_update(&V.deltas, dt);
  V.banner_t += dt;
  V.payday_t += dt;
}

/* ── HUD ─────────────────────────────────────────────────────────────── */

static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_metrics m = cw_font_measure(font(), s, &(cw_text_style){ .size = size });
  float ox = al == CW_ALIGN_CENTER ? m.width * 0.5f : al == CW_ALIGN_RIGHT ? m.width : 0;
  cw_r2d_text(font(), s, cw_v2(x - ox, y), &(cw_text_style){ .size = size, .color = c, .outline_width = size * 0.08f,
                                                              .outline_color = cw_rgba(0.02f, 0.03f, 0.06f, 0.85f) });
}
static void panel(float x, float y, float w, float h, float a) {
  /* even canvas units: whole pixels at 1x and 1.5x, so every host draws the edges alike */
  x = 2 * roundf(x * 0.5f), y = 2 * roundf(y * 0.5f), w = 2 * roundf(w * 0.5f), h = 2 * roundf(h * 0.5f);
  cw_r2d_round_rect((cw_rect){ x, y, w, h }, h * 0.18f, 6, cw_rgba(0.04f, 0.06f, 0.1f, 0.8f * a));
  cw_r2d_round_rect_lines((cw_rect){ x, y, w, h }, h * 0.18f, 2, 6, cw_rgba(0.95f, 0.72f, 0.35f, 0.5f * a));
}
/* ── city labels ── each name is placed once per screen size and map (the regions in play): near its city, on the
 * board, never running into another name, and as far as it can from other cities' plinths, the links and their cost
 * tags, picked from rings of spots by the fewest collisions. view_label_overlaps counts names that run into another
 * name or sit on their own city (a test checks 0); KILOWATTCOUNTRY_LABELS=below puts every name under its city as
 * before (the test's control). */
#define LABEL_SIZE 14.0f
static struct {
  float sx, sy;
  int regions, map;
  bool valid;
  int checked; /* maps the check has placed (one a frame, so no frame stalls on the whole set) */
  cw_rect r[KC_NCITY]; /* each label's rect, canvas units (w 0: not in play) */
  cw_rect city[KC_NCITY]; /* each city's footprint */
  cw_rect core[KC_NCITY]; /* the middle of each city's plinth: a name or a leader over it hides the city */
  bool lead[KC_NCITY];    /* the name stands off from its city or nearer another: a leader line joins them */
  cw_vec2 lead_a[KC_NCITY], lead_b[KC_NCITY]; /* the leader: from the plinth's rim to the name's nearest point */
  int overlaps, link_hits, strays, misread;                        /* this map */
  int worst_overlaps, worst_link_hits, worst_strays, worst_misread; /* the worst of every map a match can deal */
  bool footprint_rule; /* the control KILOWATTCOUNTRY_LABELS=footprint: leaders by the old footprint rule */
} LB;
int view_label_overlaps(void) { return LB.worst_overlaps; }
int view_label_maps(void) { return LB.checked; }
int view_label_link_hits(void) { return LB.worst_link_hits; }
int view_label_strays(void) { return LB.worst_strays; }
int view_label_misread(void) { return LB.worst_misread; }
void view_label_rects(int32_t *out) {
  for (int c = 0; c < KC_NCITY; c++) {
    out[4 * c] = (int32_t)lroundf(LB.r[c].x), out[4 * c + 1] = (int32_t)lroundf(LB.r[c].y);
    out[4 * c + 2] = (int32_t)lroundf(LB.r[c].w), out[4 * c + 3] = (int32_t)lroundf(LB.r[c].h);
    out[4 * (KC_NCITY + c)] = (int32_t)lroundf(LB.city[c].x), out[4 * (KC_NCITY + c) + 1] = (int32_t)lroundf(LB.city[c].y);
    out[4 * (KC_NCITY + c) + 2] = (int32_t)lroundf(LB.city[c].w), out[4 * (KC_NCITY + c) + 3] = (int32_t)lroundf(LB.city[c].h);
  }
}

static bool rect_hit(cw_rect a, cw_rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
static bool seg_hits_rect(cw_vec2 p, cw_vec2 q, cw_rect r) {
  /* Liang-Barsky clip of the segment against the rect */
  float t0 = 0, t1 = 1, dx = q.x - p.x, dy = q.y - p.y;
  float pp[4] = { -dx, dx, -dy, dy }, qq[4] = { p.x - r.x, r.x + r.w - p.x, p.y - r.y, r.y + r.h - p.y };
  for (int i = 0; i < 4; i++) {
    if (pp[i] == 0) {
      if (qq[i] < 0) return false;
      continue;
    }
    float t = qq[i] / pp[i];
    if (pp[i] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
    else { if (t < t0) return false; if (t < t1) t1 = t; }
  }
  return true;
}

/* a city's footprint on screen: its plinth and the three houses that can stand on it, roofs included */
static cw_rect city_box(int c) {
  t3_vec3 p = ly_city(c);
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  t3_vec3 pts[4 + 3 * 4];
  int n = 0;
  const float pr = 0.4f * LY_CITY_S, hw = 0.12f * LY_CITY_S, hd = 0.11f * LY_CITY_S, hh = 0.32f * LY_CITY_S;
  pts[n++] = v3(p.x - pr, p.y, p.z), pts[n++] = v3(p.x + pr, p.y, p.z), pts[n++] = v3(p.x, p.y, p.z - pr), pts[n++] = v3(p.x, p.y, p.z + pr);
  for (int k = 0; k < 3; k++) {
    t3_vec3 r = ly_room(c, k);
    pts[n++] = v3(r.x - hw, r.y, r.z + hd), pts[n++] = v3(r.x + hw, r.y, r.z + hd);
    pts[n++] = v3(r.x - hw, r.y + hh, r.z - hd), pts[n++] = v3(r.x + hw, r.y + hh, r.z - hd);
  }
  for (int i = 0; i < n; i++) {
    cw_vec2 q = to_screen(pts[i]);
    x0 = fminf(x0, q.x), y0 = fminf(y0, q.y), x1 = fmaxf(x1, q.x), y1 = fmaxf(y1, q.y);
  }
  return (cw_rect){ x0, y0, x1 - x0, y1 - y0 };
}

static cw_rect grow(cw_rect r, float d) { return (cw_rect){ r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d }; }
/* the gap between two rects (0 when they touch or overlap) */
static float rect_gap(cw_rect a, cw_rect b) {
  float dx = fmaxf(0, fmaxf(a.x - (b.x + b.w), b.x - (a.x + a.w))), dy = fmaxf(0, fmaxf(a.y - (b.y + b.h), b.y - (a.y + a.h)));
  return sqrtf(dx * dx + dy * dy);
}
/* a name that reads as a neighbour's: another city in play stands as close to it as its own city does (or closer) */
static bool label_strays(int c, cw_rect r, const bool *in, const cw_rect *city, float margin) {
  float own = rect_gap(r, city[c]);
  for (int o = 0; o < KC_NCITY; o++)
    if (o != c && in[o] && rect_gap(r, city[o]) < own + margin) return true;
  return false;
}
/* how far a point (a city's dot) is from a name's rect */
static float dot_dist(cw_vec2 p, cw_rect r) {
  float dx = fmaxf(0, fmaxf(r.x - p.x, p.x - (r.x + r.w))), dy = fmaxf(0, fmaxf(r.y - p.y, p.y - (r.y + r.h)));
  return sqrtf(dx * dx + dy * dy);
}
/* a name a player would read as another city's: some other city's dot is as near the name as its own dot (within
 * margin), so the name alone tells the wrong geography */
static bool label_misread(int c, cw_rect r, const bool *in, const cw_vec2 *at, float margin) {
  float own = dot_dist(at[c], r);
  for (int o = 0; o < KC_NCITY; o++)
    if (o != c && in[o] && dot_dist(at[o], r) < own + margin) return true;
  return false;
}
/* the leader a name needs (it stands off from its city, or nearer another city's dot than its own): from the plinth's
 * rim to the name's nearest point. False: the name sits against its own city, nearest its own dot, and needs none.
 * KILOWATTCOUNTRY_LABELS=footprint (a control) asks only whether another city's footprint is as near, as before */
static bool label_leader(int c, cw_rect r, const bool *in, const cw_rect *city, const cw_vec2 *at, float u, cw_vec2 *a, cw_vec2 *b) {
  bool off = rect_gap(r, city[c]) > 4 * u;
  bool other = LB.footprint_rule ? label_strays(c, r, in, city, 3 * u) : label_misread(c, r, in, at, 4 * u);
  if (!off && !other) return false;
  cw_vec2 cc = at[c];
  cw_vec2 p = cw_v2(fminf(fmaxf(cc.x, r.x), r.x + r.w), fminf(fmaxf(cc.y, r.y), r.y + r.h));
  float dx = p.x - cc.x, dy = p.y - cc.y, d = sqrtf(dx * dx + dy * dy), rad = city[c].w * 0.3f;
  *a = d > rad ? cw_v2(cc.x + dx / d * rad, cc.y + dy / d * rad) : p;
  *b = p;
  return true;
}
/* a leader over another city's plinth or another name (placed so far): it would point the wrong way */
static int leader_hits(int c, cw_vec2 a, cw_vec2 b, const bool *in, const cw_rect *city) {
  int n = 0;
  for (int o = 0; o < KC_NCITY; o++) {
    if (o == c || !in[o]) continue;
    if (seg_hits_rect(a, b, city[o])) n++; /* over or close by another plinth or its houses */
    if (LB.r[o].w > 0 && seg_hits_rect(a, b, LB.r[o])) n++;
  }
  return n;
}

/* a label rect's cost against everything else on the map: another name closer than `gap`, its own city and the
 * board's edge weigh most (names never run together), then another city's plinth, cost tags and links crossed */
static int label_cost(int c, cw_rect r, const bool *in, const cw_rect *city, const cw_vec2 *at, const bool *link_in,
                      const cw_rect *tag, cw_rect board, float gap) {
  int cost = 0;
  cw_rect rg = grow(r, gap);
  for (int o = 0; o < KC_NCITY; o++) {
    if (!in[o]) continue;
    if (rect_hit(r, city[o])) cost += o == c ? 1000 : 150;
    if (o != c && rect_hit(r, LB.core[o])) cost += 2000; /* over the plinth itself: it hides a city */
    if (o != c && LB.r[o].w > 0 && rect_hit(rg, LB.r[o])) cost += 1000;
  }
  if (r.x < board.x || r.y < board.y || r.x + r.w > board.x + board.w || r.y + r.h > board.y + board.h) cost += 1000;
  cw_vec2 la, lb;
  if (label_leader(c, r, in, city, at, gap / 6, &la, &lb)) { /* a leader: allowed, short, never over another city or name */
    float len = sqrtf((lb.x - la.x) * (lb.x - la.x) + (lb.y - la.y) * (lb.y - la.y));
    cost += 30 + (int)(3 * len / (gap / 6)) + 2000 * leader_hits(c, la, lb, in, city);
  }
  if (label_misread(c, r, in, at, gap * (4.0f / 6))) cost += 300; /* best nearest its own dot, with no leader needed */
  for (int l = 0; l < KC_NLINK; l++) {
    if (!link_in[l]) continue;
    if (rect_hit(r, tag[l])) cost += 20;
    if (seg_hits_rect(at[kc_links[l].a], at[kc_links[l].b], r)) cost += 4;
  }
  return cost;
}

/* every name placed for one map (a mask of the regions in play) at one screen size, and what still collides */
static void layout_labels(int regions, cw_vec2 sz) {
  float u = sz.y / 720.0f;
  const char *mode = cw_test_env("KILOWATTCOUNTRY_LABELS");
  bool below = mode && strcmp(mode, "below") == 0;
  LB.footprint_rule = mode && strcmp(mode, "footprint") == 0;
  cw_vec2 at[KC_NCITY], lsz[KC_NCITY];
  bool in[KC_NCITY];
  cw_rect city[KC_NCITY], tag[KC_MAX_LINK];
  for (int c = 0; c < KC_NCITY; c++) {
    in[c] = (regions >> kc_cities[c].region) & 1;
    at[c] = to_screen(ly_city(c));
    city[c] = LB.city[c] = city_box(c);
    LB.core[c] = (cw_rect){ at[c].x - 0.15f * city[c].w, at[c].y - 0.12f * city[c].h, 0.3f * city[c].w, 0.24f * city[c].h };
    LB.r[c] = (cw_rect){ 0, 0, 0, 0 };
    LB.lead[c] = false;
    cw_text_metrics m = cw_font_measure(font(), kc_cities[c].name, &(cw_text_style){ .size = LABEL_SIZE * u });
    lsz[c] = cw_v2(m.width + 2 * u, m.height);
  }
  bool link_in[KC_MAX_LINK];
  for (int l = 0; l < KC_NLINK; l++) {
    int a = kc_links[l].a, b = kc_links[l].b;
    link_in[l] = in[a] && in[b];
    cw_vec2 m = to_screen(v3((ly_city(a).x + ly_city(b).x) * 0.5f, LY_BOARD_Y, (ly_city(a).z + ly_city(b).z) * 0.5f));
    tag[l] = (cw_rect){ m.x - 15 * u, m.y - 12 * u, 30 * u, 22 * u }; /* the build cost tag drawn on the link */
  }
  /* the map's top face on screen, less a margin: names stay on the board */
  cw_vec2 b0 = to_screen(v3(LY_MAP_X0, LY_BOARD_Y, LY_MAP_Z0)), b1 = to_screen(v3(LY_MAP_X0 + LY_MAP_W, LY_BOARD_Y, LY_MAP_Z0 + LY_MAP_D));
  cw_rect board = { b0.x + 4 * u, b0.y + 4 * u, b1.x - b0.x - 8 * u, b1.y - b0.y - 8 * u };
  /* candidate spots: sixteen directions around the city, at five distances; the label's nearest edge or corner sits
   * just off the city's footprint along each */
  enum { DIRS = 16, RINGS = 5, NS = DIRS * RINGS };
  cw_rect cand[KC_NCITY][NS];
  for (int c = 0; c < KC_NCITY; c++) {
    if (!in[c]) continue;
    float lw = lsz[c].x, lh = lsz[c].y;
    cw_rect cb = city[c];
    float ccx = cb.x + cb.w * 0.5f, ccy = cb.y + cb.h * 0.5f;
    for (int k = 0; k < NS; k++) {
      float ang = (float)(k % DIRS) * (6.2831853f / DIRS) + 1.5707963f; /* the first: straight under */
      float dx = cosf(ang), dy = sinf(ang), gap = (2 + 8 * (k / DIRS)) * u;
      /* the distance along (dx, dy) at which the label's box just clears the footprint's box, plus the gap */
      float tx = fabsf(dx) > 1e-4f ? (cb.w * 0.5f + lw * 0.5f) / fabsf(dx) : 1e9f;
      float ty = fabsf(dy) > 1e-4f ? (cb.h * 0.5f + lh * 0.5f) / fabsf(dy) : 1e9f;
      float t = fminf(tx, ty) + gap;
      cand[c][k] = (cw_rect){ ccx + dx * t - lw * 0.5f, ccy + dy * t - lh * 0.5f, lw, lh };
    }
    if (below) cand[c][0] = (cw_rect){ at[c].x - lw * 0.5f, at[c].y + 13 * u, lw, lh }; /* the old place: under the city */
  }
  /* greedy, then passes that move each name to its best spot given all the others */
  /* (three rounds of: passes, then the pairs still too close moved together) */
  for (int outer = 0; outer < (below ? 1 : 3); outer++) {
  for (int pass = 0; pass < (below ? 1 : 16); pass++) {
    bool moved = false;
    for (int c = 0; c < KC_NCITY; c++) {
      if (!in[c]) continue;
      int best = -1, best_cost = 1 << 30;
      for (int k = 0; k < (below ? 1 : NS); k++) {
        /* near and under its city first: a name off to one side reads as its neighbour's */
        static const int DIR_COST[DIRS] = { 0, 14, 18, 14, 10, 14, 18, 14, 6, 14, 18, 14, 10, 14, 18, 14 };
        int cost = label_cost(c, cand[c][k], in, city, at, link_in, tag, board, 6 * u) + (k / DIRS) * 40 + DIR_COST[k % DIRS];
        if (cost < best_cost) best_cost = cost, best = k;
      }
      cw_rect r = cand[c][best];
      if (r.x != LB.r[c].x || r.y != LB.r[c].y) moved = true;
      LB.r[c] = r;
    }
    if (!moved) break;
  }
  /* two names still too close: both move together, to the pair of spots that costs least */
  static const int PAIR_DIR_COST[DIRS] = { 0, 14, 18, 14, 10, 14, 18, 14, 6, 14, 18, 14, 10, 14, 18, 14 };
  for (int round = 0, fixed = 1; round < 6 && fixed && !below; round++) {
   fixed = 0;
   for (int c = 0; c < KC_NCITY; c++) {
    if (!in[c]) continue;
    for (int o = c + 1; o < KC_NCITY; o++) {
      if (!in[o] || !rect_hit(grow(LB.r[c], 3 * u), LB.r[o])) continue;
      fixed++;
      LB.r[o] = (cw_rect){ 0, 0, 0, 0 }; /* (out of the way while c's spots are weighed) */
      int bc = -1, bo = -1, best = 1 << 30;
      for (int kc = 0; kc < NS; kc++) {
        LB.r[c] = cand[c][kc];
        int cc = label_cost(c, cand[c][kc], in, city, at, link_in, tag, board, 6 * u) + (kc / DIRS) * 40 + PAIR_DIR_COST[kc % DIRS];
        if (cc >= best) continue;
        for (int ko = 0; ko < NS; ko++) {
          int co = cc + label_cost(o, cand[o][ko], in, city, at, link_in, tag, board, 6 * u) + (ko / DIRS) * 40 + PAIR_DIR_COST[ko % DIRS];
          if (co < best) best = co, bc = kc, bo = ko;
        }
      }
      LB.r[c] = cand[c][bc], LB.r[o] = cand[o][bo];
    }
   }
  }
  }
  /* what still collides (the check): names closer than 3 units to a name, or over their own city; strays: a name over
   * the middle of another city's plinth (it hides that city) or a leader over another city or name (it points the
   * wrong way); and (looser) names over another city's footprint, a cost tag or a link */
  LB.overlaps = LB.link_hits = LB.strays = LB.misread = 0;
  for (int c = 0; c < KC_NCITY; c++) {
    if (!in[c]) continue;
    /* a name off its city, or nearer another, gets a leader; a leader over another city or name is a stray */
    LB.lead[c] = label_leader(c, LB.r[c], in, city, at, u, &LB.lead_a[c], &LB.lead_b[c]);
    if (!LB.lead[c] && label_misread(c, LB.r[c], in, at, 0)) LB.misread++; /* nearer another dot, and no leader to say so */
    if (LB.lead[c]) LB.strays += leader_hits(c, LB.lead_a[c], LB.lead_b[c], in, city);
    for (int o = 0; o < KC_NCITY; o++)
      if (o != c && in[o] && rect_hit(LB.r[c], LB.core[o])) LB.strays++; /* a name hiding a city */
    for (int o = 0; o < KC_NCITY; o++) {
      if (!in[o]) continue;
      if (o > c && rect_hit(grow(LB.r[c], 3 * u), LB.r[o])) LB.overlaps++; /* names run together */
      if (rect_hit(LB.r[c], city[o])) (o == c ? LB.overlaps++ : LB.link_hits++);
    }
    for (int l = 0; l < KC_NLINK; l++) {
      if (!link_in[l]) continue;
      if (rect_hit(LB.r[c], tag[l])) LB.link_hits++;
      if (seg_hits_rect(at[kc_links[l].a], at[kc_links[l].b], LB.r[c])) LB.link_hits++;
    }
  }
}

static void place_labels(const kc_state *s, cw_vec2 sz) {
  bool resized = LB.sx != sz.x || LB.sy != sz.y;
  if (resized) LB.checked = 0, LB.worst_overlaps = LB.worst_link_hits = LB.worst_strays = LB.worst_misread = 0, LB.sx = sz.x, LB.sy = sz.y, LB.valid = false;
  /* the check covers every map a match can deal: each of the maps, each joined set of three, four or five regions on
   * it (placed for the check, then thrown away); one map a frame */
  if (LB.checked < KC_NMAPS) {
    int keep = kc_map_active;
    kc_use_map(LB.checked);
    for (int i = 0; i < 3; i++)
      for (int k = 0; k < kc_map->nsets[i]; k++) {
        layout_labels(kc_map->sets[i][k], sz);
        if (LB.overlaps > LB.worst_overlaps) LB.worst_overlaps = LB.overlaps;
        if (LB.link_hits > LB.worst_link_hits) LB.worst_link_hits = LB.link_hits;
        if (LB.strays > LB.worst_strays) LB.worst_strays = LB.strays;
        if (LB.misread > LB.worst_misread) LB.worst_misread = LB.misread;
      }
    kc_use_map(keep);
    LB.checked++;
    LB.valid = false; /* the names of the map on the table are placed again below */
  }
  if (LB.valid && !resized && LB.regions == s->regions && LB.map == kc_map_active) return;
  LB.valid = true, LB.sx = sz.x, LB.sy = sz.y, LB.regions = s->regions, LB.map = kc_map_active;
  layout_labels(s->regions, sz);
}

static const char *ordinal(int i) {
  static const char *const O[4] = { "1st", "2nd", "3rd", "4th" };
  return i >= 0 && i < 4 ? O[i] : "";
}

static void plant_line(int plant, char *buf, size_t cap) {
  const kc_plant_def *d = &kc_plants[plant];
  if (d->kind == KC_K_GREEN) snprintf(buf, cap, "Plant %d: wind and sun, needs no fuel, lights %d %s", d->number, d->cities, d->cities == 1 ? "city" : "cities");
  else snprintf(buf, cap, "Plant %d: burns %d %s, lights %d %s", d->number, d->need, kc_kind_name(d->kind), d->cities, d->cities == 1 ? "city" : "cities");
}

static void target_line(const kg_game *g, const kg_target *t, char *buf, size_t cap) {
  const kc_state *s = &g->shown;
  int seat = g->turn;
  char pl[96];
  switch (t->kind) {
  case KG_T_PLANT: plant_line(s->market[t->value], pl, sizeof pl); snprintf(buf, cap, "%s. Opening bid $%d.", pl, kc_plants[s->market[t->value]].number); break;
  case KG_T_OWN: plant_line(s->p[seat].plants[t->value], pl, sizeof pl); snprintf(buf, cap, "Scrap it? %s. Its fuel moves to your other plants if they have room.", pl); break;
  case KG_T_FUEL: {
    int f = t->value, room = kc_store_room(&s->p[seat], f), need = kc_fuel_needed(s, seat, f);
    snprintf(buf, cap, "%s: the next one costs $%d, %d left. Your plants have room for %d more%s.", FUEL_TITLE[f], kc_fuel_price(s, f),
             s->fuel_mkt[f], room, need ? "" : " (they have enough for one round)");
    if (need) {
      size_t l = strlen(buf);
      snprintf(buf + l - 1, cap - l + 1, " and need %d to run every plant once.", need);
    }
    break;
  }
  case KG_T_COUNT: snprintf(buf, cap, "Buy %d %s for $%d.", t->value, kc_fuel_name(g->count_fuel), kc_fuel_cost(s, g->count_fuel, t->value)); break;
  case KG_T_CITY: {
    int route = 0, cost = kc_build_cost(s, seat, t->value, &route);
    if (s->p[seat].ncity == 0) snprintf(buf, cap, "Build in %s: your first city costs just the house, $%d.", kc_cities[t->value].name, cost);
    else snprintf(buf, cap, "Build in %s: links $%d + house $%d = $%d.", kc_cities[t->value].name, route, cost - route, cost);
    break;
  }
  default: {
    int kind = bk_move_field(t->move, 0);
    if (kind == KC_M_PASS) snprintf(buf, cap, "Pass: buy no plant this round (and stay out of this round's auctions).");
    else if (kind == KC_M_DONE) snprintf(buf, cap, s->phase == KC_PH_FUEL ? "Done buying fuel." : "Done building for this round.");
    else if (kind == KC_M_DROP) snprintf(buf, cap, "Drop out of this auction.");
    else if (kind == KC_M_RAISE) snprintf(buf, cap, "Bid $%d for plant %d.", s->auc_bid + KC_RAISES[bk_move_field(t->move, 1)], kc_plants[s->auc_plant].number);
    else buf[0] = 0;
  }
  }
}

static const char *const HELP[] = {
  "Kilowatt Country: how to play",
  "Every round has four parts. Light the most cities in the last round to win.",
  "1. Plants: in turn, put a plant from the front row up for auction, or pass. Its number is the opening bid.",
  "   Everyone still in raises or drops out; the last bidder pays. One plant per company a round, three at most.",
  "2. Fuel: buy coal, oil, garbage or uranium for your plants. Each plant stores twice what it burns.",
  "   Prices climb as a fuel runs short, and the market refills a little every round.",
  "3. Build: put houses in cities. Your first costs just the house; later ones also pay the links to your network.",
  "   In step 2 two companies may share a city, in step 3 three.",
  "4. Power: your plants burn their fuel and light your cities. Each city lit pays more money.",
  "The company with the most cities buys plants first but buys fuel and builds last.",
  "The game ends when a company reaches the city goal: the most cities lit that round wins, then the most money.",
};

static void phase_line(const kc_state *s, char *buf, size_t cap) {
  const char *what = "";
  switch (s->phase) {
  case KC_PH_AUCTION: what = s->auc_plant >= 0 ? "Plants: bidding" : "Plants: open an auction or pass"; break;
  case KC_PH_DISCARD: what = "Plants: a fourth plant, scrap one"; break;
  case KC_PH_FUEL: what = "Fuel: last in order buys first"; break;
  case KC_PH_BUILD: what = "Build: last in order builds first"; break;
  default: what = "Game over"; break;
  }
  snprintf(buf, cap, "Round %d   Step %d   %s   First to %d cities ends the game", s->round, s->step, what, kc_end_cities(s));
}

/* every city in play's name, on its placed spot; a name that had to stand off from its city gets a thin leader to it */
static void city_names(const kc_state *s, float u) {
  for (int c = 0; c < KC_NCITY; c++) {
    if (!kc_region_in(s, kc_cities[c].region)) continue;
    cw_rect r = LB.r[c];
    if (LB.lead[c]) {
      cw_r2d_line(LB.lead_a[c], LB.lead_b[c], 1.6f * u, cw_rgba(0.85f, 0.9f, 1.0f, 0.7f));
      cw_r2d_circle(LB.lead_a[c], 2.4f * u, 10, cw_rgba(0.9f, 0.94f, 1.0f, 0.85f));
    }
    /* a dark pill behind the name: it reads over a link or the edge of a neighbour's plinth */
    cw_r2d_round_rect(grow(r, 2 * u), 6 * u, 6, cw_rgba(0.02f, 0.03f, 0.07f, 0.62f));
    text(kc_cities[c].name, r.x + r.w * 0.5f, r.y, LABEL_SIZE * u, rgba(0xe4ecf8ff), CW_ALIGN_CENTER);
  }
}

/* the setup card: the maps down the left (the one picked marked), Begin under them, what the map picked is like */
static void hud_setup(const kg_game *g, cw_vec2 sz, float u) {
  const kc_state *s = &g->shown;
  place_labels(s, sz);
  if (g->help) { view_help(sz, true); return; }
  city_names(s, u);
  const kc_map_def *m = kc_map;
  int lo = 99, hi = 0;
  for (int i = 0; i < m->nlink; i++) lo = m->links[i].cost < lo ? m->links[i].cost : lo, hi = m->links[i].cost > hi ? m->links[i].cost : hi;
  int dealt = 0;
  for (int r = 0; r < KC_NREGION; r++) dealt += kc_region_in(s, r);
  float pw = 820 * u, px = sz.x * 0.5f - pw * 0.5f, py = 8 * u;
  panel(px, py, pw, 112 * u, 1);
  panel(px, py, pw, 112 * u, 1);
  char line[200];
  snprintf(line, sizeof line, "Choose the map: %s", m->title);
  text(line, sz.x * 0.5f, py + 8 * u, 26 * u, rgba(0xffd890ff), CW_ALIGN_CENTER);
  snprintf(line, sizeof line, "%d real cities in %d regions. A link costs $2 plus $1 for every %d km: $%d to $%d here.", KC_NCITY, KC_NREGION,
           m->km_per, lo, hi);
  text(line, sz.x * 0.5f, py + 44 * u, 17 * u, rgba(0xe8eef6ff), CW_ALIGN_CENTER);
  snprintf(line, sizeof line, "%d companies play on %d of the %d regions; the dark ones sit out this game.", g->players, dealt, KC_NREGION);
  text(line, sz.x * 0.5f, py + 74 * u, 17 * u, rgba(0xc8d4e2ff), CW_ALIGN_CENTER);
  int t = kg_cursor(g);
  for (int i = 0; i < g->ntargets; i++) {
    const kg_target *tt = &g->target[i];
    cw_vec2 p = to_screen(tt->at);
    bool on = i == t, chosen = tt->kind == KG_T_MAP && tt->value == g->map, begin = tt->kind == KG_T_BEGIN;
    float bw = 200 * u, bh = (begin ? 40 : 34) * u;
    cw_rect b = { p.x - bw / 2, p.y - bh / 2, bw, bh };
    cw_r2d_round_rect(b, 9 * u, 6, on ? cw_rgba(0.98f, 0.8f, 0.42f, 0.96f) : begin ? cw_rgba(0.12f, 0.2f, 0.12f, 0.92f) : cw_rgba(0.05f, 0.07f, 0.12f, 0.9f));
    cw_r2d_round_rect_lines(b, 9 * u, (chosen ? 3.0f : 1.5f) * u, 6, chosen ? cw_rgba(1, 0.86f, 0.5f, 1) : cw_rgba(0.95f, 0.72f, 0.35f, 0.55f));
    if (chosen) cw_r2d_circle(cw_v2(b.x + 14 * u, p.y), 5 * u, 12, on ? cw_rgba(0.1f, 0.07f, 0.02f, 1) : cw_rgba(1, 0.86f, 0.5f, 1));
    const char *lab = begin ? "Begin" : kc_maps[tt->value].title;
    text(lab, p.x + (chosen ? 6 * u : 0), p.y - (begin ? 12 : 11) * u, (begin ? 20 : 17) * u, on ? cw_rgba(0.1f, 0.07f, 0.02f, 1) : cw_rgba(1, 0.96f, 0.88f, 1), CW_ALIGN_CENTER);
  }
  cw_prompt_text(font(), "{left|right} {up|down} choose    {choose} pick    {how_to_play} how to play", g->turn, cw_v2(sz.x * 0.5f, sz.y - 30 * u), 18 * u,
                 cw_rgba(1, 0.97f, 0.92f, 0.95f), CW_ALIGN_CENTER);
}

void view_hud(const kg_game *g, float dt) {
  (void)dt;
  if (g->setup) {
    cw_vec2 sz = cw_r2d_canvas_size();
    hud_setup(g, sz, sz.y / 720.0f);
    return;
  }
  if (!g->match) return;
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  const kc_state *s = &g->shown;
  int n = g->players;
  view_stage = 10;
  place_labels(s, sz); /* once per map and screen size; its check fields count from the first frame of play */
  /* the companies along the top: name, money, cities, turn order */
  for (int i = 0; i < n; i++) {
    cw_rect pr = hud_panel(i, n, sz);
    float x = pr.x, y = pr.y, w = pr.w, h = pr.h;
    bool turn = !g->over && g->turn == i && !kg_busy(g);
    panel(x, y, w, h, 1);
    if (turn) cw_r2d_round_rect_lines((cw_rect){ x - 2 * u, y - 2 * u, w + 4 * u, h + 4 * u }, h * 0.2f, 3 * u, 6, rgba(SEAT_RGBA[i]));
    cw_r2d_circle(cw_v2(x + 17 * u, y + 19 * u), 7 * u, 16, rgba(SEAT_RGBA[i]));
    char line[96];
    bool cpu = kg_seat_cpu(g, i);
    snprintf(line, sizeof line, "%s%s%s", kg_seat_name(i), cpu ? "  CPU " : "", cpu ? cw_shell_cpu_level_label((cw_shell_cpu_level)g->cpu_level[i]) : "");
    text(line, x + 30 * u, y + 7 * u, 19 * u, rgba(SEAT_RGBA[i]), CW_ALIGN_LEFT);
    snprintf(line, sizeof line, "$%d", bk_counter_value(&V.money[i]));
    text(line, x + w - 12 * u, y + 4 * u, 27 * u, rgba(0xffe9b0ff), CW_ALIGN_RIGHT);
    const kc_player *p = &s->p[i];
    int ord = 0;
    for (int k = 0; k < n; k++)
      if (s->order[k] == i) ord = k;
    snprintf(line, sizeof line, "%d %s   plants light %d   %s in order", p->ncity, p->ncity == 1 ? "city" : "cities", kc_capacity(p), ordinal(ord));
    text(line, x + 12 * u, y + 36 * u, 15 * u, rgba(0xdce4ecff), CW_ALIGN_LEFT);
  }
  view_stage = 11;
  /* labels on the table: the market rows, the deck, the fuel tracks and their prices, the cities */
  if (!g->help) {
    cw_vec2 p;
    p = to_screen(v3(-10.05f, LY_BOARD_Y, -5.95f));
    text(s->step >= 3 ? "Plants for sale" : "Plants for sale now", p.x, p.y - 20 * u, 16 * u, rgba(0xf4e2c0ff), CW_ALIGN_CENTER);
    if (s->step < 3) {
      p = to_screen(v3(-10.05f, LY_BOARD_Y, -3.6f));
      text("Coming later", p.x, p.y - 9 * u, 15 * u, rgba(0xb8c0ccff), CW_ALIGN_CENTER);
    }
    char line[64];
    p = to_screen(v3(-12.0f, 0.1f, 0.8f));
    snprintf(line, sizeof line, "Deck %d", kc_deck_count(s));
    text(line, p.x, p.y, 15 * u, rgba(0xc8d0dcff), CW_ALIGN_CENTER);
    int aside = 0;
    for (int i = 0; i < KC_NPLANT; i++) aside += s->loc[i] == KC_L_ASIDE;
    if (aside) { /* the plants out of this game lie face up in a row beside the deck, large enough to read */
      p = to_screen(v3(ly_aside(0).x - LY_CARD_W * LY_ASIDE_SCALE * 0.5f, 0.1f, 0.85f)); /* from the row's left edge: clear of the deck's count */
      text("Set aside this game", p.x, p.y, 16 * u, rgba(0xc8d0dcff), CW_ALIGN_LEFT);
    }
    for (int f = 0; f < KC_NFUEL; f++) {
      p = to_screen(v3(LY_FUEL_X0 - 0.05f, 0.05f, ly_fuel_row_z(f) - 0.5f));
      text(FUEL_TITLE[f], p.x, p.y - 22 * u, 16 * u, rgba(FUEL_UI[f]), CW_ALIGN_LEFT);
      int cells = f == KC_URANIUM ? 12 : 8;
      for (int k = 0; k < cells; k++) {
        int idx = f == KC_URANIUM ? k : k * 3;
        int price = idx < 24 ? kc_fuel_price_tab[f][idx] : 0;
        t3_vec3 at = ly_fuel_cell(f, k);
        at.z -= 0.26f;
        p = to_screen(at);
        snprintf(line, sizeof line, "%d", price);
        text(line, p.x, p.y - 10 * u, 15 * u, rgba(0xf4ecdcff), CW_ALIGN_CENTER);
      }
    }
    p = to_screen(v3(LY_FUEL_X0 + LY_FUEL_W * 0.5f, 0.05f, ly_fuel_row_z(0) - 1.0f));
    text("Fuel, $ each: the cheapest left sets the price", p.x, p.y - 30 * u, 15 * u, rgba(0xf4e2c0ff), CW_ALIGN_CENTER);
    city_names(s, u);
    int focus_city = -1;
    {
      int t = kg_cursor(g);
      if (!kg_busy(g) && !g->dirty && t >= 0 && t < g->ntargets && g->target[t].kind == KG_T_CITY) focus_city = g->target[t].value;
    }
    if (s->phase == KC_PH_BUILD && !g->over)
      for (int l = 0; l < KC_NLINK; l++) {
        int a = kc_links[l].a, b = kc_links[l].b;
        if (!kc_region_in(s, kc_cities[a].region) || !kc_region_in(s, kc_cities[b].region)) continue;
        if (!V.link_lit[l] && a != focus_city && b != focus_city) continue;
        t3_vec3 pa = ly_city(a), pb = ly_city(b);
        p = to_screen(v3((pa.x + pb.x) * 0.5f, LY_BOARD_Y, (pa.z + pb.z) * 0.5f));
        snprintf(line, sizeof line, "%d", kc_links[l].cost);
        cw_r2d_round_rect((cw_rect){ p.x - 15 * u, p.y - 12 * u, 30 * u, 22 * u }, 6 * u, 6, cw_rgba(0.03f, 0.05f, 0.1f, 0.85f));
        text(line, p.x, p.y - 10 * u, 15 * u, V.link_lit[l] ? rgba(0xffd080ff) : rgba(0xa8c4e4ff), CW_ALIGN_CENTER);
      }
  }
  view_stage = 12;
  /* the status block under the companies: the auction, what the cursor offers, the round */
  if (!g->help && !g->over) {
    char line[200];
    if (s->phase == KC_PH_AUCTION && s->auc_plant >= 0) {
      char in[96] = "";
      for (int i = 0; i < n; i++)
        if (s->auc_in & (1 << i)) {
          size_t l = strlen(in);
          snprintf(in + l, sizeof in - l, "%s%s", in[0] ? ", " : "", kg_seat_name(i));
        }
      snprintf(line, sizeof line, "Auction for plant %d: high bid $%d by %s.   Still in: %s", kc_plants[s->auc_plant].number, s->auc_bid,
               kg_seat_name(s->auc_high), in);
      text(line, sz.x * 0.5f, 77 * u, 19 * u, rgba(SEAT_RGBA[s->auc_high]), CW_ALIGN_CENTER); /* between the money badges and the turn line */
    }
    phase_line(s, line, sizeof line);
    text(line, 16 * u, 136 * u, 15 * u, rgba(0xd4dce8ff), CW_ALIGN_LEFT);
  }
  view_stage = 13;
  if (V.banner[0] && V.banner_t < 3.0f && !g->over) {
    float a = clampf(V.banner_t * 2, 0, 1) * clampf((3.0f - V.banner_t) * 2.0f, 0, 1);
    panel(sz.x * 0.5f - 400 * u, 160 * u, 800 * u, 40 * u, a);
    text(V.banner, sz.x * 0.5f, 168 * u, 19 * u, cw_rgba(1, 0.9f, 0.7f, a), CW_ALIGN_CENTER);
  }
  /* payday: what each company lit and earned, over its own tray (the map stays clear to watch the lights) */
  float pay_len = g->fast ? 2.5f : 5.0f;
  if (V.payday_t < pay_len && !g->over) {
    float a = clampf(V.payday_t * 2, 0, 1) * clampf((pay_len - V.payday_t) * 1.5f, 0, 1);
    text("Power: the cities light up", sz.x * 0.5f, 150 * u, 22 * u, cw_rgba(1, 0.86f, 0.55f, a), CW_ALIGN_CENTER);
    for (int i = 0; i < n; i++) {
      const kc_player *p = &V.payday.p[i];
      char line[96];
      snprintf(line, sizeof line, "lit %d of %d %s, earned $%d", p->powered, p->ncity, p->ncity == 1 ? "city" : "cities", p->earned);
      cw_vec2 q = to_screen(v3(ly_tray_x(i, n), 0.1f, LY_TRAY_Z - LY_TRAY_D * 0.5f - 0.15f));
      /* as wide as the trays allow: neighbouring panels never touch */
      float w2 = 300 * u;
      if (n > 1) w2 = fminf(w2, fabsf(to_screen(v3(ly_tray_x(1, n), 0.1f, LY_TRAY_Z)).x - to_screen(v3(ly_tray_x(0, n), 0.1f, LY_TRAY_Z)).x) - 12 * u);
      panel(q.x - w2 * 0.5f, q.y - 40 * u, w2, 32 * u, a);
      cw_color c = rgba(SEAT_RGBA[i]);
      c.a = a;
      text(line, q.x, q.y - 35 * u, 17 * u, c, CW_ALIGN_CENTER);
    }
  }
  /* the focus card: the plant under the cursor (or up for auction), large, with its words */
  if (!g->help && !g->over) {
    int plant = -1;
    int t = kg_cursor(g);
    if (!kg_busy(g) && !g->dirty && g->turn >= 0 && t >= 0 && t < g->ntargets) {
      if (g->target[t].kind == KG_T_PLANT) plant = s->market[g->target[t].value];
      else if (g->target[t].kind == KG_T_OWN) plant = s->p[g->turn].plants[g->target[t].value];
    }
    if (plant < 0 && s->phase == KC_PH_AUCTION && s->auc_plant >= 0) plant = s->auc_plant;
    if (plant >= 0) {
      const kc_plant_def *d = &kc_plants[plant];
      float px = 14 * u, py = 300 * u, pw = 206 * u, ph = 300 * u;
      panel(px, py, pw, ph, 1);
      panel(px, py, pw, ph, 1);
      float cw2 = 150 * u, ch = 208 * u;
      cw_xform2d m = cw_xform2d_trs(cw_v2(px + (pw - cw2) * 0.5f, py + 12 * u), 0, cw_v2(1, 1));
      cw_r2d_set_transform(&m);
      paint(plant, cw_v2(cw2, ch), NULL);
      cw_r2d_set_transform(NULL);
      char l1[40], l2[40];
      if (d->kind == KC_K_GREEN) snprintf(l1, sizeof l1, "needs no fuel");
      else snprintf(l1, sizeof l1, "burns %d %s", d->need, kc_kind_name(d->kind));
      snprintf(l2, sizeof l2, "lights %d %s", d->cities, d->cities == 1 ? "city" : "cities");
      text(l1, px + pw * 0.5f, py + 228 * u, 18 * u, rgba(0xf4ecdcff), CW_ALIGN_CENTER);
      text(l2, px + pw * 0.5f, py + 256 * u, 18 * u, rgba(0xffd88aff), CW_ALIGN_CENTER);
    }
  }
  view_stage = 14;
  /* money changes: a badge on the company's panel edge that swells in, holds and fades (bk_deltas keeps the time) */
  for (int i = 0; i < BK_DELTAS_MAX; i++) {
    const bk_delta *d = &V.deltas.d[i];
    if (!d->live) continue;
    float t = d->age / V.deltas.life, a = t < 0.15f ? t / 0.15f : t > 0.6f ? (1 - t) / 0.4f : 1;
    char txt[16];
    snprintf(txt, sizeof txt, d->value > 0 ? "+%d" : "%d", d->value);
    float fs = 16 * u;
    cw_text_metrics m = cw_font_measure(font(), txt, &(cw_text_style){ .size = fs });
    cw_rect b = { d->at.x - m.width * 0.5f - 6 * u, d->at.y - 9 * u, m.width + 12 * u, 18 * u };
    cw_r2d_round_rect(b, 9 * u, 8, cw_rgba(0.03f, 0.04f, 0.08f, 0.92f * a));
    cw_color c = d->color;
    c.a *= a;
    cw_r2d_round_rect_lines(b, 9 * u, 1.5f * u, 8, c);
    text(txt, d->at.x, b.y + 0.5f * u, fs, c, CW_ALIGN_CENTER);
  }
  view_stage = 15;
  /* the choice in progress */
  bool human = g->turn >= 0 && !kg_seat_cpu(g, g->turn);
  if (!g->over && !g->help && !kg_busy(g) && g->turn >= 0 && !g->dirty) {
    int t = kg_cursor(g);
    char line[220];
    for (int i = 0; i < g->ntargets; i++) {
      const kg_target *tt = &g->target[i];
      if (tt->kind != KG_T_OPTION && tt->kind != KG_T_COUNT) continue;
      cw_vec2 p = to_screen(tt->at);
      bool on = i == t;
      char lab[48];
      int kind = bk_move_field(tt->move, 0);
      if (tt->kind == KG_T_COUNT) {
        snprintf(lab, sizeof lab, "%d", tt->value);
        cw_r2d_circle(p, 15 * u, 20, on ? cw_rgba(0.98f, 0.8f, 0.42f, 0.96f) : cw_rgba(0.05f, 0.07f, 0.12f, 0.88f));
        text(lab, p.x, p.y - 11 * u, 18 * u, on ? cw_rgba(0.1f, 0.07f, 0.02f, 1) : cw_rgba(0.95f, 0.95f, 1, 1), CW_ALIGN_CENTER);
        continue;
      }
      if (kind == KC_M_RAISE) snprintf(lab, sizeof lab, "Bid $%d", s->auc_bid + KC_RAISES[bk_move_field(tt->move, 1)]);
      else if (kind == KC_M_DROP) snprintf(lab, sizeof lab, "Drop out");
      else if (kind == KC_M_PASS) snprintf(lab, sizeof lab, "Pass");
      else snprintf(lab, sizeof lab, "Done");
      float bw = 150 * u, bh = 40 * u;
      cw_r2d_round_rect((cw_rect){ p.x - bw / 2, p.y - bh / 2, bw, bh }, 10 * u, 6, on ? cw_rgba(0.98f, 0.8f, 0.42f, 0.96f) : cw_rgba(0.05f, 0.07f, 0.12f, 0.88f));
      cw_r2d_round_rect_lines((cw_rect){ p.x - bw / 2, p.y - bh / 2, bw, bh }, 10 * u, 2, 6, cw_rgba(0.95f, 0.72f, 0.35f, 0.6f));
      text(lab, p.x, p.y - 12 * u, 20 * u, on ? cw_rgba(0.1f, 0.07f, 0.02f, 1) : cw_rgba(1, 0.96f, 0.88f, 1), CW_ALIGN_CENTER);
    }
    char info[220] = "";
    if (t >= 0 && t < g->ntargets) target_line(g, &g->target[t], info, sizeof info);
    snprintf(line, sizeof line, "%s%s %s", kg_seat_name(g->turn), human ? ", your turn:" : " is choosing:", info);
    panel(sz.x * 0.5f - 500 * u, 100 * u, 1000 * u, 30 * u, 1);
    cw_r2d_round_rect_lines((cw_rect){ sz.x * 0.5f - 500 * u, 100 * u, 1000 * u, 30 * u }, 5 * u, 2 * u, 6, rgba(SEAT_RGBA[g->turn]));
    text(line, sz.x * 0.5f, 104 * u, 17 * u, rgba(0xf0f4faff), CW_ALIGN_CENTER);
    if (human) {
      const char *pr = g->mode == UI_COUNT ? "{left|right} how many    {choose} buy    {back} another fuel    {how_to_play} how to play"
                                           : "{left|right} {up|down} choose    {choose} confirm    {how_to_play} how to play";
      cw_prompt_text(font(), pr, g->turn, cw_v2(sz.x * 0.5f, sz.y - 30 * u), 18 * u, cw_rgba(1, 0.97f, 0.92f, 0.95f), CW_ALIGN_CENTER);
    }
  }
  view_stage = 16;
  /* the final standings */
  if (g->over) {
    float pw = 720 * u, ph = (150 + 46 * n) * u, px = sz.x * 0.5f - pw * 0.5f, py = sz.y * 0.5f - ph * 0.5f;
    float a = clampf(g->over_ticks / 30.0f, 0, 1);
    panel(px, py, pw, ph, a);
    panel(px, py, pw, ph, a); /* twice: the table must not show through the standings */
    int win = kc_winner(s);
    char line[160];
    snprintf(line, sizeof line, "%s lights up the country", kg_seat_name(win));
    text(line, sz.x * 0.5f, py + 18 * u, 32 * u, rgba(SEAT_RGBA[win]), CW_ALIGN_CENTER);
    text("cities lit", px + pw * 0.48f, py + 68 * u, 16 * u, rgba(0xb8c4d4ff), CW_ALIGN_CENTER);
    text("cities", px + pw * 0.66f, py + 68 * u, 16 * u, rgba(0xb8c4d4ff), CW_ALIGN_CENTER);
    text("money", px + pw * 0.84f, py + 68 * u, 16 * u, rgba(0xb8c4d4ff), CW_ALIGN_CENTER);
    for (int i = 0; i < n; i++) {
      const kc_player *p = &s->p[i];
      float y = py + (98 + 46 * i) * u;
      text(kg_seat_name(i), px + 40 * u, y, 24 * u, rgba(SEAT_RGBA[i]), CW_ALIGN_LEFT);
      snprintf(line, sizeof line, "%d", p->powered);
      text(line, px + pw * 0.48f, y, 24 * u, rgba(0xfff0d0ff), CW_ALIGN_CENTER);
      snprintf(line, sizeof line, "%d", p->ncity);
      text(line, px + pw * 0.66f, y, 24 * u, rgba(0xfff0d0ff), CW_ALIGN_CENTER);
      snprintf(line, sizeof line, "$%d", p->money);
      text(line, px + pw * 0.84f, y, 24 * u, rgba(0xfff0d0ff), CW_ALIGN_CENTER);
    }
    cw_prompt_text(font(), "{choose} back to the title", -1, cw_v2(sz.x * 0.5f, py + ph - 36 * u), 18 * u, cw_rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER);
  }
  if (g->help) view_help(sz, true);
}

void view_help(cw_vec2 sz, bool close_prompt) {
  float u = sz.y / 720.0f;
  float pw = 1140 * u, ph = 470 * u, px = sz.x * 0.5f - pw * 0.5f, py = sz.y * 0.5f - ph * 0.5f;
  panel(px, py, pw, ph, 1);
  panel(px, py, pw, ph, 1);
  for (unsigned i = 0; i < sizeof HELP / sizeof HELP[0]; i++)
    text(HELP[i], px + 28 * u, py + (20 + i * 36) * u, (i ? 17 : 26) * u, i ? rgba(0xe8eef6ff) : rgba(0xffd890ff), CW_ALIGN_LEFT);
  if (close_prompt) cw_prompt_text(font(), "{choose} close", -1, cw_v2(sz.x * 0.5f, py + ph - 34 * u), 18 * u, cw_rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
}
