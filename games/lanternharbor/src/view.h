/* Lantern Harbor: the harbour in 3D and the HUD over it. The view reads the match (game.h) and never changes it:
 * pieces follow the table as shown, each gliding in an arc to its new place and settling there; boats sail out
 * full and come back empty, the ferry brings the townsfolk, and the quay's lanterns light up as goods ship. */
#ifndef LANTERNHARBOR_VIEW_H
#define LANTERNHARBOR_VIEW_H

#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "game.h"

void view_init(cw_tree *t);
void view_start(const mg_game *g);
void view_tick(const mg_game *g);
void view_frame(const mg_game *g, float alpha);
void view_hud(const mg_game *g);
/* the title screens' harbour (each frame while no match runs) */
void view_attract(void);
/* the How to play title page (in the shell's 2D pass, canvas units) */
void view_help_page(cw_vec2 canvas);
/* how many quay lanterns are lit (tests) */
int view_lanterns_lit(void);

#endif
