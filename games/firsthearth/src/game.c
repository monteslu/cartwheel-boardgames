/* First Hearth: the match on the table. See game.h. */
#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "cartwheel/shell.h"
#include "layout.h"

static const char *const SEAT_NAMES[TR_MAX_P] = { "Ochre", "River", "Fern", "Ember" };
const char *tg_seat_name(int seat) { return seat >= 0 && seat < TR_MAX_P ? SEAT_NAMES[seat] : "?"; }

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
    if (!cw_action_bind_text(g->act[a], BINDS[a])) CW_FATAL("firsthearth: bad bindings for %s", NAMES[a]);
  }
}

const tr_state *tg_state(const tg_game *g) { return bk_match_state(g->match); }
bool tg_busy(const tg_game *g) { return g->wait > 0 || bk_match_log_next(g->match) > g->log_seen; }
bool tg_seat_cpu(const tg_game *g, int seat) { return seat >= 0 && seat < g->players && g->cpu_level[seat] >= 0; }

int tg_default_count(const tr_state *s, int seat, int site) {
  int mx = tr_site_max(s, seat, site);
  if (mx <= 0) return 0;
  if (site > TR_S_GOLD) return mx;
  const tr_player *p = &s->p[seat];
  if (site == TR_S_HUNT) { /* enough hunters to feed everyone this round (a die brings 1.75 food on average) */
    int need = p->kin - p->field - p->res[TR_FOOD];
    int k = need <= 0 ? 1 : (int)((need * 4 + 6) / 7);
    return k < 1 ? 1 : k > mx ? mx : k;
  }
  return mx < 3 ? mx : 3;
}

int tg_menu(const tg_game *g, bk_move *out, int cap) {
  const tr_state *s = &g->shown;
  if (s->phase == TR_PH_PLACE || s->phase == TR_PH_OVER) return 0;
  return tr_moves(s, out, cap, NULL);
}

/* ── targets ─────────────────────────────────────────────────────────── */

static void add_target(tg_game *g, tg_target t) {
  if (g->ntargets >= TG_MAX_TARGETS) CW_FATAL("firsthearth: more than %d targets", TG_MAX_TARGETS);
  g->target[g->ntargets++] = t;
}

/* the count chips of a site: a row beside it, 1..max kin */
static t3_vec3 chip_at(int site, int k, int n) {
  const float *r = LY_SITE[site];
  float x = r[0] + ((float)k - (float)(n - 1) * 0.5f) * 0.68f;
  if (x < LY_BOARD_X0 + 0.6f + (float)k * 0.68f) x = LY_BOARD_X0 + 0.6f + (float)k * 0.68f;
  float z = site == TR_S_HUNT ? r[1] + 0.75f : r[1] + r[3] + 0.45f;
  return (t3_vec3){ x, LY_BOARD_Y + 0.9f, z };
}
/* the option plates of a menu: a row in front of the board */
static t3_vec3 option_at(int i, int n) { return (t3_vec3){ ((float)i - (float)(n - 1) * 0.5f) * 3.6f, LY_BOARD_Y + 1.2f, 4.3f }; }

/* a hint for a person: what a quick one-step look ahead would play (the cursor starts there, so a newcomer who
 * only confirms still plays sensibly); a CPU seat needs none */
static bk_move hint(tg_game *g) {
  const tr_state *s = &g->shown;
  if (tg_seat_cpu(g, g->turn) || tr_terminal(s, NULL) || tr_turn(s, NULL) == BK_CHANCE) return BK_MOVE_NONE;
  cw_search_options o;
  cw_search_level_options(2, CW_SEARCH_GREEDY, &o);
  o.seed = bk_seed_mix(g->seed, (uint64_t)g->decisions);
  cw_search *sr = cw_search_new(&g->search, &o);
  bk_move m = BK_MOVE_NONE;
  if (!sr || !cw_search_run(sr, s, &m)) m = BK_MOVE_NONE;
  cw_search_free(sr);
  return m;
}

static void build_targets(tg_game *g) {
  const tr_state *s = &g->shown;
  int seat = g->turn;
  int keep = 0;
  bk_move h = g->hint = hint(g);
  int h_site = h != BK_MOVE_NONE && bk_move_field(h, 0) == TR_M_PLACE ? bk_move_field(h, 1) : -1;
  g->ntargets = 0;
  if (s->phase == TR_PH_PLACE) {
    if (g->mode == UI_COUNT) {
      int mx = tr_site_max(s, seat, g->pick_site), def = tg_default_count(s, seat, g->pick_site);
      if (h_site == g->pick_site) def = bk_move_field(h, 2);
      for (int k = 1; k <= mx; k++) {
        if (k == def) keep = g->ntargets;
        add_target(g, (tg_target){ TG_T_COUNT, g->pick_site, k, bk_move_pack(TR_M_PLACE, g->pick_site, k, 0, 0, 0, 0, 0),
                                   chip_at(g->pick_site, k - 1, mx) });
      }
    } else {
      int prev = g->pick_site; /* the cursor stays where this seat last chose */
      for (int site = 0; site < TR_NSITES; site++)
        if (tr_site_open(s, seat, site)) {
          if (site == (h_site >= 0 ? h_site : prev)) keep = g->ntargets;
          t3_vec3 at = ly_site(site);
          add_target(g, (tg_target){ TG_T_SITE, site, 0, BK_MOVE_NONE, at });
        }
    }
  } else {
    bk_move mv[8];
    int n = tg_menu(g, mv, 8);
    for (int i = 0; i < n; i++) add_target(g, (tg_target){ TG_T_OPTION, -1, i, mv[i], option_at(i, n) });
    keep = 0;
    /* buying: the cursor starts on "take it"; feeding: on paying with goods; tools: on the largest gain */
    if (n && bk_move_field(mv[0], 0) == TR_M_TOOLS) keep = n - 1;
    for (int i = 0; i < n; i++)
      if (mv[i] == h) keep = i;
  }
  t3_vec3 pts[TG_MAX_TARGETS];
  for (int i = 0; i < g->ntargets; i++) pts[i] = g->target[i].at;
  cw_cursors *c = bk_select_cursors(g->sel);
  cw_cursors_set_points(c, pts, g->ntargets);
  bk_select_enable_all(g->sel, true);
  bk_select_place(g->sel, seat, g->ntargets ? keep : -1);
  g->dirty = false;
}

int tg_cursor(const tg_game *g) { return bk_select_target(g->sel, g->turn); }

bk_move tg_preview(const tg_game *g) {
  if (g->over || tg_busy(g) || g->dirty) return BK_MOVE_NONE;
  int t = tg_cursor(g);
  if (t < 0 || t >= g->ntargets) return BK_MOVE_NONE;
  const tg_target *tt = &g->target[t];
  if (tt->kind == TG_T_SITE) {
    int k = tg_default_count(&g->shown, g->turn, tt->site);
    if (g->hint != BK_MOVE_NONE && bk_move_field(g->hint, 0) == TR_M_PLACE && bk_move_field(g->hint, 1) == tt->site) k = bk_move_field(g->hint, 2);
    return bk_move_pack(TR_M_PLACE, tt->site, k, 0, 0, 0, 0, 0);
  }
  return tt->move;
}

/* ── the CPU enters its move by pressing toward it ──────────────────── */

static int find_target(const tg_game *g, int kind, int site, bk_move move) {
  for (int i = 0; i < g->ntargets; i++) {
    const tg_target *t = &g->target[i];
    if (t->kind != kind) continue;
    if (kind == TG_T_SITE && t->site == site) return i;
    if (kind != TG_T_SITE && t->move == move) return i;
  }
  return -1;
}

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  tg_game *g = user;
  if (tg_busy(g) || g->over || seat != g->turn) return false;
  if (g->dirty) build_targets(g);
  memset(out, 0, sizeof *out);
  out->hold = 1;
  int kind = bk_move_field(move, 0), target;
  if (kind == TR_M_PLACE) {
    int site = bk_move_field(move, 1);
    if (g->mode == UI_COUNT && g->pick_site != site) {
      out->v[0] = (cw_action_value){ g->act[A_BACK], 1 };
      out->count = 1;
      return true;
    }
    target = g->mode == UI_COUNT ? find_target(g, TG_T_COUNT, site, move) : find_target(g, TG_T_SITE, site, move);
  } else {
    target = find_target(g, TG_T_OPTION, -1, move);
  }
  if (target < 0) CW_FATAL("firsthearth: the CPU wants a move the table does not offer (%016llx, mode %d)", (unsigned long long)move, g->mode);
  return bk_cpu_press_toward(bk_select_cursors(g->sel), NULL, seat, target, g->act[A_LEFT], g->act[A_RIGHT], g->act[A_UP],
                             g->act[A_DOWN], g->act[A_OK], out);
}

/* ── the match ───────────────────────────────────────────────────────── */

void tg_free(tg_game *g) {
  for (int i = 0; i < TR_MAX_P; i++) {
    bk_cpu_free(g->cpu[i]);
    g->cpu[i] = NULL;
  }
  bk_match_free(g->match);
  g->match = NULL;
  bk_select_free(g->sel);
  g->sel = NULL;
}

void tg_start(tg_game *g, int players, const int *cpu_level, uint64_t seed, bool fast) {
  tg_free(g);
  cw_action act[A_COUNT];
  memcpy(act, g->act, sizeof act);
  memset(g, 0, sizeof *g);
  memcpy(g->act, act, sizeof act);
  if (players < 2 || players > TR_MAX_P) CW_FATAL("firsthearth: a match of %d seats (2..4)", players);
  g->players = players;
  g->seed = seed;
  g->fast = fast;
  tr_game(&g->search, players);
  g->rules = g->search;
  g->rules.chance = NULL; /* the match resolves chance from outcomes(): the log then names each roll and draw */
  tr_state init;
  tr_init(&init, players, seed);
  g->match = bk_match_new(&g->rules, &init, bk_seed_mix(seed, 0x7121));
  g->sel = bk_select_new(&(bk_select_desc){ .cursor = { .points_xz = true }, .left = act[A_LEFT], .right = act[A_RIGHT],
                                            .up = act[A_UP], .down = act[A_DOWN], .confirm = act[A_OK], .back = act[A_BACK] });
  for (int i = 0; i < TR_MAX_P; i++) g->cpu_level[i] = i < players ? cpu_level[i] : -1;
  for (int i = 0; i < players; i++)
    if (g->cpu_level[i] >= 0) {
      cw_search_options o;
      tr_cpu_options(g->cpu_level[i], 0, &o);
      g->cpu[i] = bk_cpu_new(&g->search, &(bk_cpu_desc){ .level = g->cpu_level[i], .kind = CW_SEARCH_MCTS, .options = &o,
                                                          .units_per_tick = 8000, .think_ticks = fast ? 2 : 0, .press_gap = fast ? 2 : 0,
                                                          .seed = seed + (uint64_t)i * 977, .next_press = cpu_next_press, .user = g });
    }
  /* the table starts as the fresh village: the opening deal shows as beats */
  g->shown = init;
  g->log_seen = 0;
  g->turn = -1;
  g->mode = UI_SITES;
  g->pick_site = -1;
  g->dirty = true;
  g->wait = beat_ticks(g, 40);
}

/* how long the table holds a beat */
static int beat_length(const tg_game *g, const tg_beat *b) {
  int kind = bk_move_field(b->move, 0);
  if (b->after.phase == TR_PH_OVER) return beat_ticks(g, 150);
  if (b->after.round != b->before.round) return beat_ticks(g, 110); /* fed, cleared up, a new round */
  switch (kind) {
  case TR_M_PLACE: return beat_ticks(g, 26);
  case TR_M_ROLL: return beat_ticks(g, 96);
  case TR_M_TOOLS: return beat_ticks(g, 34);
  case TR_M_BUY: return beat_ticks(g, bk_move_field(b->move, 1) ? 60 : 20);
  case TR_M_FEED: return beat_ticks(g, 40);
  case TR_M_DRAW_CARD: case TR_M_DRAW_BLD: return beat_ticks(g, 12);
  default: return beat_ticks(g, 20);
  }
}

/* the table takes the next logged step */
static bool follow_log(tg_game *g) {
  bk_log_entry e;
  if (bk_match_log(g->match, g->log_seen, &e, 1) != 1) return false;
  g->log_seen = e.index + 1;
  if (e.kind == BK_LOG_UNDO) CW_FATAL("firsthearth: an undo in the log (the game offers none)");
  g->beat.before = g->shown;
  tr_apply(&g->shown, e.move, NULL);
  if (bk_hash(&g->rules, &g->shown) != e.hash) CW_FATAL("firsthearth: the table's state after step %d is not the match's", e.index);
  g->beat.after = g->shown;
  g->beat.move = e.move;
  g->beat.seat = e.seat;
  g->beat.serial++;
  g->wait = beat_length(g, &g->beat);
  g->dirty = true;
  return true;
}

static void play(tg_game *g, bk_move m) {
  if (!bk_match_play(g->match, m)) CW_FATAL("firsthearth: the table offered an illegal move %016llx", (unsigned long long)m);
  g->decisions++;
  if (tg_seat_cpu(g, g->turn)) g->cpu_moves++;
  g->mode = UI_SITES;
  g->dirty = true;
}

void tg_tick(tg_game *g) {
  g->ticks++;
  /* the CPUs think and press every tick (off their turn they return nothing) */
  for (int i = 0; i < g->players; i++)
    if (g->cpu[i]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[i], g->match, i, v, 4);
      cw_input_set_actions(i, v, n);
    }
  if (g->turn >= 0 && !tg_seat_cpu(g, g->turn)) bk_select_feed(g->sel, g->turn);
  /* how to play: any person opens it; confirm or back closes it. The table goes on underneath (CPUs keep
   * playing); only a person's own choice waits for it */
  bool closed = false;
  for (int i = 0; i < g->players; i++) {
    if (tg_seat_cpu(g, i)) continue;
    if (cw_action_just_pressed(i, g->act[A_HELP])) g->help = !g->help, closed = !g->help;
    else if (g->help && (cw_action_just_pressed(i, g->act[A_OK]) || cw_action_just_pressed(i, g->act[A_BACK]))) g->help = false, closed = true;
  }
  if (g->wait > 0) { g->wait--; return; }
  if (follow_log(g)) return;
  if (g->shown.phase == TR_PH_OVER) {
    if (!g->over) g->final_hash = bk_match_hash(g->match);
    g->over = true;
    g->over_ticks++;
    return;
  }
  int seat = bk_match_turn(g->match);
  if (seat != g->turn) {
    g->turn = seat;
    g->mode = UI_SITES;
    g->dirty = true;
  }
  /* a menu step (tools, buy, feed) or placing */
  int want_mode = g->shown.phase == TR_PH_PLACE ? (g->mode == UI_COUNT ? UI_COUNT : UI_SITES) : UI_MENU;
  if (want_mode != g->mode) {
    g->mode = want_mode;
    g->dirty = true;
  }
  cw_shell_set_turn(seat); /* hot seats: the free pads drive the seat to move */
  if (g->dirty) build_targets(g);
  if (!tg_seat_cpu(g, seat) && (g->help || closed)) return; /* the press that closed the card does nothing else */
  bk_sel_event ev = bk_select_tick(g->sel, seat);
  if (ev.kind == BK_SEL_BACK && g->mode == UI_COUNT) {
    g->mode = UI_SITES;
    g->dirty = true;
    return;
  }
  if (ev.kind != BK_SEL_CONFIRM || ev.target < 0 || ev.target >= g->ntargets) return;
  const tg_target *t = &g->target[ev.target];
  if (t->kind == TG_T_SITE) {
    g->pick_site = t->site;
    int mx = tr_site_max(&g->shown, seat, t->site);
    if (t->site <= TR_S_GOLD && mx > 1) {
      g->mode = UI_COUNT;
      build_targets(g);
      return;
    }
    play(g, bk_move_pack(TR_M_PLACE, t->site, mx, 0, 0, 0, 0, 0));
    return;
  }
  play(g, t->move);
}
