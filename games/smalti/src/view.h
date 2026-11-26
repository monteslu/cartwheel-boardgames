/* Smalti: the table in 3D and the HUD over it. The view reads the match (game.h) and never changes it: tiles
 * follow the table as shown, each gliding in an arc to its new place and settling there. */
#ifndef SMALTI_VIEW_H
#define SMALTI_VIEW_H

#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "game.h"

void view_init(cw_tree *t);
/* a new match (the layout and seat count change) */
void view_start(const mg_game *g);
/* once per tick, after mg_tick: tiles move, popups age, sounds play */
void view_tick(const mg_game *g);
/* before the frame draws: interpolate between ticks */
void view_frame(const mg_game *g, float alpha);
/* the 2D layer over the table: plates, popups, prompts, the how-to card, the results */
void view_hud(const mg_game *g);
/* the title's How to play page: the how-to card without its in-play prompt (bk_title) */
void view_help_page(cw_vec2 canvas);
/* the title screens' backdrop */
void view_backdrop(cw_vec2 canvas);
/* the title screens' table: the glass set out as a finished mosaic panel (call each frame while no match runs) */
void view_attract(void);
/* sounds: the shell's effects bus */
void view_sound_init(void);

#endif
