/* Facet: the stones' shaders (gemshaders.c), from the jewels game's tracer. */
#ifndef FACET_GEMSHADERS_H
#define FACET_GEMSHADERS_H

#include <stdbool.h>

#include "cuts.h"

#define GM_STUDIO_N 512 /* the studio map: N x N texels of sky, then two rows of lamps */

/* rows of the studio map (RGBA float, GM_STUDIO_N wide, GM_STUDIO_N + 2 rows): the stones' environment */
void gm_studio_rows(float *rgba, int row0, int nrows);
/* the program for a cut (malloc'd); GL also returns its vertex stage in *vs */
char *gm_shader_gem(bool wgsl, const jw_cut *c, char **vs);

#endif
