/* Facet: the traced stones (gemtrace.h). The math of a stone is the jewels game's (gemshaders.c); here: its frame
 * on our table, the ray of each pixel from the table's camera, and the pass that draws them. Tray coordinates (the
 * tracer's): x = world x, y = -world z, z = world y, times TRAY units per world unit. */
#include "gemtrace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/gpu.h"
#include "cartwheel/pipeline.h"
#include "cuts.h"
#include "gemshaders.h"

#define TRAY 100.0f

static struct {
  bool wg, built, failed;
  unsigned prog[JW_NCUTS];
  unsigned studio_tex;
  float *studio;
  int studio_row;
  bool ready;
  gm_trace_gem g[GM_TRACE_MAX];
  int n, drawn;
} T;

static uint16_t to_half(float v) {
  uint32_t b;
  memcpy(&b, &v, 4);
  uint16_t s = (uint16_t)((b >> 16) & 0x8000);
  float a = fabsf(v);
  if (!(a >= 6.1035e-5f)) return s;
  if (a >= 65504.0f) return (uint16_t)(s | 0x7bff);
  memcpy(&b, &a, 4);
  b += 0x1000;
  int e = (int)((b >> 23) & 255) - 127 + 15;
  if (e >= 31) return (uint16_t)(s | 0x7bff);
  return (uint16_t)(s | (e << 10) | ((b >> 13) & 0x3ff));
}

void gm_trace_bake_step(void) {
  const int H = GM_STUDIO_N + 2;
  if (T.studio_row >= H) return;
  if (!T.studio) T.studio = malloc((size_t)GM_STUDIO_N * H * 4 * sizeof(float));
  gm_studio_rows(T.studio, T.studio_row, 32);
  T.studio_row += 32;
}

bool gm_trace_ready(void) { return T.ready && T.built && !T.failed; }
int gm_trace_drawn(void) { return T.drawn; }

void gm_trace_set(const gm_trace_gem *g, int n) {
  if (n > GM_TRACE_MAX) n = GM_TRACE_MAX;
  memcpy(T.g, g, sizeof *g * (size_t)n);
  T.n = n;
}

static bool build(void) {
  if (T.built) return !T.failed;
  T.built = true;
  T.wg = cw_gpu_backend() == CW_GPU_WEBGPU;
  for (int k = 0; k < JW_NCUTS - 1; k++) {
    static const char *names[] = {"gm_gem0", "gm_gem1", "gm_gem2", "gm_gem3", "gm_gem4", "gm_gem5"};
    char *vs = NULL, *src = gm_shader_gem(T.wg, &jw_cuts[k], &vs);
    T.prog[k] = T.wg ? cw_pass_program_wgsl(names[k], src) : cw_pass_program_vs(names[k], vs, src);
    free(src), free(vs);
    if (!T.prog[k]) T.failed = true;
  }
  if (T.failed) cw_error("facet: a stone shader failed to build (see above)");
  return !T.failed;
}

static void upload(void) {
  const int W = GM_STUDIO_N, H = GM_STUDIO_N + 2;
  if (T.studio_tex || !T.studio || T.studio_row < H) return;
  uint16_t *half = malloc((size_t)W * H * 4 * sizeof(uint16_t));
  for (size_t i = 0; i < (size_t)W * H * 4; i++) half[i] = to_half(T.studio[i]);
  T.studio_tex = cw_pass_texture_new(W, H, CW_RT_RGBA16F, half);
  free(half);
  free(T.studio), T.studio = NULL;
  T.ready = T.studio_tex != 0;
}

static t3_vec3 tray(t3_vec3 w) { return (t3_vec3){w.x, -w.z, w.y}; }

static void set_mat(unsigned p, const char *name, const float m[16]) { cw_pass_uniform_mat4(p, name, (const t3_mat4 *)m); }

/* world point -> pixel in the 1920 x 1080 canvas the tracer's vertex stage takes (y down) */
static bool project(const t3_mat4 *vp, t3_vec3 w, float *x, float *y) {
  const float *e = (const float *)vp;
  float cx = e[0] * w.x + e[4] * w.y + e[8] * w.z + e[12];
  float cy = e[1] * w.x + e[5] * w.y + e[9] * w.z + e[13];
  float cw = e[3] * w.x + e[7] * w.y + e[11] * w.z + e[15];
  if (cw <= 1e-4f) return false;
  *x = (cx / cw + 1) * 960.0f;
  *y = (1 - cy / cw) * 540.0f;
  return true;
}

static void cauchy(const jw_cut *c, float *A, float *B) {
  float b = c->dispersion * c->fire / (1.0f / (430.8f * 430.8f) - 1.0f / (686.7f * 686.7f));
  *B = b, *A = c->ior - b / (589.3f * 589.3f);
}

static void exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  T.drawn = 0;
  if (!build()) return;
  upload();
  if (!T.ready || !T.n) return;
  const float *v = (const float *)&ctx->view, *pr = (const float *)&ctx->proj;
  t3_vec3 right = {v[0], v[4], v[8]}, up = {v[1], v[5], v[9]}, back = {v[2], v[6], v[10]};
  float W = (float)ctx->width, H = (float)ctx->height, px = pr[0], py = pr[5];
  /* the direction through pixel (fx, fy), bottom-up: A fx + B fy + C */
  t3_vec3 A = {right.x * 2 / (W * px), right.y * 2 / (W * px), right.z * 2 / (W * px)};
  t3_vec3 B = {up.x * 2 / (H * py), up.y * 2 / (H * py), up.z * 2 / (H * py)};
  t3_vec3 C = {-back.x - right.x / px - up.x / py, -back.y - right.y / px - up.y / py, -back.z - right.z / px - up.z / py};
  A = tray(A), B = tray(B), C = tray(C);
  t3_vec3 eye = cw_node3d_global_position(ctx->camera_node);
  t3_vec3 eye_t = tray(eye);
  cw_pass_set_blend(CW_PASS_BLEND_PREMUL);
  cw_pass_set_depth(false, false);
  for (int i = 0; i < T.n; i++) {
    const gm_trace_gem *g = &T.g[i];
    if (g->opacity <= 0.01f || g->cut < 0 || g->cut >= JW_NCUTS - 1) continue;
    const jw_cut *c = &jw_cuts[g->cut];
    unsigned p = T.prog[g->cut];
    float s = g->size * TRAY;
    t3_vec3 ct = tray(g->pos);
    ct.x *= TRAY, ct.y *= TRAY, ct.z *= TRAY;
    float cs = cosf(g->yaw), sn = sinf(g->yaw);
    /* rot: local -> tray (a turn about z); inv: tray -> local = R^T (p - c) / s */
    float rot[16] = {cs, sn, 0, 0, -sn, cs, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float inv[16] = {cs / s, -sn / s, 0, 0, sn / s, cs / s, 0, 0, 0, 0, 1 / s, 0, 0, 0, 0, 1};
    inv[12] = -(cs * ct.x + sn * ct.y) / s;
    inv[13] = -(-sn * ct.x + cs * ct.y) / s;
    inv[14] = -ct.z / s;
    /* the quad: the bounding sphere's square on screen, with a margin */
    float r = c->radius * g->size * 1.15f, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool ok = true;
    for (int k = 0; k < 8 && ok; k++) {
      t3_vec3 q = {g->pos.x + ((k & 1) ? r : -r), g->pos.y + ((k & 2) ? r : -r), g->pos.z + ((k & 4) ? r : -r)};
      float sx, sy;
      ok = project(&ctx->view_proj, q, &sx, &sy);
      x0 = fminf(x0, sx), y0 = fminf(y0, sy), x1 = fmaxf(x1, sx), y1 = fmaxf(y1, sy);
    }
    if (!ok) continue;
    cw_pass_use(p);
    cw_pass_uniform4f(p, "u_view", W, H, 1 / W, 1 / H);
    cw_pass_uniform4f(p, "u_cam", eye_t.x * TRAY, eye_t.y * TRAY, eye_t.z * TRAY, 0);
    cw_pass_uniform4f(p, "u_tint", 1.0f, 0.97f, 0.92f, 0);
    set_mat(p, "u_inv", inv);
    set_mat(p, "u_rot", rot);
    cw_pass_uniform4f(p, "u_rect", x0, y0, x1, y1);
    cw_pass_uniform4f(p, "u_abs", c->sigma[0], c->sigma[1], c->sigma[2], g->opacity);
    float a, b;
    cauchy(c, &a, &b);
    cw_pass_uniform4f(p, "u_ior", a, b, 0, g->cut == 2 ? 0.022f : 0.0f);
    cw_pass_uniform4f(p, "u_q", 1, 5, 1, 0);
    cw_pass_uniform4f(p, "u_body", c->body[0], c->body[1], c->body[2], g->cut == 5 ? 0.0f : g->cut == 3 ? 0.13f : g->cut == 4 ? 0.11f : g->cut == 0 ? 0.10f : 0.05f);
    cw_pass_uniform4f(p, "u_jit", ctx->jitter[0], ctx->jitter[1], 0, 0);
    cw_pass_uniform4f(p, "u_rx", A.x, A.y, A.z, 0);
    cw_pass_uniform4f(p, "u_ry", B.x, B.y, B.z, 0);
    cw_pass_uniform4f(p, "u_rz", C.x, C.y, C.z, 0);
    static const char *ln[4] = {"u_gl0", "u_gl1", "u_gl2", "u_gl3"}, *kn[4] = {"u_gk0", "u_gk1", "u_gk2", "u_gk3"};
    for (int j = 0; j < 4; j++) {
      cw_pass_uniform4f(p, ln[j], 0, 0, 1, 0.05f);
      cw_pass_uniform4f(p, kn[j], 0, 0, 0, 0);
    }
    cw_pass_uniform4f(p, "u_gx", 0, 0, 0, 0);
    cw_pass_bind_texture(0, T.studio_tex);
    if (!T.wg) cw_pass_uniform1i(p, "u_t0", 0);
    cw_pass_draw(6);
    T.drawn++;
  }
}

void gm_trace_init(cw_tree *tree) {
  cw_pipeline *p = cw_pipeline_of(tree);
  if (cw_pipeline_has_pass(p, "gm_stones")) return;
  cw_pipeline_add_pass(p, &(cw_pass_desc){.name = "gm_stones", .phase = CW_PHASE_EFFECTS, .order = 150, .writes = CW_RES_COLOR,
                                          .execute = exec, .webgpu = true});
}
