/* Smokestack geometry: a small builder for vertex-coloured procedural meshes (terrain, buildings, track, trains).
 * Colours are given as sRGB 0xRRGGBB and stored linear. Every material it makes is double sided, so winding
 * never hides a face. */
#ifndef RL_GEOM_H
#define RL_GEOM_H

#include <stdint.h>

#include "cartwheel/render3d.h"

typedef struct rl_gb rl_gb;

rl_gb *rl_gb_new(void);
void rl_gb_free(rl_gb *b);
int rl_gb_verts(const rl_gb *b);
/* start a material group (indices from here on use material `m` of the mesh) */
void rl_gb_group(rl_gb *b, int m);
void rl_gb_tri(rl_gb *b, t3_vec3 a, t3_vec3 c1, t3_vec3 c2, uint32_t rgb);
void rl_gb_quad(rl_gb *b, t3_vec3 a, t3_vec3 c1, t3_vec3 c2, t3_vec3 d, uint32_t rgb);
/* a box: centre, half sizes, turned yaw radians about +Y */
void rl_gb_box(rl_gb *b, t3_vec3 c, t3_vec3 half, float yaw, uint32_t rgb);
/* a capped cylinder from a to b */
void rl_gb_cyl(rl_gb *b, t3_vec3 a, t3_vec3 c, float r, int seg, uint32_t rgb);
void rl_gb_cone(rl_gb *b, t3_vec3 base, float r, float h, int seg, uint32_t rgb);
/* an upright prism: `sides` corners on radius (first at angle rot), from y0 to y1, top and side colours */
void rl_gb_prism(rl_gb *b, t3_vec3 c, float radius, int sides, float rot, float y0, float y1, uint32_t top, uint32_t side);
/* a ring on the ground: between r0 and r1, `sides` corners, at height y */
void rl_gb_ring(rl_gb *b, t3_vec3 c, float r0, float r1, int sides, float rot, float y, uint32_t rgb);
/* a gable roof over a box footprint (half sizes hx along its ridge, hz across), eaves at y0, ridge at y1 */
void rl_gb_gable(rl_gb *b, t3_vec3 c, float hx, float hz, float y0, float y1, float yaw, uint32_t rgb);
/* a dome: a squashed half sphere */
void rl_gb_dome(rl_gb *b, t3_vec3 c, float r, float h, int seg, uint32_t rgb);
t3_geometry *rl_gb_geometry(rl_gb *b);

uint32_t rl_shade(uint32_t rgb, float k);           /* brighter (k > 1) or darker */
uint32_t rl_mix(uint32_t a, uint32_t b, float t);
t3_color rl_lin(uint32_t rgb);                      /* sRGB 0xRRGGBB to linear */
t3_material *rl_vc_material(float roughness);       /* vertex colours, double sided */
t3_material *rl_flat_material(uint32_t rgb, float roughness, float metalness);
/* a MeshInstance3D under parent showing geometry g with materials */
cw_node rl_mesh_node(cw_node parent, const char *name, t3_geometry *g, t3_material **mats, int n, bool shadow);

#endif
