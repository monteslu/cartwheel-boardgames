/* Kilowatt Country rules (see rules.h for the rules in words). */
#include "rules.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int bit(int seat) { return 1 << seat; }
static void advance(kc_state *s);

const char *kc_fuel_name(int fuel) {
  static const char *const N[KC_NFUEL] = { "coal", "oil", "garbage", "uranium" };
  return fuel >= 0 && fuel < KC_NFUEL ? N[fuel] : "?";
}
const char *kc_kind_name(int kind) {
  static const char *const N[6] = { "coal", "oil", "garbage", "uranium", "coal or oil", "wind and sun" };
  return kind >= 0 && kind < 6 ? N[kind] : "?";
}

/* ── shortest routes, per set of regions in play (a pure function of the mask, cached) ── */

static int16_t DIST[1 << KC_NREGION][KC_NCITY][KC_NCITY];
static bool DIST_READY[1 << KC_NREGION];

static void dist_build(int mask) {
  int16_t(*d)[KC_NCITY] = DIST[mask];
  for (int a = 0; a < KC_NCITY; a++)
    for (int b = 0; b < KC_NCITY; b++) d[a][b] = a == b ? 0 : 30000;
  for (int i = 0; i < KC_NLINK; i++) {
    const kc_link_def *l = &kc_links[i];
    if (!((mask >> kc_cities[l->a].region) & 1) || !((mask >> kc_cities[l->b].region) & 1)) continue;
    d[l->a][l->b] = d[l->b][l->a] = l->cost;
  }
  for (int k = 0; k < KC_NCITY; k++)
    for (int a = 0; a < KC_NCITY; a++)
      for (int b = 0; b < KC_NCITY; b++)
        if (d[a][k] + d[k][b] < d[a][b]) d[a][b] = (int16_t)(d[a][k] + d[k][b]);
  DIST_READY[mask] = true;
}
static const int16_t (*dist_of(int mask))[KC_NCITY] {
  if (!DIST_READY[mask]) dist_build(mask);
  return (const int16_t(*)[KC_NCITY])DIST[mask];
}

/* ── plants and fuel ─────────────────────────────────────────────────── */

int kc_nplants(const kc_player *p) {
  int n = 0;
  for (int i = 0; i < 3; i++) n += p->plants[i] >= 0;
  return n;
}
int kc_capacity(const kc_player *p) {
  int c = 0;
  for (int i = 0; i < 3; i++)
    if (p->plants[i] >= 0) c += kc_plants[p->plants[i]].cities;
  return c;
}
/* storage: twice each plant's need, by kind (hybrid separately) */
static void caps(const kc_player *p, int cap[6]) {
  for (int k = 0; k < 6; k++) cap[k] = 0;
  for (int i = 0; i < 3; i++)
    if (p->plants[i] >= 0) cap[kc_plants[p->plants[i]].kind] += 2 * kc_plants[p->plants[i]].need;
}
int kc_store_room(const kc_player *p, int fuel) {
  int c[6];
  caps(p, c);
  int coal = p->fuel[KC_COAL], oil = p->fuel[KC_OIL];
  switch (fuel) {
  case KC_COAL: return imax(0, imin(c[KC_K_COAL] + c[KC_K_HYBRID] - coal, c[KC_K_COAL] + c[KC_K_OIL] + c[KC_K_HYBRID] - coal - oil));
  case KC_OIL: return imax(0, imin(c[KC_K_OIL] + c[KC_K_HYBRID] - oil, c[KC_K_COAL] + c[KC_K_OIL] + c[KC_K_HYBRID] - coal - oil));
  case KC_GARBAGE: return imax(0, c[KC_K_GARBAGE] - p->fuel[KC_GARBAGE]);
  case KC_URANIUM: return imax(0, c[KC_K_URANIUM] - p->fuel[KC_URANIUM]);
  default: return 0;
  }
}
/* drop what the plants can no longer store (after a plant is replaced): back to the bank */
static void trim_fuel(kc_player *p) {
  int c[6];
  caps(p, c);
  p->fuel[KC_GARBAGE] = (int16_t)imin(p->fuel[KC_GARBAGE], c[KC_K_GARBAGE]);
  p->fuel[KC_URANIUM] = (int16_t)imin(p->fuel[KC_URANIUM], c[KC_K_URANIUM]);
  p->fuel[KC_COAL] = (int16_t)imin(p->fuel[KC_COAL], c[KC_K_COAL] + c[KC_K_HYBRID]);
  p->fuel[KC_OIL] = (int16_t)imin(p->fuel[KC_OIL], c[KC_K_OIL] + c[KC_K_HYBRID]);
  int over = p->fuel[KC_COAL] + p->fuel[KC_OIL] - (c[KC_K_COAL] + c[KC_K_OIL] + c[KC_K_HYBRID]);
  if (over > 0) {
    int o = imin(over, p->fuel[KC_OIL]);
    p->fuel[KC_OIL] -= (int16_t)o;
    p->fuel[KC_COAL] -= (int16_t)(over - o);
  }
}

int kc_power_plan(const kc_player *p, int *mask_out, int use_out[KC_NFUEL]) {
  int best = -1, best_fuel = 1 << 20, best_mask = 0, best_use[KC_NFUEL] = { 0 };
  for (int mask = 0; mask < 8; mask++) {
    int need[6] = { 0 }, cities = 0, fuel = 0;
    bool ok = true;
    for (int i = 0; i < 3; i++) {
      if (!((mask >> i) & 1)) continue;
      if (p->plants[i] < 0) { ok = false; break; }
      const kc_plant_def *d = &kc_plants[p->plants[i]];
      need[d->kind] += d->need;
      cities += d->cities;
      fuel += d->need;
    }
    if (!ok) continue;
    int coal = p->fuel[KC_COAL] - need[KC_K_COAL], oil = p->fuel[KC_OIL] - need[KC_K_OIL];
    if (coal < 0 || oil < 0 || p->fuel[KC_GARBAGE] < need[KC_K_GARBAGE] || p->fuel[KC_URANIUM] < need[KC_K_URANIUM]) continue;
    if (coal + oil < need[KC_K_HYBRID]) continue;
    int lit = imin(cities, p->ncity);
    if (lit > best || (lit == best && fuel < best_fuel)) {
      best = lit, best_fuel = fuel, best_mask = mask;
      int hc = imin(coal, need[KC_K_HYBRID]); /* a coal-or-oil plant burns coal first */
      best_use[KC_COAL] = need[KC_K_COAL] + hc;
      best_use[KC_OIL] = need[KC_K_OIL] + need[KC_K_HYBRID] - hc;
      best_use[KC_GARBAGE] = need[KC_K_GARBAGE];
      best_use[KC_URANIUM] = need[KC_K_URANIUM];
    }
  }
  if (mask_out) *mask_out = best_mask;
  if (use_out)
    for (int f = 0; f < KC_NFUEL; f++) use_out[f] = best_use[f];
  return best < 0 ? 0 : best;
}
int kc_can_power(const kc_player *p) { return kc_power_plan(p, NULL, NULL); }

int kc_fuel_needed(const kc_state *s, int seat, int fuel) {
  (void)s;
  const kc_player *p = &s->p[seat];
  int need[6] = { 0 };
  for (int i = 0; i < 3; i++)
    if (p->plants[i] >= 0) need[kc_plants[p->plants[i]].kind] += kc_plants[p->plants[i]].need;
  switch (fuel) {
  case KC_COAL: return imax(0, need[KC_K_COAL] + imax(0, need[KC_K_HYBRID] - imax(0, p->fuel[KC_OIL] - need[KC_K_OIL])) - p->fuel[KC_COAL]);
  case KC_OIL: return imax(0, need[KC_K_OIL] - p->fuel[KC_OIL]);
  case KC_GARBAGE: return imax(0, need[KC_K_GARBAGE] - p->fuel[KC_GARBAGE]);
  case KC_URANIUM: return imax(0, need[KC_K_URANIUM] - p->fuel[KC_URANIUM]);
  default: return 0;
  }
}

int kc_fuel_cost(const kc_state *s, int fuel, int k) {
  int m = s->fuel_mkt[fuel], S = kc_fuel_slots[fuel];
  if (k > m || k < 0) return -1;
  int c = 0;
  for (int j = 0; j < k; j++) c += kc_fuel_price_tab[fuel][S - m + j];
  return c;
}
int kc_fuel_price(const kc_state *s, int fuel) {
  int m = s->fuel_mkt[fuel], S = kc_fuel_slots[fuel];
  return m > 0 ? kc_fuel_price_tab[fuel][S - m] : 0;
}
int kc_bank(const kc_state *s, int fuel) {
  int t = kc_fuel_slots[fuel] - s->fuel_mkt[fuel];
  for (int i = 0; i < s->players; i++) t -= s->p[i].fuel[fuel];
  return t;
}

/* ── the map ─────────────────────────────────────────────────────────── */

bool kc_region_in(const kc_state *s, int region) { return (s->regions >> region) & 1; }
int kc_house_slot(const kc_state *s, int city) {
  int n = 0;
  for (int k = 0; k < 3; k++) n += s->occ[city][k] >= 0;
  return n;
}
bool kc_has_house(const kc_state *s, int seat, int city) {
  for (int k = 0; k < 3; k++)
    if (s->occ[city][k] == seat) return true;
  return false;
}
int kc_route_cost(const kc_state *s, int seat, int city) {
  if (s->p[seat].ncity == 0) return 0;
  const int16_t(*d)[KC_NCITY] = dist_of(s->regions);
  int best = 30000;
  for (int o = 0; o < KC_NCITY; o++)
    if (kc_has_house(s, seat, o) && d[o][city] < best) best = d[o][city];
  return best;
}
int kc_build_cost(const kc_state *s, int seat, int city, int *route) {
  if (route) *route = 0;
  if (!kc_region_in(s, kc_cities[city].region) || kc_has_house(s, seat, city)) return -1;
  int slot = kc_house_slot(s, city);
  if (slot >= s->step) return -1;
  int r = kc_route_cost(s, seat, city);
  if (r >= 30000) return -1;
  if (route) *route = r;
  return kc_house_cost[slot] + r;
}

int kc_end_cities(const kc_state *s) { return kc_end_tab[s->players]; }
int kc_step2_cities(const kc_state *s) { return kc_step2_tab[s->players]; }
static int max_cities(const kc_state *s) {
  int m = 0;
  for (int i = 0; i < s->players; i++) m = imax(m, s->p[i].ncity);
  return m;
}

/* ── the plant market ────────────────────────────────────────────────── */

int kc_market_open(const kc_state *s) { return s->step >= 3 ? s->nmarket : imin(4, s->nmarket); }
static int market_target(const kc_state *s) { return s->step >= 3 ? 6 : KC_MARKET; }
int kc_deck_count(const kc_state *s) {
  int n = 0;
  for (int i = 0; i < KC_NPLANT; i++) n += s->loc[i] == KC_L_DECK;
  return n;
}
static bool needs_draw(const kc_state *s) {
  if (s->phase == KC_PH_OVER || s->nmarket >= market_target(s)) return false;
  return kc_deck_count(s) > 0 || !s->step3_drawn;
}
static void market_insert(kc_state *s, int plant) {
  int i = s->nmarket;
  while (i > 0 && s->market[i - 1] > plant) {
    s->market[i] = s->market[i - 1];
    i--;
  }
  s->market[i] = (int16_t)plant;
  s->nmarket++;
  s->loc[plant] = KC_L_MARKET;
}
/* takes market slot i out (the plant's new place is the caller's) */
static int market_take(kc_state *s, int i) {
  int plant = s->market[i];
  for (int k = i; k < s->nmarket - 1; k++) s->market[k] = s->market[k + 1];
  s->nmarket--;
  s->market[s->nmarket] = -1;
  return plant;
}
static void market_drop_lowest(kc_state *s) {
  if (s->nmarket > 0) s->loc[market_take(s, 0)] = KC_L_OUT;
}
/* a plant leaves only when the deck can replace it (once the deck is spent, the market keeps what it has) */
static bool can_replace(const kc_state *s) { return kc_deck_count(s) > 0 || !s->step3_drawn; }
static void begin_step3(kc_state *s) {
  s->step3_drawn = 1;
  s->step = 3;
  market_drop_lowest(s);
  for (int i = 0; i < KC_NPLANT; i++)
    if (s->loc[i] == KC_L_UNDER) s->loc[i] = KC_L_DECK;
  while (s->nmarket > market_target(s)) s->loc[market_take(s, s->nmarket - 1)] = KC_L_UNDER; /* (never: a draw fills one hole) */
}

/* ── setup ───────────────────────────────────────────────────────────── */

void kc_init(kc_state *s, int players, uint64_t seed) {
  if (players < 2 || players > KC_MAX_P) CW_FATAL("kc_init: %d players (2..4)", players);
  memset(s, 0, sizeof *s);
  cw_rng r;
  cw_rng_seed(&r, seed);
  s->players = (int16_t)players;
  s->step = 1;
  s->round = 1;
  s->auc_plant = -1;
  s->disc_new = -1;
  s->last_draw = -1;
  /* the regions: a joined set of the right size */
  int k = kc_regions_used[players];
  const uint8_t *sets = k == 3 ? kc_region_sets_3 : k == 4 ? kc_region_sets_4 : kc_region_sets_5;
  int nsets = k == 3 ? (int)sizeof kc_region_sets_3 : k == 4 ? (int)sizeof kc_region_sets_4 : (int)sizeof kc_region_sets_5;
  s->regions = sets[cw_rng_int(&r, 0, nsets - 1)];
  for (int c = 0; c < KC_NCITY; c++)
    for (int j = 0; j < 3; j++) s->occ[c][j] = -1;
  /* plants: the eight lowest in the market, the rest in the deck, a few set aside */
  for (int i = 0; i < KC_MARKET; i++) s->market[i] = -1;
  for (int i = 0; i < KC_NPLANT; i++) {
    if (i < KC_MARKET) market_insert(s, i);
    else s->loc[i] = KC_L_DECK;
  }
  for (int out = 0; out < kc_plants_out[players];) {
    int i = cw_rng_int(&r, KC_MARKET, KC_NPLANT - 1);
    if (s->loc[i] == KC_L_DECK) s->loc[i] = KC_L_ASIDE, out++;
  }
  for (int f = 0; f < KC_NFUEL; f++) s->fuel_mkt[f] = kc_fuel_start[f];
  for (int i = 0; i < players; i++) {
    kc_player *p = &s->p[i];
    p->money = KC_START_MONEY;
    p->plants[0] = p->plants[1] = p->plants[2] = -1;
    s->order[i] = (int16_t)i;
  }
  for (int i = players - 1; i > 0; i--) { /* the first order: a shuffle */
    int j = cw_rng_int(&r, 0, i);
    int16_t t = s->order[i];
    s->order[i] = s->order[j];
    s->order[j] = t;
  }
  for (int i = players; i < KC_MAX_P; i++) s->order[i] = -1;
  s->phase = KC_PH_AUCTION;
  s->cur = -1;
  advance(s);
}

/* ── the round's flow ────────────────────────────────────────────────── */

static int biggest_plant(const kc_player *p) {
  int b = -1;
  for (int i = 0; i < 3; i++)
    if (p->plants[i] > b) b = p->plants[i];
  return b;
}
static void set_order(kc_state *s) {
  int n = s->players;
  for (int i = 0; i < n; i++) s->order[i] = (int16_t)i;
  for (int i = 1; i < n; i++) /* insertion sort: most cities, then the biggest plant, then the lower seat */
    for (int j = i; j > 0; j--) {
      const kc_player *a = &s->p[s->order[j - 1]], *b = &s->p[s->order[j]];
      bool swap = b->ncity > a->ncity || (b->ncity == a->ncity && biggest_plant(b) > biggest_plant(a));
      if (!swap) break;
      int16_t t = s->order[j];
      s->order[j] = s->order[j - 1];
      s->order[j - 1] = t;
    }
}

/* the next bidder after `from`, going round the table; the auction ends when only the high bidder is left */
static void next_bidder(kc_state *s, int from) {
  int n = s->players;
  for (int k = 1; k <= n; k++) {
    int seat = (from + k) % n;
    if (seat == s->auc_high) break;
    if (!(s->auc_in & bit(seat))) continue;
    if (s->p[seat].money < s->auc_bid + 1) { /* cannot raise: out */
      s->auc_in &= (int16_t)~bit(seat);
      continue;
    }
    s->cur = (int16_t)seat;
    return;
  }
  /* won */
  int w = s->auc_high;
  kc_player *p = &s->p[w];
  int slot = -1;
  for (int i = 0; i < s->nmarket; i++)
    if (s->market[i] == s->auc_plant) slot = i;
  if (slot < 0) CW_FATAL("kc: the auctioned plant %d is not in the market", s->auc_plant);
  market_take(s, slot);
  s->loc[s->auc_plant] = KC_L_OWNED;
  p->money -= s->auc_bid;
  p->spent += s->auc_bid;
  s->bought |= (int16_t)bit(w);
  int free_slot = -1;
  for (int i = 2; i >= 0; i--)
    if (p->plants[i] < 0) free_slot = i;
  if (free_slot >= 0) {
    p->plants[free_slot] = s->auc_plant;
  } else {
    s->phase = KC_PH_DISCARD;
    s->disc_new = s->auc_plant;
    s->cur = (int16_t)w;
  }
  s->auc_plant = -1;
}

static void end_build(kc_state *s);

/* runs everything that needs no decision, until a seat must decide, a draw is due or the game is over */
static void advance(kc_state *s) {
  for (int guard = 0; guard < 64; guard++) {
    if (s->phase == KC_PH_OVER || needs_draw(s)) return;
    switch (s->phase) {
    case KC_PH_DISCARD: return;
    case KC_PH_AUCTION:
      if (s->auc_plant >= 0) return; /* a bidder decides */
      s->cur = -1;
      for (int i = 0; i < s->players; i++) {
        int seat = s->order[i];
        if (!((s->bought | s->passed) & bit(seat))) { s->cur = (int16_t)seat; break; }
      }
      if (s->cur >= 0) return;
      /* the auction is over */
      if (s->bought == 0 && can_replace(s)) market_drop_lowest(s);
      if (s->round == 1) set_order(s);
      s->phase = KC_PH_FUEL;
      s->oi = (int16_t)(s->players - 1);
      break;
    case KC_PH_FUEL: {
      if (s->oi < 0) {
        s->phase = KC_PH_BUILD;
        s->oi = (int16_t)(s->players - 1);
        break;
      }
      int seat = s->order[s->oi];
      bool any = false;
      for (int f = 0; f < KC_NFUEL && !any; f++)
        any = s->fuel_mkt[f] > 0 && kc_store_room(&s->p[seat], f) > 0 && s->p[seat].money >= kc_fuel_price(s, f);
      if (!any) { s->oi--; break; }
      s->cur = (int16_t)seat;
      return;
    }
    case KC_PH_BUILD: {
      if (s->oi < 0) { end_build(s); break; }
      int seat = s->order[s->oi];
      bool any = false;
      for (int c = 0; c < KC_NCITY && !any; c++) {
        int cost = kc_build_cost(s, seat, c, NULL);
        any = cost >= 0 && cost <= s->p[seat].money;
      }
      if (!any) { /* nothing to build: the turn ends as a done would */
        int mc = max_cities(s);
        while (s->nmarket > 0 && can_replace(s) && kc_plants[s->market[0]].number <= mc) market_drop_lowest(s);
        s->oi--;
        break;
      }
      s->cur = (int16_t)seat;
      return;
    }
    default: CW_FATAL("kc: phase %d", s->phase);
    }
  }
  CW_FATAL("kc: the round never reached a decision (phase %d)", s->phase);
}

static void end_build(kc_state *s) {
  int n = s->players;
  if (s->step == 1 && max_cities(s) >= kc_step2_tab[n]) {
    s->step = 2;
    market_drop_lowest(s);
  }
  bool ending = max_cities(s) >= kc_end_tab[n] || s->round >= KC_MAX_ROUNDS;
  for (int i = 0; i < n; i++) { /* power */
    kc_player *p = &s->p[i];
    int use[KC_NFUEL];
    int lit = kc_power_plan(p, NULL, use);
    for (int f = 0; f < KC_NFUEL; f++) p->fuel[f] -= (int16_t)use[f];
    p->powered = (int16_t)lit;
    p->earned = kc_income[imin(lit, KC_INCOME_MAX)];
    p->money += p->earned;
  }
  if (ending) {
    s->ending = 1;
    s->phase = KC_PH_OVER;
    s->cur = -1;
    return;
  }
  const int8_t *sup = kc_supply[n - 2][s->step - 1];
  for (int f = 0; f < KC_NFUEL; f++) s->fuel_mkt[f] += (int16_t)imin(sup[f], kc_bank(s, f));
  if (s->nmarket > 0) {
    if (s->step >= 3) { if (can_replace(s)) market_drop_lowest(s); }
    else s->loc[market_take(s, s->nmarket - 1)] = KC_L_UNDER;
  }
  s->round++;
  set_order(s);
  s->bought = s->passed = 0;
  for (int i = 0; i < n; i++) s->p[i].spent = 0;
  s->phase = KC_PH_AUCTION;
  s->cur = -1;
}

/* ── the bk_game callbacks ───────────────────────────────────────────── */

int kc_turn(const void *state, void *user) {
  (void)user;
  const kc_state *s = state;
  return needs_draw(s) ? BK_CHANCE : s->cur;
}
bool kc_terminal(const void *state, void *user) {
  (void)user;
  return ((const kc_state *)state)->phase == KC_PH_OVER;
}

int kc_moves(const void *state, bk_move *out, int cap, void *user) {
  (void)user;
  const kc_state *s = state;
  int n = 0, seat = s->cur;
#define ADD(m) do { if (n < cap) out[n++] = (m); } while (0)
  if (s->phase == KC_PH_OVER || needs_draw(s) || seat < 0) return 0;
  const kc_player *p = &s->p[seat];
  switch (s->phase) {
  case KC_PH_AUCTION:
    if (s->auc_plant < 0) {
      for (int i = 0; i < kc_market_open(s); i++)
        if (p->money >= kc_plants[s->market[i]].number) ADD(bk_move_pack(KC_M_OPEN, i, 0, 0, 0, 0, 0, 0));
      if (s->round > 1 || n == 0) ADD(bk_move_pack(KC_M_PASS, 0, 0, 0, 0, 0, 0, 0));
    } else {
      for (int k = 0; k < KC_NRAISE; k++)
        if (s->auc_bid + KC_RAISES[k] <= p->money) ADD(bk_move_pack(KC_M_RAISE, k, 0, 0, 0, 0, 0, 0));
      ADD(bk_move_pack(KC_M_DROP, 0, 0, 0, 0, 0, 0, 0));
    }
    break;
  case KC_PH_DISCARD:
    for (int i = 0; i < 3; i++) ADD(bk_move_pack(KC_M_DISCARD, i, 0, 0, 0, 0, 0, 0));
    break;
  case KC_PH_FUEL:
    for (int f = 0; f < KC_NFUEL; f++) {
      int room = imin(kc_store_room(p, f), s->fuel_mkt[f]);
      for (int k = 1; k <= room; k++) {
        if (kc_fuel_cost(s, f, k) > p->money) break;
        ADD(bk_move_pack(KC_M_FUEL, f, k, 0, 0, 0, 0, 0));
      }
    }
    ADD(bk_move_pack(KC_M_DONE, 0, 0, 0, 0, 0, 0, 0));
    break;
  case KC_PH_BUILD:
    for (int c = 0; c < KC_NCITY; c++) {
      int cost = kc_build_cost(s, seat, c, NULL);
      if (cost >= 0 && cost <= p->money) ADD(bk_move_pack(KC_M_BUILD, c, 0, 0, 0, 0, 0, 0));
    }
    ADD(bk_move_pack(KC_M_DONE, 0, 0, 0, 0, 0, 0, 0));
    break;
  default: break;
  }
#undef ADD
  return n;
}

void kc_apply(void *state, bk_move m, void *user) {
  (void)user;
  kc_state *s = state;
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  if (kind == KC_M_DRAW) {
    if (!needs_draw(s)) CW_FATAL("kc: a draw with none due");
    if (a == KC_DRAW_STEP3) {
      if (kc_deck_count(s) > 0 || s->step3_drawn) CW_FATAL("kc: step 3 drawn early");
      s->last_draw = -1;
      begin_step3(s);
    } else {
      if (a >= KC_NPLANT || s->loc[a] != KC_L_DECK) CW_FATAL("kc: drew plant %d, not in the deck", a);
      s->last_draw = (int16_t)a;
      market_insert(s, a);
    }
    advance(s);
    return;
  }
  int seat = s->cur;
  if (seat < 0) CW_FATAL("kc: a move with nobody to move");
  kc_player *p = &s->p[seat];
  s->moves_played++;
  switch (kind) {
  case KC_M_OPEN:
    s->auc_plant = s->market[a];
    s->auc_bid = kc_plants[s->auc_plant].number;
    s->auc_high = (int16_t)seat;
    s->auc_opener = (int16_t)seat;
    s->auc_in = 0;
    for (int i = 0; i < s->players; i++)
      if (!((s->bought | s->passed) & bit(i))) s->auc_in |= (int16_t)bit(i);
    next_bidder(s, seat);
    break;
  case KC_M_PASS: s->passed |= (int16_t)bit(seat); break;
  case KC_M_RAISE:
    s->auc_bid += KC_RAISES[a];
    s->auc_high = (int16_t)seat;
    next_bidder(s, seat);
    break;
  case KC_M_DROP:
    s->auc_in &= (int16_t)~bit(seat);
    next_bidder(s, seat);
    break;
  case KC_M_DISCARD:
    s->loc[p->plants[a]] = KC_L_OUT;
    p->plants[a] = s->disc_new;
    s->disc_new = -1;
    trim_fuel(p);
    s->phase = KC_PH_AUCTION;
    break;
  case KC_M_FUEL: {
    int cost = kc_fuel_cost(s, a, b);
    p->money -= (int16_t)cost;
    p->spent += (int16_t)cost;
    s->fuel_mkt[a] -= (int16_t)b;
    p->fuel[a] += (int16_t)b;
    break;
  }
  case KC_M_BUILD: {
    int cost = kc_build_cost(s, seat, a, NULL);
    if (cost < 0) CW_FATAL("kc: cannot build in city %d", a);
    s->occ[a][kc_house_slot(s, a)] = (int8_t)seat;
    p->money -= (int16_t)cost;
    p->spent += (int16_t)cost;
    p->ncity++;
    break;
  }
  case KC_M_DONE:
    if (s->phase == KC_PH_BUILD) {
      int mc = max_cities(s);
      while (s->nmarket > 0 && can_replace(s) && kc_plants[s->market[0]].number <= mc) market_drop_lowest(s);
    }
    s->oi--;
    break;
  default: CW_FATAL("kc: move kind %d", kind);
  }
  advance(s);
}

int kc_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user) {
  (void)user;
  const kc_state *s = state;
  int n = 0;
  for (int i = 0; i < KC_NPLANT && n < cap; i++)
    if (s->loc[i] == KC_L_DECK) out[n] = bk_move_pack(KC_M_DRAW, i, 0, 0, 0, 0, 0, 0), weight[n++] = 1;
  if (n == 0 && !s->step3_drawn && cap > 0) out[n] = bk_move_pack(KC_M_DRAW, KC_DRAW_STEP3, 0, 0, 0, 0, 0, 0), weight[n++] = 1;
  return n;
}
void kc_chance(void *state, cw_rng *rng, void *user) {
  bk_move mv[KC_NPLANT + 1];
  uint32_t w[KC_NPLANT + 1];
  int n = kc_outcomes(state, mv, w, KC_NPLANT + 1, user);
  if (n <= 0) CW_FATAL("kc_chance: nothing to draw");
  kc_apply(state, mv[cw_rng_int(rng, 0, n - 1)], user);
}

/* ── results and the CPU's sense of value ────────────────────────────── */

int kc_winner(const kc_state *s) {
  if (s->phase != KC_PH_OVER) return -1;
  int w = 0;
  for (int i = 1; i < s->players; i++) {
    const kc_player *a = &s->p[i], *b = &s->p[w];
    if (a->powered > b->powered || (a->powered == b->powered && (a->money > b->money || (a->money == b->money && a->ncity > b->ncity)))) w = i;
  }
  return w;
}

int kc_plant_worth(const kc_state *s, int seat, int plant) {
  const kc_player *p = &s->p[seat];
  const kc_plant_def *d = &kc_plants[plant];
  int cap = kc_capacity(p), weakest = 0, np = kc_nplants(p);
  if (np == 3) {
    weakest = 99;
    for (int i = 0; i < 3; i++) weakest = imin(weakest, kc_plants[p->plants[i]].cities);
  }
  int target = kc_end_cities(s) + 1;
  int now = imin(cap, target), after = imin(cap - weakest + d->cities, target);
  int gain = after - now;
  if (gain <= 0) return d->number - 1;
  int fuel_price = d->kind == KC_K_GREEN ? 0 : d->kind == KC_K_HYBRID ? imin(kc_fuel_price(s, KC_COAL), kc_fuel_price(s, KC_OIL)) : kc_fuel_price(s, d->kind);
  if (d->kind != KC_K_GREEN && d->kind != KC_K_HYBRID && s->fuel_mkt[d->kind] < d->need) fuel_price = 12;
  int w = d->number + gain * 9 - d->need * fuel_price / 2 + (d->kind == KC_K_GREEN ? 4 : 0);
  int reserve = s->round == 1 ? 14 : 10;
  return imin(w, p->money - reserve);
}

static float rounds_left(const kc_state *s) {
  int need = kc_end_cities(s) - max_cities(s);
  float r = need <= 0 ? 0.5f : need / 2.2f;
  if (r > (float)(KC_MAX_ROUNDS - s->round)) r = (float)(KC_MAX_ROUNDS - s->round);
  return r < 0.5f ? 0.5f : r;
}

void kc_evaluate(const void *state, float score[BK_MAX_SEATS], void *user) {
  (void)user;
  const kc_state *s = state;
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  if (s->phase == KC_PH_OVER) {
    int w = kc_winner(s);
    for (int i = 0; i < s->players; i++)
      score[i] = i == w ? BK_WIN - (float)s->moves_played : BK_LOSS + (float)s->p[i].powered * 100.0f + (float)s->p[i].money * 0.1f;
    return;
  }
  int E = kc_end_cities(s);
  float rl = rounds_left(s);
  float late = 1.0f - fminf(1.0f, rl / 4.0f); /* 0 early, 1 in the last rounds */
  for (int i = 0; i < s->players; i++) {
    const kc_player *p = &s->p[i];
    int cap = kc_capacity(p), lit = kc_can_power(p);
    float v = 0;
    v += 20.0f * (float)imin(p->ncity, E + 2);
    v += 8.0f * (float)imin(cap, imin(E + 1, p->ncity + 3));
    v += 0.5f * (float)p->money;
    /* next round's pay: cities lit with the fuel in hand (or with what this round still lets it buy) */
    int expect = s->phase <= KC_PH_FUEL ? imin(cap, p->ncity) : lit;
    v += 0.35f * rl * (float)kc_income[imin(expect, KC_INCOME_MAX)] * (1.0f - late);
    /* the result: the cities it can light at the end */
    v += 30.0f * late * (float)imin(p->ncity, cap);
    /* fuel in store is worth about its price */
    for (int f = 0; f < KC_NFUEL; f++) v += 2.0f * (float)p->fuel[f];
    score[i] = v;
  }
}

float kc_priority(const void *state, bk_move m, void *user) {
  (void)state, (void)user;
  switch (bk_move_field(m, 0)) {
  case KC_M_BUILD: return 3;
  case KC_M_OPEN: return 2;
  case KC_M_FUEL: return 1.5f;
  default: return 1;
  }
}

/* a light sense of a good move, for playouts (and a person's starting cursor) */
bk_move kc_playout(const void *state, const bk_move *moves, int n, cw_rng *rng, void *user) {
  (void)user;
  const kc_state *s = state;
  if (n <= 0) CW_FATAL("kc_playout: no moves (phase %d)", s->phase);
  int seat = s->cur;
  const kc_player *p = &s->p[seat];
  int kind0 = bk_move_field(moves[0], 0);
  switch (s->phase) {
  case KC_PH_AUCTION:
    if (kind0 == KC_M_RAISE || kind0 == KC_M_DROP) {
      int worth = kc_plant_worth(s, seat, s->auc_plant) + cw_rng_int(rng, -3, 3);
      if (kind0 == KC_M_RAISE && s->auc_bid + 1 <= worth) return moves[0];
      return moves[n - 1];
    } else {
      int best = -1, bv = -1000;
      for (int i = 0; i < n; i++) {
        if (bk_move_field(moves[i], 0) != KC_M_OPEN) continue;
        int pl = s->market[bk_move_field(moves[i], 1)];
        int v = kc_plant_worth(s, seat, pl) - kc_plants[pl].number + cw_rng_int(rng, 0, 3);
        if (v > bv) bv = v, best = i;
      }
      bool can_pass = bk_move_field(moves[n - 1], 0) == KC_M_PASS;
      if (best >= 0 && (bv > 0 || !can_pass || kc_nplants(p) == 0)) return moves[best];
      return moves[n - 1];
    }
  case KC_PH_DISCARD: {
    int lo = 0;
    for (int i = 1; i < 3; i++)
      if (kc_plants[p->plants[i]].cities < kc_plants[p->plants[lo]].cities) lo = i;
    return bk_move_pack(KC_M_DISCARD, lo, 0, 0, 0, 0, 0, 0);
  }
  case KC_PH_FUEL:
    for (int i = 0; i < n; i++) {
      if (bk_move_field(moves[i], 0) != KC_M_FUEL) continue;
      int f = bk_move_field(moves[i], 1), k = bk_move_field(moves[i], 2);
      int short_ = kc_fuel_needed(s, seat, f);
      if (short_ > 0 && (k == short_ || (i + 1 < n && bk_move_field(moves[i + 1], 1) != f))) return moves[i];
    }
    return moves[n - 1];
  case KC_PH_BUILD: {
    int cap = kc_capacity(p);
    if (p->ncity >= cap + 1 && p->ncity >= 1) return moves[n - 1];
    int best = -1, bc = 1 << 20;
    for (int i = 0; i < n - 1; i++) {
      int c = bk_move_field(moves[i], 1);
      int cost = kc_build_cost(s, seat, c, NULL) + cw_rng_int(rng, 0, 4);
      if (cost < bc) bc = cost, best = i;
    }
    if (best >= 0 && p->money - bc >= (s->round == 1 ? 0 : 6)) return moves[best];
    return moves[n - 1];
  }
  default: return moves[cw_rng_int(rng, 0, n - 1)];
  }
}

void kc_label(const void *state, bk_move m, char *buf, size_t cap, void *user) {
  (void)user;
  const kc_state *s = state;
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  switch (kind) {
  case KC_M_OPEN: snprintf(buf, cap, "auction plant %d", s->market[a] >= 0 ? kc_plants[s->market[a]].number : -1); break;
  case KC_M_PASS: snprintf(buf, cap, "pass"); break;
  case KC_M_RAISE: snprintf(buf, cap, "bid %d", s->auc_bid + KC_RAISES[a]); break;
  case KC_M_DROP: snprintf(buf, cap, "drop out"); break;
  case KC_M_DISCARD: snprintf(buf, cap, "scrap plant %d", s->cur >= 0 && s->p[s->cur].plants[a] >= 0 ? kc_plants[s->p[s->cur].plants[a]].number : -1); break;
  case KC_M_FUEL: snprintf(buf, cap, "buy %d %s", b, kc_fuel_name(a)); break;
  case KC_M_DONE: snprintf(buf, cap, "done"); break;
  case KC_M_BUILD: snprintf(buf, cap, "build in %s", kc_cities[a].name); break;
  case KC_M_DRAW: snprintf(buf, cap, a == KC_DRAW_STEP3 ? "step 3" : "draw plant %d", a < KC_NPLANT ? kc_plants[a].number : 0); break;
  default: snprintf(buf, cap, "?"); break;
  }
}

uint64_t kc_hash(const kc_state *s) { return cw_hash_bytes(s, sizeof *s, 0); }

void kc_cpu_options(int level, uint64_t seed, cw_search_options *o) {
  /* Easy searches little and slips often: on the margin reward it otherwise stayed close to Normal at three seats */
  static const int nodes[3] = { 1500, 30000, 120000 }, depth[3] = { 6, 16, 24 };
  static const float mistake[3] = { 0.3f, 0.06f, 0 };
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  cw_search_level_options(level, CW_SEARCH_MCTS, o);
  o->seed = seed;
  o->max_nodes = nodes[level];
  o->playout_depth = depth[level];
  o->mistake = mistake[level];
  o->mistake_spread = level == 0 ? 3 : 2;
  /* the margin reward (the default): a lead of 20 in evaluate's units is worth 0.73. The winner's lead over the best
   * other seat on the last state before the end, in seeded games between the levels at 2, 3 and 4 seats
   * (tests/cpu_bench.c), has a median of 80 to 120: about a fifth of that. Scales 4, 10, 20 and 40 were measured;
   * 20 kept the levels furthest apart */
  o->reward_scale = 20;
}

void kc_game(bk_game *g, int players) {
  memset(g, 0, sizeof *g);
  g->players = players;
  g->state_size = sizeof(kc_state);
  g->moves = kc_moves;
  g->apply = kc_apply;
  g->turn = kc_turn;
  g->terminal = kc_terminal;
  g->evaluate = kc_evaluate;
  g->chance = kc_chance;
  g->outcomes = kc_outcomes;
  g->priority = kc_priority;
  g->playout = kc_playout;
}
