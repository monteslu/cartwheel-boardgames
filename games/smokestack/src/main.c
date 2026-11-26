/* Smokestack: lay track across a river valley and haul goods to the cities that want them. Two to four players,
 * people or CPUs, at one table (rules.h has the rules). rules.c is pure, game.c runs the match, view.c and hud.c
 * draw it.
 *
 *   smokestack        the game
 *   smokestack_auto   CPUs on every seat, match after match (cart-smoke runs it)
 *
 * Test switches (cw_test_env): SMOKESTACK_SEATS=p,c1 (bk_lobby_test_start: p a pad, h a hot seat, c0..c2 a CPU),
 * SMOKESTACK_SEED=<n>, SMOKESTACK_FAST=1 (CPUs without pauses). Debug command `replay`: replay the match so far
 * from its own input recording and compare the state (rl.replay_match) and the input hash (rl.replay_hash_match). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/ui.h"
#include "game.h"
#include "view.h"

static const char *const SEAT_NAMES[RL_MAX_PLAYERS] = { "Ember", "Tide", "Frost", "Fern" };

static struct {
  rl_game g;
  cw_tree *tree;
  cw_node world, cam;
  bool playing, fresh, started_by_env;
  uint64_t start_seed;
  int32_t phase, round, turn, score[RL_MAX_PLAYERS], hash_i, decisions, undos, pointer_picks, pad_moves, cpu_moves, matches,
      games_done, winner, help, tiles_shown, tiles_landed, trains_run, trains_arrived, ghost_shown, route_shown, glow_peaks,
      target, target_px[2], players;
  uint32_t hash;
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint32_t want_hash, want_input;
  uint8_t *recording;
  size_t recording_len;
} V;

static bool fast_mode(void) {
#ifdef SMOKESTACK_AUTO
  return true;
#else
  const char *f = cw_test_env("SMOKESTACK_FAST");
  return f && f[0] == '1';
#endif
}

static void start(int players, void *user) {
  (void)players, (void)user;
  const char *sd = cw_test_env("SMOKESTACK_SEED");
  V.start_seed = sd ? strtoull(sd, NULL, 10) : 2026u + (uint64_t)V.g.matches * 7919u + (uint64_t)cw_frame_index();
  rl_game_start(&V.g, V.start_seed, fast_mode());
  rl_view_start();
  V.fresh = true;
  V.replaying = V.replay_done = 0;
  cw_input_record_begin(); /* the whole match, CPU presses and pointer picks included */
}

static void pause_item(int index, void *user) {
  (void)user;
  if (index == 0) V.g.help_open = true;
}

static const char *const CREDIT_LINES[] = {
  "A railway game for two to four players, people or computer players.",
  "Code, rules, the valley maps and all art: this repository (MIT). The maps come from games/smokestack/tools/gen-map.mjs; the "
  "terrain, track, towns, trains and cards are drawn by code in games/smokestack/src.",
  "Font: Fira Sans (the engine's built-in sans), SIL Open Font License 1.1.",
  NULL,
};
static const bk_credits CREDITS = { .title = "Smokestack", .inspired_by = "Inspired by Steam", .lines = CREDIT_LINES };

static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  rl_view_help_page(canvas);
}

/* the menus tick (bk_title, after the kit reads SMOKESTACK_SEATS): the self-playing cart starts three CPUs when no
 * switch did. Frame 3: a host env request (sent at frame 2) is in by then. */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
#ifdef SMOKESTACK_AUTO
  if (screen == CW_SHELL_TITLE && !V.started_by_env && !bk_lobby_started_by_test() && cw_frame_index() >= 3) {
    static const int cpu[3] = { 1, 2, 0 };
    cw_shell_start_seats(3, cpu);
    V.started_by_env = true;
  }
#else
  (void)screen;
#endif
}

static const bk_title TITLE = { .test_env = "SMOKESTACK_SEATS", .credits = &CREDITS, .how_to_play = how_to_play,
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
  rl_game_start(&V.g, V.start_seed, fast_mode());
  rl_view_start();
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
  cw_shell_desc d = { .title = "Smokestack", .subtitle = "lay track, haul goods, light up the valley", .start = start,
                      .pause_items = { "How to play" }, .pause_item = pause_item };
  cw_color cols[RL_MAX_PLAYERS] = { cw_hex(0xe07b2aff), cw_hex(0x1fa39bff), cw_hex(0xe9e3d3ff), cw_hex(0x63a83aff) };
  bk_lobby_desc(&d, RL_MAX_PLAYERS, SEAT_NAMES, cols);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  V.tree = cw_scene_app_tree();
  V.world = cw_node_get(cw_tree_root(V.tree), "World");
  V.cam = cw_node_get(V.world, "Camera");
  rl_game_init(&V.g, V.world);
  rl_view_init(V.tree, V.world, V.cam, &V.g);
  cw_node_set(V.cam, "cull_layers", cw_val_int(3));
  cw_debug_i32("rl.players", &V.players);
  cw_debug_i32("rl.phase", &V.phase);
  cw_debug_i32("rl.round", &V.round);
  cw_debug_i32("rl.turn", &V.turn);
  cw_debug_field("rl.score", V.score, CW_DBG_I32, RL_MAX_PLAYERS);
  cw_debug_u32("rl.hash", &V.hash);
  cw_debug_i32("rl.decisions", &V.decisions);
  cw_debug_i32("rl.undos", &V.undos);
  cw_debug_i32("rl.pointer_picks", &V.pointer_picks);
  cw_debug_i32("rl.pad_moves", &V.pad_moves);
  cw_debug_i32("rl.cpu_moves", &V.cpu_moves);
  cw_debug_i32("rl.matches", &V.matches);
  cw_debug_i32("rl.games_done", &V.games_done);
  cw_debug_i32("rl.winner", &V.winner);
  cw_debug_i32("rl.help", &V.help);
  cw_debug_i32("rl.tiles_shown", &V.tiles_shown);
  cw_debug_i32("rl.tiles_landed", &V.tiles_landed);
  cw_debug_i32("rl.trains_run", &V.trains_run);
  cw_debug_i32("rl.trains_arrived", &V.trains_arrived);
  cw_debug_i32("rl.ghost_shown", &V.ghost_shown);
  cw_debug_i32("rl.route_shown", &V.route_shown);
  cw_debug_i32("rl.glow_peaks", &V.glow_peaks);
  cw_debug_i32("rl.target", &V.target);
  cw_debug_field("rl.target_px", V.target_px, CW_DBG_I32, 2);
  cw_debug_i32("rl.replaying", &V.replaying);
  cw_debug_i32("rl.replay_done", &V.replay_done);
  cw_debug_i32("rl.replay_match", &V.replay_match);
  cw_debug_i32("rl.replay_hash_match", &V.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (rl.replay_match)", cmd_replay);
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
    cw_info("smokestack replay: state %s, input hash %s", V.replay_match ? "same" : "DIFFERENT", V.replay_hash_match ? "same" : "DIFFERENT");
  }
  cw_view3d view;
  if (cw_canvas_views3d(V.tree, &view, 1) == 1)
    for (int p = 0; p < RL_MAX_PLAYERS; p++) cw_cursors_set_view(bk_select_cursors(V.g.sel), p, &view);
  rl_game_tick(&V.g);
  const rl_state *s = rl_game_state(&V.g);
  if (bk_match_over(V.g.match) && V.g.over_ticks > 300) {
#ifdef SMOKESTACK_AUTO
    start(V.g.players, NULL); /* the next match */
#else
    for (int p = 0; p < V.g.players; p++)
      if (!rl_game_is_cpu(&V.g, p) && cw_action_just_pressed(p, V.g.ok)) { start(V.g.players, NULL); break; }
#endif
  }
  V.players = s->players, V.phase = s->phase, V.round = s->round, V.turn = bk_match_turn(V.g.match);
  for (int p = 0; p < RL_MAX_PLAYERS; p++) V.score[p] = p < s->players ? rl_final_score(s, p) : 0;
  V.hash = (uint32_t)bk_match_hash(V.g.match);
  V.decisions = V.g.decisions, V.undos = V.g.undos, V.pointer_picks = V.g.pointer_picks, V.pad_moves = V.g.pad_moves;
  V.cpu_moves = V.g.cpu_moves, V.matches = V.g.matches, V.games_done = V.g.games_done;
  V.winner = rl_winner(s);
  V.help = V.g.help_open;
  V.target = V.turn >= 0 ? bk_select_target(V.g.sel, V.turn) : -1;
  if (V.target >= 0) {
    cw_vec2 q = cw_cursors_screen(bk_select_cursors(V.g.sel), V.turn, V.target);
    V.target_px[0] = (int32_t)lroundf(q.x), V.target_px[1] = (int32_t)lroundf(q.y);
  }
  V.playing = true;
}

static void update(void *user, double dt) {
  (void)user;
  if (!V.playing || !V.g.match) return;
  int turn = bk_match_turn(V.g.match);
  if (turn >= 0 && !rl_game_is_cpu(&V.g, turn) && !V.g.help_open) bk_select_pointer(V.g.sel, turn, V.tree);
  rl_view_update((float)dt);
  const rl_view_stats *st = rl_view_stats_get();
  V.tiles_shown = st->tiles_shown, V.tiles_landed = st->tiles_landed, V.trains_run = st->trains_run, V.trains_arrived = st->trains_arrived;
  V.ghost_shown = st->ghost_shown, V.route_shown = st->route_shown, V.glow_peaks = st->glow_peaks;
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  static double last;
  double now = cw_time();
  float dt = last > 0 ? (float)(now - last) : 0;
  last = now;
  const char *clean = cw_test_env("SMOKESTACK_CLEAN"); /* the launcher art: the table alone */
  if (V.playing && !(clean && clean[0] == '1') && (cw_shell_screen_now() == CW_SHELL_PLAY || cw_shell_screen_now() == CW_SHELL_PAUSE) && cw_shell_hud_shown()) {
    cw_r2d_begin(&(cw_r2d_pass_desc){ 0 });
    rl_view_hud(dt, V.g.help_open, SEAT_NAMES);
    cw_r2d_end();
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "Smokestack", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .update = update, .render = render, .debug = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
