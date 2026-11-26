/* crokinole's view: the board, the table and the discs in 3D (render only: nothing here feeds the rules) */
#ifndef CRK_VIEW_H
#define CRK_VIEW_H
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "game.h"

/* the board frame (crk.h: x right, y away from seat 0, h up) to world (y up, seat 0 toward +z) */
static inline t3_vec3 bw(float x, float y, float h) { return (t3_vec3){ x, h, -y }; }

#define DISC_H 0.0095f
/* the colour of each side's discs (linear) */
extern const float SIDE_COL[3][3];

void view_init(cw_node root, cw_node camera);
void view_build(void);
/* every frame: the discs where the game has them, the camera behind the seat to move */
void view_sync(const game *g, float dt);
/* a board point (crk frame, height h) on the canvas */
bool view_to_canvas(float x, float y, float h, float *cx, float *cy);
/* the board point under a canvas point (on the surface plane) */
bool view_from_canvas(float cx, float cy, float *x, float *y);
float view_cam_angle(void);

#endif
