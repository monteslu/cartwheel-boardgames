/* Smokestack rules (rules.h): pure, a bk_game (cw_search_game). */
#include "rules.h"

#include <stdio.h>
#include <string.h>

#include "map_data.h"

const char *const RL_ACTION_NAMES[RL_ACTIONS] = { "Track Gang", "Charter", "New Engine", "Freight Office", "Lead Builder", "Lead Shipper" };
const char *const RL_ACTION_TEXT[RL_ACTIONS] = {
  "lay four tiles this round", "turn a town into a city", "your engine grows a car now",
  "draw two goods, place them", "build first this round", "haul first this round",
};
const char *const RL_COLOR_NAMES[RL_COLORS] = { "red", "blue", "yellow", "violet", "black" };

#define START_MONEY 15
#define BAG_EACH 14

const rl_map_def *rl_map(int map) { return &RL_MAP_DEFS[map]; }
static const rl_map_def *M(const rl_state *s) { return &RL_MAP_DEFS[s->map]; }
int rl_rounds_for(int players) { return players <= 2 ? 8 : players == 3 ? 7 : 6; }

static int hex_dist(const rl_map_def *m, int a, int b) {
  int dq = m->q[a] - m->q[b], dr = m->r[a] - m->r[b], ds = -dq - dr;
  return ((dq < 0 ? -dq : dq) + (dr < 0 ? -dr : dr) + (ds < 0 ? -ds : ds)) / 2;
}

int rl_cubes(const rl_state *s, int set) {
  int n = 0;
  while (n < RL_CITY_CUBES && s->cube[set][n]) n++;
  return n;
}

int rl_tile_cost(const rl_state *s, int hex) {
  switch (M(s)->terrain[hex]) {
  case RL_T_PLAIN: return 2;
  case RL_T_FOREST:
  case RL_T_RIVER: return 3;
  case RL_T_HILLS: return 4;
  default: return 99;
  }
}

static bool passable(const rl_state *s, int hex) {
  int t = M(s)->terrain[hex];
  return t != RL_T_LAKE && t != RL_T_PEAK;
}
/* an empty hex a tile may go on */
static bool open_hex(const rl_state *s, int hex) {
  return hex >= 0 && passable(s, hex) && M(s)->hex_set[hex] < 0 && s->tile_link[hex] < 0;
}

int rl_current(const rl_state *s) {
  switch (s->phase) {
  case RL_PH_BID:
  case RL_PH_ACTION:
  case RL_PH_FREIGHT: return s->order[s->at];
  case RL_PH_BUILD:
  case RL_PH_EXIT:
  case RL_PH_TOWN:
  case RL_PH_COLOR:
  case RL_PH_HAUL:
  case RL_PH_ROUTE:
  case RL_PH_PAYOUT: return s->seq[s->at];
  default: return -1;
  }
}

int rl_links_done(const rl_state *s, int seat) {
  int n = 0;
  for (int i = 0; i < s->nlink; i++) n += s->link[i].owner == seat && s->link[i].done;
  return n;
}
int rl_final_score(const rl_state *s, int seat) {
  const rl_player *p = &s->p[seat];
  return p->points + RL_LINK_POINTS * rl_links_done(s, seat) + p->money / RL_MONEY_PER_POINT - p->shares;
}
int rl_share_room(const rl_state *s, int seat) {
  const rl_player *p = &s->p[seat];
  int cap = rl_links_done(s, seat) + RL_SHARES_FREE;
  if (cap > RL_MAX_SHARES) cap = RL_MAX_SHARES;
  return cap > p->shares ? cap - p->shares : 0;
}

int rl_standing(const rl_state *s, int8_t *out) {
  int n = s->players;
  for (int i = 0; i < n; i++) out[i] = s->order[i];
  for (int i = 1; i < n; i++) /* insertion sort: stable on turn order */
    for (int j = i; j > 0; j--) {
      int a = out[j - 1], b = out[j];
      int sa = rl_final_score(s, a), sb = rl_final_score(s, b);
      if (sb > sa || (sb == sa && s->p[b].money > s->p[a].money)) out[j - 1] = (int8_t)b, out[j] = (int8_t)a;
      else break;
    }
  return n;
}
int rl_winner(const rl_state *s) {
  if (s->phase != RL_PH_OVER) return -1;
  int8_t o[RL_MAX_PLAYERS];
  rl_standing(s, o);
  return o[0];
}

/* ── chance ── */
static int fill_outcomes(const rl_state *s, bk_move *out, uint32_t *w) {
  int city = s->fill_city[0], n = 0;
  for (int c = 0; c < RL_COLORS; c++)
    if (s->bag[c] && c != s->set_color[city]) out[n] = rl_mv(RL_MV_CH_FILL, c, 0), w[n++] = s->bag[c];
  return n;
}
static int draw_outcomes(const rl_state *s, bk_move *out, uint32_t *w) {
  int n = 0;
  for (int c = 0; c < RL_COLORS; c++)
    if (s->bag[c]) out[n] = rl_mv(RL_MV_CH_DRAW, c, 0), w[n++] = s->bag[c];
  return n;
}
static int prod_outcomes(const rl_state *s, bk_move *out, uint32_t *w) {
  int n = 0;
  for (int k = 0; k < M(s)->nset; k++) {
    if (s->set_kind[k] != RL_CITY || rl_cubes(s, k) >= RL_CITY_CUBES) continue;
    for (int c = 0; c < RL_COLORS; c++)
      if (s->bag[c] && c != s->set_color[k]) out[n] = rl_mv(RL_MV_CH_PROD, k, c), w[n++] = s->bag[c];
  }
  return n;
}
enum { CH_NONE, CH_FILL, CH_DRAW, CH_PROD };
static int chance_kind(const rl_state *s) {
  if (s->fill_n > 0) return CH_FILL;
  if (s->draw_n > 0) return CH_DRAW;
  if (s->phase == RL_PH_PRODUCE && s->prod_n > 0) return CH_PROD;
  return CH_NONE;
}

static void add_cube(rl_state *s, int set, int color) {
  int n = rl_cubes(s, set);
  if (n >= RL_CITY_CUBES) CW_FATAL("smokestack: a good into a full city");
  if (!s->bag[color]) CW_FATAL("smokestack: a %s good from an empty bag", RL_COLOR_NAMES[color]);
  s->bag[color]--;
  s->cube[set][n] = (uint8_t)(color + 1);
}
static void remove_cube(rl_state *s, int set, int slot) {
  for (int i = slot; i < RL_CITY_CUBES - 1; i++) s->cube[set][i] = s->cube[set][i + 1];
  s->cube[set][RL_CITY_CUBES - 1] = 0;
}
static int bag_total(const rl_state *s) {
  int t = 0;
  for (int c = 0; c < RL_COLORS; c++) t += s->bag[c];
  return t;
}

/* ── round flow ── */
static void begin_round(rl_state *s) {
  s->phase = RL_PH_BID;
  s->at = 0;
  s->bid = 0, s->bid_leader = -1, s->bid_out_n = 0;
  memset(s->bid_out, 0, sizeof s->bid_out);
  memset(s->next_order, -1, sizeof s->next_order);
  memset(s->action_owner, -1, sizeof s->action_owner);
  for (int i = 0; i < s->players; i++) {
    s->p[i].action = -1, s->p[i].charter_used = 0, s->p[i].limit = 3;
  }
}

static void make_seq(rl_state *s, int lead_action) {
  int lead = s->action_owner[lead_action], k = 0;
  if (lead >= 0) s->seq[k++] = (int8_t)lead;
  for (int i = 0; i < s->players; i++)
    if (s->order[i] != lead) s->seq[k++] = s->order[i];
}

static void begin_build_turn(rl_state *s) {
  rl_player *p = &s->p[s->seq[s->at]];
  p->built = 0, p->extended = 0;
  s->phase = RL_PH_BUILD;
}

static void begin_build(rl_state *s) {
  make_seq(s, RL_A_LEAD_BUILD);
  s->at = 0;
  begin_build_turn(s);
}

static void begin_haul(rl_state *s) {
  make_seq(s, RL_A_LEAD_SHIP);
  s->at = 0;
  s->pass_no = 0;
  s->phase = RL_PH_HAUL;
}

static void end_round(rl_state *s) {
  for (int i = 0; i < s->players; i++) {
    rl_player *p = &s->p[i];
    p->money = (int16_t)(p->money + rl_net_income(p));
    if (p->money < 0) p->points = (int16_t)(p->points + p->money), p->money = 0;
  }
  s->phase = RL_PH_PRODUCE;
  s->prod_n = s->players + 2;
}

static void next_haul(rl_state *s) {
  s->phase = RL_PH_HAUL;
  if (++s->at < s->players) return;
  s->at = 0;
  if (++s->pass_no < RL_PASSES) return;
  end_round(s);
}

static void next_action(rl_state *s) {
  s->phase = RL_PH_ACTION;
  if (++s->at < s->players) return;
  begin_build(s);
}

/* settle what needs no decision: unplaceable fills dropped, an empty bag, the end of production */
static void settle(rl_state *s) {
  for (int guard = 0; guard < 64; guard++) {
    bk_move mv[BK_MAX_MOVES];
    uint32_t w[BK_MAX_MOVES];
    if (s->fill_n > 0) {
      int city = s->fill_city[0];
      if (rl_cubes(s, city) >= RL_CITY_CUBES || fill_outcomes(s, mv, w) == 0) {
        memmove(s->fill_city, s->fill_city + 1, sizeof s->fill_city - 1);
        s->fill_city[sizeof s->fill_city - 1] = 0;
        s->fill_n--;
        continue;
      }
      return;
    }
    if (s->draw_n > 0) {
      if (bag_total(s) == 0) { s->draw_n = 0; continue; }
      return;
    }
    if (s->phase == RL_PH_FREIGHT) {
      bool room = false;
      for (int k = 0; k < M(s)->nset && s->drawn_n; k++)
        room |= s->set_kind[k] == RL_CITY && rl_cubes(s, k) < RL_CITY_CUBES && s->set_color[k] != s->drawn[0] - 1;
      if (s->drawn_n > 0 && !room) { /* nowhere to put it: back in the bag */
        s->bag[s->drawn[0] - 1]++;
        s->drawn[0] = s->drawn[1], s->drawn[1] = 0, s->drawn_n--;
        continue;
      }
      if (s->drawn_n == 0) { next_action(s); continue; }
      return;
    }
    if (s->phase == RL_PH_PRODUCE) {
      if (s->prod_n > 0 && prod_outcomes(s, mv, w) > 0) return;
      s->prod_n = 0;
      if (++s->round > s->rounds) { s->phase = RL_PH_OVER; return; }
      begin_round(s);
      continue;
    }
    return;
  }
  CW_FATAL("smokestack: settle did not settle");
}

void rl_setup(rl_state *s, int players) {
  if (players < 2 || players > RL_MAX_PLAYERS) CW_FATAL("smokestack: %d players (2 to 4)", players);
  memset(s, 0, sizeof *s);
  s->players = players;
  s->map = players <= 2 ? 0 : 1;
  s->round = 1;
  s->rounds = rl_rounds_for(players);
  for (int i = 0; i < players; i++) {
    rl_player *p = &s->p[i];
    p->money = START_MONEY, p->engine = 1, p->open_link = -1, p->action = -1, p->limit = 3;
    s->order[i] = (int8_t)i;
  }
  memset(s->tile_link, -1, sizeof s->tile_link);
  memset(s->tile_in, -1, sizeof s->tile_in);
  memset(s->tile_out, -1, sizeof s->tile_out);
  for (int i = 0; i < RL_MAX_LINK; i++) s->link[i].owner = -1;
  const rl_map_def *m = M(s);
  for (int k = 0; k < m->nset; k++) s->set_kind[k] = m->set_kind[k], s->set_color[k] = m->set_color[k];
  for (int c = 0; c < RL_COLORS; c++) s->bag[c] = BAG_EACH;
  s->new_city_colors = (1 << RL_COLORS) - 1;
  begin_round(s);
  /* the opening goods: two in each city (chance) */
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < m->nset; k++)
      if (m->set_kind[k] == RL_CITY) {
        if (s->fill_n >= (int)sizeof s->fill_city) CW_FATAL("smokestack: the fill queue is full");
        s->fill_city[s->fill_n++] = (int8_t)k;
      }
  s->build_hex = -1, s->charter_town = -1, s->good_city = -1, s->good_slot = -1, s->last_kind = 0, s->last_seat = -1;
}

/* ── routes ── */
typedef struct route_search {
  const rl_state *s;
  int color, seat, src;
  int8_t path[RL_ROUTE_LINKS];
  bool seen[RL_MAX_SET];
  rl_route best[RL_MAX_SET]; /* per destination */
  bool have[RL_MAX_SET];
} route_search;

static void route_dfs(route_search *r, int at, int depth) {
  const rl_state *s = r->s;
  int engine = s->p[r->seat].engine;
  for (int i = 0; i < s->nlink; i++) {
    const rl_link *l = &s->link[i];
    if (l->owner < 0 || !l->done) continue;
    int other = l->from == at ? l->to : l->to == at ? l->from : -1;
    if (other < 0 || r->seen[other]) continue;
    r->path[depth] = (int8_t)i;
    int n = depth + 1;
    if (s->set_kind[other] == RL_CITY && s->set_color[other] == r->color) {
      int own = 0;
      for (int k = 0; k < n; k++) own += s->link[r->path[k]].owner == r->seat;
      rl_route *b = &r->best[other];
      if (!r->have[other] || own > b->own || (own == b->own && n < b->n)) {
        memset(b, 0, sizeof *b);
        b->dest = (int8_t)other, b->n = (int8_t)n, b->own = (int8_t)own;
        memcpy(b->link, r->path, (size_t)n);
        r->have[other] = true;
      }
      continue; /* a good stops at the first city that wants it */
    }
    if (n >= engine) continue;
    r->seen[other] = true;
    route_dfs(r, other, n);
    r->seen[other] = false;
  }
}

int rl_routes(const rl_state *s, int city, int slot, int seat, rl_route *out, int cap) {
  int c = s->cube[city][slot];
  if (!c) return 0;
  route_search r;
  memset(&r, 0, sizeof r);
  r.s = s, r.color = c - 1, r.seat = seat, r.src = city;
  r.seen[city] = true;
  route_dfs(&r, city, 0);
  int n = 0;
  for (int k = 0; k < RL_MAX_SET && n < cap; k++)
    if (r.have[k]) out[n++] = r.best[k];
  return n;
}

int rl_link_hexes(const rl_state *s, int link, int8_t *out, int cap) {
  const rl_link *l = &s->link[link];
  if (l->owner < 0) return 0;
  int n = 0, h = l->first;
  while (h >= 0 && n < cap && s->tile_link[h] == link) {
    out[n++] = (int8_t)h;
    if (h == l->last) break;
    h = M(s)->nb[h][s->tile_out[h]];
  }
  return n;
}

/* ── building ── */
static bool linked(const rl_state *s, int a, int b) {
  for (int i = 0; i < s->nlink; i++) {
    const rl_link *l = &s->link[i];
    if (l->owner >= 0 && l->done && ((l->from == a && l->to == b) || (l->from == b && l->to == a))) return true;
  }
  return false;
}

/* the hex the seat's next tile goes on, its entry edge and the link's start settlement */
static int next_hex(const rl_state *s, int seat, int *entry, int *from) {
  const rl_player *p = &s->p[seat];
  if (s->phase == RL_PH_EXIT) {
    *entry = s->tile_in[s->build_hex] >= 0 ? s->tile_in[s->build_hex] : -1;
    return -1;
  }
  if (p->open_link < 0) return -1;
  const rl_link *l = &s->link[p->open_link];
  int last = l->last, d = s->tile_out[last];
  int h = M(s)->nb[last][d];
  *entry = (d + 3) % 6;
  *from = l->from;
  return h;
}

/* can a tile on hex h entering by edge `entry` (from the link that starts at settlement `from`) leave by d? */
static bool exit_ok(const rl_state *s, int seat, int h, int entry, int from, int d) {
  if (d == entry) return false;
  int n = M(s)->nb[h][d];
  if (n < 0) return false;
  int cost = rl_tile_cost(s, h);
  int set = M(s)->hex_set[n];
  if (set >= 0) {
    if (set == from || linked(s, from, set)) return false;
    cost += s->set_kind[set] == RL_TOWN;
  } else if (!open_hex(s, n)) return false;
  return s->p[seat].money >= cost;
}

static int exits(const rl_state *s, int seat, int h, int entry, int from, bk_move *out) {
  int n = 0;
  for (int d = 0; d < 6; d++)
    if (exit_ok(s, seat, h, entry, from, d)) out[n++] = rl_mv(RL_MV_EXIT, d, 0);
  return n;
}

/* the pending tile's entry and origin while choosing its exit */
static void pending(const rl_state *s, int *h, int *entry, int *from) {
  *h = s->build_hex;
  *entry = s->tile_in[*h];
  *from = M(s)->hex_set[M(s)->nb[*h][*entry]];
}

static bool can_charter(const rl_state *s, int seat) {
  const rl_player *p = &s->p[seat];
  if (p->action != RL_A_CHARTER || p->charter_used || !s->new_city_colors) return false;
  for (int k = 0; k < M(s)->nset; k++)
    if (s->set_kind[k] == RL_TOWN) return true;
  return false;
}

/* ── moves ── */
static int list_moves(const rl_state *s, bk_move *out, int cap) {
  (void)cap;
  int seat = rl_current(s), n = 0;
  if (seat < 0) return 0;
  const rl_player *p = &s->p[seat];
  const rl_map_def *m = M(s);
  switch (s->phase) {
  case RL_PH_BID: {
    out[n++] = rl_mv(RL_MV_PASS, 0, 0);
    static const int RAISE[3] = { 1, 2, 5 };
    for (int i = 0; i < 3; i++)
      if (s->bid + RAISE[i] <= p->money) out[n++] = rl_mv(RL_MV_RAISE, RAISE[i], 0);
    break;
  }
  case RL_PH_ACTION:
    for (int a = 0; a < RL_ACTIONS; a++)
      if (s->action_owner[a] < 0) out[n++] = rl_mv(RL_MV_ACTION, a, 0);
    break;
  case RL_PH_FREIGHT:
    for (int k = 0; k < m->nset; k++)
      if (s->set_kind[k] == RL_CITY && rl_cubes(s, k) < RL_CITY_CUBES && s->set_color[k] != s->drawn[0] - 1)
        out[n++] = rl_mv(RL_MV_PLACE, k, 0);
    break;
  case RL_PH_BUILD:
    if (p->built < p->limit) {
      if (p->open_link >= 0) {
        int entry = -1, from = -1, h = next_hex(s, seat, &entry, &from);
        if (open_hex(s, h) && p->money >= rl_tile_cost(s, h)) n += exits(s, seat, h, entry, from, out + n);
      } else {
        for (int h = 0; h < m->nhex; h++) {
          if (!open_hex(s, h) || p->money < rl_tile_cost(s, h)) continue;
          for (int e = 0; e < 6; e++) {
            int sh = m->nb[h][e];
            if (sh < 0 || m->hex_set[sh] < 0) continue;
            bk_move tmp[6];
            if (exits(s, seat, h, e, m->hex_set[sh], tmp) > 0) out[n++] = rl_mv(RL_MV_START, h, e);
          }
        }
      }
    }
    if (can_charter(s, seat)) out[n++] = rl_mv(RL_MV_CHARTER, 0, 0);
    if (rl_share_room(s, seat) > 0 && p->money < RL_SHARE_BELOW) out[n++] = rl_mv(RL_MV_SHARE, 0, 0);
    out[n++] = rl_mv(RL_MV_PASS, 0, 0);
    break;
  case RL_PH_EXIT: {
    int h, entry, from;
    pending(s, &h, &entry, &from);
    n += exits(s, seat, h, entry, from, out);
    break;
  }
  case RL_PH_TOWN:
    for (int k = 0; k < m->nset; k++)
      if (s->set_kind[k] == RL_TOWN) out[n++] = rl_mv(RL_MV_TOWN, k, 0);
    break;
  case RL_PH_COLOR:
    for (int c = 0; c < RL_COLORS; c++)
      if (s->new_city_colors & (1 << c)) out[n++] = rl_mv(RL_MV_COLOR, c, 0);
    break;
  case RL_PH_HAUL:
    for (int k = 0; k < m->nset; k++) {
      if (s->set_kind[k] != RL_CITY) continue;
      int nc = rl_cubes(s, k);
      for (int i = 0; i < nc; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) dup |= s->cube[k][j] == s->cube[k][i];
        if (dup) continue; /* one choice per colour in a city */
        rl_route r[1];
        if (rl_routes(s, k, i, seat, r, 1) > 0) out[n++] = rl_mv(RL_MV_GOOD, k, i);
      }
    }
    if (p->engine < RL_MAX_ENGINE) out[n++] = rl_mv(RL_MV_ENGINE, 0, 0);
    out[n++] = rl_mv(RL_MV_PASS, 0, 0);
    break;
  case RL_PH_ROUTE: {
    rl_route r[RL_MAX_ROUTES];
    int nr = rl_routes(s, s->good_city, s->good_slot, seat, r, RL_MAX_ROUTES);
    for (int i = 0; i < nr; i++) out[n++] = rl_mv(RL_MV_DEST, r[i].dest, 0);
    break;
  }
  case RL_PH_PAYOUT:
    out[n++] = rl_mv(RL_MV_INCOME, 0, 0);
    out[n++] = rl_mv(RL_MV_POINTS, 0, 0);
    break;
  default: break;
  }
  return n;
}

static int moves(const void *st, bk_move *out, int cap, void *u) {
  (void)u;
  return list_moves(st, out, cap);
}
/* the CPUs' moves: the rules' moves less a share the seat's income cannot carry (a CPU issues one only while its
 * money each round stays at zero or more after it), so no CPU sinks into upkeep it cannot pay. People keep every
 * move. */
static int moves_cpu(const void *st, bk_move *out, int cap, void *u) {
  (void)u;
  const rl_state *s = st;
  int n = list_moves(st, out, cap);
  if (s->phase != RL_PH_BUILD) return n;
  const rl_player *p = &s->p[rl_current(s)];
  if (rl_net_income(p) >= 1) return n;
  int k = 0;
  for (int i = 0; i < n; i++)
    if (rl_mv_kind(out[i]) != RL_MV_SHARE) out[k++] = out[i];
  return k;
}

int rl_turn(const rl_state *s) {
  if (chance_kind(s) != CH_NONE) return BK_CHANCE;
  return rl_current(s);
}
static int turn(const void *st, void *u) { (void)u; return rl_turn(st); }
static bool terminal(const void *st, void *u) { (void)u; return ((const rl_state *)st)->phase == RL_PH_OVER; }

static int outcomes(const void *st, bk_move *out, uint32_t *w, int cap, void *u) {
  (void)u, (void)cap;
  const rl_state *s = st;
  switch (chance_kind(s)) {
  case CH_FILL: return fill_outcomes(s, out, w);
  case CH_DRAW: return draw_outcomes(s, out, w);
  case CH_PROD: return prod_outcomes(s, out, w);
  default: return 0;
  }
}

static void tear_up(rl_state *s, int link) {
  rl_link *l = &s->link[link];
  for (int h = 0; h < M(s)->nhex; h++)
    if (s->tile_link[h] == link) s->tile_link[h] = -1, s->tile_in[h] = -1, s->tile_out[h] = -1;
  s->p[l->owner].open_link = -1;
  memset(l, 0, sizeof *l);
  l->owner = -1;
}

static void place_tile(rl_state *s, int seat, int h, int entry, int exit) {
  rl_player *p = &s->p[seat];
  const rl_map_def *m = M(s);
  int link = p->open_link;
  if (s->phase == RL_PH_EXIT) { /* a new link from the settlement beside the entry */
    link = -1;
    for (int i = 0; i < s->nlink; i++)
      if (s->link[i].owner < 0) { link = i; break; }
    if (link < 0) {
      if (s->nlink >= RL_MAX_LINK) CW_FATAL("smokestack: more than %d links", RL_MAX_LINK);
      link = s->nlink++;
    }
    rl_link *l = &s->link[link];
    memset(l, 0, sizeof *l);
    l->owner = (int8_t)seat, l->from = m->hex_set[m->nb[h][entry]], l->to = -1;
    l->first = (int8_t)h, l->from_dir = (int8_t)((entry + 3) % 6);
    p->open_link = (int8_t)link;
  }
  rl_link *l = &s->link[link];
  l->last = (int8_t)h;
  l->tiles++;
  s->tile_link[h] = (int8_t)link, s->tile_in[h] = (int8_t)entry, s->tile_out[h] = (int8_t)exit;
  int cost = rl_tile_cost(s, h);
  int end = m->hex_set[m->nb[h][exit]];
  if (end >= 0) cost += s->set_kind[end] == RL_TOWN;
  p->money = (int16_t)(p->money - cost);
  if (p->money < 0) CW_FATAL("smokestack: a tile built on credit");
  p->built++, p->extended = 1;
  s->last_kind = RL_MV_EXIT, s->last_seat = seat, s->last_a = h, s->last_b = cost;
  if (end >= 0) {
    l->to = (int8_t)end, l->done = 1;
    p->open_link = -1;
  }
  s->build_hex = -1;
  s->phase = RL_PH_BUILD;
}

static void deliver(rl_state *s, int seat, const rl_route *r) {
  int own = 0;
  for (int i = 0; i < r->n; i++) {
    int o = s->link[r->link[i]].owner;
    if (o == seat) own++;
    else s->p[o].points++;
  }
  remove_cube(s, s->good_city, s->good_slot);
  s->last_kind = RL_MV_DEST, s->last_seat = seat, s->last_a = r->dest, s->last_b = r->n;
  if (own > 0) {
    s->phase = RL_PH_PAYOUT;
    s->last_gain = own;
  } else {
    s->good_city = s->good_slot = -1;
    next_haul(s);
  }
}

static void apply_move(rl_state *s, bk_move mv) {
  int k = rl_mv_kind(mv), a = rl_mv_a(mv), b = rl_mv_b(mv);
  int turn = rl_turn(s);
  if (turn == BK_CHANCE) {
    switch (k) {
    case RL_MV_CH_FILL:
      add_cube(s, s->fill_city[0], a);
      memmove(s->fill_city, s->fill_city + 1, sizeof s->fill_city - 1);
      s->fill_city[sizeof s->fill_city - 1] = 0;
      s->fill_n--;
      break;
    case RL_MV_CH_DRAW:
      if (!s->bag[a]) CW_FATAL("smokestack: drew from an empty colour");
      s->bag[a]--;
      s->drawn[s->drawn_n++] = (uint8_t)(a + 1);
      s->draw_n--;
      break;
    case RL_MV_CH_PROD:
      add_cube(s, a, b);
      s->prod_n--;
      s->last_kind = RL_MV_CH_PROD, s->last_a = a, s->last_b = b;
      break;
    default: CW_FATAL("smokestack: chance outcome %d", k);
    }
    settle(s);
    return;
  }
  int seat = turn;
  rl_player *p = &s->p[seat];
  s->moves++;
  s->last_kind = k, s->last_seat = seat, s->last_a = a, s->last_b = b;
  switch (s->phase) {
  case RL_PH_BID:
    if (k == RL_MV_RAISE) {
      s->bid += a;
      s->bid_leader = seat;
    } else {
      s->bid_out[seat] = 1;
      s->next_order[s->players - 1 - s->bid_out_n] = (int8_t)seat;
      s->bid_out_n++;
    }
    if (s->players - s->bid_out_n == 1) {
      int w = -1;
      for (int i = 0; i < s->players; i++)
        if (!s->bid_out[i]) w = i;
      s->next_order[0] = (int8_t)w;
      s->p[w].money = (int16_t)(s->p[w].money - s->bid);
      s->last_kind = RL_MV_RAISE, s->last_seat = w, s->last_a = s->bid, s->last_b = -1; /* -1: the auction's end */
      memcpy(s->order, s->next_order, sizeof s->order);
      s->phase = RL_PH_ACTION;
      s->at = 0;
    } else {
      do s->at = (s->at + 1) % s->players; while (s->bid_out[s->order[s->at]]);
    }
    break;
  case RL_PH_ACTION:
    s->action_owner[a] = (int8_t)seat;
    p->action = (int8_t)a;
    if (a == RL_A_GANG) p->limit = 4;
    if (a == RL_A_ENGINE && p->engine < RL_MAX_ENGINE) p->engine++;
    if (a == RL_A_FREIGHT) {
      s->phase = RL_PH_FREIGHT;
      s->draw_n = 2, s->drawn_n = 0;
      memset(s->drawn, 0, sizeof s->drawn);
    } else next_action(s);
    break;
  case RL_PH_FREIGHT: {
    int c = s->drawn[0] - 1;
    s->bag[c]++; /* add_cube takes it from the bag again */
    add_cube(s, a, c);
    s->drawn[0] = s->drawn[1], s->drawn[1] = 0, s->drawn_n--;
    break;
  }
  case RL_PH_BUILD:
    switch (k) {
    case RL_MV_START:
      s->build_hex = a;
      s->tile_in[a] = (int8_t)b; /* the entry, held while the exit is chosen */
      s->phase = RL_PH_EXIT;
      break;
    case RL_MV_EXIT: {
      int entry = -1, from = -1, h = next_hex(s, seat, &entry, &from);
      place_tile(s, seat, h, entry, a);
      break;
    }
    case RL_MV_SHARE:
      p->shares++;
      p->money = (int16_t)(p->money + 5);
      break;
    case RL_MV_CHARTER: s->phase = RL_PH_TOWN; break;
    case RL_MV_PASS:
      if (p->open_link >= 0 && !p->extended) tear_up(s, p->open_link);
      if (++s->at < s->players) begin_build_turn(s);
      else begin_haul(s);
      break;
    default: CW_FATAL("smokestack: move %d while building", k);
    }
    break;
  case RL_PH_EXIT: {
    int h, entry, from;
    pending(s, &h, &entry, &from);
    place_tile(s, seat, h, entry, a);
    break;
  }
  case RL_PH_TOWN:
    s->charter_town = a;
    s->phase = RL_PH_COLOR;
    break;
  case RL_PH_COLOR:
    s->set_kind[s->charter_town] = RL_CITY;
    s->set_color[s->charter_town] = (uint8_t)a;
    s->new_city_colors = (int8_t)(s->new_city_colors & ~(1 << a));
    p->charter_used = 1;
    s->fill_city[s->fill_n++] = (int8_t)s->charter_town;
    s->fill_city[s->fill_n++] = (int8_t)s->charter_town;
    s->last_kind = RL_MV_COLOR, s->last_a = s->charter_town, s->last_b = a;
    s->charter_town = -1;
    s->phase = RL_PH_BUILD;
    break;
  case RL_PH_HAUL:
    if (k == RL_MV_GOOD) {
      s->good_city = a, s->good_slot = b;
      s->phase = RL_PH_ROUTE;
    } else {
      if (k == RL_MV_ENGINE) p->engine++;
      next_haul(s);
    }
    break;
  case RL_PH_ROUTE: {
    rl_route r[RL_MAX_ROUTES];
    int nr = rl_routes(s, s->good_city, s->good_slot, seat, r, RL_MAX_ROUTES), i = 0;
    while (i < nr && r[i].dest != a) i++;
    if (i == nr) CW_FATAL("smokestack: no route to %d", a);
    s->route = r[i];
    deliver(s, seat, &s->route);
    break;
  }
  case RL_PH_PAYOUT:
    if (k == RL_MV_INCOME) p->income = (int16_t)(p->income + s->last_gain);
    else p->points = (int16_t)(p->points + s->last_gain);
    s->last_kind = k, s->last_a = s->last_gain;
    s->good_city = s->good_slot = -1;
    next_haul(s);
    break;
  default: CW_FATAL("smokestack: a move in phase %d", s->phase);
  }
  settle(s);
}

static void apply(void *st, bk_move m, void *u) {
  (void)u;
  apply_move(st, m);
}

/* ── evaluation ── */
static float seat_value(const rl_state *s, int i) {
  const rl_player *p = &s->p[i];
  int left = s->rounds - s->round; /* whole rounds after this one */
  /* the final count with money smooth (a point per RL_MONEY_PER_POINT) and a little more while it can still build */
  float v = (float)(p->points + RL_LINK_POINTS * rl_links_done(s, i) - p->shares);
  float wm = 1.0f / RL_MONEY_PER_POINT + (left >= 2 ? 0.12f : left == 1 ? 0.06f : 0.0f);
  v += wm * (float)p->money + 0.3f * (float)rl_net_income(p) * (float)(left + 1);
  v += (left > 1 ? 0.8f : 0.2f) * (float)(p->engine > 4 ? 4 : p->engine);
  if (p->open_link >= 0) v += 0.4f * (float)s->link[p->open_link].tiles;
  return v;
}

static void evaluate(const void *st, float *score, void *u) {
  (void)u;
  const rl_state *s = st;
  if (s->phase == RL_PH_OVER) {
    int8_t o[RL_MAX_PLAYERS];
    rl_standing(s, o);
    for (int k = 0; k < s->players; k++) {
      int i = o[k];
      score[i] = k == 0 ? BK_WIN - (float)s->moves : BK_LOSS + (float)rl_final_score(s, i) + 100.0f * (float)(s->players - k);
    }
    return;
  }
  float v[RL_MAX_PLAYERS], best_other;
  for (int i = 0; i < s->players; i++) v[i] = seat_value(s, i);
  for (int i = 0; i < s->players; i++) {
    best_other = -1e9f;
    for (int j = 0; j < s->players; j++)
      if (j != i && v[j] > best_other) best_other = v[j];
    score[i] = v[i] - best_other;
  }
}

/* ── a heuristic for playouts and ordering ── */
static int nearest_settlement(const rl_state *s, int h, int not_set) {
  const rl_map_def *m = M(s);
  int best = 99;
  for (int k = 0; k < m->nset; k++) {
    if (k == not_set) continue;
    int d = hex_dist(m, h, m->set_hex[k]);
    if (d < best) best = d;
  }
  return best;
}

float rl_move_value(const rl_state *s, bk_move mv) {
  int seat = rl_current(s), k = rl_mv_kind(mv), a = rl_mv_a(mv), b = rl_mv_b(mv);
  if (seat < 0) return 0;
  const rl_player *p = &s->p[seat];
  const rl_map_def *m = M(s);
  int left = s->rounds - s->round;
  switch (s->phase) {
  case RL_PH_BID: return k == RL_MV_PASS ? 1.0f : 0.6f - 0.15f * (float)a - 0.1f * (float)s->bid;
  case RL_PH_ACTION: {
    static const float BASE[RL_ACTIONS] = { 1.0f, 0.5f, 0.9f, 0.7f, 0.6f, 0.8f };
    float v = BASE[a];
    if (a == RL_A_ENGINE && p->engine >= 3) v = 0.2f;
    return v;
  }
  case RL_PH_FREIGHT: return 0.5f + 0.1f * (float)rl_cubes(s, a);
  case RL_PH_BUILD:
  case RL_PH_EXIT:
    if (k == RL_MV_PASS) return p->money < 3 || p->built >= p->limit ? 1.0f : -1.0f;
    if (k == RL_MV_SHARE) return p->money < 4 && p->shares < 4 && left > 1 ? 0.8f : -2.0f;
    if (k == RL_MV_CHARTER) return 0.3f;
    if (k == RL_MV_START) {
      int from = m->hex_set[m->nb[a][b]];
      float v = 2.0f - 0.5f * (float)nearest_settlement(s, a, from) - 0.15f * (float)rl_tile_cost(s, a);
      if (s->set_kind[from] == RL_CITY) v += 0.2f * (float)rl_cubes(s, from);
      return v;
    }
    if (k == RL_MV_EXIT) {
      int h, entry, from;
      if (s->phase == RL_PH_EXIT) pending(s, &h, &entry, &from);
      else h = next_hex(s, seat, &entry, &from);
      int n = m->nb[h][a];
      int set = m->hex_set[n];
      if (set >= 0) return 4.0f + (s->set_kind[set] == RL_CITY ? 1.0f : 0.0f);
      return 2.0f - 0.6f * (float)nearest_settlement(s, n, from) - 0.1f * (float)rl_tile_cost(s, n);
    }
    return 0;
  case RL_PH_TOWN: return 0.1f * (float)a;
  case RL_PH_COLOR: return 0;
  case RL_PH_HAUL:
    if (k == RL_MV_GOOD) {
      rl_route r[RL_MAX_ROUTES];
      int nr = rl_routes(s, a, b, seat, r, RL_MAX_ROUTES);
      float best = 0;
      for (int i = 0; i < nr; i++)
        if (r[i].own + 0.3f * r[i].n > best) best = r[i].own + 0.3f * r[i].n;
      return 1.0f + best;
    }
    if (k == RL_MV_ENGINE) return p->engine < 3 && left > 1 ? 0.9f : 0.1f;
    return -0.5f;
  case RL_PH_ROUTE: {
    rl_route r[RL_MAX_ROUTES];
    int nr = rl_routes(s, s->good_city, s->good_slot, seat, r, RL_MAX_ROUTES);
    for (int i = 0; i < nr; i++)
      if (r[i].dest == a) return (float)r[i].own + 0.2f * (float)r[i].n;
    return 0;
  }
  case RL_PH_PAYOUT: return (k == RL_MV_INCOME) == (left >= 3) ? 1.0f : 0.0f;
  default: return 0;
  }
}

static float priority(const void *st, bk_move m, void *u) { (void)u; return rl_move_value(st, m); }

static bk_move playout(const void *st, const bk_move *mv, int n, cw_rng *rng, void *u) {
  (void)u;
  if (cw_rng_int(rng, 0, 99) < 20) return mv[cw_rng_int(rng, 0, n - 1)];
  int best = 0;
  float bv = -1e9f;
  for (int i = 0; i < n; i++) {
    float v = rl_move_value(st, mv[i]) + 0.05f * (float)cw_rng_int(rng, 0, 9);
    if (v > bv) bv = v, best = i;
  }
  return mv[best];
}

#define RL_GAME(n) { .players = n, .state_size = sizeof(rl_state), .moves = moves, .apply = apply, .turn = turn, \
                     .terminal = terminal, .evaluate = evaluate, .priority = priority, .playout = playout, .outcomes = outcomes }
const bk_game RL_RULES[RL_MAX_PLAYERS + 1] = { RL_GAME(2), RL_GAME(2), RL_GAME(2), RL_GAME(3), RL_GAME(4) };
#define RL_CPU_GAME(n) { .players = n, .state_size = sizeof(rl_state), .moves = moves_cpu, .apply = apply, .turn = turn, \
                         .terminal = terminal, .evaluate = evaluate, .priority = priority, .playout = playout, .outcomes = outcomes }
const bk_game RL_CPU_RULES[RL_MAX_PLAYERS + 1] = { RL_CPU_GAME(2), RL_CPU_GAME(2), RL_CPU_GAME(2), RL_CPU_GAME(3), RL_CPU_GAME(4) };

void rl_move_label(const void *st, bk_move m, char *buf, size_t cap, void *user) {
  (void)user;
  const rl_state *s = st;
  const rl_map_def *mp = M(s);
  int k = rl_mv_kind(m), a = rl_mv_a(m), b = rl_mv_b(m);
  switch (k) {
  case RL_MV_PASS: snprintf(buf, cap, s->phase == RL_PH_BID ? "drop out" : s->phase == RL_PH_BUILD ? "end building" : "pass"); break;
  case RL_MV_RAISE: snprintf(buf, cap, "raise by %d to %d", a, s->bid + a); break;
  case RL_MV_ACTION: snprintf(buf, cap, "%s", RL_ACTION_NAMES[a]); break;
  case RL_MV_PLACE: snprintf(buf, cap, "good to %s", mp->set_name[a]); break;
  case RL_MV_START: snprintf(buf, cap, "start a link from %s", mp->set_name[mp->hex_set[mp->nb[a][b]]]); break;
  case RL_MV_EXIT: snprintf(buf, cap, "lay a tile, out by edge %d", a); break;
  case RL_MV_SHARE: snprintf(buf, cap, "issue a share"); break;
  case RL_MV_CHARTER: snprintf(buf, cap, "charter a town"); break;
  case RL_MV_TOWN: snprintf(buf, cap, "charter %s", mp->set_name[a]); break;
  case RL_MV_COLOR: snprintf(buf, cap, "a %s city", RL_COLOR_NAMES[a]); break;
  case RL_MV_GOOD: snprintf(buf, cap, "a %s good in %s", RL_COLOR_NAMES[s->cube[a][b] - 1], mp->set_name[a]); break;
  case RL_MV_ENGINE: snprintf(buf, cap, "grow the engine"); break;
  case RL_MV_DEST: snprintf(buf, cap, "haul to %s", mp->set_name[a]); break;
  case RL_MV_INCOME: snprintf(buf, cap, "take it as income"); break;
  case RL_MV_POINTS: snprintf(buf, cap, "take it as points"); break;
  default: snprintf(buf, cap, "chance %d", k); break;
  }
}

void rl_cpu_options(int level, cw_search_options *o) {
  cw_search_level_options(level, CW_SEARCH_MCTS, o);
  o->playout_depth = 40;
  /* the margin reward (engine v0.84.0): a lead of reward_scale points is worth 0.73. CPU games between the levels end
   * about 10 points apart (median winning margin, 124 seeded games at two and three seats), so a fifth of that. */
  o->reward_scale = 2.0f;
}

int rl_next_tile(const rl_state *s, int seat, int *entry, int *from) {
  int e = -1, f = -1, h;
  if (s->phase == RL_PH_EXIT) pending(s, &h, &e, &f);
  else h = next_hex(s, seat, &e, &f);
  if (entry) *entry = e;
  if (from) *from = f;
  return h;
}
