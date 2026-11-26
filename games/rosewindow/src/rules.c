/* Rose Window: the rules. See rules.h. */
#include "rules.h"

#include <stdio.h>
#include <string.h>

const char *const rw_colour_name[RW_COLOURS] = { "ruby", "cobalt", "amber", "verdigris", "violet" };

const rw_tool_info rw_tool_infos[RW_TOOL_KINDS] = {
  { "Grozing Iron", "Raise or lower one pool die by one." },
  { "Turning Peg", "Turn one pool die to its opposite face." },
  { "Second Firing", "Roll one pool die again." },
  { "Kiln Door", "Roll every pool die again." },
  { "Lone Pane", "Set a die on a pane that touches no other die." },
  { "Tinter's Brush", "Move one of your dice, ignoring colour marks." },
  { "Etcher's Point", "Move one of your dice, ignoring number marks." },
  { "Twin Leading", "First turn of a round only: set two dice now, sit out your second turn." },
  { "Back to the Bag", "Put one pool die back in the bag and draw a new one." },
  { "Clear Glaze", "Set a die ignoring the mark on its pane." },
};

const rw_goal_info rw_goal_infos[RW_GOAL_KINDS] = {
  { "Spectrum Rows", "Each full row of five colours", 6 },
  { "Spectrum Columns", "Each full column of four colours", 5 },
  { "Stepped Rows", "Each full row of five numbers", 5 },
  { "Stepped Columns", "Each full column of four numbers", 4 },
  { "Dawn Pairs", "Each pair of a 1 and a 2", 2 },
  { "Noon Pairs", "Each pair of a 3 and a 4", 2 },
  { "Dusk Pairs", "Each pair of a 5 and a 6", 2 },
  { "Full Scale", "Each set of 1 to 6", 5 },
  { "Full Palette", "Each set of all five colours", 4 },
  { "Corner Chains", "Each die touching its colour at a corner", 1 },
};

static void pick_distinct(cw_rng *r, int n, int from, uint8_t *out) {
  int k = 0;
  while (k < n) {
    int v = cw_rng_int(r, 0, from - 1);
    bool dup = false;
    for (int i = 0; i < k; i++) dup |= out[i] == v;
    if (!dup) out[k++] = (uint8_t)v;
  }
}

void rw_init(rw_state *s, int players, uint64_t seed) {
  if (players < 1 || players > BK_MAX_SEATS) CW_FATAL("rosewindow: %d players (1..4)", players);
  memset(s, 0, sizeof *s);
  cw_rng r;
  cw_rng_seed(&r, seed);
  s->players = (uint8_t)players;
  s->phase = RW_PHASE_SETUP;
  for (int c = 0; c < RW_COLOURS; c++) s->bag[c] = RW_PER_COLOUR;
  uint8_t offers[2 * BK_MAX_SEATS];
  pick_distinct(&r, 2 * players, RW_FRAMES, offers);
  uint8_t colours[BK_MAX_SEATS];
  pick_distinct(&r, players, RW_COLOURS, colours);
  for (int i = 0; i < players; i++) {
    rw_player *pl = &s->p[i];
    /* the gentler frame first */
    uint8_t a = offers[2 * i], b = offers[2 * i + 1];
    pl->offer[0] = a < b ? a : b;
    pl->offer[1] = a < b ? b : a;
    pl->frame = 0xff;
    pl->colour = colours[i];
  }
  s->ntools = RW_TOOLS_IN_PLAY;
  pick_distinct(&r, RW_TOOLS_IN_PLAY, RW_TOOL_KINDS, s->tools);
  s->ngoals = 3;
  pick_distinct(&r, s->ngoals, RW_GOAL_KINDS, s->goals);
}

int rw_seat_of(const rw_state *s, int k) {
  int p = s->players;
  return k < p ? (s->first + k) % p : (s->first + (2 * p - 1 - k)) % p;
}

int rw_tool_cost(const rw_state *s, int t) { return s->tool_used[t] ? 2 : 1; }

/* ── placement ─────────────────────────────────────────────────────── */

static bool on_edge(int pane) {
  int r = pane / RW_COLS, c = pane % RW_COLS;
  return r == 0 || r == RW_ROWS - 1 || c == 0 || c == RW_COLS - 1;
}

bool rw_can_place_on(const uint8_t *cell, int placed, const rw_frame *fr, int pane, uint8_t die, int ignore, bool lone) {
  if (cell[pane]) return false;
  int col = rw_die_colour(die), val = rw_die_value(die);
  int code = fr->cell[pane];
  int mc = rw_mark_colour(code), mv = rw_mark_value(code);
  if (mc >= 0 && mc != col && !(ignore & 1)) return false;
  if (mv > 0 && mv != val && !(ignore & 2)) return false;
  int r = pane / RW_COLS, c = pane % RW_COLS;
  bool touches = false;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      if (!dr && !dc) continue;
      int rr = r + dr, cc = c + dc;
      if (rr < 0 || rr >= RW_ROWS || cc < 0 || cc >= RW_COLS) continue;
      uint8_t n = cell[rr * RW_COLS + cc];
      if (!n) continue;
      touches = true;
      if (dr == 0 || dc == 0)
        if (rw_die_colour(n) == col || rw_die_value(n) == val) return false;
    }
  if (placed == 0) return on_edge(pane);
  return lone ? !touches : touches;
}

bool rw_can_place(const rw_state *s, int seat, int pane, uint8_t die, int ignore, bool lone) {
  const rw_player *pl = &s->p[seat];
  if (pl->frame >= RW_FRAMES) return false;
  return rw_can_place_on(pl->cell, pl->placed, &rw_frames[pl->frame], pane, die, ignore, lone);
}

/* moving a die within a window: from must hold one, to must take it with the die lifted out */
static bool can_shift(const rw_state *s, int seat, int from, int to, int ignore) {
  const rw_player *pl = &s->p[seat];
  if (from == to || !pl->cell[from] || pl->cell[to]) return false;
  uint8_t cell[RW_CELLS];
  memcpy(cell, pl->cell, sizeof cell);
  uint8_t d = cell[from];
  cell[from] = 0;
  return rw_can_place_on(cell, pl->placed - 1, &rw_frames[pl->frame], to, d, ignore, false);
}

bool rw_tool_ready(const rw_state *s, int t) {
  if (s->phase != RW_PHASE_DRAFT || s->tool_now || t < 0 || t >= s->ntools) return false;
  int seat = rw_seat_of(s, s->turn_idx);
  return s->p[seat].favour >= rw_tool_cost(s, t);
}

/* ── moves ─────────────────────────────────────────────────────────── */

/* pool slots holding a die no lower slot already shows (identical dice give identical moves) */
static int distinct_slots(const rw_state *s, int *out) {
  int n = 0;
  for (int i = 0; i < s->pool_n; i++) {
    uint8_t d = s->pool[i];
    if (!d) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) dup |= s->pool[j] == d;
    if (!dup) out[n++] = i;
  }
  return n;
}

#define PUSH(m) do { if (n >= cap) CW_FATAL("rosewindow: more than %d moves", cap); out[n++] = (m); } while (0)

int rw_moves(const rw_state *s, bk_move *out, int cap) {
  int n = 0;
  if (s->phase == RW_PHASE_SETUP) {
    PUSH(rw_move(RW_MV_FRAME, 0, 0, 0));
    PUSH(rw_move(RW_MV_FRAME, 1, 0, 0));
    return n;
  }
  if (s->phase != RW_PHASE_DRAFT || s->draw_mask || s->reroll_mask) return 0;
  int seat = rw_seat_of(s, s->turn_idx);
  const rw_player *pl = &s->p[seat];
  int slots[RW_POOL_MAX];
  int ns = distinct_slots(s, slots);
  bool can_set = s->places_left > 0;
  if (can_set)
    for (int k = 0; k < ns; k++)
      for (int pane = 0; pane < RW_CELLS; pane++)
        if (rw_can_place(s, seat, pane, s->pool[slots[k]], 0, false)) PUSH(rw_move(RW_MV_PLACE, slots[k], pane, 0));
  for (int t = 0; t < s->ntools; t++) {
    if (!rw_tool_ready(s, t)) continue;
    switch ((rw_tool_kind)s->tools[t]) {
    case RW_T_NUDGE:
      if (!can_set) break;
      for (int k = 0; k < ns; k++) {
        int v = rw_die_value(s->pool[slots[k]]);
        if (v > 1) PUSH(rw_move(RW_MV_TOOL, t, slots[k], 0));
        if (v < 6) PUSH(rw_move(RW_MV_TOOL, t, slots[k], 1));
      }
      break;
    case RW_T_TURN:
    case RW_T_REFIRE:
    case RW_T_REDRAW:
      if (!can_set) break;
      for (int k = 0; k < ns; k++) PUSH(rw_move(RW_MV_TOOL, t, slots[k], 0));
      break;
    case RW_T_KILN:
      if (can_set && ns) PUSH(rw_move(RW_MV_TOOL, t, 0, 0));
      break;
    case RW_T_LONE:
      if (!can_set || pl->placed == 0) break;
      for (int k = 0; k < ns; k++)
        for (int pane = 0; pane < RW_CELLS; pane++)
          if (rw_can_place(s, seat, pane, s->pool[slots[k]], 0, true)) PUSH(rw_move(RW_MV_TOOL, t, slots[k], pane));
      break;
    case RW_T_GLAZE:
      if (!can_set) break;
      for (int k = 0; k < ns; k++)
        for (int pane = 0; pane < RW_CELLS; pane++) {
          uint8_t d = s->pool[slots[k]];
          if (rw_can_place(s, seat, pane, d, 3, false) && !rw_can_place(s, seat, pane, d, 0, false))
            PUSH(rw_move(RW_MV_TOOL, t, slots[k], pane));
        }
      break;
    case RW_T_TINT:
    case RW_T_ETCH: {
      int ignore = s->tools[t] == RW_T_TINT ? 1 : 2;
      for (int a = 0; a < RW_CELLS; a++)
        if (pl->cell[a])
          for (int b = 0; b < RW_CELLS; b++)
            if (can_shift(s, seat, a, b, ignore)) PUSH(rw_move(RW_MV_TOOL, t, a, b));
      break;
    }
    case RW_T_TWIN: {
      int dice = 0;
      for (int i = 0; i < s->pool_n; i++) dice += s->pool[i] != 0;
      if (s->turn_idx < s->players && s->places_left == 1 && dice >= 2) PUSH(rw_move(RW_MV_TOOL, t, 0, 0));
      break;
    }
    }
  }
  PUSH(rw_move(RW_MV_PASS, 0, 0, 0));
  return n;
}

/* ── turns and rounds ──────────────────────────────────────────────── */

static void finish(rw_state *s) {
  for (int i = 0; i < s->players; i++) {
    rw_breakdown b;
    rw_score(s, i, &b);
    s->p[i].score = (int16_t)b.total;
  }
  s->phase = RW_PHASE_OVER;
}

static void start_round(rw_state *s) {
  s->round++;
  s->turn_idx = 0;
  s->first = (uint8_t)((s->round - 1) % s->players);
  s->pool_n = (uint8_t)(s->players == 1 ? 3 : 2 * s->players + 1);
  memset(s->pool, 0, sizeof s->pool);
  s->draw_mask = (uint16_t)((1u << s->pool_n) - 1);
  s->reroll_mask = 0;
  s->tool_now = 0;
  s->places_left = 1;
  for (int i = 0; i < s->players; i++) s->p[i].skip = 0;
}

static void end_round(rw_state *s) {
  for (int i = 0; i < s->pool_n; i++)
    if (s->pool[i]) {
      if (s->track_n < RW_TRACK_MAX) {
        s->track[s->track_n] = s->pool[i];
        s->track_round[s->track_n] = s->round;
        s->track_n++;
      }
      s->track_sum = (uint16_t)(s->track_sum + rw_die_value(s->pool[i]));
      s->pool[i] = 0;
    }
  if (s->round >= RW_ROUNDS) { finish(s); return; }
  start_round(s);
}

static void end_turn(rw_state *s) {
  int p = s->players;
  s->turn_idx++;
  while (s->turn_idx < 2 * p && s->turn_idx >= p && s->p[rw_seat_of(s, s->turn_idx)].skip) s->turn_idx++;
  if (s->turn_idx >= 2 * p) { end_round(s); return; }
  s->tool_now = 0;
  s->places_left = 1;
}

static void set_die(rw_state *s, int seat, int slot, int pane) {
  rw_player *pl = &s->p[seat];
  uint8_t d = s->pool[slot];
  if (!d) CW_FATAL("rosewindow: pool slot %d is empty", slot);
  s->pool[slot] = 0;
  pl->cell[pane] = d;
  pl->placed++;
  s->places_left--;
  if (s->places_left == 0) end_turn(s);
}

void rw_apply(rw_state *s, bk_move m) {
  int kind = rw_mv_kind(m), a = rw_mv_a(m), b = rw_mv_b(m);
  if (kind == RW_MV_DIE) {
    uint16_t bit = (uint16_t)(1u << a);
    if (s->draw_mask & bit) {
      if (!s->bag[b]) CW_FATAL("rosewindow: drew a %s die from a bag without one", rw_colour_name[b]);
      s->bag[b]--;
      s->draw_mask &= (uint16_t)~bit;
    } else if (s->reroll_mask & bit) {
      if (rw_die_colour(s->pool[a]) != b) CW_FATAL("rosewindow: a reroll changed a die's colour");
      s->reroll_mask &= (uint16_t)~bit;
    } else {
      CW_FATAL("rosewindow: a die outcome for slot %d with no die due", a);
    }
    s->pool[a] = RW_DIE(b, rw_mv_c(m));
    return;
  }
  s->moves_played++;
  if (s->phase == RW_PHASE_SETUP) {
    if (kind != RW_MV_FRAME) CW_FATAL("rosewindow: move kind %d in setup", kind);
    rw_player *pl = &s->p[s->setup_seat];
    pl->frame = pl->offer[a & 1];
    pl->favour = rw_frames[pl->frame].rating;
    s->setup_seat++;
    if (s->setup_seat >= s->players) {
      s->phase = RW_PHASE_DRAFT;
      start_round(s);
    }
    return;
  }
  if (s->phase != RW_PHASE_DRAFT) CW_FATAL("rosewindow: a move after the game ended");
  int seat = rw_seat_of(s, s->turn_idx);
  rw_player *pl = &s->p[seat];
  if (kind == RW_MV_PASS) { end_turn(s); return; }
  if (kind == RW_MV_PLACE) { set_die(s, seat, a, b); return; }
  if (kind != RW_MV_TOOL) CW_FATAL("rosewindow: move kind %d in the draft", kind);
  int t = a;
  int x = b, y = rw_mv_c(m);
  pl->favour = (uint8_t)(pl->favour - rw_tool_cost(s, t));
  s->tool_used[t]++;
  s->tool_now = 1;
  switch ((rw_tool_kind)s->tools[t]) {
  case RW_T_NUDGE: {
    int v = rw_die_value(s->pool[x]) + (y ? 1 : -1);
    s->pool[x] = RW_DIE(rw_die_colour(s->pool[x]), v);
    break;
  }
  case RW_T_TURN: s->pool[x] = RW_DIE(rw_die_colour(s->pool[x]), 7 - rw_die_value(s->pool[x])); break;
  case RW_T_REFIRE: s->reroll_mask |= (uint16_t)(1u << x); break;
  case RW_T_KILN:
    for (int i = 0; i < s->pool_n; i++)
      if (s->pool[i]) s->reroll_mask |= (uint16_t)(1u << i);
    break;
  case RW_T_REDRAW:
    s->bag[rw_die_colour(s->pool[x])]++;
    s->pool[x] = 0;
    s->draw_mask |= (uint16_t)(1u << x);
    break;
  case RW_T_LONE:
  case RW_T_GLAZE: set_die(s, seat, x, y); break;
  case RW_T_TINT:
  case RW_T_ETCH:
    pl->cell[y] = pl->cell[x];
    pl->cell[x] = 0;
    break;
  case RW_T_TWIN:
    s->places_left = 2;
    pl->skip = 1;
    break;
  }
}

int rw_turn(const rw_state *s) {
  if (s->phase == RW_PHASE_SETUP) return s->setup_seat;
  if (s->draw_mask || s->reroll_mask) return BK_CHANCE;
  return rw_seat_of(s, s->turn_idx);
}

bool rw_terminal(const rw_state *s) { return s->phase == RW_PHASE_OVER; }

int rw_outcomes(const rw_state *s, bk_move *out, uint32_t *weight, int cap) {
  int n = 0;
  if (s->draw_mask) {
    int slot = __builtin_ctz(s->draw_mask);
    for (int c = 0; c < RW_COLOURS; c++)
      if (s->bag[c])
        for (int v = 1; v <= 6; v++) {
          if (n >= cap) CW_FATAL("rosewindow: outcome buffer of %d", cap);
          out[n] = rw_move(RW_MV_DIE, slot, c, v);
          weight[n++] = s->bag[c];
        }
    if (!n) CW_FATAL("rosewindow: the bag is empty");
  } else if (s->reroll_mask) {
    int slot = __builtin_ctz(s->reroll_mask);
    for (int v = 1; v <= 6; v++) {
      out[n] = rw_move(RW_MV_DIE, slot, rw_die_colour(s->pool[slot]), v);
      weight[n++] = 1;
    }
  }
  return n;
}

/* ── scoring ───────────────────────────────────────────────────────── */

static int row_full(const uint8_t *cell, int r) {
  for (int c = 0; c < RW_COLS; c++) if (!cell[r * RW_COLS + c]) return 0;
  return 1;
}
static int col_full(const uint8_t *cell, int c) {
  for (int r = 0; r < RW_ROWS; r++) if (!cell[r * RW_COLS + c]) return 0;
  return 1;
}

int rw_goal_points(int goal, const uint8_t cell[RW_CELLS]) {
  int pts = rw_goal_infos[goal].points, n = 0;
  int vcount[7] = { 0 }, ccount[RW_COLOURS] = { 0 };
  for (int i = 0; i < RW_CELLS; i++)
    if (cell[i]) vcount[rw_die_value(cell[i])]++, ccount[rw_die_colour(cell[i])]++;
  switch ((rw_goal_kind)goal) {
  case RW_G_ROW_COLOURS:
  case RW_G_ROW_VALUES:
    for (int r = 0; r < RW_ROWS; r++) {
      if (!row_full(cell, r)) continue;
      unsigned seen = 0;
      bool ok = true;
      for (int c = 0; c < RW_COLS; c++) {
        uint8_t d = cell[r * RW_COLS + c];
        int k = goal == RW_G_ROW_COLOURS ? rw_die_colour(d) : rw_die_value(d);
        ok &= !(seen >> k & 1u);
        seen |= 1u << k;
      }
      n += ok;
    }
    return n * pts;
  case RW_G_COL_COLOURS:
  case RW_G_COL_VALUES:
    for (int c = 0; c < RW_COLS; c++) {
      if (!col_full(cell, c)) continue;
      unsigned seen = 0;
      bool ok = true;
      for (int r = 0; r < RW_ROWS; r++) {
        uint8_t d = cell[r * RW_COLS + c];
        int k = goal == RW_G_COL_COLOURS ? rw_die_colour(d) : rw_die_value(d);
        ok &= !(seen >> k & 1u);
        seen |= 1u << k;
      }
      n += ok;
    }
    return n * pts;
  case RW_G_LOW: return (vcount[1] < vcount[2] ? vcount[1] : vcount[2]) * pts;
  case RW_G_MID: return (vcount[3] < vcount[4] ? vcount[3] : vcount[4]) * pts;
  case RW_G_HIGH: return (vcount[5] < vcount[6] ? vcount[5] : vcount[6]) * pts;
  case RW_G_SCALE: {
    int m = 99;
    for (int v = 1; v <= 6; v++) if (vcount[v] < m) m = vcount[v];
    return m * pts;
  }
  case RW_G_PALETTE: {
    int m = 99;
    for (int c = 0; c < RW_COLOURS; c++) if (ccount[c] < m) m = ccount[c];
    return m * pts;
  }
  case RW_G_CHAINS:
    for (int i = 0; i < RW_CELLS; i++) {
      if (!cell[i]) continue;
      int r = i / RW_COLS, c = i % RW_COLS;
      bool hit = false;
      for (int dr = -1; dr <= 1; dr += 2)
        for (int dc = -1; dc <= 1; dc += 2) {
          int rr = r + dr, cc = c + dc;
          if (rr < 0 || rr >= RW_ROWS || cc < 0 || cc >= RW_COLS) continue;
          uint8_t d = cell[rr * RW_COLS + cc];
          if (d && rw_die_colour(d) == rw_die_colour(cell[i])) hit = true;
        }
      n += hit;
    }
    return n * pts;
  }
  return 0;
}

void rw_score(const rw_state *s, int seat, rw_breakdown *out) {
  const rw_player *pl = &s->p[seat];
  memset(out, 0, sizeof *out);
  int empty = 0;
  for (int i = 0; i < RW_CELLS; i++) {
    uint8_t d = pl->cell[i];
    if (!d) { empty++; continue; }
    if (rw_die_colour(d) == pl->colour) out->colour += rw_die_value(d);
  }
  for (int g = 0; g < s->ngoals; g++) out->goal[g] = rw_goal_points(s->goals[g], pl->cell);
  out->favour = pl->favour;
  out->empty = -empty * (s->players == 1 ? 2 : 1);
  out->total = out->colour + out->favour + out->empty;
  for (int g = 0; g < s->ngoals; g++) out->total += out->goal[g];
}

unsigned rw_winners(const rw_state *s) {
  if (s->players == 1) {
    rw_breakdown b;
    rw_score(s, 0, &b);
    return b.total > s->track_sum ? 1u : 0u;
  }
  unsigned w = 0;
  int best = -9999, best_c = -1, best_f = -1;
  for (int i = 0; i < s->players; i++) {
    rw_breakdown b;
    rw_score(s, i, &b);
    bool better = b.total > best || (b.total == best && (b.colour > best_c || (b.colour == best_c && b.favour > best_f)));
    bool same = b.total == best && b.colour == best_c && b.favour == best_f;
    if (better) { best = b.total, best_c = b.colour, best_f = b.favour; w = 1u << i; }
    else if (same) w |= 1u << i;
  }
  return w;
}

int rw_dead_panes(const rw_state *s, int seat) {
  const rw_player *pl = &s->p[seat];
  if (pl->frame >= RW_FRAMES) return 0;
  const rw_frame *fr = &rw_frames[pl->frame];
  int dead = 0;
  for (int i = 0; i < RW_CELLS; i++) {
    if (pl->cell[i]) continue;
    int r = i / RW_COLS, c = i % RW_COLS;
    unsigned bad_c = 0, bad_v = 0;
    static const int DR[4] = { -1, 1, 0, 0 }, DC[4] = { 0, 0, -1, 1 };
    for (int k = 0; k < 4; k++) {
      int rr = r + DR[k], cc = c + DC[k];
      if (rr < 0 || rr >= RW_ROWS || cc < 0 || cc >= RW_COLS) continue;
      uint8_t d = pl->cell[rr * RW_COLS + cc];
      if (d) bad_c |= 1u << rw_die_colour(d), bad_v |= 1u << rw_die_value(d);
    }
    int mc = rw_mark_colour(fr->cell[i]), mv = rw_mark_value(fr->cell[i]);
    bool any = false;
    for (int col = 0; col < RW_COLOURS && !any; col++) {
      if ((mc >= 0 && col != mc) || (bad_c >> col & 1u)) continue;
      for (int v = 1; v <= 6 && !any; v++) {
        if ((mv && v != mv) || (bad_v >> v & 1u)) continue;
        any = true;
      }
    }
    dead += !any;
  }
  return dead;
}

void rw_census(const rw_state *s, int out[RW_COLOURS]) {
  for (int c = 0; c < RW_COLOURS; c++) out[c] = s->bag[c];
  for (int i = 0; i < s->pool_n; i++) if (s->pool[i]) out[rw_die_colour(s->pool[i])]++;
  for (int i = 0; i < s->track_n; i++) out[rw_die_colour(s->track[i])]++;
  for (int p = 0; p < s->players; p++)
    for (int i = 0; i < RW_CELLS; i++) if (s->p[p].cell[i]) out[rw_die_colour(s->p[p].cell[i])]++;
}

/* ── evaluation ────────────────────────────────────────────────────── */

/* a seat's expected final points from here: banked goal points, the colour goal, favour, the empty panes it can no
 * longer fill (dead or beyond the dice it still has turns for), and some credit for rows and columns on their way */
static float seat_value(const rw_state *s, int seat) {
  const rw_player *pl = &s->p[seat];
  if (pl->frame >= RW_FRAMES) return 0;
  rw_breakdown b;
  rw_score(s, seat, &b);
  float pen = s->players == 1 ? 2.0f : 1.0f;
  float v = (float)(b.total - b.empty);
  v -= 0.2f * (float)b.favour; /* favour is worth a little less than a point kept: it buys tools */
  int empty = RW_CELLS - pl->placed, dead = rw_dead_panes(s, seat);
  /* turns this seat still has: two a round after this one, and what is left of this round's snake */
  int turns = 2 * (RW_ROUNDS - s->round);
  if (s->phase == RW_PHASE_DRAFT)
    for (int k = s->turn_idx; k < 2 * s->players; k++)
      if (rw_seat_of(s, k) == seat && !(k >= s->players && pl->skip)) turns++;
  if (s->phase == RW_PHASE_SETUP) turns = 2 * RW_ROUNDS;
  int alive = empty - dead;
  int unreachable = alive > turns ? alive - turns : 0;
  v -= pen * (float)(dead + unreachable);
  /* each pane still open: a little risk, more the fewer dice it could take (its mark and its neighbours narrow it),
   * and some credit when it could still take a die of the seat's own colour (the colour goal grows with each) */
  {
    const rw_frame *fr = &rw_frames[pl->frame];
    float risk = 0, own = 0;
    int open = 0;
    for (int i = 0; i < RW_CELLS; i++) {
      if (pl->cell[i]) continue;
      int r = i / RW_COLS, c = i % RW_COLS;
      unsigned bad_c = 0, bad_v = 0;
      static const int DR[4] = { -1, 1, 0, 0 }, DC[4] = { 0, 0, -1, 1 };
      for (int k = 0; k < 4; k++) {
        int rr = r + DR[k], cc = c + DC[k];
        if (rr < 0 || rr >= RW_ROWS || cc < 0 || cc >= RW_COLS) continue;
        uint8_t d = pl->cell[rr * RW_COLS + cc];
        if (d) bad_c |= 1u << rw_die_colour(d), bad_v |= 1u << rw_die_value(d);
      }
      int mc = rw_mark_colour(fr->cell[i]), mv = rw_mark_value(fr->cell[i]), opts = 0, own_opts = 0;
      for (int col = 0; col < RW_COLOURS; col++) {
        if ((mc >= 0 && col != mc) || (bad_c >> col & 1u)) continue;
        for (int val = 1; val <= 6; val++) {
          if ((mv && val != mv) || (bad_v >> val & 1u)) continue;
          opts++;
          if (col == pl->colour) own_opts++;
        }
      }
      if (!opts) continue; /* dead: counted above */
      open++;
      risk += 1.0f - (float)opts / 30.0f;
      if (own_opts) own += (float)own_opts / (float)opts;
    }
    float reach = open > 0 && turns < open ? (float)turns / (float)open : 1.0f; /* only so many dice still to set */
    v -= pen * (0.12f * (float)(alive - unreachable) + 0.35f * risk * reach);
    v += 0.9f * own * reach;
  }
  /* sets on their way: a lone half of a pair, values or colours toward the next full set */
  {
    int vc[7] = { 0 }, cc[RW_COLOURS] = { 0 };
    for (int i = 0; i < RW_CELLS; i++)
      if (pl->cell[i]) vc[rw_die_value(pl->cell[i])]++, cc[rw_die_colour(pl->cell[i])]++;
    for (int g = 0; g < s->ngoals; g++) {
      int goal = s->goals[g], pts = rw_goal_infos[goal].points;
      if (goal == RW_G_LOW || goal == RW_G_MID || goal == RW_G_HIGH) {
        int a = goal == RW_G_LOW ? 1 : goal == RW_G_MID ? 3 : 5;
        int d = vc[a] - vc[a + 1];
        v += 0.3f * (float)pts * (float)(d < 0 ? -d : d);
      } else if (goal == RW_G_SCALE || goal == RW_G_PALETTE) {
        int n = goal == RW_G_SCALE ? 6 : RW_COLOURS, m = 99, have = 0;
        for (int k = 0; k < n; k++) { int x = goal == RW_G_SCALE ? vc[k + 1] : cc[k]; if (x < m) m = x; }
        for (int k = 0; k < n; k++) have += (goal == RW_G_SCALE ? vc[k + 1] : cc[k]) > m;
        v += 0.5f * (float)pts * (float)have / (float)n;
      }
    }
  }
  /* rows and columns still possible for the line goals */
  for (int g = 0; g < s->ngoals; g++) {
    int goal = s->goals[g];
    bool rows = goal == RW_G_ROW_COLOURS || goal == RW_G_ROW_VALUES, cols = goal == RW_G_COL_COLOURS || goal == RW_G_COL_VALUES;
    if (!rows && !cols) continue;
    bool colour = goal == RW_G_ROW_COLOURS || goal == RW_G_COL_COLOURS;
    int lines = rows ? RW_ROWS : RW_COLS, len = rows ? RW_COLS : RW_ROWS;
    for (int l = 0; l < lines; l++) {
      unsigned seen = 0;
      int k = 0;
      bool ok = true;
      for (int j = 0; j < len; j++) {
        uint8_t d = pl->cell[rows ? l * RW_COLS + j : j * RW_COLS + l];
        if (!d) continue;
        int key = colour ? rw_die_colour(d) : rw_die_value(d);
        if (seen >> key & 1u) ok = false;
        seen |= 1u << key;
        k++;
      }
      if (ok && k < len) v += 0.6f * (float)rw_goal_infos[goal].points * (float)(k * k) / (float)(len * len);
    }
  }
  return v;
}

void rw_evaluate(const rw_state *s, float score[BK_MAX_SEATS]) {
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  if (s->phase == RW_PHASE_OVER) {
    unsigned w = rw_winners(s);
    if (s->players == 1) {
      float margin = (float)(s->p[0].score - s->track_sum);
      score[0] = (w & 1u) ? BK_WIN + margin : BK_LOSS + margin;
      return;
    }
    for (int i = 0; i < s->players; i++) {
      int best_other = -1000;
      for (int j = 0; j < s->players; j++)
        if (j != i && s->p[j].score > best_other) best_other = s->p[j].score;
      float margin = (float)(s->p[i].score - best_other);
      score[i] = ((w >> i) & 1u) ? BK_WIN + margin : BK_LOSS + margin;
    }
    return;
  }
  float v[BK_MAX_SEATS];
  for (int i = 0; i < s->players; i++) v[i] = seat_value(s, i);
  /* each seat's value against the best of the rest: what wins is the margin, not the points alone */
  for (int i = 0; i < s->players; i++) {
    float best = -1e9f;
    for (int j = 0; j < s->players; j++) if (j != i && v[j] > best) best = v[j];
    score[i] = s->players > 1 ? v[i] - best : v[i];
  }
  if (s->players == 1) /* the target: the track so far and about three and a half a round to come */
    score[0] -= (float)s->track_sum + 3.5f * (float)(RW_ROUNDS - s->round);
}

uint64_t rw_hash(const rw_state *s) { return cw_hash_bytes(s, sizeof *s, 0); }

void rw_move_label(const rw_state *s, bk_move m, char *buf, int cap) {
  int kind = rw_mv_kind(m), a = rw_mv_a(m), b = rw_mv_b(m);
  switch (kind) {
  case RW_MV_PLACE: {
    uint8_t d = s->pool[a];
    (void)b;
    const char *cn = rw_colour_name[rw_die_colour(d)];
    snprintf(buf, (size_t)cap, "sets %s %s %d", cn[0] == 'a' ? "an" : "a", cn, rw_die_value(d));
    break;
  }
  case RW_MV_PASS: snprintf(buf, (size_t)cap, "rings the bell and passes"); break;
  case RW_MV_TOOL: snprintf(buf, (size_t)cap, "uses the %s", rw_tool_infos[s->tools[a]].name); break;
  case RW_MV_FRAME: snprintf(buf, (size_t)cap, "chooses the %s window", rw_frames[s->p[s->setup_seat].offer[a & 1]].name); break;
  case RW_MV_DIE: snprintf(buf, (size_t)cap, "a %s %d", rw_colour_name[b], rw_mv_c(m)); break;
  default: snprintf(buf, (size_t)cap, "?");
  }
}

/* ── bk_game glue ──────────────────────────────────────────────────── */

static int g_moves(const void *st, bk_move *out, int cap, void *u) { (void)u; return rw_moves(st, out, cap); }
static void g_apply(void *st, bk_move m, void *u) { (void)u; rw_apply(st, m); }
static int g_turn(const void *st, void *u) { (void)u; return rw_turn(st); }
static bool g_terminal(const void *st, void *u) { (void)u; return rw_terminal(st); }
static void g_evaluate(const void *st, float sc[BK_MAX_SEATS], void *u) { (void)u; rw_evaluate(st, sc); }
static uint64_t g_hash(const void *st, void *u) { (void)u; return rw_hash(st); }
static int g_outcomes(const void *st, bk_move *out, uint32_t *w, int cap, void *u) { (void)u; return rw_outcomes(st, out, w, cap); }

/* move ordering: dice of the seat's own colour and high faces first, tools after plain placing, passing last */
static float g_priority(const void *st, bk_move m, void *u) {
  (void)u;
  const rw_state *s = st;
  int kind = rw_mv_kind(m);
  if (kind == RW_MV_PASS) return -10;
  if (kind == RW_MV_PLACE) {
    uint8_t d = s->pool[rw_mv_a(m)];
    int seat = rw_seat_of(s, s->turn_idx);
    return 2.0f + (rw_die_colour(d) == s->p[seat].colour ? 0.4f * (float)rw_die_value(d) : 0);
  }
  return 0;
}

void rw_game(bk_game *g, int players) {
  memset(g, 0, sizeof *g);
  g->players = players;
  g->state_size = sizeof(rw_state);
  g->moves = g_moves;
  g->apply = g_apply;
  g->turn = g_turn;
  g->terminal = g_terminal;
  g->evaluate = g_evaluate;
  g->hash = g_hash;
  g->outcomes = g_outcomes;
  g->priority = g_priority;
}
