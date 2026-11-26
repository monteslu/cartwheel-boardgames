/* Lantern Harbor: the match. See game.h. */
#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "cartwheel/input.h"
#include "cartwheel/shell.h"

static const cw_cursor_desc CURSOR = { 0 }; /* the engine defaults: the CPUs walk the same hops */

static const char *const SEAT_NAMES[BK_MAX_SEATS] = { "Ilse", "Tomas", "Mira", "Oren" };
const char *mg_seat_name(int seat) { return seat >= 0 && seat < BK_MAX_SEATS ? SEAT_NAMES[seat] : "?"; }

/* waits in ticks (60 a second); a self-playing cart uses a third */
static int wait_of(const mg_game *g, int w) { return g->fast ? (w + 4) / 5 : w; }

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
    if (!cw_action_bind_text(g->act[a], BINDS[a])) CW_FATAL("lanternharbor: bad bindings for %s", NAMES[a]);
  }
}

const hb_state *mg_state(const mg_game *g) { return bk_match_state(g->match); }
bool mg_busy(const mg_game *g) { return g->wait > 0 || g->ending; }
bool mg_seat_is_cpu(const mg_game *g, int seat) { return seat >= 0 && seat < g->players && g->cpu_level[seat] >= 0; }

/* ── where things are ──────────────────────────────────────────────── */

static int row_index(const hb_state *s, int kind) {
  for (int i = 0; i < HB_ROW; i++)
    if (s->row[i] == kind) return i;
  return 0;
}

static int free_plot(const hb_player *pl, int kind) {
  for (int i = 0; i < pl->nplots; i++)
    if (pl->plot[i] == kind && !pl->plot_staffed[i]) return i;
  return 0;
}

ly_point mg_move_spot(const mg_game *g, const hb_state *s, bk_move m, float *y) {
  int seat = s->actor, a = hb_move_a(m), b = hb_move_b(m);
  const hb_player *pl = &s->p[seat];
  float yy = 0.35f;
  ly_point p;
  switch (hb_move_type(m)) {
  case M_ROLE: p = ly_role(a); break;
  case M_TAKE: p = ly_row(row_index(s, a)); break;
  case M_STONE: p = ly_stone_pile(); break;
  case M_SEED: p = ly_plot_pile(); break;
  case M_PASS: p = ly_pass(); break;
  case M_REARRANGE: p = ly_dock(&g->lay, seat, 1); break;
  case M_PLACE_PLOT: p = ly_plot(&g->lay, seat, free_plot(pl, a)); break;
  case M_PLACE_BLD: {
    ly_point town[HB_BUILDINGS];
    ly_town(&g->lay, seat, pl, town);
    p = town[a];
    break;
  }
  case M_BUILD: p = ly_market(a); break;
  case M_EXTRA:
  case M_SELL: p = ly_goods(&g->lay, seat, a); break;
  case M_LOAD: p = ly_ship(s->nships, b); yy = 0.9f; break;
  case M_JETTY: {
    ly_point town[HB_BUILDINGS];
    ly_town(&g->lay, seat, pl, town);
    p = town[B_JETTY];
    break;
  }
  default: p = ly_pass();
  }
  if (y) *y = yy;
  return p;
}

static bool is_ship_move(bk_move m) { return hb_move_type(m) == M_LOAD || hb_move_type(m) == M_JETTY; }

static int find_move(const mg_game *g, bk_move m) {
  for (int i = 0; i < g->ntargets; i++)
    if (g->target[i].kind == TG_MOVE && g->target[i].move == m) return i;
  return -1;
}
static int find_good(const mg_game *g, int good) {
  for (int i = 0; i < g->ntargets; i++)
    if (g->target[i].kind == TG_GOOD && g->target[i].good == good) return i;
  return -1;
}

static void build_targets(mg_game *g) {
  const hb_state *s = mg_state(g);
  int old = bk_select_target(g->sel, g->turn);
  mg_target was = old >= 0 && old < g->ntargets ? g->target[old] : (mg_target){ TG_PUTBACK, BK_MOVE_NONE, -1, { 0, 0 }, 0 };
  g->ntargets = 0;
  int keep = -1;
  bk_move mv[BK_MAX_MOVES];
  int n = bk_match_over(g->match) ? 0 : bk_match_moves(g->match, mv, BK_MAX_MOVES);
  bool ship = s->phase == PH_SHIP;
  for (int i = 0; i < n && g->ntargets < MG_MAX_TARGETS - 1; i++) {
    mg_target t = { TG_MOVE, mv[i], -1, { 0, 0 }, 0 };
    if (ship && is_ship_move(mv[i])) {
      int good = hb_move_a(mv[i]);
      if (g->mode == UI_PICK) {
        if (find_good(g, good) >= 0) continue;
        t.kind = TG_GOOD;
        t.good = good;
        t.at = ly_goods(&g->lay, s->actor, good);
        t.y = 0.35f;
      } else {
        if (good != g->pick_good) continue;
        t.at = mg_move_spot(g, s, mv[i], &t.y);
      }
    } else {
      if (ship && g->mode == UI_PLACE) continue;
      t.at = mg_move_spot(g, s, mv[i], &t.y);
    }
    if (keep < 0 && ((t.kind == TG_MOVE && was.kind == TG_MOVE && t.move == was.move) || (t.kind == TG_GOOD && was.kind == TG_GOOD && t.good == was.good)))
      keep = g->ntargets;
    g->target[g->ntargets++] = t;
  }
  if (ship && g->mode == UI_PLACE) {
    if (keep < 0) keep = 0;
    /* the held good stays a target: choosing it puts the good back (no back button on a touch screen) */
    g->target[g->ntargets++] = (mg_target){ TG_PUTBACK, BK_MOVE_NONE, g->pick_good, ly_goods(&g->lay, s->actor, g->pick_good), 0.35f };
  }
  t3_vec3 pts[MG_MAX_TARGETS];
  for (int i = 0; i < g->ntargets; i++) pts[i] = (t3_vec3){ g->target[i].at.x, g->target[i].y, g->target[i].at.z };
  cw_cursors_set_points(bk_select_cursors(g->sel), pts, g->ntargets);
  bk_select_enable_all(g->sel, true);
  bk_select_place(g->sel, g->turn, g->ntargets ? (keep >= 0 ? keep : 0) : -1);
  g->targets_dirty = false;
}

int mg_cursor(const mg_game *g) { return bk_select_target(g->sel, g->turn); }
const mg_target *mg_cursor_target(const mg_game *g) {
  int t = mg_cursor(g);
  return t >= 0 && t < g->ntargets ? &g->target[t] : NULL;
}

bk_move mg_preview(const mg_game *g) {
  if (g->over || mg_busy(g)) return BK_MOVE_NONE;
  const mg_target *t = mg_cursor_target(g);
  return t && t->kind == TG_MOVE ? t->move : BK_MOVE_NONE;
}

/* ── entering a move by presses (the CPU) ──────────────────────────── */

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  mg_game *g = user;
  if (mg_busy(g) || g->help || seat != g->turn) return false;
  if (g->targets_dirty) build_targets(g);
  memset(out, 0, sizeof *out);
  out->hold = 1;
  int target;
  if (is_ship_move(move) && mg_state(g)->phase == PH_SHIP) {
    int good = hb_move_a(move);
    if (g->mode == UI_PLACE && g->pick_good != good) {
      out->v[0] = (cw_action_value){ g->act[A_BACK], 1 };
      out->count = 1;
      return true;
    }
    target = g->mode == UI_PICK ? find_good(g, good) : find_move(g, move);
  } else {
    target = find_move(g, move);
  }
  if (target < 0) CW_FATAL("lanternharbor: a CPU press toward a move that is not on the table (%llx)", (unsigned long long)move);
  return bk_cpu_press_toward(bk_select_cursors(g->sel), &CURSOR, seat, target, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP], g->act[A_DOWN],
                             g->act[A_OK], out);
}

void mg_set_view(mg_game *g, cw_view3d view) { g->view = view; }

void mg_pointer(mg_game *g, cw_tree *t) {
  bool person = !g->over && !g->help && !mg_busy(g) && !mg_seat_is_cpu(g, g->turn);
  bk_select_pointer(g->sel, person ? g->turn : -1, t);
}

/* ── the match ─────────────────────────────────────────────────────── */

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
  if (players < 2 || players > BK_MAX_SEATS) CW_FATAL("lanternharbor: a match of %d seats (2..4)", players);
  g->players = players;
  g->seed = seed;
  g->fast = fast;
  for (int i = 0; i < BK_MAX_SEATS; i++) g->cpu_level[i] = i < players ? cpu_level[i] : -1;
  hb_game(&g->rules, players);
  hb_state init;
  hb_init(&init, players, seed);
  g->match = bk_match_new(&g->rules, &init, bk_seed_mix(seed, 0x1a47));
  ly_table_make(&g->lay, players);
  g->sel = bk_select_new(&(bk_select_desc){ .cursor = CURSOR, .left = g->act[A_LEFT], .right = g->act[A_RIGHT], .up = g->act[A_UP],
                                            .down = g->act[A_DOWN], .confirm = g->act[A_OK], .back = g->act[A_BACK], .pick_radius = 46 });
  for (int p = 0; p < BK_MAX_SEATS; p++) cw_cursors_set_view(bk_select_cursors(g->sel), p, &g->view);
  for (int i = 0; i < players; i++)
    if (g->cpu_level[i] >= 0) {
      cw_search_options o;
      hb_cpu_options(players, g->cpu_level[i], seed ^ (uint64_t)(i + 1) * 0x9e37u, &o);
      g->cpu[i] = bk_cpu_new(&g->rules, &(bk_cpu_desc){ .level = g->cpu_level[i], .kind = CW_SEARCH_MCTS, .options = &o, .units_per_tick = 40000,
                                                         .think_ticks = fast ? 1 : 0, .press_gap = fast ? 1 : 0,
                                                         .seed = seed + (uint64_t)i, .next_press = cpu_next_press, .user = g });
    }
  g->turn = bk_match_turn(g->match);
  g->mode = UI_PICK;
  g->targets_dirty = true;
  g->wait = wait_of(g, 90); /* the boats sail in */
  g->shown = *mg_state(g);
  event(g, (mg_event){ .kind = EV_START });
  event(g, (mg_event){ .kind = EV_TURN, .seat = g->turn });
}

/* how long the table needs to show a decision */
static int wait_for(const hb_state *before, const hb_state *after, bk_move m) {
  int w;
  switch (hb_move_type(m)) {
  case M_ROLE: w = 40; break;
  case M_PLACE_PLOT: case M_PLACE_BLD: w = 14; break;
  case M_PASS: w = 14; break;
  case M_REARRANGE: w = 24; break;
  default: w = 32;
  }
  if (before->phase != after->phase || before->picks != after->picks) w += 24; /* a job ended: goods made, the row redrawn */
  for (int k = 0; k < after->nships; k++)
    if (before->ship_load[k] > 0 && after->ship_load[k] == 0) { w += 150; break; } /* a boat sails off and a new one comes in */
  if (before->round != after->round) w += 30;
  return w;
}

static void landed(mg_game *g, bk_move m, int seat) {
  hb_state before = g->shown;
  g->shown = *mg_state(g);
  g->moves_by[seat]++;
  if (mg_seat_is_cpu(g, seat)) g->cpu_moves++;
  event(g, (mg_event){ .kind = EV_MOVE, .seat = seat, .move = m });
  g->wait = wait_of(g, wait_for(&before, &g->shown, m));
  g->mode = UI_PICK;
  g->targets_dirty = true;
  if (bk_match_over(g->match)) g->ending = true;
}

void mg_tick(mg_game *g) {
  g->ticks++;
  for (int i = 0; i < g->players; i++)
    if (g->cpu[i]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[i], g->match, i, v, 4);
      cw_input_set_actions(i, v, n);
    }
  for (int i = 0; i < g->players; i++) {
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help;
    else if (g->help && !mg_seat_is_cpu(g, i) && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) {
      g->help = false;
      return;
    }
  }
  if (g->help) return;
  if (g->wait > 0) { g->wait--; return; }
  if (g->ending) {
    g->ending = false;
    g->over = true;
    event(g, (mg_event){ .kind = EV_OVER });
    return;
  }
  if (g->over) return;
  int seat = bk_match_turn(g->match);
  if (seat != g->turn) {
    g->turn = seat;
    g->mode = UI_PICK;
    g->targets_dirty = true;
    event(g, (mg_event){ .kind = EV_TURN, .seat = seat });
  }
  cw_shell_set_turn(seat);
  if (g->targets_dirty) build_targets(g);
  if (!mg_seat_is_cpu(g, seat)) bk_select_feed(g->sel, seat);
  bk_sel_event e = bk_select_tick(g->sel, seat);
  if (e.pointer) g->pointer_picks++;
  int t = e.target;
  bool put_back = e.kind == BK_SEL_CONFIRM && t >= 0 && t < g->ntargets && g->target[t].kind == TG_PUTBACK;
  if ((e.kind == BK_SEL_BACK || put_back) && g->mode == UI_PLACE) {
    g->mode = UI_PICK;
    build_targets(g);
    int k = find_good(g, g->pick_good);
    if (k >= 0) bk_select_place(g->sel, seat, k);
    return;
  }
  if (e.kind != BK_SEL_CONFIRM || t < 0 || t >= g->ntargets) return;
  if (g->target[t].kind == TG_GOOD) {
    g->pick_good = g->target[t].good;
    g->mode = UI_PLACE;
    build_targets(g);
    return;
  }
  bk_move m = g->target[t].move;
  if (!bk_match_play(g->match, m)) CW_FATAL("lanternharbor: the cursor offered an illegal move");
  if (!mg_seat_is_cpu(g, seat)) g->ui_types[e.pointer ? 1 : 0][hb_move_type(m) & 15]++;
  landed(g, m, seat);
}
