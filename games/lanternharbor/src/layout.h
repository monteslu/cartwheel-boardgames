/* Lantern Harbor: where everything lies on the table (pure math, no engine calls).
 *
 * Table units, the x/z plane, y up; x right, z toward the bottom of the screen (the camera sits at +z looking down
 * at a tilt). The harbour water runs across the top with the quay and its lanterns below it; the boats moor in the
 * middle of the water, the ferry on the left and the market pier on the right. The middle column holds the row of
 * jobs, the open plots and the building market; the players' boards stand left and right of it, two a side at most.
 * art/make-art.mjs draws the boards and the market with the same numbers. */
#ifndef LANTERNHARBOR_LAYOUT_H
#define LANTERNHARBOR_LAYOUT_H

#include "rules.h"

#define LY_BOARD_W 13.0f
#define LY_BOARD_H 7.7f
#define LY_CELL 1.5f          /* a plot or a lot */
#define LY_GRID_Z 2.8f        /* the hillside and town grids start this far down a board */
#define LY_HILL_X 0.35f
#define LY_TOWN_X 6.65f
#define LY_SIDE 9.9f          /* boards start this far from the middle */
#define LY_QUAY_Z (-6.9f)     /* the quay's middle line */
#define LY_BOAT_Z (-10.2f)
#define LY_LANTERNS 24        /* lanterns along the quay */
#define LY_MARKET_ROWS 5
#define LY_PASS_X 7.9f
#define LY_PASS_Z (-2.15f)

typedef struct ly_point { float x, z; } ly_point;

typedef struct ly_table {
  int players;
  ly_point board[BK_MAX_SEATS]; /* each board's top-left corner */
  float min_x, max_x, min_z, max_z;
} ly_table;

void ly_table_make(ly_table *t, int players);
ly_point ly_on_board(const ly_table *t, int seat, float x, float z);
/* hillside plot i (claim order) of a seat */
ly_point ly_plot(const ly_table *t, int seat, int i);
/* the centre of each raised building of a seat (index by building id; grand buildings span two lots) */
void ly_town(const ly_table *t, int seat, const hb_player *pl, ly_point out[HB_BUILDINGS]);
/* the next free lot a new building of kind b would take */
ly_point ly_next_lot(const ly_table *t, int seat, const hb_player *pl, int b);
/* a worker place k of building b (centre of a place circle on its tile) */
ly_point ly_bld_place(ly_point centre, int b, int k);
ly_point ly_goods(const ly_table *t, int seat, int good); /* a seat's pile of a good */
ly_point ly_coins(const ly_table *t, int seat);
ly_point ly_dock(const ly_table *t, int seat, int k);       /* the quay corner where waiting folk stand */
ly_point ly_plate(const ly_table *t, int seat);             /* the name plate's top-left */
/* the middle column */
ly_point ly_role(int r);
ly_point ly_row(int i);       /* the open plot row */
ly_point ly_stone_pile(void);
ly_point ly_plot_pile(void);
ly_point ly_market(int b);    /* a building's place in the market */
ly_point ly_pass(void);
/* the harbour */
ly_point ly_ship(int nships, int k);              /* a boat's centre */
ly_point ly_ship_hold(int nships, int k, int cap, int i); /* hold i of boat k */
ly_point ly_ferry(void);
ly_point ly_ferry_seat(int i);
ly_point ly_stall(int i);     /* the market stall's four places */
ly_point ly_lantern(int i);   /* along the quay */

#endif
