/* Fever Line: the shell, the match and the table.
 *
 * Test switches (cw_test_env):
 *   FEVERLINE_SEATS=c1,p,...  skip the menus: p a person on a pad, h a hot seat, c0..c2 a CPU (read in the menu tick, bk_title)
 *   FEVERLINE_SETUP=0|1|2      Intro, Normal or Heroic, and no setup card (CPU-only teams never see one)
 *   FEVERLINE_MAP=0..8         the map (0 the world, the default; FV_MAPS order), the setup card's default
 *   FEVERLINE_AGENTS=2..4      agents for one person alone (the setup card's default) or one CPU seat alone
 *   FEVERLINE_SEED=<n>         the match's seed (default from the frame the match began)
 *   FEVERLINE_FAST=1           short beats (a self-playing run)
 *   BK_NO_HUD=1                the table alone (the launcher art's key frames, tools/make-art.sh)
 * The feverline_auto cart starts a team of four Normal CPUs on the Normal setup with short beats, match after match.
 *
 * Debug fields: feverline.done_once (a match has ended), feverline.turn, .phase, .agent (the agent to act), .decisions, .cpu_moves, .over, .result (1 won,
 * 2 lost), .cures, .outbreaks, .cubes (on the map), .deck, .hash (the match's state hash, low 32 bits), .matches,
 * .games_done, .moving, .mode, .targets, .target_px  (each target's canvas point, x then y, 96 pairs), .help, .cursor (the target under it), and the
 * replay check's .replaying, .replay_done, .replay_match, .replay_hash_match (debug command `replay`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_select.h"
#include "boardkit/bk_shell.h"
#include "cartwheel/debug.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "game.h"
#include "view.h"

#ifndef FEVERLINE_AUTOPILOT
#define FEVERLINE_AUTOPILOT 0
#endif

static struct {
  tg_game g;
  bool started, counted;
  int32_t done_once, ticks, games_done, turn, phase, agent, decisions, cpu_moves, over, result, cures, outbreaks, cubes, deck, matches, moving;
  int32_t mode, targets, target_px[TG_MAX_TARGETS * 2], help, cursor;
  uint32_t hash;
  uint64_t frame_seed;
  /* the start, kept for a replay */
  int start_seats, start_lv[FV_MAX_R], start_setup, start_agents, start_map;
  uint64_t start_seed;
  bool start_fast, start_skip;
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint32_t want_state, want_input;
  uint8_t *recording;
} M;

static const char *const CREDIT_LINES[] = {
  "Game design, code and art: Luis Montes, made with cartwheel-engine and three.c (MIT).",
  "The maps: city names and places from GeoNames (geonames.org, CC BY 4.0), coasts from Natural Earth (public domain);",
  "the game's own scripts pick and link the cities and draw the boards, cards and role portraits.",
  "Sounds are synthesised in code. Font: the engine's built-in sans.",
  NULL,
};
static const bk_credits CREDITS = { .title = "Fever Line", .inspired_by = "Inspired by Pandemic", .lines = CREDIT_LINES };

static int env_int(const char *name, int def, int lo, int hi) {
  const char *v = cw_test_env(name);
  if (!v || !*v) return def;
  int x = atoi(v);
  if (x < lo || x > hi) CW_FATAL("feverline: %s=%s (%d..%d)", name, v, lo, hi);
  return x;
}

static void start(int players, void *user) {
  (void)user, (void)players;
  bk_seat seats[4];
  int n = bk_lobby_seats(seats, 4);
  int lv[FV_MAX_R], k = 0;
  for (int i = 0; i < n; i++)
    if (seats[i].kind != BK_SEAT_EMPTY) lv[k++] = seats[i].kind == BK_SEAT_CPU ? seats[i].level : -1;
  if (k < 1) CW_FATAL("feverline: no seats");
  const char *sd = cw_test_env("FEVERLINE_SEED");
  uint64_t seed = sd ? (uint64_t)strtoull(sd, NULL, 10) : M.frame_seed;
  bool fast = FEVERLINE_AUTOPILOT || cw_test_env("FEVERLINE_FAST") != NULL;
  int setup = env_int("FEVERLINE_SETUP", FEVERLINE_AUTOPILOT ? FV_NORMAL : FV_INTRO, 0, 2);
  int agents = env_int("FEVERLINE_AGENTS", FEVERLINE_AUTOPILOT ? 4 : 2, 2, 4);
  int map = env_int("FEVERLINE_MAP", 0, 0, FV_NMAPS - 1);
  bool skip = cw_test_env("FEVERLINE_SETUP") != NULL;
  M.start_seats = k, M.start_map = map, M.start_setup = setup, M.start_agents = agents, M.start_seed = seed, M.start_fast = fast, M.start_skip = skip;
  memcpy(M.start_lv, lv, sizeof lv);
  M.replaying = M.replay_done = 0;
  tg_begin(&M.g, k, lv, setup, agents, map, seed, fast, skip);
  M.counted = false;
  view_start(&M.g);
  M.started = true;
  M.matches++;
  cw_input_record_begin();
}

/* `replay`: put the match back as it began and replay its own recording; when the recording runs out the match's
 * state and the input hash must be what they were at the request (pad presses, the CPUs' presses and pointer
 * picks, the setup card's choices: all in the recording) */
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
  memcpy(M.recording, d, len);
  tg_begin(&M.g, M.start_seats, M.start_lv, M.start_setup, M.start_agents, M.start_map, M.start_seed, M.start_fast, M.start_skip);
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

/* the title's How to play page: the in-play card without the team's powers or its close line (the shell draws the
 * way back) */
static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  view_help(NULL, canvas, false);
}

/* the menus' tick (bk_title; it runs while the scene app's tree is paused): the self-playing cart starts a team of
 * four Normal CPUs from the title, after the kit's test start (FEVERLINE_SEATS) */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (FEVERLINE_AUTOPILOT && screen == CW_SHELL_TITLE && !M.started) {
    int seats[4] = { 1, 1, 1, 1 };
    cw_shell_start_seats(4, seats);
  }
}

static const bk_title TITLE = { .test_env = FEVERLINE_AUTOPILOT ? NULL : "FEVERLINE_SEATS", .credits = &CREDITS,
                                .how_to_play = how_to_play, .menu_update = menu_update };

static void init(void *user) {
  (void)user;
  cw_shell_desc d = { 0 };
  static const char *const NAMES[FV_MAX_R] = { "Player 1", "Player 2", "Player 3", "Player 4" };
  cw_color cols[FV_MAX_R] = { cw_hex(0xe9e2d0ff), cw_hex(0xe8c04aff), cw_hex(0xf07ab0ff), cw_hex(0x8fb0d8ff) };
  bk_lobby_desc(&d, FV_MAX_R, NAMES, cols);
  d.title = "Fever Line";
  d.subtitle = "four fevers, one team, every card face up";
  d.start = start;
  d.quit = quit;
  d.pause_items[0] = "How to play";
  d.pause_item = pause_item;
  d.accent = cw_hex(0x6ad8e0ff);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  tg_actions_init(&M.g);
  view_init(cw_scene_app_tree());
  cw_debug_i32("feverline.ticks", &M.ticks);
  cw_debug_i32("feverline.games_done", &M.games_done);
  cw_debug_i32("feverline.done_once", &M.done_once);
  cw_debug_i32("feverline.turn", &M.turn);
  cw_debug_i32("feverline.phase", &M.phase);
  cw_debug_i32("feverline.agent", &M.agent);
  cw_debug_i32("feverline.decisions", &M.decisions);
  cw_debug_i32("feverline.cpu_moves", &M.cpu_moves);
  cw_debug_i32("feverline.over", &M.over);
  cw_debug_i32("feverline.result", &M.result);
  cw_debug_i32("feverline.cures", &M.cures);
  cw_debug_i32("feverline.outbreaks", &M.outbreaks);
  cw_debug_i32("feverline.cubes", &M.cubes);
  cw_debug_i32("feverline.deck", &M.deck);
  cw_debug_u32("feverline.hash", &M.hash);
  cw_debug_i32("feverline.matches", &M.matches);
  cw_debug_i32("feverline.moving", &M.moving);
  cw_debug_i32("feverline.mode", &M.mode);
  cw_debug_i32("feverline.targets", &M.targets);
  cw_debug_field("feverline.target_px", M.target_px, CW_DBG_I32, TG_MAX_TARGETS * 2);
  cw_debug_i32("feverline.replaying", &M.replaying);
  cw_debug_i32("feverline.replay_done", &M.replay_done);
  cw_debug_i32("feverline.replay_match", &M.replay_match);
  cw_debug_i32("feverline.replay_hash_match", &M.replay_hash_match);
  cw_debug_i32("feverline.help", &M.help);
  cw_debug_i32("feverline.cursor", &M.cursor);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (feverline.replay_match)", cmd_replay);
}

static void set_views(void) {
  cw_view3d v[1];
  if (!M.g.sel || cw_canvas_views3d(cw_scene_app_tree(), v, 1) < 1) return;
  for (int p = 0; p < FV_MAX_R; p++) cw_cursors_set_view(bk_select_cursors(M.g.sel), p, &v[0]);
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  M.ticks++;
  M.frame_seed = M.frame_seed * 6364136223846793005ull + 1442695040888963407ull + (uint64_t)cw_frame_index();
  if (!cw_shell_update()) return;
  if (!M.started) return;
  set_views();
  tg_tick(&M.g);
  if (M.replaying && !cw_input_replay_active()) {
    M.replay_match = M.g.match && (uint32_t)bk_match_hash(M.g.match) == M.want_state;
    M.replay_hash_match = cw_input_replay_hash() == M.want_input;
    M.replaying = 0;
    M.replay_done = 1;
    cw_info("feverline replay: state %s, input hash %s", M.replay_match ? "same" : "DIFFERENT", M.replay_hash_match ? "same" : "DIFFERENT");
  }
  if (M.g.over && !M.counted) {
    M.counted = true;
    M.games_done++;
    M.done_once = 1;
  }
  if (M.g.over) {
    bool again = FEVERLINE_AUTOPILOT && M.g.over_ticks > 240;
    for (int i = 0; i < M.g.seats && !again; i++)
      if (M.g.seat_level[i] < 0 && M.g.over_ticks > 60 && cw_action_just_pressed(i, M.g.act[A_OK])) again = true;
    if (again) {
      M.started = false;
      cw_shell_to_title();
    }
  }
  M.mode = M.g.mode;
  M.help = M.g.help;
  M.cursor = tg_cursor(&M.g);
  M.targets = M.g.ntargets;
  if (!M.g.match) return;
  const fv_state *s = &M.g.shown;
  M.turn = s->turns;
  M.phase = s->phase;
  M.agent = M.g.turn;
  M.decisions = M.g.decisions;
  M.cpu_moves = M.g.cpu_moves;
  M.over = M.g.over;
  M.result = s->result;
  M.cures = 0;
  M.cubes = 0;
  for (int c = 0; c < FV_NCOL; c++) M.cures += s->cured[c] != 0, M.cubes += fv_cubes_on_map(s, c);
  M.outbreaks = s->outbreaks;
  M.deck = fv_deck_left(s);
  M.hash = (uint32_t)bk_match_hash(M.g.match);
}

static void update(void *user, double dt) {
  (void)user;
  if (!M.started) return;
  int seat = tg_input_seat(&M.g);
  bool person = M.g.mode == UI_SETUP || (M.g.turn >= 0 && !tg_role_cpu(&M.g, M.g.turn));
  if (M.g.sel && person && !tg_busy(&M.g)) bk_select_pointer(M.g.sel, seat, cw_scene_app_tree());
  if (M.g.sel) bk_select_update(M.g.sel, (float)dt);
  view_update(&M.g, (float)dt);
  M.moving = view_moving();
  for (int i = 0; i < TG_MAX_TARGETS; i++) {
    cw_vec2 p = i < M.g.ntargets ? view_to_screen(M.g.target[i].at) : cw_v2(-1, -1);
    M.target_px[i * 2] = (int32_t)p.x, M.target_px[i * 2 + 1] = (int32_t)p.y;
  }
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  if (M.started && cw_shell_hud_shown() && !bk_hud_hidden()) view_hud(&M.g, 1.0f / 60.0f);
  cw_r2d_end();
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "Fever Line", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .update = update, .render = render, .debug = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
