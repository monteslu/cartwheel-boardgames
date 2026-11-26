/* boardkit: counters, score deltas, ghosts and dice (bk_view.h). */
#include "bk_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bk_game.h"
#include "cartwheel/render3d.h"

static float ease_out(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

/* ── counters ── */
void bk_counter_init(bk_counter *c, int value) {
  memset(c, 0, sizeof *c);
  c->target = value;
  c->shown = c->from = (float)value;
  c->t = 1;
  c->seconds = 0.6f;
}
void bk_counter_set(bk_counter *c, int value) {
  if (value == c->target) return;
  c->from = c->shown;
  c->target = value;
  c->t = 0;
}
void bk_counter_update(bk_counter *c, float dt) {
  if (c->t >= 1) return;
  c->t += dt / (c->seconds > 0 ? c->seconds : 0.6f);
  if (c->t >= 1) c->t = 1;
  c->shown = c->from + ((float)c->target - c->from) * ease_out(c->t);
}
int bk_counter_value(const bk_counter *c) { return c->t >= 1 ? c->target : (int)lroundf(c->shown); }
bool bk_counter_moving(const bk_counter *c) { return c->t < 1; }

/* ── score deltas ── */
void bk_deltas_init(bk_deltas *ds) {
  memset(ds, 0, sizeof *ds);
  ds->life = 1.4f;
  ds->rise = 0.06f;
}
void bk_deltas_add(bk_deltas *ds, cw_vec2 at, int value, cw_color color) {
  int slot = 0;
  float oldest = -1;
  for (int i = 0; i < BK_DELTAS_MAX; i++) {
    if (!ds->d[i].live) { slot = i; oldest = 1e9f; break; }
    if (ds->d[i].age > oldest) oldest = ds->d[i].age, slot = i;
  }
  ds->d[slot] = (bk_delta){ .at = at, .value = value, .color = color, .age = 0, .live = true };
}
void bk_deltas_update(bk_deltas *ds, float dt) {
  for (int i = 0; i < BK_DELTAS_MAX; i++)
    if (ds->d[i].live && (ds->d[i].age += dt) >= ds->life) ds->d[i].live = false;
}
int bk_deltas_live(const bk_deltas *ds) {
  int n = 0;
  for (int i = 0; i < BK_DELTAS_MAX; i++) n += ds->d[i].live;
  return n;
}
void bk_deltas_draw(const bk_deltas *ds, cw_font font, float size) {
  cw_vec2 canvas = cw_r2d_canvas_size();
  for (int i = 0; i < BK_DELTAS_MAX; i++) {
    const bk_delta *d = &ds->d[i];
    if (!d->live) continue;
    float t = d->age / ds->life;
    /* fade in over the first 15%, hold, fade out over the last 40%: never a flash */
    float a = t < 0.15f ? t / 0.15f : t > 0.6f ? (1 - t) / 0.4f : 1;
    char txt[16];
    snprintf(txt, sizeof txt, d->value > 0 ? "+%d" : "%d", d->value);
    cw_text_style st = { .size = size, .color = d->color, .outline_width = size * 0.08f,
                         .outline_color = cw_rgba(0, 0, 0, 0.75f * a) };
    st.color.a *= a;
    cw_text_metrics m = cw_font_measure(font, txt, &st);
    cw_vec2 p = { d->at.x - m.width * 0.5f, d->at.y - m.height * 0.5f - canvas.y * ds->rise * ease_out(t) };
    cw_r2d_text(font, txt, p, &st);
  }
}

/* ── ghosts ── */
static void no_shadow(t3_object *o) {
  o->cast_shadow = false;
  for (int i = 0; i < o->child_count; i++) no_shadow(o->children[i]);
}
static int ghost_nodes(cw_node n, float opacity) {
  int k = 0;
  if (cw_node_is(n, "MeshInstance3D")) {
    cw_node_set(n, "opacity", cw_val_float(opacity));
    cw_node_set(n, "cast_shadow", cw_val_bool(false));
    k++;
  }
  for (int i = 0; i < cw_node_child_count(n); i++) k += ghost_nodes(cw_node_child(n, i), opacity);
  return k;
}
int bk_ghost_set(cw_node root, float opacity) {
  t3_object *o = cw_node3d_object(root);
  if (!o) CW_FATAL("bk_ghost_set: the root is not a Node3D");
  if (!(opacity >= 0 && opacity <= 1)) CW_FATAL("bk_ghost_set: opacity %g (0..1)", (double)opacity);
  int k = ghost_nodes(root, opacity);
  /* a procedural mesh keeps its three.c object's own shadow flag (the node's cast_shadow is not applied to it) */
  no_shadow(o);
  return k;
}

static int ghost_walk(t3_object *o, float opacity, cw_color tint) {
  int n = 0;
  if (o->type == T3_MESH || o->type == T3_INSTANCED_MESH || o->type == T3_SKINNED_MESH) {
    t3_mesh *mesh = (t3_mesh *)o;
    for (int i = 0; i < mesh->material_count; i++) {
      t3_material *old = mesh->materials[i];
      if (!old) continue;
      t3_material *m = t3_material_clone(old);
      m->transparent = true;
      m->opacity = opacity;
      m->depth_write = false;
      m->color.r += (tint.r - m->color.r) * tint.a;
      m->color.g += (tint.g - m->color.g) * tint.a;
      m->color.b += (tint.b - m->color.b) * tint.a;
      m->version++;
      mesh->materials[i] = m; /* the clone's own reference passes to the mesh */
      t3_release(old);
      n++;
    }
    o->cast_shadow = false;
  }
  for (int i = 0; i < o->child_count; i++) n += ghost_walk(o->children[i], opacity, tint);
  return n;
}
int bk_ghost_apply(cw_node root, float opacity, cw_color tint) {
  t3_object *o = cw_node3d_object(root);
  if (!o) CW_FATAL("bk_ghost_apply: the root is not a Node3D");
  return ghost_walk(o, opacity, tint);
}

/* ── dice ── */
/* each face's outward normal in the die's own frame */
static const t3_vec3 FACE_N[7] = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, -1, 0 } };

/* the rotation taking unit a onto unit b */
static t3_quat quat_between(t3_vec3 a, t3_vec3 b) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z;
  if (d > 0.9999f) return t3_quat_identity();
  if (d < -0.9999f) { /* opposite: half a turn about any axis at right angles */
    t3_vec3 ax = fabsf(a.x) < 0.9f ? (t3_vec3){ 1, 0, 0 } : (t3_vec3){ 0, 0, 1 };
    return t3_quat_from_axis_angle(ax, (float)M_PI);
  }
  t3_vec3 c = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
  float l = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z);
  return t3_quat_from_axis_angle((t3_vec3){ c.x / l, c.y / l, c.z / l }, acosf(d));
}

bk_die_throw bk_die_throw_make(t3_vec3 from, t3_vec3 to, int face, uint64_t seed) {
  if (face < 1 || face > 6) CW_FATAL("bk_die_throw_make: face %d (1..6)", face);
  cw_rng r;
  cw_rng_seed(&r, seed);
  t3_vec3 ax = { cw_rng_range(&r, -1, 1), cw_rng_range(&r, -0.4f, 0.4f), cw_rng_range(&r, -1, 1) };
  float l = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z);
  if (l < 1e-3f) ax = (t3_vec3){ 1, 0, 0 }, l = 1;
  return (bk_die_throw){ .from = from, .to = to, .face = face, .yaw = cw_rng_range(&r, 0, 6.2831853f),
                         .spin_axis = { ax.x / l, ax.y / l, ax.z / l }, .turns = 2.0f + (float)cw_rng_int(&r, 0, 2),
                         .seconds = 1.1f, .height = 0.9f };
}

void bk_die_pose(const bk_die_throw *th, float t, t3_vec3 *pos, t3_quat *rot) {
  float u = th->seconds > 0 ? t / th->seconds : 1;
  if (u > 1) u = 1;
  if (u < 0) u = 0;
  /* the resting orientation: the face's normal up, then the yaw */
  t3_quat rest = t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, th->yaw), quat_between(FACE_N[th->face], (t3_vec3){ 0, 1, 0 }));
  /* tumble: a turn about the spin axis that runs down to none, so it ends exactly at rest */
  float left = (1 - ease_out(u)) * th->turns * 6.2831853f;
  *rot = t3_quat_normalize(t3_quat_multiply(t3_quat_from_axis_angle(th->spin_axis, left), rest));
  /* along the table, slowing; height: three arcs, each lower (bounces), resting at 0 */
  float s = ease_out(u);
  pos->x = th->from.x + (th->to.x - th->from.x) * s;
  pos->z = th->from.z + (th->to.z - th->from.z) * s;
  static const float SPLIT[4] = { 0, 0.45f, 0.75f, 0.92f }, PEAK[3] = { 1, 0.3f, 0.08f };
  float h = 0;
  for (int k = 0; k < 3; k++)
    if (u >= SPLIT[k] && u < SPLIT[k + 1]) {
      float v = (u - SPLIT[k]) / (SPLIT[k + 1] - SPLIT[k]);
      h = 4 * v * (1 - v) * PEAK[k] * th->height;
      if (k == 0) h = fmaxf(h, (1 - v) * th->from.y - th->to.y); /* leaves the hand at its own height */
    }
  pos->y = th->to.y + h;
}

int bk_die_face_up(t3_quat rot) {
  int best = 1;
  float top = -2;
  for (int f = 1; f <= 6; f++) {
    t3_vec3 n = t3_vec3_apply_quat(FACE_N[f], rot);
    if (n.y > top) top = n.y, best = f;
  }
  return best;
}

static cw_node box(cw_node parent, const char *name, const char *prim, t3_vec3 at, t3_vec3 size, cw_color c, float rough) {
  cw_node m = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, m);
  cw_node_set(m, "primitive", cw_val_enum(prim));
  cw_node_set(m, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(m, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(m, "roughness", cw_val_float(rough));
  cw_node3d_set_position(m, at);
  return m;
}

cw_node bk_die_node(cw_node parent, const char *name, float size, cw_color body, cw_color pips) {
  cw_node die = cw_node_new("Node3D", name);
  cw_node_add_child(parent, die);
  cw_node b = box(die, "Body", "BOX", (t3_vec3){ 0, 0, 0 }, (t3_vec3){ size, size, size }, body, 0.35f);
  cw_node_set(b, "cast_shadow", cw_val_bool(true));
  /* pip layouts on a 3 x 3 grid of a face (-1, 0, 1), per face value */
  static const int8_t PIP[7][6][2] = {
    { { 0 } }, { { 0, 0 } }, { { -1, -1 }, { 1, 1 } }, { { -1, -1 }, { 0, 0 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } }, { { -1, -1 }, { 1, -1 }, { 0, 0 }, { -1, 1 }, { 1, 1 } },
    { { -1, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 } },
  };
  float h = size * 0.5f, g = size * 0.27f, r = size * 0.17f;
  for (int f = 1; f <= 6; f++) {
    t3_vec3 n = FACE_N[f];
    /* two axes across the face */
    t3_vec3 u = fabsf(n.y) > 0.5f ? (t3_vec3){ 1, 0, 0 } : (t3_vec3){ 0, 1, 0 };
    t3_vec3 v = { n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x };
    for (int k = 0; k < f; k++) {
      float a = PIP[f][k][0] * g, c = PIP[f][k][1] * g;
      t3_vec3 at = { n.x * h + u.x * a + v.x * c, n.y * h + u.y * a + v.y * c, n.z * h + u.z * a + v.z * c };
      char nm[16];
      snprintf(nm, sizeof nm, "Pip%d_%d", f, k);
      /* a flattened sphere sunk into the face: a dimple, not a bump */
      cw_node p = box(die, nm, "SPHERE", at, (t3_vec3){ r, r, r }, pips, 0.6f);
      cw_node3d_set_scale(p, (t3_vec3){ fabsf(n.x) > 0.5f ? 0.35f : 1, fabsf(n.y) > 0.5f ? 0.35f : 1, fabsf(n.z) > 0.5f ? 0.35f : 1 });
    }
  }
  return die;
}
