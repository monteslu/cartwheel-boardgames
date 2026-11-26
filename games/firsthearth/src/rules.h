/* First Hearth: the rules, pure and headless (no engine calls besides cw_rng / cw_hash).
 *
 * The rules in our words:
 *   Each village starts with 5 kin, 12 food, an empty field and no tools. A round has three parts.
 *   1. Send kin out. Starting with the first player and going round, each player sends one or more of their
 *      kin to one place, until everyone has sent all their kin. Places:
 *        hunting grounds (food, worth 2 a pip, room for anyone), forest (wood, 3), clay bank (clay, 4),
 *        quarry (stone, 5), river (gold, 6): up to 7 kin at each of the last four, and a player goes to each
 *        place once a round (except the hunting grounds, where a player may send more kin to join their party);
 *        the hearth (exactly 2 kin: a new kin joins, up to 10), the garden (1 kin: field +1, up to 10),
 *        the tool bench (1 kin: +1 strength on your weakest tool, up to 3 tools of strength 4);
 *        four lore cards (1 kin each; the card in slot n costs n goods of any kind) and the top building
 *        of each building pile (1 kin; costs exactly the goods shown, scores their value).
 *      With fewer than 4 players a gathering site takes kin from at most players-1 players, and only two
 *      of hearth, garden and tool bench can be used in one round.
 *   2. Bring kin home. In turn order, each player resolves their places in a fixed order: the gathering
 *      sites (roll one die per kin there, add any unused tools you choose, gain total / worth of that good,
 *      while the supply lasts), then hearth, garden, tool bench, then each card and building they visited
 *      (buy it or let it go). Goods paid for cards go cheapest first.
 *   3. Feed the village. The field gives its level in food, then each kin eats 1 food. Short of food, a
 *      player feeds the rest with goods (cheapest first) or goes hungry: all food is lost and 10 points
 *      with it.
 *   Then the cheapest card still on offer goes to the fire, the other cards slide down to the cheap end
 *   and new cards fill the expensive slots, a new building comes up
 *   on each pile that lost its top, and the first player passes left. The game ends after the round in
 *   which a building pile runs out, or when the cards cannot be refilled (or at the latest after round 14).
 *   End scoring: 1 point per good left, each set of different lore marks scores its size squared, and
 *   every kin role card multiplies its number by the field level (planters), tool strength (knappers),
 *   buildings built (builders) or kin (elders). Most points wins; a tie goes to field + kin + tools.
 */
#ifndef FIRSTHEARTH_RULES_H
#define FIRSTHEARTH_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cartwheel/core.h"
#include "boardkit/bk_game.h"

#include "firsthearth_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TR_MAX_P 4
enum { TR_FOOD, TR_WOOD, TR_CLAY, TR_STONE, TR_GOLD, TR_NRES };
enum {
  TR_S_HUNT, TR_S_WOOD, TR_S_CLAY, TR_S_STONE, TR_S_GOLD, /* gathering sites, resource = site */
  TR_S_HUT, TR_S_FIELD, TR_S_TOOL,                         /* village */
  TR_S_CARD0, TR_S_CARD1, TR_S_CARD2, TR_S_CARD3,         /* card slots, cost 1..4 */
  TR_S_BLD0, TR_S_BLD1, TR_S_BLD2, TR_S_BLD3,             /* building piles */
  TR_NSITES
};
enum { TR_PH_DEAL, TR_PH_PLACE, TR_PH_RESOLVE, TR_PH_FEED, TR_PH_OVER };
enum { TR_END_NONE, TR_END_PILE, TR_END_CARDS, TR_END_MOONS };
enum { TR_NEED_NONE, TR_NEED_ROLL, TR_NEED_TOOLS, TR_NEED_BUY, TR_NEED_FEED };
/* move kinds (field 0) */
enum { TR_M_PLACE = 1, TR_M_TOOLS, TR_M_BUY, TR_M_FEED, TR_M_ROLL, TR_M_DRAW_CARD, TR_M_DRAW_BLD };

#define TR_START_KIN 5
#define TR_MAX_KIN 10
#define TR_MAX_FIELD 10
#define TR_SITE_CAP 7
#define TR_MAX_ROUNDS 14
#define TR_HUNGER 10
#define TR_PILE 7

typedef struct tr_player { /* all int16: no padding */
  int16_t kin, avail, field, ntools, tools[3], tool_used;
  int16_t res[TR_NRES];
  int16_t score, nbuild, hungry, ncards;
  int16_t marks[8]; /* lore cards per mark */
  int16_t roles[4]; /* role multiplier sums */
} tr_player;

typedef struct tr_state {
  cw_rng rng;
  int16_t players, phase, need, round, first, cur, step, pend;
  int16_t place[TR_NSITES][TR_MAX_P];
  int16_t card_slot[4];  /* card index or -1 */
  int16_t bld_top[4];    /* building index or -1 */
  int16_t bld_count[4];  /* tiles left in the pile including the top */
  int16_t supply[TR_NRES];
  int16_t card_left[TR_NCARDS];
  int16_t bld_left[TR_NBUILD];
  int16_t card_owner[TR_NCARDS]; /* the seat that took each card, -1 */
  int16_t bld_owner[TR_NBUILD];  /* the seat that built each building, -1 */
  int16_t last_event[4]; /* what the last resolution did, for the view: kind, a, b, c */
  int16_t moves_played, last_burn; /* the card burned at the last round end, or -1 */
  int16_t end_reason, pad;         /* TR_END_* once over */
  tr_player p[TR_MAX_P];
} tr_state;

/* a fresh game (memset 0 first); the deal is pending (chance) */
void tr_init(tr_state *s, int players, uint64_t seed);

/* bk_game callbacks */
int tr_moves(const void *state, bk_move *out, int cap, void *user);
void tr_apply(void *state, bk_move m, void *user);
int tr_turn(const void *state, void *user);
bool tr_terminal(const void *state, void *user);
void tr_evaluate(const void *state, float score[BK_MAX_SEATS], void *user);
void tr_chance(void *state, cw_rng *rng, void *user);
int tr_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user);
float tr_priority(const void *state, bk_move m, void *user);
bk_move tr_playout(const void *state, const bk_move *moves, int n, cw_rng *rng, void *user);
void tr_label(const void *state, bk_move m, char *buf, size_t cap, void *user);
/* the CPU at a level (0 Easy, 1 Normal, 2 Hard): MCTS with dice as chance outcomes */
void tr_cpu_options(int level, uint64_t seed, cw_search_options *o);
/* fills a bk_game for this player count */
void tr_game(bk_game *g, int players);

/* queries for tests and the view */
int tr_end_bonus(const tr_state *s, int seat, int parts[4]); /* goods, marks, roles; returns the total */
int tr_final_score(const tr_state *s, int seat);
int tr_tiebreak(const tr_state *s, int seat);
int tr_winner(const tr_state *s); /* lowest seat among the best on a full tie; -1 before the end */
int tr_tool_total(const tr_player *p);
bool tr_site_open(const tr_state *s, int seat, int site); /* could this seat send kin here now */
int tr_site_max(const tr_state *s, int seat, int site);   /* most kin it may send (0 = closed) */
bool tr_can_afford_card(const tr_player *p, int slot);
bool tr_can_afford_build(const tr_player *p, int b);
uint64_t tr_hash(const tr_state *s);
const char *tr_site_name(int site);

static inline int tr_value(int res) { return res == TR_FOOD ? 2 : res + 2; }

#ifdef __cplusplus
}
#endif
#endif
