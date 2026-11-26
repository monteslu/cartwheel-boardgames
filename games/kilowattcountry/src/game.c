/* Kilowatt Country: the match on the table. See game.h. */
#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/shell.h"
#include "layout.h"

static const char *const SEAT_NAMES[KC_MAX_P] = { "Amber", "Cobalt", "Rose", "Jade" };
const char *kg_seat_name(int seat) { return seat >= 0 && seat < KC_MAX_P ? SEAT_NAMES[seat] : "?"; }

/* beats in ticks (60 a second); a self-playing cart uses a third */
static int beat_ticks(const kg_game *g, int t) { return g->fast ? (t + 2) / 3 : t; }

void kg_actions_init(kg_game *g) {
  static const char *const NAMES[A_COUNT] = { "left", "right", "up", "down", "choose", "back", "how_to_play" };
  static const char *const BINDS[A_COUNT] = {
    "key:left, key:a, pad:left, axis:lx-@0.5", "key:right, key:d, pad:right, axis:lx+@0.5",
    "key:up, key:w, pad:up, axis:ly-@0.5", "key:down, key:s, pad:down, axis:ly+@0.5",
    "key:space, key:return, pad:south", "key:backspace, key:escape, pad:east", "key:h, pad:north",
  };
  for (int a = 0; a < A_COUNT; a++) {
    g->act[a] = cw_action_add(NAMES[a], 0.5f);
    if (!cw_action_bind_text(g->act[a], BINDS[a])) CW_FATAL("kilowattcountry: bad bindings for %s", NAMES[a]);
  }
}

bool kg_busy(const kg_game *g) { return g->match && (g->wait > 0 || bk_match_log_next(g->match) > g->log_seen); }
int kg_setup_seat(const kg_game *g) {
  for (int i = 0; i < g->players; i++)
    if (g->cpu_level[i] < 0) return i;
  return 0;
}
bool kg_seat_cpu(const kg_game *g, int seat) { return seat >= 0 && seat < g->players && g->cpu_level[seat] >= 0; }

/* ── targets ─────────────────────────────────────────────────────────── */

static void add_target(kg_game *g, kg_target t) {
  if (g->ntargets >= KG_MAX_TARGETS) CW_FATAL("kilowattcountry: more than %d targets", KG_MAX_TARGETS);
  g->target[g->ntargets++] = t;
}

t3_vec3 kg_option_at(int i, int n) { return (t3_vec3){ ((float)i - (float)(n - 1) * 0.5f) * 3.3f, 0.6f, 3.3f }; }
static t3_vec3 pass_at(void) { return (t3_vec3){ -4.6f, 0.6f, 3.3f }; }
static t3_vec3 fuel_done_at(void) { return (t3_vec3){ 10.1f, 0.6f, 0.3f }; }
static t3_vec3 build_done_at(void) { return (t3_vec3){ 0.0f, 0.6f, 3.3f }; }
static t3_vec3 chip_at(int f, int k) { return (t3_vec3){ LY_FUEL_X0 + 0.25f + (float)k * 0.44f, 0.4f, ly_fuel_row_z(f) + 0.62f }; }

/* the suggestion a person's cursor starts on: the CPU's quick sense of a good move (the playout policy, seeded
 * from the decision count so it is the same on every host) */
static bk_move hint(kg_game *g) {
  const kc_state *s = &g->shown;
  if (g->setup) return BK_MOVE_NONE;
  if (kg_seat_cpu(g, g->turn) || kc_terminal(s, NULL) || kc_turn(s, NULL) == BK_CHANCE) return BK_MOVE_NONE;
  bk_move mv[BK_MAX_MOVES];
  int n = kc_moves(s, mv, BK_MAX_MOVES, NULL);
  if (n <= 0) return BK_MOVE_NONE;
  cw_rng r;
  cw_rng_seed(&r, bk_seed_mix(g->seed, (uint64_t)g->decisions));
  return kc_playout(s, mv, n, &r, NULL);
}

/* the setup card: a plate per map down the left of the table (where the plant market lies in play), Begin under them */
static t3_vec3 map_plate_at(int i) { return (t3_vec3){ -10.0f, 0.6f, -5.95f + (float)i * 1.02f }; }
static void build_setup_targets(kg_game *g) {
  g->ntargets = 0;
  int keep = 0;
  for (int m = 0; m < KC_NMAPS && !g->hide_maps; m++) {
    if (m == g->map) keep = g->ntargets;
    add_target(g, (kg_target){ KG_T_MAP, m, BK_MOVE_NONE, map_plate_at(m) });
  }
  if (!g->map_picks) keep = g->ntargets; /* the cursor starts on Begin: one confirm plays the default map */
  add_target(g, (kg_target){ KG_T_BEGIN, 0, BK_MOVE_NONE, map_plate_at(KC_NMAPS) });
  t3_vec3 pts[KG_MAX_TARGETS];
  for (int i = 0; i < g->ntargets; i++) pts[i] = g->target[i].at;
  cw_cursors_set_points(bk_select_cursors(g->sel), pts, g->ntargets);
  bk_select_enable_all(g->sel, true);
  bk_select_place(g->sel, g->turn, keep);
  g->dirty = false;
}

static void build_targets(kg_game *g) {
  if (g->setup) { build_setup_targets(g); return; }
  const kc_state *s = &g->shown;
  int seat = g->turn, keep = 0;
  bk_move h = g->hint = hint(g);
  g->ntargets = 0;
  bk_move mv[BK_MAX_MOVES];
  int n = kc_moves(s, mv, BK_MAX_MOVES, NULL);
  if (g->mode == UI_COUNT) {
    int k = 0;
    int def = kc_fuel_needed(s, seat, g->count_fuel);
    if (h != BK_MOVE_NONE && bk_move_field(h, 0) == KC_M_FUEL && bk_move_field(h, 1) == g->count_fuel) def = bk_move_field(h, 2);
    for (int i = 0; i < n; i++)
      if (bk_move_field(mv[i], 0) == KC_M_FUEL && bk_move_field(mv[i], 1) == g->count_fuel) {
        int c = bk_move_field(mv[i], 2);
        if (c == def || (k == 0)) keep = g->ntargets;
        add_target(g, (kg_target){ KG_T_COUNT, c, mv[i], chip_at(g->count_fuel, k++) });
      }
    if (g->ntargets == 0) { /* nothing of this fuel any more: back to the rows */
      g->mode = UI_MAIN;
      build_targets(g);
      return;
    }
    for (int i = 0; i < g->ntargets; i++)
      if (g->target[i].value == def) keep = i;
  } else {
    int nopt = 0;
    for (int i = 0; i < n; i++) nopt += bk_move_field(mv[i], 0) == KC_M_RAISE || bk_move_field(mv[i], 0) == KC_M_DROP;
    int opt = 0;
    bool fuel_seen[KC_NFUEL] = { false };
    for (int i = 0; i < n; i++) {
      bk_move m = mv[i];
      int kind = bk_move_field(m, 0), a = bk_move_field(m, 1);
      kg_target t = { 0 };
      t.move = m;
      switch (kind) {
      case KC_M_OPEN: t.kind = KG_T_PLANT, t.value = a, t.at = ly_market(a), t.at.y += 0.1f; break;
      case KC_M_PASS: t.kind = KG_T_OPTION, t.value = 0, t.at = pass_at(); break;
      case KC_M_RAISE: case KC_M_DROP: t.kind = KG_T_OPTION, t.value = opt, t.at = kg_option_at(opt, nopt), opt++; break;
      case KC_M_DISCARD: t.kind = KG_T_OWN, t.value = a, t.at = ly_tray_card(seat, a, s->players); break;
      case KC_M_FUEL:
        if (fuel_seen[a]) continue;
        fuel_seen[a] = true;
        t.kind = KG_T_FUEL, t.value = a, t.move = BK_MOVE_NONE;
        t.at = ly_fuel_slot(a, kc_fuel_slots[a] - s->fuel_mkt[a]);
        break;
      case KC_M_DONE: t.kind = KG_T_OPTION, t.value = 0, t.at = s->phase == KC_PH_FUEL ? fuel_done_at() : build_done_at(); break;
      case KC_M_BUILD: t.kind = KG_T_CITY, t.value = a, t.at = ly_city(a); break;
      default: continue;
      }
      if (kind == g->hide_kind) continue; /* the control: a legal move the table never offers */
      if (h != BK_MOVE_NONE && (m == h || (t.kind == KG_T_FUEL && bk_move_field(h, 0) == KC_M_FUEL && bk_move_field(h, 1) == a))) keep = g->ntargets;
      add_target(g, t);
    }
  }
  t3_vec3 pts[KG_MAX_TARGETS];
  for (int i = 0; i < g->ntargets; i++) pts[i] = g->target[i].at;
  cw_cursors *c = bk_select_cursors(g->sel);
  cw_cursors_set_points(c, pts, g->ntargets);
  bk_select_enable_all(g->sel, true);
  bk_select_place(g->sel, seat, g->ntargets ? keep : -1);
  g->dirty = false;
}

int kg_cursor(const kg_game *g) { return bk_select_target(g->sel, g->turn); }

bk_move kg_preview(const kg_game *g) {
  if (g->setup || g->over || kg_busy(g) || g->dirty) return BK_MOVE_NONE;
  int t = kg_cursor(g);
  if (t < 0 || t >= g->ntargets) return BK_MOVE_NONE;
  const kg_target *tt = &g->target[t];
  if (tt->kind == KG_T_FUEL) { /* the row: what the count would start on */
    int k = kc_fuel_needed(&g->shown, g->turn, tt->value);
    if (k < 1) k = 1;
    bk_move m = bk_move_pack(KC_M_FUEL, tt->value, k, 0, 0, 0, 0, 0);
    return bk_is_legal(&g->rules, &g->shown, m) ? m : bk_move_pack(KC_M_FUEL, tt->value, 1, 0, 0, 0, 0, 0);
  }
  return tt->move;
}

/* ── the CPU enters its move by pressing toward it ──────────────────── */

static int find_target(const kg_game *g, int kind, int value, bk_move move) {
  for (int i = 0; i < g->ntargets; i++) {
    const kg_target *t = &g->target[i];
    if (t->kind != kind) continue;
    if (kind == KG_T_FUEL ? t->value == value : t->move == move) return i;
  }
  return -1;
}

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  kg_game *g = user;
  if (kg_busy(g) || g->over || seat != g->turn) return false;
  if (g->dirty) build_targets(g);
  memset(out, 0, sizeof *out);
  out->hold = 1;
  int kind = bk_move_field(move, 0), target = -1;
  bool back = false;
  switch (kind) {
  case KC_M_OPEN: target = find_target(g, KG_T_PLANT, 0, move); break;
  case KC_M_DISCARD: target = find_target(g, KG_T_OWN, 0, move); break;
  case KC_M_BUILD: target = find_target(g, KG_T_CITY, 0, move); break;
  case KC_M_FUEL:
    if (g->mode == UI_COUNT && g->count_fuel != bk_move_field(move, 1)) back = true;
    else if (g->mode == UI_COUNT) target = find_target(g, KG_T_COUNT, 0, move);
    else target = find_target(g, KG_T_FUEL, bk_move_field(move, 1), move);
    break;
  default:
    if (g->mode == UI_COUNT) back = true;
    else target = find_target(g, KG_T_OPTION, 0, move);
  }
  if (back) {
    out->v[0] = (cw_action_value){ g->act[A_BACK], 1 };
    out->count = 1;
    return true;
  }
  if (target < 0) CW_FATAL("kilowattcountry: the CPU wants a move the table does not offer (%016llx, mode %d)", (unsigned long long)move, g->mode);
  return bk_cpu_press_toward(bk_select_cursors(g->sel), NULL, seat, target, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP],
                             g->act[A_DOWN], g->act[A_OK], out);
}

/* ── the match ───────────────────────────────────────────────────────── */

void kg_free(kg_game *g) {
  for (int i = 0; i < KC_MAX_P; i++) {
    bk_cpu_free(g->cpu[i]);
    g->cpu[i] = NULL;
  }
  bk_match_free(g->match);
  g->match = NULL;
  bk_select_free(g->sel);
  g->sel = NULL;
}

/* the match itself, on g->map: the rules, the match and the CPU seats */
static void begin_play(kg_game *g) {
  int players = g->players;
  uint64_t seed = g->seed;
  bool fast = g->fast;
  g->setup = false;
  kc_game(&g->search, players);
  g->rules = g->search;
  g->rules.chance = NULL; /* the match resolves chance from outcomes(): the log then names each draw */
  kc_state init;
  kc_init(&init, players, g->map, seed);
  g->match = bk_match_new(&g->rules, &init, bk_seed_mix(seed, 0x6b77));
  for (int i = 0; i < players; i++)
    if (g->cpu_level[i] >= 0) {
      cw_search_options o;
      kc_cpu_options(g->cpu_level[i], 0, &o);
      g->cpu[i] = bk_cpu_new(&g->search, &(bk_cpu_desc){ .level = g->cpu_level[i], .kind = CW_SEARCH_MCTS, .options = &o,
                                                          .units_per_tick = 6000, .think_ticks = fast ? 2 : 0, .press_gap = fast ? 2 : 0,
                                                          .seed = seed + (uint64_t)i * 977, .next_press = cpu_next_press, .user = g });
    }
  g->shown = init;
  g->log_seen = 0;
  g->turn = -1;
  g->mode = UI_MAIN;
  g->dirty = true;
  g->wait = beat_ticks(g, 40);
  for (int i = 0; i < players; i++)
    if (g->cpu_level[i] < 0) g->help = true; /* people start with the how-to-play card open */
}

void kg_start(kg_game *g, int players, const int *cpu_level, int map, uint64_t seed, bool fast, bool setup) {
  kg_free(g);
  cw_action act[A_COUNT];
  memcpy(act, g->act, sizeof act);
  memset(g, 0, sizeof *g);
  memcpy(g->act, act, sizeof act);
  if (players < 2 || players > KC_MAX_P) CW_FATAL("kilowattcountry: a match of %d seats (2..4)", players);
  g->players = players;
  g->seed = seed;
  g->fast = fast;
  g->sel = bk_select_new(&(bk_select_desc){ .cursor = { .points_xz = true }, .left = act[A_LEFT], .right = act[A_RIGHT],
                                            .up = act[A_UP], .down = act[A_DOWN], .confirm = act[A_OK], .back = act[A_BACK] });
  bool person = false;
  for (int i = 0; i < KC_MAX_P; i++) g->cpu_level[i] = i < players ? cpu_level[i] : -1, person |= i < players && cpu_level[i] < 0;
  static const char *const KIND[KC_M_DRAW] = { "", "open", "pass", "raise", "drop", "discard", "fuel", "done", "build" };
  const char *hide = cw_test_env("KILOWATTCOUNTRY_TEST_HIDE");
  for (int k = 1; hide && k < KC_M_DRAW; k++)
    if (strcmp(hide, KIND[k]) == 0) g->hide_kind = k;
  g->hide_maps = hide && strcmp(hide, "map") == 0;
  if (hide && !g->hide_kind && !g->hide_maps) CW_FATAL("kilowattcountry: KILOWATTCOUNTRY_TEST_HIDE=%s is no move kind", hide);
  g->map = map;
  if (setup && person) { /* the setup card: the table shows the map as it would be dealt */
    g->setup = true;
    g->mode = UI_SETUP;
    g->turn = kg_setup_seat(g);
    kc_init(&g->shown, players, map, seed);
    g->dirty = true;
    return;
  }
  begin_play(g);
}

/* how long the table holds a beat */
static int beat_length(const kg_game *g, const kg_beat *b) {
  const kc_state *a = &b->before, *z = &b->after;
  if (z->phase == KC_PH_OVER) return beat_ticks(g, 300);
  if (z->round != a->round) return beat_ticks(g, 60 + 40 * g->players); /* power: the cities light one by one */
  if (b->seat == BK_CHANCE) return beat_ticks(g, 34);
  if (z->bought != a->bought) return beat_ticks(g, 60);
  switch (bk_move_field(b->move, 0)) {
  case KC_M_OPEN: return beat_ticks(g, 40);
  case KC_M_RAISE: return beat_ticks(g, 28);
  case KC_M_DROP: return beat_ticks(g, 22);
  case KC_M_PASS: return beat_ticks(g, 22);
  case KC_M_DISCARD: return beat_ticks(g, 40);
  case KC_M_FUEL: return beat_ticks(g, 30);
  case KC_M_BUILD: return beat_ticks(g, 40);
  default: return beat_ticks(g, 14);
  }
}

/* the table takes the next logged step */
static bool follow_log(kg_game *g) {
  bk_log_entry e;
  if (bk_match_log(g->match, g->log_seen, &e, 1) != 1) return false;
  g->log_seen = e.index + 1;
  if (e.kind == BK_LOG_UNDO) CW_FATAL("kilowattcountry: an undo in the log (the game offers none)");
  g->beat.before = g->shown;
  kc_apply(&g->shown, e.move, NULL);
  if (bk_hash(&g->rules, &g->shown) != e.hash) CW_FATAL("kilowattcountry: the table's state after step %d is not the match's", e.index);
  g->beat.after = g->shown;
  g->beat.move = e.move;
  g->beat.seat = e.seat;
  g->beat.serial++;
  g->wait = beat_length(g, &g->beat);
  g->dirty = true;
  return true;
}

static void play(kg_game *g, bk_move m) {
  if (!bk_match_play(g->match, m)) CW_FATAL("kilowattcountry: the table offered an illegal move %016llx", (unsigned long long)m);
  g->decisions++;
  if (kg_seat_cpu(g, g->turn)) g->cpu_moves++;
  g->mode = UI_MAIN;
  g->dirty = true;
}

/* the setup card's tick: the first person picks a map (the table redeals it to show) and Begin starts the match */
static void setup_tick(kg_game *g) {
  int seat = g->turn;
  cw_shell_set_turn(seat);
  bk_select_feed(g->sel, seat);
  bool closed = false;
  for (int i = 0; i < g->players; i++) {
    if (kg_seat_cpu(g, i)) continue;
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help, closed = !g->help;
    else if (g->help && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) g->help = false, closed = true;
  }
  if (g->dirty) build_targets(g);
  if (g->help || closed) return;
  bk_sel_event ev = bk_select_tick(g->sel, seat);
  if (ev.kind != BK_SEL_CONFIRM || ev.target < 0 || ev.target >= g->ntargets) return;
  if (ev.pointer) g->pointer_picks++;
  const kg_target *t = &g->target[ev.target];
  if (t->kind == KG_T_MAP) {
    g->map = t->value;
    g->map_picks++;
    kc_init(&g->shown, g->players, g->map, g->seed);
    build_targets(g);
    bk_select_place(g->sel, seat, ev.target); /* the cursor stays on the plate just picked */
    return;
  }
  begin_play(g);
}

void kg_tick(kg_game *g) {
  g->ticks++;
  if (g->setup) { setup_tick(g); return; }
  for (int i = 0; i < g->players; i++)
    if (g->cpu[i]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[i], g->match, i, v, 4);
      cw_input_set_actions(i, v, n);
    }
  if (g->turn >= 0 && !kg_seat_cpu(g, g->turn)) bk_select_feed(g->sel, g->turn);
  /* how to play: any person opens it; confirm or back closes it. The table goes on underneath (CPUs keep
   * playing); only a person's own choice waits for it */
  bool closed = false;
  for (int i = 0; i < g->players; i++) {
    if (kg_seat_cpu(g, i)) continue;
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help, closed = !g->help;
    else if (g->help && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) g->help = false, closed = true;
  }
  if (g->wait > 0) { g->wait--; return; }
  if (follow_log(g)) return;
  if (g->shown.phase == KC_PH_OVER) {
    if (!g->over) g->final_hash = bk_match_hash(g->match);
    g->over = true;
    g->over_ticks++;
    return;
  }
  int seat = bk_match_turn(g->match);
  if (seat != g->turn) {
    g->turn = seat;
    g->mode = UI_MAIN;
    g->dirty = true;
  }
  cw_shell_set_turn(seat); /* hot seats: the free pads drive the seat to move */
  if (g->dirty) build_targets(g);
  if (!kg_seat_cpu(g, seat) && (g->help || closed)) return; /* the press that closed the card does nothing else */
  bk_sel_event ev = bk_select_tick(g->sel, seat);
  if (ev.kind == BK_SEL_BACK && g->mode == UI_COUNT) {
    g->mode = UI_MAIN;
    g->dirty = true;
    return;
  }
  if (ev.kind != BK_SEL_CONFIRM || ev.target < 0 || ev.target >= g->ntargets) return;
  if (ev.pointer) g->pointer_picks++;
  const kg_target *t = &g->target[ev.target];
  if (t->kind == KG_T_FUEL) {
    g->mode = UI_COUNT;
    g->count_fuel = t->value;
    build_targets(g);
    return;
  }
  int kind = bk_move_field(t->move, 0);
  if (kind > 0 && kind < KC_M_DRAW && !kg_seat_cpu(g, seat)) (ev.pointer ? g->pointer_kinds : g->pad_kinds)[kind]++;
  g->last_kind = kind;
  play(g, t->move);
}
