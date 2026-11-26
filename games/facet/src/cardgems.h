/* Facet: the stones printed on the cards, baked once from the cuts themselves: each cut traced through its facets
 * on the CPU (cuts.c jw_trace_cpu: Fresnel at every face, total internal reflection, absorption, dispersion at three
 * wavelengths) against a small studio of soft lights, seen from a little in front of overhead, into a picture with
 * alpha. The card painter draws these pictures; the table's own stones are traced on the GPU (gemtrace.c). */
#ifndef FACET_CARDGEMS_H
#define FACET_CARDGEMS_H

#include <stdint.h>

#include "cartwheel/render2d.h"

#define GM_CARDGEM_PX 192

/* the picture of a cut, RGBA8 premultiplied-free (straight alpha), GM_CARDGEM_PX square; baked on first call */
const uint8_t *gm_cardgem_pixels(int cut);
/* the same as a 2D texture (made on first call; needs the GPU) */
cw_texture gm_cardgem_texture(int cut);

#endif
