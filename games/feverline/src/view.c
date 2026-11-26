/* Fever Line: the map table at night and the HUD over it. See view.h. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_view.h"
#include "cartwheel/audio.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "layout.h"
#include "three.h"

/* the fevers' colours and the agents' (tools/make-textures.mjs paints with the same) */
static const uint32_t FEVER_HEX[FV_NCOL] = { 0xff6a2a, 0x2ad4e0, 0x7ae04a, 0xb46aff };
static const uint32_t FEVER_RGBA[FV_NCOL] = { 0xff7a3aff, 0x4ae0ecff, 0x8ae85aff, 0xc080ffff };
static const uint32_t ROLE_HEX[FV_NROLES] = { 0xe9e2d0, 0xe8c04a, 0xf07ab0, 0xc8844a, 0x8fb0d8 };
static const uint32_t ROLE_RGBA[FV_NROLES] = { 0xf2ecdcff, 0xf0c850ff, 0xf890c0ff, 0xe09a60ff, 0xa0c0e8ff };
static const char *const COL_NAMES[FV_NCOL] = { "Ember", "Tide", "Moss", "Dusk" };
/* art.png cells (256 px): 0..3 fever emblems, 4 the surge, 5 the clinic, 8..12 the role portraits */
#define ART_SURGE 4
#define ART_CLINIC 5
#define ART_ROLE0 8
/* card faces */
/* card faces: a city card 1 + map * 64 + city, a fever card 1000 + map * 64 + city (the same city index on
 * another map is another city), the surge 3000 */
#define FACE_CITY0 1
#define FACE_FEVER0 1000
#define FACE_SURGE 3000
#define FACE_PBACK BK_FACE_BACK
#define FACE_FBACK (BK_FACE_BACK + 1)
#define RIPPLES 12

typedef struct glide {
  t3_vec3 pos, from, to;
  float t, dur, arc;
  bool moving, placed;
} glide;

static struct {
  cw_tree *tree;
  cw_node world, camera, root;
  /* pieces */
  cw_node city[FV_NCITY], halo[FV_NCITY];
  t3_material *city_mat[FV_NCOL], *board_mat;
  t3_geometry *stone_geo;
  int map_shown;
  t3_material *halo_mat[FV_NCITY];
  float halo_a[FV_NCITY];
  cw_node cube[FV_NCITY][FV_NCOL][3];
  float cube_t[FV_NCITY][FV_NCOL][3]; /* 0 gone .. 1 in place; < 0 waiting to drop */
  bool cube_on[FV_NCITY][FV_NCOL][3];
  cw_node clinic[FV_NCITY];
  float clinic_t[FV_NCITY];
  cw_node pawn[FV_MAX_R];
  glide pawn_g[FV_MAX_R];
  cw_node ghost;
  cw_node ob_mark, rate_mark;
  glide ob_g, rate_g;
  cw_node pdeck, fdeck, pdisc, fdisc, flip;
  int pdisc_face, fdisc_face;
  float flip_t;
  int flip_city;
  cw_node ripple[RIPPLES];
  t3_material *ripple_mat[RIPPLES];
  float ripple_t[RIPPLES];
  int ripple_city[RIPPLES];
  cw_node marker;
  float marker_bob;
  t3_material *cube_mat[FV_NCOL], *role_mat[FV_NROLES];
  /* 2D */
  cw_texture art;
  int roles;
  int beat_seen;
  char banner[160];
  uint32_t banner_rgba;
  float banner_t;
  /* a card flying from the deck to an agent's hand panel */
  int fly_card, fly_role;
  float fly_t;
  int moving;
  float time;
  cw_sound snd_tick, snd_drop, snd_chime, snd_boom;
  bool sound_ok;
} V;

/* ── helpers ─────────────────────────────────────────────────────────── */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 lerp3(t3_vec3 a, t3_vec3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static void show(cw_node n, bool on) { cw_node_set(n, "visible", cw_val_bool(on)); }
static cw_color rgba(uint32_t h) { return cw_hex(h); }
static cw_color with_a(cw_color c, float a) { c.a *= a; return c; }

/* a flat plane's uv v runs from the near edge (0) to the far edge (1) once laid on the table; texture rows run
 * from the image's top, which the art draws as the far side: flip v so the picture lies as drawn */
static void flip_v(t3_geometry *g) {
  t3_attribute *uv = g->attributes[T3_ATTR_UV];
  float *f = (float *)uv->array;
  for (int i = 0; i < uv->count; i++) f[i * 2 + 1] = 1.0f - f[i * 2 + 1];
}

/* a picture is colour (sRGB), said before its first load: a PNG would load as data */
static t3_texture *texture(const char *path) {
  if (!cw_res_texture_set_srgb(path, true)) CW_FATAL("feverline: %s was loaded as data before it could be said to be colour", path);
  cw_res r = cw_res_load(path, CW_RES_TEXTURE);
  if (cw_res_wait(r) != CW_RES_STATE_READY) CW_FATAL("feverline: texture %s did not load", path);
  t3_texture *t = cw_res_texture(r);
  if (!t) CW_FATAL("feverline: %s is not a texture", path);
  if (t->color_space != T3_SRGB_COLOR_SPACE) CW_FATAL("feverline: %s did not load as a colour picture", path);
  return t;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_geometry *geo, t3_material *mat, bool cast, bool receive) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  t3_mesh *m = t3_mesh_new(geo, mat);
  m->base.cast_shadow = cast;
  m->base.receive_shadow = receive;
  if (!cw_mesh3d_set_procedural(n, m)) CW_FATAL("feverline: could not attach the mesh of %s", name);
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
static t3_material *glow(uint32_t rgb, float base, float energy) {
  t3_material *m = t3_mesh_standard_material_new(rgb);
  m->roughness = 0.5f;
  m->color = (t3_color){ ((rgb >> 16) & 255) / 255.0f * base, ((rgb >> 8) & 255) / 255.0f * base, (rgb & 255) / 255.0f * base };
  m->emissive = (t3_color){ ((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f };
  m->emissive_intensity = energy;
  return m;
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
  float u = clampf(g->t / g->dur, 0, 1), e = ease(u);
  g->pos = lerp3(g->from, g->to, e);
  g->pos.y += g->arc * sinf(u * 3.14159f);
  if (u >= 1) {
    g->moving = false;
    g->pos = g->to;
    return true;
  }
  V.moving++;
  return false;
}

cw_vec2 view_to_screen(t3_vec3 p) {
  cw_vec2 sz = cw_r2d_canvas_size();
  return cw_camera3d_world_to_screen(V.camera, (cw_rect){ 0, 0, sz.x, sz.y }, p, NULL);
}

/* ── sounds (synthesised) ────────────────────────────────────────────── */

static cw_sound synth(int kind) {
  const int rate = 44100;
  int n = rate * (kind == 2 ? 14 : kind == 3 ? 12 : 3) / 10;
  float *pcm = calloc((size_t)n, sizeof *pcm);
  uint32_t r = 7919u + (uint32_t)kind * 104729u;
  for (int i = 0; i < n; i++) {
    float t = (float)i / rate, v;
    r = r * 1664525u + 1013904223u;
    float noise = ((r >> 9) / 4194304.0f) - 1.0f;
    switch (kind) {
    case 0: v = sinf(6.2832f * 660 * t) * expf(-t * 40) * 0.4f; break;                                       /* tick: a pawn steps */
    case 1: v = (sinf(6.2832f * 980 * t) * 0.5f + noise * 0.3f) * expf(-t * 55) * 0.6f; break;              /* glass cube set down */
    case 2: v = (sinf(6.2832f * 523 * t) * 0.4f + sinf(6.2832f * 659 * t) * 0.3f + sinf(6.2832f * 784 * t) * 0.3f) * expf(-t * 2.2f) * fminf(1, t * 60); break; /* a cure */
    default: v = (sinf(6.2832f * (70 + 30 * expf(-t * 6)) * t) * 0.8f + noise * 0.25f * expf(-t * 8)) * expf(-t * 3.0f) * fminf(1, t * 40); /* an outbreak: a low swell */
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

/* ── 2D art ──────────────────────────────────────────────────────────── */

static void art_cell(int cell, cw_vec2 centre, float size, float alpha) {
  cw_sprite sp = { .texture = V.art, .src = { (float)(cell % 8) * 256.0f, (float)(cell / 8) * 256.0f, 256, 256 }, .pos = centre,
                   .size = { size, size }, .pivot = { 0.5f, 0.5f }, .color = cw_rgba(1, 1, 1, alpha) };
  cw_r2d_sprite(&sp);
}

static cw_font font(void) { return cw_ui_font_sans(); }
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_metrics m = cw_font_measure(font(), s, &(cw_text_style){ .size = size });
  float ox = al == CW_ALIGN_CENTER ? m.width * 0.5f : al == CW_ALIGN_RIGHT ? m.width : 0;
  bool dark = c.r + c.g + c.b < 0.9f; /* dark words on a light plate need no outline */
  cw_r2d_text(font(), s, cw_v2(x - ox, y), &(cw_text_style){ .size = size, .color = c, .outline_width = dark ? 0 : size * 0.09f,
                                                              .outline_color = cw_rgba(0.01f, 0.02f, 0.05f, 0.9f * c.a) });
}
static float text_w(const char *s, float size) { return cw_font_measure(font(), s, &(cw_text_style){ .size = size }).width; }

/* the card painter: city cards, fever cards, the surge and the two backs, drawn once each */
static void paint(int face, cw_vec2 px, void *user) {
  (void)user;
  float w = px.x, h = px.y;
  if (face >= BK_FACE_BACK) {
    bool fever = face == FACE_FBACK;
    cw_r2d_rect(cw_rect_make(0, 0, w, h), fever ? cw_hex(0x22140eff) : cw_hex(0x142a4cff));
    cw_color ln = fever ? cw_hex(0x6a3424ff) : cw_hex(0x2a4a7aff);
    for (int i = -8; i < 16; i++) cw_r2d_line(cw_v2((float)i * w / 8, 0), cw_v2((float)i * w / 8 + h, h), 2, ln);
    for (int i = -8; i < 16; i++) cw_r2d_line(cw_v2((float)i * w / 8, h), cw_v2((float)i * w / 8 + h, 0), 2, ln);
    cw_r2d_rect_lines(cw_rect_make(w * 0.05f, w * 0.05f, w * 0.9f, h - w * 0.1f), w * 0.025f, cw_hex(0xe0c88aff));
    cw_r2d_circle(cw_v2(w * 0.5f, h * 0.5f), w * 0.3f, 40, fever ? cw_hex(0x22140eff) : cw_hex(0x142a4cff));
    art_cell(fever ? ART_SURGE : ART_CLINIC, cw_v2(w * 0.5f, h * 0.5f), w * 0.58f, 1);
    return;
  }
  cw_text_style st = { .size = h * 0.085f, .color = cw_hex(0xf6efdfff), .fixed_size = true, .own_backdrop = true };
  if (face == FACE_SURGE) {
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0x2a0c0aff));
    cw_r2d_rect_lines(cw_rect_make(w * 0.05f, w * 0.05f, w * 0.9f, h - w * 0.1f), w * 0.03f, cw_hex(0xff6a50ff));
    art_cell(ART_SURGE, cw_v2(w * 0.5f, h * 0.42f), w * 0.7f, 1);
    st.size = h * 0.11f;
    cw_text_metrics m = cw_font_measure(font(), "SURGE", &st);
    cw_r2d_text(font(), "SURGE", cw_v2((w - m.width) * 0.5f, h * 0.74f), &st);
    return;
  }
  bool fever = face >= FACE_FEVER0;
  int id = fever ? face - FACE_FEVER0 : face - FACE_CITY0, map = id / 64, city = id % 64;
  const fv_city *fc_city = &FV_MAPS[map].city[city];
  int col = fc_city->colour;
  cw_color fc = rgba(FEVER_RGBA[col]);
  cw_r2d_rect(cw_rect_make(0, 0, w, h), fever ? cw_hex(0x1a1210ff) : cw_hex(0x101826ff));
  cw_r2d_rect(cw_rect_make(0, 0, w, h * 0.2f), with_a(fc, 0.85f));
  cw_r2d_rect_lines(cw_rect_make(w * 0.04f, w * 0.04f, w * 0.92f, h - w * 0.08f), w * 0.03f, fc);
  const char *nm = fc_city->name;
  st.size = h * 0.1f;
  st.color = cw_hex(0x0c0c10ff);
  cw_text_metrics m = cw_font_measure(font(), nm, &st);
  if (m.width > w * 0.9f) st.size *= w * 0.9f / m.width, m = cw_font_measure(font(), nm, &st);
  cw_r2d_text(font(), nm, cw_v2((w - m.width) * 0.5f, h * 0.045f), &st);
  art_cell(col, cw_v2(w * 0.5f, h * 0.52f), w * 0.66f, 1);
  st.size = h * 0.065f;
  st.color = fc;
  static const char *const FEVERS[FV_NCOL] = { "Ember fever", "Tide fever", "Moss fever", "Dusk fever" };
  const char *sub = fever ? "spreads here" : FEVERS[col];
  m = cw_font_measure(font(), sub, &st);
  cw_r2d_text(font(), sub, cw_v2((w - m.width) * 0.5f, h * 0.83f), &st);
}

/* ── pieces ──────────────────────────────────────────────────────────── */

static cw_node make_pawn(cw_node parent, const char *name, t3_material *m) {
  static t3_geometry *body, *head;
  if (!body) {
    t3_vec2 prof[] = { { 0.0f, 0.0f }, { 0.17f, 0.0f }, { 0.175f, 0.04f }, { 0.12f, 0.08f }, { 0.085f, 0.3f },
                       { 0.07f, 0.42f }, { 0.12f, 0.45f }, { 0.07f, 0.49f }, { 0.0f, 0.5f } };
    body = t3_lathe_geometry_new(prof, 9, 24, 0, 6.2832f);
    head = t3_sphere_geometry_new(0.1f, 18, 12);
  }
  cw_node g = group(parent, name);
  cw_node3d_set_scale(g, v3(1.35f, 1.35f, 1.35f));
  mesh_node(g, "Body", body, m, true, true);
  cw_node h = mesh_node(g, "Head", head, m, true, true);
  cw_node3d_set_position(h, v3(0, 0.6f, 0));
  return g;
}

static cw_node make_clinic(cw_node parent) {
  static t3_geometry *walls, *roof, *win;
  static t3_material *wm, *rm, *lit;
  if (!walls) {
    walls = t3_box_geometry_new(0.34f, 0.2f, 0.26f, 1, 1, 1);
    roof = t3_cylinder_geometry_new(0.21f, 0.21f, 0.4f, 3, 1, false);
    win = t3_box_geometry_new(0.07f, 0.07f, 0.01f, 1, 1, 1);
    wm = matte(0xe6dcc6, 0.85f);
    rm = matte(0x4a6a8a, 0.8f);
    lit = glow(0xffd27a, 0.4f, 4.0f);
  }
  cw_node g = group(parent, "Clinic");
  cw_node3d_set_scale(g, v3(1.6f, 1.6f, 1.6f));
  cw_node w = mesh_node(g, "Walls", walls, wm, true, true);
  cw_node3d_set_position(w, v3(0, 0.1f, 0));
  cw_node r = mesh_node(g, "Roof", roof, rm, true, true);
  cw_node3d_set_rotation(r, v3(0, 0, 1.5708f));
  cw_node3d_set_position(r, v3(0, 0.27f, 0));
  cw_node3d_set_scale(r, v3(0.7f, 0.95f, 0.75f));
  for (int k = 0; k < 2; k++) {
    cw_node l = mesh_node(g, "Window", win, lit, false, false);
    cw_node3d_set_position(l, v3(-0.08f + 0.16f * (float)k, 0.12f, 0.131f));
  }
  return g;
}

/* a deck: a block of cards with its back on top */
static cw_node make_deck(cw_node parent, const char *name, const char *tex, float x, float z) {
  cw_node g = group(parent, name);
  t3_material *edge = matte(0xd8ceb6, 0.9f);
  t3_geometry *box = t3_box_geometry_new(LY_CARD_W, 1.0f, LY_CARD_H, 1, 1, 1);
  cw_node b = mesh_node(g, "Block", box, edge, true, true);
  cw_node3d_set_position(b, v3(0, 0.5f, 0));
  t3_material *top = matte(0xffffff, 0.8f);
  t3_material_set_map(top, texture(tex));
  t3_geometry *pg = t3_plane_geometry_new(LY_CARD_W, LY_CARD_H, 1, 1);
  flip_v(pg);
  cw_node f = mesh_node(g, "Back", pg, top, false, true);
  cw_node3d_set_rotation(f, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(f, v3(0, 1.001f, 0));
  cw_node3d_set_position(g, v3(x, 0, z));
  t3_release(edge), t3_release(box), t3_release(top), t3_release(pg);
  return g;
}
static void deck_height(cw_node d, int cards) {
  float h = 0.02f + 0.018f * (float)cards;
  cw_node3d_set_scale(d, v3(1, h, 1));
}

/* ── the scene ───────────────────────────────────────────────────────── */

void view_init(cw_tree *t) {
  memset(&V, 0, sizeof V);
  V.tree = t;
  V.world = cw_node_get(cw_tree_root(t), "World");
  if (!cw_node_valid(V.world)) V.world = cw_tree_root(t);
  V.camera = cw_node_get(V.world, "Camera");
  if (!cw_node_valid(V.camera)) CW_FATAL("feverline: main.cwscene has no Camera");
  V.root = group(V.world, "Table");
  V.art = cw_texture_load("textures/art.png", &(cw_texture_desc){ .mipmaps = true });
  if (!V.art.id) CW_FATAL("feverline: textures/art.png did not load");
  bk_cards_init(t, paint, NULL);
  /* the table */
  t3_texture *tt = texture("textures/table.png");
  tt->wrap_s = tt->wrap_t = T3_REPEAT;
  t3_material *wood = matte(0xffffff, 0.65f);
  t3_material_set_map(wood, tt);
  t3_geometry *tg = t3_plane_geometry_new(80, 50, 1, 1);
  {
    float *f = (float *)tg->attributes[T3_ATTR_UV]->array;
    for (int i = 0; i < tg->attributes[T3_ATTR_UV]->count * 2; i++) f[i] *= 8;
  }
  cw_node table = mesh_node(V.root, "TableTop", tg, wood, false, true);
  cw_node3d_set_rotation(table, v3(-1.5708f, 0, 0));
  t3_release(tg), t3_release(wood);
  /* the map board: a slab, its painted face on top */
  t3_material *edge = matte(0x15110c, 0.7f);
  t3_geometry *bg = t3_box_geometry_new(LY_BOARD_W + 0.3f, LY_BOARD_Y - 0.01f, LY_BOARD_D + 0.3f, 1, 1, 1);
  cw_node slab = mesh_node(V.root, "BoardSlab", bg, edge, true, true);
  cw_node3d_set_position(slab, v3(0, (LY_BOARD_Y - 0.01f) * 0.5f, 0));
  t3_release(bg);
  t3_material *paint_m = matte(0xffffff, 0.85f);
  t3_material_set_map(paint_m, texture("textures/board_world.png"));
  V.board_mat = paint_m; /* the map's picture goes on it (apply_map) */
  t3_geometry *pg = t3_plane_geometry_new(LY_BOARD_W, LY_BOARD_D, 1, 1);
  flip_v(pg);
  cw_node face = mesh_node(V.root, "BoardFace", pg, paint_m, false, true);
  cw_node3d_set_rotation(face, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(face, v3(0, LY_BOARD_Y, 0));
  t3_release(pg);
  /* the left wing: a tray holding the two tracks */
  t3_geometry *tray = t3_box_geometry_new(3.4f, 0.1f, 6.4f, 1, 1, 1);
  cw_node tr = mesh_node(V.root, "Tray", tray, edge, true, true);
  cw_node3d_set_position(tr, v3((LY_OB_X + LY_RATE_X) * 0.5f, 0.05f, LY_TRACK_Z0 + 2.6f));
  t3_release(tray);
  t3_geometry *hole = t3_cylinder_geometry_new(0.24f, 0.24f, 0.02f, 24, 1, false);
  t3_material *obh = matte(0x4a1a14, 0.8f), *rth = matte(0x123a40, 0.8f);
  for (int k = 0; k <= FV_MAX_OUTBREAKS; k++) {
    cw_node h = mesh_node(V.root, "ObSlot", hole, obh, false, true);
    t3_vec3 p = ly_ob_slot(k);
    cw_node3d_set_position(h, v3(p.x, 0.105f, p.z));
  }
  for (int k = 0; k < FV_NRATES; k++) {
    cw_node h = mesh_node(V.root, "RateSlot", hole, rth, false, true);
    t3_vec3 p = ly_rate_slot(k);
    cw_node3d_set_position(h, v3(p.x, 0.105f, p.z));
  }
  t3_release(hole), t3_release(obh), t3_release(rth);
  t3_geometry *tok = t3_cylinder_geometry_new(0.21f, 0.22f, 0.1f, 28, 1, false);
  t3_material *obm = glow(0xff4a30, 0.6f, 0.8f), *rtm = glow(0x40e0ff, 0.6f, 0.7f);
  V.ob_mark = mesh_node(V.root, "OutbreakMarker", tok, obm, true, true);
  V.rate_mark = mesh_node(V.root, "RateMarker", tok, rtm, true, true);
  t3_release(tok), t3_release(obm), t3_release(rtm), t3_release(edge);
  /* the decks and their discard piles */
  V.pdeck = make_deck(V.root, "CityDeck", "textures/pdeck.png", LY_PDECK_X, LY_DECK_Z);
  V.fdeck = make_deck(V.root, "FeverDeck", "textures/fdeck.png", LY_FDECK_X, LY_DECK_Z);
  bk_card_desc cd = { .width = LY_CARD_W, .height = LY_CARD_H, .thickness = 0.02f, .corner = 0.08f, .front = FACE_CITY0, .back = FACE_PBACK, .face_px = 320 };
  V.pdisc = bk_card_new(V.root, "CityDiscard", &cd);
  cw_node3d_set_position(V.pdisc, v3(LY_PDECK_X, 0.03f, LY_DISCARD_Z));
  cd.back = FACE_FBACK, cd.front = FACE_FEVER0;
  V.fdisc = bk_card_new(V.root, "FeverDiscard", &cd);
  cw_node3d_set_position(V.fdisc, v3(LY_FDECK_X, 0.03f, LY_DISCARD_Z));
  V.flip = bk_card_new(V.root, "FeverFlip", &cd);
  show(V.pdisc, false), show(V.fdisc, false), show(V.flip, false);
  V.pdisc_face = V.fdisc_face = -1;
  /* cities: a lit stone in each socket, a halo for danger */
  t3_geometry *stone = t3_cylinder_geometry_new(0.19f, 0.22f, 0.08f, 28, 1, false);
  t3_geometry *halo = t3_ring_geometry_new(0.3f, 0.36f, 40, 1);
  t3_material *cm[FV_NCOL], *hm = t3_mesh_basic_material_new(0xff3a20);
  hm->transparent = true;
  hm->opacity = 0;
  hm->depth_write = false;
  hm->blending = 2;
  for (int c = 0; c < FV_NCOL; c++) {
    cm[c] = V.city_mat[c] = glow(FEVER_HEX[c], 0.3f, 0.45f);
    V.cube_mat[c] = glow(FEVER_HEX[c], 0.75f, 0.75f);
  }
  t3_geometry *cube = t3_box_geometry_new(LY_CUBE, LY_CUBE, LY_CUBE, 1, 1, 1);
  V.stone_geo = stone;
  V.map_shown = -1;
  for (int i = 0; i < FV_NCITY; i++) {
    t3_material *hmi = t3_mesh_basic_material_new(0xff4a2a); /* each halo eases on its own */
    hmi->transparent = true, hmi->opacity = 0, hmi->depth_write = false, hmi->blending = 2;
    V.halo[i] = mesh_node(V.root, "Halo", halo, hmi, false, false);
    V.halo_mat[i] = hmi; /* kept: its opacity eases */
    cw_node3d_set_rotation(V.halo[i], v3(-1.5708f, 0, 0));
    show(V.halo[i], false);
    for (int c = 0; c < FV_NCOL; c++)
      for (int k = 0; k < 3; k++) {
        V.cube[i][c][k] = mesh_node(V.root, "Cube", cube, V.cube_mat[c], true, true);
        show(V.cube[i][c][k], false);
      }
    V.clinic[i] = make_clinic(V.root);
    show(V.clinic[i], false);
  }
  (void)cm;
  t3_release(hm), t3_release(halo), t3_release(cube);
  /* outbreak ripples: rings that swell and fade */
  t3_geometry *rg = t3_ring_geometry_new(0.82f, 1.0f, 56, 1);
  for (int i = 0; i < RIPPLES; i++) {
    V.ripple_mat[i] = t3_mesh_basic_material_new(0xff5a30);
    V.ripple_mat[i]->transparent = true, V.ripple_mat[i]->opacity = 0, V.ripple_mat[i]->depth_write = false, V.ripple_mat[i]->blending = 2;
    V.ripple[i] = mesh_node(V.root, "Ripple", rg, V.ripple_mat[i], false, false);
    cw_node3d_set_rotation(V.ripple[i], v3(-1.5708f, 0, 0));
    show(V.ripple[i], false);
    V.ripple_t[i] = -1;
  }
  t3_release(rg);
  /* agents' materials, the cursor marker */
  for (int r = 0; r < FV_NROLES; r++) V.role_mat[r] = matte(ROLE_HEX[r], 0.45f);
  t3_material *mk = glow(0xfff0c0, 0.4f, 1.6f);
  t3_geometry *tor = t3_torus_geometry_new(0.4f, 0.035f, 10, 40, 6.2832f);
  V.marker = mesh_node(V.root, "Marker", tor, mk, false, false);
  cw_node3d_set_rotation(V.marker, v3(1.5708f, 0, 0));
  show(V.marker, false);
  t3_release(tor), t3_release(mk);
  V.snd_tick = synth(0);
  V.snd_drop = synth(1);
  V.snd_chime = synth(2);
  V.snd_boom = synth(3);
  V.sound_ok = true;
}

/* lay the table out for a map: its picture on the board, a lit stone in each city's colour, the halos and clinics
 * in place (the cubes find their places each frame) */
static void apply_map(int map) {
  const fv_map *m = &FV_MAPS[map];
  char path[64];
  snprintf(path, sizeof path, "textures/board_%s.png", m->key);
  t3_material_set_map(V.board_mat, texture(path));
  for (int i = 0; i < FV_NCITY; i++) {
    if (cw_node_valid(V.city[i])) cw_node_free(V.city[i]), V.city[i] = (cw_node){ 0 };
    show(V.halo[i], false), show(V.clinic[i], false);
    for (int c = 0; c < FV_NCOL; c++)
      for (int k = 0; k < 3; k++) show(V.cube[i][c][k], false), V.cube_on[i][c][k] = false, V.cube_t[i][c][k] = 0;
    if (i >= m->ncity) continue;
    const fv_city *c = &m->city[i];
    V.city[i] = mesh_node(V.root, "City", V.stone_geo, V.city_mat[c->colour], true, true);
    cw_node3d_set_position(V.city[i], v3(c->x, LY_BOARD_Y + 0.04f, c->z));
    cw_node3d_set_position(V.halo[i], v3(c->x, LY_BOARD_Y + 0.012f, c->z));
    cw_node3d_set_position(V.clinic[i], v3(c->x - 0.56f, LY_BOARD_Y, c->z + 0.02f));
  }
  V.map_shown = map;
}

static int card_face(int city) { return FACE_CITY0 + V.map_shown * 64 + city; }
static int fever_face(int city) { return FACE_FEVER0 + V.map_shown * 64 + city; }

void view_start(const tg_game *g) {
  for (int r = 0; r < FV_MAX_R; r++)
    if (cw_node_valid(V.pawn[r])) cw_node_free(V.pawn[r]), V.pawn[r] = (cw_node){ 0 };
  if (cw_node_valid(V.ghost)) cw_node_free(V.ghost), V.ghost = (cw_node){ 0 };
  memset(V.pawn_g, 0, sizeof V.pawn_g);
  memset(V.cube_t, 0, sizeof V.cube_t);
  memset(V.cube_on, 0, sizeof V.cube_on);
  memset(V.clinic_t, 0, sizeof V.clinic_t);
  memset(V.halo_a, 0, sizeof V.halo_a);
  V.ob_g = V.rate_g = (glide){ 0 };
  V.roles = 0;
  V.beat_seen = g->beat.serial;
  V.banner[0] = 0;
  V.fly_card = -1;
  V.flip_t = -1;
  V.pdisc_face = V.fdisc_face = -1;
  for (int i = 0; i < RIPPLES; i++) V.ripple_t[i] = -1, show(V.ripple[i], false);
  for (int i = 0; i < FV_N; i++) {
    show(V.clinic[i], false), show(V.halo[i], false);
    for (int c = 0; c < FV_NCOL; c++)
      for (int k = 0; k < 3; k++) show(V.cube[i][c][k], false);
  }
  show(V.pdisc, false), show(V.fdisc, false), show(V.flip, false);
  /* the camera: the whole map, the tracks and the decks, from the players' side */
  cw_node3d_set_position(V.camera, v3(0.6f, 27.0f, 15.5f));
  cw_node3d_look_at(V.camera, v3(0.6f, 0, 1.35f), v3(0, 1, 0));
}

/* pawns appear once the match has its roles */
static void ensure_pawns(const tg_game *g) {
  if (V.roles == g->roles || !g->match) return;
  for (int r = 0; r < FV_MAX_R; r++)
    if (cw_node_valid(V.pawn[r])) cw_node_free(V.pawn[r]), V.pawn[r] = (cw_node){ 0 };
  const fv_state *s = &g->shown;
  for (int r = 0; r < g->roles; r++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Pawn%d", r);
    V.pawn[r] = make_pawn(V.root, nm, V.role_mat[s->role[r]]);
    V.pawn_g[r] = (glide){ 0 };
  }
  V.ghost = make_pawn(V.root, "Ghost", V.role_mat[0]);
  /* 0.6: with no tint toward white (the old ghost had one), a pale pawn at 0.45 all but vanished on the night map */
  if (!bk_ghost_set(V.ghost, 0.6f)) CW_FATAL("feverline: the ghost pawn has no mesh");
  show(V.ghost, false);
  V.roles = g->roles;
}

/* ── following the table ─────────────────────────────────────────────── */

static void banner(const char *msg, uint32_t col) {
  snprintf(V.banner, sizeof V.banner, "%s", msg);
  V.banner_rgba = col;
  V.banner_t = 0;
}

static void ripple_at(int city, int colour, float delay) {
  for (int i = 0; i < RIPPLES; i++)
    if (V.ripple_t[i] < 0) {
      V.ripple_t[i] = -delay - 0.0001f;
      V.ripple_city[i] = city;
      uint32_t h = FEVER_HEX[colour];
      V.ripple_mat[i]->color = (t3_color){ ((h >> 16) & 255) / 255.0f * 1.6f, ((h >> 8) & 255) / 255.0f * 1.6f, (h & 255) / 255.0f * 1.6f };
      cw_node3d_set_position(V.ripple[i], v3(FV_CITIES[city].x, LY_BOARD_Y + 0.03f, FV_CITIES[city].z));
      return;
    }
}

static void on_beat(const tg_game *g) {
  const tg_beat *b = &g->beat;
  const fv_state *a = &b->before, *z = &b->after;
  int kind = bk_move_field(b->move, 0), x = bk_move_field(b->move, 1);
  char msg[160];
  /* pawns: a drive glides along the road, a flight arcs high */
  for (int r = 0; r < g->roles; r++) {
    if (a->loc[r] == z->loc[r]) continue;
    bool air = kind == FV_M_FLY || kind == FV_M_CHARTER || kind == FV_M_SHUTTLE;
    glide_to(&V.pawn_g[r], ly_pawn(z->loc[r], r), air ? 0.7f : 0.42f, air ? 1.6f : 0.12f, 0);
    sfx(V.snd_tick, 0.5f, air ? 0.8f : 1.0f);
  }
  /* outbreaks: a ripple per city of the chain, one after another */
  for (int i = 0; i < z->ob_n; i++) {
    int col = kind == FV_M_FLIP ? fv_colour(x) : 0;
    ripple_at(z->ob_city[i], col, 0.35f * (float)i);
  }
  if (z->ob_n) {
    if (z->ob_n == 1) snprintf(msg, sizeof msg, "Outbreak in %s: every city on its roads gets a cube", FV_CITIES[z->ob_city[0]].name);
    else snprintf(msg, sizeof msg, "Outbreak chain from %s: %d outbreaks", FV_CITIES[z->ob_city[0]].name, z->ob_n);
    banner(msg, 0xff8a60ff);
    sfx(V.snd_boom, 0.9f, 1.0f);
    cw_input_rumble(g->ctl[g->turn >= 0 ? g->turn : 0], 0.5f, 0.25f, 350);
  }
  switch (kind) {
  case FV_M_CURE:
    snprintf(msg, sizeof msg, "%s fever cured!", COL_NAMES[x]);
    banner(msg, FEVER_RGBA[x]);
    sfx(V.snd_chime, 0.8f, 1.0f);
    break;
  case FV_M_BUILD:
    V.clinic_t[z->loc[z->cur]] = 0.001f;
    sfx(V.snd_drop, 0.6f, 0.7f);
    break;
  case FV_M_DRAW:
    if (fv_is_calm(x)) {
      banner("A calm card: the next spread flips one card fewer", 0x9fe8c0ff);
      sfx(V.snd_chime, 0.4f, 1.5f);
    } else if (fv_is_surge(x)) {
      snprintf(msg, sizeof msg, "Surge! The spread rate rises to %d cards a turn", fv_rate(z));
      banner(msg, 0xff6a50ff);
      bk_card_set_front(V.pdisc, FACE_SURGE);
      V.pdisc_face = FACE_SURGE;
      sfx(V.snd_boom, 0.6f, 1.4f);
    } else {
      V.fly_card = x, V.fly_role = a->cur, V.fly_t = 0;
    }
    break;
  case FV_M_FLIP:
    V.flip_city = x;
    V.flip_t = 0;
    bk_card_set_front(V.flip, fever_face(x));
    if (a->phase == FV_PH_SURGE) {
      snprintf(msg, sizeof msg, "Surge: %s gets 3 cubes, and the fever discards go back on top", FV_CITIES[x].name);
      banner(msg, 0xff6a50ff);
    }
    break;
  default: break;
  }
  if (kind == FV_M_FLY || kind == FV_M_CHARTER || kind == FV_M_BUILD || kind == FV_M_CURE || kind == FV_M_DISCARD) {
    for (int c = FV_N - 1; c >= 0; c--)
      if (a->pcard[c] != FV_PC_DISCARD && z->pcard[c] == FV_PC_DISCARD) {
        bk_card_set_front(V.pdisc, card_face(c));
        V.pdisc_face = c;
        break;
      }
  }
  (void)a;
}

static void update_cubes(const fv_state *s, float dt) {
  for (int i = 0; i < FV_N; i++)
    for (int c = 0; c < FV_NCOL; c++)
      for (int k = 0; k < 3; k++) {
        bool want = k < s->cubes[i][c];
        float *t = &V.cube_t[i][c][k];
        cw_node n = V.cube[i][c][k];
        if (want && !V.cube_on[i][c][k]) {
          V.cube_on[i][c][k] = true;
          *t = -0.08f * (float)k; /* stacked cubes drop one after another */
        }
        if (!want && V.cube_on[i][c][k]) V.cube_on[i][c][k] = false;
        if (V.cube_on[i][c][k]) {
          if (*t < 1) {
            float before = *t;
            *t += dt * 2.6f;
            if (before < 0.75f && *t >= 0.75f) sfx(V.snd_drop, 0.3f, 1.0f + 0.1f * (float)k);
            V.moving++;
          }
          float u = clampf(*t, 0, 1), e = u * u;
          t3_vec3 p = ly_cube(i, c, k);
          p.y += (1 - e) * 1.6f + (u > 0.75f ? sinf((u - 0.75f) / 0.25f * 3.14159f) * 0.05f : 0);
          show(n, *t > 0);
          cw_node3d_set_position(n, p);
          cw_node3d_set_scale(n, v3(1, 1, 1));
        } else if (*t > 0) {
          *t -= dt * 3.5f; /* a treated cube shrinks away */
          float u = clampf(*t, 0, 1);
          show(n, u > 0.02f);
          cw_node3d_set_scale(n, v3(u, u, u));
          if (u > 0.02f) V.moving++;
        } else {
          show(n, false);
        }
      }
}

void view_update(const tg_game *g, float dt) {
  V.moving = 0;
  V.time += dt;
  int want = g->match ? g->map : g->setup_map; /* the setup card shows the map it would play */
  if (!g->match) fv_use_map(want);
  if (want != V.map_shown) apply_map(want);
  if (!g->match) {
    show(V.marker, false);
    return;
  }
  ensure_pawns(g);
  const fv_state *s = &g->shown;
  if (g->beat.serial != V.beat_seen) {
    V.beat_seen = g->beat.serial;
    on_beat(g);
  }
  update_cubes(s, dt);
  /* danger halos: a city with three cubes of a fever glows red underneath, easing in and out */
  for (int i = 0; i < FV_N; i++) {
    bool danger = false;
    for (int c = 0; c < FV_NCOL; c++) danger |= s->cubes[i][c] == 3;
    float target = danger ? 0.75f : 0;
    V.halo_a[i] += (target - V.halo_a[i]) * clampf(dt * 3, 0, 1);
    bool on = V.halo_a[i] > 0.01f;
    show(V.halo[i], on);
    if (on) {
      V.halo_mat[i]->opacity = V.halo_a[i];
      float sc = 1.0f + 0.06f * sinf(V.time * 1.6f + (float)i); /* a slow breath, never a flash */
      cw_node3d_set_scale(V.halo[i], v3(sc, sc, sc));
    }
  }
  /* clinics rise out of the board when built */
  for (int i = 0; i < FV_N; i++) {
    if (!s->clinic[i]) { show(V.clinic[i], false); V.clinic_t[i] = 0; continue; }
    if (V.clinic_t[i] <= 0) V.clinic_t[i] = 1; /* there from the start */
    if (V.clinic_t[i] < 1) V.clinic_t[i] = fminf(1, V.clinic_t[i] + dt * 1.6f), V.moving++;
    float e = ease(V.clinic_t[i]);
    show(V.clinic[i], true);
    cw_node3d_set_scale(V.clinic[i], v3(1.6f, 1.6f * e, 1.6f));
  }
  /* pawns */
  for (int r = 0; r < g->roles; r++) {
    glide_to(&V.pawn_g[r], ly_pawn(s->loc[r], r), 0.42f, 0.12f, 0);
    glide_step(&V.pawn_g[r], dt);
    t3_vec3 p = V.pawn_g[r].pos;
    bool active = !g->over && r == s->cur && s->phase == FV_PH_ACT;
    p.y += active ? 0.06f + 0.04f * sinf(V.time * 3.0f) : 0;
    cw_node3d_set_position(V.pawn[r], p);
  }
  /* the track markers */
  glide_to(&V.ob_g, v3(ly_ob_slot(s->outbreaks).x, 0.16f, ly_ob_slot(s->outbreaks).z), 0.6f, 0.5f, 0);
  glide_to(&V.rate_g, v3(ly_rate_slot(s->rate_idx).x, 0.16f, ly_rate_slot(s->rate_idx).z), 0.6f, 0.5f, 0);
  glide_step(&V.ob_g, dt), glide_step(&V.rate_g, dt);
  cw_node3d_set_position(V.ob_mark, V.ob_g.pos);
  cw_node3d_set_position(V.rate_mark, V.rate_g.pos);
  /* decks and discards */
  deck_height(V.pdeck, fv_deck_left(s));
  int fd = 0, fdisc = 0;
  for (int c = 0; c < FV_N; c++) fd += s->icard[c] > 0, fdisc += s->icard[c] == 0;
  deck_height(V.fdeck, fd);
  show(V.pdisc, V.pdisc_face >= 0);
  /* the fever card flips over from the deck onto its discard pile */
  if (V.flip_t >= 0) {
    V.flip_t += dt * 1.8f;
    float u = clampf(V.flip_t, 0, 1);
    bk_pose from = { v3(LY_FDECK_X, 0.05f + 0.018f * (float)fd, LY_DECK_Z), t3_quat_identity() };
    bk_pose to = { v3(LY_FDECK_X, 0.05f, LY_DISCARD_Z), t3_quat_identity() };
    bk_pose at = { lerp3(from.pos, to.pos, ease(u)), t3_quat_identity() };
    bk_pose p = bk_card_flip_pose(at, u, 0.9f, true);
    show(V.flip, true);
    cw_node3d_set_position(V.flip, p.pos);
    cw_node3d_set_quaternion(V.flip, p.rot);
    V.moving++;
    if (u >= 1) {
      V.flip_t = -1;
      show(V.flip, false);
      bk_card_set_front(V.fdisc, fever_face(V.flip_city));
      V.fdisc_face = V.flip_city;
    }
  }
  show(V.fdisc, V.fdisc_face >= 0 && fdisc > 0);
  /* ripples: swell over 1.4 s and fade (opacity rises and falls once: no flash) */
  for (int i = 0; i < RIPPLES; i++) {
    if (V.ripple_t[i] == -1) continue;
    V.ripple_t[i] += dt;
    float t = V.ripple_t[i];
    if (t < 0) { show(V.ripple[i], false); continue; }
    float u = t / 1.4f;
    if (u >= 1) { V.ripple_t[i] = -1; show(V.ripple[i], false); continue; }
    float sc = 0.3f + 1.9f * ease(u);
    V.ripple_mat[i]->opacity = 0.85f * sinf(u * 3.14159f);
    show(V.ripple[i], true);
    cw_node3d_set_scale(V.ripple[i], v3(sc, sc, sc));
    V.moving++;
  }
  /* the cursor's marker and the ghost pawn */
  int t = tg_cursor(g);
  bool live = !g->over && !tg_busy(g) && !g->dirty && t >= 0 && t < g->ntargets && g->mode != UI_SETUP;
  bool on_city = live && g->target[t].kind == TG_CITY;
  show(V.marker, on_city);
  if (on_city) {
    V.marker_bob += dt;
    t3_vec3 at = g->target[t].at;
    cw_node3d_set_position(V.marker, v3(at.x, LY_BOARD_Y + 0.07f + 0.03f * sinf(V.marker_bob * 4), at.z));
  }
  bool person = g->turn >= 0 && !tg_role_cpu(g, g->turn);
  if (cw_node_valid(V.ghost)) {
    bool gh = on_city && (person || g->fast) && s->phase == FV_PH_ACT;
    show(V.ghost, gh);
    if (gh) cw_node3d_set_position(V.ghost, ly_pawn(g->target[t].city, s->cur));
  }
  if (V.fly_card >= 0) {
    V.fly_t += dt * 1.6f;
    if (V.fly_t >= 1) V.fly_card = -1;
  }
  V.banner_t += dt;
}

int view_moving(void) { return V.moving; }

/* ── HUD ─────────────────────────────────────────────────────────────── */

static void panel(float x, float y, float w, float h, float a) {
  /* even canvas units: whole pixels at 1x and 1.5x, so every host draws the edges alike */
  x = 2 * roundf(x * 0.5f), y = 2 * roundf(y * 0.5f), w = 2 * roundf(w * 0.5f), h = 2 * roundf(h * 0.5f);
  cw_r2d_round_rect((cw_rect){ x, y, w, h }, fminf(h * 0.18f, 12), 6, cw_rgba(0.02f, 0.035f, 0.07f, 0.84f * a));
  cw_r2d_round_rect_lines((cw_rect){ x, y, w, h }, fminf(h * 0.18f, 12), 2, 6, cw_rgba(0.55f, 0.72f, 0.85f, 0.45f * a));
}

static void agent_label(const tg_game *g, int r, char *buf, size_t cap) {
  const fv_state *s = &g->shown;
  if (tg_role_cpu(g, r)) snprintf(buf, cap, "%s, CPU %s", fv_role_name(s->role[r]), cw_shell_cpu_level_label((cw_shell_cpu_level)g->cpu_level[r]));
  else if (g->seats == 1) snprintf(buf, cap, "%s", fv_role_name(s->role[r]));
  else snprintf(buf, cap, "%s, Player %d", fv_role_name(s->role[r]), g->ctl[r] + 1);
}

/* the agents along the bottom: portrait, name, city, actions left and every card in hand */
static void hud_agents(const tg_game *g, float u) {
  const fv_state *s = &g->shown;
  cw_vec2 sz = cw_r2d_canvas_size();
  int n = g->roles;
  float gap = 10 * u, margin = 12 * u, w = (sz.x - 2 * margin - (float)(n - 1) * gap) / (float)n, h = 160 * u, y = sz.y - h - 8 * u;
  for (int r = 0; r < n; r++) {
    float x = margin + (float)r * (w + gap);
    bool turn = !g->over && (s->phase == FV_PH_DISCARD ? s->disc_role == r : s->cur == r);
    panel(x, y, w, h, 1);
    cw_color rc = rgba(ROLE_RGBA[s->role[r]]);
    if (turn) cw_r2d_round_rect_lines((cw_rect){ 2 * roundf((x - 3 * u) * 0.5f), 2 * roundf((y - 3 * u) * 0.5f), 2 * roundf((w + 6 * u) * 0.5f), 2 * roundf((h + 6 * u) * 0.5f) }, 12, 3 * u, 6, rc);
    art_cell(ART_ROLE0 + s->role[r], cw_v2(x + 34 * u, y + 34 * u), 56 * u, 1);
    char line[96];
    agent_label(g, r, line, sizeof line);
    text(line, x + 68 * u, y + 8 * u, 17 * u, rc, CW_ALIGN_LEFT);
    if (turn && s->phase == FV_PH_ACT) snprintf(line, sizeof line, "in %s, actions left %d", FV_CITIES[s->loc[r]].name, s->acts);
    else snprintf(line, sizeof line, "in %s", FV_CITIES[s->loc[r]].name);
    text(line, x + 68 * u, y + 30 * u, 15 * u, rgba(0xdce6f0ff), CW_ALIGN_LEFT);
    /* the hand: a tag per card, the fever's colour and the city's name, two or three to a row */
    int cards[FV_NCITY], k = fv_hand(s, r, cards, FV_NCITY);
    int cols = w > 380 * u ? 3 : 2;
    float tw = (w - 16 * u - (float)(cols - 1) * 6 * u) / (float)cols, th = 20 * u;
    for (int i = 0; i < k; i++) {
      float tx = x + 8 * u + (float)(i % cols) * (tw + 6 * u), ty = y + 62 * u + (float)(i / cols) * (th + 3 * u);
      int col = fv_colour(cards[i]);
      cw_r2d_round_rect((cw_rect){ tx, ty, tw, th }, 4 * u, 4, cw_rgba(0.06f, 0.08f, 0.12f, 0.95f));
      cw_r2d_round_rect((cw_rect){ tx, ty, 6 * u, th }, 2 * u, 3, rgba(FEVER_RGBA[col]));
      text(FV_CITIES[cards[i]].name, tx + 11 * u, ty + 0 * u, 15 * u, rgba(0xeef2f8ff), CW_ALIGN_LEFT);
    }
    if (k == 0) text("no cards", x + 10 * u, y + 64 * u, 15 * u, rgba(0x8898aaff), CW_ALIGN_LEFT);
    /* counts per fever, for cures: "Ember 3 of 5", right of the city on the second line */
    float cx = x + w - 10 * u, stop = x + 68 * u + text_w(line, 15 * u) + 16 * u;
    for (int c = FV_NCOL - 1; c >= 0; c--) {
      int have = fv_hand_colour(s, r, c);
      if (!have || s->cured[c]) continue;
      snprintf(line, sizeof line, "%s %d of %d", COL_NAMES[c], have, fv_cure_need(s, r));
      float tw2 = text_w(line, 13 * u);
      if (cx - tw2 < stop) break;
      text(line, cx, y + 32 * u, 15 * u, rgba(FEVER_RGBA[c]), CW_ALIGN_RIGHT);
      cx -= tw2 + 16 * u;
    }
  }
  /* a drawn card flying from the deck to its agent's panel */
  if (V.fly_card >= 0 && V.fly_role < n) {
    cw_vec2 from = view_to_screen(v3(LY_PDECK_X, 0.6f, LY_DECK_Z));
    cw_vec2 to = cw_v2(margin + (float)V.fly_role * (w + gap) + w * 0.5f, y + 40 * u);
    float e = ease(V.fly_t), a = clampf((1 - V.fly_t) * 4, 0, 1);
    cw_vec2 p = cw_v2(from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e - sinf(e * 3.14159f) * 60 * u);
    int col = fv_colour(V.fly_card);
    cw_r2d_round_rect((cw_rect){ p.x - 60 * u, p.y - 16 * u, 120 * u, 32 * u }, 6 * u, 4, cw_rgba(0.06f, 0.08f, 0.12f, 0.95f * a));
    cw_r2d_round_rect((cw_rect){ p.x - 60 * u, p.y - 16 * u, 8 * u, 32 * u }, 3 * u, 3, with_a(rgba(FEVER_RGBA[col]), a));
    text(FV_CITIES[V.fly_card].name, p.x + 4 * u, p.y - 10 * u, 16 * u, cw_rgba(1, 1, 1, a), CW_ALIGN_CENTER);
  }
}

/* the top bar: outbreaks, the spread rate, the four fevers, the deck */
static void hud_top(const tg_game *g, float u) {
  const fv_state *s = &g->shown;
  cw_vec2 sz = cw_r2d_canvas_size();
  float y = 8 * u, h = 52 * u;
  char line[96];
  /* outbreaks */
  panel(12 * u, y, 250 * u, h, 1);
  snprintf(line, sizeof line, "Outbreaks %d of %d", s->outbreaks, FV_MAX_OUTBREAKS);
  text(line, 24 * u, y + 5 * u, 17 * u, rgba(0xffb090ff), CW_ALIGN_LEFT);
  for (int k = 0; k < FV_MAX_OUTBREAKS; k++)
    cw_r2d_circle(cw_v2(30 * u + (float)k * 27 * u, y + 37 * u), 8 * u, 20, k < s->outbreaks ? rgba(0xff5a3aff) : cw_rgba(0.3f, 0.2f, 0.2f, 0.8f));
  /* the spread rate */
  panel(272 * u, y, 250 * u, h, 1);
  snprintf(line, sizeof line, "Spread rate %d cards a turn", fv_rate(s));
  text(line, 284 * u, y + 5 * u, 17 * u, rgba(0x90e8f0ff), CW_ALIGN_LEFT);
  for (int k = 0; k < FV_NRATES; k++) {
    cw_vec2 c = cw_v2(292 * u + (float)k * 33 * u, y + 37 * u);
    bool on = k == s->rate_idx;
    cw_r2d_circle(c, 12 * u, 20, on ? rgba(0x40e0ffff) : cw_rgba(0.15f, 0.25f, 0.3f, 0.9f));
    char num[4];
    snprintf(num, sizeof num, "%d", FV_RATES[k]);
    text(num, c.x, c.y - 9 * u, 15 * u, on ? rgba(0x02161cff) : rgba(0xb0d0dcff), CW_ALIGN_CENTER);
  }
  /* the fevers */
  float fx = 532 * u, fw = 128 * u;
  for (int c = 0; c < FV_NCOL; c++) {
    float x = fx + (float)c * (fw + 6 * u);
    panel(x, y, fw, h, 1);
    art_cell(c, cw_v2(x + 24 * u, y + h * 0.5f), 38 * u, s->cured[c] == 2 ? 0.45f : 1);
    text(COL_NAMES[c], x + 48 * u, y + 5 * u, 16 * u, rgba(FEVER_RGBA[c]), CW_ALIGN_LEFT);
    if (s->cured[c] == 2) snprintf(line, sizeof line, "wiped out");
    else if (s->cured[c]) snprintf(line, sizeof line, "cured");
    else snprintf(line, sizeof line, "cubes left %d", s->supply[c]);
    text(line, x + 48 * u, y + 27 * u, 15 * u, s->cured[c] ? rgba(0xfff0b0ff) : rgba(0xd0dae6ff), CW_ALIGN_LEFT);
  }
  /* the deck and the turn */
  float dx = fx + 4 * (fw + 6 * u);
  panel(dx, y, sz.x - dx - 12 * u, h, 1);
  int surges = 0;
  for (int c = FV_SURGE0; c < FV_CALM0; c++) surges += s->pcard[c] >= FV_PC_DECK && s->pcard[c] < FV_PC_DISCARD;
  snprintf(line, sizeof line, "City deck %d cards", fv_deck_left(s));
  text(line, dx + 12 * u, y + 5 * u, 16 * u, rgba(0xe8eef6ff), CW_ALIGN_LEFT);
  snprintf(line, sizeof line, "surges left %d   %s, %s", surges, FV_MAPS[s->map].title, tg_setup_name(s->setup));
  text(line, dx + 12 * u, y + 27 * u, 15 * u, rgba(0xb8c4d2ff), CW_ALIGN_LEFT);
}

static void hud_labels(const tg_game *g, float u) {
  const fv_state *s = &g->shown;
  int t = tg_cursor(g);
  int hot = t >= 0 && t < g->ntargets && g->target[t].kind == TG_CITY && !tg_busy(g) ? g->target[t].city : -1;
  for (int i = 0; i < FV_N; i++) {
    cw_vec2 p = view_to_screen(v3(FV_CITIES[i].x, LY_BOARD_Y, FV_CITIES[i].z + 0.3f));
    bool big = i == hot;
    int most = 0;
    for (int c = 0; c < FV_NCOL; c++) most = s->cubes[i][c] > most ? s->cubes[i][c] : most;
    cw_color col = most == 3 ? rgba(0xffb8a0ff) : cw_rgba(0.92f, 0.94f, 0.98f, 0.78f);
    text(FV_CITIES[i].name, p.x, p.y + 2 * u, (big ? 19 : 15) * u, big ? rgba(0xfff0c0ff) : col, CW_ALIGN_CENTER);
  }
  /* the tracks' and decks' names on the left wing */
  cw_vec2 a = view_to_screen(v3(LY_OB_X, 0.1f, LY_TRACK_Z0 - 0.55f)), b = view_to_screen(v3(LY_RATE_X, 0.1f, LY_TRACK_Z0 - 0.55f));
  text("outbreaks", a.x - 14 * u, a.y - 10 * u, 15 * u, rgba(0xffb090ff), CW_ALIGN_CENTER);
  text("spread", b.x + 14 * u, b.y - 10 * u, 15 * u, rgba(0x90e8f0ff), CW_ALIGN_CENTER);
  for (int k = 0; k <= FV_MAX_OUTBREAKS; k++) {
    cw_vec2 p = view_to_screen(ly_ob_slot(k));
    char num[4];
    snprintf(num, sizeof num, "%d", k);
    text(num, p.x - 22 * u, p.y - 8 * u, 15 * u, rgba(0xd09080ff), CW_ALIGN_CENTER);
  }
  for (int k = 0; k < FV_NRATES; k++) {
    cw_vec2 p = view_to_screen(ly_rate_slot(k));
    char num[4];
    snprintf(num, sizeof num, "%d", FV_RATES[k]);
    text(num, p.x + 22 * u, p.y - 8 * u, 15 * u, rgba(0x80c8d0ff), CW_ALIGN_CENTER);
  }
  cw_vec2 d1 = view_to_screen(v3(LY_PDECK_X, 0.1f, LY_DECK_Z + LY_CARD_H * 0.5f + 0.15f)), d2 = view_to_screen(v3(LY_FDECK_X, 0.1f, LY_DECK_Z + LY_CARD_H * 0.5f + 0.15f));
  text("city deck", d1.x, d1.y, 15 * u, rgba(0xc0d0e8ff), CW_ALIGN_CENTER);
  text("fever deck", d2.x, d2.y, 15 * u, rgba(0xe8c0a8ff), CW_ALIGN_CENTER);
  /* the discard piles' top cards, named big enough to read (the card faces are small at this distance) */
  cw_vec2 e1 = view_to_screen(v3(LY_PDECK_X, 0.1f, LY_DISCARD_Z + LY_CARD_H * 0.5f + 0.1f)), e2 = view_to_screen(v3(LY_FDECK_X, 0.1f, LY_DISCARD_Z + LY_CARD_H * 0.5f + 0.1f));
  if (V.pdisc_face >= 0) text(V.pdisc_face == FACE_SURGE ? "surge" : FV_CITIES[V.pdisc_face].name, e1.x, e1.y, 15 * u, rgba(0xe8eef6ff), CW_ALIGN_CENTER);
  if (V.fdisc_face >= 0) text(FV_CITIES[V.fdisc_face].name, e2.x, e2.y, 15 * u, rgba(FEVER_RGBA[fv_colour(V.fdisc_face)]), CW_ALIGN_CENTER);
}

static const char *const HELP[] = {
  "Fever Line: how to play",
  "Four fevers spread over the map. Your team wins by curing all four, and loses at 8 outbreaks, when a fever",
  "runs out of cubes, or when the city deck runs out. Every card is face up: plan together.",
  "On your turn take 4 actions: drive along a road, fly to a city by spending its card, charter anywhere by",
  "spending the card of your city, shuttle between clinics, build a clinic (spend your city's card), treat (take a",
  "cube off your city), share (give or take your city's card with an agent beside you), or cure a fever at a clinic",
  "(5 cards of its colour on a big map, 4 or 3 on a smaller one). Then draw 2 cards, and the fever spreads.",
  "A city with 3 cubes that gets another breaks out: every city on its roads gets a cube, and it can chain.",
  "Surges raise the spread rate, put 3 cubes on a city and bring recent fever cards back; a calm card slows one spread.",
  "Each agent has a power, listed below. A cured fever is easy to treat; with no cubes left on the map it is gone.",
};

static void hud_choice(const tg_game *g, float u) {
  const fv_state *s = &g->shown;
  cw_vec2 sz = cw_r2d_canvas_size();
  int t = tg_cursor(g);
  bool person = g->mode == UI_SETUP || (g->turn >= 0 && !tg_role_cpu(g, g->turn));
  /* the plates: each where its cursor point lies */
  for (int i = 0; i < g->ntargets; i++) {
    const tg_target *tt = &g->target[i];
    if (tt->kind == TG_CITY) continue;
    char lab[96];
    tg_plate_label(g, tt, lab, sizeof lab);
    cw_vec2 p = view_to_screen(tt->at);
    bool on = i == t;
    bool chosen = tt->kind == TG_SETUP && ((tt->plate == PL_SETUP_LEVEL && tt->value == g->setup_level) || (tt->plate == PL_SETUP_AGENTS && tt->value == g->setup_agents) || (tt->plate == PL_SETUP_MAP && tt->value == g->setup_map));
    float bw = fmaxf(160 * u, text_w(lab, 17 * u) + 28 * u), bh = 36 * u;
    if (tt->kind == TG_SETUP) bw = tt->plate == PL_SETUP_MAP ? 168 * u : fmaxf(bw, 220 * u), bh = tt->plate == PL_SETUP_MAP ? 38 * u : 44 * u;
    float bx = 2 * roundf((p.x - bw * 0.5f) * 0.5f), by = 2 * roundf((p.y - bh * 0.5f) * 0.5f);
    if (bx + bw > sz.x - 6 * u) bx = sz.x - 6 * u - bw;
    cw_r2d_round_rect((cw_rect){ bx, by, bw, bh }, 9 * u, 6, on ? cw_rgba(0.95f, 0.86f, 0.55f, 0.97f) : chosen ? cw_rgba(0.15f, 0.42f, 0.5f, 0.95f) : cw_rgba(0.03f, 0.05f, 0.1f, 0.9f));
    cw_r2d_round_rect_lines((cw_rect){ bx, by, bw, bh }, 9 * u, 2, 6, cw_rgba(0.55f, 0.75f, 0.9f, 0.6f));
    int kind = bk_move_field(tt->move, 0);
    if (tt->move != BK_MOVE_NONE && (kind == FV_M_TREAT || kind == FV_M_CURE)) art_cell(bk_move_field(tt->move, 1), cw_v2(bx + 18 * u, by + bh * 0.5f), 26 * u, 1);
    if (tt->move != BK_MOVE_NONE && kind == FV_M_BUILD) art_cell(ART_CLINIC, cw_v2(bx + 18 * u, by + bh * 0.5f), 26 * u, 1);
    text(lab, bx + bw * 0.5f + (kind == FV_M_TREAT || kind == FV_M_CURE || kind == FV_M_BUILD ? 12 * u : 0), by + bh * 0.5f - 11 * u, 17 * u,
         on ? cw_rgba(0.08f, 0.06f, 0.02f, 1) : rgba(0xeef4faff), CW_ALIGN_CENTER);
  }
  /* what the highlighted target does, in a line over the agents */
  char line[200] = "";
  if (t >= 0 && t < g->ntargets && g->mode != UI_SETUP) {
    const tg_target *tt = &g->target[t];
    if (tt->move != BK_MOVE_NONE) {
      int kind = bk_move_field(tt->move, 0), a = bk_move_field(tt->move, 1);
      switch (kind) {
      case FV_M_DRIVE: snprintf(line, sizeof line, "Drive to %s along the road (1 action)", FV_CITIES[a].name); break;
      case FV_M_SHUTTLE: snprintf(line, sizeof line, "Shuttle to the clinic in %s (1 action)", FV_CITIES[a].name); break;
      case FV_M_FLY: snprintf(line, sizeof line, "Fly to %s: spends the %s card (1 action)", FV_CITIES[a].name, FV_CITIES[a].name); break;
      case FV_M_CHARTER: snprintf(line, sizeof line, "Charter to %s: spends the %s card (1 action)", FV_CITIES[a].name, FV_CITIES[s->loc[s->cur]].name); break;
      case FV_M_TREAT: snprintf(line, sizeof line, "Treat: take %s %s cube%s off %s", s->role[s->cur] == FV_MEDIC || s->cured[a] ? "every" : "one", COL_NAMES[a],
                                s->role[s->cur] == FV_MEDIC || s->cured[a] ? "" : "", FV_CITIES[s->loc[s->cur]].name); break;
      case FV_M_BUILD: snprintf(line, sizeof line, s->role[s->cur] == FV_ENGINEER ? "Build a clinic in %s (no card needed)" : "Build a clinic in %s: spends its card", FV_CITIES[s->loc[s->cur]].name); break;
      case FV_M_CURE: snprintf(line, sizeof line, "Cure %s fever: spends %d %s cards", COL_NAMES[a], fv_cure_need(s, s->cur), COL_NAMES[a]); break;
      case FV_M_REST: snprintf(line, sizeof line, "Rest: end this agent's actions now (%d left unused)", s->acts); break;
      case FV_M_DISCARD: snprintf(line, sizeof line, "Over 7 cards: discard %s", FV_CITIES[a].name); break;
      default: tg_plate_label(g, tt, line, sizeof line);
      }
    } else if (tt->plate == PL_SHARE_OPEN) snprintf(line, sizeof line, "Share: give or take a card with an agent in this city");
    else if (tt->plate == PL_BACK) snprintf(line, sizeof line, "Back to the map");
  }
  /* a stack over the agents' panels, on a dark band so it reads over the map: the agent's power, whose move it
   * is, what the highlighted target does, and the buttons */
  float top = sz.y - 168 * u, yp = top - 24 * u, yi = yp - 34 * u, yh = yi - 26 * u, yw = yh - 22 * u;
  bool acting = g->turn >= 0 && !g->over && g->mode != UI_SETUP;
  if (acting) {
    cw_r2d_round_rect((cw_rect){ 2 * roundf((sz.x * 0.5f - 500 * u) * 0.5f), 2 * roundf((yw - 6 * u) * 0.5f), 2 * roundf(500 * u), 2 * roundf((top - yw + 2 * u) * 0.5f) },
                      12 * u, 6, cw_rgba(0.01f, 0.02f, 0.05f, 0.72f));
    char pw[160];
    snprintf(pw, sizeof pw, "%s: %s", fv_role_name(s->role[g->turn]), fv_role_power(s->role[g->turn]));
    text(pw, sz.x * 0.5f, yw, 16 * u, with_a(rgba(ROLE_RGBA[s->role[g->turn]]), 0.95f), CW_ALIGN_CENTER);
  }
  if (!person && acting) {
    char who[96];
    snprintf(who, sizeof who, "The %s is planning", fv_role_name(s->role[g->turn]));
    text(who, sz.x * 0.5f, yh, 18 * u, rgba(ROLE_RGBA[s->role[g->turn]]), CW_ALIGN_CENTER);
  }
  if (line[0]) text(line, sz.x * 0.5f, yi, 18 * u, rgba(0xf4f6faff), CW_ALIGN_CENTER);
  if (person) {
    const char *pr = g->mode == UI_SETUP ? "{left|right} {up|down} choose    {choose} pick    {how_to_play} how to play"
                   : g->mode == UI_SHARE ? "{up|down} a card    {choose} share it    {back} back"
                   : g->mode == UI_DISCARD ? "{up|down} a card    {choose} discard it"
                                           : "{left|right} {up|down} a city or an action    {choose} do it    {how_to_play} how to play";
    if (g->mode != UI_SETUP) {
      char head[96];
      snprintf(head, sizeof head, s->phase == FV_PH_DISCARD ? "%s: too many cards" : "%s, your move", fv_role_name(s->role[g->turn]));
      text(head, sz.x * 0.5f, yh, 18 * u, rgba(ROLE_RGBA[s->role[g->turn]]), CW_ALIGN_CENTER);
    }
    cw_prompt_text(font(), pr, tg_input_seat(g), cw_v2(sz.x * 0.5f, g->mode == UI_SETUP ? sz.y - 194 * u : yp), 16 * u, cw_rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
  }
}

static void hud_setup(const tg_game *g, float u) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float pw = 1000 * u, ph = 420 * u, px = sz.x * 0.5f - pw * 0.5f, py = 56 * u;
  panel(px, py, pw, ph, 1);
  text("Fever Line", sz.x * 0.5f, py + 8 * u, 30 * u, rgba(0x8ae8f0ff), CW_ALIGN_CENTER);
  char line[160];
  snprintf(line, sizeof line, "Pick a map (%d cities), and how hard the fevers push: more surges, a faster spread.", FV_MAPS[g->setup_map].ncity);
  text(line, sz.x * 0.5f, py + 46 * u, 17 * u, rgba(0xdce6f0ff), CW_ALIGN_CENTER);
  text(g->seats == 1 ? "You play every agent of the team: how many agents?" : "Each seat plays one agent of the team.", sz.x * 0.5f, py + 68 * u, 17 * u, rgba(0xdce6f0ff), CW_ALIGN_CENTER);
  (void)g;
}

void view_hud(const tg_game *g, float dt) {
  (void)dt;
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  if (g->mode == UI_SETUP) {
    hud_setup(g, u);
    hud_choice(g, u);
  } else if (g->match) {
    if (!g->help) hud_labels(g, u);
    hud_top(g, u);
    hud_agents(g, u);
    if (!g->over && !tg_busy(g) && !g->dirty) hud_choice(g, u);
    else if (!g->over && g->turn >= 0) {
      const fv_state *s = &g->shown;
      const char *what = s->phase == FV_PH_DRAW ? "draws two cards" : s->phase == FV_PH_INFECT || s->phase == FV_PH_SURGE ? "watches the fever spread" : "acts";
      char who[96];
      snprintf(who, sizeof who, "The %s %s", fv_role_name(s->role[s->cur]), what);
      float tw = text_w(who, 18 * u) + 40 * u, ty = sz.y - 168 * u - 34 * u;
      cw_r2d_round_rect((cw_rect){ 2 * roundf((sz.x * 0.5f - tw * 0.5f) * 0.5f), 2 * roundf((ty - 4 * u) * 0.5f), 2 * roundf(tw * 0.5f), 2 * roundf(15 * u) }, 10 * u, 6, cw_rgba(0.01f, 0.02f, 0.05f, 0.72f));
      text(who, sz.x * 0.5f, ty, 18 * u, rgba(ROLE_RGBA[s->role[s->cur]]), CW_ALIGN_CENTER);
    }
    /* the banner: eased in and out */
    if (V.banner[0] && V.banner_t < 3.0f && !g->over) {
      float a = clampf(V.banner_t * 3, 0, 1) * clampf((3.0f - V.banner_t) * 2, 0, 1);
      float w = text_w(V.banner, 21 * u) + 50 * u;
      panel(sz.x * 0.5f - w * 0.5f, 70 * u, w, 40 * u, a);
      text(V.banner, sz.x * 0.5f, 78 * u, 21 * u, with_a(rgba(V.banner_rgba), a), CW_ALIGN_CENTER);
    }
    /* the end */
    if (g->over) {
      const fv_state *s = &g->shown;
      float pw = 720 * u, ph = 250 * u, px = sz.x * 0.5f - pw * 0.5f, py = sz.y * 0.5f - ph * 0.5f - 60 * u;
      float a = clampf((float)g->over_ticks / 30.0f, 0, 1);
      panel(px, py, pw, ph, a);
      panel(px, py, pw, ph, a);
      bool won = s->result == FV_WON;
      text(won ? "The team cured every fever" : "The fevers win this time", sz.x * 0.5f, py + 22 * u, 32 * u, won ? cw_rgba(0.6f, 1, 0.85f, a) : cw_rgba(1, 0.6f, 0.5f, a), CW_ALIGN_CENTER);
      static const char *const WHY[4] = { "", "Eight outbreaks.", "A fever ran out of cubes.", "The city deck ran out." };
      char line[160];
      int cures = 0;
      for (int c = 0; c < FV_NCOL; c++) cures += s->cured[c] != 0;
      if (won) snprintf(line, sizeof line, "Cured in %d turns with %d outbreaks (%s setup).", s->turns + 1, s->outbreaks, tg_setup_name(s->setup));
      else snprintf(line, sizeof line, "%s Fevers cured %d of 4, turns %d (%s setup).", WHY[s->lose_reason], cures, s->turns + 1, tg_setup_name(s->setup));
      text(line, sz.x * 0.5f, py + 80 * u, 19 * u, cw_rgba(0.9f, 0.94f, 1, a), CW_ALIGN_CENTER);
      for (int c = 0; c < FV_NCOL; c++) art_cell(c, cw_v2(sz.x * 0.5f + ((float)c - 1.5f) * 70 * u, py + 160 * u), 52 * u, s->cured[c] ? a : 0.25f * a);
      cw_prompt_text(font(), "{choose} back to the title", -1, cw_v2(sz.x * 0.5f, py + ph - 36 * u), 18 * u, cw_rgba(1, 1, 1, a), CW_ALIGN_CENTER);
    }
  }
  if (g->help) view_help(g, sz, true);
}

void view_help(const tg_game *g, cw_vec2 sz, bool close_prompt) {
  float u = sz.y / 720.0f;
  {
    float pw = 1140 * u, ph = 560 * u, px = sz.x * 0.5f - pw * 0.5f, py = sz.y * 0.5f - ph * 0.5f;
    panel(px, py, pw, ph, 1);
    panel(px, py, pw, ph, 1);
    for (unsigned i = 0; i < sizeof HELP / sizeof HELP[0]; i++)
      text(HELP[i], px + 28 * u, py + (18 + (float)i * 34) * u, (i ? 17 : 26) * u, i ? rgba(0xe4ecf4ff) : rgba(0x8ae8f0ff), CW_ALIGN_LEFT);
    /* the powers: this team's in a match, every agent's on the title */
    int nr = g && g->match ? g->roles : FV_NROLES;
    float y = py + (18 + 10.0f * 34) * u;
    for (int r = 0; r < nr; r++, y += 24 * u) {
      int role = g && g->match ? g->shown.role[r] : r;
      char line[160];
      snprintf(line, sizeof line, "%s: %s", fv_role_name(role), fv_role_power(role));
      art_cell(ART_ROLE0 + role, cw_v2(px + 40 * u, y + 10 * u), 22 * u, 1);
      text(line, px + 58 * u, y, 16 * u, rgba(ROLE_RGBA[role]), CW_ALIGN_LEFT);
    }
    if (close_prompt) cw_prompt_text(font(), "{choose} close", -1, cw_v2(sz.x * 0.5f, py + ph - 34 * u), 18 * u, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
}
