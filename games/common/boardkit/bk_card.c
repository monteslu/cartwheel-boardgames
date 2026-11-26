/* boardkit: cards (bk_card.h). Faces: one 2D render target each, painted by the game's painter from the update of a
 * BkCardPainter node (pause mode ALWAYS: it runs on the title too), worn by a three.c material through
 * cw_target_texture3d. Slabs: one shared procedural geometry per card shape (front,
 * back and edge as three material groups) on a MeshInstance3D. */
#include "bk_card.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk_game.h"
#include "cartwheel/render3d.h"

static struct {
  bool ready;
  cw_tree *tree;
  bk_card_paint_fn paint;
  void *user;
  struct { int face; cw_texture target; t3_material *mat, *ghost; bool dirty; } faces[BK_CARD_FACES_MAX];
  cw_node painter;
  int nfaces;
  struct { cw_node node; t3_mesh *mesh; bk_card_desc d; } cards[1024];
  int ncards;
  struct { float w, h, t, r; t3_geometry *g; } shapes[8];
  int nshapes;
} K;

static void paint_pending(cw_node n, void *data, double dt) {
  (void)n, (void)data, (void)dt;
  for (int i = 0; i < K.nfaces; i++) {
    if (!K.faces[i].dirty) continue;
    K.faces[i].dirty = false;
    cw_r2d_begin(&(cw_r2d_pass_desc){ .target = K.faces[i].target, .clear = true, .clear_color = CW_WHITE });
    if (K.paint) K.paint(K.faces[i].face, cw_r2d_canvas_size(), K.user);
    cw_r2d_end(); /* the 3D copy follows (cw_target_texture3d) */
  }
}
static cw_node_type painter_type = { .name = "BkCardPainter", .parent = "Node", .update = paint_pending,
                                     .doc = "boardkit: paints the card faces waiting for their picture (bk_card.h)" };

void bk_cards_init(cw_tree *tree, bk_card_paint_fn paint, void *user) {
  if (!K.ready) {
    cw_node_type_register(&painter_type);
    K.ready = true;
  }
  if (K.tree != tree || !cw_node_valid(K.painter)) {
    K.painter = cw_node_new("BkCardPainter", "BkCardPainter");
    cw_node_set_pause_mode(K.painter, CW_PAUSE_ALWAYS);
    cw_node_add_child(cw_tree_root(tree), K.painter);
  }
  K.tree = tree;
  K.paint = paint;
  K.user = user;
}

static int face_index(int face) {
  for (int i = 0; i < K.nfaces; i++)
    if (K.faces[i].face == face) return i;
  return -1;
}

cw_texture bk_card_face(int face, int px_w, int px_h) {
  if (!K.ready || !K.tree) CW_FATAL("bk_card_face: call bk_cards_init first");
  int i = face_index(face);
  if (i >= 0) return K.faces[i].target;
  if (K.nfaces == BK_CARD_FACES_MAX) CW_FATAL("bk_card_face: more than %d faces", BK_CARD_FACES_MAX);
  cw_texture t = cw_target_create(&(cw_target_desc){ .width = px_w, .height = px_h, .filter = CW_FILTER_LINEAR });
  if (!t.id) CW_FATAL("bk_card_face: no render target for face %d (%dx%d px; create cards after the GPU exists)", face, px_w, px_h);
  K.faces[K.nfaces].face = face;
  K.faces[K.nfaces].target = t;
  K.faces[K.nfaces].mat = NULL;
  K.faces[K.nfaces].dirty = true;
  return K.faces[K.nfaces++].target;
}

void bk_card_face_refresh(int face) {
  int i = face_index(face);
  if (i >= 0) K.faces[i].dirty = true;
}
int bk_card_faces_made(void) { return K.nfaces; }

/* the material showing a face (shared by every card showing it) */
static t3_material *face_material(int face, const bk_card_desc *d) {
  int px_h = d->face_px > 0 ? d->face_px : 256;
  int px_w = (int)lroundf((float)px_h * d->width / d->height);
  cw_texture target = bk_card_face(face, px_w, px_h);
  int i = face_index(face);
  if (!K.faces[i].mat) {
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    m->roughness = 0.62f;
    t3_texture *tx = cw_target_texture3d(target);
    if (!tx) CW_FATAL("bk_card: face %d: the engine gave its render target no 3D texture", face);
    t3_material_set_texture(m, T3_MAP, tx);
    K.faces[i].mat = m;
  }
  return K.faces[i].mat;
}

/* one rounded slab: front (group 0, +Y), back (group 1, -Y), edge (group 2) */
static t3_geometry *shape(float w, float h, float t, float r) {
  for (int i = 0; i < K.nshapes; i++)
    if (K.shapes[i].w == w && K.shapes[i].h == h && K.shapes[i].t == t && K.shapes[i].r == r) return K.shapes[i].g;
  enum { SEG = 6, RIM = 4 * (SEG + 1) };
  float px[RIM], pz[RIM];
  static const float CX[4] = { 1, -1, -1, 1 }, CZ[4] = { -1, -1, 1, 1 };
  int k = 0;
  for (int c = 0; c < 4; c++) /* corners counterclockwise seen from +Y: top right, top left, bottom left, bottom right */
    for (int s = 0; s <= SEG; s++) {
      float a = (float)M_PI * 0.5f * ((float)c + (float)s / SEG) - (float)M_PI * 0.5f;
      float ccx = CX[c] * (w * 0.5f - r), ccz = CZ[c] * (h * 0.5f - r);
      px[k] = ccx + r * cosf(a + (float)M_PI * 0.5f);
      pz[k] = ccz - r * sinf(a + (float)M_PI * 0.5f);
      k++;
    }
  int nv = 2 * (RIM + 1) + 4 * RIM;
  float *pos = calloc((size_t)nv * 3, sizeof(float)), *nor = calloc((size_t)nv * 3, sizeof(float)), *uv = calloc((size_t)nv * 2, sizeof(float));
  int ni = 2 * RIM * 3 + RIM * 6;
  uint32_t *idx = calloc((size_t)ni, sizeof(uint32_t));
  int v = 0, ii = 0;
  for (int side = 0; side < 2; side++) { /* fans: centre then rim */
    float y = side ? -t * 0.5f : t * 0.5f, ny = side ? -1.0f : 1.0f;
    int base = v;
    for (int j = -1; j < RIM; j++) {
      float x = j < 0 ? 0 : px[j], z = j < 0 ? 0 : pz[j];
      pos[v * 3] = x, pos[v * 3 + 1] = y, pos[v * 3 + 2] = z;
      nor[v * 3 + 1] = ny;
      /* the front reads with its top toward -Z; the back mirrored so it reads right after a turn about Z */
      uv[v * 2] = side ? 0.5f - x / w : x / w + 0.5f;
      uv[v * 2 + 1] = 0.5f - z / h;
      v++;
    }
    for (int j = 0; j < RIM; j++) {
      int a = base + 1 + j, b = base + 1 + (j + 1) % RIM;
      if (side) idx[ii++] = (uint32_t)base, idx[ii++] = (uint32_t)b, idx[ii++] = (uint32_t)a;
      else idx[ii++] = (uint32_t)base, idx[ii++] = (uint32_t)a, idx[ii++] = (uint32_t)b;
    }
  }
  int edge_start = ii;
  for (int j = 0; j < RIM; j++) { /* the edge: a quad per rim segment, flat normals outward */
    int j2 = (j + 1) % RIM;
    float ex = pz[j2] - pz[j], ez = -(px[j2] - px[j]);
    float l = sqrtf(ex * ex + ez * ez);
    float nx = -ex / l, nz = -ez / l;
    float q[4][3] = { { px[j], t * 0.5f, pz[j] }, { px[j2], t * 0.5f, pz[j2] }, { px[j2], -t * 0.5f, pz[j2] }, { px[j], -t * 0.5f, pz[j] } };
    int base = v;
    for (int c = 0; c < 4; c++) {
      memcpy(&pos[v * 3], q[c], sizeof q[c]);
      nor[v * 3] = nx, nor[v * 3 + 2] = nz;
      v++;
    }
    uint32_t b = (uint32_t)base;
    idx[ii++] = b, idx[ii++] = b + 2, idx[ii++] = b + 1, idx[ii++] = b, idx[ii++] = b + 3, idx[ii++] = b + 2;
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, v, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nor, v, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, v, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, ii, 1));
  t3_geometry_add_group(g, 0, (RIM) * 3, 0);
  t3_geometry_add_group(g, RIM * 3, RIM * 3, 1);
  t3_geometry_add_group(g, edge_start, ii - edge_start, 2);
  free(pos), free(nor), free(uv), free(idx);
  if (K.nshapes < 8) K.shapes[K.nshapes++] = (typeof(K.shapes[0])){ w, h, t, r, g };
  return g;
}

static bk_card_desc filled(const bk_card_desc *in) {
  bk_card_desc d = *in;
  if (d.width <= 0) d.width = 0.63f;
  if (d.height <= 0) d.height = 0.88f;
  if (d.thickness <= 0) d.thickness = 0.012f;
  if (d.corner <= 0) d.corner = 0.06f;
  if (d.edge.a <= 0) d.edge = (cw_color){ 0.93f, 0.9f, 0.84f, 1 };
  return d;
}

static t3_material *edge_material(cw_color c) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = (t3_color){ c.r, c.g, c.b };
  m->roughness = 0.8f;
  return m;
}

cw_node bk_card_new(cw_node parent, const char *name, const bk_card_desc *in) {
  bk_card_desc d = filled(in);
  t3_geometry *g = shape(d.width, d.height, d.thickness, d.corner);
  t3_material *edge = edge_material(d.edge);
  t3_material *mats[3] = { face_material(d.front, &d), d.back >= 0 ? face_material(d.back, &d) : edge, edge };
  t3_mesh *mesh = t3_mesh_new_multi(g, mats, 3);
  t3_release(edge);
  mesh->base.cast_shadow = true;
  mesh->base.receive_shadow = true;
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  if (!cw_mesh3d_set_procedural(n, mesh)) CW_FATAL("bk_card_new: the engine refused the card mesh");
  if (K.ncards == (int)(sizeof K.cards / sizeof K.cards[0])) CW_FATAL("bk_card_new: more than %d cards", K.ncards);
  K.cards[K.ncards].node = n;
  K.cards[K.ncards].mesh = mesh; /* the node holds it */
  K.cards[K.ncards++].d = d;
  t3_release(mesh);
  return n;
}

void bk_card_set_front(cw_node card, int face) {
  for (int i = 0; i < K.ncards; i++)
    if (K.cards[i].node.id == card.id) {
      if (K.cards[i].d.front == face) return;
      t3_material *m = face_material(face, &K.cards[i].d);
      t3_mesh *mesh = K.cards[i].mesh;
      const t3_material *cur = mesh->materials[0];
      cw_value op = { 0 };
      if (!cw_node_get_prop(card, "opacity", &op)) CW_FATAL("bk_card_set_front: the card has no opacity");
      /* bk_ghost_set (opacity below 1): the engine re-clones the new face itself */
      if (op.f >= 1 && cur && cur->transparent) { /* bk_ghost_apply: its own see-through copy of the face, one per face */
        int fi = face_index(face);
        if (!K.faces[fi].ghost) {
          t3_material *g = t3_material_clone(m);
          g->transparent = true, g->opacity = cur->opacity, g->depth_write = false;
          g->color = cur->color;
          g->version++;
          K.faces[fi].ghost = g;
        }
        m = K.faces[fi].ghost;
      }
      t3_retain(m);
      t3_release(mesh->materials[0]);
      mesh->materials[0] = m;
      K.cards[i].d.front = face;
      return;
    }
  CW_FATAL("bk_card_set_front: not a card made by bk_card_new");
}

/* ── poses ── */
static float ease_in_out(float t) { return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }

bk_pose bk_card_flip_pose(bk_pose at, float t, float lift, bool to_face_up) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  float e = ease_in_out(t);
  float angle = (float)M_PI * (to_face_up ? 1 - e : e);
  bk_pose p = at;
  p.rot = t3_quat_normalize(t3_quat_multiply(at.rot, t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, angle)));
  p.pos.y += lift * sinf((float)M_PI * t);
  return p;
}

void bk_card_fan(int n, t3_vec3 centre, float radius, float spread, float tilt, bk_pose *out) {
  for (int i = 0; i < n; i++) {
    float a = n > 1 ? -spread * 0.5f + spread * (float)i / (float)(n - 1) : 0;
    t3_quat q = t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, -a), t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, tilt));
    out[i] = (bk_pose){ { centre.x + radius * sinf(a), centre.y + 0.004f * (float)i, centre.z + radius * (1 - cosf(a)) }, q };
  }
}

bk_pose bk_pose_approach(bk_pose from, bk_pose to, float rate, float dt) {
  float k = 1 - expf(-rate * dt);
  bk_pose p;
  p.pos = (t3_vec3){ from.pos.x + (to.pos.x - from.pos.x) * k, from.pos.y + (to.pos.y - from.pos.y) * k, from.pos.z + (to.pos.z - from.pos.z) * k };
  p.rot = t3_quat_slerp(from.rot, to.rot, k);
  return p;
}
