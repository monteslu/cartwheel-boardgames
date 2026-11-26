/* Fever Line: the match on the table. See game.h. */
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/shell.h"
#include "layout.h"

static const char *const SETUP_NAMES[3] = { "Intro", "Normal", "Heroic" };
const char *tg_setup_name(int setup) { return setup >= 0 && setup < 3 ? SETUP_NAMES[setup] : "?"; }
static const char *const COL_NAMES[FV_NCOL] = { "Ember", "Tide", "Moss", "Dusk" };

static void setup_done(tg_game *g);

/* beats in ticks (60 a second); a self-playing cart uses a third */
static int beat_ticks(const tg_game *g, int t) { return g->fast ? (t + 2) / 3 : t; }

void tg_actions_init(tg_game *g) {
  static const char *const NAMES[A_COUNT] = { "left", "right", "up", "down", "choose", "back", "how_to_play" };
  static const char *const BINDS[A_COUNT] = {
    "key:left, key:a, pad:left, axis:lx-@0.5", "key:right, key:d, pad:right, axis:lx+@0.5",
    "key:up, key:w, pad:up, axis:ly-@0.5", "key:down, key:s, pad:down, axis:ly+@0.5",
    "key:space, key:return, pad:south", "key:backspace, key:escape, pad:east", "key:h, pad:north",
  };
  for (int a = 0; a < A_COUNT; a++) {
    g->act[a] = cw_action_add(NAMES[a], 0.5f);
    if (!cw_action_bind_text(g->act[a], BINDS[a])) CW_FATAL("feverline: bad bindings for %s", NAMES[a]);
  }
}

const fv_state *tg_state(const tg_game *g) { return bk_match_state(g->match); }
bool tg_busy(const tg_game *g) { return g->match && (g->wait > 0 || bk_match_log_next(g->match) > g->log_seen); }
bool tg_role_cpu(const tg_game *g, int r) { return r >= 0 && r < g->roles && g->cpu_level[r] >= 0; }
int tg_input_seat(const tg_game *g) {
  if (g->mode == UI_SETUP) {
    for (int i = 0; i < g->seats; i++)
      if (g->seat_level[i] < 0) return i;
    return 0;
  }
  return g->turn >= 0 && g->turn < g->roles ? g->ctl[g->turn] : 0;
}

/* ── targets ─────────────────────────────────────────────────────────── */

static void add_target(tg_game *g, tg_target t) {
  if (g->ntargets >= TG_MAX_TARGETS) CW_FATAL("feverline: more than %d targets", TG_MAX_TARGETS);
  g->target[g->ntargets++] = t;
}

void tg_plate_label(const tg_game *g, const tg_target *t, char *buf, size_t cap) {
  const fv_state *s = &g->shown;
  if (t->kind == TG_SETUP) {
    if (t->plate == PL_SETUP_LEVEL) snprintf(buf, cap, "%s: %d surges", SETUP_NAMES[t->value], fv_surges_for(t->value));
    else if (t->plate == PL_SETUP_AGENTS) snprintf(buf, cap, "%d agents", t->value);
    else if (t->plate == PL_SETUP_MAP) snprintf(buf, cap, "%s", FV_MAPS[t->value].title);
    else snprintf(buf, cap, "Begin");
    return;
  }
  if (t->plate == PL_SHARE_OPEN) { snprintf(buf, cap, "Share a card"); return; }
  if (t->plate == PL_BACK) { snprintf(buf, cap, "Back"); return; }
  int kind = bk_move_field(t->move, 0), a = bk_move_field(t->move, 1), b = bk_move_field(t->move, 2);
  switch (kind) {
  case FV_M_TREAT: snprintf(buf, cap, "Treat %s", COL_NAMES[a]); break;
  case FV_M_CURE: snprintf(buf, cap, "Cure %s fever", COL_NAMES[a]); break;
  case FV_M_BUILD: snprintf(buf, cap, "Build a clinic"); break;
  case FV_M_REST: snprintf(buf, cap, "Rest"); break;
  case FV_M_GIVE: snprintf(buf, cap, "Give %s to %s", FV_CITIES[a].name, fv_role_name(s->role[b])); break;
  case FV_M_TAKE: snprintf(buf, cap, "Take %s from %s", FV_CITIES[a].name, fv_role_name(s->role[b])); break;
  case FV_M_DISCARD: snprintf(buf, cap, "Discard %s", FV_CITIES[a].name); break;
  default: fv_label(s, t->move, buf, cap, NULL);
  }
}

/* a hint for a person: what a short plan would play (the cursor starts there, so a newcomer who only confirms
 * still plays sensibly) */
static bk_move hint(tg_game *g) {
  const fv_state *s = &g->shown;
  int who = fv_turn(s, NULL);
  if (who < 0 || tg_role_cpu(g, who) || fv_terminal(s, NULL)) return BK_MOVE_NONE;
  cw_search_options o;
  fv_cpu_options(0, bk_seed_mix(g->seed, (uint64_t)g->decisions), &o);
  o.mistake = 0; /* a hint is the short plan's best */
  cw_search *sr = cw_search_new(&g->search, &o);
  bk_move m = BK_MOVE_NONE;
  if (!sr || !cw_search_run(sr, s, &m)) m = BK_MOVE_NONE;
  cw_search_free(sr);
  return m;
}

static void build_targets(tg_game *g) {
  g->ntargets = 0;
  int keep = 0;
  if (g->mode == UI_SETUP) {
    bool solo = g->seats == 1;
    for (int m = 0; m < FV_NMAPS; m++) { /* the maps in two rows, the world first */
      int row = m < 5 ? 0 : 1, col = row ? m - 5 : m, per = row ? FV_NMAPS - 5 : 5;
      add_target(g, (tg_target){ TG_SETUP, -1, PL_SETUP_MAP, m, BK_MOVE_NONE, { ((float)col - (float)(per - 1) * 0.5f) * 4.6f, 1.0f, -3.0f + 1.4f * (float)row } });
    }
    for (int l = 0; l < 3; l++)
      add_target(g, (tg_target){ TG_SETUP, -1, PL_SETUP_LEVEL, l, BK_MOVE_NONE, { -6.0f + (float)l * 6.0f, 1.0f, -0.2f } });
    if (solo)
      for (int a = 2; a <= 4; a++)
        add_target(g, (tg_target){ TG_SETUP, -1, PL_SETUP_AGENTS, a, BK_MOVE_NONE, { -6.0f + (float)(a - 2) * 6.0f, 1.0f, 1.2f } });
    add_target(g, (tg_target){ TG_SETUP, -1, PL_SETUP_GO, 0, BK_MOVE_NONE, { 0.0f, 1.0f, 2.8f } });
    t3_vec3 pts[TG_MAX_TARGETS];
    for (int i = 0; i < g->ntargets; i++) pts[i] = g->target[i].at;
    cw_cursors_set_points(bk_select_cursors(g->sel), pts, g->ntargets);
    bk_select_enable_all(g->sel, true);
    bk_select_place(g->sel, tg_input_seat(g), g->ntargets - 1);
    g->dirty = false;
    return;
  }
  const fv_state *s = &g->shown;
  int who = fv_turn(s, NULL);
  bk_move h = g->hint = hint(g);
  bk_move mv[BK_MAX_MOVES];
  int n = fv_moves(s, mv, BK_MAX_MOVES, NULL);
  int nplates = 0;
  tg_target plates[TG_MAX_TARGETS];
  if (s->phase == FV_PH_DISCARD) {
    g->mode = UI_DISCARD;
    for (int i = 0; i < n; i++) plates[nplates++] = (tg_target){ TG_PLATE, -1, PL_MOVE, 0, mv[i], { 0 } };
  } else if (g->mode == UI_SHARE) {
    for (int i = 0; i < n; i++) {
      int k = bk_move_field(mv[i], 0);
      if (k == FV_M_GIVE || k == FV_M_TAKE) plates[nplates++] = (tg_target){ TG_PLATE, -1, PL_MOVE, 0, mv[i], { 0 } };
    }
    plates[nplates++] = (tg_target){ TG_PLATE, -1, PL_BACK, 0, BK_MOVE_NONE, { 0 } };
    if (nplates == 1) g->mode = UI_MAIN, nplates = 0; /* nothing to share any more */
  }
  if (g->mode == UI_MAIN) {
    /* cities the agent can reach with one action, the cheapest way */
    for (int c = 0; c < FV_N; c++) {
      bk_move m = fv_move_to(s, c);
      if (m == BK_MOVE_NONE) continue;
      if (m == h || (h != BK_MOVE_NONE && bk_move_field(h, 1) == c && bk_move_field(h, 0) >= FV_M_DRIVE && bk_move_field(h, 0) <= FV_M_SHUTTLE)) {
        keep = g->ntargets;
        m = h; /* the planner's way there */
      }
      add_target(g, (tg_target){ TG_CITY, c, PL_MOVE, 0, m, ly_city(c) });
    }
    bool share = false;
    for (int i = 0; i < n; i++) {
      int k = bk_move_field(mv[i], 0);
      if (k == FV_M_GIVE || k == FV_M_TAKE) { share = true; continue; }
      if (k == FV_M_TREAT || k == FV_M_CURE || k == FV_M_BUILD) plates[nplates++] = (tg_target){ TG_PLATE, -1, PL_MOVE, 0, mv[i], { 0 } };
    }
    if (share) plates[nplates++] = (tg_target){ TG_PLATE, -1, PL_SHARE_OPEN, 0, BK_MOVE_NONE, { 0 } };
    plates[nplates++] = (tg_target){ TG_PLATE, -1, PL_MOVE, 0, bk_move_pack(FV_M_REST, 0, 0, 0, 0, 0, 0, 0), { 0 } };
  }
  for (int i = 0; i < nplates; i++) {
    plates[i].at = ly_plate(i, nplates);
    bk_move pm = plates[i].move;
    if (pm != BK_MOVE_NONE && pm == h) keep = g->ntargets;
    if (plates[i].plate == PL_SHARE_OPEN && h != BK_MOVE_NONE && (bk_move_field(h, 0) == FV_M_GIVE || bk_move_field(h, 0) == FV_M_TAKE)) keep = g->ntargets;
    add_target(g, plates[i]);
  }
  t3_vec3 pts[TG_MAX_TARGETS];
  for (int i = 0; i < g->ntargets; i++) pts[i] = g->target[i].at;
  cw_cursors_set_points(bk_select_cursors(g->sel), pts, g->ntargets);
  bk_select_enable_all(g->sel, true);
  (void)who;
  bk_select_place(g->sel, tg_input_seat(g), g->ntargets ? keep : -1);
  g->dirty = false;
}

int tg_cursor(const tg_game *g) { return g->sel ? bk_select_target(g->sel, tg_input_seat(g)) : -1; }

bk_move tg_preview(const tg_game *g) {
  if (g->mode == UI_SETUP || g->over || tg_busy(g) || g->dirty) return BK_MOVE_NONE;
  int t = tg_cursor(g);
  if (t < 0 || t >= g->ntargets) return BK_MOVE_NONE;
  return g->target[t].move;
}

/* ── the CPU enters its move by pressing toward it ──────────────────── */

static int find_target(const tg_game *g, bk_move move) {
  int kind = bk_move_field(move, 0);
  for (int i = 0; i < g->ntargets; i++) {
    const tg_target *t = &g->target[i];
    if (t->move == move) return i;
    if (g->mode == UI_MAIN && (kind == FV_M_GIVE || kind == FV_M_TAKE) && t->plate == PL_SHARE_OPEN) return i;
  }
  /* a city reached another way than the table offers first: the city target, its move swapped below */
  if (kind >= FV_M_DRIVE && kind <= FV_M_SHUTTLE)
    for (int i = 0; i < g->ntargets; i++)
      if (g->target[i].kind == TG_CITY && g->target[i].city == bk_move_field(move, 1)) return i;
  return -1;
}

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state, (void)seat;
  tg_game *g = user;
  if (tg_busy(g) || g->over || g->mode == UI_SETUP) return false;
  if (g->dirty) build_targets(g);
  memset(out, 0, sizeof *out);
  out->hold = 1;
  int kind = bk_move_field(move, 0);
  if (g->mode == UI_SHARE && kind != FV_M_GIVE && kind != FV_M_TAKE) { /* back out of the share list */
    for (int i = 0; i < g->ntargets; i++)
      if (g->target[i].plate == PL_BACK) {
        return bk_cpu_press_toward(bk_select_cursors(g->sel), NULL, g->ctl[g->turn], i, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP],
                                   g->act[A_DOWN], g->act[A_OK], out);
      }
  }
  int target = find_target(g, move);
  if (target < 0) {
    char lab[96];
    fv_label(&g->shown, move, lab, sizeof lab, NULL);
    CW_FATAL("feverline: the CPU wants a move the table does not offer (%s, mode %d)", lab, g->mode);
  }
  /* the city target plays the table's way there; the CPU's own way (a flight, say) replaces it */
  if (g->target[target].kind == TG_CITY) g->target[target].move = move;
  return bk_cpu_press_toward(bk_select_cursors(g->sel), NULL, g->ctl[g->turn], target, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP],
                             g->act[A_DOWN], g->act[A_OK], out);
}

/* ── the match ───────────────────────────────────────────────────────── */

void tg_free(tg_game *g) {
  for (int i = 0; i < FV_MAX_R; i++) {
    bk_cpu_free(g->cpu[i]);
    g->cpu[i] = NULL;
  }
  bk_match_free(g->match);
  g->match = NULL;
  bk_select_free(g->sel);
  g->sel = NULL;
}

static void start_match(tg_game *g) {
  int roles = g->roles;
  fv_game(&g->rules, roles);
  g->rules.chance = NULL; /* the match resolves chance from outcomes(): the log then names each card */
  fv_search_game(&g->search);
  fv_state init;
  fv_init(&init, roles, g->setup, g->map, g->seed);
  g->match = bk_match_new(&g->rules, &init, bk_seed_mix(g->seed, 0xfe7e));
  for (int r = 0; r < roles; r++)
    if (g->cpu_level[r] >= 0) {
      cw_search_options o;
      fv_cpu_options(g->cpu_level[r], 0, &o);
      g->cpu[r] = bk_cpu_new(&g->search, &(bk_cpu_desc){ .level = g->cpu_level[r], .kind = CW_SEARCH_MCTS, .options = &o,
                                                          .units_per_tick = 1500, .think_ticks = g->fast ? 2 : 0, .press_gap = g->fast ? 2 : 0,
                                                          .seed = g->seed + (uint64_t)r * 977, .next_press = cpu_next_press, .user = g });
    }
  g->shown = init;
  g->log_seen = 0;
  g->turn = -1;
  g->mode = UI_MAIN;
  g->dirty = true;
  g->started = true;
  g->wait = beat_ticks(g, 50);
  for (int i = 0; i < g->seats; i++)
    if (g->seat_level[i] < 0) g->help = true; /* people start with the how-to-play card open */
}

void tg_begin(tg_game *g, int seats, const int *seat_level, int setup, int agents, int map, uint64_t seed, bool fast, bool skip_setup) {
  tg_free(g);
  cw_action act[A_COUNT];
  memcpy(act, g->act, sizeof act);
  memset(g, 0, sizeof *g);
  memcpy(g->act, act, sizeof act);
  if (seats < 1 || seats > FV_MAX_R) CW_FATAL("feverline: %d seats (1..4)", seats);
  g->seats = seats;
  g->seed = seed;
  g->fast = fast;
  g->setup = setup;
  g->sel = bk_select_new(&(bk_select_desc){ .cursor = { .points_xz = true }, .left = act[A_LEFT], .right = act[A_RIGHT],
                                            .up = act[A_UP], .down = act[A_DOWN], .confirm = act[A_OK], .back = act[A_BACK] });
  bool person = false;
  for (int i = 0; i < seats; i++) g->seat_level[i] = seat_level[i], person |= seat_level[i] < 0;
  g->setup_level = setup;
  g->setup_map = g->map = map;
  g->setup_agents = agents < 2 ? 2 : agents > 4 ? 4 : agents;
  for (int i = 0; i < FV_MAX_R; i++) g->cpu_level[i] = -1;
  if (person && skip_setup) {
    setup_done(g);
    return;
  }
  if (person) {
    g->mode = UI_SETUP;
    g->dirty = true;
    g->help = false;
    return;
  }
  /* CPUs alone: no setup card */
  g->roles = seats >= 2 ? seats : g->setup_agents;
  for (int r = 0; r < g->roles; r++) g->cpu_level[r] = seats >= 2 ? seat_level[r] : seat_level[0], g->ctl[r] = r;
  start_match(g);
}

/* the setup card's choice made: seat the agents and start */
static void setup_done(tg_game *g) {
  g->setup = g->setup_level;
  g->map = g->setup_map;
  if (g->seats >= 2) {
    g->roles = g->seats;
    for (int r = 0; r < g->roles; r++) g->cpu_level[r] = g->seat_level[r], g->ctl[r] = r;
  } else {
    g->roles = g->setup_agents;
    for (int r = 0; r < g->roles; r++) g->cpu_level[r] = g->seat_level[0], g->ctl[r] = 0;
  }
  start_match(g);
}

/* how long the table holds a beat */
static int beat_length(const tg_game *g, const tg_beat *b) {
  int kind = bk_move_field(b->move, 0);
  if (b->after.phase == FV_PH_OVER) return beat_ticks(g, 120);
  int chain = b->after.ob_n * 24;
  switch (kind) {
  case FV_M_DRIVE: case FV_M_SHUTTLE: return beat_ticks(g, 30);
  case FV_M_FLY: case FV_M_CHARTER: return beat_ticks(g, 46);
  case FV_M_TREAT: return beat_ticks(g, 24);
  case FV_M_BUILD: return beat_ticks(g, 40);
  case FV_M_CURE: return beat_ticks(g, 110);
  case FV_M_GIVE: case FV_M_TAKE: return beat_ticks(g, 34);
  case FV_M_DISCARD: case FV_M_REST: return beat_ticks(g, 20);
  case FV_M_DRAW: return beat_ticks(g, fv_is_surge(bk_move_field(b->move, 1)) ? 90 : 34);
  case FV_M_FLIP: return beat_ticks(g, (b->before.phase == FV_PH_SURGE ? 100 : 44) + chain);
  default: return beat_ticks(g, 20);
  }
}

/* the table takes the next logged step */
static bool follow_log(tg_game *g) {
  bk_log_entry e;
  if (bk_match_log(g->match, g->log_seen, &e, 1) != 1) return false;
  g->log_seen = e.index + 1;
  if (e.kind == BK_LOG_UNDO) CW_FATAL("feverline: an undo in the log (the game offers none)");
  g->beat.before = g->shown;
  fv_apply(&g->shown, e.move, NULL);
  if (bk_hash(&g->rules, &g->shown) != e.hash) CW_FATAL("feverline: the table's state after step %d is not the match's", e.index);
  g->beat.after = g->shown;
  g->beat.move = e.move;
  g->beat.seat = e.seat;
  g->beat.serial++;
  g->wait = beat_length(g, &g->beat);
  g->dirty = true;
  return true;
}

static void play(tg_game *g, bk_move m) {
  if (!bk_match_play(g->match, m)) {
    char lab[96];
    fv_label(&g->shown, m, lab, sizeof lab, NULL);
    CW_FATAL("feverline: the table offered an illegal move (%s)", lab);
  }
  g->decisions++;
  if (tg_role_cpu(g, g->turn)) g->cpu_moves++;
  g->mode = UI_MAIN;
  g->dirty = true;
}

/* the setup card: the level row, the agents row (one person alone) and Begin */
static void setup_tick(tg_game *g, bk_sel_event ev) {
  if (ev.kind != BK_SEL_CONFIRM || ev.target < 0 || ev.target >= g->ntargets) return;
  const tg_target *t = &g->target[ev.target];
  if (t->plate == PL_SETUP_LEVEL) g->setup_level = t->value;
  else if (t->plate == PL_SETUP_AGENTS) g->setup_agents = t->value;
  else if (t->plate == PL_SETUP_MAP) g->setup_map = t->value;
  else setup_done(g);
}

void tg_tick(tg_game *g) {
  g->ticks++;
  int seat_in = tg_input_seat(g);
  bool person_turn = g->mode == UI_SETUP || (g->turn >= 0 && !tg_role_cpu(g, g->turn));
  if (person_turn) bk_select_feed(g->sel, seat_in);
  /* how to play: any person opens it; confirm or back closes it. The table goes on underneath (CPUs keep
   * playing); only a person's own choice waits for it */
  bool closed = false;
  for (int i = 0; i < g->seats; i++) {
    if (g->seat_level[i] >= 0) continue;
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help, closed = !g->help;
    else if (g->help && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) g->help = false, closed = true;
  }
  if (g->mode == UI_SETUP) {
    if (g->dirty) build_targets(g);
    cw_shell_set_turn(seat_in);
    if (g->help || closed) return;
    setup_tick(g, bk_select_tick(g->sel, seat_in));
    return;
  }
  /* the CPUs think and press every tick (off their turn they return nothing); each seat's actions set once */
  {
    cw_action_value v[FV_MAX_R][4];
    int nv[FV_MAX_R] = { 0 };
    bool cpu_seat[FV_MAX_R] = { false };
    for (int r = 0; r < g->roles; r++)
      if (g->cpu[r]) {
        cw_action_value tmp[4];
        int n = bk_cpu_tick(g->cpu[r], g->match, r, tmp, 4);
        int s = g->ctl[r];
        cpu_seat[s] = true;
        if (n > 0) memcpy(v[s], tmp, sizeof tmp), nv[s] = n;
      }
    for (int s = 0; s < FV_MAX_R; s++)
      if (cpu_seat[s]) cw_input_set_actions(s, v[s], nv[s]);
  }
  if (g->wait > 0) { g->wait--; return; }
  if (follow_log(g)) return;
  if (g->shown.phase == FV_PH_OVER) {
    if (!g->over) g->final_hash = bk_match_hash(g->match);
    g->over = true;
    g->over_ticks++;
    return;
  }
  int who = bk_match_turn(g->match);
  if (who != g->turn) {
    g->turn = who;
    g->mode = UI_MAIN;
    g->dirty = true;
  }
  if (g->shown.phase == FV_PH_DISCARD && g->mode != UI_DISCARD) g->mode = UI_DISCARD, g->dirty = true;
  cw_shell_set_turn(g->ctl[who]); /* hot seats: the free pads drive the seat to move */
  if (g->dirty) build_targets(g);
  if (!tg_role_cpu(g, who) && (g->help || closed)) return; /* the press that closed the card does nothing else */
  bk_sel_event ev = bk_select_tick(g->sel, g->ctl[who]);
  if (ev.kind == BK_SEL_BACK && g->mode == UI_SHARE) {
    g->mode = UI_MAIN;
    g->dirty = true;
    return;
  }
  if (ev.kind != BK_SEL_CONFIRM || ev.target < 0 || ev.target >= g->ntargets) return;
  const tg_target *t = &g->target[ev.target];
  if (t->plate == PL_SHARE_OPEN) {
    g->mode = UI_SHARE;
    build_targets(g);
    return;
  }
  if (t->plate == PL_BACK) {
    g->mode = UI_MAIN;
    build_targets(g);
    return;
  }
  play(g, t->move);
}
