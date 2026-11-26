/* Smalti: draft coloured glass from round trays, stage it on your board, set it into a Venetian
 * mosaic. Two to four seats on one screen: people on pads, people passing a pad, or the computer at Easy, Normal
 * or Hard. The title's Credits page names the game it is inspired by (the only place it is named).
 *
 * src/rules.c is the game (pure, a bk_game), src/game.c the match (seats, the cursor, the table shown step by
 * step), src/view.c the table and the HUD, this file the shell and the frame.
 *
 * Test switches (cw_test_env):
 *   SMALTI_SEATS=c2,p,...  skip the menus (read in the shell's menu tick, bk_title): p a person on a pad, h a hot seat, c0..c2 a computer
 *                          at that level (the self-playing cart smalti_auto starts c2,c1,c1,c0 by itself)
 *   SMALTI_SEED=n          the bag seed (default: a new one each match; the self-playing cart: 1)
 *   SMALTI_FAST=1          short waits between steps
 * The debug request `replay` replays the match from its own input recording and compares (game.replay_match). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "boardkit/bk_shell.h"
#include "cartwheel/pick3d.h"
#include "game.h"
#include "view.h"

typedef struct save_data {
  uint8_t seen_help;
  uint8_t pad[3];
  uint32_t matches;
} save_data;

static struct {
  mg_game g;
  bool started_by_env, fresh, in_match;
  int seats_cpu[BK_MAX_SEATS];
  int players;
  uint64_t seed;
  bool fast;
  /* the replay check */
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint64_t want_state;
  uint32_t want_input;
  uint8_t *recording;
  size_t recording_len;
  /* debug fields */
  int32_t turn, round, over, score[BK_MAX_SEATS], cpu_moves, moves, help, mode, walks, busy, ticks, players_dbg;
  uint32_t state_hash, shown_hash;
  int32_t over_tick, matches_done;
  int32_t cursor;
  int32_t ntargets, target_px[2 * 32], target_code[32]; /* the cursor's targets: canvas points (tests tap there) and what each is */
} M;

static void backdrop(cw_shell_screen screen, cw_vec2 canvas, void *user);

static struct { int mode, wait, hops; cw_rng rng; int32_t moves, kinds; } NB; /* the newcomer test player (newcomer_tick) */

static void set_view(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  if (sz.x <= 0 || sz.y <= 0) sz = (cw_vec2){ 1280, 720 };
  mg_set_view(&M.g, (cw_view3d){ cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "World"), "Camera"), { 0, 0, 1280, 720 } });
  (void)sz;
}

static void new_match(void) {
  set_view();
  const char *nb = cw_test_env("SMALTI_NEWCOMER");
  NB.mode = nb && *nb ? (strcmp(nb, "lost") == 0 ? 2 : 1) : 0;
  cw_rng_seed(&NB.rng, 77);
  NB.hops = 2;
  save_data *sv = cw_shell_game_save();
  mg_start(&M.g, M.players, M.seats_cpu, M.seed, M.fast);
  view_start(&M.g);
  bool any_person = false;
  for (int i = 0; i < M.players; i++) any_person |= M.seats_cpu[i] < 0;
  M.g.help = any_person && sv && !sv->seen_help && !cw_test_env("SMALTI_SEATS");
  if (sv && any_person) {
    sv->seen_help = 1;
    sv->matches++;
    cw_shell_save_commit();
  }
  M.fresh = true;
  M.in_match = true;
  cw_input_record_begin();
}

static uint64_t fresh_seed(void) {
  const char *e = cw_test_env("SMALTI_SEED");
  if (e && *e) return strtoull(e, NULL, 10);
#ifdef SMALTI_AUTOPILOT
  return 1;
#else
  save_data *sv = cw_shell_game_save();
  return bk_seed_mix((uint64_t)(cw_time() * 1e6), sv ? sv->matches : 0);
#endif
}

static void start(int players, void *user) {
  (void)user;
  for (int i = 0; i < BK_MAX_SEATS; i++) {
    cw_shell_cpu_level lv;
    M.seats_cpu[i] = i < players && cw_shell_seat_cpu(i, &lv) ? (int)lv : -1;
  }
  if (players < 2) { /* one person alone plays the computer */
    players = 2;
    M.seats_cpu[1] = CW_SHELL_CPU_NORMAL;
    cw_input_assign(1, -1, 0);
  }
  M.players = players > BK_MAX_SEATS ? BK_MAX_SEATS : players;
  M.seed = fresh_seed();
  const char *f = cw_test_env("SMALTI_FAST");
  M.fast = f && *f == '1';
#ifdef SMALTI_AUTOPILOT
  M.fast = true;
#endif
  new_match();
}

static void quit(void *user) {
  (void)user;
  M.in_match = false;
}

static void pause_item(int index, void *user) {
  (void)user;
  if (index == 0) M.g.help = true;
}

static void cmd_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (cw_shell_screen_now() != CW_SHELL_PLAY || !cw_input_recording_active()) {
    cw_reply_error(r, "replay needs a match in play (it replays the match from its start)");
    return;
  }
  M.want_state = bk_match_hash(M.g.match);
  cw_input_record_end();
  M.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  cw_free(M.recording);
  M.recording = cw_alloc(len ? len : 1, CW_MEM_GAME);
  memcpy(M.recording, d, len);
  M.recording_len = len;
  uint32_t ticks = cw_input_record_ticks();
  bool help = M.g.help;
  mg_start(&M.g, M.players, M.seats_cpu, M.seed, M.fast);
  M.g.help = help && false;
  view_start(&M.g);
  if (!cw_input_replay_begin(M.recording, M.recording_len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  M.replaying = 1;
  M.replay_done = M.replay_match = M.replay_hash_match = 0;
  cw_reply_printf(r, "{\"ticks\":%u,\"bytes\":%zu,\"input\":%u}", ticks, len, M.want_input);
}

static const char *const CREDIT_LINES[] = { "Design, code and art: Luis Montes", "Glass, boards and table drawn by the game's own scripts",
                                            "Made with cartwheel-engine and three.c (MIT)", NULL };
static const bk_credits CREDITS = { .title = "Smalti", .inspired_by = "Inspired by Azul", .lines = CREDIT_LINES };

/* the title's How to play page: the how-to card the pause menu opens, without its in-play prompt */
static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  view_help_page(canvas);
}

/* the menus' tick (bk_title, after the kit's SMALTI_SEATS start): the self-playing cart seats itself when no switch
 * came with the load */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (bk_lobby_started_by_test()) M.started_by_env = true;
#ifdef SMALTI_AUTOPILOT
  if (M.started_by_env || screen != CW_SHELL_TITLE) return;
  if (cw_frame_index() < 4) return; /* a switch sent with the load is in by now */
  M.started_by_env = true;
  static const int AUTO[4] = { CW_SHELL_CPU_HARD, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_EASY };
  cw_shell_start_seats(4, AUTO);
#else
  (void)screen;
#endif
}

static const bk_title TITLE = { .test_env = "SMALTI_SEATS", .credits = &CREDITS, .how_to_play = how_to_play, .menu_update = menu_update };

static void init(void *user) {
  (void)user;
  static const char *const NAMES[BK_MAX_SEATS] = { "Rubino", "Lapis", "Giada", "Ambra" };
  cw_shell_desc d = { 0 };
  bk_lobby_desc(&d, BK_MAX_SEATS, NAMES, NULL);
  d = (cw_shell_desc){ .title = "Smalti", .subtitle = "glass, set by hand", .max_players = d.max_players, .cpu_seats = true,
                      .hot_seats = true, .start = start, .quit = quit, .game_save_size = sizeof(save_data),
                      .pause_items = { "How to play" }, .pause_item = pause_item, .draw_background = backdrop,
                      .accent = cw_rgba(0.95f, 0.75f, 0.35f, 1) };
  for (int i = 0; i < BK_MAX_SEATS; i++) d.seat_names[i] = NAMES[i], d.seat_colors[i] = bk_seat_color(i);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  cw_input_pointer_channel(true); /* pointer picks go into the recording, the replay and the input hash */
  mg_actions_init(&M.g);
  view_init(cw_scene_app_tree());
  cw_debug_i32("game.turn", &M.turn);
  cw_debug_i32("game.round", &M.round);
  cw_debug_i32("game.over", &M.over);
  cw_debug_field("game.score", M.score, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_i32("game.cpu_moves", &M.cpu_moves);
  cw_debug_i32("game.moves", &M.moves);
  cw_debug_i32("game.help", &M.help);
  cw_debug_i32("game.mode", &M.mode);
  cw_debug_i32("game.pointer_picks", &M.walks);
  cw_debug_i32("game.busy", &M.busy);
  cw_debug_i32("game.ticks", &M.ticks);
  cw_debug_i32("game.players", &M.players_dbg);
  cw_debug_i32("game.over_tick", &M.over_tick);
  cw_debug_i32("game.ntargets", &M.ntargets);
  cw_debug_field("game.target_px", M.target_px, CW_DBG_I32, 64);
  cw_debug_field("game.target_code", M.target_code, CW_DBG_I32, 32);
  cw_debug_i32("game.matches_done", &M.matches_done);
  cw_debug_i32("game.newcomer_moves", &NB.moves);
  cw_debug_i32("game.newcomer_kinds", &NB.kinds);
  cw_debug_i32("game.help_opens", &M.g.help_opens);
  cw_debug_i32("game.cursor", &M.cursor);
  cw_debug_field("game.person_kinds", M.g.person_kinds, CW_DBG_I32, MG_KINDS);
  cw_debug_u32("game.state_hash", &M.state_hash);
  cw_debug_u32("game.shown_hash", &M.shown_hash);
  cw_debug_i32("game.replaying", &M.replaying);
  cw_debug_i32("game.replay_done", &M.replay_done);
  cw_debug_i32("game.replay_match", &M.replay_match);
  cw_debug_i32("game.replay_hash_match", &M.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (game.replay_match)", cmd_replay);
}

static void debug_fields(void) {
  const ms_state *s = mg_state(&M.g);
  M.turn = M.g.turn;
  M.round = s->round;
  M.over = M.g.over;
  if (M.g.over && !M.over_tick) M.over_tick = M.g.ticks, M.matches_done++;
  if (!M.g.over) M.over_tick = 0;
  for (int i = 0; i < BK_MAX_SEATS; i++) M.score[i] = i < s->players ? s->p[i].score : 0;
  M.cpu_moves = M.g.cpu_moves;
  M.moves = s->moves_played;
  M.help = M.g.help;
  M.mode = M.g.mode;
  M.walks = M.g.pointer_picks;
  M.busy = mg_busy(&M.g);
  M.ticks = M.g.ticks;
  M.players_dbg = M.g.players;
  M.state_hash = (uint32_t)bk_match_hash(M.g.match);
  M.cursor = mg_cursor(&M.g);
  M.ntargets = M.g.ntargets < 32 ? M.g.ntargets : 32;
  for (int i = 0; i < 32; i++) {
    cw_vec2 q = i < M.ntargets ? cw_cursors_screen(bk_select_cursors(M.g.sel), M.g.turn, i) : (cw_vec2){ -1, -1 };
    M.target_px[2 * i] = (int32_t)(q.x + 0.5f), M.target_px[2 * i + 1] = (int32_t)(q.y + 0.5f);
    /* a group: source * 16 + colour; a row: 1000 + row (1005 the strip, 998 put back) */
    M.target_code[i] = i >= M.ntargets ? -1 : M.g.mode == UI_PICK ? M.g.target[i].src * 16 + M.g.target[i].colour : 1000 + M.g.target[i].line;
  }
  M.shown_hash = (uint32_t)ms_hash(&M.g.shown);
}

/* SMALTI_NEWCOMER=1: a test player on seat 0 who knows only what the screen shows: the prompt says pick a colour or
 * place, the cursor glides with the d-pad, and the ghost shows the row choose would use. It hops a few times at
 * random, then goes for a kind of move it has not made yet when the table offers one (a group from the centre or a
 * tray, onto a row or straight to the breakage strip), walking the cursor there as a pad does, and chooses; else it
 * chooses where the cursor stands. SMALTI_NEWCOMER=lost hops but never chooses (the control). Its presses go in as
 * seat 0's actions, recorded like a pad's. */
static int newcomer_goal(void) {
  const int32_t *pk = M.g.person_kinds;
  if (M.g.mode == UI_PICK) {
    bool want_centre = !pk[MG_CENTRE_ROW] || !pk[MG_CENTRE_FLOOR], want_tray = !pk[MG_TRAY_ROW] || !pk[MG_TRAY_FLOOR];
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < M.g.ntargets; i++) {
        bool centre = M.g.target[i].src == MS_CENTRE;
        if (pass == 0 ? (centre && want_centre) : (!centre && want_tray)) return i;
      }
    return -1;
  }
  int src = M.g.pick_src;
  bool want_floor = !pk[mg_kind(src, MS_FLOOR)], want_row = !pk[mg_kind(src, 0)];
  int cur = mg_cursor(&M.g);
  for (int i = 0; i < M.g.ntargets; i++) {
    int line = M.g.target[i].line;
    if (line == MS_FLOOR && want_floor) return i;
  }
  if (want_row && cur >= 0 && M.g.target[cur].line >= 0 && M.g.target[cur].line < MS_FLOOR) return cur; /* the ghost's row */
  for (int i = 0; i < M.g.ntargets && want_row; i++)
    if (M.g.target[i].line >= 0 && M.g.target[i].line < MS_FLOOR) return i;
  return -1;
}

static void newcomer_tick(void) {
  if (!NB.mode || M.g.over || mg_seat_is_cpu(&M.g, 0)) return;
  cw_action_value v[2];
  int n = 0;
  if (M.g.turn == 0 && !mg_busy(&M.g) && !M.g.help && !M.g.targets_dirty && M.g.ntargets > 0) {
    if (NB.wait > 0) NB.wait--;
    else if (M.g.mode == UI_PICK && NB.hops > 0) { v[n++] = (cw_action_value){ M.g.act[A_RIGHT], 1 }; NB.hops--; NB.wait = 6; }
    else if (NB.mode == 2) { v[n++] = (cw_action_value){ M.g.act[A_RIGHT], 1 }; NB.wait = 6; }
    else {
      int goal = newcomer_goal(), cur = mg_cursor(&M.g);
      if (goal >= 0 && goal != cur) {
        bk_press p;
        if (!mg_press_to_target(&M.g, 0, goal, &p)) CW_FATAL("smalti newcomer: target %d cannot be reached from %d", goal, cur);
        for (int i = 0; i < p.count && n < 2; i++) v[n++] = p.v[i];
        NB.wait = 6;
      } else {
        v[n++] = (cw_action_value){ M.g.act[A_OK], 1 };
        if (M.g.mode == UI_PLACE) NB.moves++, NB.hops = cw_rng_int(&NB.rng, 0, 4);
        NB.wait = 8;
      }
    }
  }
  /* only the actions it uses (the d-pad and choose), each tick: the pad's own other buttons still reach the seat (the
   * idle-seat gate's control presses the how-to card's button on this pad) */
  static const int MINE[5] = { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK };
  for (int k = 0; k < 5; k++) {
    float s = 0;
    for (int i = 0; i < n; i++) if (v[i].action == M.g.act[MINE[k]]) s = v[i].strength;
    cw_input_set_action(0, M.g.act[MINE[k]], s);
  }
  const int32_t *pk = M.g.person_kinds;
  NB.kinds = (pk[0] > 0) + (pk[1] > 0) + (pk[2] > 0) + (pk[3] > 0);
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  newcomer_tick();
  if (!cw_shell_update()) return;
  if (!M.in_match || !M.g.match) return;
  if (M.fresh) { /* the tick the match began in is not recorded: play starts at the next */
    M.fresh = false;
    return;
  }
  if (M.replaying && !cw_input_replay_active()) {
    M.replay_match = bk_match_hash(M.g.match) == M.want_state;
    M.replay_hash_match = cw_input_replay_hash() == M.want_input;
    M.replaying = 0;
    M.replay_done = 1;
    cw_info("smalti replay: state %s, input hash %s", M.replay_match ? "same" : "DIFFERENT", M.replay_hash_match ? "same" : "DIFFERENT");
  }
  mg_tick(&M.g);
  view_tick(&M.g);
  if (M.g.over && !M.g.staging) {
    bool again = false, title = false;
    for (int i = 0; i < M.g.players; i++) {
      if (mg_seat_is_cpu(&M.g, i)) continue;
      again |= cw_action_just_pressed(i, M.g.act[A_OK]);
      title |= cw_action_just_pressed(i, M.g.act[A_BACK]);
    }
    bool all_cpu = true;
    for (int i = 0; i < M.g.players; i++) all_cpu &= mg_seat_is_cpu(&M.g, i);
    static int over_ticks;
    over_ticks++;
    if (all_cpu && over_ticks > 600) again = true; /* a self-playing table deals again */
    if (again) {
      over_ticks = 0;
      M.seed = M.seed + 1;
      new_match();
    } else if (title) {
      over_ticks = 0;
      M.in_match = false;
      cw_shell_to_title();
    }
  }
  debug_fields();
}

static void backdrop(cw_shell_screen screen, cw_vec2 canvas, void *user) {
  (void)screen, (void)user;
  view_backdrop(canvas); /* the 3D table shows through: the shell dims it under its menus */
}

static void render(void *user, double alpha) {
  (void)user;
  if (!M.in_match || !M.g.match) view_attract();
  if (M.in_match && M.g.match) {
    if (cw_shell_screen_now() == CW_SHELL_PLAY) mg_pointer(&M.g, cw_scene_app_tree());
    view_frame(&M.g, (float)alpha);
  }
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  if (M.in_match && M.g.match && cw_shell_hud_shown() && !bk_hud_hidden()) {
    view_hud(&M.g);
  }
  cw_r2d_end();
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "Smalti", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .render = render, .debug = true, .touch_pointer = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
