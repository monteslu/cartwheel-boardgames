/* First Hearth: the shell, the match and the table.
 *
 * Test switches (cw_test_env):
 *   FIRSTHEARTH_SEATS=c2,c1,...  skip the menus: p a person on a pad, h a hot seat, c0..c2 a CPU (read in the menu tick, bk_title)
 *   FIRSTHEARTH_SEED=<n>         the match's seed (default from the frame the match began)
 *   FIRSTHEARTH_FAST=1           short beats (a self-playing run)
 *   FIRSTHEARTH_NO_HUD=1         the table alone (the launcher art's key frames, tools/make-cart-art.sh)
 * The firsthearth_auto cart starts four Normal CPUs with short beats and plays match after match.
 *
 * Debug fields: firsthearth.round, firsthearth.phase, firsthearth.turn, firsthearth.decisions, firsthearth.cpu_moves, firsthearth.over, firsthearth.winner,
 * firsthearth.score (per seat), firsthearth.hash (the match's state hash, low 32 bits), firsthearth.matches, firsthearth.moving,
 * firsthearth.help, firsthearth.cursor (the target under it), firsthearth.mode (0 places, 1 how many, 2 a menu), firsthearth.targets, firsthearth.target_px (each target's canvas
 * point, x then y), and the replay check's firsthearth.replaying, .replay_done, .replay_match, .replay_hash_match (debug
 * command `replay`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_shell.h"
#include "boardkit/bk_select.h"
#include "cartwheel/debug.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "game.h"
#include "view.h"

#ifndef FIRSTHEARTH_AUTOPILOT
#define FIRSTHEARTH_AUTOPILOT 0
#endif

static struct {
  tg_game g;
  bool started, view_ready, counted;
  int32_t stage, ticks, games_done, round, phase, turn, decisions, cpu_moves, over, winner, score[TR_MAX_P], matches, moving;
  int32_t help, cursor, mode, targets, target_px[TG_MAX_TARGETS * 2];
  uint32_t hash;
  uint64_t frame_seed;
  /* the start, kept for a replay */
  int start_players, start_lv[TR_MAX_P];
  uint64_t start_seed;
  bool start_fast;
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint32_t want_state, want_input;
  uint8_t *recording;
} M;

static const char *const CREDIT_LINES[] = {
  "Game design, code and art: Luis Montes, made with cartwheel-engine and three.c (MIT).",
  "Cards, board, dice and sounds are drawn and synthesised by the game's own scripts.",
  "Font: the engine's built-in sans.",
  NULL,
};
static const bk_credits CREDITS = { .title = "First Hearth", .inspired_by = "Inspired by Stone Age", .lines = CREDIT_LINES };

static void start(int players, void *user) {
  (void)user;
  bk_seat seats[4];
  int n = bk_lobby_seats(seats, 4);
  (void)players;
  int lv[TR_MAX_P] = { -1, -1, -1, -1 };
  int k = 0;
  for (int i = 0; i < n; i++)
    if (seats[i].kind != BK_SEAT_EMPTY) lv[k++] = seats[i].kind == BK_SEAT_CPU ? seats[i].level : -1;
  if (k < 2) lv[k++] = CW_SHELL_CPU_NORMAL; /* one person alone plays a Normal CPU */
  const char *sd = cw_test_env("FIRSTHEARTH_SEED");
  uint64_t seed = sd ? (uint64_t)strtoull(sd, NULL, 10) : M.frame_seed;
  bool fast = FIRSTHEARTH_AUTOPILOT || cw_test_env("FIRSTHEARTH_FAST") != NULL;
  M.start_players = k, M.start_seed = seed, M.start_fast = fast;
  memcpy(M.start_lv, lv, sizeof lv);
  M.replaying = M.replay_done = 0;
  tg_start(&M.g, k, lv, seed, fast);
  for (int i = 0; i < k; i++)
    if (lv[i] < 0) M.g.help = true; /* people start with the how-to-play card open */
  M.counted = false;
  view_start(&M.g);
  M.started = true;
  M.matches++;
  cw_input_record_begin();
}

/* `replay`: put the match back as it began and replay its own recording; when the recording runs out the match's
 * state and the input hash must be what they were at the request (pad presses, the CPUs' presses and pointer picks
 * are all in the recording) */
static void cmd_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (!M.started || !M.g.match || !cw_input_recording_active()) {
    cw_reply_error(r, "replay needs a match in play (it replays the match from its start)");
    return;
  }
  M.want_state = (uint32_t)bk_match_hash(M.g.match);
  cw_input_record_end();
  M.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  free(M.recording);
  M.recording = malloc(len ? len : 1);
  if (!M.recording) CW_FATAL("firsthearth: no memory for the recording");
  memcpy(M.recording, d, len);
  tg_start(&M.g, M.start_players, M.start_lv, M.start_seed, M.start_fast);
  M.g.help = false;
  for (int i = 0; i < M.start_players; i++)
    if (M.start_lv[i] < 0) M.g.help = true; /* as the match began: the card open for people */
  view_start(&M.g);
  if (!cw_input_replay_begin(M.recording, len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  M.replaying = 1;
  M.replay_done = M.replay_match = M.replay_hash_match = 0;
  cw_reply_printf(r, "{\"bytes\":%zu,\"state\":%u,\"input\":%u}", len, M.want_state, M.want_input);
}

static void quit(void *user) {
  (void)user;
  M.started = false;
}

static void pause_item(int index, void *user) {
  (void)user;
  if (index == 0) M.g.help = true;
}

/* the title's How to play page: the in-play card without its close line (the shell draws the way back) */
static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  view_help(canvas, false);
}

/* the menus' tick (bk_title; it runs while the scene app's tree is paused): the self-playing cart starts four
 * Normal CPUs from the title, after the kit's test start (FIRSTHEARTH_SEATS) */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (FIRSTHEARTH_AUTOPILOT && screen == CW_SHELL_TITLE && !M.started) {
    int seats[4] = { 1, 1, 1, 1 };
    cw_shell_start_seats(4, seats);
  }
}

static const bk_title TITLE = { .test_env = FIRSTHEARTH_AUTOPILOT ? NULL : "FIRSTHEARTH_SEATS", .credits = &CREDITS,
                                .how_to_play = how_to_play, .menu_update = menu_update };

static void init(void *user) {
  (void)user;
  cw_shell_desc d = { 0 };
  static const char *const NAMES[TR_MAX_P] = { "Ochre", "River", "Fern", "Ember" };
  cw_color cols[TR_MAX_P] = { cw_hex(0xe0603cff), cw_hex(0x5a9ae0ff), cw_hex(0x74c050ff), cw_hex(0xf0c450ff) };
  bk_lobby_desc(&d, TR_MAX_P, NAMES, cols);
  d.title = "First Hearth";
  d.subtitle = "a village of the first people";
  d.start = start;
  d.quit = quit;
  d.pause_items[0] = "How to play";
  d.pause_item = pause_item;
  d.accent = cw_hex(0xf0c070ff);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  tg_actions_init(&M.g);
  view_init(cw_scene_app_tree());
  cw_debug_i32("firsthearth.ticks", &M.ticks);
  cw_debug_i32("firsthearth.stage", &M.stage);
  cw_debug_i32("firsthearth.games_done", &M.games_done);
  cw_debug_i32("firsthearth.view_stage", &view_stage);
  cw_debug_i32("firsthearth.texcs", &view_texcs);
  cw_debug_i32("firsthearth.round", &M.round);
  cw_debug_i32("firsthearth.phase", &M.phase);
  cw_debug_i32("firsthearth.turn", &M.turn);
  cw_debug_i32("firsthearth.decisions", &M.decisions);
  cw_debug_i32("firsthearth.cpu_moves", &M.cpu_moves);
  cw_debug_i32("firsthearth.over", &M.over);
  cw_debug_i32("firsthearth.winner", &M.winner);
  cw_debug_field("firsthearth.score", M.score, CW_DBG_I32, TR_MAX_P);
  cw_debug_u32("firsthearth.hash", &M.hash);
  cw_debug_i32("firsthearth.matches", &M.matches);
  cw_debug_i32("firsthearth.moving", &M.moving);
  cw_debug_i32("firsthearth.help", &M.help);
  cw_debug_i32("firsthearth.cursor", &M.cursor);
  cw_debug_i32("firsthearth.mode", &M.mode);
  cw_debug_i32("firsthearth.targets", &M.targets);
  cw_debug_field("firsthearth.target_px", M.target_px, CW_DBG_I32, TG_MAX_TARGETS * 2);
  cw_debug_i32("firsthearth.replaying", &M.replaying);
  cw_debug_i32("firsthearth.replay_done", &M.replay_done);
  cw_debug_i32("firsthearth.replay_match", &M.replay_match);
  cw_debug_i32("firsthearth.replay_hash_match", &M.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (firsthearth.replay_match)", cmd_replay);
}

static void set_views(void) {
  cw_view3d v[1];
  if (cw_canvas_views3d(cw_scene_app_tree(), v, 1) < 1) return;
  for (int p = 0; p < TR_MAX_P; p++) cw_cursors_set_view(bk_select_cursors(M.g.sel), p, &v[0]);
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  M.ticks++;
  M.frame_seed = M.frame_seed * 6364136223846793005ull + 1442695040888963407ull + (uint64_t)cw_frame_index();
  if (!cw_shell_update()) return;
  if (!M.started) return;
  set_views();
  M.stage = 1;
  tg_tick(&M.g);
  M.stage = 2;
  if (M.replaying && !cw_input_replay_active()) {
    M.replay_match = M.g.match && (uint32_t)bk_match_hash(M.g.match) == M.want_state;
    M.replay_hash_match = cw_input_replay_hash() == M.want_input;
    M.replaying = 0;
    M.replay_done = 1;
    cw_info("firsthearth replay: state %s, input hash %s", M.replay_match ? "same" : "DIFFERENT", M.replay_hash_match ? "same" : "DIFFERENT");
  }
  if (M.g.over && !M.counted) {
    M.counted = true;
    M.games_done++;
  }
  if (M.g.over) {
    bool again = FIRSTHEARTH_AUTOPILOT && M.g.over_ticks > 240;
    for (int i = 0; i < M.g.players && !again; i++)
      if (!tg_seat_cpu(&M.g, i) && M.g.over_ticks > 60 && cw_action_just_pressed(i, M.g.act[A_OK])) again = true;
    if (again) {
      M.started = false;
      cw_shell_to_title();
    }
  }
  const tr_state *s = &M.g.shown;
  M.round = s->round;
  M.phase = s->phase;
  M.turn = M.g.turn;
  M.decisions = M.g.decisions;
  M.cpu_moves = M.g.cpu_moves;
  M.over = M.g.over;
  M.winner = M.g.over ? tr_winner(s) : -1;
  for (int i = 0; i < TR_MAX_P; i++) M.score[i] = i < M.g.players ? tr_final_score(s, i) : 0;
  M.hash = (uint32_t)bk_match_hash(M.g.match);
  M.help = M.g.help;
  M.cursor = tg_cursor(&M.g);
  M.mode = M.g.mode;
  M.targets = M.g.ntargets;
}

static void update(void *user, double dt) {
  (void)user;
  if (!M.started) return;
  int seat = M.g.turn;
  if (seat >= 0 && !tg_seat_cpu(&M.g, seat) && !tg_busy(&M.g)) bk_select_pointer(M.g.sel, seat, cw_scene_app_tree());
  bk_select_update(M.g.sel, (float)dt);
  M.stage = 3;
  view_update(&M.g, (float)dt);
  M.stage = 4;
  M.moving = view_moving();
  for (int i = 0; i < TG_MAX_TARGETS; i++) {
    cw_vec2 p = i < M.g.ntargets ? view_to_screen(M.g.target[i].at) : cw_v2(-1, -1);
    M.target_px[i * 2] = (int32_t)p.x, M.target_px[i * 2 + 1] = (int32_t)p.y;
  }
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  M.stage = 5;
  if (M.started && cw_shell_hud_shown() && !cw_test_env("FIRSTHEARTH_NO_HUD")) view_hud(&M.g, 1.0f / 60.0f);
  M.stage = 6;
  cw_r2d_end();
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "First Hearth", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .update = update, .render = render, .debug = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
