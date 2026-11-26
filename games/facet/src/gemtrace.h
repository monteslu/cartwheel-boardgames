/* Facet: the stones traced per pixel (the jewels game's tracer, gemshaders.c), drawn into the 3D scene by a pass of
 * the table's pipeline after the opaque scene. Until the studio map is baked (a few frames of rows), the game shows
 * its faceted mesh stones; gm_trace_ready says when to switch. */
#ifndef FACET_GEMTRACE_H
#define FACET_GEMTRACE_H

#include <stdbool.h>

#include "cartwheel/scene.h"

#define GM_TRACE_MAX 64

typedef struct gm_trace_gem {
  t3_vec3 pos;  /* the girdle's centre, world */
  float yaw;    /* turn about the vertical, radians */
  float size;   /* world units per local unit of the cut (cuts.h: the girdle's half-extent is about 1) */
  int cut;      /* jw_cuts index */
  float opacity;
} gm_trace_gem;

/* once, after the scene tree exists: adds the pass */
void gm_trace_init(cw_tree *tree);
/* a slice of the studio bake (call once a frame until ready) */
void gm_trace_bake_step(void);
bool gm_trace_ready(void);
/* the stones to draw this frame (copied) */
void gm_trace_set(const gm_trace_gem *g, int n);
/* how many the last frame drew (debug field) */
int gm_trace_drawn(void);

#endif
