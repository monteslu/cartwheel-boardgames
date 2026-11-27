/* Kilowatt Country: the shell, the match and the table.
 *
 * Test switches (cw_test_env):
 *   KILOWATTCOUNTRY_SEATS=c2,c1,...  skip the menus: p a person on a pad, h a hot seat, c0..c2 a CPU (bk_lobby_test_start)
 *   KILOWATTCOUNTRY_SEED=<n>         the match's seed (default from the frame the match began)
 *   KILOWATTCOUNTRY_MAP=<key|n>      the map (usa, useast, lakes, france, iberia, balkans, central, or its index): skips
 *                                    the setup card (without it people pick the map there, starting on the first)
 *   KILOWATTCOUNTRY_FAST=1           short beats (a self-playing run)
 *   BK_NO_HUD=1                      the table alone (the launcher art's key frames, tools/make-art.sh)
 * The kilowattcountry_auto cart starts four Normal CPUs with short beats and plays match after match, each on the next
 * map (KILOWATTCOUNTRY_MAP holds it on one). Both starts run in
 * the shell's menu tick (bk_title_desc): a scene app's tree, and with it fixed_update, holds still outside play.
 * The title carries How to play and Credits as pages; How to play also opens over play (its action, or the pause item).
 *
 * Debug fields: kc.round, kc.step, kc.phase, kc.turn, kc.decisions, kc.cpu_moves, kc.over, kc.winner, kc.money (per
 * seat), kc.cities (per seat), kc.powered (per seat), kc.hash (the match's state hash, low 32 bits), kc.matches,
 * kc.games_done, kc.moving, kc.lit (houses glowing), kc.label_overlaps (city names running into a name or on their own city, the
 * worst over every map; KILOWATTCOUNTRY_LABELS=below puts each under its city as before), kc.label_link_hits (names
 * over another city, a cost tag or a link), kc.label_strays (names hiding another city, or leaders over another city or name),
 * kc.label_misread (names nearer another city's dot than their own with no leader; KILOWATTCOUNTRY_LABELS=footprint draws
 * leaders by the old footprint rule, a control), kc.label_rect (each name's rect, then each city's footprint), kc.cursor (the person's target), kc.ntargets, kc.help, kc.pointer_picks,
 * kc.target_px (each target's canvas point: tests tap there), kc.target_kind (the move kind a confirm on each plays),
 * kc.pad_kinds and kc.pointer_kinds (people's moves by kind and input), kc.last_kind, kc.kinds_used (move kinds people
 * used), kc.nplants, kc.stored (fuel held, per seat), kc.auc_plant (its number), kc.auc_bid, kc.auc_in (seat bits).
 * kc.map (the map in use), kc.setup (the setup card is up), kc.map_picks (maps picked there), kc.label_maps (maps whose
 * names the label check has placed so far; it covers every map, one a frame).
 * KILOWATTCOUNTRY_TEST_HIDE=<open|pass|raise|drop|discard|fuel|done|build> leaves that move off the table, =map the setup
 * card's map plates (controls).
 * Debug command `replay`: replay the match so far from its own input recording and compare the state
 * (kc.replay_match) and the input hash (kc.replay_hash_match); CW_TEST_SET_UNRECORDED is the engine's control. */
#include <math.h>
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

#ifndef KILOWATTCOUNTRY_AUTOPILOT
#define KILOWATTCOUNTRY_AUTOPILOT 0
#endif

static struct {
  kg_game g;
  bool started, counted, pending_begin, pending_replay;
  int players, lv[KC_MAX_P], map;
  bool setup;
  uint64_t seed;
  bool fast;
  uint8_t *recording;
  size_t recording_len;
  uint32_t want_hash, want_input;
  int32_t busy, mode, help, pointer_picks, replaying, replay_done, replay_match, replay_hash_match;
  int32_t target_px[2 * KG_MAX_TARGETS];
  int32_t ticks, games_done, round, step, phase, turn, decisions, cpu_moves, over, winner, matches, moving, lit, cursor, ntargets, label_overlaps, label_hits, label_strays, label_misread;
  int32_t money[KC_MAX_P], cities[KC_MAX_P], powered[KC_MAX_P], nplants[KC_MAX_P], stored[KC_MAX_P];
  int32_t pad_kinds[KC_M_DRAW], pointer_kinds[KC_M_DRAW], last_kind, kinds_used, auc_plant, auc_bid, auc_in;
  int32_t target_kind[KG_MAX_TARGETS], label_rect[8 * KC_NCITY], map_now, setup_now, map_picks, label_maps;
  uint32_t hash;
  uint64_t frame_seed;
} M;

static const char *const CREDIT_LINES[] = {
  "Game design, code and art: Luis Montes, made with cartwheel-engine and three.c (MIT).",
  "The maps: city names and places from GeoNames (geonames.org, CC BY 4.0); land, borders, lakes and rivers from",
  "Natural Earth (naturalearthdata.com, public domain). Picked, linked and drawn by the game's own scripts.",
  "The plants, the cards and the sounds are made by the game's own scripts.",
  "Font: the engine's built-in sans.",
  NULL,
};
static const bk_credits CREDITS = { .title = "Kilowatt Country", .inspired_by = "Inspired by Power Grid", .lines = CREDIT_LINES };

static void start(int players, void *user) {
  (void)user, (void)players;
  bk_seat seats[4];
  int n = bk_lobby_seats(seats, 4);
  int lv[KC_MAX_P] = { -1, -1, -1, -1 };
  int k = 0;
  for (int i = 0; i < n; i++)
    if (seats[i].kind != BK_SEAT_EMPTY) lv[k++] = seats[i].kind == BK_SEAT_CPU ? seats[i].level : -1;
  if (k < 2) lv[k++] = CW_SHELL_CPU_NORMAL; /* one person alone plays a Normal CPU */
  const char *sd = cw_test_env("KILOWATTCOUNTRY_SEED");
  M.seed = sd ? (uint64_t)strtoull(sd, NULL, 10) : M.frame_seed;
  M.fast = KILOWATTCOUNTRY_AUTOPILOT || cw_test_env("KILOWATTCOUNTRY_FAST") != NULL;
  const char *mp = cw_test_env("KILOWATTCOUNTRY_MAP");
  M.map = KILOWATTCOUNTRY_AUTOPILOT ? M.matches % KC_NMAPS : 0;
  M.setup = !mp && !KILOWATTCOUNTRY_AUTOPILOT;
  if (mp) {
    char *end = NULL;
    long v = strtol(mp, &end, 10);
    M.map = end && *end == 0 && end != mp ? (int)v : -1;
    for (int m = 0; m < KC_NMAPS && M.map < 0; m++)
      if (strcmp(mp, kc_maps[m].key) == 0) M.map = m;
    if (M.map < 0 || M.map >= KC_NMAPS) CW_FATAL("kilowattcountry: KILOWATTCOUNTRY_MAP=%s is no map", mp);
  }
  M.players = k;
  memcpy(M.lv, lv, sizeof lv);
  M.pending_begin = true; /* the match begins on the next tick, as a replay does */
  M.started = false;
  M.matches++;
}

/* a match from its start: in a fixed tick, whose rest is skipped (a recording or a replay starts at the next tick) */
static void begin_match(void) {
  kg_start(&M.g, M.players, M.lv, M.map, M.seed, M.fast, M.setup);
  M.counted = false;
  view_start(&M.g);
  M.started = true;
}

static void cmd_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (!M.started || !M.g.match || cw_shell_screen_now() != CW_SHELL_PLAY || !cw_input_recording_active()) {
    cw_reply_error(r, "replay needs a match in play");
    return;
  }
  M.want_hash = (uint32_t)bk_match_hash(M.g.match);
  cw_input_record_end();
  M.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  free(M.recording);
  M.recording = malloc(len ? len : 1);
  if (!M.recording) CW_FATAL("kilowattcountry: no memory for the recording");
  memcpy(M.recording, d, len);
  M.recording_len = len;
  M.pending_replay = true;
  M.replaying = 1;
  M.replay_done = M.replay_match = M.replay_hash_match = 0;
  cw_reply_printf(r, "{\"bytes\":%zu,\"state\":%u,\"input\":%u}", len, M.want_hash, M.want_input);
}

static void quit(void *user) {
  (void)user;
  M.started = false;
}

static void pause_item(int index, void *user) {
  (void)user;
  if (index == 0) M.g.help = true;
}

/* the title's How to play page: the card play opens, without its close prompt (the shell draws the way back) */
static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  view_help(canvas, false);
}

/* the menus' tick: the self-playing cart starts four Normal CPUs from the title, match after match (the test
 * switch KILOWATTCOUNTRY_SEATS is the kit's, read just before this) */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (!KILOWATTCOUNTRY_AUTOPILOT || screen != CW_SHELL_TITLE || M.started || M.pending_begin) return;
  int seats[4] = { 1, 1, 1, 1 };
  cw_shell_start_seats(4, seats);
}

static const bk_title TITLE = { .test_env = "KILOWATTCOUNTRY_SEATS", .credits = &CREDITS, .how_to_play = how_to_play,
                                .menu_update = menu_update };

static void init(void *user) {
  (void)user;
  cw_shell_desc d = { 0 };
  static const char *const NAMES[KC_MAX_P] = { "Amber", "Cobalt", "Rose", "Jade" };
  cw_color cols[KC_MAX_P] = { cw_hex(0xf4b04cff), cw_hex(0x6a9cf0ff), cw_hex(0xf07c98ff), cw_hex(0x5cd29aff) };
  bk_lobby_desc(&d, KC_MAX_P, NAMES, cols);
  d.title = "Kilowatt Country";
  d.subtitle = "power plants, fuel and a country to light";
  d.start = start;
  d.quit = quit;
  d.pause_items[0] = "How to play";
  d.pause_item = pause_item;
  d.accent = cw_hex(0xf2b84aff);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  kg_actions_init(&M.g);
  view_init(cw_scene_app_tree());
  cw_debug_i32("kc.ticks", &M.ticks);
  cw_debug_i32("kc.games_done", &M.games_done);
  cw_debug_i32("kc.view_stage", &view_stage);
  cw_debug_i32("kc.round", &M.round);
  cw_debug_i32("kc.step", &M.step);
  cw_debug_i32("kc.phase", &M.phase);
  cw_debug_i32("kc.turn", &M.turn);
  cw_debug_i32("kc.decisions", &M.decisions);
  cw_debug_i32("kc.cpu_moves", &M.cpu_moves);
  cw_debug_i32("kc.over", &M.over);
  cw_debug_i32("kc.winner", &M.winner);
  cw_debug_field("kc.money", M.money, CW_DBG_I32, KC_MAX_P);
  cw_debug_field("kc.cities", M.cities, CW_DBG_I32, KC_MAX_P);
  cw_debug_field("kc.powered", M.powered, CW_DBG_I32, KC_MAX_P);
  cw_debug_field("kc.nplants", M.nplants, CW_DBG_I32, KC_MAX_P);
  cw_debug_field("kc.stored", M.stored, CW_DBG_I32, KC_MAX_P);
  cw_debug_field("kc.pad_kinds", M.pad_kinds, CW_DBG_I32, KC_M_DRAW);
  cw_debug_field("kc.pointer_kinds", M.pointer_kinds, CW_DBG_I32, KC_M_DRAW);
  cw_debug_i32("kc.last_kind", &M.last_kind);
  cw_debug_i32("kc.kinds_used", &M.kinds_used);
  cw_debug_i32("kc.auc_plant", &M.auc_plant);
  cw_debug_i32("kc.auc_bid", &M.auc_bid);
  cw_debug_i32("kc.auc_in", &M.auc_in);
  cw_debug_field("kc.target_kind", M.target_kind, CW_DBG_I32, KG_MAX_TARGETS);
  cw_debug_u32("kc.hash", &M.hash);
  cw_debug_i32("kc.matches", &M.matches);
  cw_debug_i32("kc.moving", &M.moving);
  cw_debug_i32("kc.lit", &M.lit);
  cw_debug_i32("kc.label_overlaps", &M.label_overlaps);
  cw_debug_i32("kc.label_link_hits", &M.label_hits);
  cw_debug_i32("kc.label_strays", &M.label_strays);
  cw_debug_i32("kc.label_misread", &M.label_misread);
  cw_debug_field("kc.label_rect", M.label_rect, CW_DBG_I32, 8 * KC_NCITY);
  cw_debug_i32("kc.cursor", &M.cursor);
  cw_debug_i32("kc.map", &M.map_now);
  cw_debug_i32("kc.setup", &M.setup_now);
  cw_debug_i32("kc.map_picks", &M.map_picks);
  cw_debug_i32("kc.label_maps", &M.label_maps);
  cw_debug_i32("kc.ntargets", &M.ntargets);
  cw_debug_i32("kc.help", &M.help);
  cw_debug_i32("kc.busy", &M.busy);
  cw_debug_i32("kc.mode", &M.mode);
  cw_debug_i32("kc.pointer_picks", &M.pointer_picks);
  cw_debug_field("kc.target_px", M.target_px, CW_DBG_I32, 2 * KG_MAX_TARGETS);
  cw_debug_i32("kc.replaying", &M.replaying);
  cw_debug_i32("kc.replay_done", &M.replay_done);
  cw_debug_i32("kc.replay_match", &M.replay_match);
  cw_debug_i32("kc.replay_hash_match", &M.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (kc.replay_match)", cmd_replay);
}

static void set_views(void) {
  cw_view3d v[1];
  if (cw_canvas_views3d(cw_scene_app_tree(), v, 1) < 1) return;
  for (int p = 0; p < KC_MAX_P; p++) cw_cursors_set_view(bk_select_cursors(M.g.sel), p, &v[0]);
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  M.ticks++;
  M.frame_seed = M.frame_seed * 6364136223846793005ull + 1442695040888963407ull + (uint64_t)cw_frame_index();
  if (!cw_shell_update()) return;
  if (M.pending_begin) {
    M.pending_begin = false;
    begin_match();
    cw_input_record_begin(); /* the whole match, CPU presses and pointer picks included */
    return;
  }
  if (M.pending_replay) {
    M.pending_replay = false;
    begin_match();
    if (!cw_input_replay_begin(M.recording, M.recording_len)) CW_FATAL("kilowattcountry: the recording did not load");
    return;
  }
  if (M.replaying && !cw_input_replay_active()) {
    M.replay_match = (uint32_t)bk_match_hash(M.g.match) == M.want_hash;
    M.replay_hash_match = cw_input_replay_hash() == M.want_input;
    M.replaying = 0;
    M.replay_done = 1;
    cw_info("kilowattcountry replay: state %s, input hash %s", M.replay_match ? "same" : "DIFFERENT", M.replay_hash_match ? "same" : "DIFFERENT");
  }
  if (!M.started) return;
  set_views();
  kg_tick(&M.g);
  if (M.g.over && !M.counted) {
    M.counted = true;
    M.games_done++;
  }
  if (M.g.over) {
    bool again = KILOWATTCOUNTRY_AUTOPILOT && M.g.over_ticks > 240;
    for (int i = 0; i < M.g.players && !again; i++)
      if (!kg_seat_cpu(&M.g, i) && M.g.over_ticks > 60 && cw_action_just_pressed(i, M.g.act[A_OK])) again = true;
    if (again) {
      M.started = false;
      cw_shell_to_title();
    }
  }
  const kc_state *s = &M.g.shown;
  M.round = s->round;
  M.step = s->step;
  M.phase = s->phase;
  M.turn = M.g.turn;
  M.decisions = M.g.decisions;
  M.cpu_moves = M.g.cpu_moves;
  M.over = M.g.over;
  M.winner = M.g.over ? kc_winner(s) : -1;
  for (int i = 0; i < KC_MAX_P; i++) {
    M.money[i] = i < M.g.players ? s->p[i].money : 0;
    M.cities[i] = i < M.g.players ? s->p[i].ncity : 0;
    M.powered[i] = i < M.g.players ? s->p[i].powered : 0;
    M.nplants[i] = M.stored[i] = 0;
    for (int k = 0; i < M.g.players && k < 3; k++) M.nplants[i] += s->p[i].plants[k] >= 0;
    for (int f = 0; i < M.g.players && f < KC_NFUEL; f++) M.stored[i] += s->p[i].fuel[f];
  }
  M.kinds_used = 0;
  for (int k = 1; k < KC_M_DRAW; k++) {
    M.pad_kinds[k] = M.g.pad_kinds[k], M.pointer_kinds[k] = M.g.pointer_kinds[k];
    M.kinds_used += M.g.pad_kinds[k] + M.g.pointer_kinds[k] > 0;
  }
  M.last_kind = M.g.last_kind;
  M.auc_plant = s->auc_plant >= 0 && !M.g.setup ? kc_plants[s->auc_plant].number : -1;
  M.auc_bid = s->auc_bid;
  M.auc_in = s->auc_in;
  M.hash = M.g.match ? (uint32_t)bk_match_hash(M.g.match) : 0;
  M.cursor = kg_cursor(&M.g);
  M.map_now = M.g.map;
  M.setup_now = M.g.setup;
  M.map_picks = M.g.map_picks;
  M.ntargets = M.g.ntargets;
  M.help = M.g.help;
  M.busy = kg_busy(&M.g);
  M.mode = M.g.mode;
  M.pointer_picks = M.g.pointer_picks;
  for (int t = 0; t < KG_MAX_TARGETS; t++) {
    cw_vec2 q = t < M.g.ntargets ? cw_cursors_screen(bk_select_cursors(M.g.sel), 0, t) : cw_v2(-1, -1);
    M.target_px[2 * t] = (int32_t)lroundf(q.x), M.target_px[2 * t + 1] = (int32_t)lroundf(q.y);
    /* the move kind a confirm there plays (a fuel row: fuel, it opens the count) */
    M.target_kind[t] = t >= M.g.ntargets ? 0 : M.g.target[t].kind == KG_T_FUEL ? KC_M_FUEL : (int32_t)bk_move_field(M.g.target[t].move, 0);
  }
}

static void update(void *user, double dt) {
  (void)user;
  if (!M.started) return;
  int seat = M.g.turn;
  if (seat >= 0 && !kg_seat_cpu(&M.g, seat) && !kg_busy(&M.g)) bk_select_pointer(M.g.sel, seat, cw_scene_app_tree());
  bk_select_update(M.g.sel, (float)dt);
  view_update(&M.g, (float)dt);
  M.moving = view_moving();
  M.lit = view_lit();
  M.label_overlaps = view_label_overlaps();
  M.label_hits = view_label_link_hits();
  M.label_strays = view_label_strays();
  M.label_misread = view_label_misread();
  view_label_rects(M.label_rect);
  M.label_maps = view_label_maps();
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
  static cw_app_desc base = { .title = "Kilowatt Country", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .update = update, .render = render, .debug = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
