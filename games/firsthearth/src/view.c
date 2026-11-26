/* First Hearth: the table in 3D and the HUD over it. See view.h. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define TOKENS 10 /* goods shown per pile (the HUD prints the count) */
#define MAX_DICE 10

static const uint32_t SEAT_HEX[TR_MAX_P] = { 0xc4502eu, 0x3f7fc0u, 0x5a9a3cu, 0xe0b040u };
static const uint32_t SEAT_RGBA[TR_MAX_P] = { 0xe0603cffu, 0x5a9ae0ffu, 0x74c050ffu, 0xf0c450ffu };
static const char *const RES_NAME[TR_NRES] = { "food", "wood", "clay", "stone", "gold" };
static const char *const SITE_TITLE[TR_NSITES] = {
  "Hunting grounds", "Forest", "Clay bank", "Quarry", "River", "Hearth", "Garden", "Tool bench",
  "Lore card", "Lore card", "Lore card", "Lore card", "Building", "Building", "Building", "Building",
};

typedef struct glide {
  t3_vec3 pos, from, to;
  float t, dur, arc;
  bool moving, placed;
} glide;

int view_stage, view_texcs;
static struct {
  cw_tree *tree;
  cw_node world, camera, root;
  int players;
  /* pieces */
  cw_node kin[TR_MAX_P][TR_MAX_KIN];
  glide kin_g[TR_MAX_P][TR_MAX_KIN];
  cw_node ghost[TR_MAX_KIN];
  cw_node tok[TR_MAX_P][TR_NRES][TOKENS];
  cw_node tool[TR_MAX_P][3][4];
  cw_node field_peg[TR_MAX_P];
  glide field_g[TR_MAX_P];
  cw_node mat[TR_MAX_P];
  cw_node card[TR_NCARDS];
  glide card_g[TR_NCARDS];
  float card_fade[TR_NCARDS];
  cw_node deck;
  cw_node hut[TR_NBUILD];
  glide hut_g[TR_NBUILD];
  float hut_rise[TR_NBUILD], hut_scale[TR_NBUILD];
  int hut_owner[TR_NBUILD];
  cw_node pile_slab[4][TR_PILE];
  cw_node cost_tok[4][4][4]; /* pile, slot, resource kind (wood..gold) */
  cw_node die[MAX_DICE];
  bk_die_throw throw_[MAX_DICE];
  int dice_n;
  float dice_t, dice_hold;
  int dice_serial;
  cw_node marker;
  float marker_bob;
  cw_node first_stone;
  glide first_g;
  /* materials */
  t3_material *seat_mat[TR_MAX_P], *res_mat[TR_NRES], *slab_mat, *peg_mat;
  t3_geometry *res_geo[TR_NRES];
  /* HUD */
  bk_counter score[TR_MAX_P];
  bk_deltas deltas;
  int beat_seen;
  char banner[128];
  float banner_t;
  char roll_text[96];
  float roll_t;
  int moving;
  float time;
  /* sound */
  cw_sound snd_tock, snd_rattle, snd_chime, snd_thump;
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

/* a picture is colour (sRGB), said before its first load: a PNG would load as data */
static t3_texture *texture(const char *path) {
  if (!cw_res_texture_set_srgb(path, true)) CW_FATAL("firsthearth: %s was loaded as data before it could be said to be colour", path);
  cw_res r = cw_res_load(path, CW_RES_TEXTURE);
  if (cw_res_wait(r) != CW_RES_STATE_READY) CW_FATAL("firsthearth: texture %s did not load", path);
  t3_texture *t = cw_res_texture(r);
  view_texcs = view_texcs * 4 + (t ? (int)t->color_space : 9);
  if (t->color_space != T3_SRGB_COLOR_SPACE) CW_FATAL("firsthearth: %s did not load as a colour picture", path);
  if (!t) CW_FATAL("firsthearth: %s is not a texture", path);
  return t;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_geometry *geo, t3_material *mat, bool cast, bool receive) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  t3_mesh *m = t3_mesh_new(geo, mat);
  m->base.cast_shadow = cast;
  m->base.receive_shadow = receive;
  if (!cw_mesh3d_set_procedural(n, m)) CW_FATAL("firsthearth: could not attach the mesh of %s", name);
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
/* advance; returns true on the frame it lands */
static bool glide_step(glide *g, float dt) {
  if (!g->moving) return false;
  g->t += dt;
  float u = clampf(g->t / g->dur, 0, 1);
  float e = ease(u);
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

/* ── sounds (synthesised) ────────────────────────────────────────────── */

static cw_sound synth(int kind) {
  const int rate = 44100;
  int n = rate * (kind == 1 ? 7 : kind == 2 ? 9 : 3) / 10;
  float *pcm = calloc((size_t)n, sizeof *pcm);
  uint32_t r = 99991u + (uint32_t)kind * 7919u;
  for (int i = 0; i < n; i++) {
    float t = (float)i / rate;
    r = r * 1664525u + 1013904223u;
    float noise = ((r >> 9) / 4194304.0f) - 1.0f, v;
    switch (kind) {
    case 0: v = sinf(6.2832f * 320 * t) * expf(-t * 38) * 0.6f + noise * expf(-t * 160) * 0.2f; break; /* tock: wood */
    case 1: /* rattle: bone dice on wood, a few knocks */
      v = 0;
      for (int k = 0; k < 6; k++) {
        float tk = t - k * 0.09f - (k * k) * 0.006f;
        if (tk > 0) v += (sinf(6.2832f * (900 + k * 140) * tk) * 0.5f + noise * 0.5f) * expf(-tk * 70) * (1.0f - k * 0.12f);
      }
      v *= 0.5f;
      break;
    case 2: v = (sinf(6.2832f * 523 * t) * 0.5f + sinf(6.2832f * 784 * t) * 0.3f) * expf(-t * 4) * fminf(1, t * 150); break; /* chime */
    default: v = sinf(6.2832f * (140 - 60 * t) * t) * expf(-t * 18) * 0.8f + noise * expf(-t * 70) * 0.2f; /* thump */
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

/* ── pieces ──────────────────────────────────────────────────────────── */

/* a kin: a turned wooden figure, body and head */
static cw_node make_kin(cw_node parent, const char *name, t3_material *m) {
  static t3_geometry *body, *head;
  if (!body) {
    t3_vec2 prof[] = { { 0.0f, 0.0f }, { 0.15f, 0.0f }, { 0.155f, 0.03f }, { 0.13f, 0.07f }, { 0.095f, 0.2f },
                       { 0.075f, 0.29f }, { 0.11f, 0.31f }, { 0.07f, 0.34f }, { 0.0f, 0.345f } };
    body = t3_lathe_geometry_new(prof, 9, 20, 0, 6.2832f);
    head = t3_sphere_geometry_new(0.085f, 16, 12);
  }
  cw_node g = group(parent, name);
  cw_node3d_set_scale(g, (t3_vec3){ 1.2f, 1.2f, 1.2f });
  mesh_node(g, "Body", body, m, true, true);
  cw_node h = mesh_node(g, "Head", head, m, true, true);
  cw_node3d_set_position(h, v3(0, 0.41f, 0));
  return g;
}

/* a hut for building b: walls and roof by what it is made of */
static cw_node make_hut(cw_node parent, int b) {
  static t3_geometry *walls, *roof, *box, *prism, *knob;
  static t3_material *wall_m[4], *thatch, *gold;
  if (!walls) {
    walls = t3_cylinder_geometry_new(0.2f, 0.22f, 0.18f, 18, 1, false);
    roof = t3_cone_geometry_new(0.29f, 0.24f, 18, 1, false);
    box = t3_box_geometry_new(0.52f, 0.17f, 0.3f, 1, 1, 1);
    prism = t3_cylinder_geometry_new(0.21f, 0.21f, 0.6f, 3, 1, false);
    knob = t3_octahedron_geometry_new(0.06f, 0);
    wall_m[0] = matte(0x7a5232, 0.9f);  /* timber */
    wall_m[1] = matte(0xb8653a, 0.95f); /* daub */
    wall_m[2] = matte(0x8a8a86, 0.9f);  /* stone */
    wall_m[3] = matte(0x9a7a50, 0.85f);
    thatch = matte(0xc9a45a, 1.0f);
    gold = t3_mesh_physical_material_new(0xe0b040);
    gold->roughness = 0.35f;
    gold->metalness = 0.25f;
  }
  const int8_t *c = tr_builds[b].cost;
  int main_kind = 0, best = -1, pieces = c[0] + c[1] + c[2] + c[3];
  for (int k = 0; k < 3; k++)
    if (c[k] > best) best = c[k], main_kind = k;
  char nm[24];
  snprintf(nm, sizeof nm, "Hut%d", b);
  cw_node g = group(parent, nm);
  if (pieces >= 4) { /* a longhouse */
    cw_node w = mesh_node(g, "Walls", box, wall_m[main_kind], true, true);
    cw_node3d_set_position(w, v3(0, 0.085f, 0));
    cw_node r = mesh_node(g, "Roof", prism, thatch, true, true);
    cw_node3d_set_rotation(r, v3(0, 0, 1.5708f));
    cw_node3d_set_position(r, v3(0, 0.25f, 0));
    cw_node3d_set_scale(r, v3(1, 1, 0.95f));
  } else {
    cw_node w = mesh_node(g, "Walls", walls, wall_m[main_kind], true, true);
    cw_node3d_set_position(w, v3(0, 0.09f, 0));
    cw_node r = mesh_node(g, "Roof", roof, c[2] >= 2 ? wall_m[2] : thatch, true, true);
    cw_node3d_set_position(r, v3(0, 0.3f, 0));
  }
  if (c[3] > 0) {
    cw_node k = mesh_node(g, "Gold", knob, gold, true, true);
    cw_node3d_set_position(k, v3(0, pieces >= 4 ? 0.42f : 0.45f, 0));
  }
  return g;
}

/* a card: a thin slab with its face from the atlas on top */
static cw_node make_card(cw_node parent, int c, t3_texture *atlas, t3_material *edge) {
  t3_geometry *face = t3_plane_geometry_new(LY_CARD_W, LY_CARD_D, 1, 1);
  t3_attribute *uv = face->attributes[T3_ATTR_UV];
  float *f = (float *)uv->array;
  float u0 = (float)(c % 8) * 256.0f / 2048.0f + 36.0f / 2048.0f, u1 = u0 + 184.0f / 2048.0f;
  float v0 = (float)(c / 8) * 256.0f / 1024.0f, v1 = v0 + 256.0f / 1024.0f;
  for (int i = 0; i < uv->count; i++) {
    float u = f[i * 2], v = f[i * 2 + 1];
    f[i * 2] = u0 + u * (u1 - u0);
    f[i * 2 + 1] = 1.0f - (v1 - v * (v1 - v0)); /* texture rows run down, uv v runs up */
    f[i * 2 + 1] = 1.0f - f[i * 2 + 1];
  }
  t3_material *m = matte(0xffffff, 0.85f);
  t3_material_set_map(m, atlas);
  char nm[16];
  snprintf(nm, sizeof nm, "Card%d", c);
  cw_node g = group(parent, nm);
  static t3_geometry *slab;
  if (!slab) slab = t3_box_geometry_new(LY_CARD_W, 0.02f, LY_CARD_D, 1, 1, 1);
  cw_node s = mesh_node(g, "Slab", slab, edge, true, true);
  cw_node3d_set_position(s, v3(0, 0.01f, 0));
  cw_node fc = mesh_node(g, "Face", face, m, false, true);
  cw_node3d_set_rotation(fc, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(fc, v3(0, 0.0215f, 0));
  t3_release(face);
  t3_release(m);
  return g;
}

/* ── the scene ───────────────────────────────────────────────────────── */

static void props(cw_node root) {
  /* the forest: pines */
  t3_geometry *cone = t3_cone_geometry_new(0.32f, 0.9f, 9, 1, false), *trunk = t3_cylinder_geometry_new(0.05f, 0.07f, 0.3f, 6, 1, false);
  t3_material *pine = matte(0x2f5a2c, 0.95f), *bark = matte(0x4a3020, 0.95f);
  static const float trees[][3] = { { -8.3f, -5.3f, 1.1f }, { -7.6f, -5.5f, 0.9f }, { -4.6f, -5.4f, 1.0f }, { -4.2f, -4.9f, 0.8f },
                                    { -8.5f, -2.5f, 0.9f }, { -8.2f, -4.6f, 0.7f }, { -6.9f, -5.6f, 0.75f }, { -4.0f, -2.6f, 0.7f } };
  for (unsigned i = 0; i < sizeof trees / sizeof trees[0]; i++) {
    cw_node t = group(root, "Tree");
    cw_node3d_set_position(t, v3(trees[i][0], LY_BOARD_Y, trees[i][1]));
    cw_node3d_set_scale(t, v3(trees[i][2], trees[i][2], trees[i][2]));
    cw_node k = mesh_node(t, "Trunk", trunk, bark, true, true);
    cw_node3d_set_position(k, v3(0, 0.15f, 0));
    cw_node c1 = mesh_node(t, "Crown", cone, pine, true, true);
    cw_node3d_set_position(c1, v3(0, 0.65f, 0));
    cw_node c2 = mesh_node(t, "Crown2", cone, pine, true, true);
    cw_node3d_set_position(c2, v3(0, 0.95f, 0));
    cw_node3d_set_scale(c2, v3(0.75f, 0.75f, 0.75f));
  }
  /* the quarry and the clay bank: boulders and a clay mound */
  t3_geometry *rock = t3_dodecahedron_geometry_new(0.3f, 0);
  t3_material *grey = matte(0x8a8c8e, 0.9f), *clay = matte(0xb0603a, 0.95f);
  static const float rocks[][4] = { { 0.6f, -5.2f, 1.2f, 0 }, { 3.6f, -5.1f, 0.9f, 0 }, { 3.9f, -2.7f, 0.7f, 0 }, { 0.5f, -2.6f, 0.6f, 0 },
                                    { -3.6f, -5.2f, 1.0f, 1 }, { -0.7f, -5.2f, 0.8f, 1 }, { -3.7f, -2.6f, 0.6f, 1 } };
  for (unsigned i = 0; i < sizeof rocks / sizeof rocks[0]; i++) {
    cw_node r = mesh_node(root, "Rock", rock, rocks[i][3] ? clay : grey, true, true);
    cw_node3d_set_position(r, v3(rocks[i][0], LY_BOARD_Y + 0.12f * rocks[i][2], rocks[i][1]));
    cw_node3d_set_scale(r, v3(rocks[i][2], rocks[i][2] * 0.65f, rocks[i][2]));
    cw_node3d_set_rotation(r, v3(0.3f * i, 0.7f * i, 0));
  }
  /* the hearth: logs and an ember glow */
  t3_geometry *log = t3_cylinder_geometry_new(0.05f, 0.05f, 0.5f, 8, 1, false);
  t3_material *logm = matte(0x5a3a22, 0.9f);
  for (int k = 0; k < 4; k++) {
    cw_node l = mesh_node(root, "Log", log, logm, true, true);
    cw_node3d_set_position(l, v3(-1.55f, LY_BOARD_Y + 0.06f, 0.0f));
    cw_node3d_set_rotation(l, v3(1.5708f, 0.785f * k, 0.25f));
  }
  t3_material *ember = t3_mesh_standard_material_new(0x401008);
  ember->emissive = (t3_color){ 1.0f, 0.42f, 0.12f };
  ember->emissive_intensity = 1.6f;
  t3_geometry *eg = t3_sphere_geometry_new(0.12f, 12, 8);
  cw_node e = mesh_node(root, "Ember", eg, ember, false, false);
  cw_node3d_set_position(e, v3(-1.55f, LY_BOARD_Y + 0.04f, 0.0f));
  cw_node3d_set_scale(e, v3(1.2f, 0.4f, 1.2f));
  /* the tool bench: a flat stone anvil */
  t3_geometry *anvil = t3_box_geometry_new(0.9f, 0.18f, 0.55f, 1, 1, 1);
  cw_node a = mesh_node(root, "Anvil", anvil, grey, true, true);
  cw_node3d_set_position(a, v3(6.6f, LY_BOARD_Y + 0.09f, -0.5f));
  cw_node3d_set_rotation(a, v3(0, 0.3f, 0));
  /* the garden: sprouts in rows */
  t3_geometry *sprout = t3_cone_geometry_new(0.05f, 0.16f, 5, 1, false);
  t3_material *leaf = matte(0x5f9a34, 0.9f);
  for (int r = 0; r < 4; r++)
    for (int k = 0; k < 5; k++) {
      cw_node s = mesh_node(root, "Sprout", sprout, leaf, false, true);
      cw_node3d_set_position(s, v3(0.65f + k * 0.45f, LY_BOARD_Y + 0.08f, -0.9f + r * 0.32f));
    }
  t3_release(cone); t3_release(trunk); t3_release(pine); t3_release(bark); t3_release(rock); t3_release(grey);
  t3_release(clay); t3_release(log); t3_release(logm); t3_release(ember);
  t3_release(eg); t3_release(anvil); t3_release(sprout); t3_release(leaf);
}

cw_node view_camera(void) { return V.camera; }

void view_init(cw_tree *t) {
  memset(&V, 0, sizeof V);
  V.tree = t;
  V.world = cw_node_get(cw_tree_root(t), "World");
  if (!cw_node_valid(V.world)) V.world = cw_tree_root(t);
  V.camera = cw_node_get(V.world, "Camera");
  if (!cw_node_valid(V.camera)) CW_FATAL("firsthearth: main.cwscene has no Camera");
  V.root = group(V.world, "Table");
  /* the table */
  t3_texture *tt = texture("textures/table.png");
  tt->wrap_s = tt->wrap_t = T3_REPEAT;
  t3_material *wood = matte(0xffffff, 0.6f);
  t3_material_set_map(wood, tt);
  t3_geometry *tg = t3_plane_geometry_new(70, 44, 1, 1);
  {
    float *f = (float *)tg->attributes[T3_ATTR_UV]->array;
    for (int i = 0; i < tg->attributes[T3_ATTR_UV]->count * 2; i++) f[i] *= 7;
  }
  cw_node table = mesh_node(V.root, "TableTop", tg, wood, false, true);
  cw_node3d_set_rotation(table, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(table, v3(0, 0, 2));
  t3_release(tg);
  t3_release(wood);
  /* the village board: a thick slab, its painted face on top */
  t3_material *edge = matte(0x3a2616, 0.8f);
  t3_geometry *bg = t3_box_geometry_new(18.0f, LY_BOARD_Y - 0.01f, 10.0f, 1, 1, 1);
  cw_node slab = mesh_node(V.root, "BoardSlab", bg, edge, true, true);
  cw_node3d_set_position(slab, v3(0, (LY_BOARD_Y - 0.01f) * 0.5f, -1.0f));
  t3_release(bg);
  t3_material *paint = matte(0xffffff, 0.88f);
  t3_material_set_map(paint, texture("textures/board.png"));
  t3_geometry *pg = t3_plane_geometry_new(18.0f, 10.0f, 1, 1);
  flip_v(pg);
  cw_node face = mesh_node(V.root, "BoardFace", pg, paint, false, true);
  cw_node3d_set_rotation(face, v3(-1.5708f, 0, 0));
  cw_node3d_set_position(face, v3(0, LY_BOARD_Y, -1.0f));
  t3_release(pg);
  t3_release(paint);
  props(V.root);
  /* materials for pieces */
  for (int s = 0; s < TR_MAX_P; s++) {
    V.seat_mat[s] = matte(SEAT_HEX[s], 0.7f);
  }
  t3_texture *grain = texture("textures/grain.png");
  V.res_mat[TR_FOOD] = matte(0x9a3a24, 0.75f);
  V.res_mat[TR_WOOD] = matte(0xffffff, 0.85f);
  t3_material_set_map(V.res_mat[TR_WOOD], grain);
  V.res_mat[TR_CLAY] = matte(0xc06a38, 0.95f);
  V.res_mat[TR_STONE] = matte(0x9a9c9e, 0.9f);
  V.res_mat[TR_GOLD] = t3_mesh_physical_material_new(0xe8b830);
  V.res_mat[TR_GOLD]->roughness = 0.35f;
  V.res_mat[TR_GOLD]->metalness = 0.25f; /* full metal reads black with no reflections to show */
  V.res_mat[TR_GOLD]->emissive = (t3_color){ 0.25f, 0.17f, 0.02f };
  V.res_geo[TR_FOOD] = t3_cylinder_geometry_new(0.12f, 0.12f, 0.05f, 16, 1, false);
  V.res_geo[TR_WOOD] = t3_cylinder_geometry_new(0.055f, 0.055f, 0.3f, 10, 1, false);
  V.res_geo[TR_CLAY] = t3_box_geometry_new(0.22f, 0.08f, 0.12f, 1, 1, 1);
  V.res_geo[TR_STONE] = t3_icosahedron_geometry_new(0.09f, 0);
  V.res_geo[TR_GOLD] = t3_octahedron_geometry_new(0.075f, 0);
  V.slab_mat = matte(0x6a6460, 0.9f);
  V.peg_mat = matte(0x3a2a1e, 0.8f);
  /* cards */
  t3_texture *atlas = texture("textures/cards.png");
  t3_material *card_edge = matte(0xd8c8a0, 0.9f);
  for (int c = 0; c < TR_NCARDS; c++) {
    V.card[c] = make_card(V.root, c, atlas, card_edge);
    show(V.card[c], false);
  }
  /* the deck: face-down cards at the left of the card row */
  t3_material *back = matte(0x5a3a26, 0.9f);
  t3_geometry *dg = t3_box_geometry_new(LY_CARD_W, 0.3f, LY_CARD_D, 1, 1, 1);
  V.deck = mesh_node(V.root, "Deck", dg, back, true, true);
  t3_release(dg);
  t3_release(back);
  t3_release(card_edge);
  /* buildings */
  for (int b = 0; b < TR_NBUILD; b++) {
    V.hut[b] = make_hut(V.root, b);
    show(V.hut[b], false);
    V.hut_owner[b] = -1;
  }
  t3_geometry *ps = t3_cylinder_geometry_new(0.62f, 0.66f, 0.05f, 20, 1, false);
  for (int p = 0; p < 4; p++) {
    for (int k = 0; k < TR_PILE; k++) {
      V.pile_slab[p][k] = mesh_node(V.root, "PileSlab", ps, V.slab_mat, true, true);
      cw_node3d_set_position(V.pile_slab[p][k], v3(LY_SITE[TR_S_BLD0 + p][0], LY_BOARD_Y + 0.025f + k * 0.052f, LY_SITE[TR_S_BLD0 + p][1]));
      cw_node3d_set_rotation(V.pile_slab[p][k], v3(0, k * 0.4f, 0));
      show(V.pile_slab[p][k], false);
    }
    for (int k = 0; k < 4; k++)
      for (int r = 0; r < 4; r++) {
        V.cost_tok[p][k][r] = mesh_node(V.root, "Cost", V.res_geo[TR_WOOD + r], V.res_mat[TR_WOOD + r], true, true);
        show(V.cost_tok[p][k][r], false);
      }
  }
  t3_release(ps);
  /* dice */
  for (int d = 0; d < MAX_DICE; d++) {
    V.die[d] = bk_die_node(V.root, "Die", 0.26f, cw_hex(0xece2caff), cw_hex(0x2a1a10ff));
    show(V.die[d], false);
  }
  /* the cursor marker: a hovering ring */
  t3_material *mk = t3_mesh_standard_material_new(0xffe8a0);
  mk->emissive = (t3_color){ 1.0f, 0.85f, 0.45f };
  mk->emissive_intensity = 0.8f;
  t3_geometry *tor = t3_torus_geometry_new(0.42f, 0.05f, 10, 36, 6.2832f);
  V.marker = mesh_node(V.root, "Marker", tor, mk, false, false);
  cw_node3d_set_rotation(V.marker, v3(1.5708f, 0, 0));
  show(V.marker, false);
  t3_release(tor);
  t3_release(mk);
  /* the first player's stone */
  t3_material *fs = t3_mesh_physical_material_new(0xf0e6d0);
  fs->roughness = 0.35f;
  t3_geometry *fg = t3_cylinder_geometry_new(0.22f, 0.25f, 0.1f, 24, 1, false);
  V.first_stone = mesh_node(V.root, "FirstStone", fg, fs, true, true);
  t3_release(fg);
  t3_release(fs);
  V.snd_tock = synth(0);
  V.snd_rattle = synth(1);
  V.snd_chime = synth(2);
  V.snd_thump = synth(3);
  V.sound_ok = true;
  bk_deltas_init(&V.deltas);
}

static void free_seat_nodes(void) {
  for (int s = 0; s < TR_MAX_P; s++) {
    for (int k = 0; k < TR_MAX_KIN; k++)
      if (cw_node_valid(V.kin[s][k])) cw_node_free(V.kin[s][k]);
    for (int r = 0; r < TR_NRES; r++)
      for (int k = 0; k < TOKENS; k++)
        if (cw_node_valid(V.tok[s][r][k])) cw_node_free(V.tok[s][r][k]);
    for (int i = 0; i < 3; i++)
      for (int k = 0; k < 4; k++)
        if (cw_node_valid(V.tool[s][i][k])) cw_node_free(V.tool[s][i][k]);
    if (cw_node_valid(V.field_peg[s])) cw_node_free(V.field_peg[s]);
    if (cw_node_valid(V.mat[s])) cw_node_free(V.mat[s]);
  }
  for (int k = 0; k < TR_MAX_KIN; k++)
    if (cw_node_valid(V.ghost[k])) cw_node_free(V.ghost[k]);
  memset(V.kin, 0, sizeof V.kin);
  memset(V.tok, 0, sizeof V.tok);
  memset(V.tool, 0, sizeof V.tool);
  memset(V.field_peg, 0, sizeof V.field_peg);
  memset(V.mat, 0, sizeof V.mat);
  memset(V.ghost, 0, sizeof V.ghost);
}

void view_start(const tg_game *g) {
  free_seat_nodes();
  V.players = g->players;
  int n = g->players;
  t3_material *hide = matte(0xffffff, 1.0f);
  t3_material_set_map(hide, texture("textures/mat.png"));
  t3_geometry *mg = t3_plane_geometry_new(LY_MAT_W, LY_MAT_D, 1, 1);
  flip_v(mg);
  t3_geometry *tslab = t3_box_geometry_new(0.36f, 0.05f, 0.24f, 1, 1, 1);
  t3_geometry *peg = t3_cylinder_geometry_new(0.11f, 0.12f, 0.12f, 16, 1, false);
  t3_geometry *dot = t3_cylinder_geometry_new(0.04f, 0.04f, 0.01f, 8, 1, false);
  for (int s = 0; s < n; s++) {
    V.mat[s] = group(V.root, "Mat");
    t3_vec3 m = ly_mat(s, n);
    cw_node mm = mesh_node(V.mat[s], "Hide", mg, hide, false, true);
    cw_node3d_set_rotation(mm, v3(-1.5708f, 0, 0));
    cw_node3d_set_position(mm, v3(m.x, LY_MAT_Y, m.z));
    /* a band of the seat's colour along the mat's back edge */
    t3_geometry *band = t3_box_geometry_new(LY_MAT_W - 0.3f, 0.02f, 0.14f, 1, 1, 1);
    cw_node bn = mesh_node(V.mat[s], "Band", band, V.seat_mat[s], false, true);
    cw_node3d_set_position(bn, v3(m.x, LY_MAT_Y + 0.01f, m.z - LY_MAT_D * 0.5f + 0.12f));
    t3_release(band);
    for (int l = 0; l <= TR_MAX_FIELD; l++) { /* the field track's holes */
      cw_node d = mesh_node(V.mat[s], "Hole", dot, V.peg_mat, false, true);
      cw_node3d_set_position(d, ly_field(s, l, n));
    }
    for (int k = 0; k < TR_MAX_KIN; k++) {
      char nm[16];
      snprintf(nm, sizeof nm, "Kin%d_%d", s, k);
      V.kin[s][k] = make_kin(V.root, nm, V.seat_mat[s]);
      V.kin_g[s][k] = (glide){ 0 };
      show(V.kin[s][k], false);
    }
    for (int r = 0; r < TR_NRES; r++)
      for (int k = 0; k < TOKENS; k++) {
        V.tok[s][r][k] = mesh_node(V.root, "Good", V.res_geo[r], V.res_mat[r], true, true);
        show(V.tok[s][r][k], false);
      }
    for (int i = 0; i < 3; i++)
      for (int k = 0; k < 4; k++) {
        V.tool[s][i][k] = mesh_node(V.root, "Tool", tslab, V.slab_mat, true, true);
        t3_vec3 p = ly_tool(s, i, n);
        cw_node3d_set_position(V.tool[s][i][k], v3(p.x, p.y + 0.025f + k * 0.052f, p.z));
        cw_node3d_set_rotation(V.tool[s][i][k], v3(0, (k & 1) * 0.12f - 0.06f, 0));
        show(V.tool[s][i][k], false);
      }
    V.field_peg[s] = mesh_node(V.root, "FieldPeg", peg, V.seat_mat[s], true, true);
    V.field_g[s] = (glide){ 0 };
    bk_counter_init(&V.score[s], 0);
  }
  for (int k = 0; k < TR_MAX_KIN; k++) {
    V.ghost[k] = make_kin(V.root, "Ghost", V.seat_mat[0]);
    if (!bk_ghost_set(V.ghost[k], 0.45f)) CW_FATAL("firsthearth: the ghost kin has no mesh");
    show(V.ghost[k], false);
  }
  t3_release(mg);
  t3_release(hide);
  t3_release(tslab);
  t3_release(peg);
  t3_release(dot);
  for (int c = 0; c < TR_NCARDS; c++) V.card_g[c] = (glide){ 0 }, V.card_fade[c] = 0, show(V.card[c], false);
  for (int b = 0; b < TR_NBUILD; b++) V.hut_g[b] = (glide){ 0 }, V.hut_owner[b] = -1, V.hut_rise[b] = 1, show(V.hut[b], false);
  V.first_g = (glide){ 0 };
  V.beat_seen = g->beat.serial;
  V.dice_n = 0;
  V.banner[0] = V.roll_text[0] = 0;
  bk_deltas_init(&V.deltas);
  /* the camera: the whole board and the mats, from the near side */
  float wide = n > 2 ? 1.0f : 0.92f;
  cw_node3d_set_position(V.camera, v3(0, 21.0f * wide, 18.0f * wide));
  cw_node3d_look_at(V.camera, v3(0, 0, 2.4f), v3(0, 1, 0));
}

/* ── following the table ─────────────────────────────────────────────── */

static cw_vec2 to_screen(t3_vec3 p);
cw_vec2 view_to_screen(t3_vec3 p) { return to_screen(p); }
static cw_vec2 to_screen(t3_vec3 p) {
  cw_vec2 sz = cw_r2d_canvas_size();
  return cw_camera3d_world_to_screen(V.camera, (cw_rect){ 0, 0, sz.x, sz.y }, p, NULL);
}

/* a die split of a sum: k faces 1..6 adding to sum, spread from a seed */
static void split_sum(int sum, int k, uint64_t seed, int *faces) {
  cw_rng r;
  cw_rng_seed(&r, seed);
  for (int i = 0; i < k; i++) faces[i] = 1;
  int left = sum - k;
  while (left > 0) {
    int i = cw_rng_int(&r, 0, k - 1);
    if (faces[i] < 6) faces[i]++, left--;
  }
}

static void on_beat(const tg_game *g) {
  const tg_beat *b = &g->beat;
  const tr_state *a = &b->before, *z = &b->after;
  int kind = bk_move_field(b->move, 0);
  /* score and goods deltas at each mat */
  for (int s = 0; s < g->players; s++) {
    int ds = z->p[s].score - a->p[s].score;
    if (ds) bk_deltas_add(&V.deltas, to_screen(v3(ly_mat(s, g->players).x, 0.6f, LY_MAT_Z - 1.0f)), ds, ds > 0 ? rgba(0xffe080ff) : rgba(0xff8a70ff));
    if (z->round == a->round)
      for (int r = 0; r < TR_NRES; r++) {
        int d = z->p[s].res[r] - a->p[s].res[r];
        if (d && kind != TR_M_BUY) bk_deltas_add(&V.deltas, to_screen(v3(ly_goods(s, r, g->players).x, 0.5f, ly_goods(s, r, g->players).z)), d, d > 0 ? rgba(0xffffffff) : rgba(0xffb0a0ff));
      }
  }
  if (kind == TR_M_ROLL) {
    int site = a->step, seat = a->cur, k = a->place[site][seat];
    int sum = bk_move_field(b->move, 1) | bk_move_field(b->move, 2) << 8;
    int faces[MAX_DICE];
    split_sum(sum, k, bk_seed_mix((uint64_t)b->serial, (uint64_t)sum), faces);
    V.dice_n = k > MAX_DICE ? MAX_DICE : k;
    t3_vec3 c = ly_site(site);
    t3_vec3 from = ly_mat(seat, g->players);
    from.y = 1.2f;
    for (int i = 0; i < V.dice_n; i++) {
      t3_vec3 to = v3(c.x - 0.9f + (float)(i % 5) * 0.42f, LY_BOARD_Y + 0.13f, c.z + 0.6f + (float)(i / 5) * 0.42f);
      if (site == TR_S_HUNT) to.z = c.z + 1.0f + (float)(i / 5) * 0.42f;
      V.throw_[i] = bk_die_throw_make(from, to, faces[i], bk_seed_mix((uint64_t)b->serial * 31 + (uint64_t)i, 7));
      V.throw_[i].seconds = g->fast ? 0.5f : 1.1f;
      show(V.die[i], true);
    }
    for (int i = V.dice_n; i < MAX_DICE; i++) show(V.die[i], false);
    V.dice_t = 0;
    V.dice_hold = g->fast ? 1.0f : 2.2f;
    int v = tr_value(site), tools = 0;
    snprintf(V.roll_text, sizeof V.roll_text, "%s rolls %d at the %s: %d / %d = %d %s%s", tg_seat_name(seat), sum,
             tr_site_name(site), sum, v, sum / v, RES_NAME[site], tools ? "" : "");
    V.roll_t = 0;
    sfx(V.snd_rattle, 0.7f, 1.0f);
  } else if (kind == TR_M_TOOLS) {
    int seat = a->cur, site = a->step, v = tr_value(site);
    int t = 0;
    for (int i = 0; i < a->p[seat].ntools; i++)
      if (bk_move_field(b->move, 1) & (1 << i)) t += a->p[seat].tools[i];
    if (t) snprintf(V.roll_text, sizeof V.roll_text, "%s adds tools +%d: %d / %d = %d %s", tg_seat_name(seat), t, a->pend + t, v,
                    (a->pend + t) / v, RES_NAME[site]);
    V.roll_t = 0;
    sfx(V.snd_tock, 0.5f, 1.3f);
  } else if (kind == TR_M_PLACE) {
    sfx(V.snd_tock, 0.55f, 0.9f + 0.05f * (float)bk_move_field(b->move, 2));
  } else if (kind == TR_M_BUY && bk_move_field(b->move, 1)) {
    sfx(V.snd_thump, 0.7f, 1.0f);
  }
  if (z->round != a->round && z->phase != TR_PH_OVER) {
    snprintf(V.banner, sizeof V.banner, "Round %d: every village eats, the fire takes a card, new cards come", z->round);
    V.banner_t = 0;
    sfx(V.snd_chime, 0.45f, 1.0f);
  }
  if (z->phase == TR_PH_OVER && a->phase != TR_PH_OVER) {
    snprintf(V.banner, sizeof V.banner, "The season ends");
    V.banner_t = 0;
    sfx(V.snd_chime, 0.6f, 0.8f);
  }
  /* a hut built: it rises at its new home */
  for (int p = 0; p < 4; p++)
    if (a->bld_top[p] >= 0 && z->bld_top[p] != a->bld_top[p] && z->bld_count[p] < a->bld_count[p]) {
      int bi = a->bld_top[p];
      V.hut_rise[bi] = 0;
    }
}

static void place_tokens(cw_node *nodes, int count, t3_vec3 base, int res) {
  int shown = count < TOKENS ? count : TOKENS;
  for (int k = 0; k < TOKENS; k++) {
    show(nodes[k], k < shown);
    if (k >= shown) continue;
    t3_vec3 p = base;
    switch (res) {
    case TR_FOOD: p.y += 0.025f + k * 0.052f; p.x += (k & 1) * 0.02f; break; /* a stack of cakes */
    case TR_WOOD: p.x += (float)(k % 3 - 1) * 0.12f; p.y += 0.055f + (k / 3) * 0.1f; break;
    case TR_CLAY: p.x += (float)(k % 2) * 0.12f - 0.06f; p.y += 0.04f + (k / 2) * 0.082f; break;
    default: p.x += (float)(k % 3 - 1) * 0.15f; p.z += (float)((k / 3) % 2) * 0.15f - 0.07f; p.y += 0.07f + (k / 6) * 0.12f;
    }
    cw_node3d_set_position(nodes[k], p);
    if (res == TR_WOOD) cw_node3d_set_rotation(nodes[k], v3(1.5708f, 0, (k / 3) & 1 ? 0.3f : -0.2f));
    else if (res >= TR_STONE) cw_node3d_set_rotation(nodes[k], v3(k * 0.7f, k * 1.3f, 0));
    else cw_node3d_set_rotation(nodes[k], v3(0, k * 0.4f, 0));
  }
}

int view_moving(void) { return V.moving; }

void view_update(const tg_game *g, float dt) {
  if (!g->match) return;
  V.time += dt;
  V.moving = 0;
  const tr_state *s = &g->shown;
  int n = g->players;
  if (g->beat.serial != V.beat_seen) {
    V.beat_seen = g->beat.serial;
    on_beat(g);
  }
  float speed = g->fast ? 0.35f : 0.75f;
  /* kin: at a site (this round) or at home */
  for (int seat = 0; seat < n; seat++) {
    int k = 0;
    for (int site = 0; site < TR_NSITES; site++)
      for (int j = 0; j < s->place[site][seat]; j++, k++) {
        glide_to(&V.kin_g[seat][k], ly_kin_at_site(site, seat, j, n), speed, 0.5f, (float)j * 0.06f);
      }
    int home = 0;
    for (; k < s->p[seat].kin; k++, home++) glide_to(&V.kin_g[seat][k], ly_kin_home(seat, home, n), speed, 0.5f, home * 0.04f);
    for (int j = 0; j < TR_MAX_KIN; j++) {
      bool on = j < s->p[seat].kin;
      if (on && !V.kin_g[seat][j].placed) glide_to(&V.kin_g[seat][j], ly_kin_home(seat, j, n), speed, 0, 0);
      glide_step(&V.kin_g[seat][j], dt);
      show(V.kin[seat][j], on);
      if (!on) continue;
      t3_vec3 p = V.kin_g[seat][j].pos;
      cw_node3d_set_position(V.kin[seat][j], p);
    }
    /* goods */
    for (int r = 0; r < TR_NRES; r++) place_tokens(V.tok[seat][r], s->p[seat].res[r], ly_goods(seat, r, n), r);
    /* tools: each tool a stack of its strength */
    for (int i = 0; i < 3; i++)
      for (int k2 = 0; k2 < 4; k2++) show(V.tool[seat][i][k2], i < s->p[seat].ntools && k2 < s->p[seat].tools[i]);
    /* field */
    glide_to(&V.field_g[seat], ly_field(seat, s->p[seat].field, n), 0.5f, 0.35f, 0);
    glide_step(&V.field_g[seat], dt);
    t3_vec3 fp = V.field_g[seat].pos;
    fp.y += 0.06f;
    cw_node3d_set_position(V.field_peg[seat], fp);
    bk_counter_set(&V.score[seat], s->p[seat].score);
    bk_counter_update(&V.score[seat], dt);
  }
  /* cards: on offer, held, or gone */
  int held[TR_MAX_P] = { 0 };
  int owner[TR_NCARDS];
  for (int c = 0; c < TR_NCARDS; c++) owner[c] = -1;
  for (int c = 0; c < TR_NCARDS; c++) owner[c] = s->card_owner[c];
  for (int c = 0; c < TR_NCARDS; c++) {
    int slot = -1;
    for (int i = 0; i < 4; i++)
      if (s->card_slot[i] == c) slot = i;
    t3_vec3 deckp = v3(-10.4f, 0.32f, 2.85f);
    bool vis = true;
    if (slot >= 0) glide_to(&V.card_g[c], v3(LY_SITE[TR_S_CARD0 + slot][0], LY_BOARD_Y + 0.002f, LY_SITE[TR_S_CARD0 + slot][1]), 0.6f, 0.6f, 0);
    else if (owner[c] >= 0) glide_to(&V.card_g[c], ly_held_card(owner[c], held[owner[c]]++, n), 0.7f, 1.0f, 0.1f);
    else if (s->card_left[c]) {
      vis = false;
      V.card_g[c] = (glide){ .pos = deckp, .from = deckp, .to = deckp, .placed = true };
    } else { /* burned: rise and fade */
      V.card_fade[c] += dt;
      vis = V.card_fade[c] < 0.8f;
      glide_to(&V.card_g[c], v3(-1.55f, 2.0f, 0.0f), 0.8f, 0.5f, 0);
    }
    glide_step(&V.card_g[c], dt);
    show(V.card[c], vis);
    if (vis) cw_node3d_set_position(V.card[c], V.card_g[c].pos);
    float cs = slot < 0 && owner[c] >= 0 ? 0.6f : 1.0f;
    if (vis) cw_node3d_set_scale(V.card[c], v3(cs, 1, cs));
  }
  int deck = 0;
  for (int c = 0; c < TR_NCARDS; c++) deck += s->card_left[c];
  show(V.deck, deck > 0);
  cw_node3d_set_position(V.deck, v3(-10.4f, 0.01f * deck * 0.5f, 2.85f));
  cw_node3d_set_scale(V.deck, v3(1, fmaxf(0.05f, deck / 30.0f), 1));
  /* buildings: on a pile's top, built, or still hidden */
  int built[TR_MAX_P] = { 0 };
  const int16_t *hut_owner = s->bld_owner;
  for (int b = 0; b < TR_NBUILD; b++) {
    int pile = -1;
    for (int p = 0; p < n; p++)
      if (s->bld_top[p] == b) pile = p;
    bool vis = pile >= 0 || hut_owner[b] >= 0;
    float scale = 1.0f;
    if (pile >= 0) {
      glide_to(&V.hut_g[b], v3(LY_SITE[TR_S_BLD0 + pile][0], LY_BOARD_Y + 0.05f * s->bld_count[pile], LY_SITE[TR_S_BLD0 + pile][1] - 0.1f), 0.6f, 0.4f, 0);
      scale = 1.15f;
    } else if (hut_owner[b] >= 0) {
      glide_to(&V.hut_g[b], ly_hut(hut_owner[b], built[hut_owner[b]]++, n), 0.9f, 1.4f, 0);
      if (!V.hut_g[b].moving && V.hut_rise[b] < 1) V.hut_rise[b] = fminf(1, V.hut_rise[b] + dt * 1.6f);
    }
    glide_step(&V.hut_g[b], dt);
    show(V.hut[b], vis);
    if (!vis) continue;
    float r2 = ease(V.hut_rise[b]);
    if (pile < 0) scale = 0.8f; /* a built hut on its mat */
    float sy = scale * (hut_owner[b] >= 0 && pile < 0 ? (V.hut_g[b].moving ? 0.25f : 0.25f + 0.75f * r2 + 0.08f * sinf(r2 * 3.14159f)) : 1.0f);
    cw_node3d_set_position(V.hut[b], V.hut_g[b].pos);
    cw_node3d_set_scale(V.hut[b], v3(scale, sy, scale));
  }
  /* pile slabs and the top building's cost in goods */
  for (int p = 0; p < 4; p++) {
    int left = p < n ? s->bld_count[p] : 0;
    for (int k = 0; k < TR_PILE; k++) show(V.pile_slab[p][k], k < left - 1 || (k == 0 && left > 0));
    int b = p < n ? s->bld_top[p] : -1;
    int slot = 0;
    for (int r = 0; r < 4; r++)
      for (int k = 0; k < 4; k++) show(V.cost_tok[p][k][r], false);
    if (b < 0) continue;
    for (int r = 0; r < 4; r++)
      for (int k = 0; k < tr_builds[b].cost[r] && slot < 4; k++, slot++) {
        cw_node t = V.cost_tok[p][slot][r];
        show(t, true);
        cw_node3d_set_position(t, v3(LY_SITE[TR_S_BLD0 + p][0] - 0.45f + slot * 0.3f, LY_BOARD_Y + 0.08f, LY_SITE[TR_S_BLD0 + p][1] + 0.72f));
        cw_node3d_set_rotation(t, r == 0 ? v3(1.5708f, 0, 0) : v3(0, 0.3f, 0));
      }
  }
  /* dice */
  if (V.dice_n > 0) {
    V.dice_t += dt;
    for (int i = 0; i < V.dice_n; i++) {
      t3_vec3 pos;
      t3_quat q;
      bk_die_pose(&V.throw_[i], V.dice_t, &pos, &q);
      cw_node3d_set_position(V.die[i], pos);
      cw_node3d_set_quaternion(V.die[i], q);
    }
    if (V.dice_t > V.throw_[0].seconds + V.dice_hold) {
      for (int i = 0; i < V.dice_n; i++) show(V.die[i], false);
      V.dice_n = 0;
    }
  }
  /* the first player's stone */
  glide_to(&V.first_g, v3(ly_mat(s->first, n).x + LY_MAT_W * 0.5f - 0.35f, LY_MAT_Y + 0.05f, LY_MAT_Z - 1.35f), 0.8f, 0.8f, 0);
  glide_step(&V.first_g, dt);
  cw_node3d_set_position(V.first_stone, V.first_g.pos);
  /* the cursor's marker and the ghost preview */
  int t = tg_cursor(g);
  bool human = g->turn >= 0 && !tg_seat_cpu(g, g->turn);
  bool marker = !g->over && !tg_busy(g) && !g->dirty && t >= 0 && t < g->ntargets && g->target[t].kind != TG_T_OPTION;
  show(V.marker, marker);
  if (marker) {
    V.marker_bob += dt;
    t3_vec3 at = g->target[t].at;
    float r = g->target[t].kind == TG_T_COUNT ? 0.45f : 1.0f;
    if (g->target[t].kind == TG_T_SITE) at.y = LY_BOARD_Y + 0.05f;
    cw_node3d_set_position(V.marker, v3(at.x, at.y + 0.05f + 0.04f * sinf(V.marker_bob * 4), at.z));
    cw_node3d_set_scale(V.marker, v3(r * (1 + 0.04f * sinf(V.marker_bob * 4)), r, r));
  }
  bk_move pv = human || g->fast ? tg_preview(g) : BK_MOVE_NONE;
  int gk = 0;
  if (pv != BK_MOVE_NONE && bk_move_field(pv, 0) == TR_M_PLACE && g->turn >= 0) {
    int site = bk_move_field(pv, 1), cnt = bk_move_field(pv, 2), have = s->place[site][g->turn];
    for (; gk < cnt && gk < TR_MAX_KIN; gk++) {
      cw_node3d_set_position(V.ghost[gk], ly_kin_at_site(site, g->turn, have + gk, n));
      show(V.ghost[gk], true);
    }
  }
  for (int k = gk; k < TR_MAX_KIN; k++) show(V.ghost[k], false);
  bk_deltas_update(&V.deltas, dt);
  V.banner_t += dt;
  V.roll_t += dt;
}

/* ── HUD ─────────────────────────────────────────────────────────────── */

static cw_font font(void) { return cw_ui_font_sans(); }
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_metrics m = cw_font_measure(font(), s, &(cw_text_style){ .size = size });
  float ox = al == CW_ALIGN_CENTER ? m.width * 0.5f : al == CW_ALIGN_RIGHT ? m.width : 0;
  cw_r2d_text(font(), s, cw_v2(x - ox, y), &(cw_text_style){ .size = size, .color = c, .outline_width = size * 0.08f,
                                                              .outline_color = cw_rgba(0.05f, 0.03f, 0.02f, 0.85f) });
}
/* a good's icon on the HUD, a shape of its own for each so they never need colour to tell apart: food a
 * drumstick, wood a log end-on, clay a brick, stone a rough hexagon, gold a nugget diamond. r: half its size. */
static void good_icon(int res, cw_vec2 c, float r) {
  switch (res) {
  case TR_FOOD: {
    cw_r2d_rect((cw_rect){ c.x - r * 0.1f, c.y + r * 0.1f, r * 0.9f, r * 0.32f }, rgba(0xf2e6cfff)); /* the bone */
    cw_r2d_circle(cw_v2(c.x + r * 0.85f, c.y + r * 0.12f), r * 0.2f, 12, rgba(0xf2e6cfff));
    cw_r2d_circle(cw_v2(c.x + r * 0.85f, c.y + r * 0.42f), r * 0.2f, 12, rgba(0xf2e6cfff));
    cw_r2d_circle(cw_v2(c.x - r * 0.3f, c.y - r * 0.05f), r * 0.68f, 20, rgba(0xb8442eff));
    cw_r2d_circle(cw_v2(c.x - r * 0.45f, c.y - r * 0.25f), r * 0.22f, 12, rgba(0xe07a5cff));
    break;
  }
  case TR_WOOD:
    cw_r2d_circle(c, r * 0.9f, 22, rgba(0x6b4424ff));
    cw_r2d_circle(c, r * 0.68f, 22, rgba(0xd8a868ff));
    cw_r2d_circle(c, r * 0.42f, 18, rgba(0xb88448ff));
    cw_r2d_circle(c, r * 0.16f, 12, rgba(0x8a5a30ff));
    break;
  case TR_CLAY:
    cw_r2d_round_rect((cw_rect){ c.x - r, c.y - r * 0.55f, 2 * r, r * 1.1f }, r * 0.12f, 4, rgba(0xc4602eff));
    cw_r2d_rect((cw_rect){ c.x - r, c.y - r * 0.04f, 2 * r, r * 0.08f }, rgba(0x7a3416ff));
    cw_r2d_rect((cw_rect){ c.x - r * 0.04f, c.y - r * 0.55f, r * 0.08f, r * 0.5f }, rgba(0x7a3416ff));
    break;
  case TR_STONE: {
    cw_vec2 hx[6];
    static const float J[6] = { 1.0f, 0.82f, 0.95f, 0.88f, 1.0f, 0.8f };
    for (int i = 0; i < 6; i++) hx[i] = cw_v2(c.x + r * J[i] * cosf(0.3f + (float)i * 1.0472f), c.y + r * J[i] * 0.85f * sinf(0.3f + (float)i * 1.0472f));
    cw_r2d_polygon(hx, 6, rgba(0x9a9ca4ff));
    cw_r2d_circle(cw_v2(c.x - r * 0.3f, c.y - r * 0.25f), r * 0.25f, 10, rgba(0xc8cad0ff));
    break;
  }
  case TR_GOLD: {
    cw_vec2 d[4] = { { c.x, c.y - r }, { c.x + r * 0.8f, c.y }, { c.x, c.y + r }, { c.x - r * 0.8f, c.y } };
    cw_r2d_polygon(d, 4, rgba(0xf0c030ff));
    cw_vec2 hi[3] = { { c.x, c.y - r * 0.8f }, { c.x + r * 0.35f, c.y - r * 0.1f }, { c.x - r * 0.3f, c.y - r * 0.1f } };
    cw_r2d_polygon(hi, 3, rgba(0xfff0a0ff));
    break;
  }
  default: break;
  }
}

static void panel(float x, float y, float w, float h, float a) {
  /* even canvas units: whole pixels at 1x and 1.5x, so every host draws the edges alike */
  x = 2 * roundf(x * 0.5f), y = 2 * roundf(y * 0.5f), w = 2 * roundf(w * 0.5f), h = 2 * roundf(h * 0.5f);
  cw_r2d_round_rect((cw_rect){ x, y, w, h }, h * 0.18f, 6, cw_rgba(0.09f, 0.06f, 0.04f, 0.78f * a));
  cw_r2d_round_rect_lines((cw_rect){ x, y, w, h }, h * 0.18f, 2, 6, cw_rgba(0.85f, 0.68f, 0.4f, 0.55f * a));
}

/* what a site gives, in a line */
static void site_line(const tr_state *s, int seat, int site, char *buf, size_t cap) {
  if (site <= TR_S_GOLD) {
    int v = tr_value(site);
    if (site == TR_S_HUNT) snprintf(buf, cap, "Hunting grounds: a die per kin, every 2 pips is 1 food. Room for everyone.");
    else snprintf(buf, cap, "%s: a die per kin, every %d pips is 1 %s (worth %d points built). %d left.", SITE_TITLE[site], v,
                  RES_NAME[site], v, s->supply[site]);
  } else if (site == TR_S_HUT) snprintf(buf, cap, "Hearth: send 2 kin, a new kin joins the village (kin %d of %d).", s->p[seat].kin, TR_MAX_KIN);
  else if (site == TR_S_FIELD) snprintf(buf, cap, "Garden: field +1, it gives 1 food every round (now %d).", s->p[seat].field);
  else if (site == TR_S_TOOL) snprintf(buf, cap, "Tool bench: +1 to your weakest tool (tools add to any roll, once a round).");
  else if (site <= TR_S_CARD3) {
    int slot = site - TR_S_CARD0, c = s->card_slot[slot];
    if (c < 0) { snprintf(buf, cap, "An empty card spot."); return; }
    const tr_card_def *d = &tr_cards[c];
    char fx[64] = "";
    static const char *const FXN[8] = { "food", "wood", "clay", "stone", "gold", "points", "field", "tool" };
    for (int i = 0; i < 2; i++)
      if (d->fx[i][0] >= 0) {
        char one[24];
        snprintf(one, sizeof one, "%s%d %s", fx[0] ? ", " : "", d->fx[i][1], FXN[(int)d->fx[i][0]]);
        strncat(fx, one, sizeof fx - strlen(fx) - 1);
      }
    if (d->mark >= 0)
      snprintf(buf, cap, "%s: costs %d goods. Now: %s. End: the %s mark for your set of different marks.", d->name, slot + 1,
               fx[0] ? fx : "nothing", tr_mark_names[(int)d->mark]);
    else {
      static const char *const PER[4] = { "field level", "tool strength", "building", "kin" };
      snprintf(buf, cap, "%s: costs %d goods. Now: %s. End: %d points per %s.", d->name, slot + 1, fx[0] ? fx : "nothing", d->mult,
               PER[(int)d->role]);
    }
  } else {
    int p = site - TR_S_BLD0, b = s->bld_top[p];
    if (b < 0) { snprintf(buf, cap, "An empty building yard."); return; }
    const int8_t *c = tr_builds[b].cost;
    snprintf(buf, cap, "%s: pay %d wood %d clay %d stone %d gold, score %d. %d left in this pile.", tr_builds[b].name, c[0], c[1],
             c[2], c[3], tr_builds[b].points, s->bld_count[p]);
  }
}

static void option_label(const tr_state *s, bk_move m, char *buf, size_t cap) {
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1);
  if (s->step < 0 || s->step >= TR_NSITES) { snprintf(buf, cap, "?"); return; }
  if (kind == TR_M_TOOLS) {
    const tr_player *p = &s->p[s->cur];
    int t = 0;
    for (int i = 0; i < p->ntools; i++)
      if (a & (1 << i)) t += p->tools[i];
    int v = tr_value(s->step);
    if (t) snprintf(buf, cap, "Add tools +%d: %d %s", t, (s->pend + t) / v, RES_NAME[s->step]);
    else snprintf(buf, cap, "Keep tools: %d %s", s->pend / v, RES_NAME[s->step]);
  } else if (kind == TR_M_BUY) {
    if (s->step <= TR_S_CARD3) snprintf(buf, cap, a ? "Take the card (%d goods)" : "Let it go", s->step - TR_S_CARD0 + 1);
    else snprintf(buf, cap, a ? "Build it (+%d)" : "Let it go", s->bld_top[s->step - TR_S_BLD0] >= 0 ? tr_builds[s->bld_top[s->step - TR_S_BLD0]].points : 0);
  } else if (kind == TR_M_FEED) {
    snprintf(buf, cap, a ? "Feed with goods" : "Go hungry (-10)");
  } else snprintf(buf, cap, "?");
}

static const char *const HELP[] = {
  "First Hearth: how to play",
  "Each round has three parts.",
  "1. Send kin out. Take turns sending one or more kin to one place, until all are out.",
  "   Forest, clay bank, quarry and river: roll a die per kin. Every 3, 4, 5 or 6 pips (wood, clay, stone, gold) is one good.",
  "   Hunting grounds: every 2 pips is one food. Hearth: 2 kin bring a new kin. Garden: +1 food every round.",
  "   Tool bench: a tool adds to one roll each round. Cards and buildings: buy them with goods when your kin come home.",
  "2. Bring kin home: roll for each gathering place, then take what you can pay for.",
  "3. Feed the village: each kin eats 1 food. Short? Pay goods, or go hungry and lose 10 points.",
  "The game ends when the cards run out or a building pile is empty. Buildings score their value.",
  "At the end: 1 point per good, different lore marks score their count squared, kin roles multiply.",
};

void view_hud(const tg_game *g, float dt) {
  if (!g->match) return;
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  const tr_state *s = &g->shown;
  int n = g->players;
  view_stage = 10;
  /* seats along the top */
  float w = 300 * u, h = 64 * u, gap = 14 * u, x0 = sz.x * 0.5f - (n * w + (n - 1) * gap) * 0.5f;
  x0 = 2 * floorf(x0 * 0.5f);
  for (int i = 0; i < n; i++) {
    float x = x0 + i * (w + gap), y = 10 * u;
    bool turn = !g->over && g->turn == i && !tg_busy(g);
    panel(x, y, w, h, 1);
    if (turn) cw_r2d_round_rect_lines((cw_rect){ x - 2 * u, y - 2 * u, w + 4 * u, h + 4 * u }, h * 0.2f, 3 * u, 6, rgba(SEAT_RGBA[i]));
    cw_r2d_circle(cw_v2(x + 18 * u, y + 20 * u), 8 * u, 16, rgba(SEAT_RGBA[i]));
    char line[96];
    cw_shell_cpu_level lv;
    bool cpu = tg_seat_cpu(g, i);
    lv = cpu ? (cw_shell_cpu_level)g->cpu_level[i] : CW_SHELL_CPU_EASY;
    snprintf(line, sizeof line, "%s%s%s", tg_seat_name(i), cpu ? "  CPU " : "", cpu ? cw_shell_cpu_level_label(lv) : "");
    text(line, x + 32 * u, y + 8 * u, 20 * u, rgba(SEAT_RGBA[i]), CW_ALIGN_LEFT);
    snprintf(line, sizeof line, "%d", bk_counter_value(&V.score[i]));
    text(line, x + w - 12 * u, y + 6 * u, 26 * u, rgba(0xffe9b0ff), CW_ALIGN_RIGHT);
    const tr_player *p = &s->p[i];
    snprintf(line, sizeof line, "kin %d   food %d   field %d   tools %d   cards %d", p->kin, p->res[TR_FOOD], p->field, tr_tool_total(p), p->ncards);
    text(line, x + 12 * u, y + 37 * u, 15 * u, rgba(0xe8dcc4ff), CW_ALIGN_LEFT);
  }
  /* the place names on the board, small, at each place top edge */
  if (!g->over && !g->help) {
    /* each place's name, and under it what a die brings there in plain words */
    static const char *const TAG[8] = { "Hunting grounds", "Forest", "Clay bank", "Quarry", "River", "Hearth", "Garden", "Tool bench" };
    static const char *const GIVES[8] = { "2 pips per food", "3 pips per wood", "4 pips per clay", "5 pips per stone", "6 pips per gold",
                                          "2 kin: a new kin", "+1 food a round", "+1 tool" };
    for (int site = 0; site < 8; site++) {
      /* (the hunting grounds name sits higher: the card prices are under its edge) */
      cw_vec2 p = to_screen(v3(LY_SITE[site][0], LY_BOARD_Y, LY_SITE[site][1] + LY_SITE[site][3] * (site == TR_S_HUNT ? 0.45f : 0.78f)));
      text(TAG[site], p.x, p.y - 22 * u, 17 * u, cw_rgba(1, 0.94f, 0.8f, 0.95f), CW_ALIGN_CENTER);
      text(GIVES[site], p.x, p.y - 2 * u, 15 * u, cw_rgba(1, 0.9f, 0.7f, 0.92f), CW_ALIGN_CENTER);
    }
    /* each card spot's price above it, in goods */
    for (int k = 0; k < 4; k++) {
      int site = TR_S_CARD0 + k;
      cw_vec2 p = to_screen(v3(LY_SITE[site][0], LY_BOARD_Y, LY_SITE[site][1] - LY_SITE[site][3] - 0.02f));
      char lab[24];
      snprintf(lab, sizeof lab, "%d good%s", k + 1, k ? "s" : "");
      float lw = 70 * u;
      cw_r2d_round_rect((cw_rect){ p.x - lw * 0.5f, p.y - 12 * u, lw, 22 * u }, 6 * u, 6, cw_rgba(0.1f, 0.065f, 0.04f, 0.75f));
      text(lab, p.x, p.y - 11 * u, 15 * u, rgba(0xfff0d0ff), CW_ALIGN_CENTER);
    }
  }
  view_stage = 11;
  /* each mat's goods: a chip in front of each pile with the good's own icon, its count and its name */
  for (int i = 0; i < n; i++)
    for (int r = 0; r < TR_NRES; r++) {
      t3_vec3 at = ly_goods(i, r, n);
      at.z += 0.36f;
      cw_vec2 p = to_screen(at);
      /* (the piles sit about 40 canvas units apart with four mats: an icon of its own shape and a big count; the
       * top panel and the how-to-play card name them) */
      float cw = 38 * u, ch = 28 * u;
      cw_r2d_round_rect((cw_rect){ p.x - cw * 0.5f, p.y - ch * 0.5f, cw, ch }, 6 * u, 6, cw_rgba(0.1f, 0.065f, 0.04f, 0.82f));
      good_icon(r, cw_v2(p.x - 9 * u, p.y), 8.5f * u);
      char num[8];
      snprintf(num, sizeof num, "%d", s->p[i].res[r]);
      text(num, p.x + 9 * u, p.y - 12 * u, 21 * u, rgba(0xfff4dcff), CW_ALIGN_CENTER);
    }
  view_stage = 12;
  /* the round and the deck */
  {
    char line[64];
    int deck = 0;
    for (int c = 0; c < TR_NCARDS; c++) deck += s->card_left[c];
    snprintf(line, sizeof line, "Round %d   cards left %d", s->round, deck);
    text(line, 16 * u, sz.y - 34 * u, 17 * u, rgba(0xe8dcc4cc), CW_ALIGN_LEFT);
  }
  view_stage = 13;
  /* the roll line and the round banner, eased in and out */
  if (V.roll_text[0] && V.roll_t < 3.0f) {
    float a = clampf(V.roll_t * 3, 0, 1) * clampf((3.0f - V.roll_t) * 2, 0, 1);
    panel(sz.x * 0.5f - 300 * u, 84 * u, 600 * u, 38 * u, a);
    text(V.roll_text, sz.x * 0.5f, 92 * u, 20 * u, cw_rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER);
  }
  if (V.banner[0] && V.banner_t < 2.4f && !g->over) {
    float a = clampf(V.banner_t * 2, 0, 1) * clampf((2.4f - V.banner_t) * 2.0f, 0, 1);
    panel(sz.x * 0.5f - 380 * u, 128 * u, 760 * u, 44 * u, a);
    text(V.banner, sz.x * 0.5f, 137 * u, 21 * u, cw_rgba(1, 0.9f, 0.7f, a), CW_ALIGN_CENTER);
  }
  view_stage = 14;
  bk_deltas_draw(&V.deltas, font(), 26 * u);
  view_stage = 15;
  /* the choice in progress */
  bool human = g->turn >= 0 && !tg_seat_cpu(g, g->turn);
  /* (only once the game has laid out the targets for this step: they are stale for the tick after a step) */
  if (!g->over && !tg_busy(g) && g->turn >= 0 && !g->dirty) {
    int t = tg_cursor(g);
    char line[200];
    if (g->mode == UI_MENU) {
      for (int i = 0; i < g->ntargets; i++) {
        char lab[64];
        option_label(s, g->target[i].move, lab, sizeof lab);
        cw_vec2 p = to_screen(g->target[i].at);
        float bw = 300 * u, bh = 46 * u;
        bool on = i == t;
        cw_r2d_round_rect((cw_rect){ p.x - bw / 2, p.y - bh / 2, bw, bh }, 10 * u, 6,
                          on ? cw_rgba(0.95f, 0.78f, 0.42f, 0.95f) : cw_rgba(0.1f, 0.07f, 0.05f, 0.85f));
        text(lab, p.x, p.y - 12 * u, 21 * u, on ? cw_rgba(0.12f, 0.07f, 0.03f, 1) : cw_rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
      }
      const char *what = s->need == TR_NEED_TOOLS ? "Use tools on this roll?" : s->need == TR_NEED_BUY ? "Your kin are back with goods:" : "Not enough food:";
      snprintf(line, sizeof line, "%s %s", tg_seat_name(g->turn), what);
      text(line, sz.x * 0.5f, sz.y * 0.62f, 22 * u, rgba(SEAT_RGBA[g->turn]), CW_ALIGN_CENTER);
      if (s->need == TR_NEED_BUY) {
        char info[200];
        site_line(s, g->turn, s->step, info, sizeof info);
        text(info, sz.x * 0.5f, sz.y * 0.62f + 30 * u, 18 * u, rgba(0xf0e4ccff), CW_ALIGN_CENTER);
      }
    } else if (t >= 0 && t < g->ntargets) {
      const tg_target *tt = &g->target[t];
      if (g->mode == UI_COUNT) {
        for (int i = 0; i < g->ntargets; i++) {
          cw_vec2 p = to_screen(g->target[i].at);
          char num[8];
          snprintf(num, sizeof num, "%d", g->target[i].value);
          bool on = i == t;
          cw_r2d_circle(p, 14 * u, 20, on ? cw_rgba(0.95f, 0.78f, 0.42f, 0.95f) : cw_rgba(0.1f, 0.07f, 0.05f, 0.85f));
          text(num, p.x, p.y - 10 * u, 18 * u, on ? cw_rgba(0.12f, 0.07f, 0.03f, 1) : cw_rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
        }
        int site = tt->site, k = tt->value;
        if (site == TR_S_HUNT) snprintf(line, sizeof line, "Send %d to hunt: about %.1f food", k, k * 3.5f / 2.0f);
        else snprintf(line, sizeof line, "Send %d to the %s: about %.1f %s", k, tr_site_name(site), k * 3.5f / (float)tr_value(site), RES_NAME[site]);
      } else {
        site_line(s, g->turn, tt->site, line, sizeof line);
      }
      panel(sz.x * 0.5f - 470 * u, 424 * u, 940 * u, 36 * u, 1); /* (even canvas units: whole pixels at 1.5x too) */
      text(line, sz.x * 0.5f, 431 * u, 18 * u, rgba(0xf4ead6ff), CW_ALIGN_CENTER);
      if (!human) {
        snprintf(line, sizeof line, "%s is choosing", tg_seat_name(g->turn));
        text(line, sz.x * 0.5f, 392 * u, 20 * u, rgba(SEAT_RGBA[g->turn]), CW_ALIGN_CENTER);
      }
    }
    if (human) {
      const char *pr = g->mode == UI_COUNT ? "{left|right} how many    {choose} send    {back} another place    {how_to_play} how to play"
                     : g->mode == UI_MENU ? "{left|right} choose    {choose} confirm    {how_to_play} how to play"
                                          : "{left|right} {up|down} a place    {choose} send kin    {how_to_play} how to play";
      char head[64];
      snprintf(head, sizeof head, "%s, your turn", tg_seat_name(g->turn));
      text(head, sz.x * 0.5f, 390 * u, 22 * u, rgba(SEAT_RGBA[g->turn]), CW_ALIGN_CENTER);
      cw_prompt_text(font(), pr, g->turn, cw_v2(sz.x * 0.5f, sz.y - 52 * u), 19 * u, cw_rgba(1, 0.96f, 0.9f, 0.95f), CW_ALIGN_CENTER);
    }
  }
  view_stage = 16;
  /* the final scores */
  if (g->over) {
    float pw = 760 * u, ph = (140 + 52 * n) * u, px = sz.x * 0.5f - pw * 0.5f, py = sz.y * 0.5f - ph * 0.5f;
    float a = clampf(g->over_ticks / 30.0f, 0, 1);
    panel(px, py, pw, ph, a);
    panel(px, py, pw, ph, a); /* twice: the table must not show through the scores */
    int win = tr_winner(s);
    char line[160];
    snprintf(line, sizeof line, "%s's village thrives", tg_seat_name(win));
    text(line, sz.x * 0.5f, py + 18 * u, 32 * u, rgba(SEAT_RGBA[win]), CW_ALIGN_CENTER);
    text("score    goods    lore marks    kin roles    total", px + pw * 0.62f, py + 66 * u, 16 * u, rgba(0xcdbf9fff), CW_ALIGN_CENTER);
    for (int i = 0; i < n; i++) {
      int parts[4];
      tr_end_bonus(s, i, parts);
      snprintf(line, sizeof line, "%s", tg_seat_name(i));
      text(line, px + 30 * u, py + (96 + 52 * i) * u, 24 * u, rgba(SEAT_RGBA[i]), CW_ALIGN_LEFT);
      snprintf(line, sizeof line, "%4d     +%d        +%d             +%d          %d", s->p[i].score, parts[0], parts[1], parts[2],
               tr_final_score(s, i));
      text(line, px + pw * 0.62f, py + (98 + 52 * i) * u, 22 * u, rgba(0xfff0d0ff), CW_ALIGN_CENTER);
    }
    cw_prompt_text(font(), "{choose} back to the title", -1, cw_v2(sz.x * 0.5f, py + ph - 36 * u), 18 * u, cw_rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER);
  }
  if (g->help) view_help(sz, true);
  (void)dt;
}

void view_help(cw_vec2 sz, bool close_prompt) {
  float u = sz.y / 720.0f;
  float pw = 1100 * u, ph = 420 * u, px = sz.x * 0.5f - pw * 0.5f, py = sz.y * 0.5f - ph * 0.5f;
  panel(px, py, pw, ph, 1);
  for (unsigned i = 0; i < sizeof HELP / sizeof HELP[0]; i++)
    text(HELP[i], px + 28 * u, py + (20 + i * 36) * u, (i ? 18 : 26) * u, i ? rgba(0xf0e4ccff) : rgba(0xffd890ff), CW_ALIGN_LEFT);
  if (close_prompt) cw_prompt_text(font(), "{choose} close", -1, cw_v2(sz.x * 0.5f, py + ph - 34 * u), 18 * u, cw_rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
}
