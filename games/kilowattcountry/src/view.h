/* Kilowatt Country: the table in 3D (the night map, its cities and houses, plant cards, the fuel market, each
 * company's tray) and the HUD over it. The view follows the match's shown state and its beats. */
#ifndef KILOWATTCOUNTRY_VIEW_H
#define KILOWATTCOUNTRY_VIEW_H

#include "cartwheel/scene.h"
#include "game.h"

extern int view_stage;
void view_init(cw_tree *t);
void view_start(const kg_game *g);
void view_update(const kg_game *g, float dt);
void view_hud(const kg_game *g, float dt);
/* the how-to-play card in canvas units, in a 2D pass: over play (with its close prompt) and as the title's page (without) */
void view_help(cw_vec2 canvas, bool close_prompt);
int view_moving(void);
int view_lit(void); /* houses glowing now (a debug field) */
/* city names that run into another name or sit on their own city, and names over another city, a cost tag or a
 * link: the worst over every map a match can deal (debug fields) */
int view_label_overlaps(void);
int view_label_maps(void); /* maps the check has covered so far (it places one map's names a frame, KC_NMAPS in all) */
int view_label_link_hits(void);
/* city names that mislead: a name over the middle of another city's plinth, or a leader (drawn from a name that had to
 * stand off from its city) over another city or name; the worst over every map a match can deal (a debug field) */
int view_label_strays(void);
/* each city name's rect on this map, then each city's footprint, canvas units (x, y, w, h; a name of w 0 is not
 * in play): a debug field of 8 * KC_NCITY */
void view_label_rects(int32_t *out);
cw_node view_camera(void);

#endif
