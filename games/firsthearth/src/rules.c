/* First Hearth rules (see firsthearth_rules.h for the rules in words). */
#include "rules.h"

#include <stdio.h>
#include <string.h>

static const int16_t START_SUPPLY[TR_NRES] = {0, 28, 18, 12, 10}; /* food is unlimited */

static int goods_of(const tr_player *p) { return p->res[TR_WOOD] + p->res[TR_CLAY] + p->res[TR_STONE] + p->res[TR_GOLD]; }

int tr_tool_total(const tr_player *p) {
  int t = 0;
  for (int i = 0; i < p->ntools; i++) t += p->tools[i];
  return t;
}

static bool tools_maxed(const tr_player *p) { return p->ntools == 3 && p->tools[0] == 4 && p->tools[1] == 4 && p->tools[2] == 4; }

static void improve_tool(tr_player *p) {
  if (p->ntools < 3) { p->tools[p->ntools++] = 1; return; }
  int lo = 0;
  for (int i = 1; i < 3; i++)
    if (p->tools[i] < p->tools[lo]) lo = i;
  if (p->tools[lo] < 4) p->tools[lo]++;
}

void tr_init(tr_state *s, int players, uint64_t seed) {
  if (players < 2 || players > TR_MAX_P) CW_FATAL("tr_init: %d players (2..4)", players);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = (int16_t)players;
  s->phase = TR_PH_DEAL;
  s->round = 1;
  for (int r = 0; r < TR_NRES; r++) s->supply[r] = START_SUPPLY[r];
  for (int i = 0; i < TR_NCARDS; i++) s->card_left[i] = 1;
  for (int i = 0; i < TR_NBUILD; i++) s->bld_left[i] = 1, s->bld_owner[i] = -1;
  for (int i = 0; i < TR_NCARDS; i++) s->card_owner[i] = -1;
  for (int i = 0; i < 4; i++) {
    s->card_slot[i] = -1;
    s->bld_top[i] = -1;
    s->bld_count[i] = i < players ? TR_PILE : 0;
  }
  for (int i = 0; i < players; i++) {
    tr_player *p = &s->p[i];
    p->kin = p->avail = TR_START_KIN;
    p->res[TR_FOOD] = 12;
  }
}

/* ── placement ──────────────────────────────────────────────────────── */

static int site_seats(const tr_state *s, int site) {
  int n = 0;
  for (int i = 0; i < s->players; i++) n += s->place[site][i] > 0;
  return n;
}
static int site_kin(const tr_state *s, int site) {
  int n = 0;
  for (int i = 0; i < s->players; i++) n += s->place[site][i];
  return n;
}
static int village_used(const tr_state *s) {
  return (site_seats(s, TR_S_HUT) > 0) + (site_seats(s, TR_S_FIELD) > 0) + (site_seats(s, TR_S_TOOL) > 0);
}

int tr_site_max(const tr_state *s, int seat, int site) {
  const tr_player *p = &s->p[seat];
  if (p->avail <= 0) return 0;
  if (site == TR_S_HUNT) return p->avail; /* a player may add kin to their own hunting party */
  if (s->place[site][seat] > 0) return 0;
  if (site <= TR_S_GOLD) {
    if (s->players < 4 && site_seats(s, site) >= s->players - 1) return 0;
    int room = TR_SITE_CAP - site_kin(s, site);
    return room < p->avail ? room : p->avail;
  }
  if (site <= TR_S_TOOL) {
    if (site_seats(s, site) > 0) return 0;
    if (s->players < 4 && village_used(s) >= 2) return 0;
    if (site == TR_S_HUT) return (p->avail >= 2 && p->kin < TR_MAX_KIN) ? 2 : 0;
    if (site == TR_S_FIELD) return p->field < TR_MAX_FIELD ? 1 : 0;
    return tools_maxed(p) ? 0 : 1;
  }
  if (site <= TR_S_CARD3) return (s->card_slot[site - TR_S_CARD0] >= 0 && site_seats(s, site) == 0) ? 1 : 0;
  int pile = site - TR_S_BLD0;
  return (pile < s->players && s->bld_top[pile] >= 0 && site_seats(s, site) == 0) ? 1 : 0;
}

bool tr_site_open(const tr_state *s, int seat, int site) { return tr_site_max(s, seat, site) > 0; }

/* ── costs ──────────────────────────────────────────────────────────── */

bool tr_can_afford_card(const tr_player *p, int slot) { return goods_of(p) >= slot + 1; }

bool tr_can_afford_build(const tr_player *p, int b) {
  for (int i = 0; i < 4; i++)
    if (p->res[TR_WOOD + i] < tr_builds[b].cost[i]) return false;
  return true;
}

static void pay_cheapest(tr_state *s, tr_player *p, int n) {
  for (int r = TR_WOOD; r <= TR_GOLD && n > 0; r++) {
    int take = p->res[r] < n ? p->res[r] : n;
    p->res[r] -= take;
    s->supply[r] += take;
    n -= take;
  }
  if (n > 0) CW_FATAL("pay_cheapest: %d goods short", n);
}

static void gain(tr_state *s, tr_player *p, int r, int n) {
  if (r != TR_FOOD && n > s->supply[r]) n = s->supply[r];
  p->res[r] += n;
  if (r != TR_FOOD) s->supply[r] -= n;
}

static void card_effect(tr_state *s, tr_player *p, int kind, int n) {
  switch (kind) {
    case 0: case 1: case 2: case 3: case 4: gain(s, p, kind, n); break;
    case 5: p->score += n; break;
    case 6: for (int i = 0; i < n; i++) if (p->field < TR_MAX_FIELD) p->field++; break;
    case 7: for (int i = 0; i < n; i++) improve_tool(p); break;
    default: break;
  }
}

/* ── gathering: which tool sets are worth offering ──────────────────── */

static int gather_cap(const tr_state *s, int site) { return site == TR_S_HUNT ? 1000 : s->supply[site]; }

/* masks: 0 first, then for each larger gain the cheapest set of unused tools that reaches it */
static int tool_options(const tr_state *s, int masks[8]) {
  const tr_player *p = &s->p[s->cur];
  int site = s->step, v = tr_value(site), cap = gather_cap(s, site);
  int base = s->pend / v;
  if (base > cap) base = cap;
  int n = 0;
  masks[n++] = 0;
  int best_t[64];
  int best_m[64];
  for (int g = 0; g < 64; g++) best_t[g] = 1 << 20, best_m[g] = -1;
  for (int m = 1; m < (1 << p->ntools); m++) {
    if (m & p->tool_used) continue;
    int t = 0;
    for (int i = 0; i < p->ntools; i++)
      if (m & (1 << i)) t += p->tools[i];
    int g = (s->pend + t) / v;
    if (g > cap) g = cap;
    if (g <= base || g >= 64) continue;
    if (t < best_t[g]) best_t[g] = t, best_m[g] = m;
  }
  /* a smaller gain is dropped when a larger one costs no more tool strength (it dominates) */
  int keep[64], nk = 0, run = 1 << 20;
  for (int g = 63; g > base; g--)
    if (best_m[g] >= 0 && best_t[g] < run) {
      run = best_t[g];
      keep[nk++] = g;
    }
  for (int i = nk - 1; i >= 0 && n < 8; i--) masks[n++] = best_m[keep[i]];
  return n;
}

static void collect(tr_state *s, int mask) {
  tr_player *p = &s->p[s->cur];
  int site = s->step, v = tr_value(site);
  int t = 0;
  for (int i = 0; i < p->ntools; i++)
    if (mask & (1 << i)) t += p->tools[i];
  p->tool_used |= (int16_t)mask;
  int before = p->res[site];
  gain(s, p, site, (s->pend + t) / v);
  s->last_event[0] = 1, s->last_event[1] = (int16_t)site, s->last_event[2] = (int16_t)(p->res[site] - before),
  s->last_event[3] = s->cur;
  s->place[site][s->cur] = 0;
  s->pend = 0;
  s->need = TR_NEED_NONE;
  s->step++;
}

/* ── the flow ───────────────────────────────────────────────────────── */

static bool deal_pending(const tr_state *s) {
  int deck = 0;
  for (int i = 0; i < TR_NCARDS; i++) deck += s->card_left[i];
  for (int i = 0; i < 4; i++)
    if (s->card_slot[i] < 0 && deck > 0) return true;
  for (int i = 0; i < s->players; i++)
    if (s->bld_count[i] > 0 && s->bld_top[i] < 0) return true;
  return false;
}

static void round_end(tr_state *s) {
  memset(s->place, 0, sizeof s->place);
  for (int i = 0; i < s->players; i++) {
    s->p[i].avail = s->p[i].kin;
    s->p[i].tool_used = 0;
  }
  /* the cheapest card still on offer goes to the fire; the rest slide to the cheap end */
  s->last_burn = -1;
  for (int i = 0; i < 4; i++)
    if (s->card_slot[i] >= 0) {
      s->last_burn = s->card_slot[i];
      s->card_slot[i] = -1;
      break;
    }
  int16_t kept[4], k = 0;
  for (int i = 0; i < 4; i++)
    if (s->card_slot[i] >= 0) kept[k++] = s->card_slot[i];
  for (int i = 0; i < 4; i++) s->card_slot[i] = i < k ? kept[i] : -1;
  int deck = 0;
  for (int i = 0; i < TR_NCARDS; i++) deck += s->card_left[i];
  int reason = TR_END_NONE;
  if (s->round >= TR_MAX_ROUNDS) reason = TR_END_MOONS;
  if (deck < 4 - k) reason = TR_END_CARDS;
  for (int i = 0; i < s->players; i++)
    if (s->bld_count[i] == 0) reason = TR_END_PILE;
  if (reason != TR_END_NONE) {
    s->end_reason = (int16_t)reason;
    s->phase = TR_PH_OVER;
    return;
  }
  s->round++;
  s->first = (int16_t)((s->first + 1) % s->players);
  s->cur = s->first;
  s->phase = TR_PH_DEAL;
}

static void feed_auto(tr_state *s, tr_player *p, bool pay_goods) {
  int short_ = p->kin - p->res[TR_FOOD];
  if (short_ <= 0) {
    p->res[TR_FOOD] -= p->kin;
  } else if (pay_goods) {
    p->res[TR_FOOD] = 0;
    pay_cheapest(s, p, short_);
  } else {
    p->res[TR_FOOD] = 0;
    p->score -= TR_HUNGER;
    p->hungry++;
  }
}

/* run every automatic step until a decision, a chance event or the end */
static void advance(tr_state *s) {
  for (;;) {
    switch (s->phase) {
      case TR_PH_OVER: return;
      case TR_PH_DEAL:
        if (deal_pending(s)) return;
        s->phase = TR_PH_PLACE;
        s->cur = s->first;
        continue;
      case TR_PH_PLACE: {
        int n = s->players;
        for (int k = 0; k < n; k++) {
          int seat = (s->cur + k) % n;
          if (s->p[seat].avail > 0) {
            s->cur = (int16_t)seat;
            return;
          }
        }
        s->phase = TR_PH_RESOLVE;
        s->cur = s->first;
        s->step = 0;
        s->need = TR_NEED_NONE;
        continue;
      }
      case TR_PH_RESOLVE: {
        if (s->need != TR_NEED_NONE) return;
        if (s->step >= TR_NSITES) {
          s->cur = (int16_t)((s->cur + 1) % s->players);
          s->step = 0;
          if (s->cur == s->first) {
            s->phase = TR_PH_FEED;
            s->need = TR_NEED_NONE;
          }
          continue;
        }
        int site = s->step, seat = s->cur;
        tr_player *p = &s->p[seat];
        if (s->place[site][seat] == 0) {
          s->step++;
          continue;
        }
        if (site <= TR_S_GOLD) {
          s->need = TR_NEED_ROLL;
          return;
        }
        if (site <= TR_S_TOOL) {
          if (site == TR_S_HUT && p->kin < TR_MAX_KIN) p->kin++;
          if (site == TR_S_FIELD && p->field < TR_MAX_FIELD) p->field++;
          if (site == TR_S_TOOL) improve_tool(p);
          s->last_event[0] = 2, s->last_event[1] = (int16_t)site, s->last_event[2] = 1, s->last_event[3] = (int16_t)seat;
          s->place[site][seat] = 0;
          s->step++;
          continue;
        }
        bool ok = site <= TR_S_CARD3 ? (s->card_slot[site - TR_S_CARD0] >= 0 && tr_can_afford_card(p, site - TR_S_CARD0))
                                     : (s->bld_top[site - TR_S_BLD0] >= 0 && tr_can_afford_build(p, s->bld_top[site - TR_S_BLD0]));
        if (ok) {
          s->need = TR_NEED_BUY;
          return;
        }
        s->place[site][seat] = 0;
        s->step++;
        continue;
      }
      case TR_PH_FEED: {
        if (s->need != TR_NEED_NONE) return;
        tr_player *p = &s->p[s->cur];
        if (s->step == 0) { /* field income once per seat */
          p->res[TR_FOOD] += p->field;
          s->step = 1;
        }
        int short_ = p->kin - p->res[TR_FOOD];
        if (short_ > 0 && goods_of(p) >= short_) {
          s->need = TR_NEED_FEED;
          return;
        }
        feed_auto(s, p, false);
        s->step = 0;
        s->cur = (int16_t)((s->cur + 1) % s->players);
        if (s->cur == s->first) round_end(s);
        continue;
      }
      default: CW_FATAL("firsthearth: bad phase %d", s->phase);
    }
  }
}

int tr_turn(const void *state, void *user) {
  (void)user;
  const tr_state *s = state;
  if (s->phase == TR_PH_DEAL) return BK_CHANCE;
  if (s->phase == TR_PH_RESOLVE && s->need == TR_NEED_ROLL) return BK_CHANCE;
  return s->cur;
}

bool tr_terminal(const void *state, void *user) {
  (void)user;
  return ((const tr_state *)state)->phase == TR_PH_OVER;
}

int tr_moves(const void *state, bk_move *out, int cap, void *user) {
  (void)user;
  const tr_state *s = state;
  int n = 0;
#define PUSH(m) do { if (n >= cap) CW_FATAL("tr_moves: more than %d moves", cap); out[n++] = (m); } while (0)
  if (s->phase == TR_PH_PLACE) {
    for (int site = 0; site < TR_NSITES; site++) {
      int mx = tr_site_max(s, s->cur, site);
      if (mx <= 0) continue;
      if (site <= TR_S_GOLD)
        for (int k = 1; k <= mx; k++) PUSH(bk_move_pack(TR_M_PLACE, site, k, 0, 0, 0, 0, 0));
      else PUSH(bk_move_pack(TR_M_PLACE, site, mx, 0, 0, 0, 0, 0));
    }
  } else if (s->phase == TR_PH_RESOLVE && s->need == TR_NEED_TOOLS) {
    int masks[8], k = tool_options(s, masks);
    for (int i = 0; i < k; i++) PUSH(bk_move_pack(TR_M_TOOLS, masks[i], 0, 0, 0, 0, 0, 0));
  } else if (s->phase == TR_PH_RESOLVE && s->need == TR_NEED_BUY) {
    PUSH(bk_move_pack(TR_M_BUY, 1, 0, 0, 0, 0, 0, 0));
    PUSH(bk_move_pack(TR_M_BUY, 0, 0, 0, 0, 0, 0, 0));
  } else if (s->phase == TR_PH_FEED && s->need == TR_NEED_FEED) {
    PUSH(bk_move_pack(TR_M_FEED, 1, 0, 0, 0, 0, 0, 0));
    PUSH(bk_move_pack(TR_M_FEED, 0, 0, 0, 0, 0, 0, 0));
  }
#undef PUSH
  return n;
}

static void buy(tr_state *s, bool yes) {
  int site = s->step, seat = s->cur;
  tr_player *p = &s->p[seat];
  if (yes) {
    if (site <= TR_S_CARD3) {
      int slot = site - TR_S_CARD0, c = s->card_slot[slot];
      pay_cheapest(s, p, slot + 1);
      const tr_card_def *d = &tr_cards[c];
      if (d->mark >= 0) p->marks[d->mark]++;
      else p->roles[d->role] += d->mult;
      for (int i = 0; i < 2; i++)
        if (d->fx[i][0] >= 0) card_effect(s, p, d->fx[i][0], d->fx[i][1]);
      p->ncards++;
      s->card_owner[c] = (int16_t)seat;
      s->card_slot[slot] = -1;
      s->last_event[0] = 3, s->last_event[1] = (int16_t)site, s->last_event[2] = (int16_t)c, s->last_event[3] = (int16_t)seat;
    } else {
      int pile = site - TR_S_BLD0, b = s->bld_top[pile];
      for (int i = 0; i < 4; i++) {
        p->res[TR_WOOD + i] -= tr_builds[b].cost[i];
        s->supply[TR_WOOD + i] += tr_builds[b].cost[i];
      }
      p->score += tr_builds[b].points;
      p->nbuild++;
      s->bld_owner[b] = (int16_t)seat;
      s->bld_top[pile] = -1;
      s->bld_count[pile]--;
      s->last_event[0] = 4, s->last_event[1] = (int16_t)site, s->last_event[2] = (int16_t)b, s->last_event[3] = (int16_t)seat;
    }
  }
  s->place[site][seat] = 0;
  s->need = TR_NEED_NONE;
  s->step++;
}

void tr_apply(void *state, bk_move m, void *user) {
  (void)user;
  tr_state *s = state;
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  switch (kind) {
    case TR_M_PLACE: {
      if (s->phase != TR_PH_PLACE || a >= TR_NSITES || b < 1 || b > tr_site_max(s, s->cur, a))
        CW_FATAL("firsthearth: illegal placement site %d x%d", a, b);
      if (a > TR_S_GOLD && b != tr_site_max(s, s->cur, a)) CW_FATAL("firsthearth: illegal count %d at %d", b, a);
      s->place[a][s->cur] += (int16_t)b;
      s->p[s->cur].avail -= (int16_t)b;
      s->cur = (int16_t)((s->cur + 1) % s->players);
      break;
    }
    case TR_M_ROLL: {
      if (s->phase != TR_PH_RESOLVE || s->need != TR_NEED_ROLL) CW_FATAL("firsthearth: roll out of turn");
      int k = s->place[s->step][s->cur];
      int sum = a | (b << 8);
      if (sum < k || sum > 6 * k) CW_FATAL("firsthearth: roll %d for %d dice", sum, k);
      s->pend = (int16_t)sum;
      int masks[8];
      if (tool_options(s, masks) > 1) s->need = TR_NEED_TOOLS;
      else collect(s, 0);
      break;
    }
    case TR_M_TOOLS: {
      if (s->need != TR_NEED_TOOLS) CW_FATAL("firsthearth: tools out of turn");
      collect(s, a);
      break;
    }
    case TR_M_BUY:
      if (s->need != TR_NEED_BUY) CW_FATAL("firsthearth: buy out of turn");
      buy(s, a != 0);
      break;
    case TR_M_FEED: {
      if (s->need != TR_NEED_FEED) CW_FATAL("firsthearth: feed out of turn");
      feed_auto(s, &s->p[s->cur], a != 0);
      s->need = TR_NEED_NONE;
      s->step = 0;
      s->cur = (int16_t)((s->cur + 1) % s->players);
      if (s->cur == s->first) round_end(s);
      break;
    }
    case TR_M_DRAW_CARD: {
      if (s->phase != TR_PH_DEAL || a >= TR_NCARDS || !s->card_left[a]) CW_FATAL("firsthearth: bad card draw %d", a);
      int slot = 0;
      while (slot < 4 && s->card_slot[slot] >= 0) slot++;
      if (slot == 4) CW_FATAL("firsthearth: no empty card slot");
      s->card_slot[slot] = (int16_t)a;
      s->card_left[a] = 0;
      break;
    }
    case TR_M_DRAW_BLD: {
      if (s->phase != TR_PH_DEAL || a >= TR_NBUILD || !s->bld_left[a] || b >= s->players || s->bld_top[b] >= 0)
        CW_FATAL("firsthearth: bad building draw %d on pile %d", a, b);
      s->bld_top[b] = (int16_t)a;
      s->bld_left[a] = 0;
      break;
    }
    default: CW_FATAL("firsthearth: unknown move kind %d (move %016llx)", kind, (unsigned long long)m);
  }
  if (kind != TR_M_DRAW_CARD && kind != TR_M_DRAW_BLD && kind != TR_M_ROLL) s->moves_played++;
  advance(s);
}

/* ── chance ─────────────────────────────────────────────────────────── */

int tr_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user) {
  (void)user;
  const tr_state *s = state;
  int n = 0;
  if (s->phase == TR_PH_DEAL) {
    int deck = 0;
    for (int i = 0; i < TR_NCARDS; i++) deck += s->card_left[i];
    bool card = false;
    for (int i = 0; i < 4; i++)
      if (s->card_slot[i] < 0 && deck > 0) card = true;
    if (card) {
      for (int i = 0; i < TR_NCARDS; i++)
        if (s->card_left[i]) {
          if (n >= cap) CW_FATAL("tr_outcomes: cap");
          out[n] = bk_move_pack(TR_M_DRAW_CARD, i, 0, 0, 0, 0, 0, 0), weight[n++] = 1;
        }
      return n;
    }
    for (int pile = 0; pile < s->players; pile++)
      if (s->bld_count[pile] > 0 && s->bld_top[pile] < 0) {
        for (int i = 0; i < TR_NBUILD; i++)
          if (s->bld_left[i]) {
            if (n >= cap) CW_FATAL("tr_outcomes: cap");
            out[n] = bk_move_pack(TR_M_DRAW_BLD, i, pile, 0, 0, 0, 0, 0), weight[n++] = 1;
          }
        return n;
      }
    return 0;
  }
  if (s->phase == TR_PH_RESOLVE && s->need == TR_NEED_ROLL) {
    int k = s->place[s->step][s->cur];
    double ways[61] = {0}, next[61];
    ways[0] = 1;
    for (int d = 0; d < k; d++) {
      memset(next, 0, sizeof next);
      for (int t = 0; t <= 6 * d; t++)
        if (ways[t] > 0)
          for (int f = 1; f <= 6; f++) next[t + f] += ways[t];
      memcpy(ways, next, sizeof ways);
    }
    for (int t = k; t <= 6 * k; t++) {
      if (n >= cap) CW_FATAL("tr_outcomes: cap");
      out[n] = bk_move_pack(TR_M_ROLL, t & 0xff, t >> 8, 0, 0, 0, 0, 0);
      weight[n++] = (uint32_t)ways[t];
    }
  }
  return n;
}

void tr_chance(void *state, cw_rng *rng, void *user) {
  tr_state *s = state;
  if (s->phase == TR_PH_RESOLVE && s->need == TR_NEED_ROLL) {
    int k = s->place[s->step][s->cur], sum = 0;
    for (int d = 0; d < k; d++) sum += cw_rng_int(rng, 1, 6);
    tr_apply(s, bk_move_pack(TR_M_ROLL, sum & 0xff, sum >> 8, 0, 0, 0, 0, 0), user);
    return;
  }
  bk_move out[TR_NCARDS + TR_NBUILD];
  uint32_t w[TR_NCARDS + TR_NBUILD];
  int n = tr_outcomes(s, out, w, TR_NCARDS + TR_NBUILD, user);
  if (n <= 0) CW_FATAL("tr_chance: nothing to draw");
  tr_apply(s, out[cw_rng_int(rng, 0, n - 1)], user); /* every draw weighs 1 */
}

/* ── scoring ────────────────────────────────────────────────────────── */

int tr_end_bonus(const tr_state *s, int seat, int parts[4]) {
  const tr_player *p = &s->p[seat];
  int goods = goods_of(p);
  int marks = 0;
  for (int set = 1; set <= 2; set++) {
    int k = 0;
    for (int i = 0; i < 8; i++) k += p->marks[i] >= set;
    marks += k * k;
  }
  int roles = p->roles[0] * p->field + p->roles[1] * tr_tool_total(p) + p->roles[2] * p->nbuild + p->roles[3] * p->kin;
  if (parts) parts[0] = goods, parts[1] = marks, parts[2] = roles, parts[3] = 0;
  return goods + marks + roles;
}

int tr_final_score(const tr_state *s, int seat) { return s->p[seat].score + tr_end_bonus(s, seat, NULL); }

int tr_tiebreak(const tr_state *s, int seat) {
  const tr_player *p = &s->p[seat];
  return p->field + p->kin + tr_tool_total(p);
}

int tr_winner(const tr_state *s) {
  if (s->phase != TR_PH_OVER) return -1;
  int best = 0;
  for (int i = 1; i < s->players; i++) {
    int a = tr_final_score(s, i), b = tr_final_score(s, best);
    if (a > b || (a == b && tr_tiebreak(s, i) > tr_tiebreak(s, best))) best = i;
  }
  return best;
}

static float rounds_left(const tr_state *s) {
  int deck = 0, pile = 99;
  for (int i = 0; i < TR_NCARDS; i++) deck += s->card_left[i];
  for (int i = 0; i < s->players; i++)
    if (s->bld_count[i] < pile) pile = s->bld_count[i];
  float r = (float)deck / 3.0f;
  if (pile * 1.5f < r) r = pile * 1.5f;
  if (TR_MAX_ROUNDS - s->round < r) r = (float)(TR_MAX_ROUNDS - s->round);
  return r < 0 ? 0 : r;
}

void tr_evaluate(const void *state, float score[BK_MAX_SEATS], void *user) {
  (void)user;
  const tr_state *s = state;
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  if (s->phase == TR_PH_OVER) {
    int w = tr_winner(s);
    for (int i = 0; i < s->players; i++)
      score[i] = i == w ? BK_WIN - (float)s->moves_played : BK_LOSS + (float)tr_final_score(s, i);
    return;
  }
  float rl = rounds_left(s);
  for (int i = 0; i < s->players; i++) {
    const tr_player *p = &s->p[i];
    float v = (float)tr_final_score(s, i);
    /* kin still out gathering: what they should bring home (a die averages 3.5, tools about half used) */
    float tools = tr_tool_total(p) * 0.5f;
    float food = p->res[TR_FOOD];
    if (s->place[TR_S_HUNT][i]) food += (s->place[TR_S_HUNT][i] * 3.5f + tools) / 2.0f;
    float goods_pts = 0;
    for (int r = TR_WOOD; r <= TR_GOLD; r++) {
      float n = p->res[r];
      if (s->place[r][i]) n += (s->place[r][i] * 3.5f + tools) / (float)tr_value(r);
      goods_pts += n * (tr_value(r) - 1) * 0.6f; /* each good already scores 1; built, it scores its value */
    }
    v += goods_pts;
    /* this round's meal, unless this seat has already eaten */
    int n = s->players;
    bool fed = s->phase == TR_PH_FEED && (i - s->first + n) % n < (s->cur - s->first + n) % n;
    if (!fed) {
      float short_ = p->kin - p->field - food;
      int goods = p->res[TR_WOOD] + p->res[TR_CLAY] + p->res[TR_STONE] + p->res[TR_GOLD];
      if (short_ > 0) v -= short_ <= goods ? short_ * 3.5f : (float)TR_HUNGER + short_;
      else food -= p->kin - p->field;
    }
    /* the rounds to come: kin not needed for hunting work at about 3.5 points a round */
    float hunters = (p->kin - p->field) / 1.75f;
    if (hunters < 0) hunters = 0;
    v += rl * (2.2f * (p->kin - hunters) + 0.6f * tr_tool_total(p));
    v += food * 0.6f; /* stored food saves hunting later */
    /* kin not yet sent out this round will still gather (about what a kin brings home from the forest), and a
     * kin waiting on a card or a building will take it if the village can pay */
    if (s->phase == TR_PH_PLACE) v += p->avail * 2.6f;
    for (int site = TR_S_CARD0; site <= TR_S_BLD3; site++)
      if (s->place[site][i]) {
        bool pay = site <= TR_S_CARD3 ? s->card_slot[site - TR_S_CARD0] >= 0 && tr_can_afford_card(p, site - TR_S_CARD0)
                                      : s->bld_top[site - TR_S_BLD0] >= 0 && tr_can_afford_build(p, s->bld_top[site - TR_S_BLD0]);
        v += pay ? 4.0f : 0.5f;
      }
    /* cards pay at the end: role cards multiply what the village will have grown to, and each lore mark makes
     * the next different mark worth more */
    v += rl * (p->roles[0] * 0.3f + p->roles[1] * 0.4f + p->roles[2] * 0.25f + p->roles[3] * 0.15f);
    int distinct = 0;
    for (int k = 0; k < 8; k++) distinct += p->marks[k] > 0;
    v += distinct * 1.5f;
    score[i] = v;
  }
}

/* ── search helpers ─────────────────────────────────────────────────── */

float tr_priority(const void *state, bk_move m, void *user) {
  (void)state, (void)user;
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  if (kind == TR_M_BUY || kind == TR_M_FEED) return (float)a;
  if (kind == TR_M_TOOLS) return 0.5f;
  if (a >= TR_S_BLD0) return 5;
  if (a >= TR_S_CARD0) return 4;
  if (a >= TR_S_HUT) return 3.5f;
  return (float)b * 0.5f;
}

bk_move tr_playout(const void *state, const bk_move *moves, int n, cw_rng *rng, void *user) {
  (void)user;
  const tr_state *s = state;
  if (n <= 0) CW_FATAL("tr_playout: no moves (phase %d need %d)", s->phase, s->need);
  int kind = bk_move_field(moves[0], 0);
  if (kind == TR_M_BUY || kind == TR_M_FEED) return moves[0];   /* buy, feed with goods */
  if (kind == TR_M_TOOLS) return moves[n - 1];                  /* the largest gain */
  /* placements: weighted by a light sense of value */
  const tr_player *p = &s->p[s->cur];
  float w[BK_MAX_MOVES], sum = 0;
  for (int i = 0; i < n; i++) {
    int a = bk_move_field(moves[i], 1), b = bk_move_field(moves[i], 2);
    float x;
    if (a >= TR_S_BLD0) x = tr_can_afford_build(p, s->bld_top[a - TR_S_BLD0]) ? 6.f : 0.5f;
    else if (a >= TR_S_CARD0) x = tr_can_afford_card(p, a - TR_S_CARD0) ? 3.f : 0.4f;
    else if (a == TR_S_HUT) x = p->res[TR_FOOD] + p->field >= p->kin + 2 ? 4.f : 1.f;
    else if (a == TR_S_FIELD) x = 3.f;
    else if (a == TR_S_TOOL) x = 2.f;
    else if (a == TR_S_HUNT) x = (p->res[TR_FOOD] + p->field < p->kin ? 3.f : 0.8f) * (b == p->avail ? 1.5f : 0.6f);
    else x = b >= 3 ? 1.6f : 0.6f;
    w[i] = x, sum += x;
  }
  float r = cw_rng_float(rng) * sum;
  for (int i = 0; i < n; i++) {
    if (r < w[i]) return moves[i];
    r -= w[i];
  }
  return moves[n - 1];
}

static const char *const SITE_NAMES[TR_NSITES] = {
  "hunting grounds", "forest", "clay bank", "quarry", "river", "hearth", "garden", "tool bench",
  "card 1", "card 2", "card 3", "card 4", "pile 1", "pile 2", "pile 3", "pile 4",
};
const char *tr_site_name(int site) { return site >= 0 && site < TR_NSITES ? SITE_NAMES[site] : "?"; }

void tr_label(const void *state, bk_move m, char *buf, size_t cap, void *user) {
  (void)user;
  const tr_state *s = state;
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  switch (kind) {
    case TR_M_PLACE: snprintf(buf, cap, "send %d to the %s", b, tr_site_name(a)); break;
    case TR_M_TOOLS: {
      const tr_player *p = &s->p[s->cur];
      int t = 0;
      for (int i = 0; i < p->ntools; i++)
        if (a & (1 << i)) t += p->tools[i];
      if (a) snprintf(buf, cap, "add tools +%d", t);
      else snprintf(buf, cap, "keep tools");
      break;
    }
    case TR_M_BUY: snprintf(buf, cap, a ? "take it" : "let it go"); break;
    case TR_M_FEED: snprintf(buf, cap, a ? "feed with goods" : "go hungry"); break;
    case TR_M_ROLL: snprintf(buf, cap, "roll %d", a | (b << 8)); break;
    case TR_M_DRAW_CARD: snprintf(buf, cap, "card %s", tr_cards[a].name); break;
    case TR_M_DRAW_BLD: snprintf(buf, cap, "building %s", tr_builds[a].name); break;
    default: snprintf(buf, cap, "?"); break;
  }
}

uint64_t tr_hash(const tr_state *s) { return cw_hash_bytes(s, sizeof *s, 0); }

void tr_cpu_options(int level, uint64_t seed, cw_search_options *o) {
  /* measured with firsthearth_cpu_bench: short playouts scored by evaluate beat long ones at every budget (16 moves at
   * 20000 nodes won 67% against 40 moves); more nodes kept winning up to 300000 (70% over 100000) */
  static const int nodes[3] = {8000, 40000, 200000}, depth[3] = {8, 12, 16};
  static const float mistake[3] = {0.15f, 0.06f, 0};
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  cw_search_level_options(level, CW_SEARCH_MCTS, o);
  o->seed = seed;
  o->max_nodes = nodes[level];
  o->playout_depth = depth[level];
  o->mistake = mistake[level];
  o->mistake_spread = level == 0 ? 3 : 2;
  /* the MCTS reward is the margin (a logistic of the lead over the best other seat): 6 points of lead is worth 0.73.
   * Measured with firsthearth_cpu_bench (RSCALE): a winner leads the next seat by a median 44 points at two Normal
   * seats, 14 at three, 11 at four, 35 in Easy / Normal / Hard games, so about 30 is a typical margin and 6 a fifth of
   * it. Over 60 games each, 6 kept the levels furthest apart: Hard beat Normal 45 of 60 (4: 41, 9: 38) and Normal beat
   * Easy 48 of 60 (4: 47, 9: 47) */
  o->reward_scale = 6;
}

void tr_game(bk_game *g, int players) {
  memset(g, 0, sizeof *g);
  g->players = players;
  g->state_size = sizeof(tr_state);
  g->moves = tr_moves;
  g->apply = tr_apply;
  g->turn = tr_turn;
  g->terminal = tr_terminal;
  g->evaluate = tr_evaluate;
  g->chance = tr_chance;
  g->outcomes = tr_outcomes;
  g->priority = tr_priority;
  g->playout = tr_playout;
}
