/* Crokinole: the traditional flicking game on a round board, for two to four players (four as two teams), against
 * the computer or each other, on cartwheel and three.c.
 *
 *   title -> join (seats: a person on a pad, pass-the-pad seats, CPU Easy / Normal / Hard) -> the match
 *   match  the seat to move slides its disc along the shooting line, turns its aim, holds to charge the power meter
 *          and lets go (or drags back from the disc with a finger or the mouse); the camera stands behind the shooter.
 *          A ghost line shows where the shot meets its first disc or peg, and while charging where it would come to
 *          rest. After every round a card counts it; the first side to the target wins.
 *   crokinole_auto plays itself (two Normal CPUs unless CRK_SEATS says otherwise).
 *   Test switches (any build): CRK_SEATS (the kit's lobby switch: "c2,c0", "p,c1"), CRK_TARGET (points), CRK_SEED, CRK_IDLE (seconds before
 *   the computer plays an idle person's turn; 0 never), CRK_SPIN (1: the Spin setting on).
 *   The title carries How to play and Credits as pages (bk_title_desc). The pointer is the engine's recorded channel.
 *   Debug command `input_replay`: replay the match so far from its own input recording (the pad, the pointer and the CPU's
 *   presses) and compare the state (crk.replay_match) and the input hash (crk.replay_hash_match). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "boardkit/bk_shell.h"
#include "boardkit/bk_view.h"
#include "game.h"
#include "sound.h"
#include "view.h"

enum { OV_NONE, OV_HOWTO };

static struct {
  game g;
  bool in_match, built, auto_started, fresh;
  int players, kind[4], level[4];
  int len_choice, spin_choice;
  int overlay, overlay_t;
  bool howto_seen, start_howto_seen;
  uint64_t start_seed;
  /* the input replay (debug command input_replay) */
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint32_t want_hash, want_input;
  uint8_t *recording;
  size_t recording_len;
  uint32_t ticks;
  uint64_t seed;
  cw_node camera;
  cw_font font;
  float slide_speed;
  /* the ghost (what the HUD draws; the newcomer bot reads the same through the debug fields) */
  int ghost_kind, ghost_index;   /* 0 nothing (the line runs to the edge), 1 an opposing disc, 2 an own disc, 3 a peg */
  float ghost_x, ghost_y, ghost_t, stop_x, stop_y, stop_t;
  int32_t ghost_opp, stop_mm, seat_shots[4], seat_fouls[4];
  /* debug fields */
  int32_t d_phase, d_turn, d_round, d_rounds, d_shots, d_over, d_winner, d_fouls, d_twenties, d_contacts, d_checks;
  int32_t d_cpu_shots, d_pad_shots, d_ptr_shots, d_standby, d_replays, d_players, d_target, d_onboard, d_opp, d_open;
  int32_t d_pos, d_aim, d_meter, d_charging, d_cpu, d_thinking, d_overlay, d_shots_round, d_in_hand, d_matches;
  int32_t d_spin, d_spin_on, d_cancels, d_slides, d_spins;
  int32_t score[4], round_pts[4], finished, last_winner, last_rounds, final_score[4];
  uint32_t hash;
  float d_disc_x, d_disc_y, d_dir_x, d_dir_y, d_disc_wx, d_disc_wy;
} G;

static const char *const CREDIT_LINES[] = {
  "Crokinole is a traditional game, played on wooden boards since the 1800s.",
  "Rules restated in our own words; the board, discs, art and sound are made in code.",
  "Game: Luis Montes",
  "Built on cartwheel-engine and three.c (MIT licence)",
  NULL,
};
static const bk_credits CREDITS = { .title = "Crokinole", .inspired_by = "A traditional game, free for everyone to play", .lines = CREDIT_LINES };
static void draw_howto(cw_vec2 cs, bool over_play);

static const char *const LEN_CHOICES[] = { "100 points", "50 points", "One round" };
static const int LEN_TARGET[] = { 100, 50, 1 };
static const char *const SPIN_CHOICES[] = { "Off", "On" };
static const char *const SIDE_NAME[3] = { "Maple", "Ebony", "Cherry" };

/* ── the match ───────────────────────────────────────────────────────── */

static int test_int(const char *name, int dflt) {
  const char *v = cw_test_env(name);
  return v && *v ? atoi(v) : dflt;
}

static int target_now(void) { return test_int("CRK_TARGET", LEN_TARGET[CW_CLAMP(G.len_choice, 0, 2)]); }
static bool spin_now(void) { return test_int("CRK_SPIN", G.spin_choice) == 1; } /* CRK_SPIN=1: the Spin setting on */

static void new_game(void) {
  game_new(&G.g, G.players, G.kind, G.level, target_now(), spin_now(), G.seed);
  G.g.idle_limit = test_int("CRK_IDLE", IDLE_TICKS / 60) * 60; /* a test's idle seat (seconds; 0 never) */
  G.in_match = true;
  G.d_matches++;
  bool any_person = false;
  for (int s = 0; s < G.players; s++)
    if (G.kind[s] != SEAT_CPU) any_person = true;
  G.overlay = any_person && !G.howto_seen ? OV_HOWTO : OV_NONE, G.overlay_t = 0;
}

static void begin_match(void) {
  G.seed = (uint64_t)test_int("CRK_SEED", (int)(G.seed ? G.seed : 1));
  new_game();
}

static void start(int players, void *u) {
  (void)u, (void)players;
  bk_seat seats[4];
  int n = bk_lobby_seats(seats, 4);
  for (int s = 0; s < n; s++) {
    G.kind[s] = seats[s].kind == BK_SEAT_CPU ? SEAT_CPU : seats[s].kind == BK_SEAT_HOT ? SEAT_HOT : SEAT_PAD;
    G.level[s] = seats[s].kind == BK_SEAT_CPU ? seats[s].level : 1;
  }
  if (n < 2) G.kind[1] = SEAT_CPU, G.level[1] = 1, n = 2; /* alone: a CPU across the board */
  G.players = n;
  G.seed = 0x9e3779b9u ^ (uint64_t)cw_frame_index() * 2654435761u; /* the frames a person took to start: every match differs, a replay agrees */
  G.start_howto_seen = G.howto_seen;
  begin_match();
  G.start_seed = G.seed;
  G.replaying = G.replay_done = 0;
  G.fresh = true;
  cw_input_record_begin(); /* the whole session from here: pad, pointer and the CPU's presses */
}

static void quit(void *u) {
  (void)u;
  G.in_match = false;
  sound_silence();
}

static void pause_item(int index, void *u) {
  (void)u;
  if (index == 0) G.overlay = OV_HOWTO, G.overlay_t = 0;
}

static void how_to_play(cw_vec2 canvas, void *u) {
  (void)u;
  draw_howto(canvas, false);
}

/* the menus' tick (after the kit's, which starts CRK_SEATS): crokinole_auto with no switch starts two Normal CPUs.
 * Frame 3: a host's env request (sent at frame 2) is in by then. */
static void menu_update(cw_shell_screen screen, void *u) {
  (void)u;
#ifdef CROKINOLE_AUTOPILOT
  if (screen == CW_SHELL_TITLE && !G.auto_started && !bk_lobby_started_by_test() && cw_frame_index() >= 3) {
    int cpu[2] = { 1, 1 };
    cw_shell_start_seats(2, cpu);
    G.auto_started = true;
  }
#else
  (void)screen;
#endif
}

static const bk_title TITLE = { .test_env = "CRK_SEATS", .credits = &CREDITS, .how_to_play = how_to_play, .menu_update = menu_update };

static void cmd_input_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (!G.in_match || !cw_input_recording_active()) {
    cw_reply_error(r, "input_replay needs a match in play");
    return;
  }
  G.want_hash = G.hash = (uint32_t)bk_match_hash(G.g.m);
  cw_input_record_end();
  G.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  free(G.recording);
  G.recording = malloc(len ? len : 1);
  memcpy(G.recording, d, len);
  G.recording_len = len;
  /* the match as it began */
  G.seed = G.start_seed, G.howto_seen = G.start_howto_seen;
  G.finished = 0, G.d_matches--;
  new_game();
  G.d_matches++;
  if (!cw_input_replay_begin(G.recording, G.recording_len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  G.replaying = 1;
  G.replay_done = G.replay_match = G.replay_hash_match = 0;
  cw_reply_printf(r, "{\"bytes\":%zu,\"state\":%u,\"input\":%u}", len, G.want_hash, G.want_input);
}

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  sound_menu(s == CW_SHELL_SND_MOVE ? 0 : s == CW_SHELL_SND_BACK ? 2 : 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  if (to != CW_SHELL_PLAY) sound_silence();
}

static bool warmup(void *u) {
  (void)u;
  if (G.built) return true;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  G.camera = cw_node_get(root, "Main/Camera");
  view_init(cw_node_get(root, "Main"), G.camera);
  view_build();
  G.built = true;
  return true;
}

/* behind the menus: the board itself (the 3D frame shows through; the shell dims it) */
static void draw_background(cw_shell_screen screen, cw_vec2 canvas, void *u) { (void)screen, (void)canvas, (void)u; }

static void init(void *u) {
  (void)u;
  G.last_winner = -1;
  cw_project_load_input();
  static const cw_shell_setting rows[] = {
    { "Play to", "length", &G.len_choice, LEN_CHOICES, 3 },
    { "Spin", "spin", &G.spin_choice, SPIN_CHOICES, 2 },
  };
  cw_shell_desc d = { .title = "CROKINOLE", .subtitle = "flick the discs, hit the twenty", .max_players = 4, .cpu_seats = true,
                      .hot_seats = true, .shared_screen = true, .start = start, .quit = quit,
                      .pause_items = { "How to play" }, .pause_item = pause_item, .settings_items = rows,
                      .settings_item_count = 2, .sound = shell_sound, .screen_changed = shell_screen, .draw_background = draw_background,
                      .accent = (cw_color){ 0.98f, 0.78f, 0.42f, 1 } };
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  cw_input_pointer_channel(true); /* taps and clicks go into the recording, its replay and the input hash */
  G.font = cw_ui_font_sans();
  cw_prompt_set_font(G.font);
  cw_quality_full_resolution(true);
  sound_init();
  cw_debug_i32("game.phase", &G.d_phase);
  cw_debug_i32("game.turn", &G.d_turn);
  cw_debug_i32("game.round", &G.d_round);
  cw_debug_i32("game.rounds", &G.d_rounds);
  cw_debug_i32("game.shots", &G.d_shots);
  cw_debug_i32("game.shots_round", &G.d_shots_round);
  cw_debug_i32("game.over", &G.d_over);
  cw_debug_i32("game.winner", &G.d_winner);
  cw_debug_i32("game.matches", &G.d_matches);
  cw_debug_i32("match.finished", &G.finished);
  cw_debug_i32("match.winner", &G.last_winner);
  cw_debug_i32("match.rounds", &G.last_rounds);
  cw_debug_field("match.score", G.final_score, CW_DBG_I32, 4);
  cw_debug_i32("game.fouls", &G.d_fouls);
  cw_debug_i32("game.twenties", &G.d_twenties);
  cw_debug_i32("game.contacts", &G.d_contacts);
  cw_debug_i32("game.checks", &G.d_checks);
  cw_debug_i32("game.cpu_shots", &G.d_cpu_shots);
  cw_debug_i32("game.pad_shots", &G.d_pad_shots);
  cw_debug_i32("game.pointer_shots", &G.d_ptr_shots);
  cw_debug_i32("game.standby_turns", &G.d_standby);
  cw_debug_i32("game.replays", &G.d_replays);
  cw_debug_i32("game.players", &G.d_players);
  cw_debug_i32("game.target", &G.d_target);
  cw_debug_i32("game.overlay", &G.d_overlay);
  cw_debug_field("game.score", G.score, CW_DBG_I32, 4);
  cw_debug_field("round.points", G.round_pts, CW_DBG_I32, 4);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("board.discs", &G.d_onboard);
  cw_debug_i32("board.opposing", &G.d_opp);
  cw_debug_i32("board.open", &G.d_open);
  cw_debug_i32("board.in_hand", &G.d_in_hand);
  cw_debug_i32("aim.pos", &G.d_pos);
  cw_debug_i32("aim.aim", &G.d_aim);
  cw_debug_i32("aim.meter", &G.d_meter);
  cw_debug_i32("aim.spin", &G.d_spin);
  cw_debug_i32("game.spin_on", &G.d_spin_on);
  cw_debug_i32("game.cancels", &G.d_cancels);
  cw_debug_i32("game.slides", &G.d_slides);
  cw_debug_i32("game.spins", &G.d_spins);
  cw_debug_i32("aim.charging", &G.d_charging);
  cw_debug_i32("aim.cpu", &G.d_cpu);
  cw_debug_i32("aim.thinking", &G.d_thinking);
  cw_debug_f32("aim.disc_x", &G.d_disc_x);
  cw_debug_f32("aim.disc_y", &G.d_disc_y);
  cw_debug_f32("aim.dir_x", &G.d_dir_x);
  cw_debug_f32("aim.dir_y", &G.d_dir_y);
  cw_debug_f32("aim.disc_wx", &G.d_disc_wx);
  cw_debug_f32("aim.disc_wy", &G.d_disc_wy);
  cw_debug_i32("ghost.kind", &G.ghost_kind);
  cw_debug_i32("ghost.index", &G.ghost_index);
  cw_debug_f32("ghost.x", &G.ghost_x);
  cw_debug_f32("ghost.y", &G.ghost_y);
  cw_debug_f32("ghost.stop_x", &G.stop_x);
  cw_debug_f32("ghost.stop_y", &G.stop_y);
  cw_debug_i32("ghost.opp", &G.ghost_opp);
  cw_debug_i32("ghost.stop_mm", &G.stop_mm);
  cw_debug_field("seat.shots", G.seat_shots, CW_DBG_I32, 4);
  cw_debug_field("seat.fouls", G.seat_fouls, CW_DBG_I32, 4);
  cw_debug_i32("crk.replaying", &G.replaying);
  cw_debug_i32("crk.replay_done", &G.replay_done);
  cw_debug_i32("crk.replay_match", &G.replay_match);
  cw_debug_i32("crk.replay_hash_match", &G.replay_hash_match);
  cw_debug_command("input_replay", "input_replay: replay the match from its start out of its own input recording, then compare (crk.replay_match)", cmd_input_replay);
}

/* ── the ghost: the aim line's first meeting, and where the charge would come to rest ── */

static void ghost_update(void) {
  const game *g = &G.g;
  G.ghost_kind = 0, G.ghost_index = -1, G.ghost_opp = 0, G.stop_mm = 0;
  if (!G.in_match || g->phase != GP_AIM) return;
  const crk_state *s = game_state(g);
  float x, y, dx, dy;
  game_aim_disc(g, &x, &y, &dx, &dy);
  G.d_disc_x = x, G.d_disc_y = y, G.d_dir_x = dx, G.d_dir_y = dy;
  float cx, cy; /* where the disc is in the window (a touch test's target) */
  if (G.built && view_to_canvas(x, y, DISC_H, &cx, &cy)) {
    cw_vec2 w = cw_r2d_canvas_to_window(cw_v2(cx, cy));
    G.d_disc_wx = w.x, G.d_disc_wy = w.y;
  }
  float best = 2.0f;
  int kind = 0, index = -1;
  int side = crk_side_of(s, g->aim_seat);
  for (int i = 0; i < CRK_NDISC; i++) {
    const crk_disc *d = &s->d[i];
    if (d->st != CRK_BOARD) continue;
    float px = d->x - x, py = d->y - y, b = px * dx + py * dy, c = px * px + py * py - 4 * CRK_R_DISC * CRK_R_DISC;
    float disc = b * b - c;
    if (b <= 0 || disc < 0) continue;
    float t = b - sqrtf(disc);
    if (t > 0 && t < best) best = t, kind = d->side == side ? 2 : 1, index = i;
  }
  for (int p = 0; p < CRK_NPEG; p++) {
    float qx, qy;
    crk_peg(p, &qx, &qy);
    float rs = CRK_R_DISC + CRK_R_PEG, px = qx - x, py = qy - y, b = px * dx + py * dy, c = px * px + py * py - rs * rs;
    float disc = b * b - c;
    if (b <= 0 || disc < 0) continue;
    float t = b - sqrtf(disc);
    if (t > 0 && t < best) best = t, kind = 3, index = p;
  }
  if (kind == 0) { /* to the far edge of the surface */
    float b = x * dx + y * dy, c = x * x + y * y - CRK_R_SURF * CRK_R_SURF;
    best = -b + sqrtf(b * b - c);
  }
  G.ghost_kind = kind, G.ghost_index = index, G.ghost_t = best;
  G.ghost_x = x + dx * best, G.ghost_y = y + dy * best;
  float power = g->charging || g->ptr_dragging ? crk_pow_f(g->meter) : 0;
  G.stop_t = power > 0 ? crk_slide_dist(power) : 0;
  G.stop_x = x + dx * G.stop_t, G.stop_y = y + dy * G.stop_t;
  G.ghost_opp = kind == 1, G.stop_mm = (int32_t)(G.stop_t * 1000);
}

/* ── the tick ────────────────────────────────────────────────────────── */

static void rumble_events(const game_events *ev) {
  int seat = G.g.last_seat;
  if (seat < 0 || seat >= 4 || G.g.seat_kind[seat] == SEAT_CPU) return;
  int p = cw_input_player_pad(seat) >= 0 ? seat : 0;
  float hit = 0;
  for (int i = 0; i < ev->n; i++)
    if (ev->e[i].kind == CRK_EV_DISC || ev->e[i].kind == CRK_EV_PEG) hit = fmaxf(hit, ev->e[i].speed);
  if (ev->flick) cw_input_rumble(p, 0.1f, 0.25f + 0.3f * ev->flick_power, 70);
  else if (hit > 0.15f) cw_input_rumble(p, 0.15f * fminf(1, hit), 0.4f * fminf(1, hit), 50);
  if (ev->twenty) cw_input_rumble(p, 0.5f, 0.3f, 220);
}

static void publish(void) {
  const game *g = &G.g;
  if (!g->m) return;
  const crk_state *s = game_state(g);
  G.d_phase = g->phase, G.d_turn = g->aim_seat, G.d_round = s->round, G.d_rounds = s->rounds_done, G.d_shots = g->shots;
  G.d_shots_round = s->shots, G.d_over = g->phase == GP_OVER, G.d_winner = s->winner, G.d_fouls = g->fouls, G.d_twenties = g->twenties;
  G.d_contacts = g->contacts, G.d_checks = g->checks, G.d_cpu_shots = g->cpu_shots, G.d_pad_shots = g->pad_shots;
  G.d_ptr_shots = g->pointer_shots, G.d_standby = g->standby_turns, G.d_replays = g->replays, G.d_players = g->players;
  G.d_target = s->target, G.d_overlay = G.overlay;
  G.d_spin = g->aim.spin, G.d_spin_on = g->spin_on, G.d_cancels = g->cancels, G.d_slides = g->slides, G.d_spins = g->spins;
  for (int k = 0; k < 4; k++) G.score[k] = s->score[k], G.seat_shots[k] = g->seat_shots[k], G.seat_fouls[k] = g->seat_fouls[k];
  int16_t pts[4];
  crk_count(game_view_state(g), pts);
  for (int k = 0; k < 4; k++) G.round_pts[k] = pts[k];
  G.hash = (uint32_t)bk_match_hash(g->m);
  int on = 0, opp = 0, hand = 0, side = crk_side_of(s, g->aim_seat);
  for (int i = 0; i < CRK_NDISC; i++) {
    if (s->d[i].st == CRK_BOARD) on++, opp += s->d[i].side != side;
    if (s->d[i].st == CRK_HAND) hand++;
  }
  G.d_onboard = on, G.d_opp = opp, G.d_open = opp == 0, G.d_in_hand = hand;
  G.d_pos = g->aim.pos, G.d_aim = g->aim.aim, G.d_meter = g->meter, G.d_charging = g->charging || g->ptr_dragging;
  G.d_cpu = game_seat_is_cpu(g, g->aim_seat) || g->standby_on, G.d_thinking = game_cpu_thinking(g);
}

static bool any_person_pressed(cw_action a) {
  for (int s = 0; s < G.g.players; s++)
    if (G.g.seat_kind[s] != SEAT_CPU && cw_action_just_pressed(s, a)) return true;
  return false;
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  G.ticks++;
  if (!cw_shell_update()) { publish(); return; }
  if (!G.in_match) return;
  if (G.fresh) { G.fresh = false; publish(); return; } /* the start tick is not recorded: play begins at the next */
  if (G.replaying && !cw_input_replay_active()) {
    G.replay_match = (uint32_t)bk_match_hash(G.g.m) == G.want_hash;
    G.replay_hash_match = cw_input_replay_hash() == G.want_input;
    G.replaying = 0, G.replay_done = 1;
    cw_info("crokinole input replay: state %s, input hash %s", G.replay_match ? "same" : "DIFFERENT", G.replay_hash_match ? "same" : "DIFFERENT");
  }
  game *g = &G.g;
  cw_shell_set_turn(g->seat_kind[g->aim_seat] == SEAT_HOT ? g->aim_seat : -1);
  /* this tick's pointer: the engine's recorded channel (live, or the recording's during a replay) */
  cw_pointer pt = cw_input_pointer();
  if (G.overlay != OV_NONE) { /* the how-to card: any person's flick or back, or a tap, closes it */
    G.overlay_t++;
    bool close = G.overlay_t > 20 && (any_person_pressed(g->act[CT_FLICK]) || any_person_pressed(g->act[CT_CANCEL]) || pt.pressed);
    if (g->idle_limit > 0 && G.overlay_t >= g->idle_limit) close = true; /* nobody there to read it: play on */
    if (close) {
      if (G.overlay == OV_HOWTO) G.howto_seen = true;
      G.overlay = OV_NONE;
      sound_menu(1);
      memset(g->held, 0, sizeof g->held);
    }
    publish();
    return;
  }
  /* the pointer on the board */
  bool down = pt.down, pressed = pt.pressed, valid = false;
  float bx = 0, by = 0;
  if (pt.present && G.built) {
    cw_vec2 c = cw_r2d_window_to_canvas(pt.pos);
    valid = view_from_canvas(c.x, c.y, &bx, &by);
  }
  game_input(g, down, pressed, bx, by, valid);
  game_tick(g);
  sound_events(&g->ev, view_cam_angle());
  rumble_events(&g->ev);
  if (g->ev.game_over) { /* a match finished: what the gates read (a CPU-only match starts the next one later) */
    const crk_state *s = game_state(g);
    G.finished++, G.last_winner = s->winner, G.last_rounds = s->rounds_done;
    for (int k = 0; k < 4; k++) G.final_score[k] = s->score[k];
  }
  if (g->phase == GP_OVER && g->phase_t > 90) {
    bool all_cpu = true;
    for (int s = 0; s < g->players; s++)
      if (g->seat_kind[s] != SEAT_CPU) all_cpu = false;
    if ((all_cpu && g->phase_t > 480) || (!all_cpu && g->pt.tap) || any_person_pressed(g->act[CT_FLICK])) {
      G.seed = G.seed * 6364136223846793005ull + 1442695040888963407ull;
      begin_match();
    }
  }
  ghost_update();
  publish();
}

/* ── the HUD ─────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_color side_ui(int side, float a) {
  static const float C[3][3] = { { 0.96f, 0.80f, 0.56f }, { 0.80f, 0.77f, 0.75f }, { 0.92f, 0.38f, 0.30f } };
  return rgba(C[side][0], C[side][1], C[side][2], a);
}
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .own_backdrop = true }; }
static cw_text_metrics measure(const char *s, float size) { cw_text_style st = tstyle(size, rgba(1, 1, 1, 1)); return cw_font_measure(G.font, s, &st); }
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) { cw_text_metrics m = cw_font_measure(G.font, s, &st); x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width; }
  return cw_r2d_text(G.font, s, cw_v2(x, y), &st);
}
static void panel(float x, float y, float w, float h, float u, float a) {
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 14 * u, 8, rgba(0.06f, 0.04f, 0.03f, 0.92f * a)); /* nearly opaque: text never sits on the board's lines and discs */
  cw_r2d_round_rect_lines(cw_rect_make(x, y, w, h), 14 * u, 2 * u, 8, rgba(0.85f, 0.65f, 0.4f, 0.35f * a));
}

static const char *seat_label(int seat, char *buf, size_t n) {
  const game *g = &G.g;
  if (g->seat_kind[seat] == SEAT_CPU) snprintf(buf, n, "CPU %s", cw_shell_cpu_level_label((cw_shell_cpu_level)g->seat_level[seat]));
  else snprintf(buf, n, "Player %d", seat + 1);
  return buf;
}

static void draw_disc_chip(float x, float y, float r, int side) {
  cw_r2d_circle(cw_v2(x, y), r * 1.1f, 20, rgba(0, 0, 0, 0.5f));
  cw_r2d_circle(cw_v2(x, y), r, 20, side_ui(side, 1));
  cw_r2d_circle_lines(cw_v2(x, y), r * 0.62f, r * 0.12f, 20, rgba(0, 0, 0, 0.25f));
}

static void hud_scores(cw_vec2 cs, float u) {
  const game *g = &G.g;
  const crk_state *s = game_state(g);
  const crk_state *vs = game_view_state(g);
  int16_t pts[4];
  crk_count(vs, pts);
  float x = 24 * u, y = 24 * u, w = 400 * u, row = 100 * u;
  panel(x, y, w, 56 * u + row * s->sides, u, 1);
  char b[96];
  bool card = g->phase == GP_ROUND || g->phase == GP_OVER; /* the round on the card, not the one dealt behind it */
  int round = card ? s->rounds_done : s->round;
  snprintf(b, sizeof b, "Round %d   to %d", round, s->target);
  if (s->target <= 1) snprintf(b, sizeof b, "Round %d   one round", round);
  text(b, x + 18 * u, y + 12 * u, 26 * u, rgba(1, 0.92f, 0.8f, 0.85f), CW_ALIGN_LEFT);
  for (int k = 0; k < s->sides; k++) {
    float ry = y + 52 * u + row * (float)k;
    bool turn = crk_side_of(s, g->aim_seat) == k && g->phase == GP_AIM;
    if (turn) cw_r2d_round_rect(cw_rect_make(x + 6 * u, ry - 4 * u, w - 12 * u, row - 4 * u), 10 * u, 6, rgba(1, 0.85f, 0.6f, 0.10f));
    draw_disc_chip(x + 34 * u, ry + 26 * u, 16 * u, k);
    /* the seats of the side */
    char names[64] = "", one[24];
    for (int seat = 0; seat < g->players; seat++)
      if (crk_side_of(s, seat) == k) {
        if (names[0]) strncat(names, " + ", sizeof names - strlen(names) - 1);
        strncat(names, seat_label(seat, one, sizeof one), sizeof names - strlen(names) - 1);
      }
    snprintf(b, sizeof b, "%s", SIDE_NAME[k]);
    text(b, x + 60 * u, ry + 2 * u, 28 * u, side_ui(k, 1), CW_ALIGN_LEFT);
    text(names, x + 60 * u, ry + 34 * u, 22 * u, rgba(1, 1, 1, 0.72f), CW_ALIGN_LEFT);
    if (!card) cw_r2d_round_rect(cw_rect_make(x + w - 122 * u, ry + 50 * u, 108 * u, 30 * u), 6 * u, 4, rgba(0.06f, 0.04f, 0.03f, 0.9f));
    snprintf(b, sizeof b, "%d", s->score[k]);
    text(b, x + w - 18 * u, ry, 44 * u, rgba(1, 1, 1, 0.95f), CW_ALIGN_RIGHT);
    snprintf(b, sizeof b, "+%d now", pts[k]);
    if (!card) text(b, x + w - 20 * u, ry + 52 * u, 22 * u, rgba(1, 0.9f, 0.6f, pts[k] ? 0.9f : 0.45f), CW_ALIGN_RIGHT);
    /* discs left in hand, in words */
    int left = 0;
    for (int i = 0; i < CRK_NDISC; i++)
      if (vs->d[i].side == k && vs->d[i].st == CRK_HAND) left++;
    snprintf(b, sizeof b, "%d disc%s to shoot", left, left == 1 ? "" : "s");
    text(b, x + 60 * u, ry + 62 * u, 22 * u, side_ui(k, 0.85f), CW_ALIGN_LEFT);
  }
}

static void ring2d(float r, float h, float width, cw_color c) {
  cw_vec2 pts[97];
  int n = 0;
  for (int i = 0; i <= 96; i++) {
    float a = (float)i / 96 * 6.2831853f, x, y;
    if (view_to_canvas(cosf(a) * r, sinf(a) * r, h, &x, &y)) pts[n++] = cw_v2(x, y);
  }
  if (n > 1) cw_r2d_polyline(pts, n, width, false, c);
}

static void disc_outline(float bx, float by, float width, cw_color c) {
  cw_vec2 pts[33];
  int n = 0;
  for (int i = 0; i <= 32; i++) {
    float a = (float)i / 32 * 6.2831853f, x, y;
    if (view_to_canvas(bx + cosf(a) * CRK_R_DISC * 1.12f, by + sinf(a) * CRK_R_DISC * 1.12f, DISC_H, &x, &y)) pts[n++] = cw_v2(x, y);
  }
  if (n > 1) cw_r2d_polyline(pts, n, width, false, c);
}

/* what each region scores, lettered on the board along one quiet radius (between two quadrant lines) */
static void hud_rings(float u) {
  static const float R[4] = { (CRK_R_5 + CRK_R_10) * 0.5f, (CRK_R_10 + CRK_R_15) * 0.5f, (CRK_R_15 + CRK_R_HOLE) * 0.5f, 0 };
  static const char *const V[4] = { "5", "10", "15", "20" };
  const float a = 1.5707963f;
  for (int i = 0; i < 4; i++) {
    float x, y;
    if (!view_to_canvas(cosf(a) * R[i], sinf(a) * R[i], 0.0005f, &x, &y)) continue;
    cw_color c = i == 3 ? rgba(1, 0.95f, 0.85f, 0.9f) : rgba(0.3f, 0.18f, 0.08f, 0.72f);
    text(V[i], x, y - 14 * u, 28 * u, c, CW_ALIGN_CENTER);
  }
}

static void hud_aim(cw_vec2 cs, float u) {
  const game *g = &G.g;
  if (g->phase != GP_AIM) return;
  const crk_state *s = game_state(g);
  int side = crk_side_of(s, g->aim_seat);
  bool open = !crk_opponent_on_board(s, g->aim_seat);
  /* what this shot must do */
  if (open) ring2d(CRK_R_15, 0.0005f, 3 * u, rgba(1, 0.85f, 0.4f, 0.35f + 0.1f * sinf((float)G.ticks * 0.05f)));
  else
    for (int i = 0; i < CRK_NDISC; i++)
      if (s->d[i].st == CRK_BOARD && s->d[i].side != side) disc_outline(s->d[i].x, s->d[i].y, 2 * u, rgba(1, 0.85f, 0.4f, 0.45f));
  /* the line to the first meeting */
  float x = G.d_disc_x, y = G.d_disc_y;
  float ax, ay, bx2, by2;
  bool cpu = game_seat_is_cpu(g, g->aim_seat) || g->standby_on;
  float alpha = cpu ? 0.45f : 0.85f;
  if (view_to_canvas(x, y, DISC_H, &ax, &ay) && view_to_canvas(G.ghost_x, G.ghost_y, DISC_H, &bx2, &by2)) {
    float len = hypotf(bx2 - ax, by2 - ay), dash = 12 * u;
    int nd = (int)(len / dash);
    for (int i = 0; i < nd; i += 2) {
      float t0 = (float)i / (float)nd, t1 = (float)(i + 1) / (float)nd;
      cw_r2d_line(cw_v2(ax + (bx2 - ax) * t0, ay + (by2 - ay) * t0), cw_v2(ax + (bx2 - ax) * t1, ay + (by2 - ay) * t1), 3 * u, rgba(1, 1, 1, alpha * 0.8f));
    }
    if (G.ghost_kind == 1 || G.ghost_kind == 2) {
      float gx = x + G.d_dir_x * G.ghost_t, gy = y + G.d_dir_y * G.ghost_t;
      disc_outline(gx, gy, 2.5f * u, rgba(1, 1, 1, alpha * 0.7f));
      const crk_disc *d = &s->d[G.ghost_index];
      disc_outline(d->x, d->y, 4 * u, G.ghost_kind == 1 ? rgba(0.45f, 1, 0.55f, alpha) : rgba(1, 0.6f, 0.3f, alpha));
    }
  }
  /* while charging: where it would rest if it met nothing */
  if ((g->charging || g->ptr_dragging) && G.stop_t > 0 && G.stop_t < G.ghost_t + CRK_R_DISC) disc_outline(G.stop_x, G.stop_y, 3 * u, rgba(1, 0.9f, 0.5f, 0.9f));
  /* the power meter */
  if (g->charging || g->ptr_dragging) {
    float w = 420 * u, h = 22 * u, mx = cs.x - w - 70 * u, my = cs.y - 190 * u; /* beside the board, clear of the disc on the line */
    panel(mx - 12 * u, my - 40 * u, w + 24 * u, h + 54 * u, u, 1);
    text("Power", mx, my - 34 * u, 22 * u, rgba(1, 0.9f, 0.75f, 0.9f), CW_ALIGN_LEFT);
    char b[32];
    snprintf(b, sizeof b, "%d%%", g->meter * 100 / CRK_POW_N);
    text(b, mx + w, my - 34 * u, 22 * u, rgba(1, 1, 1, 0.95f), CW_ALIGN_RIGHT);
    cw_r2d_round_rect(cw_rect_make(mx, my, w, h), 6 * u, 4, rgba(0, 0, 0, 0.6f));
    float f = (float)g->meter / CRK_POW_N;
    cw_r2d_round_rect(cw_rect_make(mx, my, w * f, h), 6 * u, 4, rgba(0.95f, 0.55f + 0.3f * (1 - f), 0.25f, 0.95f));
    for (int k = 1; k < 4; k++) cw_r2d_rect(cw_rect_make(mx + w * (float)k / 4 - u, my, 2 * u, h), rgba(1, 1, 1, 0.3f));
  }
  /* whose turn and what to do */
  char b[96], one[24];
  snprintf(b, sizeof b, "%s to shoot: %s", SIDE_NAME[side], seat_label(g->aim_seat, one, sizeof one));
  const char *need = open ? "Open board: come to rest inside the 15 ring" : "Hit an opposing disc, or lose this one";
  {
    float bw = fmaxf(measure(b, 34 * u).width, measure(need, 27 * u).width) + 64 * u;
    panel(cs.x * 0.5f - bw * 0.5f, 14 * u, bw, (g->spin_on && g->aim.spin ? 128 : 100) * u, u, 1);
  }
  text(b, cs.x * 0.5f, 22 * u, 34 * u, side_ui(side, 1), CW_ALIGN_CENTER);
  text(need, cs.x * 0.5f, 70 * u, 27 * u, rgba(1, 0.92f, 0.75f, 0.85f), CW_ALIGN_CENTER);
  if (g->spin_on && g->aim.spin != 0) {
    snprintf(b, sizeof b, "Spin: %d %s", abs(g->aim.spin), g->aim.spin > 0 ? "to the left" : "to the right");
    text(b, cs.x * 0.5f, 104 * u, 22 * u, rgba(0.8f, 0.9f, 1, 0.85f), CW_ALIGN_CENTER);
  }
  if (cpu) {
    text(g->standby_on ? "Away? The computer plays this turn. Press anything to take it back." : game_cpu_thinking(g) ? "Thinking..." : "Lining up...",
         cs.x * 0.5f, cs.y - 64 * u, 24 * u, rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
    return;
  }
  float py = cs.y - 82 * u;
  { /* the prompts sit on a dark band, not on the light wood */
    float bw = 1100 * u, bh = (g->charging ? 52 : 92) * u;
    panel(cs.x * 0.5f - bw * 0.5f, py - 10 * u, bw, bh, u, 1);
  }
  if (g->charging) cw_prompt_text(G.font, "Let go of {flick} to shoot   {cancel}: put it down", g->aim_seat, cw_v2(cs.x * 0.5f, py), 26 * u, rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER);
  else {
    cw_prompt_text(G.font, "{slide_left|slide_right}: slide   {aim_left|aim_right}: aim   Hold {flick}: power", g->aim_seat, cw_v2(cs.x * 0.5f, py), 26 * u,
                   rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER);
    char extra[96] = "";
    if (g->spin_on) strcat(extra, "{spin_up|spin_down}: spin   ");
    if (g->have_last) strcat(extra, "{replay}: replay the last shot   ");
    strcat(extra, "Or drag back from the disc");
    cw_prompt_text(G.font, extra, g->aim_seat, cw_v2(cs.x * 0.5f, py + 38 * u), 23 * u, rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
  }
}

static void hud_result(cw_vec2 cs, float u) {
  const game *g = &G.g;
  if (g->phase == GP_FLIGHT && g->replaying) {
    text("Replay", cs.x * 0.5f, 26 * u, 36 * u, rgba(1, 0.9f, 0.6f, 0.9f), CW_ALIGN_CENTER);
    return;
  }
  if (g->phase != GP_RESULT) return;
  const crk_state *s = &g->anim;
  float a = fminf(1, (float)g->phase_t / 12.0f);
  const char *msg = NULL;
  char b[96];
  if (s->last_foul == 1) msg = "Foul: no opposing disc was hit. The disc comes off.";
  else if (s->last_foul == 2) msg = "Short of the 15 ring on an open board. The disc comes off.";
  else if (s->d[s->shot_disc].st == CRK_TWENTY) msg = "Twenty!";
  else {
    int16_t now[4], was[4];
    crk_count(s, now), crk_count(&g->before, was);
    int sd = s->d[s->shot_disc].side, mine = now[sd] - was[sd], theirs = 0;
    for (int k = 0; k < s->sides; k++)
      if (k != sd) theirs += now[k] - was[k];
    if (theirs < 0 && mine >= 0) snprintf(b, sizeof b, "Hit! %d off the other side", -theirs), msg = b;
    else if (mine > 0) snprintf(b, sizeof b, "+%d on the board", mine), msg = b;
    else msg = "No change";
  }
  cw_text_metrics m = measure(msg, 38 * u);
  panel(cs.x * 0.5f - m.width * 0.5f - 28 * u, cs.y * 0.18f - 12 * u, m.width + 56 * u, m.height + 24 * u, u, a);
  text(msg, cs.x * 0.5f, cs.y * 0.18f, 38 * u, rgba(1, 0.95f, 0.8f, a), CW_ALIGN_CENTER);
}

static void hud_round(cw_vec2 cs, float u) {
  const game *g = &G.g;
  if (g->phase != GP_ROUND && g->phase != GP_OVER) return;
  const crk_state *s = game_state(g);
  const crk_round_result *R = &g->round;
  float a = fminf(1, (float)g->phase_t / 20.0f);
  float w = 860 * u, h = (200 + 70 * (float)s->sides) * u, x = cs.x * 0.5f - w * 0.5f, y = cs.y * 0.5f - h * 0.5f;
  panel(x, y, w, h, u, a);
  char b[128];
  if (g->phase == GP_OVER) snprintf(b, sizeof b, "%s wins the match", SIDE_NAME[s->winner >= 0 ? s->winner : 0]);
  else snprintf(b, sizeof b, "Round %d counted", s->rounds_done);
  text(b, cs.x * 0.5f, y + 22 * u, 40 * u, g->phase == GP_OVER ? side_ui(s->winner >= 0 ? s->winner : 0, a) : rgba(1, 0.92f, 0.78f, a), CW_ALIGN_CENTER);
  for (int k = 0; k < s->sides; k++) {
    float ry = y + 90 * u + 70 * u * (float)k;
    draw_disc_chip(x + 50 * u, ry + 20 * u, 16 * u, k);
    snprintf(b, sizeof b, "%s", SIDE_NAME[k]);
    text(b, x + 80 * u, ry + 4 * u, 30 * u, side_ui(k, a), CW_ALIGN_LEFT);
    snprintf(b, sizeof b, "counted %d (%d %s)", R->pts[k], R->twenties[k], R->twenties[k] == 1 ? "twenty" : "twenties");
    text(b, x + 250 * u, ry + 8 * u, 26 * u, rgba(1, 1, 1, 0.8f * a), CW_ALIGN_LEFT);
    snprintf(b, sizeof b, "%d", s->score[k]);
    text(b, x + w - 30 * u, ry, 40 * u, rgba(1, 1, 1, a), CW_ALIGN_RIGHT);
  }
  if (R->winner >= 0) snprintf(b, sizeof b, "%s scores the difference: %d", SIDE_NAME[R->winner], R->scored);
  else snprintf(b, sizeof b, "Even round: nobody scores");
  text(b, cs.x * 0.5f, y + h - 100 * u, 26 * u, rgba(1, 0.9f, 0.6f, a), CW_ALIGN_CENTER);
  bool people = false;
  for (int st = 0; st < g->players; st++)
    if (g->seat_kind[st] != SEAT_CPU) people = true;
  if (people && g->phase_t > 45)
    cw_prompt_text(G.font, g->phase == GP_OVER ? "{flick}: play again" : "{flick}: next round", -1, cw_v2(cs.x * 0.5f, y + h - 54 * u), 26 * u,
                   rgba(1, 1, 1, 0.9f * a), CW_ALIGN_CENTER);
}

/* the how-to-play card: over play (dimmed behind, with its close prompt) and as the title's page (the shell draws the way
 * back) */
static void draw_howto(cw_vec2 cs, bool over_play) {
  float u = cs.y / 1080.0f;
  float w = 1180 * u, h = 720 * u, x = cs.x * 0.5f - w * 0.5f, y = cs.y * 0.5f - h * 0.5f;
  if (over_play) cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0, 0, 0, 0.45f));
  panel(x, y, w, h, u, 1);
  float ty = y + 28 * u, lx = x + 48 * u;
  {
    text("How to play", cs.x * 0.5f, ty, 44 * u, rgba(1, 0.88f, 0.6f, 1), CW_ALIGN_CENTER);
    static const char *const L[] = {
      "Flick your discs from the shooting line in your quarter of the board.",
      "If another side has a disc on the board, your shot must hit one of them.",
      "  Miss them all and your disc comes off (with any of your own it touched).",
      "If the board is open, your disc must come to rest inside the 15 ring.",
      "A disc that drops into the centre hole scores 20 at once.",
      "Discs touching the shooting line or beyond it come off after each shot.",
      "When all discs are played: 15, 10 or 5 each by ring (a disc on a line",
      "  takes the lower ring), plus the 20s. The higher side scores the difference.",
    };
    for (int i = 0; i < 8; i++) text(L[i], lx, ty + (72 + 38 * (float)i) * u, 27 * u, rgba(1, 1, 1, 0.9f), CW_ALIGN_LEFT);
    float py = ty + 400 * u;
    cw_prompt_text(G.font, "{slide_left|slide_right}: slide along the line   {aim_left|aim_right}: aim", -1, cw_v2(cs.x * 0.5f, py), 25 * u, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
    cw_prompt_text(G.font, "Hold {flick} to charge, let go to flick   {replay}: watch the last shot again", -1, cw_v2(cs.x * 0.5f, py + 42 * u), 25 * u,
                   rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
    text("With a finger or the mouse: drag back from the disc and let go.", cs.x * 0.5f, py + 84 * u, 25 * u, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
    text("The dashed line shows the first disc your shot meets; the ring shows where it would stop.", cs.x * 0.5f, py + 126 * u, 22 * u, rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
    if (over_play) cw_prompt_text(G.font, "{flick}: start", -1, cw_v2(cs.x * 0.5f, y + h - 60 * u), 28 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
}

static void hud_overlay(cw_vec2 cs) {
  if (G.overlay == OV_HOWTO) draw_howto(cs, true);
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f;
  cw_r2d_begin(NULL);
  if (G.in_match && G.g.m) hud_rings(u);
  hud_scores(cs, u);
  hud_aim(cs, u);
  hud_result(cs, u);
  hud_round(cs, u);
  hud_overlay(cs);
  cw_r2d_end();
}


static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  const float dt = 1.0f / 60.0f;
  if (G.built) {
    if (G.in_match && G.g.m) view_sync(&G.g, dt);
    else {
      static game idle;
      static bool made;
      if (!made) {
        int kind[4] = { SEAT_CPU, SEAT_CPU, SEAT_CPU, SEAT_CPU }, lv[4] = { 1, 1, 1, 1 };
        game_new(&idle, 2, kind, lv, 100, false, 1);
        made = true;
      }
      view_sync(&idle, dt);
    }
  }
  float fastest = 0;
  if (G.in_match && G.g.phase == GP_FLIGHT && cw_shell_screen_now() == CW_SHELL_PLAY)
    for (int i = 0; i < CRK_NDISC; i++) {
      const crk_disc *d = &G.g.anim.d[i];
      if (d->st == CRK_BOARD) fastest = fmaxf(fastest, hypotf(d->vx, d->vy));
    }
  sound_slide(fastest);
  if (G.in_match && cw_shell_hud_shown() && !cw_test_env("CRK_NO_HUD")) hud(); /* CRK_NO_HUD: the art script's clean frames */
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Crokinole", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup,
                             .fixed_update = fixed_update, .render = render, .touch_pointer = true };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
