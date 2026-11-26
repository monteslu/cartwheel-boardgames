/* crokinole's rules as the kit's bk_game (the engine's cw_search_game): crk.h's state, one move a shot.
 *
 * A shot is a place, an aim, a power and a spin: millions of codes, too many to list. So moves() lists the candidate
 * shots a CPU's search tries (crk_cpu.h, aimed at the discs and spots that matter, at the level in the user data), and
 * the match takes any shot crk_rules_legal accepts (bk_match_set_legal).
 * apply() runs the shot to rest (crk_apply); the view replays the same ticks from the state before it. */
#ifndef CRK_RULES_H
#define CRK_RULES_H

#include "boardkit/bk_game.h"
#include "crk.h"
#include "crk_cpu.h"

typedef struct crk_rules_user { int level; /* 0..2 a CPU's candidates; -1 the match (Normal's) */ } crk_rules_user;

/* the rules with 2..4 seats */
bk_game crk_rules(crk_rules_user *u, int players);
/* any shot the seat to move may play: every field in range and the disc moves */
bool crk_rules_legal(const void *state, bk_move move, void *user);

#endif
