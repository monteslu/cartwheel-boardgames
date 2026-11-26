/* Fever Line: the map table at night in 3D (the board, glowing fever cubes, pawns, clinics, the decks and the two
 * tracks) and the HUD over it. It reads the game's table state and beats; it never decides anything. */
#ifndef FEVERLINE_VIEW_H
#define FEVERLINE_VIEW_H

#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "game.h"

void view_init(cw_tree *t);
void view_start(const tg_game *g);
/* per rendered frame: the 3D table follows the game */
void view_update(const tg_game *g, float dt);
/* in a cw_r2d pass: the HUD (tracks, agents and their open hands, the choice, the how-to-play card, the end) */
void view_hud(const tg_game *g, float dt);
/* in a cw_r2d pass: the how-to-play card on a canvas of size sz; g (NULL on the title) adds the team's powers;
 * close_prompt: the in-play "close" line (the title's page leaves it out, the shell draws the way back) */
void view_help(const tg_game *g, cw_vec2 sz, bool close_prompt);
cw_vec2 view_to_screen(t3_vec3 p);
int view_moving(void); /* pieces still gliding */

#endif
