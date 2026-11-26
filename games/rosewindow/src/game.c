/* Rose Window: the match. See game.h. */
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/input.h"
#include "cartwheel/shell.h"

static const cw_cursor_desc CURSOR = { 0 };

static const char *const SEAT_NAMES[BK_MAX_SEATS] = { "Rosa", "Bram", "Wren", "Ines" };
const char *mg_seat_name(int seat) { return seat >= 0 && seat < BK_MAX_SEATS ? SEAT_NAMES[seat] : "?"; }

/* waits in ticks (60 a second); a self-playing cart uses a third */
enum { W_PLACE = 26, W_TOOL = 22, W_ROLL = 70, W_ROLL_EACH = 7, W_FRAME = 40, W_ROUND = 40 };
static int wait_of(const mg_game *g, int w) { return g->fast ? (w + 2) / 3 : w; }

void mg_actions_init(mg_game *g) {
  static const char *const NAMES[A_COUNT] = { "left", "right", "up", "down", "choose", "back", "how_to_play", "cards" };
  static const char *const BINDS[A_COUNT] = {
    "key:left, key:a, pad:left, axis:lx-@0.5", "key:right, key:d, pad:right, axis:lx+@0.5",
    "key:up, key:w, pad:up, axis:ly-@0.5", "key:down, key:s, pad:down, axis:ly+@0.5",
    "key:space, key:return, pad:south", "key:backspace, pad:east", "key:h, pad:north", "key:c, pad:west",
  };
  for (int a = 0; a < A_COUNT; a++) {
    g->act[a] = cw_action_add(NAMES[a], 0.5f);
    if (!cw_action_bind_text(g->act[a], BINDS[a])) CW_FATAL("rosewindow: bad bindings for %s", NAMES[a]);
  }
}

const rw_state *mg_state(const mg_game *g) { return bk_match_state(g->match); }
bool mg_busy(const mg_game *g) { return g->wait > 0; }
bool mg_seat_is_cpu(const mg_game *g, int seat) { return seat >= 0 && seat < g->players && g->cpu_level[seat] >= 0; }

int mg_move_steps(const rw_state *s, bk_move m, int out[MG_STEPS]) {
  int kind = rw_mv_kind(m), a = rw_mv_a(m), b = rw_mv_b(m), c = rw_mv_c(m);
  switch (kind) {
  case RW_MV_FRAME: out[0] = tg_id(TG_FRAME, a); return 1;
  case RW_MV_PLACE: out[0] = tg_id(TG_DIE, a); out[1] = tg_id(TG_PANE, b); return 2;
  case RW_MV_PASS: out[0] = tg_id(TG_PASS, 0); return 1;
  case RW_MV_TOOL:
    out[0] = tg_id(TG_TOOL, a);
    switch ((rw_tool_kind)s->tools[a]) {
    case RW_T_NUDGE: out[1] = tg_id(TG_DIE, b); out[2] = tg_id(TG_DIR, c); return 3;
    case RW_T_TURN:
    case RW_T_REFIRE:
    case RW_T_REDRAW: out[1] = tg_id(TG_DIE, b); return 2;
    case RW_T_KILN:
    case RW_T_TWIN: return 1;
    case RW_T_LONE:
    case RW_T_GLAZE: out[1] = tg_id(TG_DIE, b); out[2] = tg_id(TG_PANE, c); return 3;
    case RW_T_TINT:
    case RW_T_ETCH: out[1] = tg_id(TG_PANE, b); out[2] = tg_id(TG_PANE, c); return 3;
    }
  }
  CW_FATAL("rosewindow: no steps for move kind %d", kind);
  return 0;
}

t3_vec3 mg_target_point(const mg_game *g, int id) {
  const rw_state *s = mg_state(g);
  int k = tg_kind(id), i = tg_index(id);
  switch (k) {
  case TG_DIE: return g->lay.pool[i];
  case TG_PANE: return ly_pane(&g->lay, g->turn, i, 0.3f);
  case TG_TOOL: return g->lay.tool[i];
  case TG_PASS: return (t3_vec3){ g->lay.pass.x, 0.3f, g->lay.pass.z };
  case TG_FRAME: return g->lay.win[LY_OFFER0 + i];
  case TG_DIR: { /* lower on the left of the held die, raise on the right */
    int slot = g->plen >= 2 ? tg_index(g->path[1]) : 0;
    t3_vec3 p = g->lay.pool[slot];
    return (t3_vec3){ p.x + (i ? 0.55f : -0.55f), p.y, p.z + 0.75f };
  }
  case TG_BACK: return g->plen ? mg_target_point(g, g->path[g->plen - 1]) : g->lay.tool[g->undo_slot]; /* put back, or take back the tool */
  }
  (void)s;
  CW_FATAL("rosewindow: no point for target %x", id);
  return (t3_vec3){ 0 };
}

static bool path_matches(const mg_game *g, const int *steps, int n) {
  if (n < g->plen) return false;
  for (int i = 0; i < g->plen; i++)
    if (steps[i] != g->path[i]) return false;
  return true;
}

/* the choices offered now: every next step of a legal move that starts with the path, in a stable order */
static void build_targets(mg_game *g) {
  const rw_state *s = mg_state(g);
  int keep_id = -1, old = bk_select_target(g->sel, g->turn);
  if (old >= 0 && old < g->ntargets) keep_id = g->target[old];
  g->ntargets = 0;
  if (!bk_match_over(g->match) && rw_turn(s) >= 0) {
    bk_move mv[BK_MAX_MOVES];
    int n = bk_match_moves(g->match, mv, BK_MAX_MOVES);
    for (int i = 0; i < n; i++) {
      int st[MG_STEPS];
      int k = mg_move_steps(s, mv[i], st);
      if (!path_matches(g, st, k) || k == g->plen) continue;
      int id = st[g->plen];
      bool dup = false;
      for (int j = 0; j < g->ntargets; j++) dup |= g->target[j] == id;
      if (!dup && g->ntargets < MG_MAX_TARGETS - 1) g->target[g->ntargets++] = id;
    }
    if (g->plen || (s->tool_now && bk_match_can_undo(g->match))) g->target[g->ntargets++] = tg_id(TG_BACK, 0);
  }
  /* a stable order: by kind, then index (dice left to right, panes in reading order) */
  for (int i = 1; i < g->ntargets; i++)
    for (int j = i; j > 0 && g->target[j] < g->target[j - 1]; j--) {
      int t = g->target[j]; g->target[j] = g->target[j - 1]; g->target[j - 1] = t;
    }
  int keep = -1;
  for (int i = 0; i < g->ntargets; i++) {
    g->target_at[i] = mg_target_point(g, g->target[i]);
    if (g->target[i] == keep_id) keep = i;
  }
  if (keep < 0) /* the first die, else the first choice */
    for (int i = 0; i < g->ntargets && keep < 0; i++)
      if (tg_kind(g->target[i]) == TG_DIE || tg_kind(g->target[i]) == TG_PANE || tg_kind(g->target[i]) == TG_FRAME) keep = i;
  cw_cursors_set_points(bk_select_cursors(g->sel), g->target_at, g->ntargets);
  bk_select_enable_all(g->sel, true);
  bk_select_place(g->sel, g->turn, g->ntargets ? (keep >= 0 ? keep : 0) : -1);
  g->targets_dirty = false;
}

int mg_cursor_id(const mg_game *g) {
  int t = bk_select_target(g->sel, g->turn);
  return t >= 0 && t < g->ntargets ? g->target[t] : -1;
}

bk_move mg_preview(const mg_game *g) {
  if (g->over || mg_busy(g)) return BK_MOVE_NONE;
  int id = mg_cursor_id(g);
  if (id < 0 || tg_kind(id) == TG_BACK) return BK_MOVE_NONE;
  const rw_state *s = mg_state(g);
  bk_move mv[BK_MAX_MOVES];
  int n = bk_match_moves(g->match, mv, BK_MAX_MOVES);
  for (int i = 0; i < n; i++) {
    int st[MG_STEPS];
    int k = mg_move_steps(s, mv[i], st);
    if (k == g->plen + 1 && path_matches(g, st, k) && st[g->plen] == id) return mv[i];
  }
  return BK_MOVE_NONE;
}

/* ── entering a move by presses (the CPU) ─────────────────────────── */

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  mg_game *g = user;
  if (mg_busy(g) || g->help || g->cards || seat != g->turn) return false;
  if (g->targets_dirty) build_targets(g);
  memset(out, 0, sizeof *out);
  out->hold = 1;
  int st[MG_STEPS];
  int k = mg_move_steps(state, move, st);
  if (!path_matches(g, st, k) || k <= g->plen) {
    out->v[0] = (cw_action_value){ g->act[A_BACK], 1 };
    out->count = 1;
    return true;
  }
  int want = -1;
  for (int i = 0; i < g->ntargets; i++)
    if (g->target[i] == st[g->plen]) want = i;
  if (want < 0) CW_FATAL("rosewindow: the CPU's next choice %x is not on the table", st[g->plen]);
  return bk_cpu_press_toward(bk_select_cursors(g->sel), &CURSOR, seat, want, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP], g->act[A_DOWN],
                             g->act[A_OK], out);
}

void mg_set_view(mg_game *g, cw_view3d view) { g->view = view; }

void mg_pointer(mg_game *g, cw_tree *t) {
  bool person = !g->over && !g->help && !g->cards && !mg_busy(g) && !mg_seat_is_cpu(g, g->turn);
  bk_select_pointer(g->sel, person ? g->turn : -1, t);
}

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
  if (players < 1 || players > BK_MAX_SEATS) CW_FATAL("rosewindow: a match of %d seats (1..4)", players);
  g->players = players;
  g->seed = seed;
  g->fast = fast;
  for (int i = 0; i < BK_MAX_SEATS; i++) g->cpu_level[i] = i < players ? cpu_level[i] : -1;
  rw_game(&g->rules, players);
  rw_state init;
  rw_init(&init, players, seed);
  g->match = bk_match_new(&g->rules, &init, bk_seed_mix(seed, 0x9a55));
  ly_table_make(&g->lay, players);
  g->sel = bk_select_new(&(bk_select_desc){ .cursor = CURSOR, .left = g->act[A_LEFT], .right = g->act[A_RIGHT], .up = g->act[A_UP],
                                            .down = g->act[A_DOWN], .confirm = g->act[A_OK], .back = g->act[A_BACK], .pick_radius = 60 });
  for (int p = 0; p < BK_MAX_SEATS; p++) cw_cursors_set_view(bk_select_cursors(g->sel), p, &g->view);
  for (int i = 0; i < players; i++)
    if (g->cpu_level[i] >= 0) {
      cw_search_options o;
      rw_cpu_options(players, g->cpu_level[i], seed ^ (uint64_t)(i + 1) * 0x9e37u, &o);
      g->cpu[i] = bk_cpu_new(&g->rules, &(bk_cpu_desc){ .level = g->cpu_level[i], .options = &o, .units_per_tick = 6000,
                                                         .think_ticks = fast ? 2 : 0, .press_gap = fast ? 2 : 0,
                                                         .seed = seed + (uint64_t)i, .next_press = cpu_next_press, .user = g });
    }
  g->log_seen = bk_match_log_next(g->match);
  g->turn = bk_match_turn(g->match);
  g->targets_dirty = true;
  g->wait = wait_of(g, W_FRAME);
}

static void caption(mg_game *g, const char *fmt, const char *a, const char *b) {
  snprintf(g->caption, sizeof g->caption, fmt, a, b);
  g->caption_age = 0;
}

/* a decision landed: say what happened, and wait while its dice move */
static void landed(mg_game *g, const rw_state *before, bk_move m, int seat) {
  g->moves_by[seat]++;
  if (mg_seat_is_cpu(g, seat)) g->cpu_moves++;
  char what[64];
  rw_move_label(before, m, what, sizeof what);
  caption(g, "%s: %s", mg_seat_name(seat), what);
  int kind = rw_mv_kind(m);
  g->wait = wait_of(g, kind == RW_MV_FRAME ? W_FRAME : kind == RW_MV_TOOL ? W_TOOL : W_PLACE);
  if (kind == RW_MV_TOOL) g->tools_used++, g->undo_slot = rw_mv_a(m);
  if (!mg_seat_is_cpu(g, seat) && kind >= 0 && kind < 4) {
    g->person_kinds[kind]++;
    if (kind == RW_MV_TOOL) g->person_tools |= 1 << before->tools[rw_mv_a(m)];
  }
  /* dice rolled after it (a refire, the kiln, a fresh pool): each throw plays out before the next choice */
  bk_log_entry log[32];
  int n = bk_match_log(g->match, g->log_seen, log, 32), rolls = 0;
  for (int i = 0; i < n; i++) {
    g->log_seen = log[i].index + 1;
    if (log[i].kind == BK_LOG_CHANCE) rolls++;
  }
  if (rolls) g->wait += wait_of(g, W_ROLL + W_ROLL_EACH * rolls);
  if (mg_state(g)->round != before->round && before->round) g->wait += wait_of(g, W_ROUND);
}

void mg_tick(mg_game *g) {
  g->ticks++;
  g->caption_age++;
  for (int i = 0; i < g->players; i++)
    if (g->cpu[i]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[i], g->match, i, v, 4);
      cw_input_set_actions(i, v, n);
    }
  for (int i = 0; i < g->players; i++) {
    if (mg_seat_is_cpu(g, i)) continue;
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help, g->cards = false;
    else if (cw_action_just_pressed(i, g->act[A_CARDS])) g->cards = !g->cards, g->help = false;
    else if ((g->help || g->cards) && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) {
      g->help = g->cards = false;
      return;
    }
  }
  if (g->help || g->cards) return;
  if (g->wait > 0) { g->wait--; return; }
  if (bk_match_over(g->match)) { g->over = true; return; }
  int seat = bk_match_turn(g->match);
  if (seat != g->turn) {
    g->turn = seat;
    g->plen = 0;
    g->targets_dirty = true;
  }
  cw_shell_set_turn(seat);
  if (g->targets_dirty) build_targets(g);
  if (!mg_seat_is_cpu(g, seat)) bk_select_feed(g->sel, seat);
  bk_sel_event e = bk_select_tick(g->sel, seat);
  if (e.pointer && e.kind != BK_SEL_NONE) g->pointer_picks++;
  int t = e.target;
  bool back = e.kind == BK_SEL_BACK || (e.kind == BK_SEL_CONFIRM && t >= 0 && t < g->ntargets && tg_kind(g->target[t]) == TG_BACK);
  if (back) {
    if (g->plen > 0) {
      g->plen--;
      g->targets_dirty = true;
    } else if (bk_match_can_undo(g->match)) { /* a tool used this turn, taken back */
      bk_match_undo(g->match);
      g->log_seen = bk_match_log_next(g->match);
      g->undos++;
      caption(g, "%s took back a tool%s", mg_seat_name(seat), "");
      g->targets_dirty = true;
    }
    return;
  }
  if (e.kind != BK_SEL_CONFIRM || t < 0 || t >= g->ntargets) return;
  if (g->plen >= MG_STEPS) CW_FATAL("rosewindow: a path longer than %d", MG_STEPS);
  g->path[g->plen++] = g->target[t];
  /* a whole move? */
  const rw_state *s = mg_state(g);
  bk_move mv[BK_MAX_MOVES];
  int n = bk_match_moves(g->match, mv, BK_MAX_MOVES);
  for (int i = 0; i < n; i++) {
    int st[MG_STEPS];
    int k = mg_move_steps(s, mv[i], st);
    if (k != g->plen || !path_matches(g, st, k)) continue;
    rw_state before = *s;
    if (!bk_match_play(g->match, mv[i])) CW_FATAL("rosewindow: the table offered an illegal move");
    g->plen = 0;
    g->targets_dirty = true;
    landed(g, &before, mv[i], seat);
    return;
  }
  g->targets_dirty = true;
}
