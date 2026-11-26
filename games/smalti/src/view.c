/* Smalti: the table in 3D and the HUD over it. See view.h. */
#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "boardkit/bk_shell.h"
#include "three.h"

#define NTILES (MS_COLOURS * MS_TILES_PER_COLOUR)
#define TABLE_Y 0.0f
#define BOARD_TOP 0.22f
#define TRAY_TOP 0.16f
#define POOL_TOP 0.07f

/* the glass colours (linear-ish, for the HUD and tints) in the rules' order: cobalt, amber, coral, jade, pearl */
static const uint32_t GLASS_HEX[MS_COLOURS] = { 0x2f62d0ff, 0xf0ad2aff, 0xe0533fff, 0x27a877ff, 0xefe9dcff };

/* a slot a tile can stand in: what holds it and where */
enum { SL_HIDDEN, SL_TRAY, SL_POOL, SL_LINE, SL_FLOOR, SL_WALL };
typedef struct slot {
  uint8_t kind, a, b, c; /* tray: a tray, b slot; pool: a colour, b index; line: a seat, b row, c k; floor: a seat, b index;
                          * wall: a seat, b row, c col */
} slot;
static uint32_t slot_key(slot s) { return (uint32_t)s.kind << 24 | (uint32_t)s.a << 16 | (uint32_t)s.b << 8 | s.c; }

typedef struct tile_view {
  cw_node node;
  int colour;
  slot at;          /* where it is going (or is) */
  t3_vec3 from, to; /* the glide */
  t3_vec3 pos, prev;
  float yaw, yaw_from, yaw_to, prev_yaw;
  int t, dur, delay; /* ticks */
  int settle;        /* ticks of the landing settle left */
  bool moving, hide_on_arrival, visible, hi;
  float lift, prev_lift; /* picked-group lift */
} tile_view;

typedef struct popup {
  t3_vec3 at;
  char text[24];
  uint32_t colour;
  int age, life;
} popup;
#define POPUPS 24

static struct {
  cw_tree *tree;
  cw_node world, camera, table_root;
  t3_material *glass[MS_COLOURS], *glass_hi[MS_COLOURS], *ghost[MS_COLOURS];
  t3_geometry *tile_geo;
  tile_view tile[NTILES];
  cw_node marker;
  t3_vec3 marker_pos, marker_from, marker_to, marker_prev;
  int marker_t, marker_dur;
  uint32_t marker_key;
  cw_node boards[BK_MAX_SEATS], frames[BK_MAX_SEATS], trays[MS_MAX_TRAYS], pool;
  cw_node rowhit[BK_MAX_SEATS][MS_SIZE + 1];
  cw_node ghost_nodes[12];
  int ghost_count;
  float glow[BK_MAX_SEATS];
  int players, trays_n;
  ly_table lay;
  uint64_t shown_hash;
  int ev_seen;
  popup pop[POPUPS];
  float shown_score[BK_MAX_SEATS];
  int ticks;
  /* sounds */
  cw_sound snd_clack, snd_chime, snd_thud, snd_slide, snd_turn;
  bool sound_ok;
  cw_res res[24];
  int nres;
  bool attract; /* the title screens' mosaic panel is laid out */
} V;

/* ── helpers ─────────────────────────────────────────────────────────── */

/* srgb: a colour picture (albedo) rather than data (a normal map), said before its first load (a PNG would load as
 * data; a cart's KTX2 says so in its DFD, and this agrees with it) */
static t3_texture *texture(const char *path, bool srgb) {
  if (!cw_res_texture_set_srgb(path, srgb)) CW_FATAL("smalti: %s was loaded the other way before it could be said to be %s", path, srgb ? "colour" : "data");
  cw_res r = cw_res_load(path, CW_RES_TEXTURE);
  if (cw_res_wait(r) != CW_RES_STATE_READY) CW_FATAL("smalti: texture %s did not load", path);
  if (V.nres < (int)(sizeof V.res / sizeof V.res[0])) V.res[V.nres++] = r;
  t3_texture *t = cw_res_texture(r);
  if (!t) CW_FATAL("smalti: %s is not a texture", path);
  return t;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_geometry *geo, t3_material *mat, bool cast, bool receive) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  t3_mesh *m = t3_mesh_new(geo, mat);
  m->base.cast_shadow = cast;
  m->base.receive_shadow = receive;
  if (!cw_mesh3d_set_procedural(n, m)) CW_FATAL("smalti: could not attach the mesh of %s", name);
  t3_release(m);
  return n;
}

static void mesh_material(t3_mesh *m, t3_material *mat) {
  if (m->material_count < 1 || m->materials[0] == mat) return;
  t3_retain(mat);
  t3_release(m->materials[0]);
  m->materials[0] = mat;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

/* A tile: a slab with its top edges chamfered (so it catches a rim of light), the top face carrying the whole
 * texture and the chamfers and sides its border. Size w x h x w, centred on x/z, its base at y 0. */
static t3_geometry *tile_geometry(float w, float h, float bevel) {
  enum { MAXV = 64, MAXI = 128 };
  float pos[MAXV * 3], nrm[MAXV * 3], uv[MAXV * 2];
  uint16_t idx[MAXI];
  int nv = 0, ni = 0;
  float hw = w * 0.5f, iw = hw - bevel, top = h, mid = h - bevel;
  float uin = bevel / w;
#define V3(x, y, z, nx, ny, nz, u, v) (pos[nv * 3] = (x), pos[nv * 3 + 1] = (y), pos[nv * 3 + 2] = (z), nrm[nv * 3] = (nx), \
                                       nrm[nv * 3 + 1] = (ny), nrm[nv * 3 + 2] = (nz), uv[nv * 2] = (u), uv[nv * 2 + 1] = (v), nv++)
#define QUAD(a, b, c, d) (idx[ni++] = (uint16_t)(a), idx[ni++] = (uint16_t)(b), idx[ni++] = (uint16_t)(c), \
                          idx[ni++] = (uint16_t)(a), idx[ni++] = (uint16_t)(c), idx[ni++] = (uint16_t)(d))
  /* top: u along x, v along z */
  int b0 = nv;
  V3(-iw, top, -iw, 0, 1, 0, uin, uin);
  V3(-iw, top, iw, 0, 1, 0, uin, 1 - uin);
  V3(iw, top, iw, 0, 1, 0, 1 - uin, 1 - uin);
  V3(iw, top, -iw, 0, 1, 0, 1 - uin, uin);
  QUAD(b0, b0 + 1, b0 + 2, b0 + 3);
  /* four sides: a chamfer strip and a wall each */
  static const float dir[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
  float s = 0.7071f;
  for (int k = 0; k < 4; k++) {
    float dx = dir[k][0], dz = dir[k][1];
    float tx = -dz, tz = dx; /* along the edge */
    /* chamfer: from the top's edge (inset) out and down */
    int c0 = nv;
    float u0 = 0.0f, u1 = 1.0f;
    V3(dx * iw - tx * iw, top, dz * iw - tz * iw, dx * s, s, dz * s, u0, 1 - uin);
    V3(dx * iw + tx * iw, top, dz * iw + tz * iw, dx * s, s, dz * s, u1, 1 - uin);
    V3(dx * hw + tx * hw, mid, dz * hw + tz * hw, dx * s, s, dz * s, u1, 1);
    V3(dx * hw - tx * hw, mid, dz * hw - tz * hw, dx * s, s, dz * s, u0, 1);
    QUAD(c0, c0 + 1, c0 + 2, c0 + 3);
    int w0 = nv;
    V3(dx * hw - tx * hw, mid, dz * hw - tz * hw, dx, 0, dz, u0, 1);
    V3(dx * hw + tx * hw, mid, dz * hw + tz * hw, dx, 0, dz, u1, 1);
    V3(dx * hw + tx * hw, 0, dz * hw + tz * hw, dx, 0, dz, u1, 0.9f);
    V3(dx * hw - tx * hw, 0, dz * hw - tz * hw, dx, 0, dz, u0, 0.9f);
    QUAD(w0, w0 + 1, w0 + 2, w0 + 3);
  }
#undef V3
#undef QUAD
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, nrm, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  a = t3_attribute_new(T3_UINT16, idx, ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  t3_geometry_compute_bounding_box(g);
  t3_geometry_compute_bounding_sphere(g);
  return g;
}

/* ── sounds (synthesised: short glass and wood sounds) ──────────────── */

static cw_sound synth(int kind) {
  const int rate = 44100;
  int n = rate * (kind == 1 ? 9 : 4) / 10;
  float *pcm = calloc((size_t)n, sizeof *pcm);
  uint32_t r = 1234567u + (uint32_t)kind * 7919u;
  for (int i = 0; i < n; i++) {
    float t = (float)i / rate;
    r = r * 1664525u + 1013904223u;
    float noise = ((r >> 9) / 4194304.0f) - 1.0f;
    float v = 0;
    switch (kind) {
    case 0: /* clack: glass on glass, a bright knock */
      v = (sinf(6.2832f * 2350 * t) * 0.5f + sinf(6.2832f * 3710 * t) * 0.3f + sinf(6.2832f * 5230 * t) * 0.2f) * expf(-t * 55) +
          noise * expf(-t * 300) * 0.35f;
      break;
    case 1: /* chime: a soft bell, fifths */
      v = (sinf(6.2832f * 880 * t) * 0.5f + sinf(6.2832f * 1320 * t) * 0.28f + sinf(6.2832f * 2640 * t) * 0.1f) *
          expf(-t * 4.5f) * fminf(1, t * 200);
      break;
    case 2: /* thud: a tile dropped on the strip */
      v = sinf(6.2832f * (180 - 90 * t) * t) * expf(-t * 22) * 0.8f + noise * expf(-t * 90) * 0.25f;
      break;
    case 3: /* slide: tiles over wood */
      v = noise * 0.22f * sinf(3.1416f * clampf(t / 0.38f, 0, 1));
      break;
    default: /* turn: a low wooden tock */
      v = sinf(6.2832f * 420 * t) * expf(-t * 40) * 0.6f + sinf(6.2832f * 630 * t) * expf(-t * 60) * 0.3f;
    }
    pcm[i] = v * 0.6f;
  }
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, rate, &(cw_sound_desc){ .max_voices = 6, .bus = cw_shell_sfx_bus() });
  free(pcm);
  return s;
}

void view_sound_init(void) {
  V.snd_clack = synth(0);
  V.snd_chime = synth(1);
  V.snd_thud = synth(2);
  V.snd_slide = synth(3);
  V.snd_turn = synth(4);
  V.sound_ok = true;
}

static void play(cw_sound s, float vol, float pitch) {
  if (!V.sound_ok) return;
  cw_audio_play(s, &(cw_play_desc){ .volume = vol, .pitch = pitch });
}

/* ── the scene ───────────────────────────────────────────────────────── */

static t3_material *physical(uint32_t rgb, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(rgb);
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

void view_init(cw_tree *t) {
  memset(&V, 0, sizeof V);
  V.tree = t;
  V.world = cw_node_get(cw_tree_root(t), "World");
  if (!cw_node_valid(V.world)) V.world = cw_tree_root(t);
  V.camera = cw_node_get(V.world, "Camera");
  if (!cw_node_valid(V.camera)) CW_FATAL("smalti: main.cwscene has no Camera");
  V.table_root = cw_node_new("Node3D", "Table");
  cw_node_add_child(V.world, V.table_root);

  /* the table: walnut, and a linen runner down the middle under the trays */
  t3_texture *walnut = texture("textures/walnut.png", true);
  walnut->wrap_s = walnut->wrap_t = T3_REPEAT;
  t3_material *wood = physical(0xffffff, 0.55f, 0);
  t3_material_set_map(wood, walnut);
  wood->clearcoat = 0.35f;
  wood->clearcoat_roughness = 0.35f;
  t3_geometry *tg = t3_plane_geometry_new(90, 60, 1, 1);
  {
    t3_attribute *uv = tg->attributes[T3_ATTR_UV];
    float *f = (float *)uv->array;
    for (int i = 0; i < uv->count * 2; i += 2) f[i] *= 6, f[i + 1] *= 4;
  }
  cw_node table = mesh_node(V.table_root, "Wood", tg, wood, false, true);
  cw_node3d_set_rotation(table, (t3_vec3){ -1.5708f, 0, 0 });
  t3_release(tg);
  t3_release(wood);

  t3_texture *linen = texture("textures/linen.png", true);
  linen->wrap_s = linen->wrap_t = T3_REPEAT;
  t3_material *cloth = physical(0xffffff, 1.0f, 0);
  t3_material_set_map(cloth, linen);
  cloth->sheen = 0.6f;
  cloth->sheen_roughness = 0.8f;
  cloth->sheen_color = (t3_color){ 0.35f, 0.55f, 0.55f };
  t3_geometry *rg = t3_box_geometry_new(10.2f, 0.04f, 60, 1, 1, 1);
  {
    t3_attribute *uv = rg->attributes[T3_ATTR_UV];
    float *f = (float *)uv->array;
    for (int i = 0; i < uv->count * 2; i += 2) f[i] *= 4, f[i + 1] *= 24;
  }
  cw_node runner = mesh_node(V.table_root, "Runner", rg, cloth, false, true);
  cw_node3d_set_position(runner, (t3_vec3){ 0, 0.02f, 0 });
  t3_release(rg);
  t3_release(cloth);

  /* glass */
  static const char *const NAMES[MS_COLOURS] = { "cobalt", "amber", "coral", "jade", "pearl" };
  V.tile_geo = tile_geometry(LY_TILE, LY_TILE_H, 0.07f);
  for (int c = 0; c < MS_COLOURS; c++) {
    char p[64];
    snprintf(p, sizeof p, "textures/tile_%s.png", NAMES[c]);
    t3_texture *albedo = texture(p, true);
    snprintf(p, sizeof p, "textures/tile_%s_n.png", NAMES[c]);
    t3_texture *normal = texture(p, false);
    t3_material *m = physical(0xffffff, c == 4 ? 0.22f : 0.14f, 0);
    t3_material_set_map(m, albedo);
    t3_material_set_texture(m, T3_NORMAL_MAP, normal);
    m->normal_scale = (t3_vec2){ 0.6f, 0.6f };
    /* Venetian glass: a hard glossy skin over a body that stays matte enough to read, and a thin oily film that
     * turns the highlights a little rainbow at a glancing angle (the pearl, mother-of-pearl, the most) */
    m->clearcoat = 1.0f;
    m->clearcoat_roughness = 0.04f;
    m->ior = 1.52f;
    m->iridescence = c == 4 ? 0.6f : 0.3f;
    m->iridescence_ior = 1.4f;
    static const t3_color GLOW[MS_COLOURS] = { { 0.25f, 0.45f, 1.0f }, { 1.0f, 0.7f, 0.2f }, { 1.0f, 0.35f, 0.25f }, { 0.2f, 0.9f, 0.6f }, { 0.8f, 0.8f, 0.75f } };
    m->emissive = GLOW[c]; /* a breath of light through the glass, far below the cursor's glow */
    m->emissive_intensity = 0.03f;
    V.glass[c] = m;
    t3_material *hi = t3_material_clone(m); /* the group under the cursor: lit from within, gently */
    hi->emissive = GLOW[c];
    hi->emissive_intensity = 0.55f;
    V.glass_hi[c] = hi;
    t3_material *gh = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_map(gh, albedo);
    gh->transparent = true;
    gh->opacity = 0.72f;
    gh->depth_write = false;
    gh->roughness = 0.3f;
    gh->emissive = GLOW[c];
    gh->emissive_intensity = 0.35f;
    V.ghost[c] = gh;
  }
  for (int i = 0; i < NTILES; i++) {
    tile_view *tv = &V.tile[i];
    tv->colour = i / MS_TILES_PER_COLOUR;
    char name[24];
    snprintf(name, sizeof name, "Tile%d", i);
    tv->node = mesh_node(V.table_root, name, V.tile_geo, V.glass[tv->colour], true, true);
    cw_node_add_group(tv->node, "tile");
    cw_node_set_user(tv->node, (void *)(intptr_t)(i + 1));
    cw_node_set(tv->node, "visible", cw_val_bool(false));
    tv->at = (slot){ SL_HIDDEN, 0, 0, 0 };
  }
  for (int i = 0; i < 12; i++) {
    V.ghost_nodes[i] = mesh_node(V.table_root, "Ghost", V.tile_geo, V.ghost[0], false, false);
    cw_node_set(V.ghost_nodes[i], "visible", cw_val_bool(false));
  }
  /* the first-player marker: a gilded glass roundel */
  t3_geometry *mg = t3_cylinder_geometry_new(0.52f, 0.56f, 0.34f, 40, 1, false);
  t3_material *gold = physical(0xe8b440, 0.18f, 0);
  gold->emissive = (t3_color){ 0.12f, 0.07f, 0.0f };
  gold->clearcoat = 1;
  gold->clearcoat_roughness = 0.05f;
  V.marker = mesh_node(V.table_root, "Marker", mg, gold, true, true);
  t3_release(mg);
  t3_release(gold);
  view_sound_init();
}

static void clear_layout_nodes(void) {
  for (int i = 0; i < BK_MAX_SEATS; i++) {
    if (cw_node_valid(V.boards[i])) cw_node_free(V.boards[i]);
    if (cw_node_valid(V.frames[i])) cw_node_free(V.frames[i]);
    V.boards[i] = V.frames[i] = (cw_node){ 0 };
    for (int r = 0; r <= MS_SIZE; r++) {
      if (cw_node_valid(V.rowhit[i][r])) cw_node_free(V.rowhit[i][r]);
      V.rowhit[i][r] = (cw_node){ 0 };
    }
  }
  for (int i = 0; i < MS_MAX_TRAYS; i++) {
    if (cw_node_valid(V.trays[i])) cw_node_free(V.trays[i]);
    V.trays[i] = (cw_node){ 0 };
  }
  if (cw_node_valid(V.pool)) cw_node_free(V.pool);
  V.pool = (cw_node){ 0 };
}

static t3_vec3 slot_pos(slot s, const ms_state *st) {
  (void)st;
  ly_point p;
  float y = TABLE_Y;
  int layer = 0;
  switch (s.kind) {
  case SL_TRAY: p = ly_tray_slot(&V.lay, s.a, s.b); y = TRAY_TOP; break;
  case SL_POOL: p = ly_pool_slot(&V.lay, s.a, s.b, &layer); y = POOL_TOP + 0.02f + layer * LY_TILE_H; break;
  case SL_LINE: p = ly_line_cell(&V.lay, s.a, s.b, s.c); y = BOARD_TOP; break;
  case SL_FLOOR: p = ly_floor_cell(&V.lay, s.a, s.b); y = BOARD_TOP; break;
  case SL_WALL: p = ly_wall_cell(&V.lay, s.a, s.b, s.c); y = BOARD_TOP; break;
  default: p = V.lay.bag; y = 2.0f;
  }
  return (t3_vec3){ p.x, y, p.z };
}

void view_start(const mg_game *g) {
  V.attract = false;
  clear_layout_nodes();
  V.players = g->players;
  V.lay = g->lay;
  V.trays_n = g->lay.trays;
  t3_texture *bt = texture("textures/board.png", true);
  t3_material *bm = physical(0xcfc6b8, 0.7f, 0);
  t3_material_set_map(bm, bt);
  bm->diffuse_roughness = 0.4f;
  t3_geometry *bg = t3_box_geometry_new(LY_BOARD_W, BOARD_TOP, LY_BOARD_H, 1, 1, 1);
  { /* the texture loads with row 0 at v 0 (KTX2 orientation): the top face reads it with v toward -z */
    t3_attribute *uv = bg->attributes[T3_ATTR_UV];
    float *f = (float *)uv->array;
    for (int i = 0; i < uv->count * 2; i += 2) f[i + 1] = 1 - f[i + 1];
  }
  t3_geometry *hit = t3_box_geometry_new(1, 0.1f, 1, 1, 1, 1);
  t3_material *hm = t3_mesh_basic_material_new(0xffffff);
  t3_material *fm = physical(0x24160d, 0.5f, 0);
  fm->emissive_intensity = 0;
  for (int s = 0; s < g->players; s++) {
    char name[24];
    snprintf(name, sizeof name, "Board%d", s);
    ly_point o = V.lay.board[s];
    V.boards[s] = mesh_node(V.table_root, name, bg, bm, true, true);
    cw_node3d_set_position(V.boards[s], (t3_vec3){ o.x + LY_BOARD_W * 0.5f, BOARD_TOP * 0.5f, o.z + LY_BOARD_H * 0.5f });
    /* the turn glow: a low lit rim under the board's edge */
    t3_geometry *fg = t3_box_geometry_new(LY_BOARD_W + 0.45f, 0.05f, LY_BOARD_H + 0.45f, 1, 1, 1);
    t3_material *own = t3_material_clone(fm);
    cw_color sc = bk_seat_color(s);
    own->emissive = (t3_color){ sc.r, sc.g, sc.b };
    V.frames[s] = mesh_node(V.table_root, "Glow", fg, own, false, false);
    cw_node3d_set_position(V.frames[s], (t3_vec3){ o.x + LY_BOARD_W * 0.5f, 0.03f, o.z + LY_BOARD_H * 0.5f });
    t3_release(own);
    t3_release(fg);
    for (int r = 0; r <= MS_SIZE; r++) {
      V.rowhit[s][r] = mesh_node(V.table_root, "RowHit", hit, hm, false, false);
      ly_point c = r < MS_SIZE ? ly_line_centre(&V.lay, s, r) : ly_floor_centre(&V.lay, s);
      cw_node3d_set_position(V.rowhit[s][r], (t3_vec3){ c.x, BOARD_TOP + 0.2f, c.z });
      cw_node3d_set_scale(V.rowhit[s][r], (t3_vec3){ r < MS_SIZE ? r + 1.0f : 7.2f, 1, 0.98f });
      cw_node_add_group(V.rowhit[s][r], "rowhit");
      cw_node_set_user(V.rowhit[s][r], (void *)(intptr_t)(s * 8 + r + 1));
      cw_node_set(V.rowhit[s][r], "visible", cw_val_bool(false));
    }
  }
  t3_release(bg);
  t3_release(bm);
  t3_release(hit);
  t3_release(hm);
  t3_release(fm);
  /* trays: glazed terracotta coasters */
  t3_texture *tt = texture("textures/tray.png", true);
  t3_material *tm = physical(0xffffff, 0.45f, 0);
  t3_material_set_map(tm, tt);
  tm->clearcoat = 0.6f;
  tm->clearcoat_roughness = 0.2f;
  t3_geometry *trg = t3_cylinder_geometry_new(LY_TRAY_R, LY_TRAY_R + 0.06f, TRAY_TOP, 48, 1, false);
  for (int i = 0; i < V.trays_n; i++) {
    V.trays[i] = mesh_node(V.table_root, "Tray", trg, tm, true, true);
    cw_node3d_set_position(V.trays[i], (t3_vec3){ V.lay.tray[i].x, TRAY_TOP * 0.5f, V.lay.tray[i].z });
  }
  t3_release(trg);
  t3_release(tm);
  /* the centre pool: a low walnut frame lined with dark felt */
  t3_material *rim = physical(0x3b2416, 0.45f, 0);
  rim->clearcoat = 0.5f;
  rim->clearcoat_roughness = 0.3f;
  t3_geometry *pg = t3_box_geometry_new(V.lay.pool_w, POOL_TOP, V.lay.pool_h, 1, 1, 1);
  V.pool = mesh_node(V.table_root, "Pool", pg, rim, true, true);
  cw_node3d_set_position(V.pool, (t3_vec3){ V.lay.pool.x, POOL_TOP * 0.5f, V.lay.pool.z });
  t3_release(pg);
  t3_release(rim);
  t3_material *felt = physical(0x163330, 1.0f, 0);
  felt->sheen = 0.7f;
  felt->sheen_roughness = 0.7f;
  felt->sheen_color = (t3_color){ 0.3f, 0.5f, 0.45f };
  t3_geometry *fg2 = t3_box_geometry_new(V.lay.pool_w - 0.5f, 0.02f, V.lay.pool_h - 0.5f, 1, 1, 1);
  cw_node felt_n = mesh_node(V.pool, "Felt", fg2, felt, false, true);
  cw_node3d_set_position(felt_n, (t3_vec3){ 0, POOL_TOP * 0.5f, 0 });
  t3_release(fg2);
  t3_release(felt);
  /* the camera frames the whole table: everything inside the layout's box, a strip left for the prompts */
  float w = V.lay.max_x - V.lay.min_x, h = V.lay.max_z - V.lay.min_z;
  float fov = 30.0f * T3_DEG2RAD, tanv = tanf(fov * 0.5f), aspect = 16.0f / 9.0f;
  float dist = fmaxf(w * 0.5f / (tanv * aspect), h * 0.5f / tanv) * 1.08f + 1.0f;
  float tilt = 0.30f;
  t3_vec3 look = { 0, 0, 0.35f };
  cw_node3d_set_position(V.camera, (t3_vec3){ 0, dist * cosf(tilt), look.z + dist * sinf(tilt) });
  cw_node3d_look_at(V.camera, look, (t3_vec3){ 0, 1, 0 });
  /* tiles start in the bag */
  for (int i = 0; i < NTILES; i++) {
    tile_view *tv = &V.tile[i];
    tv->at = (slot){ SL_HIDDEN, 0, 0, 0 };
    tv->pos = tv->prev = tv->from = tv->to = slot_pos(tv->at, NULL);
    tv->moving = false;
    tv->visible = false;
    cw_node_set(tv->node, "visible", cw_val_bool(false));
  }
  V.marker_key = 0xffffffffu;
  V.marker_pos = V.marker_prev = (t3_vec3){ V.lay.marker_home.x, POOL_TOP, V.lay.marker_home.z };
  V.shown_hash = 0;
  V.ev_seen = g->ev_next;
  memset(V.pop, 0, sizeof V.pop);
  for (int i = 0; i < BK_MAX_SEATS; i++) V.shown_score[i] = 0, V.glow[i] = 0;
}

/* ── following the table ─────────────────────────────────────────────── */

typedef struct want { slot s; t3_vec3 p; } want;

/* every slot the table shows for colour c, wall slots first (so a tiled row's nearest tile goes up), hidden last */
static int wanted(const ms_state *st, int c, want *out) {
  int n = 0;
  for (int s = 0; s < st->players; s++)
    for (int r = 0; r < MS_SIZE; r++) {
      int col = ms_wall_col(r, c);
      if (ms_wall_has(&st->p[s], r, col)) out[n++] = (want){ { SL_WALL, (uint8_t)s, (uint8_t)r, (uint8_t)col }, { 0 } };
    }
  for (int s = 0; s < st->players; s++)
    for (int r = 0; r < MS_SIZE; r++)
      if (st->p[s].line_count[r] && st->p[s].line_colour[r] == c)
        for (int k = 0; k < st->p[s].line_count[r]; k++) out[n++] = (want){ { SL_LINE, (uint8_t)s, (uint8_t)r, (uint8_t)k }, { 0 } };
  for (int s = 0; s < st->players; s++) {
    int idx = st->p[s].floor_marker;
    for (int k = 0; k < c; k++) idx += st->p[s].floor_colour[k];
    for (int k = 0; k < st->p[s].floor_colour[c]; k++) out[n++] = (want){ { SL_FLOOR, (uint8_t)s, (uint8_t)(idx + k), 0 }, { 0 } };
  }
  for (int k = 0; k < st->centre[c]; k++) out[n++] = (want){ { SL_POOL, (uint8_t)c, (uint8_t)k, 0 }, { 0 } };
  for (int t = 0; t < st->trays; t++) {
    int slotn = 0;
    for (int k = 0; k < c; k++) slotn += st->tray[t][k];
    for (int k = 0; k < st->tray[t][c]; k++) out[n++] = (want){ { SL_TRAY, (uint8_t)t, (uint8_t)(slotn + k), 0 }, { 0 } };
  }
  int hidden = st->bag[c] + st->box[c];
  for (int k = 0; k < hidden; k++) out[n++] = (want){ { SL_HIDDEN, 0, 0, 0 }, { 0 } };
  if (n != MS_TILES_PER_COLOUR) CW_FATAL("smalti view: colour %d shows %d tiles, not %d", c, n, MS_TILES_PER_COLOUR);
  for (int i = 0; i < n; i++) out[i].p = slot_pos(out[i].s, st);
  return n;
}

static float dist3(t3_vec3 a, t3_vec3 b) {
  float dx = a.x - b.x, dz = a.z - b.z;
  return sqrtf(dx * dx + dz * dz);
}

static void follow(const ms_state *st, bool instant) {
  int moved = 0;
  for (int c = 0; c < MS_COLOURS; c++) {
    want w[MS_TILES_PER_COLOUR];
    wanted(st, c, w);
    tile_view *tl[MS_TILES_PER_COLOUR];
    bool taken[MS_TILES_PER_COLOUR] = { 0 }, done[MS_TILES_PER_COLOUR] = { 0 };
    for (int i = 0; i < MS_TILES_PER_COLOUR; i++) tl[i] = &V.tile[c * MS_TILES_PER_COLOUR + i];
    /* tiles already where a wanted slot is stay */
    for (int k = 0; k < MS_TILES_PER_COLOUR; k++)
      for (int i = 0; i < MS_TILES_PER_COLOUR; i++)
        if (!taken[i] && slot_key(tl[i]->at) == slot_key(w[k].s)) { taken[i] = done[k] = true; break; }
    /* the rest: each wanted slot takes the nearest free tile (hidden slots take what is left) */
    for (int k = 0; k < MS_TILES_PER_COLOUR; k++) {
      if (done[k]) continue;
      int best = -1;
      float bd = 1e9f;
      for (int i = 0; i < MS_TILES_PER_COLOUR; i++) {
        if (taken[i]) continue;
        float d = dist3(tl[i]->to, w[k].p);
        if (w[k].s.kind == SL_HIDDEN) d = tl[i]->at.kind == SL_HIDDEN ? 0 : 1000 - d; /* any non-hidden left goes */
        if (d < bd) bd = d, best = i;
      }
      if (best < 0) CW_FATAL("smalti view: no tile left for a slot");
      taken[best] = true;
      tile_view *tv = tl[best];
      slot was = tv->at;
      tv->at = w[k].s;
      if (instant) {
        tv->pos = tv->prev = tv->from = tv->to = w[k].p;
        tv->moving = false;
        tv->visible = w[k].s.kind != SL_HIDDEN;
        continue;
      }
      tv->from = tv->pos;
      if (was.kind == SL_HIDDEN) { tv->from = slot_pos(was, st); tv->pos = tv->prev = tv->from; tv->visible = true; }
      tv->to = w[k].p;
      float d = dist3(tv->from, tv->to);
      tv->dur = (int)clampf(18 + d * 1.1f, 18, 44);
      tv->t = 0;
      tv->delay = moved * 2 < 24 ? moved * 2 : 24;
      tv->yaw_from = tv->yaw;
      tv->yaw_to = 0;
      tv->moving = true;
      tv->hide_on_arrival = w[k].s.kind == SL_HIDDEN;
      moved++;
    }
  }
  if (moved && !instant) play(V.snd_slide, 0.5f, 1.0f);
}

static void place_marker(const ms_state *st) {
  uint32_t key = 0xfffffffeu;
  t3_vec3 to = { V.lay.marker_home.x, POOL_TOP, V.lay.marker_home.z };
  for (int s = 0; s < st->players; s++)
    if (st->p[s].floor_marker) {
      key = (uint32_t)s;
      ly_point p = ly_floor_cell(&V.lay, s, 0);
      to = (t3_vec3){ p.x, BOARD_TOP, p.z };
    }
  if (!st->centre_marker && key == 0xfffffffeu) { key = 0xfffffffdu; to = (t3_vec3){ V.lay.bag.x, 2, V.lay.bag.z }; }
  if (key == V.marker_key) return;
  V.marker_key = key;
  V.marker_from = V.marker_pos;
  V.marker_to = to;
  V.marker_t = 0;
  V.marker_dur = 30;
}

static void popup_add(t3_vec3 at, const char *text, uint32_t colour, int life) {
  for (int i = 0; i < POPUPS; i++)
    if (V.pop[i].age >= V.pop[i].life) {
      V.pop[i] = (popup){ at, "", colour, 0, life };
      snprintf(V.pop[i].text, sizeof V.pop[i].text, "%s", text);
      return;
    }
}

void view_tick(const mg_game *g) {
  V.ticks++;
  const ms_state *st = &g->shown;
  uint64_t h = ms_hash(st);
  if (h != V.shown_hash) {
    follow(st, V.shown_hash == 0 && false);
    place_marker(st);
    V.shown_hash = h;
  }
  /* events: popups and sounds */
  for (; V.ev_seen < g->ev_next; V.ev_seen++) {
    const mg_event *e = &g->ev[V.ev_seen % MG_EVENTS];
    char buf[24];
    if (e->kind == EV_TILE) {
      ly_point p = ly_wall_cell(&V.lay, e->seat, e->row, e->col);
      snprintf(buf, sizeof buf, "+%d", e->points);
      popup_add((t3_vec3){ p.x, BOARD_TOP + 0.6f, p.z }, buf, 0xffe9a0ff, 80);
      play(V.snd_chime, 0.45f, 1.0f + 0.06f * (float)(e->points > 6 ? 6 : e->points));
    } else if (e->kind == EV_FLOOR) {
      ly_point p = ly_floor_centre(&V.lay, e->seat);
      snprintf(buf, sizeof buf, "%d", e->points);
      if (e->points < 0) popup_add((t3_vec3){ p.x, BOARD_TOP + 0.6f, p.z }, buf, 0xff8a70ff, 80);
      play(V.snd_thud, 0.6f, 1.0f);
      if (!mg_seat_is_cpu(g, e->seat) && e->points < 0) cw_input_rumble(e->seat, 0.35f, 0.1f, 160);
    } else if (e->kind == EV_TURN) {
      play(V.snd_turn, 0.35f, 1.0f);
    } else if (e->kind == EV_OVER) {
      play(V.snd_chime, 0.6f, 0.75f);
    }
  }
  for (int i = 0; i < POPUPS; i++)
    if (V.pop[i].age < V.pop[i].life) V.pop[i].age++;
  /* tiles glide */
  bk_move preview = mg_preview(g);
  bool picking = !mg_busy(g) && !g->over && g->mode == UI_PICK && !mg_seat_is_cpu(g, g->turn);
  int cur = mg_cursor(g);
  int lift_src = -1, lift_colour = -1;
  if (!mg_busy(g) && !g->over && cur >= 0 && cur < g->ntargets) {
    lift_src = g->target[cur].src, lift_colour = g->target[cur].colour;
    (void)picking;
  }
  if (g->mode == UI_PLACE) lift_src = g->pick_src, lift_colour = g->pick_colour;
  for (int i = 0; i < NTILES; i++) {
    tile_view *tv = &V.tile[i];
    tv->prev = tv->pos;
    tv->prev_yaw = tv->yaw;
    tv->prev_lift = tv->lift;
    bool lifted = false;
    if (lift_src >= 0 && tv->colour == lift_colour && !tv->moving)
      lifted = (lift_src == MS_CENTRE && tv->at.kind == SL_POOL) || (tv->at.kind == SL_TRAY && tv->at.a == lift_src);
    tv->hi = lifted;
    float lt = lifted ? (g->mode == UI_PLACE ? 0.7f : 0.45f) : 0;
    tv->lift += (lt - tv->lift) * 0.22f;
    if (!tv->moving) {
      if (tv->settle > 0) tv->settle--;
      continue;
    }
    if (tv->delay > 0) { tv->delay--; continue; }
    tv->t++;
    float u = (float)tv->t / (float)tv->dur;
    float e = ease(u);
    float d = dist3(tv->from, tv->to);
    float arc = sinf(3.14159f * clampf(u, 0, 1)) * (0.6f + d * 0.09f);
    tv->pos = (t3_vec3){ tv->from.x + (tv->to.x - tv->from.x) * e, tv->from.y + (tv->to.y - tv->from.y) * e + arc,
                         tv->from.z + (tv->to.z - tv->from.z) * e };
    tv->yaw = tv->yaw_from + sinf(3.14159f * u) * 0.35f * ((i & 1) ? 1 : -1);
    if (tv->t >= tv->dur) {
      tv->pos = tv->to;
      tv->yaw = 0;
      tv->moving = false;
      tv->settle = 14;
      if (tv->hide_on_arrival) tv->visible = false;
      else if ((i & 3) == 0) play(V.snd_clack, 0.35f, 0.9f + 0.05f * (float)(i % 5));
    }
  }
  (void)preview;
  /* the marker */
  V.marker_prev = V.marker_pos;
  if (V.marker_t < V.marker_dur) {
    V.marker_t++;
    float u = (float)V.marker_t / V.marker_dur, e = ease(u);
    V.marker_pos = (t3_vec3){ V.marker_from.x + (V.marker_to.x - V.marker_from.x) * e,
                              V.marker_from.y + (V.marker_to.y - V.marker_from.y) * e + sinf(3.14159f * u) * 1.2f,
                              V.marker_from.z + (V.marker_to.z - V.marker_from.z) * e };
  }
  /* the turn glow swells in and out (never flashes) */
  for (int s = 0; s < V.players; s++) {
    float want_g = (!g->over && s == g->turn && !g->staging) ? 1.0f : 0.0f;
    V.glow[s] += (want_g - V.glow[s]) * 0.06f;
  }
  for (int s = 0; s < V.players; s++) V.shown_score[s] += ((float)st->p[s].score - V.shown_score[s]) * 0.12f;
}

/* ── the frame ───────────────────────────────────────────────────────── */

static void set_tile(cw_node n, t3_vec3 p, float yaw, float squash) {
  cw_node3d_set_position(n, p);
  cw_node3d_set_rotation(n, (t3_vec3){ 0, yaw, 0 });
  cw_node3d_set_scale(n, (t3_vec3){ 1 + squash * 0.5f, 1 - squash, 1 + squash * 0.5f });
}

void view_frame(const mg_game *g, float alpha) {
  for (int i = 0; i < NTILES; i++) {
    tile_view *tv = &V.tile[i];
    cw_node_set(tv->node, "visible", cw_val_bool(tv->visible));
    if (!tv->visible) continue;
    t3_vec3 p = { tv->prev.x + (tv->pos.x - tv->prev.x) * alpha, tv->prev.y + (tv->pos.y - tv->prev.y) * alpha,
                  tv->prev.z + (tv->pos.z - tv->prev.z) * alpha };
    float lift = tv->prev_lift + (tv->lift - tv->prev_lift) * alpha;
    p.y += lift;
    float squash = 0;
    if (tv->settle > 0) { /* the landing: a short damped squash, glass has weight */
      float k = (14 - tv->settle + alpha) / 14.0f;
      squash = sinf(k * 3.14159f * 2.0f) * 0.07f * (1 - k);
    }
    set_tile(tv->node, p, tv->prev_yaw + (tv->yaw - tv->prev_yaw) * alpha, squash);
    t3_object *to = cw_node3d_object(tv->node);
    if (to && to->child_count) mesh_material((t3_mesh *)to->children[0], tv->hi ? V.glass_hi[tv->colour] : V.glass[tv->colour]);
  }
  t3_vec3 m = { V.marker_prev.x + (V.marker_pos.x - V.marker_prev.x) * alpha, V.marker_prev.y + (V.marker_pos.y - V.marker_prev.y) * alpha,
                V.marker_prev.z + (V.marker_pos.z - V.marker_prev.z) * alpha };
  cw_node3d_set_position(V.marker, m);
  for (int s = 0; s < V.players; s++) {
    t3_object *o = cw_node3d_object(V.frames[s]);
    if (o && o->child_count > 0) {
      t3_mesh *mesh = (t3_mesh *)o->children[0];
      if (mesh->material_count) {
        t3_material *fm = mesh->materials[0];
        float want = 1.1f * V.glow[s];
        if (fabsf(fm->emissive_intensity - want) > 0.002f) { fm->emissive_intensity = want; fm->version++; }
      }
    }
  }
  /* the ghost: where the held tiles would land, and what would break */
  bk_move pv = mg_preview(g);
  int ng = 0;
  if (pv != BK_MOVE_NONE && !mg_busy(g)) {
    const ms_state *st = &g->shown;
    int seat = g->turn, c = ms_move_colour(pv), line = ms_move_line(pv);
    int n = ms_source_count(st, ms_move_source(pv), c);
    const ms_player *pl = &st->p[seat];
    int put = 0;
    if (line != MS_FLOOR) {
      int room = line + 1 - pl->line_count[line];
      put = n < room ? n : room;
      for (int k = 0; k < put && ng < 12; k++) {
        ly_point p = ly_line_cell(&V.lay, seat, line, pl->line_count[line] + k);
        cw_node gn = V.ghost_nodes[ng++];
        t3_object *o = cw_node3d_object(gn);
        if (o && o->child_count) mesh_material((t3_mesh *)o->children[0], V.ghost[c]);
        set_tile(gn, (t3_vec3){ p.x, BOARD_TOP + 0.02f + 0.04f * sinf(V.ticks * 0.08f), p.z }, 0, 0);
      }
    }
    int slots_used = pl->floor_tiles + pl->floor_marker + (ms_move_source(pv) == MS_CENTRE && st->centre_marker ? 1 : 0);
    for (int k = 0; k < n - put && ng < 12; k++) {
      int at = slots_used + k;
      if (at >= MS_FLOOR_SLOTS) break;
      ly_point p = ly_floor_cell(&V.lay, seat, at);
      cw_node gn = V.ghost_nodes[ng++];
      t3_object *o = cw_node3d_object(gn);
      if (o && o->child_count) mesh_material((t3_mesh *)o->children[0], V.ghost[c]);
      set_tile(gn, (t3_vec3){ p.x, BOARD_TOP + 0.02f, p.z }, 0.25f, 0);
    }
  }
  for (int i = 0; i < 12; i++) cw_node_set(V.ghost_nodes[i], "visible", cw_val_bool(i < ng));
}

/* ── the HUD ─────────────────────────────────────────────────────────── */

static cw_rect view_rect(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  return (cw_rect){ 0, 0, sz.x, sz.y };
}

static cw_vec2 project(t3_vec3 p) { return cw_camera3d_world_to_screen(V.camera, view_rect(), p, NULL); }

/* cw_r2d_text aligns only inside wrap_width: a line without one is placed by its measured width here */
static void put_text(const char *s, cw_vec2 at, cw_text_style st) {
  if (st.wrap_width <= 0 && st.align != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), s, &st);
    at.x -= st.align == CW_ALIGN_RIGHT ? m.width : m.width * 0.5f;
    st.align = CW_ALIGN_LEFT;
  }
  cw_r2d_text(cw_ui_font_sans(), s, at, &st);
}

static void text(const char *s, cw_vec2 at, float size, cw_color c, cw_text_align align) {
  put_text(s, at,
              (cw_text_style){ .size = size, .color = c, .align = align, .outline_width = size * 0.08f,
                                .outline_color = cw_rgba(0.05f, 0.03f, 0.02f, 0.85f) });
}

static void card(cw_rect r) {
  cw_r2d_round_rect((cw_rect){ r.x + 6, r.y + 8, r.w, r.h }, 18, 8, cw_rgba(0, 0, 0, 0.35f));
  cw_r2d_round_rect(r, 18, 8, cw_rgba(0.96f, 0.93f, 0.86f, 1)); /* opaque: nothing behind shows through the text */
  cw_r2d_round_rect_lines(r, 18, 3, 8, cw_rgba(0.79f, 0.6f, 0.23f, 1));
}

static void draw_help(cw_vec2 sz, bool in_play) {
  float u = sz.y / 720.0f;
  cw_rect r = { sz.x * 0.5f - 400 * u, sz.y * 0.5f - 270 * u, 800 * u, 540 * u };
  card(r);
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.08f, 1), gold = cw_rgba(0.62f, 0.42f, 0.1f, 1);
  put_text("How to play", (cw_vec2){ r.x + r.w * 0.5f, r.y + 28 * u }, (cw_text_style){ .size = 40 * u, .color = gold, .align = CW_ALIGN_CENTER });
  static const char *const LINES[] = {
    "On your turn take every tile of one colour from a tray. The rest of that tray slides to the centre. "
    "Or take one colour from the centre: the first to do so also takes the gilded roundel, which costs a point.",
    "Put the tiles on one staging row. A row holds one colour, and never a colour its mosaic row already has. "
    "What does not fit drops to the breakage strip and costs points.",
    "When the trays and centre are empty, each full row sets one tile into the mosaic. It scores a point, "
    "plus one for each tile it touches in a straight line, across and down.",
    "The game ends after a round in which someone finishes a mosaic row. Bonuses: 2 a row, 7 a column, 10 a full colour.",
  };
  float y = r.y + 92 * u;
  for (int i = 0; i < 4; i++) {
    put_text(LINES[i], (cw_vec2){ r.x + 44 * u, y },
                (cw_text_style){ .size = 21 * u, .color = ink, .wrap_width = r.w - 88 * u, .line_spacing = 1.15f });
    y += 96 * u;
  }
  if (in_play) /* on the title the shell draws the way back */
    cw_prompt_text(cw_ui_font_sans(), "{choose} play    {how_to_play} this card any time", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 46 * u },
                 22 * u, ink, CW_ALIGN_CENTER);
}

static void draw_results(const mg_game *g, cw_vec2 sz) {
  float u = sz.y / 720.0f;
  const ms_state *st = mg_state(g);
  cw_rect r = { sz.x * 0.5f - 330 * u, sz.y * 0.5f - 200 * u, 660 * u, 400 * u };
  card(r);
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.08f, 1), gold = cw_rgba(0.62f, 0.42f, 0.1f, 1);
  unsigned w = ms_winners(st);
  char line[96];
  int nw = 0, last = 0;
  for (int i = 0; i < st->players; i++)
    if (w >> i & 1) nw++, last = i;
  if (nw == 1) snprintf(line, sizeof line, "%s wins", mg_seat_name(last));
  else snprintf(line, sizeof line, "A shared win");
  put_text(line, (cw_vec2){ r.x + r.w * 0.5f, r.y + 26 * u }, (cw_text_style){ .size = 42 * u, .color = gold, .align = CW_ALIGN_CENTER });
  /* a row per seat, best first */
  int order[BK_MAX_SEATS];
  for (int i = 0; i < st->players; i++) order[i] = i;
  for (int i = 0; i < st->players; i++)
    for (int j = i + 1; j < st->players; j++)
      if (st->p[order[j]].score > st->p[order[i]].score) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  float y = r.y + 100 * u;
  static const char *const HEAD[4] = { "rows", "columns", "colours", "score" };
  const float colx[4] = { r.x + r.w - 330 * u, r.x + r.w - 240 * u, r.x + r.w - 150 * u, r.x + r.w - 60 * u };
  for (int k = 0; k < 4; k++)
    put_text(HEAD[k], (cw_vec2){ colx[k], y - 30 * u }, (cw_text_style){ .size = 17 * u, .color = cw_rgba(0.4f, 0.32f, 0.25f, 1), .align = CW_ALIGN_CENTER });
  for (int k = 0; k < st->players; k++) {
    int s = order[k], rows, cols, sets;
    ms_end_bonus(st->p[s].wall, &rows, &cols, &sets);
    cw_r2d_circle((cw_vec2){ r.x + 60 * u, y + 16 * u }, 11 * u, 20, bk_seat_color(s));
    snprintf(line, sizeof line, "%s%s", mg_seat_name(s), mg_seat_is_cpu(g, s) ? " (computer)" : "");
    put_text(line, (cw_vec2){ r.x + 84 * u, y }, (cw_text_style){ .size = 26 * u, .color = ink });
    const int cell[3] = { rows, cols, sets };
    for (int k = 0; k < 3; k++) {
      snprintf(line, sizeof line, "%d", cell[k]);
      put_text(line, (cw_vec2){ colx[k], y + 2 * u }, (cw_text_style){ .size = 22 * u, .color = ink, .align = CW_ALIGN_CENTER });
    }
    snprintf(line, sizeof line, "%d", st->p[s].score);
    put_text(line, (cw_vec2){ colx[3], y - 2 * u }, (cw_text_style){ .size = 30 * u, .color = gold, .align = CW_ALIGN_CENTER });
    y += 52 * u;
  }
  cw_prompt_text(cw_ui_font_sans(), "{choose} play again    {back} title", -1, (cw_vec2){ r.x + r.w * 0.5f, r.y + r.h - 44 * u }, 22 * u,
                 ink, CW_ALIGN_CENTER);
}

void view_hud(const mg_game *g) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 720.0f;
  const ms_state *st = &g->shown;
  /* under each breakage cell, what a tile there costs, as a number */
  for (int s = 0; s < g->players; s++) {
    static const int PEN[7] = { 1, 1, 2, 2, 2, 3, 3 };
    for (int i = 0; i < 7; i++) {
      ly_point c = ly_floor_cell(&V.lay, s, i);
      cw_vec2 at = project((t3_vec3){ c.x, BOARD_TOP, c.z + 0.66f });
      char pen[8];
      snprintf(pen, sizeof pen, "-%d", PEN[i]);
      /* dark ink straight on the light board (no outline: an outline round dark ink blots it), above the text floor */
      put_text(pen, (cw_vec2){ at.x, at.y - 12 * u }, (cw_text_style){ .size = 17 * u, .color = cw_rgba(0.45f, 0.08f, 0.05f, 1), .align = CW_ALIGN_CENTER });
    }
  }
  /* plates: name, who plays it, score */
  for (int s = 0; s < g->players; s++) {
    /* the plate fills the empty corner above the staging stairs: rows 0 and 1, left of their cells (2.9 x 1.9 cells) */
    ly_point o = V.lay.board[s];
    cw_vec2 a = project((t3_vec3){ o.x + LY_MARGIN + 0.05f, BOARD_TOP, o.z + LY_MARGIN + 0.06f });
    cw_vec2 b = project((t3_vec3){ o.x + LY_MARGIN + 2.92f, BOARD_TOP, o.z + LY_MARGIN + 1.92f });
    /* never below the text floor (render2d.h: 1.67% of the canvas height): the smallest line is 0.25 of the plate */
    float h = fmaxf(b.y - a.y, sz.y * 0.072f);
    cw_rect plate = { a.x, a.y, fmaxf(b.x - a.x, h * 1.62f), h };
    char line[48];
    cw_color sc = bk_seat_color(s);
    float glow = V.glow[s];
    cw_r2d_round_rect(plate, h * 0.14f, 6, cw_rgba(0.12f, 0.08f, 0.05f, 0.66f + 0.24f * glow));
    if (glow > 0.02f) cw_r2d_round_rect_lines(plate, h * 0.14f, h * 0.05f, 6, cw_rgba(sc.r, sc.g, sc.b, glow));
    cw_r2d_circle((cw_vec2){ plate.x + h * 0.17f, plate.y + h * 0.22f }, h * 0.09f, 20, sc);
    snprintf(line, sizeof line, "%s", mg_seat_name(s));
    text(line, (cw_vec2){ plate.x + h * 0.32f, plate.y + h * 0.04f }, h * 0.3f, cw_rgba(1, 0.97f, 0.9f, 1), CW_ALIGN_LEFT);
    if (mg_seat_is_cpu(g, s)) snprintf(line, sizeof line, "%s", cw_shell_cpu_level_label((cw_shell_cpu_level)g->cpu_level[s]));
    else snprintf(line, sizeof line, "player");
    text(line, (cw_vec2){ plate.x + h * 0.08f, plate.y + h * 0.64f }, h * 0.26f, cw_rgba(0.9f, 0.85f, 0.76f, 0.95f), CW_ALIGN_LEFT);
    snprintf(line, sizeof line, "%d", (int)floorf(V.shown_score[s] + 0.5f));
    text(line, (cw_vec2){ plate.x + plate.w - h * 0.08f, plate.y + h * 0.38f }, h * 0.48f, cw_rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_RIGHT);
  }
  (void)st;
  /* the cursor: a frame round what it is on (the group in a tray or the pool, or the staging row), in the seat's colour,
   * breathing slowly */
  int ct = mg_cursor(g);
  if (!mg_busy(g) && !g->over && !g->help && ct >= 0 && ct < g->ntargets) {
    const mg_target *tg = &g->target[ct];
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int n = 0;
    ly_point cells[40];
    if (g->mode == UI_PICK || tg->line == -2) {
      for (int i = 0; i < NTILES; i++) {
        const tile_view *tv = &V.tile[i];
        if (tv->colour != tg->colour || tv->moving) continue;
        bool in = (tg->src == MS_CENTRE && tv->at.kind == SL_POOL) || (tv->at.kind == SL_TRAY && tv->at.a == tg->src);
        if (in && n < 40) cells[n++] = (ly_point){ tv->to.x, tv->to.z };
      }
    } else if (tg->line == MS_FLOOR) {
      for (int i = 0; i < MS_FLOOR_SLOTS; i++) cells[n++] = ly_floor_cell(&V.lay, g->turn, i);
    } else {
      for (int k = 0; k <= tg->line; k++) cells[n++] = ly_line_cell(&V.lay, g->turn, tg->line, k);
    }
    for (int i = 0; i < n; i++)
      for (int cx = -1; cx <= 1; cx += 2)
        for (int cz = -1; cz <= 1; cz += 2) {
          cw_vec2 q = project((t3_vec3){ cells[i].x + cx * 0.5f, BOARD_TOP + 0.4f, cells[i].z + cz * 0.5f });
          x0 = fminf(x0, q.x), y0 = fminf(y0, q.y), x1 = fmaxf(x1, q.x), y1 = fmaxf(y1, q.y);
        }
    if (n) {
      cw_color sc = bk_seat_color(g->turn);
      float a = 0.75f + 0.25f * sinf(V.ticks * 0.07f);
      float pad = 5 * u;
      cw_rect fr = { x0 - pad, y0 - pad, x1 - x0 + 2 * pad, y1 - y0 + 2 * pad };
      cw_r2d_round_rect_lines(fr, 8 * u, 5 * u, 8, cw_rgba(0, 0, 0, 0.35f * a));
      cw_r2d_round_rect_lines(fr, 8 * u, 3 * u, 8, cw_rgba(sc.r * 0.6f + 0.4f, sc.g * 0.6f + 0.4f, sc.b * 0.6f + 0.4f, a));
    }
  }
  /* popups rise and fade */
  for (int i = 0; i < POPUPS; i++) {
    popup *pp = &V.pop[i];
    if (pp->age >= pp->life) continue;
    float k = (float)pp->age / pp->life;
    cw_vec2 at = project((t3_vec3){ pp->at.x, pp->at.y + k * 1.2f, pp->at.z });
    cw_color c = cw_hex(pp->colour);
    c.a = k < 0.15f ? k / 0.15f : k > 0.7f ? (1 - k) / 0.3f : 1;
    text(pp->text, (cw_vec2){ at.x, at.y - 18 * u }, 34 * u, c, CW_ALIGN_CENTER);
  }
  /* the prompt line */
  if (!g->over && !g->help) {
    char line[160];
    int s = g->turn;
    cw_color ink = cw_rgba(1, 0.96f, 0.88f, 1);
    if (mg_busy(g)) {
      line[0] = 0;
    } else if (mg_seat_is_cpu(g, s)) {
      snprintf(line, sizeof line, "%s is choosing", mg_seat_name(s));
      text(line, (cw_vec2){ sz.x * 0.5f, sz.y - 46 * u }, 24 * u, ink, CW_ALIGN_CENTER);
    } else if (g->mode == UI_PICK) {
      snprintf(line, sizeof line, "%s: {left|right} {up|down} pick a colour    {choose} take    {how_to_play} how to play", mg_seat_name(s));
      cw_prompt_text(cw_ui_font_sans(), line, s, (cw_vec2){ sz.x * 0.5f, sz.y - 46 * u }, 22 * u, ink, CW_ALIGN_CENTER);
    } else {
      bk_move pv = mg_preview(g);
      int n = ms_source_count(st, g->pick_src, g->pick_colour);
      const char *where = pv == BK_MOVE_NONE ? NULL : "the breakage strip";
      char rowname[24];
      if (pv != BK_MOVE_NONE && ms_move_line(pv) != MS_FLOOR) {
        snprintf(rowname, sizeof rowname, "row %d", ms_move_line(pv) + 1);
        where = rowname;
      }
      if (where) snprintf(line, sizeof line, "%d tile%s to %s    {up|down} row    {choose} place    {back} put back", n, n == 1 ? "" : "s", where);
      else snprintf(line, sizeof line, "{choose} put the tiles back    {up|down} choose a row");
      cw_prompt_text(cw_ui_font_sans(), line, s, (cw_vec2){ sz.x * 0.5f, sz.y - 46 * u }, 22 * u, ink, CW_ALIGN_CENTER);
    }
  }
  /* the round, top centre */
  if (!g->over) {
    char line[32];
    snprintf(line, sizeof line, "Round %d", st->round > 0 ? st->round : 1);
    text(line, (cw_vec2){ sz.x * 0.5f, 8 * u }, 22 * u, cw_rgba(0.95f, 0.88f, 0.7f, 0.9f), CW_ALIGN_CENTER);
  }
  if (g->help) draw_help(sz, true);
  if (g->over && !g->staging) draw_results(g, sz);
}

void view_help_page(cw_vec2 canvas) { draw_help(canvas, false); }

void view_backdrop(cw_vec2 canvas) {
  (void)canvas; /* the table itself shows behind the menus */
}


/* The title screens' table: the glass laid out as a finished panel of mosaic, ten by ten, in diagonal bands (every
 * colour twenty times), under the camera of a four-seat table. */
void view_attract(void) {
  if (!V.attract) {
    clear_layout_nodes();
    V.players = 0;
    ly_table_make(&V.lay, 4, 9);
    for (int i = 0; i < NTILES; i++) {
      int x = i % 10, y = i / 10, c = (x + 2 * y) % MS_COLOURS;
      /* the tile of colour c with the lowest free index */
      tile_view *tv = NULL;
      for (int k = 0; k < MS_TILES_PER_COLOUR && !tv; k++) {
        tile_view *t = &V.tile[c * MS_TILES_PER_COLOUR + k];
        if (!t->hi) tv = t;
      }
      tv->hi = true; /* marks it placed for this loop; cleared below */
      t3_vec3 p = { (x - 4.5f) * 1.02f, 0.06f, (y - 4.5f) * 1.02f - 0.5f };
      tv->pos = tv->prev = tv->from = tv->to = p;
      tv->moving = false;
      tv->visible = true;
      tv->lift = tv->prev_lift = 0;
      tv->settle = 0;
      tv->yaw = tv->prev_yaw = 0;
      tv->at = (slot){ SL_HIDDEN, 0, 0, 0 };
    }
    for (int i = 0; i < NTILES; i++) V.tile[i].hi = false;
    V.marker_pos = V.marker_prev = (t3_vec3){ 6.4f, 0.0f, -5.0f };
    float w = 34.0f, h = 17.0f, fov = 30.0f * T3_DEG2RAD, tanv = tanf(fov * 0.5f);
    float dist = fmaxf(w * 0.5f / (tanv * 16.0f / 9.0f), h * 0.5f / tanv) * 1.08f + 1.0f, tilt = 0.30f;
    cw_node3d_set_position(V.camera, (t3_vec3){ 0, dist * cosf(tilt), 0.35f + dist * sinf(tilt) });
    cw_node3d_look_at(V.camera, (t3_vec3){ 0, 0, 0.35f }, (t3_vec3){ 0, 1, 0 });
    V.attract = true;
  }
  for (int i = 0; i < NTILES; i++) {
    tile_view *tv = &V.tile[i];
    cw_node_set(tv->node, "visible", cw_val_bool(true));
    set_tile(tv->node, tv->pos, 0, 0);
    t3_object *to = cw_node3d_object(tv->node);
    if (to && to->child_count) mesh_material((t3_mesh *)to->children[0], V.glass[tv->colour]);
  }
  cw_node3d_set_position(V.marker, V.marker_pos);
  for (int i = 0; i < 12; i++) cw_node_set(V.ghost_nodes[i], "visible", cw_val_bool(false));
}
