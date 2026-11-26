/* crokinole's view (view.h): a lacquered maple board with a walnut rail and a felt ditch, brass pegs in black rubber
 * sleeves, turned discs with a groove, on a dark wooden table under one warm lamp. Built in code at load. */
#include "view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "cartwheel/math.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/render2d.h"
#include "crk.h"

#define R_DITCH 0.384f   /* the ditch's outer edge (the rail's inner face) */
#define R_RAIL 0.418f    /* the rail's outer edge */
#define DITCH_D 0.011f   /* how deep the ditch is under the surface */
#define RAIL_H 0.030f    /* the rail's top over the surface */
#define BOARD_T 0.045f   /* the board's thickness under the surface (it stands on the table) */
#define TABLE_Y (-BOARD_T)

const float SIDE_COL[3][3] = { { 0.80f, 0.62f, 0.40f }, { 0.055f, 0.045f, 0.04f }, { 0.50f, 0.07f, 0.05f } };

typedef struct disc_view {
  float x, y, h;       /* drawn (board frame) */
  float ditch_a;       /* its place in the ditch (angle), once it fell there */
  int last_st;
  float t;             /* seconds in the current state (a 20's drop) */
  bool placed;
} disc_view;

static struct {
  cw_node root, camera, board;
  cw_node disc[CRK_NDISC];
  disc_view dv[CRK_NDISC];
  t3_material *disc_mat[CRK_NDISC];
  float cam_a, cam_a_want;
  bool cam_init;
  t3_vec3 eye, at;
  float fov;
} V;

static t3_material *std_mat(float r, float g, float b, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = (t3_color){ r, g, b };
  m->roughness = rough, m->metalness = metal;
  return m;
}

static t3_texture *tex_from(uint8_t *px, int n, bool repeat, float rep) {
  t3_texture *t = t3_texture_new(n, n, px);
  free(px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->anisotropy = 8;
  if (repeat) t->wrap_s = t->wrap_t = T3_REPEAT, t->repeat = (t3_vec2){ rep, rep };
  return t;
}

static cw_node piece(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast, bool receive) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *grp = t3_group_new();
  t3_mesh *mesh = t3_mesh_new(g, m);
  mesh->base.cast_shadow = cast, mesh->base.receive_shadow = receive;
  t3_object_add(grp, mesh);
  cw_node3d_set_object(n, grp);
  t3_release(mesh), t3_release(grp), t3_release(g), t3_release(m);
  return n;
}

/* a flat ring (y up) from r0 to r1 at height h, its uv planar over a square of half size uv_r */
static t3_geometry *flat_ring(float r0, float r1, float h, int segs, int rings, float uv_r) {
  int nv = (segs + 1) * (rings + 1);
  float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nrm = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
  unsigned *idx = malloc(sizeof(unsigned) * 6 * (size_t)segs * (size_t)rings);
  int v = 0;
  for (int k = 0; k <= rings; k++) {
    float r = r0 + (r1 - r0) * (float)k / (float)rings;
    for (int i = 0; i <= segs; i++) {
      float a = (float)i / (float)segs * 6.2831853f, x = cosf(a) * r, y = sinf(a) * r; /* board frame */
      t3_vec3 p = bw(x, y, h);
      pos[v * 3] = p.x, pos[v * 3 + 1] = p.y, pos[v * 3 + 2] = p.z;
      nrm[v * 3] = 0, nrm[v * 3 + 1] = 1, nrm[v * 3 + 2] = 0;
      uv[v * 2] = 0.5f + x / (2 * uv_r), uv[v * 2 + 1] = 0.5f + y / (2 * uv_r);
      v++;
    }
  }
  int n = 0;
  for (int k = 0; k < rings; k++)
    for (int i = 0; i < segs; i++) {
      unsigned a = (unsigned)(k * (segs + 1) + i), b = a + 1, c = a + (unsigned)(segs + 1), d = c + 1;
      /* counter-clockwise seen from above (+y) */
      idx[n++] = a, idx[n++] = c, idx[n++] = b;
      idx[n++] = b, idx[n++] = c, idx[n++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, n, 1));
  t3_geometry_compute_bounding_sphere(g);
  t3_geometry_compute_bounding_box(g);
  free(pos), free(nrm), free(uv), free(idx);
  return g;
}

/* a turned profile (r, height) round the vertical axis */
static t3_geometry *lathe(const float (*p)[2], int n, int segs) {
  t3_vec2 *pts = malloc(sizeof(t3_vec2) * (size_t)n);
  for (int i = 0; i < n; i++) pts[i] = (t3_vec2){ p[i][0], p[i][1] };
  t3_geometry *g = t3_lathe_geometry_new(pts, n, segs, 0, 6.2831853f);
  free(pts);
  return g;
}

static void build_board(void) {
  cw_node b = V.board;
  /* the playing surface: lacquered maple (a clear coat over the painted wood) */
  {
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->roughness = 0.42f, m->metalness = 0;
    m->clearcoat = 1.0f, m->clearcoat_roughness = 0.12f;
    t3_material_set_map(m, tex_from(art_board(1536), 1536, false, 1));
    t3_release(m->map);
    piece(b, "Surface", flat_ring(CRK_R_HOLE, CRK_R_SURF, 0, 192, 24, CRK_R_SURF), m, false, true);
  }
  /* the hole: a bored shaft into dark wood, a felt pad at its foot */
  {
    t3_geometry *g = t3_cylinder_geometry_new(CRK_R_HOLE, CRK_R_HOLE, 0.024f, 40, 1, true);
    t3_material *m = std_mat(0.10f, 0.06f, 0.035f, 0.7f, 0);
    m->side = T3_BACK_SIDE;
    cw_node n = piece(b, "Hole", g, m, false, true);
    cw_node3d_set_position(n, (t3_vec3){ 0, -0.012f, 0 });
    cw_node f = piece(b, "HoleFoot", flat_ring(0, CRK_R_HOLE, -0.024f, 40, 1, 0.05f), std_mat(0.02f, 0.05f, 0.03f, 0.95f, 0), false, true);
    (void)f;
  }
  /* the ditch: felt, and the surface's edge falling into it */
  {
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->roughness = 0.95f;
    m->sheen = 1, m->sheen_roughness = 0.6f, m->sheen_color = (t3_color){ 0.15f, 0.3f, 0.2f };
    t3_material_set_map(m, tex_from(art_felt(256), 256, true, 6));
    t3_release(m->map);
    piece(b, "Ditch", flat_ring(CRK_R_SURF, R_DITCH, -DITCH_D, 192, 2, 0.05f), m, false, true);
    t3_geometry *edge = t3_cylinder_geometry_new(CRK_R_SURF, CRK_R_SURF, DITCH_D, 192, 1, true);
    cw_node e = piece(b, "Edge", edge, std_mat(0.55f, 0.36f, 0.2f, 0.5f, 0), false, true);
    cw_node3d_set_position(e, (t3_vec3){ 0, -DITCH_D * 0.5f, 0 });
  }
  /* the rail: walnut, rounded over, standing on the table with the board */
  {
    const float p[][2] = { { R_RAIL + 0.004f, TABLE_Y }, { R_RAIL + 0.004f, RAIL_H - 0.010f }, { R_RAIL + 0.002f, RAIL_H - 0.004f },
                           { R_RAIL - 0.004f, RAIL_H },    { R_DITCH + 0.006f, RAIL_H },       { R_DITCH + 0.001f, RAIL_H - 0.004f },
                           { R_DITCH, RAIL_H - 0.010f },    { R_DITCH, -DITCH_D } };
    /* the lathe turns the profile listed from the outside in; three.js's lathe faces outward for a profile going up, so
     * list it so the outer face looks out */
    float q[8][2];
    for (int i = 0; i < 8; i++) q[i][0] = p[7 - i][0], q[i][1] = p[7 - i][1];
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->roughness = 0.4f, m->clearcoat = 0.8f, m->clearcoat_roughness = 0.2f;
    t3_material_set_map(m, tex_from(art_walnut(512), 512, true, 1));
    t3_release(m->map);
    m->side = T3_DOUBLE_SIDE;
    piece(b, "Rail", lathe((const float (*)[2])q, 8, 192), m, true, true);
  }
  /* the pegs: brass posts in black rubber sleeves */
  {
    for (int i = 0; i < CRK_NPEG; i++) {
      float x, y;
      crk_peg(i, &x, &y);
      t3_vec3 c = bw(x, y, 0);
      cw_node sleeve = piece(b, "PegSleeve", t3_cylinder_geometry_new(CRK_R_PEG, CRK_R_PEG, 0.016f, 24, 1, false), std_mat(0.02f, 0.02f, 0.022f, 0.75f, 0), true, true);
      cw_node3d_set_position(sleeve, (t3_vec3){ c.x, 0.008f, c.z });
      cw_node top = piece(b, "PegTop", t3_cylinder_geometry_new(0.0030f, 0.0034f, 0.006f, 20, 1, false), std_mat(0.95f, 0.72f, 0.36f, 0.25f, 1), true, true);
      cw_node3d_set_position(top, (t3_vec3){ c.x, 0.019f, c.z });
    }
  }
  /* the table: dark planks under the board, wide enough to fill the view */
  {
    t3_material *m = std_mat(1, 1, 1, 0.55f, 0);
    t3_material_set_map(m, tex_from(art_table(512), 512, true, 3));
    t3_release(m->map);
    t3_geometry *pg = t3_plane_geometry_new(4.0f, 4.0f, 1, 1);
    t3_mat4 rx = { { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } };
    t3_geometry_apply_matrix4(pg, &rx);
    cw_node n = piece(V.root, "Table", pg, m, false, true);
    cw_node3d_set_position(n, (t3_vec3){ 0, TABLE_Y, 0 });
  }
}

/* a disc: turned maple or ebonised, bevelled, a groove on its face */
static t3_geometry *disc_geo(void) {
  const float r = CRK_R_DISC, h = DISC_H;
  const float p[][2] = { { 0, 0 },           { r - 0.0012f, 0 },        { r, 0.0012f },          { r, h - 0.0012f },
                         { r - 0.0012f, h }, { r * 0.70f, h },          { r * 0.66f, h - 0.0006f }, { r * 0.62f, h },
                         { 0, h } };
  return lathe(p, 9, 40);
}

void view_init(cw_node root, cw_node camera) {
  memset(&V, 0, sizeof V);
  V.root = root, V.camera = camera;
  V.fov = 34;
}

void view_build(void) {
  V.board = cw_node_new("Node3D", "Board");
  cw_node_add_child(V.root, V.board);
  build_board();
  t3_texture *top = tex_from(art_disc_top(256), 256, false, 1);
  for (int i = 0; i < CRK_NDISC; i++) {
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->roughness = 0.35f, m->clearcoat = 0.9f, m->clearcoat_roughness = 0.15f;
    t3_material_set_map(m, top);
    V.disc_mat[i] = m;
    t3_retain(m);
    V.disc[i] = piece(V.root, "Disc", disc_geo(), m, true, true);
  }
  t3_release(top);
}

/* where a seat's discs wait: a row on the table behind its part of the rail */
static void rack_place(int players, int seat, int k, float *x, float *y) {
  float ax, ay;
  crk_seat_axis(players, seat, &ax, &ay);
  float tx = -ay, ty = ax; /* along the rail */
  float off = ((float)k - 5.5f) * 0.036f;
  float rr = R_RAIL + 0.05f;
  *x = ax * rr + tx * off, *y = ay * rr + ty * off;
}

static void twenty_place(int players, int seat, int k, float *x, float *y) {
  float ax, ay;
  crk_seat_axis(players, seat, &ax, &ay);
  float tx = -ay, ty = ax;
  float rr = R_RAIL + 0.095f;
  *x = ax * rr + tx * (0.16f + (float)k * 0.036f), *y = ay * rr + ty * (0.16f + (float)k * 0.036f);
}

static float approach(float v, float to, float k) { return v + (to - v) * k; }

void view_sync(const game *g, float dt) {
  const crk_state *s = game_view_state(g);
  int aim_disc = g->phase == GP_AIM ? crk_next_disc(s, g->aim_seat) : -1;
  int in_hand[4] = { 0 }, twenties[4] = { 0 };
  float k = 1.0f - expf(-dt * 9.0f);
  for (int i = 0; i < CRK_NDISC; i++) {
    const crk_disc *d = &s->d[i];
    disc_view *v = &V.dv[i];
    float tx = v->x, ty = v->y, th = 0;
    bool exact = false;
    if (d->st != v->last_st) v->t = 0, v->last_st = d->st;
    v->t += dt;
    switch (d->st) {
    case CRK_HAND:
      if (i == aim_disc) {
        float dx, dy;
        game_aim_disc(g, &tx, &ty, &dx, &dy);
      } else rack_place(s->players, d->owner, in_hand[d->owner]++, &tx, &ty);
      th = i == aim_disc ? 0 : TABLE_Y;
      break;
    case CRK_BOARD:
      tx = d->x, ty = d->y, th = 0, exact = true;
      break;
    case CRK_DITCH:
      if (v->t <= dt * 1.5f) { /* just fell: its place in the ditch, clear of the others */
        float a = crk_atan2(d->y, d->x);
        for (int tries = 0; tries < 40; tries++) {
          bool clear = true;
          for (int j = 0; j < CRK_NDISC; j++)
            if (j != i && s->d[j].st == CRK_DITCH && V.dv[j].last_st == CRK_DITCH && V.dv[j].t > dt * 1.5f) {
              float da = fabsf(remainderf(a - V.dv[j].ditch_a, 6.2831853f));
              if (da < 0.095f) clear = false;
            }
          if (clear) break;
          a += 0.1f;
        }
        v->ditch_a = a;
      }
      tx = cosf(v->ditch_a) * (CRK_R_SURF + R_DITCH) * 0.5f, ty = sinf(v->ditch_a) * (CRK_R_SURF + R_DITCH) * 0.5f, th = -DITCH_D;
      break;
    case CRK_TWENTY:
      if (v->t < 0.6f) tx = 0, ty = 0, th = -0.02f, v->x = approach(v->x, 0, 0.5f), v->y = approach(v->y, 0, 0.5f); /* down the hole: centred at once, then it sinks */
      else twenty_place(s->players, d->owner, twenties[d->owner]++, &tx, &ty), th = TABLE_Y;
      if (v->t >= 0.6f) break;
      twenties[d->owner]++;
      break;
    }
    if (!v->placed || exact) v->x = tx, v->y = ty, v->h = th, v->placed = true;
    else {
      v->x = approach(v->x, tx, k), v->y = approach(v->y, ty, k), v->h = approach(v->h, th, k);
    }
    cw_node3d_set_position(V.disc[i], bw(v->x, v->y, v->h));
    cw_node3d_set_rotation(V.disc[i], (t3_vec3){ 0, d->rot, 0 });
    const float *c = SIDE_COL[d->side < 3 ? d->side : 0];
    V.disc_mat[i]->color = (t3_color){ c[0], c[1], c[2] };
  }
  /* the camera: behind the seat to move, turning round the table between turns */
  float ax, ay;
  crk_seat_axis(s->players, g->phase == GP_FLIGHT || g->phase == GP_RESULT ? g->last_seat : g->aim_seat, &ax, &ay);
  V.cam_a_want = atan2f(ay, ax);
  if (!V.cam_init) V.cam_a = V.cam_a_want, V.cam_init = true;
  float da = remainderf(V.cam_a_want - V.cam_a, 6.2831853f);
  V.cam_a += da * (1.0f - expf(-dt * 2.5f));
  float cx = cosf(V.cam_a), cy = sinf(V.cam_a);
  V.eye = bw(cx * 0.70f, cy * 0.70f, 0.86f);
  V.at = bw(cx * 0.035f, cy * 0.035f, -0.02f);
  cw_node3d_set_position(V.camera, V.eye);
  cw_node3d_look_at(V.camera, V.at, (t3_vec3){ 0, 1, 0 });
  cw_node_set(V.camera, "fov", cw_val_float(V.fov));
}

float view_cam_angle(void) { return V.cam_a; }

static cw_rect canvas_rect(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  return (cw_rect){ 0, 0, cs.x, cs.y };
}

bool view_to_canvas(float x, float y, float h, float *cx, float *cy) {
  float depth;
  cw_vec2 p = cw_camera3d_world_to_screen(V.camera, canvas_rect(), bw(x, y, h), &depth);
  *cx = p.x, *cy = p.y;
  return depth > 0;
}

bool view_from_canvas(float cx, float cy, float *x, float *y) {
  cw_ray3d r = cw_camera3d_screen_ray(V.camera, canvas_rect(), (cw_vec2){ cx, cy });
  if (r.direction.y > -1e-4f) return false;
  float t = -r.origin.y / r.direction.y;
  t3_vec3 p = { r.origin.x + r.direction.x * t, 0, r.origin.z + r.direction.z * t };
  *x = p.x, *y = -p.z;
  return true;
}
