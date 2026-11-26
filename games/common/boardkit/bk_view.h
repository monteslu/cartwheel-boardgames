/* boardkit: small view helpers every board game draws with. None of them touch rules state: they read what
 * the rules decided and show it.
 *
 *   bk_counter  a number on the HUD that counts toward its new value instead of jumping (money, score)
 *   bk_deltas   "+3" / "-2" that rise from where the change happened and fade (a score delta overlay)
 *   bk_ghost    a see-through copy of a piece where a move would put it, before the player confirms
 *   bk_die3d    a die that tumbles across the table and lands on the face the rules rolled
 *
 * Animation runs on render time (dt in seconds) and never decides anything; a test or a replay reads rules
 * state, never these. No effect here flashes: changes ease in and fade out. */
#ifndef BK_VIEW_H
#define BK_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── counters ── */
typedef struct bk_counter {
  int target;
  float shown;
  float seconds; /* time a full change takes to count (0.6) */
  float from;    /* value when the change began */
  float t;       /* 0..1 through the change */
} bk_counter;
void bk_counter_init(bk_counter *c, int value);      /* shows value at once */
void bk_counter_set(bk_counter *c, int value);       /* counts toward value */
void bk_counter_update(bk_counter *c, float dt);
int bk_counter_value(const bk_counter *c);           /* the number to print now */
bool bk_counter_moving(const bk_counter *c);

/* ── score deltas ── */
#define BK_DELTAS_MAX 32
typedef struct bk_delta { cw_vec2 at; int value; cw_color color; float age; bool live; } bk_delta;
typedef struct bk_deltas {
  bk_delta d[BK_DELTAS_MAX];
  float life;   /* seconds each shows (1.4) */
  float rise;   /* canvas px it rises over its life, as a share of the canvas height (0.06) */
} bk_deltas;
void bk_deltas_init(bk_deltas *ds);
/* a delta at a canvas point (cw_camera3d_world_to_screen for a 3D spot); a full pool drops the oldest */
void bk_deltas_add(bk_deltas *ds, cw_vec2 at, int value, cw_color color);
void bk_deltas_update(bk_deltas *ds, float dt);
/* inside a cw_r2d pass: each one's "+n" centred on its point, easing up and fading out */
void bk_deltas_draw(const bk_deltas *ds, cw_font font, float size);
int bk_deltas_live(const bk_deltas *ds);

/* ── ghost previews ── */
/* Make every MeshInstance3D at and under `root` (a copy of the piece the game built) see-through at `opacity`
 * (0..1, the node's opacity property: the engine clones its materials transparent) and cast no shadow (a ghost
 * stays shadowless). Works at once, on any frame; returns the meshes set (0: nothing to show, a bug in the caller). Move
 * or hide the root to show the preview. Its program states ("+trans") need to be in the game's program table: run
 * the programs survey with a ghost on screen. */
int bk_ghost_set(cw_node root, float opacity);
/* Before v0.84.0 (kept until every game moves to bk_ghost_set): make every mesh under `root` (a Node3D subtree the game built as a copy of the piece) see-through: each
 * mesh gets its own transparent copy of its material at `opacity`, tinted toward `tint` by tint.a, drawing no
 * depth. Call once after building the copy (a frame after: the meshes must exist; returns the meshes changed,
 * 0 means call again next frame). Move or hide the root to show the preview. Its program states need to be
 * in the game's program table: run the programs survey with a ghost on screen. */
int bk_ghost_apply(cw_node root, float opacity, cw_color tint);

/* ── dice ── */
/* Faces: +Y 1, +X 2, +Z 3, -Z 4, -X 5, -Y 6 (opposites add to 7). */
typedef struct bk_die_throw {
  t3_vec3 from, to;   /* where it leaves the hand and where it comes to rest (its centre, at rest height) */
  int face;           /* 1..6: the face that ends up */
  float yaw;          /* its final turn about the vertical, radians */
  t3_vec3 spin_axis;  /* unit; the tumble's axis */
  float turns;        /* whole turns of tumble (3) */
  float seconds;      /* the throw's length (1.1) */
  float height;       /* the first arc's peak above the rest height (0.9) */
} bk_die_throw;
/* a throw from a seed (the rules' roll result as face; seed from the state hash, so a replay throws alike) */
bk_die_throw bk_die_throw_make(t3_vec3 from, t3_vec3 to, int face, uint64_t seed);
/* the die's pose t seconds into the throw (t >= seconds: at rest on its face) */
void bk_die_pose(const bk_die_throw *th, float t, t3_vec3 *pos, t3_quat *rot);
/* the face pointing up for an orientation */
int bk_die_face_up(t3_quat rot);
/* A die node: a rounded-looking ivory cube (edge `size` metres) with dark pips on its six faces, under
 * parent. Its pose is the caller's to set (cw_node3d_set_position / _quaternion from bk_die_pose). */
cw_node bk_die_node(cw_node parent, const char *name, float size, cw_color body, cw_color pips);

#ifdef __cplusplus
}
#endif
#endif
