/* First Hearth: the village table in 3D (board, kin, goods, huts, cards, dice) and the HUD over it. It reads the
 * game's table state and beats; it never decides anything. */
#ifndef FIRSTHEARTH_VIEW_H
#define FIRSTHEARTH_VIEW_H

#include "cartwheel/scene.h"
#include "game.h"

void view_init(cw_tree *t);
void view_start(const tg_game *g);
/* per rendered frame: the 3D table follows the game */
void view_update(const tg_game *g, float dt);
/* in a cw_r2d pass: the HUD (seats, prompts, the how-to-play card, the final scores) */
void view_hud(const tg_game *g, float dt);
/* in a cw_r2d pass: the how-to-play card on a canvas of size sz (close_prompt: the in-play "close" line; the title's
 * page leaves it out, the shell draws the way back) */
void view_help(cw_vec2 sz, bool close_prompt);
cw_node view_camera(void);
/* a table point on the canvas (the debug field firsthearth.target_px, where a test taps) */
cw_vec2 view_to_screen(t3_vec3 p);
/* debug figures */
int view_moving(void); /* pieces still gliding */


extern int view_stage, view_texcs;
#endif
