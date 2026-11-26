/* Lantern Harbor: the table layout. See layout.h. */
#include "layout.h"

#include <string.h>

void ly_table_make(ly_table *t, int players) {
  memset(t, 0, sizeof *t);
  t->players = players;
  const float zt = -6.0f, zb = 2.2f, zc = -1.9f;
  /* seats round the table, clockwise: four: left top, right top, right bottom, left bottom; three: left top, right
   * middle, left bottom; two: left and right, middle */
  for (int s = 0; s < players; s++) {
    bool left;
    float z;
    if (players == 2) { left = s == 0; z = zc; }
    else if (players == 3) { left = s != 1; z = s == 0 ? zt : s == 1 ? zc : zb; }
    else { left = s == 0 || s == 3; z = s < 2 ? zt : zb; }
    t->board[s] = (ly_point){ left ? -LY_SIDE - LY_BOARD_W : LY_SIDE, z };
  }
  t->min_x = -LY_SIDE - LY_BOARD_W - 0.4f;
  t->max_x = LY_SIDE + LY_BOARD_W + 0.4f;
  t->min_z = -13.6f;
  t->max_z = 10.4f;
}

ly_point ly_on_board(const ly_table *t, int seat, float x, float z) {
  return (ly_point){ t->board[seat].x + x, t->board[seat].z + z };
}

static ly_point cell(const ly_table *t, int seat, float x0, int col, int row) {
  return ly_on_board(t, seat, x0 + col * LY_CELL + LY_CELL * 0.5f, LY_GRID_Z + row * LY_CELL + LY_CELL * 0.5f);
}

ly_point ly_plot(const ly_table *t, int seat, int i) { return cell(t, seat, LY_HILL_X, i % 4, i / 4); }

/* the town fills row by row; a grand building takes the first two free lots side by side */
static void town_fill(const hb_player *pl, int extra, int lot_of[HB_BUILDINGS + 1][2]) {
  bool used[12] = { 0 };
  int n = pl->nbld + (extra >= 0 ? 1 : 0);
  for (int i = 0; i < n; i++) {
    int b = i < pl->nbld ? pl->bld[i] : extra;
    int slot = i < pl->nbld ? i : HB_BUILDINGS;
    lot_of[slot][0] = lot_of[slot][1] = -1;
    if (hb_bld[b].lots == 1) {
      for (int k = 0; k < 12; k++)
        if (!used[k]) { used[k] = true; lot_of[slot][0] = k; break; }
    } else {
      for (int k = 0; k < 12 && lot_of[slot][0] < 0; k++)
        if (k % 4 < 3 && !used[k] && !used[k + 1]) { used[k] = used[k + 1] = true; lot_of[slot][0] = k; lot_of[slot][1] = k + 1; }
      for (int k = 0; k < 12 && lot_of[slot][1] < 0; k++) /* no pair side by side: any two lots (never in play: 12 lots) */
        if (!used[k]) {
          used[k] = true;
          if (lot_of[slot][0] < 0) lot_of[slot][0] = k;
          else lot_of[slot][1] = k;
        }
    }
  }
}

static ly_point lot_centre(const ly_table *t, int seat, const int lots[2]) {
  int a = lots[0] < 0 ? 11 : lots[0];
  ly_point p = cell(t, seat, LY_TOWN_X, a % 4, a / 4);
  if (lots[1] >= 0) {
    ly_point q = cell(t, seat, LY_TOWN_X, lots[1] % 4, lots[1] / 4);
    p = (ly_point){ (p.x + q.x) * 0.5f, (p.z + q.z) * 0.5f };
  }
  return p;
}

void ly_town(const ly_table *t, int seat, const hb_player *pl, ly_point out[HB_BUILDINGS]) {
  int lots[HB_BUILDINGS + 1][2];
  town_fill(pl, -1, lots);
  for (int b = 0; b < HB_BUILDINGS; b++) out[b] = (ly_point){ 0, 0 };
  for (int i = 0; i < pl->nbld; i++) out[pl->bld[i]] = lot_centre(t, seat, lots[i]);
}

ly_point ly_next_lot(const ly_table *t, int seat, const hb_player *pl, int b) {
  int lots[HB_BUILDINGS + 1][2];
  town_fill(pl, b, lots);
  return lot_centre(t, seat, lots[HB_BUILDINGS]);
}

ly_point ly_bld_place(ly_point c, int b, int k) {
  int n = hb_bld[b].slots;
  return (ly_point){ c.x + ((float)k - (float)(n - 1) * 0.5f) * 0.42f, c.z + 0.36f };
}

ly_point ly_goods(const ly_table *t, int seat, int good) { return ly_on_board(t, seat, 4.75f + good * 1.05f, 1.45f); }
ly_point ly_coins(const ly_table *t, int seat) { return ly_on_board(t, seat, 10.25f, 1.45f); }
ly_point ly_dock(const ly_table *t, int seat, int k) {
  return ly_on_board(t, seat, 11.3f + (float)(k % 3) * 0.48f, 0.85f + (float)((k / 3) % 3) * 0.55f);
}
ly_point ly_plate(const ly_table *t, int seat) { return ly_on_board(t, seat, 0.35f, 0.3f); }

ly_point ly_role(int r) { return (ly_point){ ((float)r - 3.0f) * 2.65f, -4.55f }; }
ly_point ly_row(int i) { return (ly_point){ -7.2f + (float)i * 1.75f, -2.15f }; }
ly_point ly_stone_pile(void) { return (ly_point){ 1.7f, -2.15f }; }
ly_point ly_plot_pile(void) { return (ly_point){ 3.9f, -2.15f }; }

ly_point ly_market(int b) {
  static const int8_t ROW[HB_BUILDINGS] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4 };
  static const int8_t FIRST[LY_MARKET_ROWS] = { 0, 6, 10, 14, 18 };
  static const int8_t COUNT[LY_MARKET_ROWS] = { 6, 4, 4, 4, 5 };
  int r = ROW[b], i = b - FIRST[r];
  return (ly_point){ ((float)i - (float)(COUNT[r] - 1) * 0.5f) * 3.05f + (r == 4 ? 0.0f : 0.25f), -0.3f + (float)r * 2.1f };
}

ly_point ly_pass(void) { return (ly_point){ LY_PASS_X, LY_PASS_Z }; }

ly_point ly_ship(int nships, int k) {
  if (nships == 2) return (ly_point){ k == 0 ? -4.6f : 4.6f, LY_BOAT_Z };
  return (ly_point){ ((float)k - 1.0f) * 8.6f, LY_BOAT_Z };
}

ly_point ly_ship_hold(int nships, int k, int cap, int i) {
  ly_point c = ly_ship(nships, k);
  return (ly_point){ c.x + ((float)i - (float)(cap - 1) * 0.5f) * 0.92f, c.z + 0.05f };
}

ly_point ly_ferry(void) { return (ly_point){ -17.6f, LY_BOAT_Z }; }
ly_point ly_ferry_seat(int i) {
  ly_point c = ly_ferry();
  int k = i % 14;
  return (ly_point){ c.x - 2.6f + (float)(k % 7) * 0.62f + 0.6f, c.z - 0.33f + (float)(k / 7) * 0.66f };
}

ly_point ly_stall(int i) { return (ly_point){ 15.4f + (float)i * 1.22f, LY_BOAT_Z + 0.2f }; }
ly_point ly_lantern(int i) { return (ly_point){ -22.4f + (float)i * (44.8f / (LY_LANTERNS - 1)), -7.45f }; }
