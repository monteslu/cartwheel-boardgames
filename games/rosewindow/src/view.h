/* Rose Window: the table in 3D and the HUD over it. The view reads the match (never changes it): dice follow the
 * state with throws and glides, windows light up as dice are set, the HUD shows the choices, the prompts, the
 * scores and the cards. */
#ifndef ROSEWINDOW_VIEW_H
#define ROSEWINDOW_VIEW_H

#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "game.h"

void view_init(cw_tree *t);
/* a new match on the table */
void view_start(const mg_game *g);
/* per rendered frame: follow the match, animate (dt seconds) */
void view_frame(const mg_game *g, float dt);
/* the HUD, inside a cw_r2d pass */
void view_hud(const mg_game *g, float dt);
/* the title screens' table: a finished window lit up */
void view_attract(float dt);
/* debug: dice in flight, a ghost shown, lit panes */
int view_dice_moving(void);
int view_ghosts_shown(void);
int view_lit_panes(void);
/* the canvas point of a choice (tests tap there) */
cw_vec2 view_project(t3_vec3 p);

/* the How to play card as the title's page (no prompt line), inside the shell's 2D pass */
void view_help_page(cw_vec2 canvas);

#endif
