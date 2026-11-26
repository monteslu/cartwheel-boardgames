/* Mandible: the rules. See mandible_rules.h. */
#include "rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int KIND_COUNT[SW_KINDS] = { 1, 2, 3, 2, 3 };

static void sw_fail(const char *what) { CW_FATAL("mandible rules: %s", what); }

/* ── hashing ─────────────────────────────────────────────────────────── */

static uint64_t mix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}
#define KEY_SIDE 0x5bd1e9955bd1e995ull
static uint64_t piece_key(const sw_piece *p) {
  uint64_t v = (uint64_t)p->kind | (uint64_t)p->side << 4 | (uint64_t)(uint8_t)p->h << 8 |
               (uint64_t)(uint16_t)p->q << 16 | (uint64_t)(uint16_t)p->r << 32;
  return mix64(v);
}

/* ── the grid ────────────────────────────────────────────────────────── */

int sw_top_at(const sw_state *s, cw_axial h) { return s->top[sw_cell(h)] - 1; }
int sw_height_at(const sw_state *s, cw_axial h) {
  int t = s->top[sw_cell(h)];
  return t ? s->p[t - 1].h + 1 : 0;
}
static inline bool occ(const sw_state *s, cw_axial h) { return s->top[sw_cell(h)] != 0; }

static void lift(sw_state *s, int id) {
  sw_piece *p = &s->p[id];
  s->key ^= piece_key(p);
  s->top[sw_cell((cw_axial){ p->q, p->r })] = (int8_t)(p->below + 1);
  p->placed = 0;
}
void sw_put(sw_state *s, int id, cw_axial at) {
  if (at.q < -2000 || at.q > 2000 || at.r < -2000 || at.r > 2000) sw_fail("a piece left the coordinate range");
  sw_piece *p = &s->p[id];
  int c = sw_cell(at);
  p->q = (int16_t)at.q;
  p->r = (int16_t)at.r;
  p->below = (int8_t)(s->top[c] - 1);
  p->h = (int8_t)(p->below >= 0 ? s->p[p->below].h + 1 : 0);
  p->placed = 1;
  s->top[c] = (int8_t)(id + 1);
  s->key ^= piece_key(p);
}

void sw_init(sw_state *s) {
  memset(s, 0, sizeof *s);
  for (int side = 0; side < 2; side++) {
    int id = side * SW_SIDE_PIECES;
    for (int k = 0; k < SW_KINDS; k++)
      for (int i = 0; i < KIND_COUNT[k]; i++, id++) {
        s->p[id].kind = (uint8_t)k;
        s->p[id].side = (uint8_t)side;
        s->p[id].below = -1;
      }
  }
  s->key = mix64(0x6d616e6469626c65ull);
  s->hist[0] = (uint32_t)s->key;
  s->reps = 1;
}

/* how many times the position after this ply has come up, this time included (same side to move: every other
 * ply) */
static int repetitions(const sw_state *s) {
  int n = 1;
  uint32_t k = (uint32_t)s->key;
  for (int i = s->ply - 2; i >= 0; i -= 2) n += s->hist[i] == k;
  return n;
}

int sw_in_hand(const sw_state *s, int side, int kind) {
  int n = 0;
  for (int i = side * SW_SIDE_PIECES; i < (side + 1) * SW_SIDE_PIECES; i++)
    if (s->p[i].kind == kind && !s->p[i].placed) n++;
  return n;
}
static int first_in_hand(const sw_state *s, int side, int kind) {
  for (int i = side * SW_SIDE_PIECES; i < (side + 1) * SW_SIDE_PIECES; i++)
    if (s->p[i].kind == kind && !s->p[i].placed) return i;
  return -1;
}
static int empress_id(int side) { return side * SW_SIDE_PIECES; }
bool sw_empress_placed(const sw_state *s, int side) { return s->p[empress_id(side)].placed; }
static int pieces_on_table(const sw_state *s) {
  int n = 0;
  for (int i = 0; i < SW_PIECES; i++) n += s->p[i].placed;
  return n;
}

int sw_empress_pressure(const sw_state *s, int side) {
  const sw_piece *e = &s->p[empress_id(side)];
  if (!e->placed) return 0;
  int n = 0;
  for (int d = 0; d < 6; d++) n += occ(s, cw_hex_neighbor((cw_axial){ e->q, e->r }, d));
  return n;
}

/* ── the one-group rule: articulation hexes of the table ───────────────── */

typedef struct {
  cw_axial hex[SW_PIECES];
  int n;
  int8_t idx[SW_CELLS]; /* node + 1 */
  int disc[SW_PIECES], low[SW_PIECES], timer;
  uint8_t cut[SW_PIECES];
} cut_ctx;

static void cut_dfs(const sw_state *s, cut_ctx *c, int u, int parent) {
  c->disc[u] = c->low[u] = ++c->timer;
  int children = 0;
  for (int d = 0; d < 6; d++) {
    cw_axial nh = cw_hex_neighbor(c->hex[u], d);
    int v = c->idx[sw_cell(nh)] - 1;
    if (v < 0) continue;
    if (!c->disc[v]) {
      children++;
      cut_dfs(s, c, v, u);
      if (c->low[v] < c->low[u]) c->low[u] = c->low[v];
      if (parent >= 0 && c->low[v] >= c->disc[u]) c->cut[u] = 1;
    } else if (v != parent && c->disc[v] < c->low[u]) {
      c->low[u] = c->disc[v];
    }
  }
  if (parent < 0 && children > 1) c->cut[u] = 1;
}

void sw_pinned(const sw_state *s, uint8_t out[SW_PIECES]) {
  memset(out, 0, SW_PIECES);
  cut_ctx c;
  memset(&c, 0, sizeof c);
  for (int i = 0; i < SW_PIECES; i++) {
    const sw_piece *p = &s->p[i];
    if (!p->placed || p->h != 0) continue;
    cw_axial h = { p->q, p->r };
    c.hex[c.n] = h;
    c.idx[sw_cell(h)] = (int8_t)(c.n + 1);
    c.n++;
  }
  if (c.n == 0) return;
  cut_dfs(s, &c, 0, -1);
  for (int u = 0; u < c.n; u++) {
    if (!c.disc[u]) sw_fail("the table split into two groups");
    if (!c.cut[u]) continue;
    int id = s->top[sw_cell(c.hex[u])] - 1;
    if (id >= 0 && s->p[id].h == 0) out[id] = 1;
  }
}

/* ── movement ────────────────────────────────────────────────────────── */

/* A ground slide from `from` one step in direction d: the target is empty, and exactly one of the two hexes
 * beside the gap is occupied (both: too tight; neither: the piece would lose touch with the group). The mover has
 * been lifted already. */
static bool can_slide(const sw_state *s, cw_axial from, int d) {
  cw_axial to = cw_hex_neighbor(from, d);
  if (occ(s, to)) return false;
  bool a = occ(s, cw_hex_neighbor(from, d - 1)), b = occ(s, cw_hex_neighbor(from, d + 1));
  return a != b;
}

/* A Ram step (lifted already): heights decide the gap. */
static bool ram_can_step(const sw_state *s, cw_axial from, int d) {
  cw_axial to = cw_hex_neighbor(from, d);
  int hf = sw_height_at(s, from), ht = sw_height_at(s, to);
  int ha = sw_height_at(s, cw_hex_neighbor(from, d - 1)), hb = sw_height_at(s, cw_hex_neighbor(from, d + 1));
  int lo = ha < hb ? ha : hb, hi = hf > ht ? hf : ht;
  if (lo > hi) return false; /* squeezed between two taller stacks */
  if (hf == 0 && ht == 0) return ha > 0 || hb > 0; /* on the ground: keep touching the group (both is caught above) */
  return true;
}

typedef struct {
  sw_move *out;
  int n, cap;
} move_list;
static void push(move_list *l, sw_move m) {
  if (l->n >= l->cap) sw_fail("move list overflow (raise the cap)");
  l->out[l->n++] = m;
}

/* destinations into a small set, deduplicated, kept in discovery order */
typedef struct {
  cw_axial h[64];
  int n;
} hex_set;
static bool set_has(const hex_set *set, cw_axial h) {
  for (int i = 0; i < set->n; i++)
    if (set->h[i].q == h.q && set->h[i].r == h.r) return true;
  return false;
}
static void set_add(hex_set *set, cw_axial h) {
  if (set_has(set, h)) return;
  if (set->n >= 64) sw_fail("destination set overflow");
  set->h[set->n++] = h;
}

static void weaver_walk(const sw_state *s, cw_axial at, cw_axial path[4], int depth, hex_set *dest) {
  if (depth == 3) {
    set_add(dest, at);
    return;
  }
  for (int d = 0; d < 6; d++) {
    if (!can_slide(s, at, d)) continue;
    cw_axial to = cw_hex_neighbor(at, d);
    bool used = false;
    for (int i = 0; i <= depth; i++)
      if (path[i].q == to.q && path[i].r == to.r) used = true;
    if (used) continue;
    path[depth + 1] = to;
    weaver_walk(s, to, path, depth + 1, dest);
  }
}

static void piece_moves(sw_state *s, int id, move_list *l) {
  sw_piece *p = &s->p[id];
  cw_axial from = { p->q, p->r };
  hex_set dest = { .n = 0 };
  lift(s, id);
  switch (p->kind) {
  case SW_EMPRESS:
    for (int d = 0; d < 6; d++)
      if (can_slide(s, from, d)) set_add(&dest, cw_hex_neighbor(from, d));
    break;
  case SW_RAM:
    for (int d = 0; d < 6; d++)
      if (ram_can_step(s, from, d)) set_add(&dest, cw_hex_neighbor(from, d));
    break;
  case SW_LOCUST:
    for (int d = 0; d < 6; d++) {
      cw_axial at = cw_hex_neighbor(from, d);
      if (!occ(s, at)) continue;
      while (occ(s, at)) at = cw_hex_neighbor(at, d);
      set_add(&dest, at);
    }
    break;
  case SW_WEAVER: {
    cw_axial path[4] = { from };
    weaver_walk(s, from, path, 0, &dest);
    break;
  }
  case SW_RUNNER: {
    hex_set seen = { .n = 0 };
    set_add(&seen, from);
    for (int i = 0; i < seen.n; i++)
      for (int d = 0; d < 6; d++)
        if (can_slide(s, seen.h[i], d)) set_add(&seen, cw_hex_neighbor(seen.h[i], d));
    for (int i = 1; i < seen.n; i++) set_add(&dest, seen.h[i]);
    break;
  }
  }
  sw_put(s, id, from);
  for (int i = 0; i < dest.n; i++)
    if (!(dest.h[i].q == from.q && dest.h[i].r == from.r)) push(l, sw_move_make(SW_MOVE_STEP, id, from, dest.h[i]));
}

/* hexes where `side` may place a new piece */
static int place_cells(const sw_state *s, int side, cw_axial *out, int cap) {
  int on = pieces_on_table(s), n = 0;
  if (on == 0) {
    out[n++] = (cw_axial){ 0, 0 };
    return n;
  }
  if (on == 1) {
    for (int i = 0; i < SW_PIECES; i++)
      if (s->p[i].placed)
        for (int d = 0; d < 6; d++) out[n++] = cw_hex_neighbor((cw_axial){ s->p[i].q, s->p[i].r }, d);
    return n;
  }
  uint8_t seen[SW_CELLS];
  memset(seen, 0, sizeof seen);
  for (int i = side * SW_SIDE_PIECES; i < (side + 1) * SW_SIDE_PIECES; i++) {
    const sw_piece *p = &s->p[i];
    if (!p->placed || s->top[sw_cell((cw_axial){ p->q, p->r })] != i + 1) continue;
    for (int d = 0; d < 6; d++) {
      cw_axial c = cw_hex_neighbor((cw_axial){ p->q, p->r }, d);
      int ci = sw_cell(c);
      if (seen[ci] || occ(s, c)) continue;
      seen[ci] = 1;
      bool ok = true;
      for (int e = 0; e < 6 && ok; e++) {
        int t = sw_top_at(s, cw_hex_neighbor(c, e));
        if (t >= 0 && s->p[t].side != side) ok = false;
      }
      if (!ok) continue;
      if (n >= cap) sw_fail("placement list overflow");
      out[n++] = c;
    }
  }
  return n;
}

int sw_moves(const sw_state *cs, sw_move *out, int cap) {
  if (cs->result != SW_ONGOING) return 0;
  sw_state *s = (sw_state *)cs; /* moves() lifts and drops pieces but leaves the state as it found it */
  uint64_t key0 = s->key;
  move_list l = { out, 0, cap };
  int side = s->to_move;
  cw_axial cells[64];
  int nc = place_cells(s, side, cells, 64);
  bool must_empress = s->turns[side] == 3 && !sw_empress_placed(s, side);
  for (int k = 0; k < SW_KINDS; k++) {
    if (must_empress && k != SW_EMPRESS) continue;
    int id = first_in_hand(s, side, k);
    if (id < 0) continue;
    for (int i = 0; i < nc; i++) push(&l, sw_move_make(SW_MOVE_PLACE, id, cells[i], cells[i]));
  }
  if (sw_empress_placed(s, side) && !must_empress) {
    uint8_t pinned[SW_PIECES];
    sw_pinned(s, pinned);
    for (int id = side * SW_SIDE_PIECES; id < (side + 1) * SW_SIDE_PIECES; id++) {
      const sw_piece *p = &s->p[id];
      if (!p->placed || pinned[id]) continue;
      if (s->top[sw_cell((cw_axial){ p->q, p->r })] != id + 1) continue; /* covered */
      piece_moves(s, id, &l);
    }
  }
  if (s->key != key0) sw_fail("move generation changed the state");
  if (l.n == 0) push(&l, sw_move_make(SW_MOVE_PASS, 0, (cw_axial){ 0, 0 }, (cw_axial){ 0, 0 }));
  return l.n;
}

static bool surrounded(const sw_state *s, int side) {
  return sw_empress_placed(s, side) && sw_empress_pressure(s, side) == 6;
}

void sw_apply(sw_state *s, sw_move m) {
  if (s->result != SW_ONGOING) sw_fail("apply on a finished game");
  int kind = sw_move_kind(m), id = sw_move_id(m);
  if (kind == SW_MOVE_PLACE) {
    sw_put(s, id, sw_move_to(m));
  } else if (kind == SW_MOVE_STEP) {
    lift(s, id);
    sw_put(s, id, sw_move_to(m));
  } else if (kind != SW_MOVE_PASS) {
    sw_fail("unknown move kind");
  }
  s->turns[s->to_move]++;
  s->to_move ^= 1;
  s->key ^= KEY_SIDE;
  s->ply++;
  s->hist[s->ply] = (uint32_t)s->key;
  s->reps = kind == SW_MOVE_PLACE ? 1 : (uint8_t)repetitions(s); /* a placement never repeats a position */
  bool w = surrounded(s, 0), b = surrounded(s, 1);
  if (w && b) s->result = SW_DRAW;
  else if (w) s->result = SW_BLACK_WINS;
  else if (b) s->result = SW_WHITE_WINS;
  else if (s->reps >= 3 || s->ply >= SW_MAX_PLY) s->result = SW_DRAW;
}

void sw_undo(sw_state *s, sw_move m) {
  int kind = sw_move_kind(m), id = sw_move_id(m);
  s->result = SW_ONGOING;
  s->hist[s->ply] = 0;
  s->ply--;
  s->key ^= KEY_SIDE;
  s->to_move ^= 1;
  s->turns[s->to_move]--;
  if (kind == SW_MOVE_PLACE) {
    lift(s, id);
    s->p[id].below = -1;
    s->p[id].h = 0;
    s->p[id].q = s->p[id].r = 0;
  } else if (kind == SW_MOVE_STEP) {
    lift(s, id);
    sw_put(s, id, sw_move_from(m));
  }
  s->reps = (uint8_t)repetitions(s);
}

bool sw_is_legal(const sw_state *s, sw_move m) {
  sw_move list[SW_MAX_MOVES];
  int n = sw_moves(s, list, SW_MAX_MOVES);
  for (int i = 0; i < n; i++)
    if (list[i] == m) return true;
  return false;
}

/* ── evaluation ──────────────────────────────────────────────────────── */

/* how bad k occupied hexes around your own Empress are (grows fast near six) */
static const float PRESSURE[7] = { 0, 6, 16, 34, 64, 130, 0 };
/* what a piece that is free to move is worth */
static const float FREE_VALUE[SW_KINDS] = { 3, 7, 4, 4, 9 };
/* what a piece still in hand is worth (it can still come in where needed) */
static const float HAND_VALUE[SW_KINDS] = { 0, 4, 3, 3, 6 };

static bool can_move_at_all(sw_state *s, int id) {
  const sw_piece *p = &s->p[id];
  cw_axial from = { p->q, p->r };
  if (p->kind == SW_RAM || p->kind == SW_LOCUST) return true; /* free and on top: always has somewhere */
  bool any = false;
  lift(s, id);
  for (int d = 0; d < 6 && !any; d++) any = can_slide(s, from, d);
  sw_put(s, id, from);
  return any;
}

void sw_evaluate(const sw_state *cs, float score[2]) {
  if (cs->result != SW_ONGOING) {
    float w = SW_WIN - (float)cs->ply;
    if (cs->result == SW_DRAW) score[0] = score[1] = 0;
    else if (cs->result == SW_WHITE_WINS) { score[0] = w; score[1] = -w; }
    else { score[0] = -w; score[1] = w; }
    return;
  }
  sw_state *s = (sw_state *)cs;
  uint8_t pinned[SW_PIECES];
  sw_pinned(s, pinned);
  float v[2] = { 0, 0 };
  for (int side = 0; side < 2; side++) {
    int other = side ^ 1;
    v[side] -= PRESSURE[sw_empress_pressure(s, side)];
    /* an Empress that cannot move at all is in more trouble */
    const sw_piece *e = &s->p[empress_id(side)];
    if (e->placed && (pinned[empress_id(side)] || s->top[sw_cell((cw_axial){ e->q, e->r })] != empress_id(side) + 1 ||
                      !can_move_at_all(s, empress_id(side))))
      v[side] -= 4 + 2.0f * (float)sw_empress_pressure(s, side);
    for (int id = side * SW_SIDE_PIECES; id < (side + 1) * SW_SIDE_PIECES; id++) {
      const sw_piece *p = &s->p[id];
      if (!p->placed) {
        v[side] += HAND_VALUE[p->kind] * (sw_empress_placed(s, side) ? 1.0f : 0.6f);
        continue;
      }
      if (s->top[sw_cell((cw_axial){ p->q, p->r })] != id + 1) continue; /* covered */
      if (pinned[id]) continue;
      if (!sw_empress_placed(s, side)) continue;
      if (!can_move_at_all(s, id)) continue;
      v[side] += FREE_VALUE[p->kind];
      /* a Ram sitting on or next to the other Empress pins her and adds pressure */
      if (p->kind == SW_RAM && sw_empress_placed(s, other)) {
        const sw_piece *oe = &s->p[empress_id(other)];
        int dq = p->q - oe->q, dr = p->r - oe->r;
        int dist = (abs(dq) + abs(dr) + abs(dq + dr)) / 2;
        if (dist == 0) v[side] += 14;
        else if (dist == 1) v[side] += 4;
      }
    }
  }
  /* the side to move gets a small tempo bonus */
  v[s->to_move] += 1.5f;
  score[0] = v[0] - v[1];
  score[1] = v[1] - v[0];
}

/* ── names ───────────────────────────────────────────────────────────── */

static const char *KIND_NAMES[SW_KINDS] = { "Empress", "Ram", "Locust", "Weaver", "Runner" };
const char *sw_kind_name(int kind) { return kind >= 0 && kind < SW_KINDS ? KIND_NAMES[kind] : "?"; }

void sw_move_label(const sw_state *s, sw_move m, char *buf, size_t cap) {
  int kind = sw_move_kind(m), id = sw_move_id(m);
  cw_axial to = sw_move_to(m), from = sw_move_from(m);
  const char *name = sw_kind_name(s->p[id].kind);
  if (kind == SW_MOVE_PASS) snprintf(buf, cap, "pass");
  else if (kind == SW_MOVE_PLACE) snprintf(buf, cap, "place %s at %d,%d", name, to.q, to.r);
  else snprintf(buf, cap, "%s %d,%d to %d,%d", name, from.q, from.r, to.q, to.r);
}

/* ── move ordering for the search ─────────────────────────────────────── */

float sw_priority(const sw_state *s, sw_move m) {
  int kind = sw_move_kind(m);
  if (kind == SW_MOVE_PASS) return 0;
  int id = sw_move_id(m), side = s->p[id].side, other = side ^ 1;
  cw_axial to = sw_move_to(m), from = sw_move_from(m);
  float v = 0;
  const sw_piece *oe = &s->p[other * SW_SIDE_PIECES], *me = &s->p[side * SW_SIDE_PIECES];
  if (oe->placed) {
    cw_axial oh = { oe->q, oe->r };
    int dt = cw_hex_distance(to, oh);
    if (dt == 0) v += 30;
    else if (dt == 1) v += 20;
    if (kind == SW_MOVE_STEP && cw_hex_distance(from, oh) == 1) v -= 15; /* leaving the siege */
  }
  if (me->placed && kind == SW_MOVE_STEP && id != side * SW_SIDE_PIECES) {
    cw_axial mh = { me->q, me->r };
    if (cw_hex_distance(from, mh) == 1 && cw_hex_distance(to, mh) > 1) v += 8; /* unblock your own Empress */
  }
  if (kind == SW_MOVE_PLACE && s->p[id].kind == SW_EMPRESS) v += 5;
  if (s->p[id].kind == SW_RUNNER) v += 1;
  return v;
}

/* ── the kit's rules interface (boardkit/bk_game.h) ───────────────────── */

static int bk_moves_cb(const void *st, bk_move *out, int cap, void *u) {
  (void)u;
  return sw_moves((const sw_state *)st, out, cap);
}
static void bk_apply_cb(void *st, bk_move m, void *u) {
  (void)u;
  sw_apply((sw_state *)st, m);
}
static void bk_undo_cb(void *st, bk_move m, void *u) {
  (void)u;
  sw_undo((sw_state *)st, m);
}
static int bk_turn_cb(const void *st, void *u) {
  (void)u;
  return ((const sw_state *)st)->to_move;
}
static bool bk_terminal_cb(const void *st, void *u) {
  (void)u;
  return sw_terminal((const sw_state *)st);
}
static void bk_evaluate_cb(const void *st, float score[BK_MAX_SEATS], void *u) {
  (void)u;
  float sc[2];
  sw_evaluate((const sw_state *)st, sc);
  score[0] = sc[0];
  score[1] = sc[1];
}
static uint64_t bk_hash_cb(const void *st, void *u) {
  (void)u;
  return ((const sw_state *)st)->key;
}
static float bk_priority_cb(const void *st, bk_move m, void *u) {
  (void)u;
  return sw_priority((const sw_state *)st, m);
}

const bk_game *mandible_bk_game(void) {
  static const bk_game g = {
    .players = 2,
    .state_size = sizeof(sw_state),
    .moves = bk_moves_cb,
    .apply = bk_apply_cb,
    .undo = bk_undo_cb,
    .turn = bk_turn_cb,
    .terminal = bk_terminal_cb,
    .evaluate = bk_evaluate_cb,
    .hash = bk_hash_cb,
    .priority = bk_priority_cb,
  };
  return &g;
}

void mandible_level_options(int level, cw_search_options *o) {
  cw_search_level_options(level, CW_SEARCH_MINIMAX, o);
  /* measured (tests/test_rules.c, the level ladder, 40 games a pair): depth 1 / 3 / 5 separates the levels clearly;
   * with the repetition rule Hard beats Normal 35 to 2 with 3 draws (depth 4 without it: 23 to 2, 15 draws at the
   * ply cap). A Hard search averages about 140k nodes, spread over the ticks by bk_cpu */
  static const int depth[3] = { 1, 3, 5 };
  static const float mistake[3] = { 0.30f, 0.15f, 0 };
  static const int64_t nodes[3] = { 4000, 40000, 600000 };
  int l = level < 0 ? 0 : level > 2 ? 2 : level;
  o->max_depth = depth[l];
  o->mistake = mistake[l];
  o->mistake_spread = l == 0 ? 3 : 2;
  o->max_nodes = nodes[l];
  o->tt_entries = 1 << 16;
}
