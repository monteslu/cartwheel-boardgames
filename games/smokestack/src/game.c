/* Smokestack match (game.h). */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/shell.h"

const rl_state *rl_game_state(const rl_game *g) { return bk_match_state(g->match); }
bool rl_game_is_cpu(const rl_game *g, int seat) { return seat >= 0 && g->seat[seat].kind == BK_SEAT_CPU; }
bool rl_game_any_person(const rl_game *g) {
  for (int i = 0; i < g->players; i++)
    if (g->seat[i].kind != BK_SEAT_CPU) return true;
  return false;
}

int rl_game_target_of(const rl_game *g, bk_move m) {
  for (int t = 0; t < RL_TARGETS; t++)
    if (g->tmove[t] == m) return t;
  return -1;
}

/* the option plaque a move takes in its phase, -1 when it is not an option */
static int opt_slot(const rl_state *s, bk_move m) {
  int k = rl_mv_kind(m), a = rl_mv_a(m);
  switch (s->phase) {
  case RL_PH_BID: return k == RL_MV_PASS ? 0 : a == 1 ? 1 : a == 2 ? 2 : 3;
  case RL_PH_BUILD: return k == RL_MV_PASS ? 0 : k == RL_MV_SHARE ? 1 : k == RL_MV_CHARTER ? 2 : -1;
  case RL_PH_COLOR: return a;
  case RL_PH_HAUL: return k == RL_MV_PASS ? 0 : k == RL_MV_ENGINE ? 1 : -1;
  case RL_PH_PAYOUT: return k == RL_MV_INCOME ? 0 : 1;
  default: return -1;
  }
}

/* where a board move's site stands */
static t3_vec3 site_at(const rl_state *s, bk_move m) {
  int k = rl_mv_kind(m), a = rl_mv_a(m), b = rl_mv_b(m);
  const rl_map_def *mp = rl_map(s->map);
  switch (k) {
  case RL_MV_PLACE:
  case RL_MV_TOWN:
  case RL_MV_DEST: return rl_hex_at(s->map, mp->set_hex[a], 0.12f);
  case RL_MV_START: return rl_edge_at(s->map, a, b, 0.3f, 0.04f);
  case RL_MV_EXIT: {
    int h = rl_next_tile(s, rl_current(s), NULL, NULL);
    return rl_edge_at(s->map, h, a, 0.18f, 0.04f);
  }
  case RL_MV_GOOD: return rl_good_at(s->map, a, b);
  default: CW_FATAL("smokestack: move kind %d has no site", k);
  }
  return (t3_vec3){ 0, 0, 0 };
}

/* lay the targets out for the moves legal now: options, job cards and board sites */
static void layout_targets(rl_game *g) {
  const rl_state *s = rl_game_state(g);
  int turn = bk_match_turn(g->match);
  /* where the cursor of the seat to move was: it goes to the nearest new target */
  int was = turn >= 0 ? bk_select_target(g->sel, turn) : -1;
  t3_vec3 near = was >= 0 && g->tmove[was] != BK_MOVE_NONE ? g->tpos[was] : (t3_vec3){ NAN, 0, 0 };
  for (int t = 0; t < RL_TARGETS; t++) g->tmove[t] = BK_MOVE_NONE;
  bk_move mv[BK_MAX_MOVES];
  int n = bk_match_over(g->match) ? 0 : bk_match_moves(g->match, mv, BK_MAX_MOVES), site = 0;
  for (int i = 0; i < n; i++) {
    int t = -1, o = opt_slot(s, mv[i]);
    t3_vec3 at;
    if (o >= 0) t = RL_T_OPT0 + o, at = rl_opt_at(s->map, o, rl_opt_count(s->phase));
    else if (rl_mv_kind(mv[i]) == RL_MV_ACTION) t = RL_T_ACT0 + rl_mv_a(mv[i]), at = rl_act_at(s->map, rl_mv_a(mv[i]));
    else {
      if (site >= RL_SITES) CW_FATAL("smokestack: more than %d board choices", RL_SITES);
      t = RL_T_SITE0 + site++;
      at = site_at(s, mv[i]);
    }
    if (g->tmove[t] != BK_MOVE_NONE) CW_FATAL("smokestack: two moves on target %d", t);
    g->tmove[t] = mv[i];
    g->tpos[t] = at;
  }
  int best = -1;
  float bd = 1e30f;
  for (int t = 0; t < RL_TARGETS; t++) {
    bool on = g->tmove[t] != BK_MOVE_NONE;
    if (on) cw_node3d_set_position(g->target[t], g->tpos[t]);
    else cw_node3d_set_position(g->target[t], (t3_vec3){ 0, -50, 0 });
    bk_select_set_enabled(g->sel, t, on);
    if (on && !isnan(near.x)) {
      float dx = g->tpos[t].x - near.x, dz = g->tpos[t].z - near.z, d = dx * dx + dz * dz;
      if (d < bd) bd = d, best = t;
    }
  }
  if (turn >= 0) {
    /* a fresh decision starts on the move the rules' own heuristic likes, else stays near where it was */
    if (best < 0 || isnan(near.x)) {
      float bv = -1e30f;
      for (int t = 0; t < RL_TARGETS; t++)
        if (g->tmove[t] != BK_MOVE_NONE) {
          float v = rl_move_value(s, g->tmove[t]);
          if (v > bv) bv = v, best = t;
        }
    }
    if (best >= 0) bk_select_place(g->sel, turn, best);
  }
  g->targets_for = bk_match_log_next(g->match);
}

/* the CPU's next press: walk the cursor toward the move's target over the targets on now (as a pad's hops go),
 * then confirm */
static bool allow(int player, int target, void *user) { (void)player; return bk_select_enabled(user, target); }
static bool enter_move(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  rl_game *g = user;
  int target = rl_game_target_of(g, move);
  if (target < 0) CW_FATAL("smokestack: the CPU's move 0x%llx has no target", (unsigned long long)move);
  if (!bk_cpu_press_toward_allow(bk_select_cursors(g->sel), NULL, allow, g->sel, seat, target, g->left, g->right, g->up, g->down,
                                 g->ok, out))
    CW_FATAL("smokestack: the CPU's cursor cannot walk from target %d to %d", cw_cursors_target(bk_select_cursors(g->sel), seat), target);
  return true;
}

void rl_game_init(rl_game *g, cw_node parent) {
  memset(g, 0, sizeof *g);
  static const char *const NAMES[6] = { "left", "right", "up", "down", "ok", "back" };
  static const char *const BINDS[6] = {
    "pad:left, axis:lx-@0.5, key:left, key:a", "pad:right, axis:lx+@0.5, key:right, key:d",
    "pad:up, axis:ly-@0.5, key:up, key:w", "pad:down, axis:ly+@0.5, key:down, key:s",
    "pad:south, key:space, key:return", "pad:east, key:backspace",
  };
  cw_action *acts[6] = { &g->left, &g->right, &g->up, &g->down, &g->ok, &g->back };
  for (int i = 0; i < 6; i++) {
    *acts[i] = cw_action_add(NAMES[i], 0.5f);
    cw_action_bind_text(*acts[i], BINDS[i]);
  }
  g->sel = bk_select_new(&(bk_select_desc){ .left = g->left, .right = g->right, .up = g->up, .down = g->down,
                                            .confirm = g->ok, .back = g->back, .pick_radius = 40 });
  for (int t = 0; t < RL_TARGETS; t++) {
    g->target[t] = cw_node_new("Node3D", "Target");
    cw_node_add_child(parent, g->target[t]);
    cw_node3d_set_position(g->target[t], (t3_vec3){ 0, -50, 0 });
    g->tmove[t] = BK_MOVE_NONE;
  }
  cw_cursors_set_nodes(bk_select_cursors(g->sel), g->target, RL_TARGETS);
}

void rl_game_start(rl_game *g, uint64_t seed, bool fast) {
  bk_match_free(g->match);
  for (int s = 0; s < RL_MAX_PLAYERS; s++) bk_cpu_free(g->cpu[s]), g->cpu[s] = NULL;
  int n = bk_lobby_seats(g->seat, RL_MAX_PLAYERS);
  if (n == 1) g->seat[1] = (bk_seat){ BK_SEAT_CPU, 1 }, n = 2; /* alone at the table: a Normal CPU sits opposite */
  if (n < 2 || n > RL_MAX_PLAYERS) CW_FATAL("smokestack: %d players (2 to 4)", n);
  g->players = n;
  g->seed = seed;
  g->fast = fast;
  rl_state s0;
  rl_setup(&s0, n);
  g->match = bk_match_new(&RL_RULES[n], &s0, seed);
  for (int s = 0; s < n; s++)
    if (g->seat[s].kind == BK_SEAT_CPU) {
      cw_search_options o;
      rl_cpu_options(g->seat[s].level, &o);
      g->cpu[s] = bk_cpu_new(&RL_CPU_RULES[n], &(bk_cpu_desc){ .level = g->seat[s].level, .kind = CW_SEARCH_MCTS, .options = &o,
                                                           .next_press = enter_move, .user = g, .seed = bk_seed_mix(seed, (uint64_t)s),
                                                           .think_ticks = fast ? 1 : 0, .press_gap = fast ? 1 : 0,
                                                           .units_per_tick = fast ? 400000 : 6000 });
    }
  /* a match starts from nothing on the cursors: a replay or the next match lays out as the first did */
  for (int t = 0; t < RL_TARGETS; t++) g->tmove[t] = BK_MOVE_NONE;
  for (int p = 0; p < RL_MAX_PLAYERS; p++) bk_select_place(g->sel, p, -1);
  g->over_ticks = 0;
  g->targets_for = -1;
  g->haul_n = 0;
  g->help_open = !fast && rl_game_any_person(g);
}

static int s_good_city(const rl_game *g) { const rl_state *s = rl_game_state(g); return s->phase == RL_PH_ROUTE ? s->good_city : -1; }

void rl_game_tick(rl_game *g) {
  if (bk_match_over(g->match)) {
    if (g->over_ticks++ == 0) {
      g->wins[rl_winner(rl_game_state(g))]++, g->matches++, g->games_done++;
      layout_targets(g);
    }
    return;
  }
  int turn = bk_match_turn(g->match);
  cw_shell_set_turn(turn); /* hot seats: the free pads drive the seat whose turn it is */
  if (g->targets_for != bk_match_log_next(g->match)) layout_targets(g);
  /* CPUs press first (the whole seat each tick), then the rules read every seat alike */
  for (int p = 0; p < g->players; p++)
    if (g->cpu[p]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[p], g->match, p, v, 4);
      cw_input_set_actions(p, v, n);
    }
  if (g->help_open) { /* the card is up: a person's confirm or back closes it; CPUs play on under it */
    for (int p = 0; p < g->players; p++)
      if (!g->cpu[p] && (cw_action_just_pressed(p, g->ok) || cw_action_just_pressed(p, g->back))) g->help_open = false;
    if (!g->cpu[turn]) return;
  }
  if (!g->cpu[turn]) bk_select_feed(g->sel, turn); /* a tap this frame: this tick's recorded pick */
  bk_sel_event e = bk_select_tick(g->sel, turn);
  if (e.kind == BK_SEL_CONFIRM && e.target >= 0 && g->tmove[e.target] != BK_MOVE_NONE) {
    bk_move m = g->tmove[e.target];
    int src = s_good_city(g), color = src >= 0 ? rl_game_state(g)->cube[src][rl_game_state(g)->good_slot] - 1 : -1;
    if (bk_match_play(g->match, m)) {
      if (rl_mv_kind(m) == RL_MV_DEST) { /* the good has gone: no taking it back */
        bk_match_seal(g->match);
        if (g->haul_n == RL_HAUL_EVENTS) memmove(g->haul, g->haul + 1, sizeof g->haul[0] * (RL_HAUL_EVENTS - 1)), g->haul_n--;
        g->haul[g->haul_n++] = (rl_haul_event){ turn, src, color, rl_game_state(g)->route };
      }
      g->decisions++;
      if (g->cpu[turn]) g->cpu_moves++;
      else if (e.pointer) g->pointer_picks++;
      else g->pad_moves++;
    }
  } else if (e.kind == BK_SEL_BACK && !g->cpu[turn] && bk_match_can_undo(g->match)) {
    if (bk_match_undo(g->match)) g->undos++;
  }
  if (g->targets_for != bk_match_log_next(g->match)) layout_targets(g);
}
