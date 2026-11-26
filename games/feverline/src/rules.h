/* Fever Line: the rules, pure and headless (no engine calls besides cw_rng / cw_hash).
 *
 * The rules in our words:
 *   A team of two to four field agents, each with a role, holds back four fevers spreading over a map of real
 *   cities (the world, or one of eight regional maps: 21 to 44 cities), split into four regions, one fever in each. Everyone wins or loses together, and every card is
 *   face up: the team plans in the open.
 *   Setup: every agent starts in the first clinic's city (the map's biggest). Nine cities catch the fever (three
 *   with three cubes, three with two, three with one; six on a map of fewer than 30 cities). Agents get city cards (4 each with two agents, 3 with three, 2 with four).
 *   The rest of the city cards are cut into as many piles as there are surge cards (3 Intro, 4 Normal, 5 Heroic),
 *   one surge goes into each pile, and the piles are stacked: a surge waits somewhere in every stretch of the deck.
 *   A turn: the agent takes 4 actions, then draws 2 cards, then the fever spreads.
 *     Actions (any mix, the same one more than once):
 *       drive to a city joined by a road; fly to a city by spending its card; charter a flight anywhere by
 *       spending the card of the city you are in; shuttle from a clinic to any other clinic;
 *       build a clinic where you stand by spending that city's card (at most 6 clinics);
 *       treat: take one cube of a fever off your city (every cube of it once that fever is cured);
 *       share: give the card of the city you are in to an agent standing with you, or take it from them (whoever
 *       goes over 7 cards discards at once);
 *       cure a fever at a clinic by spending 5 cards of its colour (4 on a map of 28 to 43 cities, 3 below that);
 *       or rest (end your actions early).
 *     Draw: two cards from the deck. A surge card: the spread rate goes up a step, the city at the bottom of the
 *     fever deck gets 3 cubes, and the fever discards are shuffled and laid back on top of the fever deck (the
 *     cities that just caught it come round again soon). Then the hand limit: 7 cards; an agent over it discards.
 *     A calm card (some maps hold one to three among the city cards): the next spread flips one card fewer.
 *     Spread: flip as many fever cards as the rate says (2, 2, 2, 3, 3, 3, 4) and give each city one cube of its
 *     fever. A city that would get a fourth cube of one fever breaks out instead: every city on its roads gets a
 *     cube of that fever, and a neighbour that already has three breaks out too (each city once per chain).
 *   A cured fever with no cubes left on the map is wiped out: its cards spread nothing any more.
 *   The team wins the moment all four fevers are cured. It loses at the 8th outbreak, when a fever needs a cube
 *   and none are left, or when a card must be drawn and the deck is empty.
 *   Roles: Field Medic (treat takes every cube of the fever; cured fevers vanish from wherever she stands and
 *   never land there), Analyst (a cure needs one card fewer), Courier (may give any city card, and others may take
 *   any city card from her, when standing together), Engineer (builds a clinic without spending a card),
 *   Warden (no cube is ever placed in her city or the cities on its roads).
 */
#ifndef FEVERLINE_RULES_H
#define FEVERLINE_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boardkit/bk_game.h"
#include "cartwheel/core.h"
#include "feverline_maps.h"

/* The map in use: one per match, set by fv_init (fv_use_map for a state made elsewhere). The rules read the map's
 * cities through these; a state carries its map, and fv_apply refuses a state of another map. */
extern int fv_map_active;
void fv_use_map(int map);
#define FV_CITIES (FV_MAPS[fv_map_active].city)
#define FV_N (FV_MAPS[fv_map_active].ncity)
#define FV_START_CITY (FV_MAPS[fv_map_active].start)

#ifdef __cplusplus
extern "C" {
#endif

#define FV_MAX_R 4
#define FV_NCOL 4
#define FV_MAX_SURGE 5
#define FV_MAX_CALM 4
#define FV_NCARDS (FV_NCITY + FV_MAX_SURGE + FV_MAX_CALM) /* city cards 0..47, then the surges, then the calm cards */
#define FV_SURGE0 FV_NCITY
#define FV_CALM0 (FV_SURGE0 + FV_MAX_SURGE)
static inline bool fv_is_surge(int card) { return card >= FV_SURGE0 && card < FV_CALM0; }
static inline bool fv_is_calm(int card) { return card >= FV_CALM0 && card < FV_NCARDS; }
#define FV_HAND_MAX 7
#define FV_CUBES 24       /* per fever */
#define FV_MAX_CLINICS 6
#define FV_MAX_OUTBREAKS 8
#define FV_ACTIONS 4
#define FV_NRATES 7
#define FV_NROLES 5
extern const int FV_RATES[FV_NRATES]; /* the spread rate by step: cards flipped a turn */

enum { FV_MEDIC, FV_ANALYST, FV_COURIER, FV_ENGINEER, FV_WARDEN };
enum { FV_PH_ACT, FV_PH_DRAW, FV_PH_SURGE, FV_PH_DISCARD, FV_PH_INFECT, FV_PH_OVER };
enum { FV_PLAYING, FV_WON, FV_LOST };
enum { FV_LOSE_NONE, FV_LOSE_OUTBREAKS, FV_LOSE_CUBES, FV_LOSE_DECK };
/* move kinds (field 0) */
enum {
  FV_M_DRIVE = 1, FV_M_FLY, FV_M_CHARTER, FV_M_SHUTTLE, FV_M_BUILD, FV_M_TREAT, FV_M_GIVE, FV_M_TAKE, FV_M_CURE,
  FV_M_REST, FV_M_DISCARD,
  FV_M_DRAW = 20, /* chance: a player card (field 1) */
  FV_M_FLIP,      /* chance: a fever card (field 1) */
};
/* where a player card is */
#define FV_PC_DISCARD 0xf0
#define FV_PC_OUT 0xff
#define FV_PC_DECK 0x10 /* + its pile (0 the top) */
/* the setups */
enum { FV_INTRO, FV_NORMAL, FV_HEROIC };

/* the last thing that happened, for the view (kind, a, b, c) */
enum { FV_EV_NONE, FV_EV_MOVE, FV_EV_BUILD, FV_EV_TREAT, FV_EV_SHARE, FV_EV_CURE, FV_EV_REST, FV_EV_DISCARD, FV_EV_DRAW,
       FV_EV_SURGE, FV_EV_INFECT, FV_EV_QUIET, FV_EV_CALM };

typedef struct fv_state {
  int16_t roles, setup, surges, phase, cur, acts, draws, infects;
  int16_t outbreaks, rate_idx, result, lose_reason, turns, moves_played, disc_role, clinics;
  int16_t disc_back, map, ncity, calm; /* calm: cards one spread fewer, from calm cards drawn */ /* a discard during actions (a share went over the limit): the phase to go back to, + 1 */
  int16_t role[FV_MAX_R], loc[FV_MAX_R];
  int16_t ev[4];        /* the last event: FV_EV_*, a, b, c */
  int16_t ob_n, ob_city[FV_NCITY]; /* the cities that broke out in the last step, in order */
  uint8_t cubes[FV_NCITY][FV_NCOL];
  uint8_t supply[FV_NCOL], cured[FV_NCOL]; /* cured: 0 no, 1 cured, 2 wiped out */
  uint8_t clinic[FV_NCITY];
  uint8_t pcard[FV_NCARDS]; /* a hand (0..3), FV_PC_DECK + pile, FV_PC_DISCARD, FV_PC_OUT */
  uint8_t icard[FV_NCITY];  /* 0 in the fever discards, k >= 1 in the fever deck's layer k (the highest on top) */
  uint8_t itop, pad[4]; /* (no compiler padding: every byte of the state is set) */
} fv_state;

/* a fresh game: roles dealt, nine cities infected, hands dealt, the deck cut and stacked (all face up) */
void fv_init(fv_state *s, int roles, int setup, int map, uint64_t seed);

/* bk_game callbacks */
int fv_moves(const void *state, bk_move *out, int cap, void *user);
void fv_apply(void *state, bk_move m, void *user);
int fv_turn(const void *state, void *user);
bool fv_terminal(const void *state, void *user);
void fv_evaluate(const void *state, float score[BK_MAX_SEATS], void *user);
int fv_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user);
void fv_chance(void *state, cw_rng *rng, void *user);
void fv_label(const void *state, bk_move m, char *buf, size_t cap, void *user);

/* The rules for the match (players = roles, turn = the agent to act). */
void fv_game(bk_game *g, int roles);
/* The team's planner for the CPUs: one player (the team), sensible moves only (no flights that spend a card the
 * team needs, no rests), a plan that covers the agent's actions this turn (or a discard) with the draws and the
 * spread after it scored by evaluate as risks (each city's chance of its fever card turning up, a surge bringing
 * the discards back), cures (each fever's cheapest plan: who collects, the cards to gather from teammates, the
 * walk to a clinic), cube counts and outbreaks, as a 0..1 value; a playout policy that plays like a careful agent. */
void fv_search_game(bk_game *g);
/* the CPU at a level (0 Easy, 1 Normal, 2 Hard): MCTS over the plan, on a node budget (6000, 20000, 60000); Easy
 * plays a lesser plan one decision in five */
void fv_cpu_options(int level, uint64_t seed, cw_search_options *o);
/* the team's value 0..1 (what the planner maximises) */
float fv_value(const fv_state *s);

/* queries for tests and the view */
int fv_hand(const fv_state *s, int r, int *cards, int cap); /* city cards in role r's hand, ascending */
int fv_hand_count(const fv_state *s, int r);
int fv_hand_colour(const fv_state *s, int r, int colour);
int fv_cure_need(const fv_state *s, int r);
int fv_cure_base(void); /* cards a cure needs on this map: 5 with 44 cities or more, 4 with 28, else 3 */
int fv_deck_left(const fv_state *s);
int fv_rate(const fv_state *s);
int fv_cubes_on_map(const fv_state *s, int colour);
int fv_clinics(const fv_state *s);
int fv_dist(int a, int b); /* roads between two cities */
bool fv_adjacent(int a, int b);
bool fv_protected(const fv_state *s, int city, int colour); /* a Warden or the Medic keeps cubes off it */
/* how the current agent would reach a city with one action, the cheapest way (a move code), BK_MOVE_NONE */
bk_move fv_move_to(const fv_state *s, int city);
uint64_t fv_hash(const fv_state *s);
const char *fv_role_name(int role);
const char *fv_role_power(int role);
int fv_surges_for(int setup);

static inline int fv_colour(int city) { return FV_CITIES[city].colour; }

#ifdef __cplusplus
}
#endif
#endif
