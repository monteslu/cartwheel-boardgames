/* Smokestack view: the 3D valley on the table, track tiles, goods, trains, option plaques and job cards, the
 * HUD. It reads the rules' state and the match's haul events, and never decides anything. */
#ifndef RL_VIEW_H
#define RL_VIEW_H

#include "game.h"

typedef struct rl_view_stats {
  int32_t tiles_shown, tiles_landed, trains_run, trains_arrived, ghost_shown, route_shown, glow_peaks;
} rl_view_stats;

void rl_view_init(cw_tree *tree, cw_node world, cw_node cam, rl_game *g);
void rl_view_start(void);                 /* a new match: the map, the board cleared */
void rl_view_update(float dt);            /* per frame, after the ticks */
void rl_view_hud(float dt, bool help, const char *const *seat_names); /* inside a cw_r2d pass */
void rl_view_help_page(cw_vec2 canvas);  /* the how-to-play card as the title page (no close prompt), in a cw_r2d pass */
const rl_view_stats *rl_view_stats_get(void);

#endif
