/* Mandible: the match (match.h). */
#include "match.h"

#include <math.h>
#include <string.h>

#include "boardkit/bk_cpu.h"
#include "boardkit/bk_match.h"
#include "cartwheel/core.h"

#define HEX_SIZE 0.5f     /* centre to corner, metres */
#define SQRT3 1.7320508f
#define OVER_WAIT 60      /* ticks before a finished game takes a confirm */
#define AUTO_REMATCH 300  /* the self-playing build starts the next game after this many ticks */

/* outside the hashed match: the kit's sequencer holding the rules state (mirrored in sw_match.st) and the CPU
 * seats (bk_cpu: search over ticks, then the pad's presses) */
static bk_match *g_bm;
static bk_cpu *g_cpu[2];
static sw_match *g_cpu_match; /* the match next_press reads */

void match_bind_actions(cw_action act[A_COUNT]) {
  static const char *const NAMES[A_COUNT] = { "left", "right", "up", "down", "confirm", "back", "next", "prev", "help", "point" };
  static const char *const BINDS[A_COUNT] = {
    "key:left, key:a, pad:left, axis:lx-@0.5",
    "key:right, key:d, pad:right, axis:lx+@0.5",
    "key:up, key:w, pad:up, axis:ly-@0.5",
    "key:down, key:s, pad:down, axis:ly+@0.5",
    "key:space, key:return, pad:south",
    "key:backspace, key:x, pad:east, mouse:right",
    "key:e, key:tab, pad:rshoulder",
    "key:q, pad:lshoulder",
    "key:h, key:f1, pad:north",
    "",
  };
  for (int a = 0; a < A_COUNT; a++) {
    act[a] = cw_action_add(NAMES[a], a == A_POINT ? 0.0f : 0.5f);
    if (BINDS[a][0]) cw_action_bind_text(act[a], BINDS[a]);
  }
}

/* ── layout on the table ──────────────────────────────────────────────── */

void match_hex_world(cw_axial h, float *x, float *z) {
  *x = HEX_SIZE * SQRT3 * ((float)h.q + 0.5f * (float)h.r);
  *z = HEX_SIZE * 1.5f * (float)h.r;
}

void match_hand_world(const sw_state *s, int side, int kind, float *x, float *z) {
  float minx = 0, maxx = 0, minz = 0, maxz = 0;
  bool any = false;
  for (int i = 0; i < SW_PIECES; i++) {
    if (!s->p[i].placed) continue;
    float px, pz;
    match_hex_world((cw_axial){ s->p[i].q, s->p[i].r }, &px, &pz);
    if (!any || px < minx) minx = px;
    if (!any || px > maxx) maxx = px;
    if (!any || pz < minz) minz = pz;
    if (!any || pz > maxz) maxz = pz;
    any = true;
  }
  /* trays beside the group, a little wider than it, rounded to half metres so they move in steps */
  float half = fmaxf(2.4f, (maxx - minx) * 0.5f + 1.7f);
  half = ceilf(half * 2) * 0.5f;
  float cx = floorf((minx + maxx) * 0.5f * 2 + 0.5f) * 0.5f, cz = floorf((minz + maxz) * 0.5f * 2 + 0.5f) * 0.5f;
  *x = side == 0 ? cx - half : cx + half;
  *z = cz + ((float)kind - 2.0f) * 1.2f;
}

/* ── seats ────────────────────────────────────────────────────────────── */

int match_seat_of_side(const sw_match *m, int side) {
  (void)m;
  return side;
}
bool match_cpu_turn(const sw_match *m) { return m->cpu[m->st.to_move] >= 0; }
static bool any_human(const sw_match *m) { return m->cpu[0] < 0 || m->cpu[1] < 0; }

float match_point_strength(int pick) { return (float)(pick + 1) / 512.0f; }
int match_point_pick(float strength) { return (int)floorf(strength * 512.0f + 0.5f) - 1; }

uint32_t match_hash(const sw_match *m) { return cw_hash32(m, sizeof *m, CW_HASH32_INIT); }

static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user);
static cw_action g_act[A_COUNT];

static void make_cpus(sw_match *m) {
  for (int side = 0; side < 2; side++) {
    bk_cpu_free(g_cpu[side]);
    g_cpu[side] = NULL;
    if (m->cpu[side] < 0) continue;
    cw_search_options o;
    mandible_level_options(m->cpu[side], &o);
    int lv = m->cpu[side] > 2 ? 2 : m->cpu[side];
    static const int THINK[3] = { 40, 45, 50 }, GAP[3] = { 7, 6, 5 };
    g_cpu[side] = bk_cpu_new(mandible_bk_game(), &(bk_cpu_desc){ .level = lv, .options = &o, .units_per_tick = 6000,
                                                               .think_ticks = m->auto_rematch ? 12 : THINK[lv],
                                                               .press_gap = m->auto_rematch ? 3 : GAP[lv],
                                                               .seed = m->seed + (uint64_t)side,
                                                               .next_press = cpu_next_press, .user = m });
  }
}
bool match_cpu_thinking(int side) { return side >= 0 && side < 2 && g_cpu[side] && bk_cpu_thinking(g_cpu[side]); }

void match_new_game(sw_match *m) {
  sw_init(&m->st);
  m->phase = PH_SELECT;
  m->help = any_human(m) && m->games == 0; /* the card shows once a match (help brings it back) */
  m->cursor = 0;
  m->src = -1;
  m->over_ticks = 0;
  m->has_last = 0;
  m->last_move = 0;
  m->last_mover = -1;
  bk_match_free(g_bm);
  g_bm = bk_match_new(mandible_bk_game(), &m->st, m->seed ^ (uint64_t)m->games);
}

void match_start(sw_match *m, int seats, const int32_t cpu[2], uint64_t seed, bool autoplay) {
  memset(m, 0, sizeof *m);
  m->seats = seats;
  m->cpu[0] = cpu[0];
  m->cpu[1] = cpu[1];
  m->seed = seed;
  m->auto_rematch = autoplay;
  match_new_game(m);
  make_cpus(m);
}

int match_move_type(const sw_state *before, sw_move mv) {
  int k = sw_move_kind(mv);
  if (k == SW_MOVE_PASS) return MT_PASS;
  if (k == SW_MOVE_PLACE) return MT_PLACE;
  if (k != SW_MOVE_STEP) CW_FATAL("mandible: move kind %d", k);
  return sw_top_at(before, sw_move_to(mv)) >= 0 ? MT_CLIMB : MT_STEP;
}

/* the first piece of that side and kind (ids are in kind order) */
static int piece_id(const sw_state *s, int side, int kind, int nth) {
  for (int id = side * SW_SIDE_PIECES; id < (side + 1) * SW_SIDE_PIECES; id++)
    if (s->p[id].kind == kind && nth-- == 0) return id;
  CW_FATAL("mandible: no piece %d of kind %d for side %d", nth, kind, side);
  return -1;
}

bool match_setup(sw_match *m, const char *name) {
  sw_state s;
  sw_init(&s);
  if (strcmp(name, "moves") == 0) {
    /* Ivory's Ram beside its Empress, Ebony's Empress beyond: the Ram can slide or climb onto the Empress, and the
     * hand can place */
    sw_put(&s, piece_id(&s, 0, SW_RAM, 0), (cw_axial){ -1, 0 });
    sw_put(&s, piece_id(&s, 0, SW_EMPRESS, 0), (cw_axial){ 0, 0 });
    sw_put(&s, piece_id(&s, 1, SW_EMPRESS, 0), (cw_axial){ 1, 0 });
    s.turns[0] = 2, s.turns[1] = 1;
  } else if (strcmp(name, "pass") == 0) {
    /* Ivory's one Ram, its Empress still in hand, ringed by six of Ebony's: no space touches Ivory alone and
     * nothing of Ivory's may move before its Empress is down */
    sw_put(&s, piece_id(&s, 0, SW_RAM, 0), (cw_axial){ 0, 0 });
    static const int RING[6][2] = { { SW_EMPRESS, 0 }, { SW_RAM, 0 }, { SW_RAM, 1 }, { SW_LOCUST, 0 }, { SW_LOCUST, 1 }, { SW_WEAVER, 0 } };
    for (int d = 0; d < 6; d++) sw_put(&s, piece_id(&s, 1, RING[d][0], RING[d][1]), cw_hex_neighbor((cw_axial){ 0, 0 }, d));
    s.turns[0] = 1, s.turns[1] = 2;
  } else {
    return false;
  }
  s.hist[0] = (uint32_t)s.key;
  m->st = s;
  m->phase = PH_SELECT;
  m->help = 0;
  m->cursor = 0;
  m->src = -1;
  m->over_ticks = 0;
  m->has_last = 0;
  m->last_move = 0;
  m->last_mover = -1;
  bk_match_free(g_bm);
  g_bm = bk_match_new(mandible_bk_game(), &m->st, m->seed);
  return true;
}

/* ── picks ────────────────────────────────────────────────────────────── */

static int move_source(const sw_state *s, sw_move mv) {
  int k = sw_move_kind(mv);
  if (k == SW_MOVE_PASS) return SRC_PASS;
  if (k == SW_MOVE_PLACE) return SRC_HAND + s->p[sw_move_id(mv)].kind;
  return sw_move_id(mv);
}

int match_picks(const sw_match *m, sw_pick *out, int cap) {
  const sw_state *s = &m->st;
  if (m->phase != PH_SELECT && m->phase != PH_TARGET) return 0;
  sw_move mv[SW_MAX_MOVES];
  int n = sw_moves(s, mv, SW_MAX_MOVES), c = 0;
  if (m->phase == PH_SELECT) {
    /* hand kinds first (in kind order), then pieces on the table (by id), then a pass */
    for (int pass = 0; pass < 3; pass++)
      for (int i = 0; i < n; i++) {
        int src = move_source(s, mv[i]);
        bool want = pass == 0 ? (src >= SRC_HAND && src < SRC_PASS) : pass == 1 ? src < SRC_HAND : src == SRC_PASS;
        if (!want) continue;
        bool seen = false;
        for (int j = 0; j < c; j++) seen = seen || out[j].src == src;
        if (seen) continue;
        if (c >= cap) CW_FATAL("mandible: more picks than %d", cap);
        sw_pick p = { .src = src, .move = mv[i] };
        if (src == SRC_PASS) {
          p.hex = (cw_axial){ 0, 0 };
          match_hex_world(p.hex, &p.x, &p.z);
        } else if (src >= SRC_HAND) {
          match_hand_world(s, s->to_move, src - SRC_HAND, &p.x, &p.z);
        } else {
          p.hex = (cw_axial){ s->p[src].q, s->p[src].r };
          match_hex_world(p.hex, &p.x, &p.z);
        }
        out[c++] = p;
      }
    /* order the hand picks along the tray and the table picks by place, so "next" walks the table in reading order */
    return c;
  }
  for (int i = 0; i < n; i++) {
    if (move_source(s, mv[i]) != m->src) continue;
    if (c >= cap) CW_FATAL("mandible: more picks than %d", cap);
    sw_pick p = { .src = m->src, .hex = sw_move_to(mv[i]), .move = mv[i] };
    match_hex_world(p.hex, &p.x, &p.z);
    out[c++] = p;
  }
  return c;
}

/* the nearest pick in a direction on the table (x right, z toward the viewer); -1 none */
static int nav(const sw_pick *p, int n, int from, float dx, float dz) {
  int best = -1;
  float bs = 1e30f;
  for (int i = 0; i < n; i++) {
    if (i == from) continue;
    float vx = p[i].x - p[from].x, vz = p[i].z - p[from].z;
    float along = vx * dx + vz * dz, across = fabsf(vx * dz - vz * dx);
    if (along < 0.2f) continue;
    float sc = along + 2.2f * across;
    if (sc < bs) bs = sc, best = i;
  }
  return best;
}

/* ── the turn ─────────────────────────────────────────────────────────── */

static void play_move(sw_match *m, sw_move mv, sw_match_events *ev) {
  if (!sw_is_legal(&m->st, mv)) CW_FATAL("mandible: an illegal move reached the table");
  int side = m->st.to_move;
  if (ev) {
    ev->moved = true;
    ev->move = mv;
    ev->before = m->st;
  }
  if (!bk_match_play(g_bm, mv)) CW_FATAL("mandible: the match refused a legal move");
  memcpy(&m->st, bk_match_state(g_bm), sizeof m->st);
  if (m->cpu[side] >= 0) m->cpu_moves++;
  else m->human_moves++;
  m->has_last = 1;
  m->last_move = mv;
  m->last_mover = side;
  m->phase = PH_SELECT;
  m->cursor = 0;
  m->src = -1;
  if (sw_terminal(&m->st)) {
    m->phase = PH_OVER;
    m->over_ticks = 0;
    m->games++;
    m->wins[m->st.result == SW_WHITE_WINS ? 0 : m->st.result == SW_BLACK_WINS ? 1 : 2]++;
    if (ev) ev->game_over = true;
  }
}

/* a confirm on pick i of the current phase */
static void take(sw_match *m, const sw_pick *p, int n, int i, sw_match_events *ev) {
  if (i < 0 || i >= n) return;
  if (m->phase == PH_SELECT) {
    if (p[i].src == SRC_PASS) {
      play_move(m, p[i].move, ev);
      return;
    }
    m->src = p[i].src;
    m->phase = PH_TARGET;
    m->cursor = 0;
    return;
  }
  play_move(m, p[i].move, ev);
}

/* bk_cpu's next press toward the move it wants: step the cursor through the picks (the shorter way round),
 * confirm when it stands on the right one; in SELECT the right pick is the move's source */
static bool cpu_next_press(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state, (void)seat;
  sw_match *m = user;
  if (m->phase != PH_SELECT && m->phase != PH_TARGET) return false;
  sw_pick p[MAX_PICKS];
  int n = match_picks(m, p, MAX_PICKS), want = -1;
  for (int i = 0; i < n; i++) {
    bool hit = m->phase == PH_SELECT ? p[i].src == move_source(&m->st, move) : p[i].move == move;
    if (hit) want = i;
  }
  if (want < 0) CW_FATAL("mandible: the CPU's move is not among the picks");
  memset(out, 0, sizeof *out);
  out->count = 1;
  out->hold = 1;
  if (want == m->cursor) out->v[0] = (cw_action_value){ g_act[A_CONFIRM], 1.0f };
  else {
    int fwd = (want - m->cursor + n) % n;
    out->v[0] = (cw_action_value){ g_act[fwd <= n - fwd ? A_NEXT : A_PREV], 1.0f };
  }
  return true;
}

void match_tick(sw_match *m, const cw_action act[A_COUNT], sw_match_events *ev) {
  memset(ev, 0, sizeof *ev);
  memcpy(g_act, act, sizeof g_act);
  /* the CPUs press first (every tick, released off their turn) */
  for (int side = 0; side < 2; side++) {
    if (m->cpu[side] < 0 || !g_cpu[side]) continue;
    bool live = m->phase == PH_SELECT || m->phase == PH_TARGET;
    cw_action_value v[4];
    int nv = live ? bk_cpu_tick(g_cpu[side], g_bm, side, v, 4) : 0;
    cw_input_set_actions(match_seat_of_side(m, side), v, nv);
  }
  if (m->help) {
    bool closed = false;
    for (int side = 0; side < 2 && !closed; side++) {
      if (m->cpu[side] >= 0) continue;
      int pl = match_seat_of_side(m, side);
      if (cw_action_just_pressed(pl, act[A_CONFIRM]) || cw_action_just_pressed(pl, act[A_BACK]) ||
          cw_action_just_pressed(pl, act[A_HELP]) || cw_action_just_pressed(pl, act[A_POINT]))
        closed = true;
    }
    if (closed) {
      m->help = 0;
      return;
    }
  }
  if (m->phase == PH_OVER) {
    m->over_ticks++;
    bool again = m->auto_rematch && m->over_ticks >= AUTO_REMATCH;
    for (int side = 0; side < 2 && m->over_ticks >= OVER_WAIT; side++)
      if (m->cpu[side] < 0 && cw_action_just_pressed(match_seat_of_side(m, side), act[A_CONFIRM])) again = true;
    if (again) {
      match_new_game(m);
      if (ev) ev->started = true;
    }
    return;
  }
  int side = m->st.to_move, pl = match_seat_of_side(m, side);
  if (m->help && m->cpu[side] < 0) return; /* a person reads the card; a CPU's turn goes on under it */
  /* presses from the seat whose turn it is not (a hot seat must hear nothing: the routing gate holds this at 0) */
  int other = side ^ 1;
  if (m->cpu[other] < 0 && m->cpu[side] < 0) /* two people: the one waiting must not be heard */
    for (int a = 0; a < A_COUNT; a++)
      if (a != A_HELP && cw_action_just_pressed(match_seat_of_side(m, other), act[a])) m->stray++;
  if (m->cpu[side] < 0 && cw_action_just_pressed(pl, act[A_HELP])) {
    m->help = 1;
    return;
  }
  sw_pick p[MAX_PICKS];
  int n = match_picks(m, p, MAX_PICKS);
  if (n == 0) CW_FATAL("mandible: a live turn with nothing to pick");
  if (m->cursor >= n) m->cursor = 0;
  if (cw_action_just_pressed(pl, act[A_POINT])) {
    int i = match_point_pick(cw_action_strength(pl, act[A_POINT]));
    if (i >= 0 && i < n) {
      m->cursor = i;
      take(m, p, n, i, ev);
    }
    return;
  }
  if (cw_action_just_pressed(pl, act[A_NEXT])) m->cursor = (m->cursor + 1) % n;
  if (cw_action_just_pressed(pl, act[A_PREV])) m->cursor = (m->cursor + n - 1) % n;
  static const float DIR[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
  for (int a = A_LEFT; a <= A_DOWN; a++)
    if (cw_action_just_pressed(pl, act[a])) {
      int j = nav(p, n, m->cursor, DIR[a][0], DIR[a][1]);
      if (j >= 0) m->cursor = j;
    }
  if (cw_action_just_pressed(pl, act[A_BACK]) && m->phase == PH_TARGET) {
    /* back to choosing, the cursor on the piece that was picked */
    int was = m->src;
    m->phase = PH_SELECT;
    m->src = -1;
    int k = match_picks(m, p, MAX_PICKS);
    m->cursor = 0;
    for (int i = 0; i < k; i++)
      if (p[i].src == was) m->cursor = i;
    return;
  }
  if (cw_action_just_pressed(pl, act[A_CONFIRM])) take(m, p, n, m->cursor, ev);
}
