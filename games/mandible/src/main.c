/* Mandible: the game. Two players, one table, carved insect pieces; surround the other side's Empress.
 *
 * The shell (cartwheel/shell.h) runs the title, join, settings and pause screens; seats are people on pads,
 * people passing one pad (hot seats) or CPUs at Easy, Normal or Hard. match.c holds the rules state and reads
 * every seat through actions; this file draws the table, the pieces and their motion, the highlights, the HUD,
 * the how-to-play card (in play, and as a page on the title beside the credits).
 *
 * Test switches (cw_test_env):
 *   MANDIBLE_SEATS=a,b   skip the menus (read in the shell's menu tick, bk_title): -1 a person on the next pad, -2 a hot seat, 0..2 a CPU at that level
 *                     (the self-playing build, mandible_auto, starts "2,1" by itself)
 *   MANDIBLE_SEED=n      the match seed (default 2026)
 * Debug state: game.* fields (turn, ply, result, phase, picks, wins, cpu_moves, stray, state_hash). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "boardkit/bk_shell.h"
#include "match.h"
#include "rules.h"

#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"

#ifndef MANDIBLE_AUTO
#define MANDIBLE_AUTO 0
#endif

#define H ART_PIECE_HEIGHT
#define STACK_STEP (ART_PIECE_HEIGHT * 0.97f)
#define MAX_RINGS MAX_PICKS

typedef struct anim {
  bool on;
  int id;
  t3_vec3 pts[24];  /* the path: points the piece passes through */
  int npts;
  float t, dur;     /* seconds */
  bool arc;         /* a jump or a lift (else a slide along the table) */
  float arc_h;
  int seat;         /* who to rumble on landing (-1 none) */
} anim;

static struct {
  sw_match m;
  cw_action act[A_COUNT];
  cw_tree *tree;
  cw_node world, cam, piece[SW_PIECES], ring[MAX_RINGS], cursor_ring, last_ring[2], ghost[2][SW_KINDS];
  t3_material *ring_mat, *cursor_mat, *last_mat, *ghost_mat[2];
  t3_vec3 shown[SW_PIECES];   /* where each piece is drawn */
  anim an;
  t3_vec3 cam_at, cam_target;
  float cam_dist;
  bool cam_set;
  bool fresh, started_by_env;
  int32_t ticks, phase, picks, turn, ply, result, cursor, state_hash, last_click;
  float frame_dt;
  int32_t start_seats, start_cpu[2];
  int32_t replaying, replay_done, replay_match, replay_hash_match, pointer_picks;
  uint32_t want_state, want_input;
  uint8_t *recording;
  float pick_px[64];      /* where the first 32 picks sit on the canvas (x, y): tests tap them */
  int32_t pick_code[32];  /* what each is: SELECT the source (piece id, 100 + kind from the hand, 200 the pass); TARGET
                           * 1000 + the move's kind (MT_PLACE, MT_STEP, MT_CLIMB, MT_PASS) */
  int32_t person_kinds[MT_COUNT]; /* moves people made, by kind (tests drive each kind through the pad and the pointer) */
  float time;
  int32_t help_page;
  char last_label[96];
  float banner_t;
} G;

static const char *const SIDE_NAME[2] = { "Ivory", "Ebony" };

/* ── scene building ───────────────────────────────────────────────────── */

static cw_node proc_node(const char *name, t3_geometry *geo, t3_material *mat, bool shadows) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(G.world, n);
  t3_mesh *mesh = t3_mesh_new(geo, mat);
  mesh->base.cast_shadow = shadows;
  mesh->base.receive_shadow = true;
  if (!cw_mesh3d_set_procedural(n, mesh)) CW_FATAL("mandible: could not attach the mesh of %s", name);
  t3_release(mesh);
  return n;
}

static cw_node box_node(const char *name, t3_vec3 at, t3_vec3 size, float r, float g, float b, float rough) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(G.world, n);
  cw_node_set(n, "primitive", cw_val_enum("BOX"));
  cw_node_set(n, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(n, "color", cw_val_color(r, g, b, 1));
  cw_node_set(n, "roughness", cw_val_float(rough));
  cw_node_set(n, "receive_shadow", cw_val_bool(true));
  cw_node3d_set_position(n, at);
  return n;
}

/* the last move in words for the HUD: who did what, next to which piece (never coordinates) */
static void say_move(const sw_state *s, sw_move m, char *buf, size_t cap) {
  int kind = sw_move_kind(m), id = sw_move_id(m);
  if (kind == SW_MOVE_PASS) {
    snprintf(buf, cap, "%s had no legal move and passed", SIDE_NAME[s->to_move]);
    return;
  }
  const sw_piece *pc = &s->p[id];
  const char *who = SIDE_NAME[pc->side], *name = sw_kind_name(pc->kind);
  cw_axial to = sw_move_to(m);
  int under = sw_top_at(s, to);
  if (under >= 0 && under != id) { /* onto a stack */
    snprintf(buf, cap, "%s's %s climbed onto %s's %s", who, name, SIDE_NAME[s->p[under].side], sw_kind_name(s->p[under].kind));
    return;
  }
  int near = -1; /* a neighbour to name: an Empress first, else the first found */
  for (int d = 0; d < 6; d++) {
    int t = sw_top_at(s, cw_hex_neighbor(to, d));
    if (t < 0 || t == id) continue;
    if (near < 0 || (s->p[t].kind == SW_EMPRESS && s->p[near].kind != SW_EMPRESS)) near = t;
  }
  const char *a = strchr("AEIOU", name[0]) ? "an" : "a";
  char where[48];
  if (near >= 0) snprintf(where, sizeof where, "next to %s's %s", SIDE_NAME[s->p[near].side], sw_kind_name(s->p[near].kind));
  else snprintf(where, sizeof where, "in the middle of the table");
  if (kind == SW_MOVE_PLACE) snprintf(buf, cap, "%s placed %s %s %s", who, a, name, where);
  else snprintf(buf, cap, "%s moved the %s %s", who, name, where);
}

static void set_visible(cw_node n, bool on) { cw_node_set(n, "visible", cw_val_bool(on)); }

static t3_material *flat_mat(float r, float g, float b, float opacity) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ r, g, b };
  if (opacity < 1) {
    m->transparent = true;
    m->opacity = opacity;
    m->depth_write = false;
  }
  return m;
}

static void build_scene(void) {
  G.tree = cw_scene_app_tree();
  G.world = cw_node_get(cw_tree_root(G.tree), "World");
  G.cam = cw_node_get(G.world, "Camera");
  /* the table: a thick walnut top of planks, a felt mat inlaid in it (mottled, its nap catching the light, darker
   * toward its edge) framed by a brass strip; the group plays in the middle and the wood shows round it */
  box_node("Table", (t3_vec3){ 0, -0.26f, 0 }, (t3_vec3){ 50, 0.5f, 36 }, 0.20f, 0.11f, 0.06f, 0.62f);
  {
    const float FX = 10.0f, FZ = 6.5f; /* the felt's half size: the wood shows round the group */
    t3_material *wood = t3_mesh_standard_material_new(0xffffff), *felt = t3_mesh_standard_material_new(0xffffff);
    wood->vertex_colors = felt->vertex_colors = true;
    wood->metalness = felt->metalness = 0;
    wood->roughness = 0.55f, felt->roughness = 1.0f;
    t3_geometry *wg = art_wood_top_geometry(25, 18, 0.9f), *fg = art_felt_geometry(FX, FZ);
    cw_node3d_set_position(proc_node("TableTop", wg, wood, false), (t3_vec3){ 0, -0.004f, 0 });
    proc_node("Felt", fg, felt, false);
    t3_release(wg), t3_release(fg), t3_release(wood), t3_release(felt);
    box_node("FeltEdgeN", (t3_vec3){ 0, 0.0f, -FZ - 0.06f }, (t3_vec3){ 2 * FX + 0.24f, 0.012f, 0.12f }, 0.62f, 0.48f, 0.22f, 0.4f);
    box_node("FeltEdgeS", (t3_vec3){ 0, 0.0f, FZ + 0.06f }, (t3_vec3){ 2 * FX + 0.24f, 0.012f, 0.12f }, 0.62f, 0.48f, 0.22f, 0.4f);
    box_node("FeltEdgeW", (t3_vec3){ -FX - 0.06f, 0.0f, 0 }, (t3_vec3){ 0.12f, 0.012f, 2 * FZ + 0.24f }, 0.62f, 0.48f, 0.22f, 0.4f);
    box_node("FeltEdgeE", (t3_vec3){ FX + 0.06f, 0.0f, 0 }, (t3_vec3){ 0.12f, 0.012f, 2 * FZ + 0.24f }, 0.62f, 0.48f, 0.22f, 0.4f);
  }
  /* the pieces: one carved geometry per side and kind, shared */
  t3_geometry *geo[2][SW_KINDS];
  t3_material *mat[2];
  for (int side = 0; side < 2; side++) {
    mat[side] = t3_mesh_standard_material_new(0xffffff);
    mat[side]->vertex_colors = true;
    mat[side]->roughness = side == 0 ? 0.48f : 0.42f;
    mat[side]->metalness = 0;
    G.ghost_mat[side] = t3_mesh_standard_material_new(0xffffff);
    G.ghost_mat[side]->vertex_colors = true;
    G.ghost_mat[side]->transparent = true;
    G.ghost_mat[side]->opacity = 0.5f;
    G.ghost_mat[side]->depth_write = false;
    for (int k = 0; k < SW_KINDS; k++) geo[side][k] = art_piece_geometry(side, k);
  }
  sw_state init;
  sw_init(&init);
  for (int i = 0; i < SW_PIECES; i++) {
    char nm[24];
    snprintf(nm, sizeof nm, "Piece%d", i);
    G.piece[i] = proc_node(nm, geo[init.p[i].side][init.p[i].kind], mat[init.p[i].side], true);
    cw_node_add_group(G.piece[i], "pieces");
  }
  for (int side = 0; side < 2; side++)
    for (int k = 0; k < SW_KINDS; k++) {
      char nm[24];
      snprintf(nm, sizeof nm, "Ghost%d%d", side, k);
      G.ghost[side][k] = proc_node(nm, geo[side][k], G.ghost_mat[side], false);
      set_visible(G.ghost[side][k], false);
    }
  for (int side = 0; side < 2; side++) {
    for (int k = 0; k < SW_KINDS; k++) t3_release(geo[side][k]);
    t3_release(mat[side]);
  }
  /* markers */
  G.ring_mat = flat_mat(1.0f, 0.55f, 0.08f, 1);
  G.cursor_mat = flat_mat(1.0f, 0.97f, 0.85f, 1);
  G.last_mat = flat_mat(0.55f, 0.75f, 0.85f, 1);
  t3_geometry *ring = art_ring_geometry(0.47f, 0.37f), *fat = art_ring_geometry(0.56f, 0.40f);
  for (int i = 0; i < MAX_RINGS; i++) {
    char nm[24];
    snprintf(nm, sizeof nm, "Ring%d", i);
    G.ring[i] = proc_node(nm, ring, G.ring_mat, false);
    set_visible(G.ring[i], false);
  }
  G.cursor_ring = proc_node("Cursor", fat, G.cursor_mat, false);
  set_visible(G.cursor_ring, false);
  for (int i = 0; i < 2; i++) {
    G.last_ring[i] = proc_node(i ? "LastTo" : "LastFrom", ring, G.last_mat, false);
    set_visible(G.last_ring[i], false);
  }
  t3_release(ring), t3_release(fat);
}

/* ── where things rest ────────────────────────────────────────────────── */

static t3_vec3 hex_pos(cw_axial h, int level) {
  float x, z;
  match_hex_world(h, &x, &z);
  return (t3_vec3){ x, (float)level * STACK_STEP, z };
}

/* the rest position of every piece in a state: on the table, or stacked in its owner's tray */
static t3_vec3 rest_pos(const sw_state *s, int id) {
  const sw_piece *p = &s->p[id];
  if (p->placed) return hex_pos((cw_axial){ p->q, p->r }, p->h);
  int n = 0;
  for (int j = p->side * SW_SIDE_PIECES; j < id; j++)
    if (s->p[j].kind == p->kind && !s->p[j].placed) n++;
  float x, z;
  match_hand_world(s, p->side, p->kind, &x, &z);
  return (t3_vec3){ x, (float)n * STACK_STEP, z };
}

/* the top of the tray stack of a kind (where the next piece of that kind comes from) */
static int hand_top_id(const sw_state *s, int side, int kind) {
  int top = -1;
  for (int j = side * SW_SIDE_PIECES; j < (side + 1) * SW_SIDE_PIECES; j++)
    if (s->p[j].kind == kind && !s->p[j].placed) top = j;
  return top;
}

/* ── motion ───────────────────────────────────────────────────────────── */

/* a walker's way from `from` to `to` (shortest slide path in the state before the move, the mover lifted) */
static int walk_path(const sw_state *before, int id, cw_axial from, cw_axial to, cw_axial *out, int cap) {
  sw_state s = *before;
  /* lift the mover so it does not block itself */
  s.top[sw_cell(from)] = (int8_t)(s.p[id].below + 1);
  cw_axial q[128];
  int prev[128], n = 0, head = 0;
  q[n] = from, prev[n++] = -1;
  int found = -1;
  int steps_needed = s.p[id].kind == SW_WEAVER ? 3 : 0;
  (void)steps_needed;
  while (head < n && found < 0) {
    int cur = head++;
    for (int d = 0; d < 6 && found < 0; d++) {
      cw_axial nx = cw_hex_neighbor(q[cur], d);
      if (sw_top_at(&s, nx) >= 0) continue;
      bool a = sw_top_at(&s, cw_hex_neighbor(q[cur], d - 1)) >= 0, b = sw_top_at(&s, cw_hex_neighbor(q[cur], d + 1)) >= 0;
      if (a == b) continue;
      bool seen = false;
      for (int i = 0; i < n; i++) seen = seen || cw_hex_eq(q[i], nx);
      if (seen || n >= 128) continue;
      q[n] = nx, prev[n] = cur;
      if (cw_hex_eq(nx, to)) found = n;
      n++;
    }
  }
  if (found < 0) {
    out[0] = from, out[1] = to;
    return 2;
  }
  cw_axial rev[64];
  int k = 0;
  for (int i = found; i >= 0 && k < 64; i = prev[i]) rev[k++] = q[i];
  int c = 0;
  for (int i = k - 1; i >= 0 && c < cap; i--) out[c++] = rev[i];
  return c;
}

static void start_anim(const sw_match_events *ev) {
  const sw_state *b = &ev->before;
  sw_move mv = ev->move;
  anim *a = &G.an;
  memset(a, 0, sizeof *a);
  if (sw_move_kind(mv) == SW_MOVE_PASS) return;
  int id = sw_move_id(mv);
  int kind = b->p[id].kind;
  a->id = id;
  a->seat = G.m.cpu[b->p[id].side] < 0 ? match_seat_of_side(&G.m, b->p[id].side) : -1;
  t3_vec3 end = rest_pos(&G.m.st, id);
  t3_vec3 start = G.shown[id];
  if (sw_move_kind(mv) == SW_MOVE_PLACE || kind == SW_LOCUST || kind == SW_RAM) {
    a->pts[0] = start, a->pts[1] = end, a->npts = 2;
    a->arc = true;
    float dist = hypotf(end.x - start.x, end.z - start.z);
    a->arc_h = kind == SW_RAM && sw_move_kind(mv) == SW_MOVE_STEP ? 0.35f + fabsf(end.y - start.y) : 0.5f + dist * 0.09f;
    a->dur = sw_move_kind(mv) == SW_MOVE_PLACE ? 0.55f + dist * 0.025f : 0.45f + dist * 0.04f;
  } else {
    cw_axial path[24];
    int n = walk_path(b, id, sw_move_from(mv), sw_move_to(mv), path, 24);
    for (int i = 0; i < n; i++) a->pts[i] = hex_pos(path[i], 0);
    a->pts[0] = start;
    a->npts = n;
    a->dur = 0.16f + 0.17f * (float)(n - 1);
  }
  a->on = true;
}

static float ease_out(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }

/* advance the piece in motion; the others glide to rest */
static void update_motion(float dt) {
  for (int i = 0; i < SW_PIECES; i++) {
    if (G.an.on && G.an.id == i) continue;
    t3_vec3 r = rest_pos(&G.m.st, i);
    float k = 1 - expf(-dt * 9);
    G.shown[i].x += (r.x - G.shown[i].x) * k;
    G.shown[i].y += (r.y - G.shown[i].y) * k;
    G.shown[i].z += (r.z - G.shown[i].z) * k;
  }
  anim *a = &G.an;
  if (!a->on) return;
  a->t += dt;
  float u = fminf(a->t / a->dur, 1);
  t3_vec3 p;
  if (a->arc) {
    float e = ease_out(u * 0.92f + 0.08f * u * u);
    t3_vec3 s = a->pts[0], f = a->pts[1];
    p = (t3_vec3){ s.x + (f.x - s.x) * e, s.y + (f.y - s.y) * e + a->arc_h * 4 * u * (1 - u), s.z + (f.z - s.z) * e };
  } else {
    /* along the path, slowing into the last hex: a heavy piece pushed across the felt */
    float segs = (float)(a->npts - 1), w = u < 1 ? (1 - powf(1 - u, 2.2f)) * segs : segs;
    int i = (int)floorf(w);
    if (i >= a->npts - 1) i = a->npts - 2;
    float f = w - (float)i;
    t3_vec3 s = a->pts[i], e = a->pts[i + 1];
    p = (t3_vec3){ s.x + (e.x - s.x) * f, s.y + (e.y - s.y) * f + 0.03f * sinf(u * 3.14159f), s.z + (e.z - s.z) * f };
  }
  G.shown[a->id] = p;
  if (u >= 1) {
    a->on = false;
    G.shown[a->id] = rest_pos(&G.m.st, a->id);
    if (a->seat >= 0) cw_input_rumble(a->seat, 0.45f, 0.15f, 110); /* the thump of a heavy piece landing */
  }
}

/* ── seats and the shell ──────────────────────────────────────────────── */

static uint64_t match_seed(void) {
  const char *s = cw_test_env("MANDIBLE_SEED");
  return s && *s ? (uint64_t)strtoull(s, NULL, 10) : 2026;
}

static void begin_match(int players) {
  int32_t cpu[2] = { -1, -1 };
  for (int side = 0; side < 2; side++) {
    cw_shell_cpu_level lv;
    if (side < players && cw_shell_seat_cpu(side, &lv)) cpu[side] = (int32_t)lv;
  }
  if (players < 2) cpu[1] = CW_SHELL_CPU_NORMAL; /* alone at the table: a CPU takes the dark pieces */
  match_start(&G.m, players < 2 ? 1 : 2, cpu, match_seed(), MANDIBLE_AUTO != 0);
  for (int i = 0; i < SW_PIECES; i++) G.shown[i] = rest_pos(&G.m.st, i);
  memset(&G.an, 0, sizeof G.an);
  G.last_label[0] = 0;
  G.cam_set = false;
  G.fresh = true;
  G.help_page = 0;
  cw_shell_set_turn(G.m.st.to_move);
  G.start_seats = players < 2 ? 1 : 2;
  G.start_cpu[0] = cpu[0], G.start_cpu[1] = cpu[1];
  G.replaying = G.replay_done = 0;
  cw_input_record_begin();
}

/* `replay`: put the match back as it began and replay its own recording; when the recording runs out the state
 * and the input hash must be what they were at the request (pad presses, the CPUs' presses and pointer picks are
 * all in the recording) */
static void cmd_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (cw_shell_screen_now() != CW_SHELL_PLAY || !cw_input_recording_active()) {
    cw_reply_error(r, "replay needs a match in play (it replays the match from its start)");
    return;
  }
  G.want_state = match_hash(&G.m);
  cw_input_record_end();
  G.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  cw_free(G.recording);
  G.recording = cw_alloc(len ? len : 1, CW_MEM_GAME);
  memcpy(G.recording, d, len);
  match_start(&G.m, G.start_seats, G.start_cpu, match_seed(), MANDIBLE_AUTO != 0);
  for (int i = 0; i < SW_PIECES; i++) G.shown[i] = rest_pos(&G.m.st, i);
  memset(&G.an, 0, sizeof G.an);
  if (!cw_input_replay_begin(G.recording, len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  G.replaying = 1;
  G.replay_done = G.replay_match = G.replay_hash_match = 0;
  cw_reply_printf(r, "{\"bytes\":%zu,\"state\":%u,\"input\":%u}", len, G.want_state, G.want_input);
}

/* `setup <name>`: the match goes on from a test position (match_setup), for tests that drive every kind of move
 * through the pad and the pointer */
static void cmd_setup(int argc, const char **argv, cw_reply *r) {
  if (cw_shell_screen_now() != CW_SHELL_PLAY) {
    cw_reply_error(r, "setup needs a match in play");
    return;
  }
  if (argc < 2 || !match_setup(&G.m, argv[1])) {
    cw_reply_error(r, "setup moves|pass");
    return;
  }
  for (int i = 0; i < SW_PIECES; i++) G.shown[i] = rest_pos(&G.m.st, i);
  memset(&G.an, 0, sizeof G.an);
  G.last_label[0] = 0;
  cw_shell_set_turn(G.m.st.to_move);
  cw_reply_printf(r, "{\"setup\":\"%s\"}", argv[1]);
}

static void start(int players, void *user) {
  (void)user;
  begin_match(players);
}

static void pause_item(int index, void *user) {
  (void)user;
  if (index == 0) G.m.help = 1, G.help_page = 0;
}

/* ── the pointer: a click on a pick presses `point` with its index ──────── */

/* the height a pick is pointed at: a piece's top, a tray stack's top, a space's floor */
static float pick_y(const sw_pick *p) {
  if (G.m.phase == PH_TARGET) return (float)sw_height_at(&G.m.st, p->hex) * STACK_STEP;
  if (p->src < SW_PIECES) return (float)G.m.st.p[p->src].h * STACK_STEP + H;
  if (p->src >= SRC_HAND && p->src < SRC_PASS) {
    int top = hand_top_id(&G.m.st, G.m.st.to_move, p->src - SRC_HAND), n = 0;
    for (int j = G.m.st.to_move * SW_SIDE_PIECES; j <= top && top >= 0; j++)
      if (G.m.st.p[j].kind == G.m.st.p[top].kind && !G.m.st.p[j].placed) n++;
    return (float)n * STACK_STEP;
  }
  return 0.15f;
}

/* this tick's click or tap (the engine's pointer channel: live, or the recording's during a replay) */
static int pointer_pick(void) {
  cw_pointer pt = cw_input_pointer();
  if (!pt.pressed) return -1;
  cw_vec2 at = cw_r2d_window_to_canvas(pt.pos);
  cw_view3d views[1];
  if (cw_canvas_views3d(G.tree, views, 1) < 1) return -1;
  cw_ray3d ray = cw_camera3d_screen_ray(views[0].camera, views[0].rect, at);
  if (G.m.help) return 0;
  sw_pick p[MAX_PICKS];
  int n = match_picks(&G.m, p, MAX_PICKS), best = -1;
  float bd = 0.48f;
  for (int i = 0; i < n; i++) {
    /* the ray at the height of the pick's top */
    float y = pick_y(&p[i]);
    if (fabsf(ray.direction.y) < 1e-4f) continue;
    float t = (y - ray.origin.y) / ray.direction.y;
    float x = ray.origin.x + ray.direction.x * t, z = ray.origin.z + ray.direction.z * t;
    float d = hypotf(x - p[i].x, z - p[i].z);
    if (d < bd) bd = d, best = i;
  }
  return best;
}

/* ── the tick ─────────────────────────────────────────────────────────── */

static void start_from_env(void) {
  const char *seats = cw_test_env("MANDIBLE_SEATS");
  if (MANDIBLE_AUTO && (!seats || !*seats)) seats = "c2:c1";
  if (!seats || !*seats) return;
  /* seats split by ',' or ':': p a person on the next pad, h a hot seat, c0..c2 a CPU at that level (the kit's
   * lobby form), or the shell's numbers (-1 pad, -2 hot seat, 0..2 CPU) */
  int cpu[2] = { -1, -1 }, n = 0;
  for (const char *c = seats; *c && n < 2;) {
    if (*c == 'p') cpu[n] = -1;
    else if (*c == 'h') cpu[n] = -2;
    else if (*c == 'c') cpu[n] = atoi(c + 1);
    else cpu[n] = atoi(c);
    n++;
    while (*c && *c != ',' && *c != ':') c++;
    if (!*c) break;
    c++;
  }
  G.started_by_env = true;
  cw_shell_start_seats(n, cpu);
}

/* MANDIBLE_NEWCOMER=1: a test player on Ivory's pad who knows only what the screen shows: the lit pieces it may
 * take, the lit spaces each one may go to, and on a stack a lit space reads as a climb. It closes the card, then
 * each turn takes a lit piece and puts it on a lit space, preferring a kind of move it has not made yet (placing,
 * stepping, climbing): when the piece it took shows no new kind, it puts it back and tries its next piece on the
 * table, and after trying them all plays the first space. MANDIBLE_NEWCOMER=lost only walks the cursor (the
 * control: no move is ever made). Its presses go in as seat 0's actions, recorded like a pad's. */
static struct { int mode, wait, tried, want_src; int32_t moves, kinds; } NB;

static void newcomer_tick(void) {
  if (!NB.mode) {
    const char *e = cw_test_env("MANDIBLE_NEWCOMER");
    if (!e || !*e) return;
    NB.mode = strcmp(e, "lost") == 0 ? 2 : 1;
    NB.want_src = -1;
  }
  if (cw_shell_screen_now() != CW_SHELL_PLAY || G.fresh || G.m.cpu[0] >= 0) return;
  cw_action_value v[1];
  int nv = 0;
  if (NB.wait > 0) {
    NB.wait--;
  } else if (G.m.help) {
    v[nv++] = (cw_action_value){ G.act[A_CONFIRM], 1 }; /* the card says how to close it (the lost player too) */
    NB.wait = 10;
  } else if (G.m.st.to_move == 0 && (G.m.phase == PH_SELECT || G.m.phase == PH_TARGET)) {
    sw_pick p[MAX_PICKS];
    int n = match_picks(&G.m, p, MAX_PICKS), goal = 0;
    bool fresh_kind = false;
    if (NB.mode == 2) {
      v[nv++] = (cw_action_value){ G.act[A_NEXT], 1 };
    } else if (G.m.phase == PH_SELECT) {
      /* the piece to take: the next table piece not tried this turn while a kind of move is still new, else the first */
      bool want_more = !G.person_kinds[MT_STEP] || !G.person_kinds[MT_CLIMB];
      int k = 0;
      for (int i = 0; i < n && want_more; i++)
        if (p[i].src < SRC_HAND && k++ == NB.tried) { goal = i; fresh_kind = true; break; }
      if (!fresh_kind) goal = 0;
      v[nv++] = (cw_action_value){ G.act[G.m.cursor == goal ? A_CONFIRM : A_NEXT], 1 };
      if (G.m.cursor == goal) NB.want_src = fresh_kind ? p[goal].src : -1;
    } else {
      int first_new = -1;
      for (int i = 0; i < n && first_new < 0; i++) {
        int t = match_move_type(&G.m.st, p[i].move);
        if (!G.person_kinds[t]) first_new = i;
      }
      if (first_new < 0 && NB.want_src >= 0) { /* nothing new from this piece: put it back, try the next one */
        v[nv++] = (cw_action_value){ G.act[A_BACK], 1 };
        NB.tried++;
        NB.want_src = -1;
      } else {
        goal = first_new >= 0 ? first_new : 0;
        v[nv++] = (cw_action_value){ G.act[G.m.cursor == goal ? A_CONFIRM : A_NEXT], 1 };
        if (G.m.cursor == goal) NB.moves++, NB.tried = 0, NB.want_src = -1;
      }
    }
    NB.wait = 8;
  }
  cw_input_set_actions(0, v, nv);
  NB.kinds = (G.person_kinds[MT_PLACE] > 0) + (G.person_kinds[MT_STEP] > 0) + (G.person_kinds[MT_CLIMB] > 0);
}

static void fixed_update(void *user, double dt) {
  (void)user;
  newcomer_tick();
  bool play = cw_shell_update();
  G.time += (float)dt;
  if (G.replaying && !cw_input_replay_active()) {
    G.replay_match = match_hash(&G.m) == G.want_state;
    G.replay_hash_match = cw_input_replay_hash() == G.want_input;
    G.replaying = 0;
    G.replay_done = 1;
    cw_info("mandible replay: state %s, input hash %s", G.replay_match ? "same" : "DIFFERENT", G.replay_hash_match ? "same" : "DIFFERENT");
  }
  if (play && !G.fresh) {
    /* a human's click becomes a press of `point` this tick, before the rules read the seat */
    int side = G.m.st.to_move;
    if (G.m.cpu[side] < 0 && (G.m.phase == PH_SELECT || G.m.phase == PH_TARGET || G.m.help)) {
      int pick = pointer_pick();
      if (pick >= 0 && cw_input_set_action(match_seat_of_side(&G.m, side), G.act[A_POINT], match_point_strength(pick))) G.pointer_picks++;
    }
    sw_match_events ev;
    int help_before = G.m.help;
    match_tick(&G.m, G.act, &ev);
    if (G.m.help && help_before) {
      /* the card has two pages: left and right turn them (read for drawing only) */
      for (int s = 0; s < 2; s++) {
        if (cw_action_just_pressed(s, G.act[A_RIGHT])) G.help_page = 1;
        if (cw_action_just_pressed(s, G.act[A_LEFT])) G.help_page = 0;
      }
    }
    if (ev.moved) {
      if (G.m.cpu[ev.before.to_move] < 0) G.person_kinds[match_move_type(&ev.before, ev.move)]++;
      start_anim(&ev);
      say_move(&ev.before, ev.move, G.last_label, sizeof G.last_label);
    }
    if (ev.game_over) G.banner_t = 0;
    if (ev.started) {
      for (int i = 0; i < SW_PIECES; i++) G.shown[i] = rest_pos(&G.m.st, i);
      G.last_label[0] = 0;
    }
    cw_shell_set_turn(G.m.phase == PH_OVER ? -1 : G.m.st.to_move);
  }
  if (G.fresh) G.fresh = false;
  update_motion((float)dt);
  G.banner_t += (float)dt;
  G.ticks++;
  G.turn = G.m.st.to_move;
  G.ply = G.m.st.ply;
  G.result = G.m.st.result;
  G.phase = G.m.phase;
  G.cursor = G.m.cursor;
  G.state_hash = (int32_t)match_hash(&G.m);
}

/* ── per-frame view ───────────────────────────────────────────────────── */

static void place_ring(cw_node n, float x, float y, float z, float scale) {
  cw_node3d_set_position(n, (t3_vec3){ x, y + 0.008f, z });
  cw_node3d_set_scale(n, (t3_vec3){ scale, 1, scale });
  set_visible(n, true);
}

static void update_view(float dt) {
  bool play = cw_shell_screen_now() == CW_SHELL_PLAY || cw_shell_screen_now() == CW_SHELL_PAUSE;
  const sw_state *s = &G.m.st;
  sw_pick p[MAX_PICKS];
  int n = play ? match_picks(&G.m, p, MAX_PICKS) : 0;
  G.picks = n;
  {
    cw_view3d views[1];
    bool v = cw_canvas_views3d(G.tree, views, 1) > 0;
    for (int i = 0; i < 32; i++) {
      G.pick_code[i] = i >= n ? -1 : G.m.phase == PH_SELECT ? p[i].src : 1000 + match_move_type(s, p[i].move);
      cw_vec2 sp = { -1, -1 };
      if (v && i < n) {
        float y = pick_y(&p[i]);
        sp = cw_camera3d_world_to_screen(views[0].camera, views[0].rect, (t3_vec3){ p[i].x, y, p[i].z }, NULL);
      }
      G.pick_px[2 * i] = sp.x, G.pick_px[2 * i + 1] = sp.y;
    }
  }
  bool human = play && !match_cpu_turn(&G.m);
  float bob = 0.5f + 0.5f * sinf(G.time * 4.0f);
  /* lift the piece under the cursor (SELECT) or the picked one (TARGET) */
  int lifted = -1;
  if (play && n > 0 && G.m.cursor < n) {
    int src = G.m.phase == PH_SELECT ? p[G.m.cursor].src : G.m.src;
    if (src >= 0 && src < SW_PIECES) lifted = src;
    else if (src >= SRC_HAND && src < SRC_PASS) lifted = hand_top_id(s, s->to_move, src - SRC_HAND);
  }
  for (int i = 0; i < SW_PIECES; i++) {
    t3_vec3 at = G.shown[i];
    if (i == lifted && !(G.an.on && G.an.id == i)) at.y += G.m.phase == PH_TARGET ? 0.14f : 0.05f + 0.03f * bob;
    cw_node3d_set_position(G.piece[i], at);
    /* white faces the far side, black the near: the carving is already turned; spin nothing */
  }
  /* the rings under the picks */
  int used = 0;
  for (int i = 0; i < n && used < MAX_RINGS; i++) {
    if (i == G.m.cursor) continue;
    if (p[i].src == SRC_PASS) continue;
    /* a space's floor, or the top of a piece that can be picked */
    place_ring(G.ring[used++], p[i].x, pick_y(&p[i]), p[i].z, G.m.phase == PH_TARGET ? 1.0f : 0.96f);
  }
  for (int i = used; i < MAX_RINGS; i++) set_visible(G.ring[i], false);
  /* the cursor and the ghost */
  for (int side = 0; side < 2; side++)
    for (int k = 0; k < SW_KINDS; k++) set_visible(G.ghost[side][k], false);
  if (n > 0 && G.m.cursor < n && p[G.m.cursor].src != SRC_PASS) {
    float y = pick_y(&p[G.m.cursor]);
    if (G.m.phase == PH_SELECT) y += 0.05f + 0.03f * bob; /* on the lifted piece */
    place_ring(G.cursor_ring, p[G.m.cursor].x, y, p[G.m.cursor].z, 1.0f + 0.04f * bob);
    if (G.m.phase == PH_TARGET) {
      int id = sw_move_id(p[G.m.cursor].move);
      cw_node g = G.ghost[s->p[id].side][s->p[id].kind];
      cw_node3d_set_position(g, (t3_vec3){ p[G.m.cursor].x, y + 0.002f, p[G.m.cursor].z });
      set_visible(g, true);
    }
  } else {
    set_visible(G.cursor_ring, false);
  }
  (void)human;
  /* the last move */
  if (play && G.m.has_last && sw_move_kind(G.m.last_move) == SW_MOVE_STEP && !G.an.on) {
    cw_axial f = sw_move_from(G.m.last_move), t = sw_move_to(G.m.last_move);
    t3_vec3 a = hex_pos(f, sw_height_at(s, f)), b = hex_pos(t, sw_height_at(s, t) - 1);
    place_ring(G.last_ring[0], a.x, a.y, a.z, 0.9f);
    (void)b;
    set_visible(G.last_ring[1], false);
  } else {
    set_visible(G.last_ring[0], false);
    set_visible(G.last_ring[1], false);
  }
  /* the camera: frames the group and both trays, eased */
  float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
  for (int i = 0; i < SW_PIECES; i++) {
    t3_vec3 r = rest_pos(s, i);
    minx = fminf(minx, r.x), maxx = fmaxf(maxx, r.x), minz = fminf(minz, r.z), maxz = fmaxf(maxz, r.z);
  }
  t3_vec3 target = { (minx + maxx) * 0.5f, 0, (minz + maxz) * 0.5f - 0.45f };
  float w = (maxx - minx) + 2.2f, d = (maxz - minz) + 2.4f;
  float dist = fmaxf(w * 0.92f, d * 1.55f);
  dist = fmaxf(dist, 6.8f);
  if (!G.cam_set) {
    G.cam_at = target, G.cam_dist = dist, G.cam_set = true;
  } else {
    float k = 1 - expf(-dt * 2.5f);
    G.cam_at.x += (target.x - G.cam_at.x) * k;
    G.cam_at.z += (target.z - G.cam_at.z) * k;
    G.cam_dist += (dist - G.cam_dist) * k;
  }
  float pitch = 0.98f; /* radians above the table */
  t3_vec3 eye = { G.cam_at.x, G.cam_at.y + G.cam_dist * sinf(pitch), G.cam_at.z + G.cam_dist * cosf(pitch) };
  cw_node3d_set_position(G.cam, eye);
  cw_node3d_look_at(G.cam, G.cam_at, (t3_vec3){ 0, 1, 0 });
}

/* ── HUD ──────────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = { .size = size, .color = c, .align = al };
  if (al != CW_ALIGN_LEFT) {
    cw_text_metrics tm = cw_font_measure(cw_ui_font_sans(), s, &(cw_text_style){ .size = size });
    x -= al == CW_ALIGN_CENTER ? tm.width * 0.5f : tm.width;
    st.align = CW_ALIGN_LEFT;
  }
  cw_r2d_text(cw_ui_font_sans(), s, ((cw_vec2){ x, y }), &st);
}

static const char *seat_label(int side) {
  static char buf[2][40];
  if (G.m.cpu[side] >= 0)
    snprintf(buf[side], sizeof buf[side], "%s  (CPU %s)", SIDE_NAME[side], cw_shell_cpu_level_label((cw_shell_cpu_level)G.m.cpu[side]));
  else if (G.m.seats > 1)
    snprintf(buf[side], sizeof buf[side], "%s  (Player %d)", SIDE_NAME[side], side + 1);
  else
    snprintf(buf[side], sizeof buf[side], "%s  (You)", SIDE_NAME[side]);
  return buf[side];
}

/* the six hexes around an Empress, filled as she is surrounded: the score of this game */
static void draw_pressure(int side, float cx, float cy, float r) {
  int k = sw_empress_pressure(&G.m.st, side);
  bool placed = sw_empress_placed(&G.m.st, side);
  for (int i = 0; i < 6; i++) {
    float a = (60.0f * (float)i - 90.0f) * 3.14159f / 180.0f;
    cw_vec2 c = { cx + cosf(a) * r * 1.75f, cy + sinf(a) * r * 1.75f };
    cw_vec2 pts[6];
    for (int j = 0; j < 6; j++) {
      float b = (60.0f * (float)j + 30.0f) * 3.14159f / 180.0f;
      pts[j] = (cw_vec2){ c.x + cosf(b) * r * 0.9f, c.y + sinf(b) * r * 0.9f };
    }
    bool on = placed && i < k;
    cw_r2d_polygon(pts, 6, on ? (k >= 5 ? rgba(0.95f, 0.38f, 0.25f, 0.95f) : rgba(0.95f, 0.78f, 0.40f, 0.95f)) : rgba(1, 1, 1, 0.12f));
  }
  cw_vec2 mid[6];
  for (int j = 0; j < 6; j++) {
    float b = (60.0f * (float)j + 30.0f) * 3.14159f / 180.0f;
    mid[j] = (cw_vec2){ cx + cosf(b) * r * 0.9f, cy + sinf(b) * r * 0.9f };
  }
  const float *g = art_inlay_color(SW_EMPRESS);
  cw_r2d_polygon(mid, 6, placed ? rgba(g[0], g[1], g[2], 1) : rgba(1, 1, 1, 0.25f));
}

static void draw_panel(int side, float x, float y, float w, float s) {
  const sw_state *st = &G.m.st;
  bool turn = G.m.phase != PH_OVER && st->to_move == side;
  cw_r2d_round_rect((cw_rect){ x, y, w, s * 5.4f }, s * 0.5f, 8, rgba(0.05f, 0.04f, 0.035f, turn ? 0.78f : 0.55f));
  if (turn) cw_r2d_round_rect_lines((cw_rect){ x, y, w, s * 5.4f }, s * 0.5f, s * 0.12f, 8, rgba(0.95f, 0.80f, 0.45f, 0.9f));
  cw_color ink = side == 0 ? rgba(0.95f, 0.92f, 0.84f, 1) : rgba(0.80f, 0.70f, 0.62f, 1);
  text(seat_label(side), x + s * 0.7f, y + s * 0.5f, s * 1.0f, ink, CW_ALIGN_LEFT);
  draw_pressure(side, x + w - s * 2.0f, y + s * 2.9f, s * 0.55f);
  /* the hand: how many of each kind are left */
  for (int k = 0; k < SW_KINDS; k++) {
    const float *c = art_inlay_color(k);
    float cx = x + s * 1.5f + (float)k * s * 3.1f, cy = y + s * 2.6f;
    cw_r2d_circle((cw_vec2){ cx, cy }, s * 0.42f, 16, rgba(c[0], c[1], c[2], sw_in_hand(st, side, k) ? 1 : 0.25f));
    char num[8];
    snprintf(num, sizeof num, "%d", sw_in_hand(st, side, k));
    text(num, cx + s * 0.6f, cy - s * 0.45f, s * 0.85f, ink, CW_ALIGN_LEFT);
    text(sw_kind_name(k), cx + s * 0.45f, cy + s * 0.55f, s * 0.8f, rgba(ink.r, ink.g, ink.b, 0.8f), CW_ALIGN_CENTER);
  }
  char wins[48];
  snprintf(wins, sizeof wins, "wins %d", G.m.wins[side]);
  text(wins, x + s * 0.7f, y + s * 4.1f, s * 0.85f, rgba(ink.r, ink.g, ink.b, 0.85f), CW_ALIGN_LEFT);
}

static const char *HOW_TO[2] = {
  "Surround the other Empress on all six sides to win.\n"
  "Each turn, place a piece from your tray or move one of yours.\n"
  "A new piece must touch your own pieces and none of theirs.\n"
  "Your Empress must be down by your fourth turn; until then nothing moves.\n"
  "The pieces must always stay in one group, and a piece cannot squeeze\n"
  "through a gap too narrow for it.",
  "Empress: one step.\n"
  "Ram: one step, and may climb on top of other pieces.\n"
  "Locust: jumps in a straight line over pieces to the next empty space.\n"
  "Weaver: exactly three steps around the group.\n"
  "Runner: any number of steps around the group.\n"
  "Legal choices glow on the table. A ghost shows the piece before you commit.",
};

static void draw_help(cw_vec2 sz, bool in_play) {
  float s = sz.y * 0.03f;
  cw_rect r = { sz.x * 0.17f, sz.y * 0.16f, sz.x * 0.66f, sz.y * 0.62f };
  cw_r2d_round_rect(r, s, 10, rgba(0.07f, 0.06f, 0.05f, 0.92f));
  cw_r2d_round_rect_lines(r, s, s * 0.12f, 10, rgba(0.90f, 0.75f, 0.42f, 0.9f));
  text(G.help_page == 0 ? "How to play  (1 of 2)" : "The pieces  (2 of 2)", r.x + s, r.y + s * 0.8f, s * 1.3f, rgba(0.98f, 0.86f, 0.55f, 1), CW_ALIGN_LEFT);
  cw_text_style st = { .size = s * 0.95f, .color = rgba(0.93f, 0.90f, 0.83f, 1), .line_spacing = 1.35f };
  /* line by line, so each piece's colour dot sits on its own line */
  const char *t = HOW_TO[G.help_page];
  for (int line = 0; *t; line++) {
    const char *end = strchr(t, '\n');
    size_t len = end ? (size_t)(end - t) : strlen(t);
    char buf[160];
    if (len >= sizeof buf) CW_FATAL("mandible: a how-to line is too long");
    memcpy(buf, t, len);
    buf[len] = 0;
    float y = r.y + s * 3.0f + (float)line * s * 1.55f;
    cw_r2d_text(cw_ui_font_sans(), buf, ((cw_vec2){ r.x + s * 1.2f, y }), &st);
    if (G.help_page == 1 && line < SW_KINDS) {
      const float *c = art_inlay_color(line);
      cw_r2d_circle((cw_vec2){ r.x + s * 0.65f, y + s * 0.55f }, s * 0.25f, 12, rgba(c[0], c[1], c[2], 1));
    }
    t += len + (end ? 1 : 0);
  }
  /* on the title page the shell draws the way back: only the page turn is offered there */
  cw_prompt_text(cw_ui_font_sans(), in_play ? "{left|right} page    {confirm} play" : "{ui_left|ui_right} page", -1, (cw_vec2){ sz.x * 0.5f, r.y + r.h - s * 1.8f }, s * 0.95f,
                 rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
}

static const char *const CREDIT_LINES[] = {
  "Rules, pieces, insect carvings and table made for this game, in code",
  "Built with cartwheel-engine (MIT)",
  "Code: MIT",
  NULL,
};
static const bk_credits CREDITS = { .title = "Mandible", .inspired_by = "Inspired by Hive", .lines = CREDIT_LINES };

static void draw_hud(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float s = sz.y * 0.026f;
  draw_panel(0, s, s, s * 19.5f, s);
  draw_panel(1, sz.x - s * 20.5f, s, s * 19.5f, s);
  const sw_state *st = &G.m.st;
  int side = st->to_move;
  bool human = G.m.cpu[side] < 0;
  char line[160];
  /* the caption at the top middle: whose turn and what to do */
  if (G.m.phase == PH_OVER) {
    const char *w = st->result == SW_WHITE_WINS ? "Ivory wins: the Ebony Empress is surrounded"
                    : st->result == SW_BLACK_WINS ? "Ebony wins: the Ivory Empress is surrounded"
                    : sw_drawn_by_repetition(st) ? "A draw: the same position came up three times"
                    : st->ply >= SW_MAX_PLY ? "A draw: the game ran its full length" : "A draw: both Empresses are surrounded";
    float a = fminf(G.banner_t / 0.6f, 1); /* swells in, never flashes */
    cw_r2d_round_rect((cw_rect){ sz.x * 0.2f, sz.y * 0.40f, sz.x * 0.6f, s * 4.6f }, s * 0.6f, 8, rgba(0.06f, 0.05f, 0.04f, 0.85f * a));
    text(w, sz.x * 0.5f, sz.y * 0.40f + s * 0.8f, s * 1.4f, rgba(0.98f, 0.86f, 0.55f, a), CW_ALIGN_CENTER);
    bool any_human = G.m.cpu[0] < 0 || G.m.cpu[1] < 0;
    if (any_human && G.m.over_ticks >= 60)
      cw_prompt_text(cw_ui_font_sans(), "{confirm} play again", -1, (cw_vec2){ sz.x * 0.5f, sz.y * 0.40f + s * 2.9f }, s * 1.0f,
                     rgba(1, 1, 1, a), CW_ALIGN_CENTER);
    return;
  }
  bool must_empress = st->turns[side] == 3 && !sw_empress_placed(st, side);
  if (!human) snprintf(line, sizeof line, "%s is thinking", SIDE_NAME[side]);
  else if (G.m.phase == PH_SELECT) snprintf(line, sizeof line, "%s to play: %s", SIDE_NAME[side], must_empress ? "your Empress must go down now" : "choose a piece");
  else {
    int src = G.m.src;
    const char *nm = src >= SRC_HAND && src < SRC_PASS ? sw_kind_name(src - SRC_HAND) : src < SW_PIECES ? sw_kind_name(st->p[src].kind) : "";
    snprintf(line, sizeof line, "%s to play: where does the %s go?", SIDE_NAME[side], nm);
  }
  text(line, sz.x * 0.5f, s * 0.9f, s * 1.15f, rgba(0.97f, 0.94f, 0.86f, 1), CW_ALIGN_CENTER);
  if (G.last_label[0]) {
    snprintf(line, sizeof line, "%s", G.last_label);
    text(line, sz.x * 0.5f, s * 2.55f, s * 0.95f, rgba(0.93f, 0.91f, 0.86f, 0.95f), CW_ALIGN_CENTER); /* light ink on the felt: readable from the couch */
  }
  if (human && !G.m.help) {
    sw_pick p[MAX_PICKS];
    int n = match_picks(&G.m, p, MAX_PICKS);
    const char *prompt = n == 1 && p[0].src == SRC_PASS ? "No legal move.   {confirm} pass"
                         : G.m.phase == PH_SELECT ? "{left|right} {up|down} choose    {prev|next} cycle    {confirm} pick up    {help} rules"
                                                  : "{left|right} {up|down} choose a space    {confirm} place it    {back} put it back";
    cw_prompt_text(cw_ui_font_sans(), prompt, match_seat_of_side(&G.m, side), (cw_vec2){ sz.x * 0.5f, sz.y - s * 2.2f }, s * 0.95f,
                   rgba(1, 1, 1, 0.92f), CW_ALIGN_CENTER);
  }
}

/* the title's How to play page (bk_title): the card play opens, both pages */
static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  draw_help(canvas, false);
}

/* the menus' tick (bk_title; a scene app's tree and its fixed_update are paused under the menus): the test switch
 * that skips them, and the How to play page's turn (ui_left / ui_right, which the shell leaves to the page) */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (screen == CW_SHELL_TITLE) {
    G.help_page = 0;
    if (!G.started_by_env) start_from_env();
  }
  if (screen == CW_SHELL_PAGE && cw_shell_page() == 0) {
    static cw_action left = -1, right = -1;
    if (left < 0) left = cw_action_find("ui_left"), right = cw_action_find("ui_right");
    if (left < 0 || right < 0) CW_FATAL("mandible: the shell's ui_left / ui_right actions are missing");
    for (int p = 0; p < CW_INPUT_MAX_PLAYERS; p++) {
      if (cw_action_just_pressed(p, right)) G.help_page = 1;
      if (cw_action_just_pressed(p, left)) G.help_page = 0;
    }
  }
}

static const bk_title TITLE = { .credits = &CREDITS, .how_to_play = how_to_play, .menu_update = menu_update };

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  static double last = 0;
  double now = cw_time();
  float dt = last > 0 ? (float)(now - last) : 1.0f / 60;
  last = now;
  G.frame_dt = fminf(dt, 0.1f);
  update_view(fminf(dt, 0.1f));
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 sz = cw_r2d_canvas_size();
  bool play = cw_shell_screen_now() == CW_SHELL_PLAY;
  if (play && cw_shell_hud_shown() && !bk_hud_hidden()) {
    draw_hud();
    if (G.m.help) draw_help(sz, true);
  }
  cw_r2d_end();
  cw_shell_draw();
}

static void init(void *user) {
  (void)user;
  cw_shell_desc d = { .title = "Mandible", .subtitle = "surround the Empress", .max_players = 2, .cpu_seats = true, .hot_seats = true,
                      .start = start, .pause_items = { "How to play" }, .pause_item = pause_item };
  d.seat_names[0] = SIDE_NAME[0], d.seat_names[1] = SIDE_NAME[1];
  d.seat_colors[0] = (cw_color){ 0.92f, 0.88f, 0.78f, 1 }, d.seat_colors[1] = (cw_color){ 0.45f, 0.30f, 0.22f, 1 };
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  cw_input_pointer_channel(true); /* clicks and taps go into the recording, the replay and the input hash */
  match_bind_actions(G.act);
  build_scene();
  for (int i = 0; i < SW_PIECES; i++) {
    sw_state s;
    sw_init(&s);
    G.shown[i] = rest_pos(&s, i);
  }
  cw_debug_i32("game.turn", &G.turn);
  cw_debug_i32("game.ply", &G.ply);
  cw_debug_i32("game.result", &G.result);
  cw_debug_i32("game.phase", &G.phase);
  cw_debug_i32("game.picks", &G.picks);
  cw_debug_i32("game.cursor", &G.cursor);
  cw_debug_field("game.wins", G.m.wins, CW_DBG_I32, 3);
  cw_debug_i32("game.games", &G.m.games);
  cw_debug_i32("game.cpu_moves", &G.m.cpu_moves);
  cw_debug_i32("game.human_moves", &G.m.human_moves);
  cw_debug_i32("game.stray", &G.m.stray);
  cw_debug_i32("game.ticks", &G.ticks);
  cw_debug_i32("game.state_hash", &G.state_hash);
  cw_debug_i32("game.help", &G.m.help);
  cw_debug_i32("game.help_page", &G.help_page);
  cw_debug_i32("game.replaying", &G.replaying);
  cw_debug_i32("game.replay_done", &G.replay_done);
  cw_debug_i32("game.replay_match", &G.replay_match);
  cw_debug_i32("game.replay_hash_match", &G.replay_hash_match);
  cw_debug_i32("game.pointer_picks", &G.pointer_picks);
  cw_debug_field("game.pick_px", G.pick_px, CW_DBG_F32, 64);
  cw_debug_field("game.pick_code", G.pick_code, CW_DBG_I32, 32);
  cw_debug_i32("game.newcomer_moves", &NB.moves);
  cw_debug_i32("game.newcomer_kinds", &NB.kinds);
  cw_debug_field("game.person_kinds", G.person_kinds, CW_DBG_I32, MT_COUNT);
  cw_debug_command("setup", "setup moves|pass: go on from a test position, Ivory to choose (moves: place, step or climb; pass: only the pass)", cmd_setup);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (game.replay_match)", cmd_replay);
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "Mandible", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .render = render, .debug = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
