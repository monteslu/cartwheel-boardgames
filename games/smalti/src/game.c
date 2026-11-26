/* Smalti: the match. See game.h. */
#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "cartwheel/input.h"
#include "cartwheel/shell.h"

#define LINE_PUT_BACK (-2) /* the held group itself in UI_PLACE: choosing it puts the tiles back */

static const cw_cursor_desc CURSOR = { 0 }; /* the engine defaults: the CPUs walk the same hops */

static const char *const SEAT_NAMES[BK_MAX_SEATS] = { "Rubino", "Lapis", "Giada", "Ambra" };
const char *mg_seat_name(int seat) { return seat >= 0 && seat < BK_MAX_SEATS ? SEAT_NAMES[seat] : "?"; }

/* waits in ticks (60 a second); a self-playing cart uses a third */
enum { W_TAKE = 34, W_TILE = 30, W_FLOOR = 34, W_REFILL = 64, W_FIRST = 80 };
static int wait_of(const mg_game *g, int w) { return g->fast ? (w + 2) / 3 : w; }

static void event(mg_game *g, mg_event e) {
  e.index = g->ev_next;
  g->ev[g->ev_next % MG_EVENTS] = e;
  g->ev_next++;
}

void mg_actions_init(mg_game *g) {
  static const char *const NAMES[A_COUNT] = { "left", "right", "up", "down", "choose", "back", "how_to_play" };
  static const char *const BINDS[A_COUNT] = {
    "key:left, key:a, pad:left, axis:lx-@0.5", "key:right, key:d, pad:right, axis:lx+@0.5",
    "key:up, key:w, pad:up, axis:ly-@0.5", "key:down, key:s, pad:down, axis:ly+@0.5",
    "key:space, key:return, pad:south", "key:backspace, pad:east", "key:h, pad:north",
  };
  for (int a = 0; a < A_COUNT; a++) {
    g->act[a] = cw_action_add(NAMES[a], 0.5f);
    if (!cw_action_bind_text(g->act[a], BINDS[a])) CW_FATAL("smalti: bad bindings for %s", NAMES[a]);
  }
}

const ms_state *mg_state(const mg_game *g) { return bk_match_state(g->match); }
bool mg_busy(const mg_game *g) { return g->wait > 0 || g->staging; }
bool mg_seat_is_cpu(const mg_game *g, int seat) { return seat >= 0 && seat < g->players && g->cpu_level[seat] >= 0; }

/* ── where the groups lie (the view lays tiles out the same way) ─────── */

static ly_point group_at(const mg_game *g, const ms_state *s, int src, int colour) {
  float x = 0, z = 0;
  int n = 0;
  if (src == MS_CENTRE) {
    for (int i = 0; i < s->centre[colour] && i < LY_POOL_SLOTS; i++) {
      ly_point p = ly_pool_slot(&g->lay, colour, i, NULL);
      x += p.x, z += p.z, n++;
    }
  } else {
    int slot = 0;
    for (int c = 0; c < MS_COLOURS; c++)
      for (int k = 0; k < s->tray[src][c]; k++, slot++)
        if (c == colour) {
          ly_point p = ly_tray_slot(&g->lay, src, slot);
          x += p.x, z += p.z, n++;
        }
  }
  if (!n) return src == MS_CENTRE ? g->lay.pool : g->lay.tray[src];
  return (ly_point){ x / n, z / n };
}

static int find_group(const mg_game *g, int src, int colour) {
  if (g->mode != UI_PICK) return -1;
  for (int i = 0; i < g->ntargets; i++)
    if (g->target[i].src == src && g->target[i].colour == colour) return i;
  return -1;
}
static int find_line(const mg_game *g, int line) {
  if (g->mode != UI_PLACE) return -1;
  for (int i = 0; i < g->ntargets; i++)
    if (g->target[i].line == line) return i;
  return -1;
}

/* the targets of the seat to move: groups to take, or rows to put them on */
static void build_targets(mg_game *g) {
  const ms_state *s = mg_state(g);
  g->ntargets = 0;
  int keep = -1, old = bk_select_target(g->sel, g->turn);
  mg_target was = old >= 0 && old < MG_MAX_TARGETS ? g->target[old] : (mg_target){ -1, -1, -1, { 0, 0 } };
  if (s->phase == MS_PHASE_DRAFT) {
    if (g->mode == UI_PICK) {
      for (int src = 0; src <= MS_CENTRE; src++) {
        if (src >= s->trays && src != MS_CENTRE) continue;
        for (int c = 0; c < MS_COLOURS; c++)
          if (ms_source_count(s, src, c)) {
            if (src == was.src && c == was.colour) keep = g->ntargets;
            g->target[g->ntargets++] = (mg_target){ src, c, -1, group_at(g, s, src, c) };
          }
      }
    } else {
      bk_move mv[BK_MAX_MOVES];
      int n = ms_moves(s, mv, BK_MAX_MOVES);
      int best = -1;
      const ms_player *pl = &s->p[g->turn];
      int have = ms_source_count(s, g->pick_src, g->pick_colour);
      for (int i = 0; i < n; i++) {
        if (ms_move_source(mv[i]) != g->pick_src || ms_move_colour(mv[i]) != g->pick_colour) continue;
        int line = ms_move_line(mv[i]);
        ly_point at = line == MS_FLOOR ? ly_floor_centre(&g->lay, g->turn) : ly_line_centre(&g->lay, g->turn, line);
        if (line != MS_FLOOR && best < 0 && line + 1 - pl->line_count[line] >= have) best = g->ntargets;
        g->target[g->ntargets++] = (mg_target){ g->pick_src, g->pick_colour, line, at };
      }
      keep = best >= 0 ? best : 0; /* the first row the whole group fits, else the top one */
      /* the held group stays a target: choosing it (a tap on it, with no back button on a touch screen) puts back */
      g->target[g->ntargets++] = (mg_target){ g->pick_src, g->pick_colour, LINE_PUT_BACK, group_at(g, s, g->pick_src, g->pick_colour) };
    }
  }
  t3_vec3 pts[MG_MAX_TARGETS];
  for (int i = 0; i < g->ntargets; i++) pts[i] = (t3_vec3){ g->target[i].at.x, 0.3f, g->target[i].at.z };
  cw_cursors_set_points(bk_select_cursors(g->sel), pts, g->ntargets);
  bk_select_enable_all(g->sel, true);
  bk_select_place(g->sel, g->turn, g->ntargets ? (keep >= 0 ? keep : 0) : -1);
  g->targets_dirty = false;
}

int mg_cursor(const mg_game *g) { return bk_select_target(g->sel, g->turn); }

bk_move mg_preview(const mg_game *g) {
  if (g->mode != UI_PLACE || g->over) return BK_MOVE_NONE;
  int t = mg_cursor(g);
  if (t < 0 || t >= g->ntargets || g->target[t].line == LINE_PUT_BACK) return BK_MOVE_NONE;
  return ms_move(g->target[t].src, g->target[t].colour, g->target[t].line);
}

/* ── entering a move by presses (the CPU and the pointer) ───────────── */

static bool press_toward(mg_game *g, int seat, int src, int colour, int line, bk_press *out) {
  memset(out, 0, sizeof *out);
  out->hold = 1;
  if (g->mode == UI_PLACE && (g->pick_src != src || g->pick_colour != colour)) {
    out->v[0] = (cw_action_value){ g->act[A_BACK], 1 };
    out->count = 1;
    return true;
  }
  int target = g->mode == UI_PICK ? find_group(g, src, colour) : find_line(g, line);
  if (target < 0) CW_FATAL("smalti: a press toward a move that is not on the table (source %d colour %d row %d)", src, colour, line);
  return bk_cpu_press_toward(bk_select_cursors(g->sel), &CURSOR, seat, target, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP], g->act[A_DOWN],
                             g->act[A_OK], out);
}

bool mg_press_to_target(mg_game *g, int seat, int target, bk_press *out) {
  memset(out, 0, sizeof *out);
  out->hold = 1;
  if (target < 0 || target >= g->ntargets) CW_FATAL("smalti: a press toward target %d of %d", target, g->ntargets);
  return bk_cpu_press_toward(bk_select_cursors(g->sel), &CURSOR, seat, target, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP], g->act[A_DOWN],
                             g->act[A_OK], out);
}

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  mg_game *g = user;
  if (mg_busy(g) || g->help || seat != g->turn) return false;
  if (g->targets_dirty) build_targets(g);
  return press_toward(g, seat, ms_move_source(move), ms_move_colour(move), ms_move_line(move), out);
}

void mg_set_view(mg_game *g, cw_view3d view) { g->view = view; }

void mg_pointer(mg_game *g, cw_tree *t) {
  bool person = !g->over && !g->help && !mg_busy(g) && !mg_seat_is_cpu(g, g->turn);
  bk_select_pointer(g->sel, person ? g->turn : -1, t);
}

/* ── the match ───────────────────────────────────────────────────────── */

void mg_free(mg_game *g) {
  for (int i = 0; i < BK_MAX_SEATS; i++) {
    bk_cpu_free(g->cpu[i]);
    g->cpu[i] = NULL;
  }
  bk_match_free(g->match);
  g->match = NULL;
  bk_select_free(g->sel);
  g->sel = NULL;
}

void mg_start(mg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast) {
  mg_free(g);
  cw_action act[A_COUNT];
  memcpy(act, g->act, sizeof act);
  cw_view3d view = g->view;
  memset(g, 0, sizeof *g);
  memcpy(g->act, act, sizeof act);
  g->view = view;
  if (players < 2 || players > BK_MAX_SEATS) CW_FATAL("smalti: a match of %d seats (2..4)", players);
  g->players = players;
  g->seed = seed;
  g->fast = fast;
  for (int i = 0; i < BK_MAX_SEATS; i++) g->cpu_level[i] = i < players ? cpu_level[i] : -1;
  ms_game(&g->rules, players);
  ms_state init;
  ms_init(&init, players, seed);
  g->match = bk_match_new(&g->rules, &init, bk_seed_mix(seed, 0x5a17));
  ly_table_make(&g->lay, players, init.trays);
  g->sel = bk_select_new(&(bk_select_desc){ .cursor = CURSOR, .left = g->act[A_LEFT], .right = g->act[A_RIGHT], .up = g->act[A_UP],
                                            .down = g->act[A_DOWN], .confirm = g->act[A_OK], .back = g->act[A_BACK], .pick_radius = 70 });
  for (int p = 0; p < BK_MAX_SEATS; p++) cw_cursors_set_view(bk_select_cursors(g->sel), p, &g->view);
  for (int i = 0; i < players; i++)
    if (g->cpu_level[i] >= 0) {
      cw_search_options o;
      ms_cpu_options(players, g->cpu_level[i], seed ^ (uint64_t)(i + 1) * 0x9e37u, &o);
      g->cpu[i] = bk_cpu_new(&g->rules, &(bk_cpu_desc){ .level = g->cpu_level[i], .options = &o, .units_per_tick = 6000,
                                                         .think_ticks = fast ? 2 : 0, .press_gap = fast ? 2 : 0,
                                                         .seed = seed + (uint64_t)i, .next_press = cpu_next_press, .user = g });
    }
  /* the first draw fills the trays: the view brings the tiles in from the bag */
  g->log_seen = bk_match_log_next(g->match);
  g->turn = bk_match_turn(g->match);
  g->mode = UI_PICK;
  g->targets_dirty = true;
  g->wait = wait_of(g, W_FIRST);
  event(g, (mg_event){ .kind = EV_REFILL });
  g->shown = *mg_state(g);
  event(g, (mg_event){ .kind = EV_TURN, .seat = g->turn });
}

/* a decision landed: show it step by step */
static void landed(mg_game *g, bk_move m, int seat) {
  ms_state *d = &g->shown;
  int n = ms_source_count(d, ms_move_source(m), ms_move_colour(m));
  ms_take(d, m);
  g->moves_by[seat]++;
  if (mg_seat_is_cpu(g, seat)) g->cpu_moves++;
  else g->person_kinds[mg_kind(ms_move_source(m), ms_move_line(m))]++;
  event(g, (mg_event){ .kind = EV_TAKE, .seat = seat, .colour = ms_move_colour(m), .count = n, .line = ms_move_line(m),
                       .row = ms_move_source(m) });
  g->wait = wait_of(g, W_TAKE);
  if (ms_draft_empty(d)) {
    g->staging = true;
    g->stage_seat = 0;
    g->stage_row = 0;
  } else if (ms_hash(d) != ms_hash(mg_state(g))) {
    CW_FATAL("smalti: the table shown after a take is not the match's state");
  }
  g->mode = UI_PICK;
  g->targets_dirty = true;
}

/* the next round-end step */
static void stage_step(mg_game *g) {
  ms_state *d = &g->shown;
  while (g->stage_seat < g->players) {
    int s = g->stage_seat;
    if (g->stage_row < MS_SIZE) {
      int r = g->stage_row++;
      if (d->p[s].line_count[r] == r + 1) {
        int c = d->p[s].line_colour[r];
        int pts = ms_tile_row(d, s, r);
        event(g, (mg_event){ .kind = EV_TILE, .seat = s, .row = r, .col = ms_wall_col(r, c), .colour = c, .points = pts });
        g->wait = wait_of(g, W_TILE);
        return;
      }
      continue;
    }
    g->stage_row = 0;
    g->stage_seat++;
    if (d->p[s].floor_tiles || d->p[s].floor_marker) {
      int pts = ms_clear_floor(d, s);
      event(g, (mg_event){ .kind = EV_FLOOR, .seat = s, .points = pts });
      g->wait = wait_of(g, W_FLOOR);
      return;
    }
  }
  /* every seat done: the round finishes; the match already drew the next trays (or ended) */
  ms_round_finish(d);
  const ms_state *s = mg_state(g);
  g->staging = false;
  if (s->phase == MS_PHASE_OVER) {
    if (ms_hash(d) != ms_hash(s)) CW_FATAL("smalti: the final table shown is not the match's state");
    g->over = true;
    event(g, (mg_event){ .kind = EV_OVER });
    return;
  }
  for (int i = 0; i < g->players; i++)
    if (d->p[i].score != s->p[i].score || d->p[i].wall != s->p[i].wall)
      CW_FATAL("smalti: the round end shown differs from the match's (seat %d)", i);
  *d = *s;
  event(g, (mg_event){ .kind = EV_REFILL });
  g->wait = wait_of(g, W_REFILL);
  g->targets_dirty = true;
}

void mg_tick(mg_game *g) {
  g->ticks++;
  /* the CPUs think and press every tick (off their turn they return nothing), a queued tap becomes this tick's pick */
  for (int i = 0; i < g->players; i++)
    if (g->cpu[i]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[i], g->match, i, v, 4);
      cw_input_set_actions(i, v, n);
    }
  /* how to play: any seat opens and closes it */
  for (int i = 0; i < g->players; i++) {
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help, g->help_opens += g->help;
    else if (g->help && !mg_seat_is_cpu(g, i) && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) {
      g->help = false;
      return; /* that press closes the card, nothing else */
    }
  }
  if (g->help) return;
  if (g->wait > 0) { g->wait--; return; }
  if (g->staging) { stage_step(g); return; }
  if (g->over) return;
  int seat = bk_match_turn(g->match);
  if (seat != g->turn) {
    g->turn = seat;
    g->mode = UI_PICK;
    g->targets_dirty = true;
    event(g, (mg_event){ .kind = EV_TURN, .seat = seat });
  }
  cw_shell_set_turn(seat); /* hot seats: the free pads drive the seat to move */
  if (g->targets_dirty) build_targets(g);
  if (!mg_seat_is_cpu(g, seat)) bk_select_feed(g->sel, seat);
  bk_sel_event e = bk_select_tick(g->sel, seat);
  if (e.pointer) g->pointer_picks++;
  int t = e.target;
  bool put_back = e.kind == BK_SEL_CONFIRM && g->mode == UI_PLACE && t >= 0 && t < g->ntargets && g->target[t].line == LINE_PUT_BACK;
  if ((e.kind == BK_SEL_BACK || put_back) && g->mode == UI_PLACE) {
    g->mode = UI_PICK;
    g->targets_dirty = true;
    return;
  }
  if (e.kind != BK_SEL_CONFIRM || t < 0 || t >= g->ntargets) return;
  if (g->mode == UI_PICK) {
    g->pick_src = g->target[t].src;
    g->pick_colour = g->target[t].colour;
    g->mode = UI_PLACE;
    build_targets(g);
    return;
  }
  bk_move m = ms_move(g->target[t].src, g->target[t].colour, g->target[t].line);
  if (!bk_match_play(g->match, m)) CW_FATAL("smalti: the cursor offered an illegal move");
  g->log_seen = bk_match_log_next(g->match);
  landed(g, m, seat);
}
