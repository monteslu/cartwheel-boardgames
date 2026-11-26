/* Facet: the rules (rules.h). */
#include "rules.h"

#include <stdio.h>
#include <string.h>

#include "cards_data.h"

const gm_card *const gm_card_set = gm_cards;
const gm_patron *const gm_patron_set = gm_patrons;

static const char *const color_name[GM_NTOK] = {"ruby", "sapphire", "emerald", "amethyst", "diamond", "topaz"};

int gm_hand(const gm_seat *p) {
  int n = 0;
  for (int i = 0; i < GM_NTOK; i++) n += p->tok[i];
  return n;
}

bool gm_price(const gm_seat *p, int card, uint8_t pay[GM_NTOK]) {
  const gm_card *c = &gm_cards[card];
  int wild = 0;
  for (int i = 0; i < GM_NCOLORS; i++) {
    int need = c->cost[i] - p->bonus[i];
    if (need < 0) need = 0;
    int from = need < p->tok[i] ? need : p->tok[i];
    pay[i] = (uint8_t)from;
    wild += need - from;
  }
  pay[GM_WILD] = (uint8_t)wild;
  return wild <= p->tok[GM_WILD];
}

bool gm_patron_met(const gm_seat *p, int patron) {
  for (int i = 0; i < GM_NCOLORS; i++)
    if (p->bonus[i] < gm_patrons[patron].need[i]) return false;
  return true;
}

int gm_deck_left(const gm_state *st, int tier) {
  int n = 0;
  for (int i = 0; i < GM_NCARDS; i++)
    if (gm_cards[i].tier == tier && st->where[i] == GM_AT_DECK) n++;
  return n;
}

/* the k-th card left in a tier's deck, in card-id order (the deck keeps contents, never an order) */
static int deck_pick(const gm_state *st, int tier, int k) {
  for (int i = 0; i < GM_NCARDS; i++)
    if (gm_cards[i].tier == tier && st->where[i] == GM_AT_DECK && k-- == 0) return i;
  CW_FATAL("facet: deck pick %d past the end of tier %d", k, tier);
  return -1;
}

void gm_new(gm_state *st, int players, uint64_t seed) {
  if (players < 2 || players > BK_MAX_SEATS) CW_FATAL("facet: %d players (2..4)", players);
  memset(st, 0, sizeof *st);
  cw_rng_seed(&st->rng, seed);
  st->players = (uint8_t)players;
  int per = players == 2 ? 4 : players == 3 ? 5 : 7;
  for (int i = 0; i < GM_NCOLORS; i++) st->bank[i] = (uint8_t)per;
  st->bank[GM_WILD] = 5;
  for (int t = 0; t < GM_TIERS; t++)
    for (int s = 0; s < GM_SLOTS; s++) {
      int c = deck_pick(st, t, cw_rng_int(&st->rng, 0, gm_deck_left(st, t) - 1));
      st->market[t][s] = (uint8_t)c;
      st->where[c] = GM_AT_MARKET;
    }
  /* players + 1 patrons, picked without replacement */
  for (int k = 0; k < players + 1; k++) {
    int left = GM_NPATRONS - k, pick = cw_rng_int(&st->rng, 0, left - 1);
    for (int i = 0; i < GM_NPATRONS; i++)
      if (!st->patron_open[i] && pick-- == 0) { st->patron_open[i] = 1; break; }
  }
  for (int i = 0; i < GM_NPATRONS; i++) st->patron_owner[i] = GM_NONE;
  for (int p = 0; p < BK_MAX_SEATS; p++)
    for (int r = 0; r < GM_MAX_RES; r++) st->s[p].res[r] = GM_NONE;
  st->last_kind = 0;
  st->last_seat = GM_NONE;
}

static void finish(gm_state *st) {
  st->phase = GM_PH_OVER;
  int best = -1, fewest = 1 << 30;
  uint8_t w = 0;
  for (int p = 0; p < st->players; p++) {
    int pts = st->s[p].points, n = st->s[p].ncards;
    if (pts > best || (pts == best && n < fewest)) { best = pts; fewest = n; w = (uint8_t)(1u << p); }
    else if (pts == best && n == fewest) w |= (uint8_t)(1u << p);
  }
  st->winners = w;
}

static void end_turn(gm_state *st) {
  if (st->s[st->seat].points >= GM_WIN_POINTS) st->last_round = 1;
  st->turns++;
  if ((st->last_round && st->seat == st->players - 1) || st->turns >= GM_MAX_TURNS) {
    finish(st);
    return;
  }
  st->seat = (uint8_t)((st->seat + 1) % st->players);
  st->phase = GM_PH_ACT;
}

static void take_patron(gm_state *st, int i) {
  gm_seat *p = &st->s[st->seat];
  st->patron_open[i] = 2;
  st->patron_owner[i] = st->seat;
  p->points += gm_patrons[i].points;
  p->npatrons++;
}

/* after a decision: settle what the turn still owes (a draw, hand-backs, a patron) or pass the turn on */
static void advance(gm_state *st) {
  if (st->draw_kind != GM_DRAW_NONE) return;
  gm_seat *p = &st->s[st->seat];
  int met = 0, only = -1;
  for (int i = 0; i < GM_NPATRONS; i++)
    if (st->patron_open[i] == 1 && gm_patron_met(p, i)) { met++; only = i; }
  if (met == 1) take_patron(st, only);
  if (met >= 2) { st->phase = GM_PH_PATRON; return; }
  end_turn(st);
}

int gm_turn(const void *state, void *user) {
  (void)user;
  const gm_state *st = state;
  if (st->draw_kind != GM_DRAW_NONE) return BK_CHANCE;
  return st->seat;
}

bool gm_terminal(const void *state, void *user) {
  (void)user;
  return ((const gm_state *)state)->phase == GM_PH_OVER;
}

#define PUSH(m) do { if (n < cap) out[n] = (m); n++; } while (0)

/* hand-backs: when a take or a set-aside lifts a hand past ten, the move itself names the stones handed back
 * (fields 4..6), so one move is one whole turn and the search compares whole turns. Takes that end with the same
 * stones in hand are one turn, listed once. */
typedef struct {
  bk_move *out;
  int cap, n, nfinal;
  bool dedupe;
  uint8_t finals[BK_MAX_MOVES][GM_NTOK];
} gen;

static void emit(gen *g, bk_move m, const uint8_t hand[GM_NTOK]) {
  if (g->dedupe) {
    for (int i = 0; i < g->nfinal; i++)
      if (!memcmp(g->finals[i], hand, GM_NTOK)) return;
    if (g->nfinal < BK_MAX_MOVES) memcpy(g->finals[g->nfinal++], hand, GM_NTOK);
  }
  if (g->n < g->cap) g->out[g->n] = m;
  g->n++;
}

#define RET_NONE ((bk_move)GM_NONE << 32 | (bk_move)GM_NONE << 40 | (bk_move)GM_NONE << 48)

/* m holds the action in fields 0..3; hand is the hand after the gain: list every way back down to ten */
static void with_returns(gen *g, bk_move m, const uint8_t hand[GM_NTOK]) {
  int excess = -GM_HAND_CAP;
  for (int i = 0; i < GM_NTOK; i++) excess += hand[i];
  if (excess <= 0) { emit(g, m | RET_NONE, hand); return; }
  if (excess > 3) CW_FATAL("facet: a hand %d past ten", excess);
  uint8_t h[GM_NTOK];
  for (int x = 0; x < GM_NTOK; x++) {
    if (!hand[x]) continue;
    if (excess == 1) {
      memcpy(h, hand, GM_NTOK);
      h[x]--;
      emit(g, m | (bk_move)x << 32 | (bk_move)GM_NONE << 40 | (bk_move)GM_NONE << 48, h);
      continue;
    }
    for (int y = x; y < GM_NTOK; y++) {
      if (hand[y] < 1 + (y == x)) continue;
      if (excess == 2) {
        memcpy(h, hand, GM_NTOK);
        h[x]--, h[y]--;
        emit(g, m | (bk_move)x << 32 | (bk_move)y << 40 | (bk_move)GM_NONE << 48, h);
        continue;
      }
      for (int z = y; z < GM_NTOK; z++) {
        if (hand[z] < 1 + (z == x) + (z == y)) continue;
        memcpy(h, hand, GM_NTOK);
        h[x]--, h[y]--, h[z]--;
        emit(g, m | (bk_move)x << 32 | (bk_move)y << 40 | (bk_move)z << 48, h);
      }
    }
  }
}

int gm_moves(const void *state, bk_move *out, int cap, void *user) {
  (void)user;
  const gm_state *st = state;
  if (st->phase == GM_PH_OVER || st->draw_kind != GM_DRAW_NONE) return 0;
  const gm_seat *p = &st->s[st->seat];
  gen g;
  g.out = out;
  g.cap = cap;
  g.n = g.nfinal = 0;
  g.dedupe = false;
  int n = 0;
  uint8_t pay[GM_NTOK], hand[GM_NTOK];
  if (st->phase == GM_PH_PATRON) {
    for (int i = 0; i < GM_NPATRONS; i++)
      if (st->patron_open[i] == 1 && gm_patron_met(p, i)) PUSH(bk_move_pack(GM_MV_PATRON, i, 0, 0, 0, 0, 0, 0));
    return n;
  }
  /* fulfilments first (the order search breaks ties by), then takes, then set-asides */
  for (int t = GM_TIERS - 1; t >= 0; t--)
    for (int s = 0; s < GM_SLOTS; s++) {
      int c = st->market[t][s];
      if (c != GM_NONE && gm_price(p, c, pay)) PUSH(bk_move_pack(GM_MV_BUY, t, s, 0, 0, 0, 0, 0));
    }
  for (int r = 0; r < p->nres; r++)
    if (gm_price(p, p->res[r], pay)) PUSH(bk_move_pack(GM_MV_BUY_RES, r, 0, 0, 0, 0, 0, 0));
  g.n = n;
  g.dedupe = true;
  int avail[GM_NCOLORS], na = 0;
  for (int c = 0; c < GM_NCOLORS; c++)
    if (st->bank[c]) avail[na++] = c;
  /* three different colours, or as many as are left */
  int pick[3];
  int k = na < 3 ? na : 3;
  for (pick[0] = 0; k >= 1 && pick[0] < na; pick[0]++)
    for (pick[1] = k >= 2 ? pick[0] + 1 : na; k < 2 || pick[1] < na; pick[1]++) {
      for (pick[2] = k >= 3 ? pick[1] + 1 : na; k < 3 || pick[2] < na; pick[2]++) {
        int cs[3] = {GM_NONE, GM_NONE, GM_NONE};
        memcpy(hand, p->tok, GM_NTOK);
        for (int i = 0; i < k; i++) {
          cs[i] = avail[pick[i]];
          hand[cs[i]]++;
        }
        with_returns(&g, bk_move_pack(GM_MV_TAKE3, cs[0], cs[1], cs[2], 0, 0, 0, 0), hand);
        if (k < 3) break;
      }
      if (k < 2) break;
    }
  for (int c = 0; c < GM_NCOLORS; c++)
    if (st->bank[c] >= 4) {
      memcpy(hand, p->tok, GM_NTOK);
      hand[c] += 2;
      with_returns(&g, bk_move_pack(GM_MV_TAKE2, c, 0, 0, 0, 0, 0, 0), hand);
    }
  g.dedupe = false;
  if (p->nres < GM_MAX_RES) {
    memcpy(hand, p->tok, GM_NTOK);
    if (st->bank[GM_WILD]) hand[GM_WILD]++;
    for (int t = 0; t < GM_TIERS; t++)
      for (int s = 0; s < GM_SLOTS; s++)
        if (st->market[t][s] != GM_NONE) with_returns(&g, bk_move_pack(GM_MV_RESERVE, t, s, 0, 0, 0, 0, 0), hand);
    for (int t = 0; t < GM_TIERS; t++)
      if (gm_deck_left(st, t)) with_returns(&g, bk_move_pack(GM_MV_RESERVE_DECK, t, 0, 0, 0, 0, 0, 0), hand);
  }
  n = g.n;
  if (n == 0) PUSH(bk_move_pack(GM_MV_PASS, 0, 0, 0, 0, 0, 0, 0));
  if (n > cap) CW_FATAL("facet: %d moves past the cap of %d", n, cap);
  return n;
}

static void pay_for(gm_state *st, gm_seat *p, int card) {
  uint8_t pay[GM_NTOK];
  if (!gm_price(p, card, pay)) CW_FATAL("facet: seat %d cannot pay for card %d", st->seat, card);
  for (int i = 0; i < GM_NTOK; i++) {
    p->tok[i] -= pay[i];
    st->bank[i] += pay[i];
  }
  const gm_card *c = &gm_cards[card];
  p->bonus[c->bonus]++;
  p->points += c->points;
  p->ncards++;
  st->where[card] = GM_AT_BOUGHT;
  st->owner[card] = st->seat;
}

static void refill(gm_state *st, int t, int s) {
  st->market[t][s] = GM_NONE;
  if (gm_deck_left(st, t)) {
    st->draw_kind = GM_DRAW_MARKET;
    st->draw_tier = (uint8_t)t;
    st->draw_slot = (uint8_t)s;
  }
}

static void take_wild(gm_state *st, gm_seat *p) {
  if (st->bank[GM_WILD]) {
    st->bank[GM_WILD]--;
    p->tok[GM_WILD]++;
  }
}

/* the stones a move hands back (fields 4..6) */
static void hand_back(gm_state *st, gm_seat *p, bk_move m) {
  for (int i = 4; i <= 6; i++) {
    int c = bk_move_field(m, i);
    if (c == GM_NONE) continue;
    if (c >= GM_NTOK || !p->tok[c]) CW_FATAL("facet: hand back a stone not held (%d)", c);
    p->tok[c]--;
    st->bank[c]++;
  }
  if (gm_hand(p) > GM_HAND_CAP) CW_FATAL("facet: seat %d left holding %d stones", st->seat, gm_hand(p));
}

void gm_apply(void *state, bk_move m, void *user) {
  (void)user;
  gm_state *st = state;
  gm_seat *p = &st->s[st->seat];
  int k = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2), c3 = bk_move_field(m, 3);
  if (k != GM_MV_DRAW && k != GM_MV_PATRON) {
    st->last_kind = (uint8_t)k;
    st->last_a = (uint8_t)a;
    st->last_b = (uint8_t)b;
    st->last_seat = st->seat;
  }
  switch (k) {
  case GM_MV_TAKE3: {
    int cs[3] = {a, b, c3};
    for (int i = 0; i < 3; i++)
      if (cs[i] != GM_NONE) {
        if (!st->bank[cs[i]]) CW_FATAL("facet: take from an empty %s pile", color_name[cs[i]]);
        st->bank[cs[i]]--;
        p->tok[cs[i]]++;
      }
    hand_back(st, p, m);
    advance(st);
    break;
  }
  case GM_MV_TAKE2:
    if (st->bank[a] < 4) CW_FATAL("facet: take two from a %s pile of %d", color_name[a], st->bank[a]);
    st->bank[a] -= 2;
    p->tok[a] += 2;
    hand_back(st, p, m);
    advance(st);
    break;
  case GM_MV_BUY: {
    int card = st->market[a][b];
    st->last_b = (uint8_t)card; /* the view wants the card, not the slot */
    st->last_a = (uint8_t)(a * GM_SLOTS + b);
    pay_for(st, p, card);
    refill(st, a, b);
    advance(st);
    break;
  }
  case GM_MV_BUY_RES: {
    int card = p->res[a];
    st->last_b = (uint8_t)card;
    pay_for(st, p, card);
    for (int r = a; r < GM_MAX_RES - 1; r++) p->res[r] = p->res[r + 1];
    p->res[GM_MAX_RES - 1] = GM_NONE;
    p->nres--;
    advance(st);
    break;
  }
  case GM_MV_RESERVE: {
    int card = st->market[a][b];
    st->last_b = (uint8_t)card;
    st->last_a = (uint8_t)(a * GM_SLOTS + b);
    p->res[p->nres++] = (uint8_t)card;
    st->where[card] = GM_AT_RESERVED;
    st->owner[card] = st->seat;
    take_wild(st, p);
    hand_back(st, p, m);
    refill(st, a, b);
    advance(st);
    break;
  }
  case GM_MV_RESERVE_DECK:
    take_wild(st, p);
    hand_back(st, p, m);
    st->draw_kind = GM_DRAW_RESERVE;
    st->draw_tier = (uint8_t)a;
    st->draw_slot = 0;
    break;
  case GM_MV_DRAW: {
    if (st->where[a] != GM_AT_DECK || gm_cards[a].tier != st->draw_tier) CW_FATAL("facet: draw of card %d not in deck %d", a, st->draw_tier);
    if (st->draw_kind == GM_DRAW_MARKET) {
      st->market[st->draw_tier][st->draw_slot] = (uint8_t)a;
      st->where[a] = GM_AT_MARKET;
    } else if (st->draw_kind == GM_DRAW_RESERVE) {
      p->res[p->nres++] = (uint8_t)a;
      st->where[a] = GM_AT_RESERVED;
      st->owner[a] = st->seat;
      st->last_b = (uint8_t)a;
    } else CW_FATAL("facet: a draw with nothing pending");
    st->draw_kind = GM_DRAW_NONE;
    advance(st);
    break;
  }
  case GM_MV_PATRON:
    take_patron(st, a);
    end_turn(st);
    break;
  case GM_MV_PASS:
    advance(st);
    break;
  default: CW_FATAL("facet: bad move kind %d", k);
  }
}

int gm_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user) {
  (void)user;
  const gm_state *st = state;
  int n = 0;
  if (st->draw_kind == GM_DRAW_NONE) return 0;
  for (int i = 0; i < GM_NCARDS; i++)
    if (gm_cards[i].tier == st->draw_tier && st->where[i] == GM_AT_DECK) {
      if (n < cap) { out[n] = bk_move_pack(GM_MV_DRAW, i, 0, 0, 0, 0, 0, 0); weight[n] = 1; }
      n++;
    }
  return n;
}

void gm_chance(void *state, cw_rng *rng, void *user) {
  gm_state *st = state;
  int left = gm_deck_left(st, st->draw_tier);
  if (!left) CW_FATAL("facet: draw from an empty deck");
  int c = deck_pick(st, st->draw_tier, cw_rng_int(rng, 0, left - 1));
  gm_apply(st, bk_move_pack(GM_MV_DRAW, c, 0, 0, 0, 0, 0, 0), user);
}

/* how far a seat is from a card: the stones it still needs after bonuses (need) and how many of those its hand
 * already covers, topaz included (have) */
static void progress(const gm_seat *p, int card, int *need, int *have) {
  const gm_card *c = &gm_cards[card];
  int nd = 0, hv = 0;
  for (int i = 0; i < GM_NCOLORS; i++) {
    int n = c->cost[i] - p->bonus[i];
    if (n <= 0) continue;
    nd += n;
    hv += p->tok[i] < n ? p->tok[i] : n;
  }
  int gap = nd - hv;
  hv += p->tok[GM_WILD] < gap ? p->tok[GM_WILD] : gap;
  *need = nd;
  *have = hv;
}

/* a card as a target: what it pays, scaled by how much of it the hand already covers */
static float target_value(const gm_seat *p, int card) {
  int need, have;
  progress(p, card, &need, &have);
  float f = need ? (float)have / (float)need : 1.0f;
  float worth = gm_cards[card].points * 4.0f + 3.0f;
  /* the stones still missing are turns away: about three a turn */
  return worth * f * f / (1.0f + (float)(need - have) / 3.0f);
}

static void keep_best(float best[3], float w) {
  for (int k = 0; k < 3; k++)
    if (w > best[k]) { float tmp = best[k]; best[k] = w; w = tmp; }
}

static float seat_value(const gm_state *st, int who) {
  const gm_seat *p = &st->s[who];
  float v = p->points * 10.0f;
  int nb = 0;
  for (int i = 0; i < GM_NCOLORS; i++) nb += p->bonus[i];
  /* bonuses are worth most early (they discount every later buy) */
  v += nb * (nb < 10 ? 3.0f : 1.8f);
  /* loose stones are worth little on their own; their worth is in the targets below */
  v += gm_hand(p) * 0.2f + p->tok[GM_WILD] * 0.3f;
  /* a full hand chokes the bank and the next take: spend before it fills */
  if (gm_hand(p) > 8) v -= (float)(gm_hand(p) - 8) * 1.5f;
  /* patrons on the table: progress toward each, squared so the close ones count */
  for (int i = 0; i < GM_NPATRONS; i++) {
    if (st->patron_open[i] != 1) continue;
    int have = 0, need = 0;
    for (int c = 0; c < GM_NCOLORS; c++) {
      int nd = gm_patrons[i].need[c];
      need += nd;
      have += p->bonus[c] < nd ? p->bonus[c] : nd;
    }
    float f = (float)have / (float)need;
    v += f * f * 7.0f;
  }
  /* the best few targets: own set-asides (safe from others) and the market */
  float best[3] = {0, 0, 0};
  for (int t = 0; t < GM_TIERS; t++)
    for (int s = 0; s < GM_SLOTS; s++)
      if (st->market[t][s] != GM_NONE) keep_best(best, target_value(p, st->market[t][s]));
  for (int r = 0; r < p->nres; r++) {
    int need, have;
    progress(p, p->res[r], &need, &have);
    keep_best(best, target_value(p, p->res[r]) * 1.1f);
    v -= 1.0f + 0.4f * (float)(need - have); /* a set-aside that never gets built is a dead card */
  }
  v += best[0] * 1.0f + best[1] * 0.5f + best[2] * 0.25f;
  return v;
}

void gm_evaluate(const void *state, float score[BK_MAX_SEATS], void *user) {
  (void)user;
  const gm_state *st = state;
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  if (st->phase == GM_PH_OVER) {
    for (int p = 0; p < st->players; p++)
      score[p] = (st->winners >> p & 1) ? BK_WIN - st->turns : BK_LOSS + st->s[p].points;
    return;
  }
  for (int p = 0; p < st->players; p++) score[p] = seat_value(st, p);
}

void gm_label(const void *state, bk_move m, char *buf, size_t cap, void *user) {
  (void)user;
  const gm_state *st = state;
  const gm_seat *p = &st->s[st->seat];
  int k = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2), c = bk_move_field(m, 3);
  switch (k) {
  case GM_MV_TAKE3:
    snprintf(buf, cap, "take %s%s%s%s%s", color_name[a], b != GM_NONE ? " " : "", b != GM_NONE ? color_name[b] : "",
             c != GM_NONE ? " " : "", c != GM_NONE ? color_name[c] : "");
    break;
  case GM_MV_TAKE2: snprintf(buf, cap, "take 2 %s", color_name[a]); break;
  case GM_MV_BUY: snprintf(buf, cap, "fulfil %s", gm_cards[st->market[a][b]].name); break;
  case GM_MV_BUY_RES: snprintf(buf, cap, "fulfil set-aside %s", gm_cards[p->res[a]].name); break;
  case GM_MV_RESERVE: snprintf(buf, cap, "set aside %s", gm_cards[st->market[a][b]].name); break;
  case GM_MV_RESERVE_DECK: snprintf(buf, cap, "set aside from deck %d", a + 1); break;
  case GM_MV_PATRON: snprintf(buf, cap, "welcome %s", gm_patrons[a].name); break;
  case GM_MV_PASS: snprintf(buf, cap, "pass"); break;
  case GM_MV_DRAW: snprintf(buf, cap, "draw %s", gm_cards[a].name); break;
  default: snprintf(buf, cap, "?%d", k);
  }
  int r0 = bk_move_field(m, 4);
  if ((k == GM_MV_TAKE3 || k == GM_MV_TAKE2 || k == GM_MV_RESERVE || k == GM_MV_RESERVE_DECK) && r0 != GM_NONE) {
    size_t len = strlen(buf);
    for (int i = 4; i <= 6 && len < cap; i++) {
      int c = bk_move_field(m, i);
      if (c == GM_NONE) break;
      snprintf(buf + len, cap - len, "%s%s", i == 4 ? ", hand back " : " ", color_name[c]);
      len = strlen(buf);
    }
  }
}

static float gm_priority(const void *state, bk_move m, void *user) {
  (void)state;
  (void)user;
  switch (bk_move_field(m, 0)) {
  case GM_MV_BUY: case GM_MV_BUY_RES: return 3.0f;
  case GM_MV_PATRON: return 3.0f;
  case GM_MV_TAKE3: case GM_MV_TAKE2: return 2.0f;
  case GM_MV_RESERVE: return 1.0f;
  default: return 0.0f;
  }
}

void gm_rules(bk_game *g, int players) {
  memset(g, 0, sizeof *g);
  g->players = players;
  g->state_size = sizeof(gm_state);
  g->moves = gm_moves;
  g->apply = gm_apply;
  g->turn = gm_turn;
  g->terminal = gm_terminal;
  g->evaluate = gm_evaluate;
  g->chance = gm_chance;
  g->outcomes = gm_outcomes;
  g->priority = gm_priority;
}
