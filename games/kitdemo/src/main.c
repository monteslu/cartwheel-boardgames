/* kitdemo: the board-game kit's reference game (docs/boardkit.md). Two players, people or CPUs, at one table:
 * take a card from a market of three, then bank it or roll a die for double (rules.h). It uses every part of
 * the kit: the shell lobby (pads, hot seats, CPUs), bk_match (turns, undo, chance), bk_cpu (search, pressing the
 * pad's actions), bk_select (pad cursor and recorded pointer picks), bk_card (faces drawn once, slabs, flips),
 * bk_view (counters, score deltas, a ghost preview, the die's throw), bk_shell (the title's How to play and Credits
 * pages, the test start in the menu tick). Game authors copy its
 * layout: rules.c pure, game.c the match, main.c the view.
 *
 *   kitdemo        the game
 *   kitdemo_auto   two CPUs play match after match (cart-smoke runs it)
 *
 * Test switches (cw_test_env): KITDEMO_SEATS=p,c1 (bk_lobby_test_start: p a pad, h a hot seat, c0..c2 a CPU),
 * KITDEMO_SEED=<n>. Debug command `replay`: replay the match so far from its own input recording and compare
 * the state (kd.replay_match) and the input hash (kd.replay_hash_match); CW_TEST_SET_UNRECORDED is the engine's
 * control for it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_view.h"
#include "cartwheel/debug.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/ui.h"
#include "game.h"

#define CARD_W 1.0f
#define CARD_H 1.4f

static const char *const SEAT_NAMES[2] = { "Amber", "Slate" };
static const uint32_t CARD_COL[3] = { 0xb8323aff, 0x1f8a8aff, 0xd08a1eff }; /* crimson, teal, amber; each its own shape too */

static struct {
  kd_game g;
  cw_tree *tree;
  cw_node world, cam, card[KD_CARDS], ghost, die, coin, marker, target[KD_TARGETS];
  bk_pose pose[KD_CARDS];
  bk_counter shown[2];
  bk_deltas deltas;
  bk_die_throw throw_;
  float throw_t;
  bool throwing, playing, fresh, started_by_env;
  int32_t target_px[2 * KD_TARGETS]; /* each target's canvas point (tests tap there) */
  int log_at, last_mover;
  uint64_t start_seed;
  /* debug */
  int32_t phase, turn, score[2], moves, undos, pointer_picks, pad_moves, cpu_moves, matches, wins[3], throws, ghost_shown,
      deltas_shown, faces_made, winner;
  uint32_t hash;
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint32_t want_hash, want_input;
  uint8_t *recording;
  size_t recording_len;
} V;

/* ── table layout ── */
static t3_vec3 slot_at(int i) { return (t3_vec3){ -1.5f + 1.5f * (float)i, 0.012f, -0.9f }; }
static t3_vec3 deck_at(int k) { return (t3_vec3){ -4.0f, 0.012f + 0.014f * (float)k, -0.9f }; }
static t3_vec3 held_at(void) { return (t3_vec3){ 0, 0.35f, 1.25f }; }
static t3_vec3 pile_at(int seat, int k) { return (t3_vec3){ seat ? 4.3f : -4.3f, 0.012f + 0.014f * (float)k, 2.0f }; }
static const t3_vec3 COIN_AT = { 3.0f, 0.06f, -0.9f }, DIE_AT = { 4.4f, 0.2f, -0.9f };

/* ── the card painter: faces drawn once into textures ── */
static void paint(int face, cw_vec2 px, void *user) {
  (void)user;
  float w = px.x, h = px.y;
  if (face >= BK_FACE_BACK) { /* the back: deep blue with a lattice */
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0x1d2f5aff));
    for (int i = -8; i < 16; i++) cw_r2d_line(cw_v2((float)i * w / 8, 0), cw_v2((float)i * w / 8 + h, h), 3, cw_hex(0x2f4a85ff));
    for (int i = -8; i < 16; i++) cw_r2d_line(cw_v2((float)i * w / 8, h), cw_v2((float)i * w / 8 + h, 0), 3, cw_hex(0x2f4a85ff));
    cw_r2d_rect_lines(cw_rect_make(w * 0.06f, w * 0.06f, w * 0.88f, h - w * 0.12f), w * 0.03f, cw_hex(0xd9c48aff));
    return;
  }
  int v = kd_value(face), c = kd_colour(face);
  cw_color col = cw_hex(CARD_COL[c]);
  cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0xf3ecdcff));
  cw_r2d_rect_lines(cw_rect_make(w * 0.05f, w * 0.05f, w * 0.9f, h - w * 0.1f), w * 0.035f, col);
  char num[4];
  snprintf(num, sizeof num, "%d", v);
  cw_text_style st = { .size = h * 0.42f, .color = col, .fixed_size = true, .own_backdrop = true };
  cw_text_metrics m = cw_font_measure(cw_ui_font_sans(), num, &st);
  cw_r2d_text(cw_ui_font_sans(), num, cw_v2((w - m.width) * 0.5f, h * 0.12f), &st);
  /* v marks of the colour's own shape: circle, square, triangle */
  float r = w * 0.075f, y = h * 0.78f;
  for (int i = 0; i < v; i++) {
    float x = w * 0.5f + ((float)i - (float)(v - 1) * 0.5f) * r * 2.6f;
    if (c == 0) cw_r2d_circle(cw_v2(x, y), r, 32, col);
    else if (c == 1) cw_r2d_rect(cw_rect_make(x - r * 0.9f, y - r * 0.9f, r * 1.8f, r * 1.8f), col);
    else {
      cw_vec2 t[3] = { { x, y - r * 1.1f }, { x + r * 1.05f, y + r * 0.8f }, { x - r * 1.05f, y + r * 0.8f } };
      cw_r2d_polygon(t, 3, col);
    }
  }
}

static cw_node mesh(cw_node parent, const char *name, const char *prim, t3_vec3 at, t3_vec3 size, uint32_t rgb, float rough) {
  cw_node m = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, m);
  cw_node_set(m, "primitive", cw_val_enum(prim));
  cw_node_set(m, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_color c = cw_hex(rgb);
  cw_node_set(m, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(m, "roughness", cw_val_float(rough));
  cw_node3d_set_position(m, at);
  return m;
}

static const t3_quat FACE_UP = { 0, 0, 0, 1 };
static t3_quat face_down(void) { return t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, (float)M_PI); }

/* where each card belongs in this state (the view eases toward it) */
static bk_pose card_target(const kd_state *s, int c) {
  for (int i = 0; i < KD_SLOTS; i++)
    if (s->market[i] == c) return (bk_pose){ slot_at(i), FACE_UP };
  if (s->card == c) return (bk_pose){ held_at(), t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, 0.5f) };
  if (s->owner[c] != 0xff) {
    int k = 0;
    for (int j = 0; j < c; j++) k += s->owner[j] == s->owner[c];
    return (bk_pose){ pile_at(s->owner[c], k), FACE_UP };
  }
  int k = 0;
  for (int j = 0; j < s->deck_n; j++) k += s->deck[j] < c;
  return (bk_pose){ deck_at(k), face_down() };
}

/* ── shell ── */
static void start(int players, void *user) {
  (void)players, (void)user;
  const char *sd = cw_test_env("KITDEMO_SEED");
  V.start_seed = sd ? strtoull(sd, NULL, 10) : 2026u + (uint64_t)V.g.matches;
  kd_game_start(&V.g, V.start_seed);
  for (int s = 0; s < 2; s++) bk_counter_init(&V.shown[s], 0);
  V.log_at = 0;
  V.throwing = false;
  V.fresh = true;
  V.replaying = V.replay_done = 0;
  for (int c = 0; c < KD_CARDS; c++) V.pose[c] = card_target(kd_game_state(&V.g), c);
  cw_input_record_begin(); /* the whole match, CPU presses and pointer picks included */
}

static const char *const CREDIT_LINES[] = {
  "A reference game for the cartwheel board-game kit.",
  "Code, rules and art: this repository (MIT). The card faces, table and die are drawn by code in games/kitdemo/src.",
  "Font: Fira Sans (the engine's built-in sans), SIL Open Font License 1.1.",
  NULL,
};
static const bk_credits CREDITS = { .title = "Kit Demo", .inspired_by = "", .lines = CREDIT_LINES };

/* the title's How to play page (bk_title): a card over the backdrop, prompts for every device */
static void how_to_play(cw_vec2 sz, void *user) {
  (void)user;
  cw_font f = cw_ui_font_sans();
  float w = sz.x * 0.7f, h = sz.y * 0.62f, x = (sz.x - w) * 0.5f, y = sz.y * 0.14f, ts = sz.y * 0.04f;
  cw_r2d_rect(cw_rect_make(x, y, w, h), cw_rgba(0.95f, 0.92f, 0.85f, 0.97f));
  cw_color ink = cw_rgba(0.13f, 0.1f, 0.08f, 1);
  cw_r2d_text(f, "How to play", cw_v2(x, y + ts), &(cw_text_style){ .size = ts * 1.4f, .color = ink, .wrap_width = w, .align = CW_ALIGN_CENTER });
  static const char *const LINES[] = {
    "{left|right} choose a card from the market    {ok} take it",
    "Bank it for its points, or roll: at or over its value doubles them",
    "{back} puts a taken card back",
    "When the market is empty, the higher score wins",
  };
  for (int i = 0; i < 4; i++)
    cw_prompt_text(f, LINES[i], -1, cw_v2(sz.x * 0.5f, y + ts * (3.6f + 1.8f * (float)i)), ts * 0.72f, ink, CW_ALIGN_CENTER);
}

/* the menus' tick (bk_title): the self-playing cart starts its match from the title */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
#ifdef KITDEMO_AUTO
  if (screen == CW_SHELL_TITLE && !V.started_by_env && cw_frame_index() >= 1) {
    static const int cpu[2] = { 2, 1 };
    cw_shell_start_seats(2, cpu);
    V.started_by_env = true;
  }
#else
  (void)screen;
#endif
}

static const bk_title TITLE = { .test_env = "KITDEMO_SEATS", .credits = &CREDITS, .how_to_play = how_to_play,
                                .menu_update = menu_update };

static void cmd_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (cw_shell_screen_now() != CW_SHELL_PLAY || !cw_input_recording_active()) {
    cw_reply_error(r, "replay needs a match in play");
    return;
  }
  V.want_hash = (uint32_t)bk_match_hash(V.g.match);
  cw_input_record_end();
  V.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  free(V.recording);
  V.recording = malloc(len ? len : 1);
  memcpy(V.recording, d, len);
  V.recording_len = len;
  kd_game_start(&V.g, V.start_seed); /* the match from its start; the recording drives it */
  if (!cw_input_replay_begin(V.recording, V.recording_len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  V.replaying = 1;
  V.replay_done = V.replay_match = V.replay_hash_match = 0;
  cw_reply_printf(r, "{\"bytes\":%zu,\"state\":%u,\"input\":%u}", len, V.want_hash, V.want_input);
}

static void init(void *user) {
  (void)user;
  cw_shell_desc d = { .title = "Kit Demo", .subtitle = "take a card, bank it or roll for double", .start = start };
  bk_lobby_desc(&d, 2, SEAT_NAMES, NULL);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  kd_game_init(&V.g);
  V.tree = cw_scene_app_tree();
  V.world = cw_node_get(cw_tree_root(V.tree), "World");
  V.cam = cw_node_get(V.world, "Camera");
  cw_node3d_look_at(V.cam, (t3_vec3){ 0, 0, 0.75f }, (t3_vec3){ 0, 1, 0 });
  /* the table: a walnut slab and a green felt mat on it (2 cm apart: no shared plane) */
  mesh(V.world, "Table", "BOX", (t3_vec3){ 0, -0.22f, 0.4f }, (t3_vec3){ 13, 0.4f, 8.2f }, 0x3b2110ff, 0.5f);
  cw_node felt = mesh(V.world, "Felt", "BOX", (t3_vec3){ 0, -0.01f, 0.4f }, (t3_vec3){ 12.2f, 0.02f, 7.4f }, 0x123d29ff, 0.95f);
  cw_node_set(felt, "receive_shadow", cw_val_bool(true));
  bk_cards_init(V.tree, paint, NULL);
  for (int c = 0; c < KD_CARDS; c++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Card%d", c);
    V.card[c] = bk_card_new(V.world, nm, &(bk_card_desc){ .width = CARD_W, .height = CARD_H, .thickness = 0.014f, .corner = 0.09f,
                                                           .front = c, .back = BK_FACE_BACK, .face_px = 320 });
  }
  V.ghost = bk_card_new(V.world, "Ghost", &(bk_card_desc){ .width = CARD_W, .height = CARD_H, .thickness = 0.014f, .corner = 0.09f,
                                                            .front = 0, .back = BK_FACE_BACK, .face_px = 320 });
  cw_node_set(V.ghost, "visible", cw_val_bool(false));
  if (!bk_ghost_set(V.ghost, 0.45f)) CW_FATAL("kitdemo: the ghost card has no mesh");
  V.coin = mesh(V.world, "Coin", "CYLINDER", COIN_AT, (t3_vec3){ 0.8f, 0.12f, 0 }, 0xd4a640ff, 0.3f);
  cw_node_set(V.coin, "metalness", cw_val_float(0.9f));
  cw_node_set(V.coin, "cast_shadow", cw_val_bool(true));
  V.die = bk_die_node(V.world, "Die", 0.4f, cw_hex(0xf1e9d6ff), cw_hex(0x24201bff));
  cw_node3d_set_position(V.die, DIE_AT);
  V.marker = mesh(V.world, "Marker", "BOX", (t3_vec3){ 0, 0.02f, 0 }, (t3_vec3){ 1.1f, 0.03f, 0.08f }, 0xffffffff, 0.4f);
  cw_node_set(V.marker, "layers", cw_val_int(2));
  cw_node_set(V.cam, "cull_layers", cw_val_int(3));
  /* the select targets: fixed points where the slots, the coin and the die rest */
  t3_vec3 at[KD_TARGETS] = { slot_at(0), slot_at(1), slot_at(2), COIN_AT, DIE_AT };
  for (int t = 0; t < KD_TARGETS; t++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Target%d", t);
    V.target[t] = cw_node_new("Node3D", nm);
    cw_node_add_child(V.world, V.target[t]);
    cw_node3d_set_position(V.target[t], at[t]);
  }
  cw_cursors_set_nodes(bk_select_cursors(V.g.sel), V.target, KD_TARGETS);
  bk_deltas_init(&V.deltas);
  cw_debug_i32("kd.phase", &V.phase);
  cw_debug_i32("kd.turn", &V.turn);
  cw_debug_field("kd.score", V.score, CW_DBG_I32, 2);
  cw_debug_i32("kd.moves", &V.moves);
  cw_debug_u32("kd.hash", &V.hash);
  cw_debug_i32("kd.undos", &V.undos);
  cw_debug_i32("kd.pointer_picks", &V.pointer_picks);
  cw_debug_i32("kd.pad_moves", &V.pad_moves);
  cw_debug_i32("kd.cpu_moves", &V.cpu_moves);
  cw_debug_i32("kd.matches", &V.matches);
  cw_debug_field("kd.wins", V.wins, CW_DBG_I32, 3);
  cw_debug_i32("kd.winner", &V.winner);
  cw_debug_i32("kd.throws", &V.throws);
  cw_debug_i32("kd.ghost_shown", &V.ghost_shown);
  cw_debug_i32("kd.deltas_shown", &V.deltas_shown);
  cw_debug_i32("kd.faces_made", &V.faces_made);
  cw_debug_field("kd.target_px", V.target_px, CW_DBG_I32, 2 * KD_TARGETS);
  cw_debug_i32("kd.replaying", &V.replaying);
  cw_debug_i32("kd.replay_done", &V.replay_done);
  cw_debug_i32("kd.replay_match", &V.replay_match);
  cw_debug_i32("kd.replay_hash_match", &V.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (kd.replay_match)", cmd_replay);
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  if (!cw_shell_update()) return;
  if (V.fresh) { V.fresh = false; return; } /* the start tick is not recorded: play begins at the next */
  if (V.replaying && !cw_input_replay_active()) {
    V.replay_match = (uint32_t)bk_match_hash(V.g.match) == V.want_hash;
    V.replay_hash_match = cw_input_replay_hash() == V.want_input;
    V.replaying = 0;
    V.replay_done = 1;
    cw_info("kitdemo replay: state %s, input hash %s", V.replay_match ? "same" : "DIFFERENT", V.replay_hash_match ? "same" : "DIFFERENT");
  }
  /* the cursors read directions and pointer points on the camera's picture (set in the tick, before the CPUs walk their cursors) */
  cw_view3d view;
  if (cw_canvas_views3d(V.tree, &view, 1) == 1) {
    for (int p = 0; p < 2; p++) cw_cursors_set_view(bk_select_cursors(V.g.sel), p, &view);
    for (int t = 0; t < KD_TARGETS; t++) {
      cw_vec2 q = cw_cursors_screen(bk_select_cursors(V.g.sel), 0, t);
      V.target_px[2 * t] = (int32_t)lroundf(q.x), V.target_px[2 * t + 1] = (int32_t)lroundf(q.y);
    }
  }
  kd_game_tick(&V.g);
  const kd_state *s = kd_game_state(&V.g);
  if (bk_match_over(V.g.match) && V.g.over_ticks > 240) {
#ifdef KITDEMO_AUTO
    start(2, NULL); /* the next match */
#else
    for (int p = 0; p < 2; p++)
      if (cw_action_just_pressed(p, V.g.ok)) start(2, NULL);
#endif
  }
  V.phase = s->phase, V.turn = s->turn, V.score[0] = s->score[0], V.score[1] = s->score[1], V.moves = s->moves;
  V.hash = (uint32_t)bk_match_hash(V.g.match);
  V.undos = V.g.undos, V.pointer_picks = V.g.pointer_picks, V.pad_moves = V.g.pad_moves, V.cpu_moves = V.g.cpu_moves;
  V.matches = V.g.matches, V.wins[0] = V.g.wins[0], V.wins[1] = V.g.wins[1], V.wins[2] = V.g.wins[2];
  V.winner = kd_winner(s);
  V.playing = true;
}

/* per frame: the pointer (a tap queued for the next tick), the log turned into animations */
static void update(void *user, double dt) {
  (void)user;
  if (!V.playing || !V.g.match) return;
  const kd_state *s = kd_game_state(&V.g);
  int turn = bk_match_turn(V.g.match);
  if (turn >= 0 && !kd_game_is_cpu(&V.g, turn)) bk_select_pointer(V.g.sel, turn, V.tree);
  bk_log_entry log[32];
  int n = bk_match_log(V.g.match, V.log_at, log, 32);
  for (int i = 0; i < n; i++) {
    const bk_log_entry *e = &log[i];
    V.log_at = e->index + 1;
    if (e->kind == BK_LOG_MOVE) V.last_mover = e->seat;
    if (e->kind == BK_LOG_CHANCE && e->move >= KD_MV_FACE(1) && e->move <= KD_MV_FACE(6)) { /* the die rolled */
      int seat = V.last_mover; /* who rolled: the seat of the move before the roll */
      V.throw_ = bk_die_throw_make((t3_vec3){ seat ? 3.5f : -3.5f, 1.4f, 2.4f }, DIE_AT, (int)(e->move - KD_MV_FACE(0)), e->hash);
      V.throw_t = 0;
      V.throwing = true;
      V.throws++;
    }
  }
  /* scores: a roll's change shows when the die lands; a bank's at once */
  for (int p = 0; p < 2; p++) {
    if (V.throwing || s->score[p] == V.shown[p].target) continue;
    int gain = s->score[p] - V.shown[p].target;
    bk_counter_set(&V.shown[p], s->score[p]);
    cw_vec2 sz = cw_r2d_canvas_size();
    bk_deltas_add(&V.deltas, cw_v2(p ? sz.x * 0.86f : sz.x * 0.14f, sz.y * 0.27f), gain, bk_seat_color(p));
    V.deltas_shown++;
  }
  if (V.throwing) {
    V.throw_t += (float)dt;
    t3_vec3 p;
    t3_quat q;
    bk_die_pose(&V.throw_, V.throw_t, &p, &q);
    cw_node3d_set_position(V.die, p);
    cw_node3d_set_quaternion(V.die, q);
    if (V.throw_t >= V.throw_.seconds + 0.35f) V.throwing = false;
  }
  for (int p = 0; p < 2; p++) bk_counter_update(&V.shown[p], (float)dt);
  bk_deltas_update(&V.deltas, (float)dt);
  bk_select_update(V.g.sel, (float)dt);
  /* cards ease toward where the state puts them; a card turning over lifts as it turns */
  for (int c = 0; c < KD_CARDS; c++) {
    bk_pose to = card_target(s, c);
    float dq = fabsf(V.pose[c].rot.x * to.rot.x + V.pose[c].rot.y * to.rot.y + V.pose[c].rot.z * to.rot.z + V.pose[c].rot.w * to.rot.w);
    if (dq < 0.98f) to.pos.y += 0.6f * (1 - dq);
    V.pose[c] = bk_pose_approach(V.pose[c], to, 7.0f, (float)dt);
    cw_node3d_set_position(V.card[c], V.pose[c].pos);
    cw_node3d_set_quaternion(V.card[c], V.pose[c].rot);
  }
  /* the ghost: a person's cursor on a market card shows where it would go */
  int tgt = turn >= 0 ? bk_select_target(V.g.sel, turn) : -1;
  bool ghost = turn >= 0 && !kd_game_is_cpu(&V.g, turn) && s->phase == KD_PICK && tgt >= 0 && tgt < KD_SLOTS &&
               s->market[tgt] != BK_NO_CARD;
  if (ghost) {
    bk_card_set_front(V.ghost, s->market[tgt]);
    cw_node3d_set_position(V.ghost, held_at());
    cw_node3d_set_quaternion(V.ghost, t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, 0.5f));
    V.ghost_shown++;
  }
  cw_node_set(V.ghost, "visible", cw_val_bool(ghost));
  /* the cursor's marker: a bar in the seat's colour under the target it is on */
  bool show = turn >= 0 && tgt >= 0 && !bk_match_over(V.g.match);
  cw_node_set(V.marker, "visible", cw_val_bool(show));
  if (show) {
    t3_vec3 at = cw_node3d_position(V.target[tgt]);
    float lift = 0.05f * sinf((float)cw_time() * 2.0f); /* a slow bob, not a flash */
    cw_node3d_set_position(V.marker, (t3_vec3){ at.x, 0.03f + lift * 0.2f, at.z + (tgt < KD_SLOTS ? CARD_H * 0.5f + 0.15f : 0.55f) });
    cw_color c = bk_seat_color(turn);
    cw_node_set(V.marker, "color", cw_val_color(c.r, c.g, c.b, 1));
    cw_node_set(V.marker, "emissive", cw_val_color(c.r * 0.5f, c.g * 0.5f, c.b * 0.5f, 1));
  }
  V.faces_made = bk_card_faces_made();
}

static void hud(float dt) {
  cw_vec2 sz = cw_r2d_canvas_size();
  cw_font f = cw_ui_font_sans();
  const kd_state *s = kd_game_state(&V.g);
  int turn = bk_match_turn(V.g.match);
  float ts = sz.y * 0.04f;
  for (int p = 0; p < 2; p++) {
    char line[64];
    const bk_seat *st = &V.g.seat[p];
    snprintf(line, sizeof line, "%s%s", SEAT_NAMES[p],
             st->kind == BK_SEAT_CPU ? (st->level == 0 ? " (CPU Easy)" : st->level == 1 ? " (CPU Normal)" : " (CPU Hard)") : "");
    cw_text_style name = { .size = ts * 0.8f, .color = bk_seat_color(p), .outline_width = 2, .align = CW_ALIGN_CENTER };
    float cx = p ? sz.x * 0.86f : sz.x * 0.14f;
    cw_text_metrics m = cw_font_measure(f, line, &name);
    cw_r2d_text(f, line, cw_v2(cx - m.width * 0.5f, sz.y * 0.04f), &name);
    snprintf(line, sizeof line, "%d", bk_counter_value(&V.shown[p]));
    cw_text_style sc = { .size = ts * 1.6f, .color = CW_WHITE, .outline_width = 3 };
    m = cw_font_measure(f, line, &sc);
    cw_r2d_text(f, line, cw_v2(cx - m.width * 0.5f, sz.y * 0.09f), &sc);
    if (p == turn) cw_r2d_rect(cw_rect_make(cx - sz.x * 0.06f, sz.y * 0.205f, sz.x * 0.12f, sz.y * 0.006f), bk_seat_color(p));
  }
  bk_deltas_draw(&V.deltas, f, ts * 1.2f);
  cw_color ink = cw_rgba(1, 1, 1, 0.9f);
  cw_vec2 at = cw_v2(sz.x * 0.5f, sz.y * 0.92f);
  if (bk_match_over(V.g.match)) {
    int w = kd_winner(s);
    char line[64];
    if (w == 2) snprintf(line, sizeof line, "A draw, %d all", s->score[0]);
    else snprintf(line, sizeof line, "%s wins, %d to %d", SEAT_NAMES[w], s->score[w], s->score[w ^ 1]);
    cw_text_style big = { .size = ts * 1.5f, .color = CW_WHITE, .outline_width = 3 };
    cw_text_metrics m = cw_font_measure(f, line, &big);
    cw_r2d_text(f, line, cw_v2((sz.x - m.width) * 0.5f, sz.y * 0.4f), &big);
    if (V.g.over_ticks > 240) cw_prompt_text(f, "{ok} play again", -1, at, ts * 0.8f, ink, CW_ALIGN_CENTER);
  } else if (turn >= 0 && kd_game_is_cpu(&V.g, turn)) {
    char line[48];
    snprintf(line, sizeof line, "%s is thinking", SEAT_NAMES[turn]);
    cw_prompt_text(f, line, turn, at, ts * 0.8f, ink, CW_ALIGN_CENTER);
  } else if (s->phase == KD_PICK)
    cw_prompt_text(f, "{left|right} choose a card    {ok} take it", turn, at, ts * 0.8f, ink, CW_ALIGN_CENTER);
  else if (s->phase == KD_CHOOSE)
    cw_prompt_text(f, "{left|right} bank or roll    {ok} choose    {back} put it back", turn, at, ts * 0.8f, ink, CW_ALIGN_CENTER);
  if (s->last_die && !V.throwing && !bk_match_over(V.g.match)) {
    char line[48];
    snprintf(line, sizeof line, "rolled %d", s->last_die);
    cw_text_style st = { .size = ts * 0.7f, .color = ink, .outline_width = 2 };
    cw_vec2 p = cw_camera3d_world_to_screen(V.cam, (cw_rect){ 0, 0, sz.x, sz.y }, (t3_vec3){ DIE_AT.x, 0, DIE_AT.z + 0.6f }, NULL);
    cw_text_metrics m = cw_font_measure(f, line, &st);
    cw_r2d_text(f, line, cw_v2(p.x - m.width * 0.5f, p.y), &st);
  }
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  static double last;
  double now = cw_time();
  float dt = last > 0 ? (float)(now - last) : 0;
  last = now;
  if (V.playing && (cw_shell_screen_now() == CW_SHELL_PLAY || cw_shell_screen_now() == CW_SHELL_PAUSE) && cw_shell_hud_shown() && !bk_hud_hidden()) {
    cw_r2d_begin(&(cw_r2d_pass_desc){ 0 });
    hud(dt);
    cw_r2d_end();
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "Kit Demo", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .update = update, .render = render, .debug = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
