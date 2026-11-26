/* Smokestack rules: a railway game of building track and hauling goods across a hex valley, for two to four
 * players (people or CPUs). Pure: a bk_game (the engine's cw_search_game), no engine calls but cw_rng / cw_hash.
 *
 * The valley (map_data.h, made by tools/gen-map.mjs) has cities, each wanting goods of its own colour, and
 * towns, which want nothing until someone charters them into a city. Goods cubes sit in cities. Every round:
 *
 *   1. Bid for turn order: an open auction. In order each player raises the bid (by 1, 2 or 5) or drops out;
 *      the first to drop out takes the last place, the next the place above it, and the last one bidding goes
 *      first and pays the bid. Nobody else pays.
 *   2. Choose a job, in the new order, one each from a shared row of six:
 *        Track Gang (build four tiles instead of three), Charter (turn a town into a city of a colour you pick;
 *        it gets two goods), New Engine (your engine grows a car at once), Freight Office (draw two goods and
 *        put each in a city you choose), Lead Builder (build first), Lead Shipper (haul first).
 *   3. Build, in turn order (the Lead Builder first): lay up to three tiles. A link is a run of tiles from a
 *      settlement to another settlement; a tile costs 2 on open ground, 3 in forest or across a river, 4 in
 *      the hills, and 1 more where it ends in a town. A link finished belongs to its builder. A link not
 *      finished this turn must be carried on next turn or it is torn up. Holding less than 8, you may issue a
 *      share: 5 at once, then 1 a round and 1 point at the end. Nobody holds more shares than their finished
 *      links plus two (investors back a railway they can see).
 *   4. Haul, twice round the table in turn order (the Lead Shipper first): move one good, grow your engine
 *      by a car, or pass. A good travels over finished links, at most as many links as your engine has
 *      cars, through settlements that do not want it, to the first city of its own colour. Each link it
 *      used pays its owner 1: other owners score it as points, and you take your own as points or as income.
 *   5. Income: money plus income plus 2 (every railway's mail contract), less 1 for each share. A player who
 *      cannot pay loses
 *      the shortfall in points.
 *   6. New goods: players + 2 goods drawn from the bag, each into a city with room (at random).
 * After the last round (8 with two players, 7 with three, 6 with four) each finished link is worth 2 points,
 * every 4 money left is worth 1 and each share costs 1; the most points wins (then the most money).
 *
 * Nothing is hidden: money, income, points, shares and engines are on the table; the bag's counts are known.
 * The state is flat and memcpy-able; every draw is a chance node listed with weights (outcomes). */
#ifndef RL_RULES_H
#define RL_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "boardkit/bk_game.h"

#define RL_MAX_HEX 96
#define RL_MAX_SET 16
#define RL_MAX_LINK 64
#define RL_COLORS 5
#define RL_CITY_CUBES 4
#define RL_ACTIONS 6
#define RL_MAX_PLAYERS 4
#define RL_MAPS 2
#define RL_MAX_ENGINE 6
#define RL_MAX_SHARES 15
#define RL_SHARE_BELOW 8 /* a share may be issued only while holding less than this */
#define RL_SHARES_FREE 2 /* shares a player may hold beyond one per finished link of theirs */
#define RL_LINK_POINTS 2 /* each finished link at the end */
#define RL_BASE_INCOME 2 /* every railway's money each round on top of its income (no seat is ever stranded) */
#define RL_MONEY_PER_POINT 4 /* money left at the end: a point for each this much */
#define RL_MAX_ROUTES 16
#define RL_ROUTE_LINKS RL_MAX_ENGINE
#define RL_PASSES 2

enum { RL_T_PLAIN, RL_T_FOREST, RL_T_RIVER, RL_T_HILLS, RL_T_LAKE, RL_T_PEAK };
enum { RL_CITY, RL_TOWN };
enum { RL_A_GANG, RL_A_CHARTER, RL_A_ENGINE, RL_A_FREIGHT, RL_A_LEAD_BUILD, RL_A_LEAD_SHIP };

/* phases: who decides what (RL_PH_PRODUCE and the fills are chance) */
enum {
  RL_PH_BID,      /* the auction: raise or drop out */
  RL_PH_ACTION,   /* choose a job */
  RL_PH_FREIGHT,  /* Freight Office: put the drawn goods in cities */
  RL_PH_BUILD,    /* start a link, carry one on, charter, issue a share, or end the turn */
  RL_PH_EXIT,     /* the tile just placed: which way it leaves the hex */
  RL_PH_TOWN,     /* Charter: which town */
  RL_PH_COLOR,    /* Charter: which colour */
  RL_PH_HAUL,     /* pick a good, grow the engine, or pass */
  RL_PH_ROUTE,    /* where the picked good goes */
  RL_PH_PAYOUT,   /* your own links as income or as points */
  RL_PH_PRODUCE,  /* new goods (chance) */
  RL_PH_OVER,
};

/* moves: field 0 the kind, then its arguments (bk_move_pack) */
enum {
  RL_MV_PASS = 1,  /* drop out of the auction, pass a haul, end building */
  RL_MV_RAISE,     /* f1: by 1, 2 or 5 */
  RL_MV_ACTION,    /* f1: the job */
  RL_MV_PLACE,     /* f1: the city a Freight Office good goes to */
  RL_MV_START,     /* f1: the hex, f2: the edge the track comes in by (toward the settlement) */
  RL_MV_EXIT,      /* f1: the edge the tile leaves by */
  RL_MV_SHARE,     /* issue a share */
  RL_MV_CHARTER,   /* use the Charter now */
  RL_MV_TOWN,      /* f1: the settlement */
  RL_MV_COLOR,     /* f1: the colour */
  RL_MV_GOOD,      /* f1: the city, f2: the slot */
  RL_MV_ENGINE,    /* grow the engine */
  RL_MV_DEST,      /* f1: the destination city (the best route there) */
  RL_MV_INCOME,
  RL_MV_POINTS,
  RL_MV_CH_FILL,   /* chance: f1 the colour into the city waiting for a good */
  RL_MV_CH_DRAW,   /* chance: f1 the colour drawn for the Freight Office */
  RL_MV_CH_PROD,   /* chance: f1 the city, f2 the colour */
};
static inline bk_move rl_mv(int kind, int a, int b) { return bk_move_pack(kind, a, b, 0, 0, 0, 0, 0); }
static inline int rl_mv_kind(bk_move m) { return bk_move_field(m, 0); }
static inline int rl_mv_a(bk_move m) { return bk_move_field(m, 1); }
static inline int rl_mv_b(bk_move m) { return bk_move_field(m, 2); }

/* a map (map_data.h) */
typedef struct rl_map_def {
  const char *name;
  int nhex, cols, rows, nset;
  int8_t q[RL_MAX_HEX], r[RL_MAX_HEX];
  uint8_t terrain[RL_MAX_HEX];
  int8_t river_in[RL_MAX_HEX], river_out[RL_MAX_HEX]; /* the river's way through a hex (-1 none) */
  int8_t hex_set[RL_MAX_HEX];                        /* the settlement on a hex, -1 none */
  int8_t nb[RL_MAX_HEX][6];                          /* neighbour in each cw_hex direction, -1 off the map */
  uint8_t set_hex[RL_MAX_SET], set_kind[RL_MAX_SET], set_color[RL_MAX_SET];
  const char *set_name[RL_MAX_SET];
} rl_map_def;
const rl_map_def *rl_map(int map);

typedef struct rl_link {
  int8_t owner;       /* -1: torn up / free slot */
  int8_t from, to;    /* settlements; to -1 while unfinished */
  int8_t first, last; /* hexes of its first and last tile */
  int8_t from_dir;    /* the settlement's edge it leaves by */
  int8_t tiles;
  int8_t done;
} rl_link;

typedef struct rl_player {
  int16_t money, income, points, shares;
  int8_t engine, action, open_link, extended; /* open_link: the unfinished link (-1); extended: built on it this turn */
  int8_t built, limit, charter_used, pad;
} rl_player;

typedef struct rl_route {
  int8_t dest, n, own, pad;
  int8_t link[RL_ROUTE_LINKS];
} rl_route;

typedef struct rl_state {
  int32_t map, players, round, rounds, phase, moves;
  rl_player p[RL_MAX_PLAYERS];
  int8_t order[RL_MAX_PLAYERS];     /* turn order: seats, first to last */
  int8_t seq[RL_MAX_PLAYERS];       /* this phase's order (build or haul: the lead seat moved first) */
  int32_t at;                       /* index into seq / order of the seat to move */
  int32_t pass_no;                  /* haul: which time round (0, 1) */
  /* the auction */
  int32_t bid, bid_leader, bid_out_n;
  int8_t bid_out[RL_MAX_PLAYERS];   /* dropped out */
  int8_t next_order[RL_MAX_PLAYERS];
  /* jobs */
  int8_t action_owner[RL_ACTIONS];  /* -1 free */
  /* the board */
  int8_t tile_link[RL_MAX_HEX];     /* -1 no tile */
  int8_t tile_in[RL_MAX_HEX], tile_out[RL_MAX_HEX]; /* edges (directions) the track enters and leaves by */
  rl_link link[RL_MAX_LINK];
  int32_t nlink;
  uint8_t set_kind[RL_MAX_SET], set_color[RL_MAX_SET];
  uint8_t cube[RL_MAX_SET][RL_CITY_CUBES]; /* colour + 1, 0 empty (packed from slot 0) */
  uint8_t bag[RL_COLORS];
  int8_t new_city_colors;           /* charter colours left (bit mask) */
  /* pending */
  int8_t fill_city[24];             /* cities waiting for a good each (a queue) */
  int32_t fill_n, draw_n, prod_n;   /* goods to fill / draw for the Freight Office / produce */
  uint8_t drawn[2];                 /* Freight Office goods drawn, waiting to be placed (colour + 1) */
  int32_t drawn_n;
  int32_t build_hex;                /* RL_PH_EXIT: the tile being placed */
  int32_t charter_town;
  int32_t good_city, good_slot;     /* the good being hauled */
  rl_route route;                   /* the route it took (RL_PH_PAYOUT) */
  /* the last thing that happened (for the view) */
  int32_t last_kind, last_seat, last_a, last_b, last_gain;
} rl_state;

extern const bk_game RL_RULES[RL_MAX_PLAYERS + 1]; /* by player count (2..4) */
/* what the CPUs search: the same rules, less shares their income cannot carry (rules.c moves_cpu) */
/* a seat's money each round: income + RL_BASE_INCOME - shares */
static inline int rl_net_income(const rl_player *p) { return p->income + RL_BASE_INCOME - p->shares; }
extern const bk_game RL_CPU_RULES[RL_MAX_PLAYERS + 1];
void rl_setup(rl_state *s, int players);
int rl_rounds_for(int players);
/* placing: 0..players-1 best first (ties: money, then turn order); returns players. Valid at any time. */
int rl_standing(const rl_state *s, int8_t *out);
/* the final score of a seat (points + 2 a finished link + 1 per 4 money - shares) */
int rl_final_score(const rl_state *s, int seat);
/* how many more shares the seat may issue now (its finished links + RL_SHARES_FREE, less those it holds) */
int rl_share_room(const rl_state *s, int seat);
int rl_winner(const rl_state *s); /* -1 not over */
int rl_turn(const rl_state *s);
/* the seat to move in the current decision phase */
int rl_current(const rl_state *s);
/* a tile's cost on a hex (terrain), not counting a town at its end */
int rl_tile_cost(const rl_state *s, int hex);
/* the best route for a good to each city it can reach: count (<= cap) */
int rl_routes(const rl_state *s, int city, int slot, int seat, rl_route *out, int cap);
/* the hexes of a link from its start, in order: count */
int rl_link_hexes(const rl_state *s, int link, int8_t *out, int cap);
int rl_cubes(const rl_state *s, int set);
int rl_links_done(const rl_state *s, int seat);
void rl_move_label(const void *state, bk_move m, char *buf, size_t cap, void *user);
extern const char *const RL_ACTION_NAMES[RL_ACTIONS];
extern const char *const RL_ACTION_TEXT[RL_ACTIONS];
extern const char *const RL_COLOR_NAMES[RL_COLORS];
/* a heuristic value of a move for the mover (playouts and move ordering) */
float rl_move_value(const rl_state *s, bk_move m);
/* the CPU levels: MCTS (cw_search) at the shell level 0..2, as the game and the bench run it */
void rl_cpu_options(int level, cw_search_options *o);
/* the hex the seat's next tile goes on (the tile being placed, or the next of its unfinished link; -1 none), its
 * entry edge and the settlement the link starts from */
int rl_next_tile(const rl_state *s, int seat, int *entry, int *from);

#endif
