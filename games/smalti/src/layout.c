/* Smalti: the table layout. See layout.h. */
#include "layout.h"

#include <string.h>

void ly_table_make(ly_table *t, int players, int trays) {
  memset(t, 0, sizeof *t);
  t->players = players;
  t->trays = trays;
  const float side = 4.9f; /* half the middle column's width, boards beyond it */
  float zt = -LY_BOARD_H - 0.3f, zb = 0.3f, zc = -LY_BOARD_H * 0.5f;
  /* seats in play order round the table, clockwise: four: left top, right top, right bottom, left bottom; three:
   * left top, right middle, left bottom; two: left and right, middle */
  for (int s = 0; s < players; s++) {
    bool left;
    float z;
    if (players == 2) { left = s == 0; z = zc; }
    else if (players == 3) { left = s != 1; z = s == 0 ? zt : s == 1 ? zc : zb; }
    else { left = s == 0 || s == 3; z = s < 2 ? zt : zb; }
    t->board[s] = (ly_point){ left ? -side - LY_BOARD_W : side, z };
  }
  /* trays: rows of up to three, centred, at the top of the middle column */
  static const int ROWS5[] = { 3, 2 }, ROWS7[] = { 2, 3, 2 }, ROWS9[] = { 3, 3, 3 };
  const int *rows = trays == 5 ? ROWS5 : trays == 7 ? ROWS7 : ROWS9;
  int nrows = trays == 5 ? 2 : 3;
  const float step = 2.75f;
  float z0 = -7.1f + (3 - nrows) * step * 0.5f;
  int k = 0;
  for (int r = 0; r < nrows; r++)
    for (int c = 0; c < rows[r]; c++) t->tray[k++] = (ly_point){ (c - (rows[r] - 1) * 0.5f) * step, z0 + r * step };
  t->pool = (ly_point){ 0, 3.85f };
  t->pool_w = 9.0f;
  t->pool_h = 6.9f;
  t->marker_home = (ly_point){ 0, 0.95f };
  t->bag = (ly_point){ 0, -14.0f };
  t->min_x = -side - LY_BOARD_W - 0.3f;
  t->max_x = side + LY_BOARD_W + 0.3f;
  t->min_z = -LY_BOARD_H - 0.6f;
  t->max_z = LY_BOARD_H + 0.6f;
}

static ly_point on_board(const ly_table *t, int seat, float x, float z) {
  return (ly_point){ t->board[seat].x + x, t->board[seat].z + z };
}

ly_point ly_line_cell(const ly_table *t, int seat, int row, int k) {
  return on_board(t, seat, LY_MARGIN + 4 - k + 0.5f, LY_MARGIN + row + 0.5f);
}
ly_point ly_wall_cell(const ly_table *t, int seat, int row, int col) {
  return on_board(t, seat, LY_MARGIN + 5 + LY_GAP + col + 0.5f, LY_MARGIN + row + 0.5f);
}
ly_point ly_floor_cell(const ly_table *t, int seat, int i) {
  return on_board(t, seat, LY_MARGIN + i * LY_STRIP_STEP + 0.5f, LY_STRIP_Y + 0.5f);
}
ly_point ly_line_centre(const ly_table *t, int seat, int row) {
  return on_board(t, seat, LY_MARGIN + 5 - (row + 1) * 0.5f, LY_MARGIN + row + 0.5f);
}
ly_point ly_floor_centre(const ly_table *t, int seat) {
  return on_board(t, seat, LY_MARGIN + 3 * LY_STRIP_STEP + 0.5f, LY_STRIP_Y + 0.5f);
}
ly_point ly_tray_slot(const ly_table *t, int tray, int i) {
  ly_point c = t->tray[tray];
  float d = 0.46f;
  return (ly_point){ c.x + ((i & 1) ? d : -d), c.z + ((i & 2) ? d : -d) };
}
ly_point ly_pool_slot(const ly_table *t, int colour, int i, int *layer) {
  if (layer) *layer = i / LY_POOL_SLOTS;
  int k = i % LY_POOL_SLOTS;
  return (ly_point){ t->pool.x + (k - (LY_POOL_SLOTS - 1) * 0.5f) * 1.0f + (i / LY_POOL_SLOTS) * 0.12f,
                     2.05f + colour * 1.12f };
}
ly_point ly_plate(const ly_table *t, int seat) { return on_board(t, seat, LY_MARGIN + 1.6f, LY_MARGIN + 0.85f); }
