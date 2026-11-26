/* Mandible: piece and marker geometry, built in code from our own designs (art.c). */
#ifndef MANDIBLE_ART_H
#define MANDIBLE_ART_H

#include "three.h"

#define ART_PIECE_RADIUS 0.47f /* centre to corner (the hex grid's is 0.5: a small gap between pieces) */
#define ART_PIECE_HEIGHT 0.30f

/* a carved piece of that side (0 pale stone, 1 dark wood) and kind, base at y 0, vertex colours */
t3_geometry *art_piece_geometry(int side, int kind);
/* a flat hexagonal ring and a flat hexagon at y 0, facing up */
t3_geometry *art_ring_geometry(float r_out, float r_in);
t3_geometry *art_hex_geometry(float r);
/* the table: a felt mat (centred, facing up at y 0, vertex colours) and a walnut plank top (the same) */
t3_geometry *art_felt_geometry(float half_x, float half_z);
t3_geometry *art_wood_top_geometry(float half_x, float half_z, float plank);
/* the inlay colour of a kind (rgb 0..1) */
const float *art_inlay_color(int kind);

#endif
