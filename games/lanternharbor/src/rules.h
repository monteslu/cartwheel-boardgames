/* Lantern Harbor (lanternharbor): the rules, pure and headless.
 *
 * A harbour town of free guild craftspeople. Each round the players in turn take one job from an open row of
 * jobs; a job nobody took gathers a coin for whoever takes it later. When a job is taken every player carries it
 * out, starting with the one who took it, and that one gets a small extra:
 *   Surveyor      claim a new plot for your hillside (the taker may open a stone pit instead)
 *   Ferryman      the ferry lands its townsfolk; everyone hands them out over plots and workshops (taker: one more)
 *   Mason         raise one building in your town (taker: one coin cheaper)
 *   Artisan       every staffed plot and workshop makes goods (taker: one extra good)
 *   Merchant      sell one good to the market stall (taker: one coin more)
 *   Skipper       load goods onto the cargo boats for lanterns, as long as anyone can (taker: one lantern more);
 *                 goods that cannot be stored spoil when the boats have sailed
 *   Diver         the taker alone dives for a pearl: a coin
 * Lanterns (the points) lie open on the table. The game ends after the round in which the townsfolk run out, the
 * lantern supply runs out, or someone fills the twelfth lot of their town. Lanterns plus building values plus the
 * bonuses of staffed grand buildings decide; a tie goes to more goods and coins together.
 *
 * Two players follow the two-player setup: three jobs each a round, fewer plots, goods and buildings, two boats.
 *
 * The state is flat and fixed size (bk_game rules). Randomness: the plots drawn for the open row and the seed
 * shed's extra plot, chance nodes (turn() == BK_CHANCE), drawn from unordered piles when they are drawn. */
#ifndef HARBOR_RULES_H
#define HARBOR_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "boardkit/bk_game.h"

/* goods, in order of value; a plot of each kind grows it (rice needs no workshop) */
enum { G_RICE, G_INDIGO, G_GLASS, G_TEA, G_SPICE, HB_GOODS };
#define HB_STONE 5      /* the stone pit plot kind */
#define HB_PLOT_KINDS 6
#define HB_NONE 0xff

/* buildings */
enum {
  /* workshops (they turn a plot's crop into goods) */
  B_DYE_SHED, B_KILN, B_DYE_WORKS, B_GLASSWORKS, B_TEA_LOFT, B_SPICE_MILL,
  /* small guild houses, by tier */
  B_STALL, B_SEED_SHED, B_STONEYARD, B_STOREHOUSE,
  B_HOSTEL, B_COUNTING_HOUSE, B_NIGHT_MARKET, B_WAREHOUSE,
  B_COOPERATIVE, B_ACADEMY, B_BEACON, B_JETTY,
  /* grand buildings (two lots each) */
  B_GUILDHALL, B_GARDENS, B_BELL_TOWER, B_HARBOUR_OFFICE, B_ASSEMBLY,
  HB_BUILDINGS
};

enum { R_SURVEYOR, R_FERRYMASTER, R_MASON, R_ARTISAN, R_MERCHANT, R_SKIPPER, R_DIVER, HB_ROLES };

enum {
  PH_PICK,      /* the next seat takes a job */
  PH_SETTLE,    /* the actor claims a plot */
  PH_ARRANGE,   /* the actor hands out townsfolk */
  PH_BUILD,     /* the actor raises a building */
  PH_EXTRA,     /* the artisan picks an extra good */
  PH_TRADE,     /* the actor sells a good */
  PH_SHIP,      /* the actor loads a boat */
  PH_OVER
};

enum { CH_NONE, CH_ROW, CH_SEED };

#define HB_ISLAND 12
#define HB_TOWN 12
#define HB_SHIPS 3
#define HB_MARKET 4
#define HB_ROW 5        /* most plots in the open row (players + 1) */
#define HB_MAX_ROUNDS 60 /* a safety cap, never reached in play */

/* moves: bk_move_pack(type, a, b) */
enum {
  M_PASS, M_ROLE, M_TAKE, M_STONE, M_SEED, M_REARRANGE, M_PLACE_PLOT, M_PLACE_BLD, M_BUILD, M_EXTRA, M_SELL, M_LOAD,
  M_JETTY
};

typedef struct hb_building_info {
  const char *name;
  uint8_t cost, value, slots, tier; /* tier 1..4: how many stone pits may help pay (4 = grand) */
  int8_t good;                      /* a workshop's good, else -1 */
  uint8_t lots;                     /* lots in the town: 2 for grand buildings */
} hb_building_info;
extern const hb_building_info hb_bld[HB_BUILDINGS];
extern const char *const hb_good_name[HB_GOODS];
extern const char *const hb_plot_name[HB_PLOT_KINDS];
extern const char *const hb_role_name[HB_ROLES];
extern const char *const hb_role_effect[HB_ROLES];
extern const char *const hb_bld_effect[HB_BUILDINGS];

typedef struct hb_player {
  uint8_t coins;
  uint8_t dock;                    /* townsfolk waiting on the quay */
  uint16_t lanterns;               /* lanterns won by shipping */
  uint8_t goods[HB_GOODS];
  uint8_t nplots;
  uint8_t plot[HB_ISLAND];         /* plot kinds in the order claimed */
  uint8_t plot_staffed[HB_ISLAND]; /* 1: a worker stands on it */
  uint8_t nbld;
  uint8_t bld[HB_TOWN];            /* buildings in the order raised */
  uint8_t staff[HB_BUILDINGS];     /* workers in each building */
  uint32_t built;                  /* bit b: building b raised */
} hb_player;

typedef struct hb_state {
  cw_rng rng;
  uint8_t players, governor, phase, chance;
  uint8_t role, picker, actor, acted;   /* the job being carried out, who took it, who acts now, actors done */
  uint8_t picks, round, end_triggered, ended_by_cap;
  uint8_t role_taken[HB_ROLES];
  uint8_t role_coins[HB_ROLES];
  uint8_t roles_in_play;               /* 6 (three seats) or 7 */
  uint8_t row[HB_ROW];                 /* the open row of plots (HB_NONE empty) */
  uint8_t plot_pile[HB_GOODS];         /* face down, unordered */
  uint8_t plot_discard[HB_GOODS];
  uint8_t stones;                      /* stone pits left */
  uint8_t folk_supply, ferry;          /* townsfolk still to come, and on the ferry now */
  int16_t lantern_supply;              /* may go below 0 in the last shipping */
  uint8_t goods_supply[HB_GOODS];
  uint8_t bld_supply[HB_BUILDINGS];
  uint8_t nships;
  uint8_t ship_cap[HB_SHIPS], ship_good[HB_SHIPS], ship_load[HB_SHIPS];
  uint8_t market[HB_MARKET], nmarket;
  /* this job's flags */
  uint8_t seed_used;                   /* the actor used the seed shed this turn */
  uint8_t arranged;                    /* the actor has placed a worker (no rearranging after) */
  uint8_t skipper_bonus;               /* the skipper's extra lantern was given */
  uint8_t jetty_used;                  /* bit per seat */
  uint8_t ship_done;                   /* bit per seat: cannot or will not load again */
  uint8_t pad0;
  uint16_t moves_played;
  uint16_t pad1;
  hb_player p[BK_MAX_SEATS];
} hb_state;

static inline bk_move hb_move(int type, int a, int b) { return bk_move_pack(type, a, b, 0, 0, 0, 0, 0); }
static inline int hb_move_type(bk_move m) { return bk_move_field(m, 0); }
static inline int hb_move_a(bk_move m) { return bk_move_field(m, 1); }
static inline int hb_move_b(bk_move m) { return bk_move_field(m, 2); }

void hb_init(hb_state *s, int players, uint64_t seed);
void hb_game(bk_game *g, int players);
int hb_moves(const hb_state *s, bk_move *out, int cap);
void hb_apply(hb_state *s, bk_move m);
int hb_turn(const hb_state *s);
bool hb_terminal(const hb_state *s);
void hb_chance(hb_state *s, cw_rng *rng);
void hb_evaluate(const hb_state *s, float score[BK_MAX_SEATS]);
uint64_t hb_hash(const hb_state *s);

/* helpers the view, the match and the tests read */
bool hb_owns(const hb_player *pl, int b);
bool hb_staffed(const hb_player *pl, int b); /* raised with at least one worker */
int hb_town_lots(const hb_player *pl);
int hb_staffed_stones(const hb_player *pl);
int hb_build_cost(const hb_state *s, int seat, int b);
int hb_empty_slots(const hb_player *pl);     /* plots and building places without a worker */
int hb_workers(const hb_player *pl);         /* every worker, the dock included */
int hb_sale_price(const hb_state *s, int seat, int good);
int hb_bonus(const hb_player *pl, int b);    /* a staffed grand building's end bonus now */
int hb_score(const hb_player *pl);           /* lanterns + building values + grand bonuses */
int hb_tiebreak(const hb_player *pl);        /* goods + coins */
unsigned hb_winners(const hb_state *s);
int hb_production(const hb_player *pl, int good); /* goods of a kind the player makes at the artisan */
int hb_load_amount(const hb_state *s, int seat, int good, int ship); /* 0: not a legal load */
/* every townsperson accounted for: supply, ferry, docks, workers */
int hb_folk_total(const hb_state *s);
/* every good: supply, players, boats, market */
int hb_goods_total(const hb_state *s, int good);
int hb_picks_per_round(const hb_state *s);
void hb_move_label(const hb_state *s, bk_move m, char *buf, int cap);
/* a rough worth of a move to the seat making it (move ordering and the playout policy) */
float hb_move_worth(const hb_state *s, bk_move m);

#endif
