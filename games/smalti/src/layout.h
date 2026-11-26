/* Smalti: where everything lies on the table (pure math, no engine calls beyond the vector types).
 *
 * Table units: one mosaic cell is 1. The table is the x/z plane, y up; x right and z toward the bottom of the
 * screen (the camera sits at +z looking down). The trays and the centre pool share a column in the middle; the
 * player boards stand left and right of it, two a side at most. art/make-art.mjs draws the board with the same
 * numbers (BOARD there). */
#ifndef SMALTI_LAYOUT_H
#define SMALTI_LAYOUT_H

#include "rules.h"

#define LY_BOARD_W 11.6f
#define LY_BOARD_H 7.4f
#define LY_MARGIN 0.45f
#define LY_GAP 0.7f
#define LY_STRIP_Y 5.85f
#define LY_STRIP_STEP 1.02f
#define LY_TRAY_R 1.22f        /* a tray's radius */
#define LY_TILE 0.84f          /* a tile's side */
#define LY_TILE_H 0.30f        /* a tile's thickness */
#define LY_POOL_SLOTS 8        /* tiles in one lane of the centre pool before they stack */

typedef struct ly_point { float x, z; } ly_point;

typedef struct ly_table {
  int players, trays;
  ly_point board[BK_MAX_SEATS]; /* each board's top-left corner */
  ly_point tray[MS_MAX_TRAYS];  /* tray centres */
  ly_point pool;                /* the centre pool's centre */
  float pool_w, pool_h;
  ly_point marker_home;         /* the first-player marker in the pool */
  ly_point bag;                 /* where tiles go to the box and come from the bag (off the top edge) */
  float min_x, max_x, min_z, max_z; /* what the camera must show */
} ly_table;

void ly_table_make(ly_table *t, int players, int trays);
/* a staging cell: row r, slot k counted from the right (the cell next to the mosaic is k = 0) */
ly_point ly_line_cell(const ly_table *t, int seat, int row, int k);
ly_point ly_wall_cell(const ly_table *t, int seat, int row, int col);
ly_point ly_floor_cell(const ly_table *t, int seat, int i);
/* the centre of a staging row (the whole row, all its cells), for the cursor and pointer */
ly_point ly_line_centre(const ly_table *t, int seat, int row);
ly_point ly_floor_centre(const ly_table *t, int seat);
/* slot i (0..3) of a tray */
ly_point ly_tray_slot(const ly_table *t, int tray, int i);
/* slot i of colour c's lane in the pool; *layer is the stack height (0 on the cloth) */
ly_point ly_pool_slot(const ly_table *t, int colour, int i, int *layer);
/* the score plate's place on a board (the empty corner above the staging stairs) */
ly_point ly_plate(const ly_table *t, int seat);

#endif
