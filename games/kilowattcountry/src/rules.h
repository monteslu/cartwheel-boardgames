/* Kilowatt Country: the rules, pure and headless (no engine calls besides cw_rng / cw_hash).
 *
 * The rules in our words:
 *   Each company starts with 50 money and no plants. Money is open: everyone sees everyone's cash. A round has
 *   five parts.
 *   1. Order. The company with the most cities goes first; a tie goes to the one with the biggest plant. (The first
 *      round's order is drawn at random, and set again after its auction.)
 *   2. Plants. Eight plants lie in the market, sorted by number: the four lowest can be bought now, the four
 *      highest come later. In order, each company that has not bought a plant this round either puts one of the
 *      four up for auction (the opening bid is its number) or passes (and buys nothing this round; in the first
 *      round nobody may pass). Going round from the opener, every company still in the auction raises or drops
 *      out; the last one in pays its bid. If the opener did not win, it opens again. A company holds at most three
 *      plants: a fourth replaces one of the three. Each plant bought is replaced from the deck, and the market is
 *      sorted again. A round in which nobody buys loses the market's lowest plant.
 *   3. Fuel. Last in order first, each company buys coal, oil, garbage or uranium from the fuel market. Each fuel
 *      fills its track from the expensive end, so the cheapest cube left sets the price and prices climb as a fuel
 *      runs short. A plant stores up to twice what it burns in a round (a plant that burns coal or oil stores
 *      either).
 *   4. Build. Last in order first, each company builds houses in cities. The first house costs only the house.
 *      Every later one also pays the cheapest route from any of its cities along the links. In step 1 a city has
 *      room for one company, in step 2 for two, in step 3 for three; the house costs 10, 15 or 20 by the room it
 *      takes. A market plant whose number is at or below anyone's city count leaves the game.
 *   5. Power. Every company runs its plants on its fuel and powers as many of its cities as it can, earning money
 *      by the income table. The fuel market refills by the supply table, and the market's highest plant goes under
 *      the deck (in step 3 its lowest leaves the game) to make room for a new one.
 *   Step 2 starts when a company has 6 cities (5 with 3 or 4 players): the lowest plant leaves the game. Step 3
 *   starts when the deck runs out: the lowest plant leaves the game, the plants under the deck become the deck, and
 *   the market holds six plants that can all be bought. The game ends after the round in which a company reaches
 *   12 cities (11 with 3 players, 10 with 4), or after round 20: the company that powered the most cities that
 *   last round wins, then the most money, then the most cities.
 *   Once the deck and the plants under it are spent, the market keeps what it has: no plant leaves it unless a new
 *   one can take its place.
 *   Fewer players play on fewer regions of the map (3 regions for 2, 4 for 3, all 5 for 4), always joined, and
 *   a few plants are set aside face up at the start (4 with 2, 3 with 3, 2 with 4).
 *   The map is a real country or area (kc_maps): thirty real cities in five regions of six, each link costing by the
 *   real distance between its cities.
 */
#ifndef KILOWATTCOUNTRY_RULES_H
#define KILOWATTCOUNTRY_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boardkit/bk_game.h"
#include "cartwheel/core.h"
#include "kc_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The map in use: one per match, set by kc_init (kc_use_map for a state made elsewhere). The rules and the view read
 * the map's cities and links through these names. */
extern int kc_map_active;
void kc_use_map(int map);
#define kc_map (&kc_maps[kc_map_active])
#define kc_cities (kc_maps[kc_map_active].cities)
#define kc_links (kc_maps[kc_map_active].links)
#define KC_NLINK (kc_maps[kc_map_active].nlink)

#define KC_MAX_P 4
#define KC_MARKET 8
#define KC_NRAISE 4
enum { KC_PH_AUCTION, KC_PH_DISCARD, KC_PH_FUEL, KC_PH_BUILD, KC_PH_OVER };
/* move kinds (field 0) */
enum { KC_M_OPEN = 1, KC_M_PASS, KC_M_RAISE, KC_M_DROP, KC_M_DISCARD, KC_M_FUEL, KC_M_DONE, KC_M_BUILD, KC_M_DRAW };
#define KC_DRAW_STEP3 255
/* where a plant is */
enum { KC_L_OUT, KC_L_DECK, KC_L_UNDER, KC_L_MARKET, KC_L_OWNED, KC_L_ASIDE };

static const int8_t KC_RAISES[KC_NRAISE] = { 1, 2, 5, 10 };

typedef struct kc_player { /* int16 only: no padding */
  int16_t money, ncity, plants[3], fuel[KC_NFUEL];
  int16_t powered, earned; /* the last power phase */
  int16_t spent;           /* money spent this round (plants, fuel, houses), for the view */
} kc_player;

typedef struct kc_state {
  int16_t players, phase, step, round, cur, oi, regions, ending, map;
  int16_t order[KC_MAX_P];
  int16_t auc_plant, auc_bid, auc_high, auc_in, auc_opener, bought, passed, disc_new;
  int16_t nmarket, step3_drawn, moves_played, last_draw;
  int16_t market[KC_MARKET];
  int16_t fuel_mkt[KC_NFUEL];
  int8_t loc[KC_NPLANT];
  int8_t occ[KC_NCITY][3]; /* who has a house in each room of a city, -1 none */
  kc_player p[KC_MAX_P];
} kc_state;

/* a fresh game on a map (kc_maps index; it becomes the map in use): regions, set-aside plants and the first order
 * drawn from seed (all open on the table) */
void kc_init(kc_state *s, int players, int map, uint64_t seed);

/* bk_game callbacks */
int kc_moves(const void *state, bk_move *out, int cap, void *user);
void kc_apply(void *state, bk_move m, void *user);
int kc_turn(const void *state, void *user);
bool kc_terminal(const void *state, void *user);
void kc_evaluate(const void *state, float score[BK_MAX_SEATS], void *user);
void kc_chance(void *state, cw_rng *rng, void *user);
int kc_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user);
float kc_priority(const void *state, bk_move m, void *user);
bk_move kc_playout(const void *state, const bk_move *moves, int n, cw_rng *rng, void *user);
void kc_label(const void *state, bk_move m, char *buf, size_t cap, void *user);
/* the CPU at a level (0 Easy, 1 Normal, 2 Hard): MCTS with deck draws as chance outcomes */
void kc_cpu_options(int level, uint64_t seed, cw_search_options *o);
/* fills a bk_game for this player count */
void kc_game(bk_game *g, int players);

/* queries for tests and the view */
int kc_winner(const kc_state *s); /* -1 before the end */
int kc_market_open(const kc_state *s); /* how many market plants can be bought now (4, or all in step 3) */
bool kc_region_in(const kc_state *s, int region);
int kc_nplants(const kc_player *p);
int kc_capacity(const kc_player *p);                 /* cities its plants could power with fuel to spare */
int kc_can_power(const kc_player *p);                /* cities it would power now with the fuel it holds */
int kc_store_room(const kc_player *p, int fuel);     /* cubes of this fuel it could still store */
int kc_fuel_cost(const kc_state *s, int fuel, int k); /* the k cheapest cubes in the market; -1 if fewer */
int kc_fuel_price(const kc_state *s, int fuel);      /* the next cube's price, 0 when none is left */
int kc_build_cost(const kc_state *s, int seat, int city, int *route); /* -1 when it cannot build there */
int kc_route_cost(const kc_state *s, int seat, int city); /* the link part alone (0 for a first city) */
int kc_house_slot(const kc_state *s, int city);      /* rooms taken */
bool kc_has_house(const kc_state *s, int seat, int city);
int kc_fuel_needed(const kc_state *s, int seat, int fuel); /* cubes short of running every plant once */
int kc_bank(const kc_state *s, int fuel);            /* cubes in no market and no plant */
int kc_deck_count(const kc_state *s);
int kc_end_cities(const kc_state *s);
int kc_step2_cities(const kc_state *s);
int kc_plant_worth(const kc_state *s, int seat, int plant); /* the most a CPU would pay for it */
uint64_t kc_hash(const kc_state *s);
const char *kc_fuel_name(int fuel);
const char *kc_kind_name(int kind);
/* the power plan: which plants run (bit mask over plants[]) and how many cities they light */
int kc_power_plan(const kc_player *p, int *mask, int use[KC_NFUEL]);

#ifdef __cplusplus
}
#endif
#endif
