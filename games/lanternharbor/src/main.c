/* Lantern Harbor (lanternharbor): take a job each round, claim plots, raise a guild town and ship its goods for the
 * lanterns that light the quay. Two to four seats on one screen: people on pads, people passing a pad, or the computer
 * at Easy, Normal or Hard. The title carries How to play and Credits; the credits name the game it is inspired by (the
 * only place it is named).
 *
 * src/rules.c is the game (pure, a bk_game), src/game.c the match (seats, the cursor, the table shown move by move),
 * src/view.c the harbour and the HUD, this file the shell and the frame.
 *
 * Test switches (cw_test_env):
 *   LANTERNHARBOR_SEATS=c2,p,...  skip the menus (the shell's menu tick, bk_title_desc): p a person on a pad, h a hot seat, c0..c2 a
 *                                 computer at that level (the self-playing cart lanternharbor_auto starts c2,c1,c1,c0)
 *   LANTERNHARBOR_SEED=n          the match seed (default: a new one each match; the self-playing cart: 1)
 *   LANTERNHARBOR_FAST=1          short waits between moves
 *   LANTERNHARBOR_NEWCOMER=1      a test player on seat 1 who only follows the screen (=lost: it never chooses)
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
  int32_t phase, lit, cursor;
  int32_t over_tick, matches_done;
  int32_t ntargets, target_px[2 * 32], target_code[32]; /* the cursor's targets: canvas points (tests tap there) and what each is */
} M;

static void backdrop(cw_shell_screen screen, cw_vec2 canvas, void *user);

static struct { int mode, wait, hops, search; cw_rng rng; int32_t moves; } NB; /* the newcomer test player (newcomer_tick) */

static void set_view(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  if (sz.x <= 0 || sz.y <= 0) sz = (cw_vec2){ 1280, 720 };
  mg_set_view(&M.g, (cw_view3d){ cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "World"), "Camera"), { 0, 0, 1280, 720 } });
  (void)sz;
}

static void new_match(void) {
  set_view();
  const char *nb = cw_test_env("LANTERNHARBOR_NEWCOMER");
  NB.mode = nb && *nb ? (strcmp(nb, "lost") == 0 ? 2 : 1) : 0;
  cw_rng_seed(&NB.rng, 77);
  NB.hops = 2;
  NB.search = 0;
  save_data *sv = cw_shell_game_save();
  mg_start(&M.g, M.players, M.seats_cpu, M.seed, M.fast);
  view_start(&M.g);
  bool any_person = false;
  for (int i = 0; i < M.players; i++) any_person |= M.seats_cpu[i] < 0;
  M.g.help = any_person && sv && !sv->seen_help && !cw_test_env("LANTERNHARBOR_SEATS");
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
  const char *e = cw_test_env("LANTERNHARBOR_SEED");
  if (e && *e) return strtoull(e, NULL, 10);
#ifdef LANTERNHARBOR_AUTOPILOT
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
  const char *f = cw_test_env("LANTERNHARBOR_FAST");
  M.fast = f && *f == '1';
#ifdef LANTERNHARBOR_AUTOPILOT
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

static const char *const CREDIT_LINES[] = { "Design, code and art: Luis Montes", "Boards, quay and table drawn by the game's own scripts", "Cards, boats and townsfolk drawn by its code",
                                            "Made with cartwheel-engine and three.c (MIT)", NULL };
static const bk_credits CREDITS = { .title = "Lantern Harbor", .inspired_by = "Inspired by Puerto Rico", .lines = CREDIT_LINES };

static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  view_help_page(canvas);
}

/* the menus tick (bk_title, after the kit reads LANTERNHARBOR_SEATS): the self-playing cart seats itself when no
 * switch did. Frame 4: a switch sent with the load is in by then. */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
#ifdef LANTERNHARBOR_AUTOPILOT
  if (screen == CW_SHELL_TITLE && !M.started_by_env && !bk_lobby_started_by_test() && cw_frame_index() >= 4) {
    static const int AUTO[4] = { CW_SHELL_CPU_HARD, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_EASY };
    cw_shell_start_seats(4, AUTO);
    M.started_by_env = true;
  }
#else
  (void)screen;
#endif
}

static const bk_title TITLE = { .test_env = "LANTERNHARBOR_SEATS", .credits = &CREDITS, .how_to_play = how_to_play, .menu_update = menu_update };

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


static void init(void *user) {
  (void)user;
  static const char *const NAMES[BK_MAX_SEATS] = { "Ilse", "Tomas", "Mira", "Oren" };
  cw_shell_desc d = { 0 };
  bk_lobby_desc(&d, BK_MAX_SEATS, NAMES, NULL);
  d = (cw_shell_desc){ .title = "Lantern Harbor", .subtitle = "a guild town at dusk", .max_players = d.max_players, .cpu_seats = true,
                      .hot_seats = true, .start = start, .quit = quit, .game_save_size = sizeof(save_data),
                      .pause_items = { "How to play" }, .pause_item = pause_item, .draw_background = backdrop,
                      .accent = cw_rgba(0.98f, 0.72f, 0.32f, 1) };
  for (int i = 0; i < BK_MAX_SEATS; i++) d.seat_names[i] = NAMES[i], d.seat_colors[i] = bk_seat_color(i);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  mg_actions_init(&M.g);
  view_init(cw_scene_app_tree());
  cw_debug_i32("game.turn", &M.turn);
  cw_debug_i32("game.round", &M.round);
  cw_debug_i32("game.phase", &M.phase);
  cw_debug_i32("game.lanterns_lit", &M.lit);
  cw_debug_i32("game.over", &M.over);
  cw_debug_field("game.score", M.score, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_i32("game.cpu_moves", &M.cpu_moves);
  cw_debug_field("game.moves_by", M.g.moves_by, CW_DBG_I32, BK_MAX_SEATS);
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
  cw_debug_i32("game.cursor", &M.cursor);
  /* a person's moves by type (rules.h M_PASS..M_JETTY), entered on the pad and by the pointer */
  cw_debug_field("game.pad_types", M.g.ui_types[0], CW_DBG_I32, 16);
  cw_debug_field("game.pointer_types", M.g.ui_types[1], CW_DBG_I32, 16);
  cw_debug_i32("game.newcomer_moves", &NB.moves);
  cw_debug_u32("game.state_hash", &M.state_hash);
  cw_debug_u32("game.shown_hash", &M.shown_hash);
  cw_debug_i32("game.replaying", &M.replaying);
  cw_debug_i32("game.replay_done", &M.replay_done);
  cw_debug_i32("game.replay_match", &M.replay_match);
  cw_debug_i32("game.replay_hash_match", &M.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (game.replay_match)", cmd_replay);
}

static void debug_fields(void) {
  const hb_state *s = mg_state(&M.g);
  M.turn = M.g.turn;
  M.round = s->round;
  M.phase = s->phase;
  M.lit = view_lanterns_lit();
  M.over = M.g.over;
  if (M.g.over && !M.over_tick) M.over_tick = M.g.ticks, M.matches_done++;
  if (!M.g.over) M.over_tick = 0;
  for (int i = 0; i < BK_MAX_SEATS; i++) M.score[i] = i < s->players ? hb_score(&s->p[i]) : 0;
  M.cpu_moves = M.g.cpu_moves;
  M.moves = s->moves_played;
  M.help = M.g.help;
  M.mode = M.g.mode;
  M.walks = M.g.pointer_picks;
  M.busy = mg_busy(&M.g) || M.g.targets_dirty; /* tests: the targets this field lists are current only once rebuilt */
  M.ticks = M.g.ticks;
  M.players_dbg = M.g.players;
  M.state_hash = (uint32_t)bk_match_hash(M.g.match);
  M.ntargets = M.g.ntargets < 32 ? M.g.ntargets : 32;
  M.cursor = M.g.sel ? mg_cursor(&M.g) : -1;
  for (int i = 0; i < 32; i++) {
    cw_vec2 q = i < M.ntargets ? cw_cursors_screen(bk_select_cursors(M.g.sel), M.g.turn, i) : (cw_vec2){ -1, -1 };
    M.target_px[2 * i] = (int32_t)(q.x + 0.5f), M.target_px[2 * i + 1] = (int32_t)(q.y + 0.5f);
    /* a group: source * 16 + colour; a row: 1000 + row (1005 the strip, 998 put back) */
    /* a move: type * 100 + a * 4 + b; a good to load: 2000 + good; put back: 2100 */
    const mg_target *t = &M.g.target[i];
    M.target_code[i] = i >= M.ntargets ? -1 : t->kind == TG_GOOD ? 2000 + t->good : t->kind == TG_PUTBACK ? 2100
                                                                   : hb_move_type(t->move) * 100 + hb_move_a(t->move) * 4 + hb_move_b(t->move);
  }
  M.shown_hash = (uint32_t)hb_hash(&M.g.shown);
}

/* how much the newcomer wants a choice, from what the table and the focus panel show: a kind of choice it has not
 * made yet, or a building (or a place in one) whose panel offers a kind of choice it has not had (the seed shed's
 * hidden plot, the private jetty's shipping) */
static int newcomer_want(const mg_target *t);
static int newcomer_best(void) {
  int best = 0;
  for (int i = 0; i < M.g.ntargets; i++) {
    int w = newcomer_want(&M.g.target[i]);
    if (w > best) best = w;
  }
  return best;
}

static int newcomer_want(const mg_target *t) {
  if (!t || t->kind != TG_MOVE) return 0;
  const int32_t *used = M.g.ui_types[0];
  const hb_state *st = mg_state(&M.g);
  const hb_player *me = &st->p[0];
  int type = hb_move_type(t->move), a = hb_move_a(t->move);
  if ((type == M_BUILD || type == M_PLACE_BLD) && ((a == B_SEED_SHED && !used[M_SEED]) || (a == B_JETTY && !used[M_JETTY]))) return 3;
  /* the private jetty last: once every other kind of choice (but the hidden plot) is made, it works toward the one
   * building whose panel offers a kind of shipping not had yet: coins first (the merchant, the diver, selling, no
   * other building), then the mason when it can pay, the ferry to staff it, the skipper to use it */
  bool others = true;
  for (int k = 0; k <= M_JETTY; k++)
    if (k != M_JETTY && k != M_SEED && !used[k]) others = false;
  if (others && !used[M_JETTY] && (st->bld_supply[B_JETTY] || hb_owns(me, B_JETTY))) {
    bool owns = hb_owns(me, B_JETTY), staffed = hb_staffed(me, B_JETTY);
    bool afford = !owns && hb_build_cost(st, 0, B_JETTY) <= me->coins;
    if (type == M_ROLE) {
      if (staffed) return a == R_SKIPPER ? 2 : 0;
      if (owns) return a == R_FERRYMASTER ? 2 : 0;
      if (afford) return a == R_MASON ? 2 : 0;
      return a == R_MERCHANT || a == R_DIVER ? 2 : 0;
    }
    if (type == M_PASS && st->phase == PH_BUILD) return owns ? 0 : 1;
    if (type == M_SELL) return 1;
    if (type == M_JETTY) return 2;
    return 0;
  }
  if (type == M_ROLE) { /* a job whose card names a kind of choice not made yet */
    static const int8_t KINDS[HB_ROLES][3] = { [R_SURVEYOR] = { M_TAKE, M_STONE, M_SEED }, [R_FERRYMASTER] = { M_REARRANGE, M_PLACE_PLOT, M_PLACE_BLD },
                                               [R_MASON] = { M_BUILD, M_BUILD, M_BUILD }, [R_ARTISAN] = { M_EXTRA, M_EXTRA, M_EXTRA },
                                               [R_MERCHANT] = { M_SELL, M_SELL, M_SELL }, [R_SKIPPER] = { M_LOAD, M_JETTY, M_LOAD },
                                               [R_DIVER] = { M_ROLE, M_ROLE, M_ROLE } };
    for (int k = 0; k < 3; k++)
      if (!used[KINDS[a][k]]) return 1;
    return used[type] ? 0 : 1;
  }
  return used[type] ? 0 : 1;
}

/* LANTERNHARBOR_NEWCOMER=1: a test player on seat 0 who knows only what the screen shows: the prompt says what to do,
 * the cursor glides over the choices the table offers (only legal ones) with left and right, and the ghost and the
 * focus panel show what choose would do. It hops a few times at random, then on toward a kind of choice it has not
 * tried yet when the table offers one (newcomer_want), and chooses; after choosing a good to load it chooses the boat
 * the cursor lands on. LANTERNHARBOR_NEWCOMER=lost hops but never chooses (the control). Its presses go in as seat
 * 0's actions, recorded like a pad's. */
static void newcomer_tick(void) {
  if (!NB.mode || M.g.over || mg_seat_is_cpu(&M.g, 0)) return;
  cw_action_value v[1];
  int n = 0;
  if (M.g.turn == 0 && !mg_busy(&M.g) && !M.g.help) {
    if (NB.wait > 0) NB.wait--;
    else if (NB.hops > 0) { v[n++] = (cw_action_value){ M.g.act[A_RIGHT], 1 }; NB.hops--; NB.wait = 3; }
    else if (NB.mode == 2) { v[n++] = (cw_action_value){ M.g.act[A_RIGHT], 1 }; NB.wait = 3; }
    else if (NB.search < 2 * M.g.ntargets && newcomer_want(mg_cursor_target(&M.g)) < newcomer_best()) {
      /* along the row first, then down a row now and then (the cursor hops to the nearest choice that way) */
      v[n++] = (cw_action_value){ M.g.act[NB.search % 4 == 3 ? A_DOWN : A_RIGHT], 1 };
      NB.search++;
      NB.wait = 3;
    } else {
      v[n++] = (cw_action_value){ M.g.act[A_OK], 1 };
      const mg_target *t = mg_cursor_target(&M.g);
      if (t && t->kind == TG_MOVE) NB.moves++;
      NB.hops = M.g.mode == UI_PICK && t && t->kind == TG_GOOD ? 0 : cw_rng_int(&NB.rng, 0, 3);
      NB.search = 0;
      NB.wait = 4;
    }
  }
  cw_input_set_actions(0, v, n);
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
    cw_info("lanternharbor replay: state %s, input hash %s", M.replay_match ? "same" : "DIFFERENT", M.replay_hash_match ? "same" : "DIFFERENT");
  }
  mg_tick(&M.g);
  view_tick(&M.g);
  if (M.g.over) {
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
  (void)canvas; /* the 3D harbour shows through: the shell dims it under its menus */
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
  static cw_app_desc base = { .title = "Lantern Harbor", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                              .render = render, .debug = true, .touch_pointer = true };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
