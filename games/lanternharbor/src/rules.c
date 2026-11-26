/* Lantern Harbor (lanternharbor): the rules. See rules.h. */
#include "rules.h"

#include <stdio.h>
#include <string.h>

const hb_building_info hb_bld[HB_BUILDINGS] = {
  [B_DYE_SHED] = { "Dye Shed", 1, 1, 1, 1, G_INDIGO, 1 },
  [B_KILN] = { "Glass Kiln", 2, 1, 1, 1, G_GLASS, 1 },
  [B_DYE_WORKS] = { "Dye Works", 3, 2, 3, 2, G_INDIGO, 1 },
  [B_GLASSWORKS] = { "Glassworks", 4, 2, 3, 2, G_GLASS, 1 },
  [B_TEA_LOFT] = { "Tea Loft", 5, 3, 3, 3, G_TEA, 1 },
  [B_SPICE_MILL] = { "Spice Mill", 6, 3, 2, 3, G_SPICE, 1 },
  [B_STALL] = { "Lamp Stall", 1, 1, 1, 1, -1, 1 },
  [B_SEED_SHED] = { "Seed Shed", 2, 1, 1, 1, -1, 1 },
  [B_STONEYARD] = { "Stoneyard", 2, 1, 1, 1, -1, 1 },
  [B_STOREHOUSE] = { "Storehouse", 3, 1, 1, 1, -1, 1 },
  [B_HOSTEL] = { "Hostel", 4, 2, 1, 2, -1, 1 },
  [B_COUNTING_HOUSE] = { "Counting House", 5, 2, 1, 2, -1, 1 },
  [B_NIGHT_MARKET] = { "Night Market", 5, 2, 1, 2, -1, 1 },
  [B_WAREHOUSE] = { "Warehouse", 6, 2, 1, 2, -1, 1 },
  [B_COOPERATIVE] = { "Cooperative", 7, 3, 1, 3, -1, 1 },
  [B_ACADEMY] = { "Academy", 8, 3, 1, 3, -1, 1 },
  [B_BEACON] = { "Beacon Pier", 8, 3, 1, 3, -1, 1 },
  [B_JETTY] = { "Private Jetty", 9, 3, 1, 3, -1, 1 },
  [B_GUILDHALL] = { "Guildhall", 10, 4, 1, 4, -1, 2 },
  [B_GARDENS] = { "Tea Gardens", 10, 4, 1, 4, -1, 2 },
  [B_BELL_TOWER] = { "Bell Tower", 10, 4, 1, 4, -1, 2 },
  [B_HARBOUR_OFFICE] = { "Harbour Office", 10, 4, 1, 4, -1, 2 },
  [B_ASSEMBLY] = { "Assembly Hall", 10, 4, 1, 4, -1, 2 },
};

const char *const hb_good_name[HB_GOODS] = { "rice", "indigo", "glass", "tea", "spice" };
const char *const hb_plot_name[HB_PLOT_KINDS] = { "rice paddy", "indigo field", "sand dune", "tea terrace", "spice grove", "stone pit" };
const char *const hb_role_name[HB_ROLES] = { "Surveyor", "Ferryman", "Mason", "Artisan", "Merchant", "Skipper", "Diver" };
const char *const hb_role_effect[HB_ROLES] = {
  "Everyone claims a plot. Yours may be a stone pit.",
  "The ferry lands townsfolk. You get one more.",
  "Everyone may raise a building. Yours costs 1 less.",
  "Staffed plots and workshops make goods. You get 1 extra.",
  "Everyone may sell a good. You get 1 coin more.",
  "Everyone must load boats for lanterns. You get 1 more.",
  "You dive for a pearl: 1 coin.",
};
const char *const hb_bld_effect[HB_BUILDINGS] = {
  [B_DYE_SHED] = "Turns 1 indigo crop into dye.",
  [B_KILN] = "Turns 1 sand crop into glass.",
  [B_DYE_WORKS] = "Turns up to 3 indigo crops into dye.",
  [B_GLASSWORKS] = "Turns up to 3 sand crops into glass.",
  [B_TEA_LOFT] = "Dries up to 3 tea crops.",
  [B_SPICE_MILL] = "Grinds up to 2 spice crops.",
  [B_STALL] = "Sales earn 1 coin more.",
  [B_SEED_SHED] = "Surveyor: also draw a hidden plot first.",
  [B_STONEYARD] = "Surveyor: you may open a stone pit.",
  [B_STOREHOUSE] = "Keeps a whole pile of one good from spoiling.",
  [B_HOSTEL] = "Surveyor: a worker moves onto your new plot.",
  [B_COUNTING_HOUSE] = "You may sell a good the stall already holds.",
  [B_NIGHT_MARKET] = "Sales earn 2 coins more.",
  [B_WAREHOUSE] = "Keeps whole piles of two goods from spoiling.",
  [B_COOPERATIVE] = "Artisan: coins for each kind of good you make.",
  [B_ACADEMY] = "Mason: a worker moves into your new building.",
  [B_BEACON] = "Each load you ship earns 1 lantern more.",
  [B_JETTY] = "Skipper: once, ship a whole pile off your own jetty.",
  [B_GUILDHALL] = "End: 1 per small and 2 per large workshop.",
  [B_GARDENS] = "End: 4 to 7 for a hillside of 9 to 12 plots.",
  [B_BELL_TOWER] = "End: 1 for every 3 townsfolk you have.",
  [B_HARBOUR_OFFICE] = "End: 1 for every 4 lanterns you hold.",
  [B_ASSEMBLY] = "End: 1 for each guild house in your town.",
};

static const uint8_t PRICE[HB_GOODS] = { 0, 1, 2, 3, 4 };

/* ── small helpers ─────────────────────────────────────────────────── */

bool hb_owns(const hb_player *pl, int b) { return (pl->built >> b) & 1u; }
bool hb_staffed(const hb_player *pl, int b) { return hb_owns(pl, b) && pl->staff[b] > 0; }

int hb_town_lots(const hb_player *pl) {
  int n = 0;
  for (int i = 0; i < pl->nbld; i++) n += hb_bld[pl->bld[i]].lots;
  return n;
}

static int staffed_plots(const hb_player *pl, int kind) {
  int n = 0;
  for (int i = 0; i < pl->nplots; i++) n += pl->plot[i] == kind && pl->plot_staffed[i];
  return n;
}

int hb_staffed_stones(const hb_player *pl) { return staffed_plots(pl, HB_STONE); }

int hb_build_cost(const hb_state *s, int seat, int b) {
  const hb_player *pl = &s->p[seat];
  int stones = hb_staffed_stones(pl);
  if (stones > hb_bld[b].tier) stones = hb_bld[b].tier;
  int c = hb_bld[b].cost - stones - (seat == s->picker ? 1 : 0);
  return c < 0 ? 0 : c;
}

int hb_empty_slots(const hb_player *pl) {
  int n = 0;
  for (int i = 0; i < pl->nplots; i++) n += !pl->plot_staffed[i];
  for (int i = 0; i < pl->nbld; i++) n += hb_bld[pl->bld[i]].slots - pl->staff[pl->bld[i]];
  return n;
}

static int empty_building_slots(const hb_player *pl) {
  int n = 0;
  for (int i = 0; i < pl->nbld; i++) n += hb_bld[pl->bld[i]].slots - pl->staff[pl->bld[i]];
  return n;
}

int hb_workers(const hb_player *pl) {
  int n = pl->dock;
  for (int i = 0; i < pl->nplots; i++) n += pl->plot_staffed[i];
  for (int i = 0; i < pl->nbld; i++) n += pl->staff[pl->bld[i]];
  return n;
}

int hb_sale_price(const hb_state *s, int seat, int good) {
  const hb_player *pl = &s->p[seat];
  int p = PRICE[good];
  if (seat == s->picker) p++;
  if (hb_staffed(pl, B_STALL)) p += 1;
  if (hb_staffed(pl, B_NIGHT_MARKET)) p += 2;
  return p;
}

int hb_production(const hb_player *pl, int good) {
  int crops = staffed_plots(pl, good);
  if (good == G_RICE) return crops;
  int cap = 0;
  for (int i = 0; i < pl->nbld; i++) {
    int b = pl->bld[i];
    if (hb_bld[b].good == good) cap += pl->staff[b];
  }
  return crops < cap ? crops : cap;
}

static int small_workshops(const hb_player *pl) { return hb_owns(pl, B_DYE_SHED) + hb_owns(pl, B_KILN); }
static int large_workshops(const hb_player *pl) {
  return hb_owns(pl, B_DYE_WORKS) + hb_owns(pl, B_GLASSWORKS) + hb_owns(pl, B_TEA_LOFT) + hb_owns(pl, B_SPICE_MILL);
}

int hb_bonus(const hb_player *pl, int b) {
  if (!hb_staffed(pl, b)) return 0;
  switch (b) {
  case B_GUILDHALL: return small_workshops(pl) + 2 * large_workshops(pl);
  case B_GARDENS: return pl->nplots <= 9 ? 4 : pl->nplots - 6;
  case B_BELL_TOWER: return hb_workers(pl) / 3;
  case B_HARBOUR_OFFICE: return pl->lanterns / 4;
  case B_ASSEMBLY: {
    int n = 0;
    for (int i = 0; i < pl->nbld; i++) n += hb_bld[pl->bld[i]].good < 0;
    return n;
  }
  default: return 0;
  }
}

int hb_score(const hb_player *pl) {
  int v = pl->lanterns;
  for (int i = 0; i < pl->nbld; i++) v += hb_bld[pl->bld[i]].value + hb_bonus(pl, pl->bld[i]);
  return v;
}

int hb_tiebreak(const hb_player *pl) {
  int n = pl->coins;
  for (int g = 0; g < HB_GOODS; g++) n += pl->goods[g];
  return n;
}

unsigned hb_winners(const hb_state *s) {
  int best = -1, best_t = -1;
  unsigned w = 0;
  for (int i = 0; i < s->players; i++) {
    int sc = hb_score(&s->p[i]), t = hb_tiebreak(&s->p[i]);
    if (sc > best || (sc == best && t > best_t)) { best = sc; best_t = t; w = 1u << i; }
    else if (sc == best && t == best_t) w |= 1u << i;
  }
  return w;
}

int hb_picks_per_round(const hb_state *s) { return s->players == 2 ? 6 : s->players; }

int hb_folk_total(const hb_state *s) {
  int n = s->folk_supply + s->ferry;
  for (int i = 0; i < s->players; i++) n += hb_workers(&s->p[i]);
  return n;
}

int hb_goods_total(const hb_state *s, int good) {
  int n = s->goods_supply[good];
  for (int i = 0; i < s->players; i++) n += s->p[i].goods[good];
  for (int k = 0; k < s->nships; k++)
    if (s->ship_good[k] == good) n += s->ship_load[k];
  for (int k = 0; k < s->nmarket; k++) n += s->market[k] == good;
  return n;
}

/* ── setup ─────────────────────────────────────────────────────────── */

void hb_init(hb_state *s, int players, uint64_t seed) {
  if (players < 2 || players > BK_MAX_SEATS) CW_FATAL("harbor: %d players (2..4)", players);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = (uint8_t)players;
  bool two = players == 2;
  static const uint8_t PLOTS[HB_GOODS] = { 10, 12, 11, 9, 8 };
  static const uint8_t GOODS[HB_GOODS] = { 10, 11, 11, 9, 9 };
  for (int g = 0; g < HB_GOODS; g++) {
    s->plot_pile[g] = (uint8_t)(PLOTS[g] - (two ? 3 : 0));
    s->goods_supply[g] = (uint8_t)(GOODS[g] - (two ? 2 : 0));
  }
  s->stones = two ? 5 : 8;
  for (int b = 0; b < HB_BUILDINGS; b++) {
    int n;
    if (b == B_DYE_SHED || b == B_KILN) n = two ? 3 : 4;
    else if (b <= B_SPICE_MILL) n = two ? 2 : 3;
    else if (b < B_GUILDHALL) n = two ? 1 : 2;
    else n = 1;
    s->bld_supply[b] = (uint8_t)n;
  }
  s->folk_supply = (uint8_t)(players == 2 ? 40 : players == 3 ? 55 : 75);
  s->ferry = (uint8_t)players;
  s->lantern_supply = (int16_t)(players == 2 ? 65 : players == 3 ? 75 : 100);
  static const uint8_t SHIPS[3][3] = { { 4, 6, 0 }, { 4, 5, 6 }, { 5, 6, 7 } };
  s->nships = two ? 2 : 3;
  for (int k = 0; k < HB_SHIPS; k++) {
    s->ship_cap[k] = SHIPS[players - 2][k];
    s->ship_good[k] = HB_NONE;
  }
  s->roles_in_play = players == 3 ? 6 : 7;
  int coins = players == 3 ? 2 : 3;
  for (int i = 0; i < players; i++) {
    hb_player *pl = &s->p[i];
    pl->coins = (uint8_t)coins;
    /* the first seats start on indigo, the rest on rice */
    int k = (players == 2 ? i < 1 : i < 2) ? G_INDIGO : G_RICE;
    pl->plot[pl->nplots++] = (uint8_t)k;
    s->plot_pile[k]--;
  }
  for (int i = 0; i < HB_ROW; i++) s->row[i] = HB_NONE;
  s->governor = 0;
  s->phase = PH_PICK;
  s->actor = 0;
  s->round = 1;
  s->chance = CH_ROW; /* the open row is drawn first */
}

/* ── moves ─────────────────────────────────────────────────────────── */

static bool row_has(const hb_state *s, int kind) {
  for (int i = 0; i < HB_ROW; i++)
    if (s->row[i] == kind) return true;
  return false;
}

static int pile_total(const hb_state *s) {
  int n = 0;
  for (int g = 0; g < HB_GOODS; g++) n += s->plot_pile[g] + s->plot_discard[g];
  return n;
}

static bool may_open_stone(const hb_state *s, int seat) {
  return s->stones > 0 && (seat == s->picker || hb_staffed(&s->p[seat], B_STONEYARD));
}

static bool good_on_other_ship(const hb_state *s, int good, int ship) {
  for (int k = 0; k < s->nships; k++)
    if (k != ship && s->ship_good[k] == good && s->ship_load[k] > 0) return true;
  return false;
}

/* goods a boat would take; 0 when it cannot */
static int raw_load(const hb_state *s, int seat, int good, int ship) {
  int have = s->p[seat].goods[good];
  if (!have || ship >= s->nships) return 0;
  int room = s->ship_cap[ship] - s->ship_load[ship];
  if (room <= 0) return 0;
  if (s->ship_load[ship] > 0 && s->ship_good[ship] != good) return 0;
  if (s->ship_load[ship] == 0 && good_on_other_ship(s, good, ship)) return 0;
  return have < room ? have : room;
}

int hb_load_amount(const hb_state *s, int seat, int good, int ship) {
  int n = raw_load(s, seat, good, ship);
  if (!n) return 0;
  /* of the boats that can take this good, only those taking the most */
  for (int k = 0; k < s->nships; k++)
    if (raw_load(s, seat, good, k) > n) return 0;
  return n;
}

static bool can_load_any(const hb_state *s, int seat) {
  for (int g = 0; g < HB_GOODS; g++)
    for (int k = 0; k < s->nships; k++)
      if (raw_load(s, seat, g, k)) return true;
  return false;
}

static bool jetty_ready(const hb_state *s, int seat) {
  return hb_staffed(&s->p[seat], B_JETTY) && !((s->jetty_used >> seat) & 1u);
}

/* the targets a worker can go to now: plot kinds with an empty plot, buildings with a free place */
static bool plot_kind_free(const hb_player *pl, int kind) {
  for (int i = 0; i < pl->nplots; i++)
    if (pl->plot[i] == kind && !pl->plot_staffed[i]) return true;
  return false;
}

int hb_moves(const hb_state *s, bk_move *out, int cap) {
  int n = 0;
#define ADD(m) do { if (n >= cap) CW_FATAL("harbor: move buffer of %d is too small", cap); out[n++] = (m); } while (0)
  if (s->chance != CH_NONE || s->phase == PH_OVER) return 0;
  int seat = s->actor;
  const hb_player *pl = &s->p[seat];
  switch (s->phase) {
  case PH_PICK:
    for (int r = 0; r < s->roles_in_play; r++)
      if (!s->role_taken[r]) ADD(hb_move(M_ROLE, r, 0));
    break;
  case PH_SETTLE:
    if (pl->nplots < HB_ISLAND) {
      for (int k = 0; k < HB_GOODS; k++)
        if (row_has(s, k)) ADD(hb_move(M_TAKE, k, 0));
      if (may_open_stone(s, seat)) ADD(hb_move(M_STONE, 0, 0));
      if (!s->seed_used && hb_staffed(pl, B_SEED_SHED) && pile_total(s) > 0 && pl->nplots < HB_ISLAND - 1)
        ADD(hb_move(M_SEED, 0, 0));
    }
    ADD(hb_move(M_PASS, 0, 0));
    break;
  case PH_ARRANGE:
    if (!s->arranged && hb_workers(pl) > pl->dock) ADD(hb_move(M_REARRANGE, 0, 0));
    for (int k = 0; k < HB_PLOT_KINDS; k++)
      if (plot_kind_free(pl, k)) ADD(hb_move(M_PLACE_PLOT, k, 0));
    for (int i = 0; i < pl->nbld; i++) {
      int b = pl->bld[i];
      if (pl->staff[b] < hb_bld[b].slots) ADD(hb_move(M_PLACE_BLD, b, 0));
    }
    break;
  case PH_BUILD:
    for (int b = 0; b < HB_BUILDINGS; b++) {
      if (!s->bld_supply[b] || hb_owns(pl, b)) continue;
      if (hb_town_lots(pl) + hb_bld[b].lots > HB_TOWN) continue;
      if (hb_build_cost(s, seat, b) > pl->coins) continue;
      ADD(hb_move(M_BUILD, b, 0));
    }
    ADD(hb_move(M_PASS, 0, 0));
    break;
  case PH_EXTRA:
    for (int g = 0; g < HB_GOODS; g++)
      if (hb_production(pl, g) > 0 && s->goods_supply[g] > 0) ADD(hb_move(M_EXTRA, g, 0));
    break;
  case PH_TRADE:
    if (s->nmarket < HB_MARKET)
      for (int g = 0; g < HB_GOODS; g++) {
        if (!pl->goods[g]) continue;
        bool in = false;
        for (int k = 0; k < s->nmarket; k++) in |= s->market[k] == g;
        if (in && !hb_staffed(pl, B_COUNTING_HOUSE)) continue;
        ADD(hb_move(M_SELL, g, 0));
      }
    ADD(hb_move(M_PASS, 0, 0));
    break;
  case PH_SHIP: {
    bool any = false;
    for (int g = 0; g < HB_GOODS; g++)
      for (int k = 0; k < s->nships; k++)
        if (hb_load_amount(s, seat, g, k)) { ADD(hb_move(M_LOAD, g, k)); any = true; }
    if (jetty_ready(s, seat))
      for (int g = 0; g < HB_GOODS; g++)
        if (pl->goods[g]) ADD(hb_move(M_JETTY, g, 0));
    if (!any) ADD(hb_move(M_PASS, 0, 0));
    break;
  }
  default: break;
  }
#undef ADD
  return n;
}

/* ── the flow ──────────────────────────────────────────────────────── */

static void lanterns(hb_state *s, int seat, int n) {
  s->p[seat].lanterns = (uint16_t)(s->p[seat].lanterns + n);
  s->lantern_supply = (int16_t)(s->lantern_supply - n);
  if (s->lantern_supply <= 0) s->end_triggered = 1;
}

static void game_over(hb_state *s) { s->phase = PH_OVER; }

static void next_pick(hb_state *s);

/* the job ends: back to taking jobs */
static void role_done(hb_state *s) {
  s->picks++;
  next_pick(s);
}

static void next_pick(hb_state *s) {
  if (s->picks >= hb_picks_per_round(s)) {
    /* the round ends: a coin on every job nobody took */
    for (int r = 0; r < s->roles_in_play; r++) {
      if (!s->role_taken[r]) s->role_coins[r]++;
      s->role_taken[r] = 0;
    }
    if (s->end_triggered) { game_over(s); return; }
    if (s->round >= HB_MAX_ROUNDS) { s->ended_by_cap = 1; game_over(s); return; }
    s->round++;
    s->picks = 0;
    s->governor = (uint8_t)((s->governor + 1) % s->players);
  }
  s->phase = PH_PICK;
  s->actor = (uint8_t)((s->governor + s->picks) % s->players);
}

/* the next actor of a once-round job (surveyor, mason, merchant); false when everyone has acted */
static bool next_actor(hb_state *s) {
  s->acted++;
  if (s->acted >= s->players) return false;
  s->actor = (uint8_t)((s->picker + s->acted) % s->players);
  s->seed_used = 0;
  s->arranged = 0;
  return true;
}

static void draw_plot_into(hb_state *s, cw_rng *rng, uint8_t *dst) {
  int total = 0;
  for (int g = 0; g < HB_GOODS; g++) total += s->plot_pile[g];
  if (total == 0) {
    for (int g = 0; g < HB_GOODS; g++) { s->plot_pile[g] = s->plot_discard[g]; s->plot_discard[g] = 0; total += s->plot_pile[g]; }
    if (total == 0) { *dst = HB_NONE; return; }
  }
  int r = cw_rng_int(rng, 0, total - 1);
  for (int g = 0; g < HB_GOODS; g++) {
    if (r < s->plot_pile[g]) { s->plot_pile[g]--; *dst = (uint8_t)g; return; }
    r -= s->plot_pile[g];
  }
  CW_FATAL("harbor: plot draw out of range");
}

static void place_new_worker(hb_state *s, hb_player *pl, int plot_index) {
  /* the hostel: a worker from the supply (else the ferry) onto the new plot */
  if (s->folk_supply > 0) s->folk_supply--;
  else if (s->ferry > 0) s->ferry--;
  else return;
  pl->plot_staffed[plot_index] = 1;
}

static void settle_auto(hb_state *s);

/* the ferry lands: the ferrymaster's extra, then one each in turn from the ferrymaster */
static void ferry_lands(hb_state *s) {
  if (s->folk_supply > 0) { s->folk_supply--; s->p[s->picker].dock++; }
  for (int k = 0; s->ferry > 0; k++) {
    s->ferry--;
    s->p[(s->picker + k) % s->players].dock++;
  }
}

static void ferry_refill(hb_state *s) {
  int need = 0;
  for (int i = 0; i < s->players; i++) need += empty_building_slots(&s->p[i]);
  if (need < s->players) need = s->players;
  if (s->folk_supply < need) {
    s->ferry = s->folk_supply;
    s->folk_supply = 0;
    s->end_triggered = 1;
  } else {
    s->ferry = (uint8_t)need;
    s->folk_supply = (uint8_t)(s->folk_supply - need);
  }
}

static void produce(hb_state *s) {
  for (int k = 0; k < s->players; k++) {
    int seat = (s->picker + k) % s->players;
    hb_player *pl = &s->p[seat];
    int kinds = 0;
    for (int g = 0; g < HB_GOODS; g++) {
      int n = hb_production(pl, g);
      if (n > s->goods_supply[g]) n = s->goods_supply[g];
      if (n > 0) kinds++;
      s->goods_supply[g] = (uint8_t)(s->goods_supply[g] - n);
      pl->goods[g] = (uint8_t)(pl->goods[g] + n);
    }
    if (hb_staffed(pl, B_COOPERATIVE)) {
      static const uint8_t PAY[6] = { 0, 0, 1, 2, 3, 5 };
      pl->coins = (uint8_t)(pl->coins + PAY[kinds]);
    }
  }
}

/* after the boats sail: full boats unload, and each player keeps what storage allows */
static void ships_sail(hb_state *s) {
  for (int k = 0; k < s->nships; k++)
    if (s->ship_load[k] >= s->ship_cap[k]) {
      s->goods_supply[s->ship_good[k]] = (uint8_t)(s->goods_supply[s->ship_good[k]] + s->ship_load[k]);
      s->ship_load[k] = 0;
      s->ship_good[k] = HB_NONE;
    }
  for (int i = 0; i < s->players; i++) {
    hb_player *pl = &s->p[i];
    int piles = (hb_staffed(pl, B_STOREHOUSE) ? 1 : 0) + (hb_staffed(pl, B_WAREHOUSE) ? 2 : 0);
    uint8_t keep[HB_GOODS] = { 0 };
    /* whole piles: the biggest first (the dearer on a tie) */
    for (int p = 0; p < piles; p++) {
      int best = -1;
      for (int g = HB_GOODS - 1; g >= 0; g--)
        if (!keep[g] && pl->goods[g] && (best < 0 || pl->goods[g] > pl->goods[best])) best = g;
      if (best < 0) break;
      keep[best] = 1;
    }
    /* and one good of the dearest kind left */
    int one = -1;
    for (int g = HB_GOODS - 1; g >= 0 && one < 0; g--)
      if (!keep[g] && pl->goods[g]) one = g;
    for (int g = 0; g < HB_GOODS; g++) {
      if (keep[g]) continue;
      int stay = g == one ? 1 : 0;
      int lose = pl->goods[g] - stay;
      s->goods_supply[g] = (uint8_t)(s->goods_supply[g] + lose);
      pl->goods[g] = (uint8_t)stay;
    }
  }
}

/* the next skipper in turn who has not finished; false when nobody can load */
static bool next_shipper(hb_state *s) {
  for (int k = 1; k <= s->players; k++) {
    int seat = (s->actor + k) % s->players;
    if (!((s->ship_done >> seat) & 1u)) { s->actor = (uint8_t)seat; return true; }
  }
  return false;
}

/* a job's phase reached its end */
static void phase_end(hb_state *s) {
  switch (s->phase) {
  case PH_SETTLE:
    /* the open row is cleared and drawn anew */
    for (int i = 0; i < HB_ROW; i++)
      if (s->row[i] != HB_NONE) { s->plot_discard[s->row[i]]++; s->row[i] = HB_NONE; }
    s->chance = CH_ROW;
    role_done(s);
    return;
  case PH_ARRANGE:
    ferry_refill(s);
    role_done(s);
    return;
  case PH_TRADE:
    if (s->nmarket >= HB_MARKET) {
      for (int k = 0; k < s->nmarket; k++) s->goods_supply[s->market[k]]++;
      s->nmarket = 0;
    }
    role_done(s);
    return;
  case PH_SHIP:
    ships_sail(s);
    role_done(s);
    return;
  default:
    role_done(s);
  }
}

static void begin_role(hb_state *s, int r) {
  s->role = (uint8_t)r;
  s->picker = s->actor;
  s->acted = 0;
  s->seed_used = s->arranged = 0;
  hb_player *pl = &s->p[s->picker];
  pl->coins = (uint8_t)(pl->coins + s->role_coins[r]);
  s->role_coins[r] = 0;
  s->role_taken[r] = 1;
  switch (r) {
  case R_SURVEYOR: s->phase = PH_SETTLE; break;
  case R_FERRYMASTER: ferry_lands(s); s->phase = PH_ARRANGE; break;
  case R_MASON: s->phase = PH_BUILD; break;
  case R_ARTISAN:
    produce(s);
    s->phase = PH_EXTRA;
    break;
  case R_MERCHANT: s->phase = PH_TRADE; break;
  case R_SKIPPER:
    s->phase = PH_SHIP;
    s->skipper_bonus = 0;
    s->jetty_used = 0;
    s->ship_done = 0;
    break;
  case R_DIVER:
    pl->coins++;
    role_done(s);
    return;
  }
}

/* the actor's turn in a once-round job is over */
static void actor_done(hb_state *s) {
  if (!next_actor(s)) phase_end(s);
}

/* fill every free place: when there are no more places than workers, nothing is a choice */
static void fill_all(hb_player *pl) {
  for (int i = 0; i < pl->nplots && pl->dock; i++)
    if (!pl->plot_staffed[i]) { pl->plot_staffed[i] = 1; pl->dock--; }
  for (int i = 0; i < pl->nbld && pl->dock; i++) {
    int b = pl->bld[i];
    while (pl->staff[b] < hb_bld[b].slots && pl->dock) { pl->staff[b]++; pl->dock--; }
  }
}

/* steps no one decides: one-choice turns and turns with nothing to do pass by themselves */
static void settle_auto(hb_state *s) {
  for (int guard = 0; guard < 64; guard++) {
    if (s->chance != CH_NONE || s->phase == PH_OVER) return;
    hb_player *pl = &s->p[s->actor];
    bk_move mv[BK_MAX_MOVES];
    switch (s->phase) {
    case PH_ARRANGE:
      if (pl->dock == 0 || hb_empty_slots(pl) == 0) { actor_done(s); continue; }
      if (hb_empty_slots(pl) <= pl->dock) { fill_all(pl); actor_done(s); continue; }
      return;
    case PH_EXTRA: {
      int n = hb_moves(s, mv, BK_MAX_MOVES);
      if (n == 0) { phase_end(s); continue; }
      if (n == 1) { hb_apply(s, mv[0]); return; }
      return;
    }
    case PH_SETTLE:
    case PH_BUILD:
    case PH_TRADE: {
      int n = hb_moves(s, mv, BK_MAX_MOVES);
      if (n == 1 && hb_move_type(mv[0]) == M_PASS) { actor_done(s); continue; }
      return;
    }
    case PH_SHIP: {
      int n = hb_moves(s, mv, BK_MAX_MOVES);
      if (n == 1 && hb_move_type(mv[0]) == M_PASS) {
        s->ship_done |= (uint8_t)(1u << s->actor);
        if (!next_shipper(s)) phase_end(s);
        continue;
      }
      return;
    }
    default: return;
    }
  }
  CW_FATAL("harbor: the automatic steps did not settle");
}

static void apply_move(hb_state *s, bk_move m) {
  int t = hb_move_type(m), a = hb_move_a(m), b = hb_move_b(m);
  int seat = s->actor;
  hb_player *pl = &s->p[seat];
  switch (t) {
  case M_ROLE: begin_role(s, a); break;
  case M_TAKE: {
    int i = 0;
    while (i < HB_ROW && s->row[i] != a) i++;
    if (i == HB_ROW) CW_FATAL("harbor: no %s in the open row", hb_plot_name[a]);
    s->row[i] = HB_NONE;
    int at = pl->nplots;
    pl->plot[pl->nplots++] = (uint8_t)a;
    if (hb_staffed(pl, B_HOSTEL)) place_new_worker(s, pl, at);
    actor_done(s);
    break;
  }
  case M_STONE: {
    s->stones--;
    int at = pl->nplots;
    pl->plot[pl->nplots++] = HB_STONE;
    if (hb_staffed(pl, B_HOSTEL)) place_new_worker(s, pl, at);
    actor_done(s);
    break;
  }
  case M_SEED:
    s->seed_used = 1;
    s->chance = CH_SEED;
    break;
  case M_PASS:
    if (s->phase == PH_SHIP) {
      s->ship_done |= (uint8_t)(1u << seat);
      if (!next_shipper(s)) phase_end(s);
    } else {
      actor_done(s);
    }
    break;
  case M_REARRANGE:
    for (int i = 0; i < pl->nplots; i++)
      if (pl->plot_staffed[i]) { pl->plot_staffed[i] = 0; pl->dock++; }
    for (int i = 0; i < pl->nbld; i++) { pl->dock = (uint8_t)(pl->dock + pl->staff[pl->bld[i]]); pl->staff[pl->bld[i]] = 0; }
    s->arranged = 1;
    break;
  case M_PLACE_PLOT:
    for (int i = 0; i < pl->nplots; i++)
      if (pl->plot[i] == a && !pl->plot_staffed[i]) { pl->plot_staffed[i] = 1; break; }
    pl->dock--;
    s->arranged = 1;
    break;
  case M_PLACE_BLD:
    pl->staff[a]++;
    pl->dock--;
    s->arranged = 1;
    break;
  case M_BUILD: {
    int cost = hb_build_cost(s, seat, a);
    pl->coins = (uint8_t)(pl->coins - cost);
    s->bld_supply[a]--;
    pl->bld[pl->nbld++] = (uint8_t)a;
    pl->built |= 1u << a;
    if (hb_staffed(pl, B_ACADEMY) && s->folk_supply > 0) { s->folk_supply--; pl->staff[a] = 1; }
    else if (hb_staffed(pl, B_ACADEMY) && s->ferry > 0) { s->ferry--; pl->staff[a] = 1; }
    if (hb_town_lots(pl) >= HB_TOWN) s->end_triggered = 1;
    actor_done(s);
    break;
  }
  case M_EXTRA:
    s->goods_supply[a]--;
    pl->goods[a]++;
    phase_end(s);
    break;
  case M_SELL: {
    pl->goods[a]--;
    pl->coins = (uint8_t)(pl->coins + hb_sale_price(s, seat, a));
    s->market[s->nmarket++] = (uint8_t)a;
    actor_done(s);
    break;
  }
  case M_LOAD:
  case M_JETTY: {
    int n;
    if (t == M_LOAD) {
      n = hb_load_amount(s, seat, a, b);
      s->ship_good[b] = (uint8_t)a;
      s->ship_load[b] = (uint8_t)(s->ship_load[b] + n);
    } else {
      n = pl->goods[a];
      s->goods_supply[a] = (uint8_t)(s->goods_supply[a] + n);
      s->jetty_used |= (uint8_t)(1u << seat);
    }
    pl->goods[a] = (uint8_t)(pl->goods[a] - n);
    int pts = n + (hb_staffed(pl, B_BEACON) ? 1 : 0);
    if (seat == s->picker && !s->skipper_bonus) { pts++; s->skipper_bonus = 1; }
    lanterns(s, seat, pts);
    if (!next_shipper(s)) phase_end(s);
    break;
  }
  default: CW_FATAL("harbor: unknown move type %d", t);
  }
}

void hb_apply(hb_state *s, bk_move m) {
  if (s->chance != CH_NONE) CW_FATAL("harbor: a move at a chance node");
  if (s->phase == PH_OVER) CW_FATAL("harbor: a move after the game ended");
  apply_move(s, m);
  s->moves_played++;
  settle_auto(s);
}

int hb_turn(const hb_state *s) { return s->chance != CH_NONE ? BK_CHANCE : s->actor; }
bool hb_terminal(const hb_state *s) { return s->phase == PH_OVER; }

void hb_chance(hb_state *s, cw_rng *rng) {
  if (s->chance == CH_ROW) {
    for (int i = 0; i < s->players + 1 && i < HB_ROW; i++) draw_plot_into(s, rng, &s->row[i]);
    s->chance = CH_NONE;
  } else if (s->chance == CH_SEED) {
    hb_player *pl = &s->p[s->actor];
    uint8_t k = HB_NONE;
    draw_plot_into(s, rng, &k);
    s->chance = CH_NONE;
    if (k != HB_NONE) pl->plot[pl->nplots++] = k;
  } else {
    CW_FATAL("harbor: chance with nothing pending");
  }
  settle_auto(s);
}

/* ── evaluation ────────────────────────────────────────────────────── */

/* a seat's expected final points from here: points banked, plus what its coins, goods and working town are likely
 * to turn into in the rounds left */
static float seat_value(const hb_state *s, int seat) {
  const hb_player *pl = &s->p[seat];
  float v = (float)hb_score(pl);
  /* how far along the game is: the nearest end */
  float left_lanterns = (float)s->lantern_supply / (s->players == 2 ? 65.0f : s->players == 3 ? 75.0f : 100.0f);
  float left_folk = (float)s->folk_supply / (s->players == 2 ? 40.0f : s->players == 3 ? 55.0f : 75.0f);
  float left_town = 1.0f;
  for (int i = 0; i < s->players; i++) {
    float t = 1.0f - (float)hb_town_lots(&s->p[i]) / HB_TOWN;
    if (t < left_town) left_town = t;
  }
  float left = left_lanterns < left_folk ? left_lanterns : left_folk;
  if (left_town < left) left = left_town;
  if (left < 0) left = 0;
  float rounds = 1.0f + 12.0f * left;
  int make = 0, value = 0;
  for (int g = 0; g < HB_GOODS; g++) {
    int n = hb_production(pl, g);
    make += n;
    value += n * PRICE[g];
    v += pl->goods[g] * (0.55f + 0.12f * PRICE[g]);
  }
  v += 0.32f * (float)pl->coins;
  /* production turns into lanterns and coins in later rounds */
  v += rounds * (0.22f * (float)make + 0.05f * (float)value);
  /* buildings in a short game are worth less than their cost; staffed guild houses keep paying */
  for (int i = 0; i < pl->nbld; i++) {
    int b = pl->bld[i];
    if (hb_bld[b].good < 0 && b < B_GUILDHALL && pl->staff[b]) v += 0.04f * rounds * (float)hb_bld[b].cost;
    if (b >= B_GUILDHALL && !pl->staff[b]) v += 0.5f * (float)hb_bonus(&(hb_player){ 0 }, b);
  }
  v += 0.15f * (float)hb_staffed_stones(pl) * rounds * 0.25f;
  v += 0.1f * (float)(pl->nplots) + 0.12f * (float)pl->dock;
  return v;
}

void hb_evaluate(const hb_state *s, float score[BK_MAX_SEATS]) {
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  if (s->phase == PH_OVER) {
    unsigned w = hb_winners(s);
    for (int i = 0; i < s->players; i++) {
      int best_other = -1000;
      for (int j = 0; j < s->players; j++)
        if (j != i && hb_score(&s->p[j]) > best_other) best_other = hb_score(&s->p[j]);
      float margin = (float)(hb_score(&s->p[i]) - best_other);
      score[i] = ((w >> i) & 1u) ? BK_WIN + margin - (float)s->moves_played * 0.001f : BK_LOSS + margin;
    }
    return;
  }
  for (int i = 0; i < s->players; i++) score[i] = seat_value(s, i);
}

uint64_t hb_hash(const hb_state *s) { return cw_hash_bytes(s, sizeof *s, 0); }

/* ── labels ────────────────────────────────────────────────────────── */

void hb_move_label(const hb_state *s, bk_move m, char *buf, int cap) {
  int a = hb_move_a(m), b = hb_move_b(m);
  switch (hb_move_type(m)) {
  case M_PASS: snprintf(buf, (size_t)cap, "pass"); break;
  case M_ROLE: snprintf(buf, (size_t)cap, "take the %s", hb_role_name[a]); break;
  case M_TAKE: snprintf(buf, (size_t)cap, "claim a %s", hb_plot_name[a]); break;
  case M_STONE: snprintf(buf, (size_t)cap, "open a stone pit"); break;
  case M_SEED: snprintf(buf, (size_t)cap, "draw a hidden plot"); break;
  case M_REARRANGE: snprintf(buf, (size_t)cap, "call every worker back"); break;
  case M_PLACE_PLOT: snprintf(buf, (size_t)cap, "a worker to a %s", hb_plot_name[a]); break;
  case M_PLACE_BLD: snprintf(buf, (size_t)cap, "a worker to the %s", hb_bld[a].name); break;
  case M_BUILD: snprintf(buf, (size_t)cap, "raise the %s", hb_bld[a].name); break;
  case M_EXTRA: snprintf(buf, (size_t)cap, "one extra %s", hb_good_name[a]); break;
  case M_SELL: snprintf(buf, (size_t)cap, "sell %s for %d", hb_good_name[a], hb_sale_price(s, s->actor, a)); break;
  case M_LOAD: snprintf(buf, (size_t)cap, "load %d %s on boat %d", hb_load_amount(s, s->actor, a, b), hb_good_name[a], b + 1); break;
  case M_JETTY: snprintf(buf, (size_t)cap, "ship %s off the jetty", hb_good_name[a]); break;
  default: snprintf(buf, (size_t)cap, "?");
  }
}

/* ── bk_game glue ──────────────────────────────────────────────────── */

static int g_moves(const void *st, bk_move *out, int cap, void *u) { (void)u; return hb_moves(st, out, cap); }
static void g_apply(void *st, bk_move m, void *u) { (void)u; hb_apply(st, m); }
static int g_turn(const void *st, void *u) { (void)u; return hb_turn(st); }
static bool g_terminal(const void *st, void *u) { (void)u; return hb_terminal(st); }
static void g_evaluate(const void *st, float sc[BK_MAX_SEATS], void *u) { (void)u; hb_evaluate(st, sc); }
static uint64_t g_hash(const void *st, void *u) { (void)u; return hb_hash(st); }
static void g_chance(void *st, cw_rng *rng, void *u) { (void)u; hb_chance(st, rng); }

/* a rough worth of a move to the one making it: move ordering, and the playout policy's weights */
float hb_move_worth(const hb_state *s, bk_move m) {
  int a = hb_move_a(m), b = hb_move_b(m);
  const hb_player *pl = &s->p[s->actor];
  switch (hb_move_type(m)) {
  case M_PASS: return 0.0f;
  case M_ROLE: {
    float w = 1.0f + 1.2f * s->role_coins[a];
    if (a == R_SKIPPER) { int g = 0; for (int k = 0; k < HB_GOODS; k++) g += pl->goods[k]; w += 0.6f * g; }
    if (a == R_MERCHANT) { int best = 0; for (int k = 0; k < HB_GOODS; k++) if (pl->goods[k] && PRICE[k] > best) best = PRICE[k]; w += best; }
    if (a == R_ARTISAN) { int make = 0; for (int k = 0; k < HB_GOODS; k++) make += hb_production(pl, k); w += 0.5f * make; }
    if (a == R_MASON) w += pl->coins >= 4 ? 2.0f : 0.5f;
    if (a == R_FERRYMASTER) w += 0.3f * hb_empty_slots(pl);
    return w;
  }
  case M_TAKE: return 1.0f + (a == G_RICE ? 0.6f : 0.25f * a);
  case M_STONE: return 1.6f;
  case M_SEED: return 1.2f;
  case M_REARRANGE: return 0.05f;
  case M_PLACE_PLOT: {
    if (a == HB_STONE) return 1.2f;
    int make = hb_production(pl, a);
    hb_player t = *pl;
    for (int i = 0; i < t.nplots; i++)
      if (t.plot[i] == a && !t.plot_staffed[i]) { t.plot_staffed[i] = 1; break; }
    return 0.5f + 2.0f * (float)(hb_production(&t, a) - make);
  }
  case M_PLACE_BLD: {
    if (hb_bld[a].good < 0) return 2.0f + 0.2f * hb_bld[a].cost;
    int g = hb_bld[a].good, make = hb_production(pl, g);
    hb_player t = *pl;
    t.staff[a]++;
    return 0.4f + 2.0f * (float)(hb_production(&t, g) - make);
  }
  case M_BUILD: return 1.0f + 0.5f * hb_bld[a].value + 0.15f * hb_bld[a].cost;
  case M_EXTRA: return 1.0f + PRICE[a];
  case M_SELL: return 1.0f + (float)hb_sale_price(s, s->actor, a);
  case M_LOAD: return 1.0f + (float)hb_load_amount(s, s->actor, a, b) - 0.2f * PRICE[a];
  case M_JETTY: return 1.0f + (float)pl->goods[a];
  default: return 0.0f;
  }
}

static float g_priority(const void *st, bk_move m, void *u) { (void)u; return hb_move_worth(st, m); }

/* playouts: moves picked in proportion to their worth (squared), so a playout looks like a plausible game */
static bk_move g_playout(const void *st, const bk_move *moves, int n, cw_rng *rng, void *u) {
  (void)u;
  float w[BK_MAX_MOVES], total = 0;
  for (int i = 0; i < n; i++) {
    float x = hb_move_worth(st, moves[i]) + 0.05f;
    w[i] = x * x;
    total += w[i];
  }
  float r = cw_rng_float(rng) * total;
  for (int i = 0; i < n; i++) {
    if (r < w[i]) return moves[i];
    r -= w[i];
  }
  return moves[n - 1];
}

void hb_game(bk_game *g, int players) {
  memset(g, 0, sizeof *g);
  g->players = players;
  g->state_size = sizeof(hb_state);
  g->moves = g_moves;
  g->apply = g_apply;
  g->turn = g_turn;
  g->terminal = g_terminal;
  g->evaluate = g_evaluate;
  g->hash = g_hash;
  g->chance = g_chance;
  g->priority = g_priority;
  g->playout = g_playout;
}
