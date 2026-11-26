/* Rose Window (codename rosewindow): draft coloured glass dice and set them into a stained-glass window lit from
 * behind. One to four seats on one screen: people on pads, people passing a pad, or the computer at Easy, Normal or
 * Hard; alone, a player tries to beat the round track. How to play and Credits are pages on the title; the credits
 * page names the game it is inspired by (the only place it is named).
 *
 * src/rules.c is the game (pure, a bk_game), src/game.c the match (seats, the choices, the cursor), src/view.c the
 * table and the HUD, this file the shell and the frame.
 *
 * Test switches (cw_test_env):
 *   ROSEWINDOW_SEATS=c2,p,...  skip the menus (read in the shell's menu tick, bk_title): p a person on a pad, h a hot seat, c0..c2 a
 *                              computer at that level (the self-playing cart rosewindow_auto seats c2,c1,c1,c0 itself)
 *   ROSEWINDOW_SEED=n          the game seed (default: a new one each match; the self-playing cart: 1)
 *   ROSEWINDOW_FAST=1          short waits between steps
 *   ROSEWINDOW_NEWCOMER=1      a test player on seat 1 who uses only what the screen offers (=lost: never chooses); with
 *                              no seats switch it starts itself against a Normal computer
 * The debug request `replay` replays the match from its own input recording and compares (game.replay_match). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_shell.h"
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
  int over_ticks;
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint64_t want_state;
  uint32_t want_input;
  uint8_t *recording;
  size_t recording_len;
  int32_t turn, round, phase, over, score[BK_MAX_SEATS], cpu_moves, moves, help, plen, busy, ticks, players_dbg, credits, matches_done,
      pointer_picks, undos, tools_used, track_sum, winners, dice_moving, ghosts, lit_panes, favour[BK_MAX_SEATS], over_frame,
      person_kinds[4], person_tools, cursor, tool_kinds[RW_TOOLS_IN_PLAY];
  uint32_t state_hash;
  int32_t ntargets, target_px[2 * 32], target_code[32];
  double last_time;
} M;

/* the newcomer test player: want is the kind of lit choice it is curious to try next (0 none), sought the hops spent
 * looking for it, tried a bit per kind it has chosen at the start of a decision */
static struct { int mode, wait, hops, dir, last, want, sought, tried; cw_rng rng; int32_t moves; } NB;

static void set_view(void) {
  mg_set_view(&M.g, (cw_view3d){ cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "World"), "Camera"), { 0, 0, 1280, 720 } });
}

static void new_match(void) {
  set_view();
  const char *nb = cw_test_env("ROSEWINDOW_NEWCOMER");
  NB.mode = nb && *nb ? (strcmp(nb, "lost") == 0 ? 2 : 1) : 0;
  cw_rng_seed(&NB.rng, 77);
  NB.hops = 2;
  NB.moves = 0, NB.dir = 0, NB.last = -2, NB.wait = 0, NB.want = 0, NB.sought = 0, NB.tried = 0;
  save_data *sv = cw_shell_game_save();
  mg_start(&M.g, M.players, M.seats_cpu, M.seed, M.fast);
  view_start(&M.g);
  bool any_person = false;
  for (int i = 0; i < M.players; i++) any_person |= M.seats_cpu[i] < 0;
  M.g.help = any_person && sv && !sv->seen_help && !cw_test_env("ROSEWINDOW_SEATS") && !NB.mode;
  if (sv && any_person) {
    sv->seen_help = 1;
    sv->matches++;
    cw_shell_save_commit();
  }
  M.fresh = true;
  M.in_match = true;
  M.over_ticks = 0;
  cw_input_record_begin();
}

static uint64_t fresh_seed(void) {
  const char *e = cw_test_env("ROSEWINDOW_SEED");
  if (e && *e) return strtoull(e, NULL, 10);
#ifdef ROSEWINDOW_AUTOPILOT
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
  M.players = players > BK_MAX_SEATS ? BK_MAX_SEATS : players < 1 ? 1 : players;
  M.seed = fresh_seed();
  const char *f = cw_test_env("ROSEWINDOW_FAST");
  M.fast = f && *f == '1';
#ifdef ROSEWINDOW_AUTOPILOT
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
  mg_start(&M.g, M.players, M.seats_cpu, M.seed, M.fast);
  view_start(&M.g);
  NB.hops = 2, NB.wait = 0, NB.moves = 0, NB.dir = 0, NB.last = -2, NB.want = 0, NB.sought = 0, NB.tried = 0;
  cw_rng_seed(&NB.rng, 77);
  if (!cw_input_replay_begin(M.recording, M.recording_len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  M.replaying = 1;
  M.replay_done = M.replay_match = M.replay_hash_match = 0;
  cw_reply_printf(r, "{\"ticks\":%u,\"bytes\":%zu,\"input\":%u}", ticks, len, M.want_input);
}

static const char *const SEAT_NAMES[BK_MAX_SEATS] = { "Rosa", "Bram", "Wren", "Ines" };

static const char *const CREDIT_LINES[] = { "Design, code and art: Luis Montes", "Glass, windows, cards and table drawn by the game's own scripts",
                                            "Made with cartwheel-engine and three.c (MIT)", NULL };
static const bk_credits CREDITS = { .title = "Rose Window", .inspired_by = "Inspired by Sagrada", .lines = CREDIT_LINES };

/* the title's How to play page: the in-play card without its prompt line (the shell draws the way back) */
static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  view_help_page(canvas);
}

/* the menus' tick (bk_title, after the kit read ROSEWINDOW_SEATS; it runs while the scene app's tree is paused):
 * the newcomer alone starts itself against a Normal computer, the self-playing cart four computers. Frame 4: a host's
 * env request (sent at frame 2) is in by then. */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (M.started_by_env || screen != CW_SHELL_TITLE || cw_frame_index() < 4) return;
  if (bk_lobby_started_by_test()) { M.started_by_env = true; return; }
  const char *nb = cw_test_env("ROSEWINDOW_NEWCOMER");
  if (nb && *nb) { /* the newcomer alone (a programs survey): a person against a Normal computer */
    static const int SEATS[2] = { CW_SHELL_SEAT_PAD, CW_SHELL_CPU_NORMAL };
    M.started_by_env = true;
    cw_shell_start_seats(2, SEATS);
    return;
  }
#ifdef ROSEWINDOW_AUTOPILOT
  M.started_by_env = true;
  static const int AUTO[4] = { CW_SHELL_CPU_HARD, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_EASY };
  cw_shell_start_seats(4, AUTO);
#endif
}

static const bk_title TITLE = { .test_env = "ROSEWINDOW_SEATS", .credits = &CREDITS, .how_to_play = how_to_play, .menu_update = menu_update };

static void init(void *user) {
  (void)user;
  cw_shell_desc d = { 0 };
  bk_lobby_desc(&d, BK_MAX_SEATS, SEAT_NAMES, NULL);
  d.title = "Rose Window";
  d.subtitle = "glass dice, lit from behind";
  d.start = start;
  d.quit = quit;
  d.game_save_size = sizeof(save_data);
  d.pause_items[0] = "How to play";
  d.pause_item = pause_item;
  d.accent = cw_rgba(0.95f, 0.7f, 0.3f, 1);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  mg_actions_init(&M.g);
  view_init(cw_scene_app_tree());
  cw_debug_i32("game.turn", &M.turn);
  cw_debug_i32("game.round", &M.round);
  cw_debug_i32("game.phase", &M.phase);
  cw_debug_i32("game.over", &M.over);
  cw_debug_field("game.score", M.score, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_field("game.favour", M.favour, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_i32("game.cpu_moves", &M.cpu_moves);
  cw_debug_i32("game.moves", &M.moves);
  cw_debug_i32("game.help", &M.help);
  cw_debug_i32("game.plen", &M.plen);
  cw_debug_i32("game.busy", &M.busy);
  cw_debug_i32("game.ticks", &M.ticks);
  cw_debug_i32("game.players", &M.players_dbg);
  cw_debug_field("game.person_kinds", M.person_kinds, CW_DBG_I32, 4);
  cw_debug_i32("game.person_tools", &M.person_tools);
  cw_debug_i32("game.cursor", &M.cursor);
  cw_debug_field("game.tool_kinds", M.tool_kinds, CW_DBG_I32, RW_TOOLS_IN_PLAY);
  cw_debug_i32("game.matches_done", &M.matches_done);
  cw_debug_i32("game.over_frame", &M.over_frame);
  cw_debug_i32("game.pointer_picks", &M.pointer_picks);
  cw_debug_i32("game.undos", &M.undos);
  cw_debug_i32("game.tools_used", &M.tools_used);
  cw_debug_i32("game.track_sum", &M.track_sum);
  cw_debug_i32("game.winners", &M.winners);
  cw_debug_i32("game.dice_moving", &M.dice_moving);
  cw_debug_i32("game.ghosts", &M.ghosts);
  cw_debug_i32("game.lit_panes", &M.lit_panes);
  cw_debug_i32("game.ntargets", &M.ntargets);
  cw_debug_field("game.target_px", M.target_px, CW_DBG_I32, 64);
  cw_debug_field("game.target_code", M.target_code, CW_DBG_I32, 32);
  cw_debug_i32("game.newcomer_moves", &NB.moves);
  cw_debug_u32("game.state_hash", &M.state_hash);
  cw_debug_i32("game.replaying", &M.replaying);
  cw_debug_i32("game.replay_done", &M.replay_done);
  cw_debug_i32("game.replay_match", &M.replay_match);
  cw_debug_i32("game.replay_hash_match", &M.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (game.replay_match)", cmd_replay);
}

static void debug_fields(void) {
  const rw_state *s = mg_state(&M.g);
  M.turn = M.g.turn;
  M.round = s->round;
  M.phase = s->phase;
  if (M.g.over && !M.over) M.matches_done++, M.over_frame = (int32_t)cw_frame_index();
  M.over = M.g.over;
  for (int i = 0; i < BK_MAX_SEATS; i++) {
    rw_breakdown b = { 0 };
    if (i < s->players && s->p[i].frame < RW_FRAMES) rw_score(s, i, &b);
    M.score[i] = i < s->players ? (s->phase == RW_PHASE_OVER ? s->p[i].score : b.total) : 0;
    M.favour[i] = i < s->players ? s->p[i].favour : 0;
  }
  M.cpu_moves = M.g.cpu_moves;
  M.moves = s->moves_played;
  M.help = M.g.help;
  M.plen = M.g.plen;
  M.busy = mg_busy(&M.g);
  M.ticks = M.g.ticks;
  M.players_dbg = M.g.players;
  M.pointer_picks = M.g.pointer_picks;
  M.undos = M.g.undos;
  M.tools_used = M.g.tools_used;
  memcpy(M.person_kinds, M.g.person_kinds, sizeof M.person_kinds);
  M.person_tools = M.g.person_tools;
  M.cursor = mg_cursor_id(&M.g);
  for (int i = 0; i < RW_TOOLS_IN_PLAY; i++) M.tool_kinds[i] = s->tools[i];
  M.track_sum = s->track_sum;
  M.winners = s->phase == RW_PHASE_OVER ? (int32_t)rw_winners(s) : -1;
  M.state_hash = (uint32_t)bk_match_hash(M.g.match);
  M.ntargets = M.g.ntargets < 32 ? M.g.ntargets : 32;
  for (int i = 0; i < 32; i++) {
    cw_vec2 q = i < M.ntargets ? cw_cursors_screen(bk_select_cursors(M.g.sel), M.g.turn, i) : (cw_vec2){ -1, -1 };
    M.target_px[2 * i] = (int32_t)(q.x + 0.5f), M.target_px[2 * i + 1] = (int32_t)(q.y + 0.5f);
    M.target_code[i] = i < M.ntargets ? M.g.target[i] : -1;
  }
}

/* ROSEWINDOW_NEWCOMER=1: a test player on seat 1 who knows only what the screen shows: the prompt names choose, the
 * cursor glides between the lit choices with the arrows, and every path the screen offers ends in a move. It hops a
 * few times at random, then chooses, again and again. It is curious once about each kind of thing that lights up
 * at the start of a turn: from the second round it looks for a lit tool card (and follows what the tool asks), from
 * the fourth the bell, and takes it when the cursor finds it (after 40 hops without finding it, it gives up until
 * its next turn). =lost hops but never chooses (the control). Its presses go in as seat 1's actions, recorded like
 * a pad's. */
static void newcomer_tick(void) {
  if (!NB.mode || !M.in_match || !M.g.match || M.g.over || mg_seat_is_cpu(&M.g, 0)) { if (NB.mode) cw_input_set_actions(0, NULL, 0); return; }
  cw_action_value v[1];
  int n = 0;
  static const int DIRS[4] = { A_RIGHT, A_DOWN, A_LEFT, A_UP };
  if (M.g.turn == 0 && !mg_busy(&M.g) && !M.g.help && !M.g.cards) {
    int id = mg_cursor_id(&M.g), round = mg_state(&M.g)->round;
    /* what it is curious about at the start of this decision: something lit it has not chosen yet */
    if (NB.mode == 1 && M.g.plen == 0 && NB.wait == 0 && !NB.want && NB.sought == 0) {
      bool lit[8] = { 0 };
      for (int i = 0; i < M.g.ntargets; i++) lit[tg_kind(M.g.target[i]) & 7] = true;
      if (round >= 2 && lit[TG_TOOL] && !(NB.tried & 1 << TG_TOOL)) NB.want = TG_TOOL;
      else if (round >= 4 && lit[TG_PASS] && !(NB.tried & 1 << TG_PASS)) NB.want = TG_PASS;
    }
    /* a hop that left the cursor where it was: try the next way round (right, down, left, up) */
    if (NB.wait == 1 && id == NB.last) NB.dir = (NB.dir + 1) % 4;
    if (NB.wait > 0) NB.wait--;
    else if (NB.want && M.g.plen == 0 && !(id >= 0 && tg_kind(id) == NB.want)) {
      v[n++] = (cw_action_value){ M.g.act[DIRS[NB.dir]], 1 };
      NB.last = id;
      NB.wait = 6;
      if (++NB.sought > 40) NB.want = 0; /* not found this turn: plain play, and look again next turn */
    } else if (!NB.want && (NB.hops > 0 || NB.mode == 2 || (id >= 0 && tg_kind(id) == TG_BACK))) {
      /* hop along the lit choices; never confirm "put back" (the newcomer moves on to a lit choice instead) */
      v[n++] = (cw_action_value){ M.g.act[DIRS[NB.dir]], 1 };
      if (NB.hops > 0) NB.hops--;
      NB.last = id;
      NB.wait = 6;
    } else if (NB.mode != 2) {
      if (NB.want && id >= 0 && tg_kind(id) == NB.want) NB.tried |= 1 << NB.want, NB.want = 0, NB.sought = 0;
      v[n++] = (cw_action_value){ M.g.act[A_OK], 1 };
      NB.last = -2;
      NB.moves++;
      NB.hops = cw_rng_int(&NB.rng, 0, 3);
      NB.wait = 8;
    }
  }
  if (M.g.turn != 0) NB.sought = 0, NB.want = 0;
  cw_input_set_actions(0, v, n);
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  newcomer_tick();
  if (!cw_shell_update()) return;
  if (!M.in_match || !M.g.match) return;
  if (M.fresh) { M.fresh = false; return; }
  if (M.replaying && !cw_input_replay_active()) {
    M.replay_match = bk_match_hash(M.g.match) == M.want_state;
    M.replay_hash_match = cw_input_replay_hash() == M.want_input;
    M.replaying = 0;
    M.replay_done = 1;
    cw_info("rosewindow replay: state %s, input hash %s", M.replay_match ? "same" : "DIFFERENT", M.replay_hash_match ? "same" : "DIFFERENT");
  }
  mg_tick(&M.g);
  if (M.g.over) {
    M.over_ticks++;
    bool again = false, title = false;
    for (int i = 0; i < M.g.players && M.over_ticks > 60; i++) {
      if (mg_seat_is_cpu(&M.g, i)) continue;
      again |= cw_action_just_pressed(i, M.g.act[A_OK]);
      title |= cw_action_just_pressed(i, M.g.act[A_BACK]);
    }
    bool all_cpu = true;
    for (int i = 0; i < M.g.players; i++) all_cpu &= mg_seat_is_cpu(&M.g, i);
    if (all_cpu && M.over_ticks > 600) again = true;
    if (again) {
      M.seed = M.seed + 1;
      debug_fields();
      new_match();
      return;
    } else if (title) {
      M.in_match = false;
      cw_shell_to_title();
    }
  }
  debug_fields();
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  /* animation steps a fixed sixtieth a frame, never the wall clock: every host draws the same frame N */
  float dt = 1.0f / 60.0f;
  if (!M.in_match || !M.g.match) view_attract(dt);
  if (M.in_match && M.g.match) {
    if (cw_shell_screen_now() == CW_SHELL_PLAY) mg_pointer(&M.g, cw_scene_app_tree());
    view_frame(&M.g, dt);
    M.dice_moving = view_dice_moving();
    M.ghosts = view_ghosts_shown();
    M.lit_panes = view_lit_panes();
  }
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  if (M.in_match && M.g.match && cw_shell_hud_shown() && !bk_hud_hidden()) {
    view_hud(&M.g, dt);
  }
  cw_r2d_end();
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = { .title = "Rose Window", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .render = render, .debug = true, .touch_pointer = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
