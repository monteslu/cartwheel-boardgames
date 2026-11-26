/* crokinole's painted textures (art.c): malloc'd RGBA, n x n, sRGB */
#ifndef CRK_ART_H
#define CRK_ART_H
#include <stdint.h>

uint8_t *art_board(int n);    /* the playing surface: a square over the surface's circle (u right, v down = +y up) */
uint8_t *art_walnut(int n);   /* the rail */
uint8_t *art_table(int n);    /* the table top */
uint8_t *art_felt(int n);     /* the ditch */
uint8_t *art_disc_top(int n); /* a disc's face (grey: the material's colour tints it) */

#endif
