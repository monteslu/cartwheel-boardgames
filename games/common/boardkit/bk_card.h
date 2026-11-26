/* boardkit: cards on the table. A card is a thin rounded slab: a front, a back and an edge. Each face is
 * drawn ONCE into a 2D render target by the game's painter (plain cw_r2d calls), worn in 3D through
 * cw_target_texture3d and shared by every card that shows it; the slab is a procedural mesh on a MeshInstance3D, so cards pick, cast
 * shadows and move like any node. Flips and fans are pose helpers the game eases between.
 *
 *   bk_cards_init(tree, paint, &G);                   // once, in init
 *   cw_node c = bk_card_new(world, "Card7", &(bk_card_desc){ .front = 7, .back = BK_FACE_BACK });
 *   ...
 *   static void paint(int face, cw_vec2 px, void *user) { cw_r2d_rect(...); cw_r2d_text(...); }
 *
 * A face id is the game's (a card's kind); BK_FACE_BACK and up are the game's backs. The painter draws in
 * face pixels, origin top left, (0,0)..px, and must draw the same for the same id every time: it runs once
 * per face (and again only after bk_card_face_refresh), in the frame's update (a node of the kit's, running on every
 * screen), never inside a 2D pass, so a face first shown in render is painted at the next frame's update. */
#ifndef BK_CARD_H
#define BK_CARD_H

#include <stdbool.h>

#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_FACE_BACK 0x4000 /* ids from here up are backs, by convention */
#define BK_CARD_FACES_MAX 256

typedef void (*bk_card_paint_fn)(int face, cw_vec2 px, void *user);

typedef struct bk_card_desc {
  float width, height;   /* metres (0.63 x 0.88: a playing card at board scale, one unit = 10 cm) */
  float thickness;       /* (0.012) */
  float corner;          /* corner radius (0.06) */
  int front, back;       /* face ids (back < 0: plain `edge` colour) */
  cw_color edge;         /* the card stock's edge (alpha 0: warm white) */
  int face_px;           /* face texture height in px (256); width by aspect */
} bk_card_desc;

/* Once, in init: the painter for every face. Registers the face node type. */
void bk_cards_init(cw_tree *tree, bk_card_paint_fn paint, void *user); /* tree: where the kit's painter node lives */
/* A card lying face up in the XZ plane (front toward +Y, its top edge toward -Z), centred on its node. */
cw_node bk_card_new(cw_node parent, const char *name, const bk_card_desc *d);
/* Show another front (the face drawn on first use); a ghosted card (bk_ghost_apply) stays see-through. */
void bk_card_set_front(cw_node card, int face);
/* The render target holding a face's picture (made on first use, painted at the next update), for a 2D or UI
 * drawing of the same face. */
cw_texture bk_card_face(int face, int px_w, int px_h);
/* Draw a face again (its art changed). */
void bk_card_face_refresh(int face);
int bk_card_faces_made(void);

/* ── poses (pure: the game eases its card nodes along them) ── */
typedef struct bk_pose { t3_vec3 pos; t3_quat rot; } bk_pose;
/* a flip t (0..1) of a card at rest at `at` turning face down to face up (or back): it lifts, turns half a turn
 * about its long axis, and lands; t = 0 and t = 1 lie flat */
bk_pose bk_card_flip_pose(bk_pose at, float t, float lift, bool to_face_up);
/* n cards fanned in a hand: an arc of `spread` radians centred on `centre`, each turned to the arc, standing
 * up toward the camera by `tilt`; out[0..n-1], left to right, each slightly above the one before */
void bk_card_fan(int n, t3_vec3 centre, float radius, float spread, float tilt, bk_pose *out);
/* ease a pose toward a target (rate per second, frame-rate independent) */
bk_pose bk_pose_approach(bk_pose from, bk_pose to, float rate, float dt);

#ifdef __cplusplus
}
#endif
#endif
