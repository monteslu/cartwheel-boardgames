/* Fever Line: the rules. See rules.h. */
#include "rules.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const int FV_RATES[FV_NRATES] = { 2, 2, 2, 3, 3, 3, 4 };
static const char *const ROLE_NAMES[FV_NROLES] = { "Field Medic", "Analyst", "Courier", "Engineer", "Warden" };
static const char *const ROLE_POWERS[FV_NROLES] = {
  "Treat takes every cube of a fever. Cured fevers vanish where she stands.",
  "Cures a fever with one card fewer than the others need.",
  "Gives any city card to an agent beside her, and they may take any from her.",
  "Builds a clinic without spending a card.",
  "No cube lands in her city or any city on its roads.",
};

const char *fv_role_name(int role) { return role >= 0 && role < FV_NROLES ? ROLE_NAMES[role] : "?"; }
const char *fv_role_power(int role) { return role >= 0 && role < FV_NROLES ? ROLE_POWERS[role] : "?"; }
int fv_surges_for(int setup) { return setup <= FV_INTRO ? 3 : setup == FV_NORMAL ? 4 : 5; }

/* ── the map's distances (roads), worked out once ───────────────────── */

int fv_map_active;
void fv_use_map(int map) {
  if (map < 0 || map >= FV_NMAPS) CW_FATAL("feverline: no map %d", map);
  fv_map_active = map;
}
static int8_t DISTS[FV_NMAPS][FV_NCITY][FV_NCITY];
static bool dist_ready[FV_NMAPS];
#define DIST DISTS[fv_map_active]
static void dist_init(void) {
  if (dist_ready[fv_map_active]) return;
  for (int a = 0; a < FV_N; a++) {
    int q[FV_NCITY], h = 0, t = 0;
    for (int b = 0; b < FV_N; b++) DIST[a][b] = -1;
    DIST[a][a] = 0;
    q[t++] = a;
    while (h < t) {
      int c = q[h++];
      for (int k = 0; k < FV_CITIES[c].nnb; k++) {
        int n = FV_CITIES[c].nb[k];
        if (DIST[a][n] < 0) DIST[a][n] = (int8_t)(DIST[a][c] + 1), q[t++] = n;
      }
    }
  }
  dist_ready[fv_map_active] = true;
}
int fv_dist(int a, int b) { dist_init(); return DIST[a][b]; }
bool fv_adjacent(int a, int b) {
  for (int k = 0; k < FV_CITIES[a].nnb; k++)
    if (FV_CITIES[a].nb[k] == b) return true;
  return false;
}

/* ── queries ────────────────────────────────────────────────────────── */

int fv_hand(const fv_state *s, int r, int *cards, int cap) {
  int n = 0;
  for (int c = 0; c < FV_N; c++)
    if (s->pcard[c] == r && n < cap) cards[n++] = c;
  return n;
}
int fv_hand_count(const fv_state *s, int r) {
  int n = 0;
  for (int c = 0; c < FV_N; c++) n += s->pcard[c] == r;
  return n;
}
int fv_hand_colour(const fv_state *s, int r, int colour) {
  int n = 0;
  for (int c = 0; c < FV_N; c++) n += s->pcard[c] == r && fv_colour(c) == colour;
  return n;
}
int fv_cure_base(void) { return FV_N >= 44 ? 5 : FV_N >= 28 ? 4 : 3; }
int fv_cure_need(const fv_state *s, int r) { return fv_cure_base() - (s->role[r] == FV_ANALYST); }
int fv_deck_left(const fv_state *s) {
  int n = 0;
  for (int c = 0; c < FV_NCARDS; c++) n += s->pcard[c] >= FV_PC_DECK && s->pcard[c] < FV_PC_DISCARD;
  return n;
}
int fv_rate(const fv_state *s) { return FV_RATES[s->rate_idx < FV_NRATES ? s->rate_idx : FV_NRATES - 1]; }
int fv_cubes_on_map(const fv_state *s, int colour) { return FV_CUBES - s->supply[colour]; }
int fv_clinics(const fv_state *s) { return s->clinics; }
uint64_t fv_hash(const fv_state *s) { return cw_hash_bytes(s, sizeof *s, 0); }

static int role_of(const fv_state *s, int kind) {
  for (int r = 0; r < s->roles; r++)
    if (s->role[r] == kind) return r;
  return -1;
}

bool fv_protected(const fv_state *s, int city, int colour) {
  int w = role_of(s, FV_WARDEN);
  if (w >= 0 && (s->loc[w] == city || fv_adjacent(s->loc[w], city))) return true;
  int m = role_of(s, FV_MEDIC);
  if (m >= 0 && s->cured[colour] && s->loc[m] == city) return true;
  return false;
}

/* ── changes ────────────────────────────────────────────────────────── */

static void lose(fv_state *s, int why) {
  if (s->result != FV_PLAYING) return;
  s->result = FV_LOST;
  s->lose_reason = (int16_t)why;
  s->phase = FV_PH_OVER;
}

static void check_wiped(fv_state *s) {
  for (int c = 0; c < FV_NCOL; c++)
    if (s->cured[c] == 1 && s->supply[c] == FV_CUBES) s->cured[c] = 2;
}

/* one cube of a fever onto a city, an outbreak chain if it already has three */
static void add_cubes(fv_state *s, int city, int colour, int n) {
  if (s->cured[colour] == 2 || fv_protected(s, city, colour)) return;
  for (int k = 0; k < n && s->result == FV_PLAYING; k++) {
    if (s->cubes[city][colour] < 3) {
      if (s->supply[colour] == 0) { lose(s, FV_LOSE_CUBES); return; }
      s->cubes[city][colour]++;
      s->supply[colour]--;
      continue;
    }
    /* an outbreak, spreading breadth first, each city once */
    bool done[FV_NCITY] = { false };
    int q[FV_NCITY], h = 0, t = 0;
    q[t++] = city;
    done[city] = true;
    while (h < t && s->result == FV_PLAYING) {
      int c = q[h++];
      s->outbreaks++;
      if (s->ob_n < FV_NCITY) s->ob_city[s->ob_n++] = (int16_t)c;
      if (s->outbreaks >= FV_MAX_OUTBREAKS) { lose(s, FV_LOSE_OUTBREAKS); return; }
      for (int j = 0; j < FV_CITIES[c].nnb; j++) {
        int nb = FV_CITIES[c].nb[j];
        if (fv_protected(s, nb, colour)) continue;
        if (s->cubes[nb][colour] == 3) {
          if (!done[nb]) done[nb] = true, q[t++] = nb;
          continue;
        }
        if (s->supply[colour] == 0) { lose(s, FV_LOSE_CUBES); return; }
        s->cubes[nb][colour]++;
        s->supply[colour]--;
      }
    }
    return;
  }
}

/* the Medic clears cured fevers wherever she stands */
static void medic_sweep(fv_state *s) {
  int m = role_of(s, FV_MEDIC);
  if (m < 0) return;
  int city = s->loc[m];
  for (int c = 0; c < FV_NCOL; c++)
    if (s->cured[c] && s->cubes[city][c]) {
      s->supply[c] += s->cubes[city][c];
      s->cubes[city][c] = 0;
    }
  check_wiped(s);
}

/* the fever deck: its top layer always holds a card (the discards come back as a layer when it runs dry) */
static void inf_norm(fv_state *s) {
  for (;;) {
    while (s->itop > 0) {
      bool any = false;
      for (int c = 0; c < FV_N && !any; c++) any = s->icard[c] == s->itop;
      if (any) return;
      s->itop--;
    }
    s->itop = 1;
    for (int c = 0; c < FV_N; c++)
      if (s->icard[c] == 0) s->icard[c] = 1;
  }
}
static int inf_bottom(const fv_state *s) {
  int lo = 255;
  for (int c = 0; c < FV_N; c++)
    if (s->icard[c] >= 1 && s->icard[c] < lo) lo = s->icard[c];
  return lo == 255 ? 0 : lo;
}
static int deck_top_pile(const fv_state *s) {
  int lo = 255;
  for (int c = 0; c < FV_NCARDS; c++)
    if (s->pcard[c] >= FV_PC_DECK && s->pcard[c] < FV_PC_DISCARD && s->pcard[c] < lo) lo = s->pcard[c];
  return lo;
}

static void end_turn(fv_state *s) {
  s->cur = (int16_t)((s->cur + 1) % s->roles);
  s->phase = FV_PH_ACT;
  s->acts = FV_ACTIONS;
  s->turns++;
}
static void begin_infect(fv_state *s) {
  s->phase = FV_PH_INFECT;
  s->infects = (int16_t)(fv_rate(s) - s->calm > 1 ? fv_rate(s) - s->calm : 1);
  s->calm = 0;
  inf_norm(s);
}
/* after the draws: anyone over the hand limit discards, then the spread */
static void after_draws(fv_state *s) {
  for (int r = 0; r < s->roles; r++)
    if (fv_hand_count(s, r) > FV_HAND_MAX) {
      s->phase = FV_PH_DISCARD;
      s->disc_role = (int16_t)r;
      return;
    }
  begin_infect(s);
}
static void next_draw(fv_state *s) {
  if (s->draws <= 0) { after_draws(s); return; }
  if (deck_top_pile(s) == 255) { lose(s, FV_LOSE_DECK); return; }
  s->phase = FV_PH_DRAW;
}
static void spend_action(fv_state *s) {
  if (s->phase != FV_PH_ACT) return;
  if (--s->acts <= 0) {
    s->draws = 2;
    next_draw(s);
  }
}

/* ── setup ──────────────────────────────────────────────────────────── */

void fv_init(fv_state *s, int roles, int setup, int map, uint64_t seed) {
  if (roles < 2 || roles > FV_MAX_R) CW_FATAL("feverline: %d agents (2..4)", roles);
  fv_use_map(map);
  dist_init();
  memset(s, 0, sizeof *s);
  s->map = (int16_t)map;
  s->ncity = (int16_t)FV_N;
  for (int c = 0; c < FV_NCARDS; c++) s->pcard[c] = FV_PC_OUT; /* a map with fewer cities leaves cards out */
  cw_rng r;
  cw_rng_seed(&r, seed ^ 0xfe7e811eull);
  s->roles = 0; /* no agents on the map during the first infections */
  s->setup = (int16_t)setup;
  s->surges = (int16_t)fv_surges_for(setup);
  /* roles: four or fewer of the five, dealt */
  int pool[FV_NROLES] = { 0, 1, 2, 3, 4 };
  for (int i = FV_NROLES - 1; i > 0; i--) {
    int j = cw_rng_int(&r, 0, i), t = pool[i];
    pool[i] = pool[j], pool[j] = t;
  }
  s->clinic[FV_START_CITY] = 1;
  s->clinics = 1;
  for (int c = 0; c < FV_NCOL; c++) s->supply[c] = FV_CUBES;
  for (int c = 0; c < FV_N; c++) s->icard[c] = 1;
  s->itop = 1;
  /* nine cities catch it: 3, 3, 3, 2, 2, 2, 1, 1, 1 (six on a small map: 3, 3, 2, 2, 1, 1) */
  int first = FV_N >= 30 ? 9 : 6;
  for (int k = 0; k < first; k++) {
    int left = 0;
    for (int c = 0; c < FV_N; c++) left += s->icard[c] == 1;
    int pick = cw_rng_int(&r, 0, left - 1), city = -1;
    for (int c = 0; c < FV_N; c++)
      if (s->icard[c] == 1 && pick-- == 0) { city = c; break; }
    s->icard[city] = 0;
    add_cubes(s, city, fv_colour(city), 3 - k / (first / 3));
  }
  s->ob_n = 0;
  /* the agents arrive after the first cases (a Warden protects nothing yet) */
  s->roles = (int16_t)roles;
  for (int i = 0; i < roles; i++) s->role[i] = (int16_t)pool[i], s->loc[i] = FV_START_CITY;
  /* hands */
  int deck[FV_NCITY], n = 0;
  for (int c = 0; c < FV_N; c++) deck[n++] = c;
  for (int i = n - 1; i > 0; i--) {
    int j = cw_rng_int(&r, 0, i), t = deck[i];
    deck[i] = deck[j], deck[j] = t;
  }
  int each = roles == 2 ? 4 : roles == 3 ? 3 : 2, at = 0;
  for (int p = 0; p < roles; p++)
    for (int k = 0; k < each; k++) s->pcard[deck[at++]] = (uint8_t)p;
  /* the rest cut into piles, a surge in each, stacked */
  int rest = n - at, piles = s->surges;
  for (int k = 0; k < rest; k++) {
    int pile = k * piles / rest;
    s->pcard[deck[at + k]] = (uint8_t)(FV_PC_DECK + pile);
  }
  for (int k = 0; k < FV_MAX_SURGE; k++) s->pcard[FV_SURGE0 + k] = k < piles ? (uint8_t)(FV_PC_DECK + k) : FV_PC_OUT;
  for (int k = 0; k < FV_MAPS[map].calm && k < FV_MAX_CALM; k++) s->pcard[FV_CALM0 + k] = (uint8_t)(FV_PC_DECK + cw_rng_int(&r, 0, piles - 1));
  s->phase = FV_PH_ACT;
  s->acts = FV_ACTIONS;
  s->cur = 0;
  s->ev[0] = FV_EV_NONE;
}

/* ── moves ──────────────────────────────────────────────────────────── */

static bk_move mv(int kind, int a, int b) { return bk_move_pack(kind, a, b, 0, 0, 0, 0, 0); }

bk_move fv_move_to(const fv_state *s, int city) {
  int r = s->cur, at = s->loc[r];
  if (city == at || s->phase != FV_PH_ACT) return BK_MOVE_NONE;
  if (fv_adjacent(at, city)) return mv(FV_M_DRIVE, city, 0);
  if (s->clinic[at] && s->clinic[city]) return mv(FV_M_SHUTTLE, city, 0);
  if (s->pcard[city] == r) return mv(FV_M_FLY, city, 0);
  if (s->pcard[at] == r) return mv(FV_M_CHARTER, city, 0);
  return BK_MOVE_NONE;
}

/* cards of a fever still in play (hands and the deck) beyond what its cheapest cure needs */
static int fever_slack(const fv_state *s, int col) {
  int avail = 0, need = 5;
  for (int c = 0; c < FV_N; c++) avail += fv_colour(c) == col && s->pcard[c] != FV_PC_DISCARD;
  for (int r = 0; r < s->roles; r++) need = fv_cure_need(s, r) < need ? fv_cure_need(s, r) : need;
  return avail - need;
}

/* every legal move, or (sensible) only those the planner weighs */
static int list_moves(const fv_state *s, bk_move *out, int cap, bool sensible) {
  int n = 0;
#define PUSH(m) do { if (n < cap) out[n++] = (m); } while (0)
  if (s->result != FV_PLAYING) return 0;
  if (s->phase == FV_PH_DISCARD) {
    int r = s->disc_role;
    for (int c = 0; c < FV_N; c++)
      if (s->pcard[c] == r) PUSH(mv(FV_M_DISCARD, c, 0));
    return n;
  }
  if (s->phase != FV_PH_ACT) return 0;
  int r = s->cur, at = s->loc[r], need = fv_cure_need(s, r);
  /* cure */
  if (s->clinic[at])
    for (int c = 0; c < FV_NCOL; c++)
      if (!s->cured[c] && fv_hand_colour(s, r, c) >= need) PUSH(mv(FV_M_CURE, c, 0));
  /* treat */
  for (int c = 0; c < FV_NCOL; c++)
    if (s->cubes[at][c]) PUSH(mv(FV_M_TREAT, c, 0));
  /* build */
  if (!s->clinic[at] && s->clinics < FV_MAX_CLINICS && (s->role[r] == FV_ENGINEER || s->pcard[at] == r)) {
    bool near = false;
    if (sensible)
      for (int c = 0; c < FV_N && !near; c++) near = s->clinic[c] && fv_dist(c, at) <= 1;
    if (!near && (!sensible || s->role[r] == FV_ENGINEER || s->cured[fv_colour(at)] || fever_slack(s, fv_colour(at)) >= 2)) PUSH(mv(FV_M_BUILD, 0, 0));
  }
  /* share */
  for (int o = 0; o < s->roles; o++) {
    if (o == r || s->loc[o] != at) continue;
    for (int c = 0; c < FV_N; c++) {
      bool any_give = s->role[r] == FV_COURIER, any_take = s->role[o] == FV_COURIER;
      if (s->pcard[c] == r && (c == at || any_give)) PUSH(mv(FV_M_GIVE, c, o));
      if (s->pcard[c] == o && (c == at || any_take)) PUSH(mv(FV_M_TAKE, c, o));
    }
  }
  /* drive */
  for (int k = 0; k < FV_CITIES[at].nnb; k++) PUSH(mv(FV_M_DRIVE, FV_CITIES[at].nb[k], 0));
  /* shuttle */
  if (s->clinic[at])
    for (int c = 0; c < FV_N; c++)
      if (c != at && s->clinic[c]) PUSH(mv(FV_M_SHUTTLE, c, 0));
  /* flights: sensible ones go where cubes pile up, to a clinic, or to a teammate */
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1 && s->pcard[at] != r) break;
    for (int c = 0; c < FV_N; c++) {
      if (c == at || fv_adjacent(at, c)) continue;
      if (pass == 0 && s->pcard[c] != r) continue;
      if (sensible) {
        int most = 0;
        for (int k = 0; k < FV_NCOL; k++) most = s->cubes[c][k] > most ? s->cubes[c][k] : most;
        bool mate = false;
        for (int o = 0; o < s->roles; o++) mate |= o != r && s->loc[o] == c;
        if (most < 3 && !s->clinic[c] && !mate) continue;
        if (s->clinic[at] && s->clinic[c]) continue; /* a shuttle does it free */
        /* never spend a card the team needs for a cure it is close to */
        int spend = pass == 0 ? c : at, col = fv_colour(spend);
        if (!s->cured[col] && (fv_hand_colour(s, r, col) >= need - 2 || fever_slack(s, col) < 3)) continue;
      }
      PUSH(mv(pass == 0 ? FV_M_FLY : FV_M_CHARTER, c, 0));
    }
  }
  if (!sensible || n == 0) PUSH(mv(FV_M_REST, 0, 0)); /* the planner never wastes actions */
#undef PUSH
  return n;
}

int fv_moves(const void *state, bk_move *out, int cap, void *user) {
  (void)user;
  return list_moves(state, out, cap, false);
}
static int fv_moves_sensible(const void *state, bk_move *out, int cap, void *user) {
  (void)user;
  return list_moves(state, out, cap, true);
}

static void discard_card(fv_state *s, int c) { s->pcard[c] = FV_PC_DISCARD; }

void fv_apply(void *state, bk_move m, void *user) {
  (void)user;
  fv_state *s = state;
  if (s->map != fv_map_active) CW_FATAL("feverline: a state of map %d while map %d is in use", s->map, fv_map_active);
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  int r = s->cur;
  s->ob_n = 0;
  if (kind < FV_M_DRAW) s->moves_played++;
  switch (kind) {
  case FV_M_DRIVE: case FV_M_FLY: case FV_M_CHARTER: case FV_M_SHUTTLE: {
    int from = s->loc[r];
    if (kind == FV_M_FLY) discard_card(s, a);
    if (kind == FV_M_CHARTER) discard_card(s, from);
    s->loc[r] = (int16_t)a;
    s->ev[0] = FV_EV_MOVE, s->ev[1] = (int16_t)from, s->ev[2] = (int16_t)a, s->ev[3] = (int16_t)kind;
    medic_sweep(s);
    spend_action(s);
    break;
  }
  case FV_M_BUILD: {
    int at = s->loc[r];
    if (s->role[r] != FV_ENGINEER) discard_card(s, at);
    s->clinic[at] = 1;
    s->clinics++;
    s->ev[0] = FV_EV_BUILD, s->ev[1] = (int16_t)at, s->ev[2] = 0, s->ev[3] = 0;
    spend_action(s);
    break;
  }
  case FV_M_TREAT: {
    int at = s->loc[r], took = s->role[r] == FV_MEDIC || s->cured[a] ? s->cubes[at][a] : 1;
    s->cubes[at][a] = (uint8_t)(s->cubes[at][a] - took);
    s->supply[a] = (uint8_t)(s->supply[a] + took);
    check_wiped(s);
    s->ev[0] = FV_EV_TREAT, s->ev[1] = (int16_t)at, s->ev[2] = (int16_t)a, s->ev[3] = (int16_t)took;
    spend_action(s);
    break;
  }
  case FV_M_GIVE: case FV_M_TAKE: {
    s->pcard[a] = (uint8_t)(kind == FV_M_GIVE ? b : r);
    s->ev[0] = FV_EV_SHARE, s->ev[1] = (int16_t)a, s->ev[2] = (int16_t)(kind == FV_M_GIVE ? r : b),
    s->ev[3] = (int16_t)(kind == FV_M_GIVE ? b : r);
    spend_action(s);
    int to = kind == FV_M_GIVE ? b : r;
    if (s->result == FV_PLAYING && fv_hand_count(s, to) > FV_HAND_MAX) { /* over the limit: discard now */
      s->disc_back = (int16_t)(s->phase + 1);
      s->phase = FV_PH_DISCARD;
      s->disc_role = (int16_t)to;
    }
    break;
  }
  case FV_M_CURE: {
    int need = fv_cure_need(s, r), left = need;
    for (int c = 0; c < FV_N && left > 0; c++)
      if (s->pcard[c] == r && fv_colour(c) == a) discard_card(s, c), left--;
    s->cured[a] = 1;
    s->ev[0] = FV_EV_CURE, s->ev[1] = (int16_t)a, s->ev[2] = (int16_t)s->loc[r], s->ev[3] = 0;
    check_wiped(s);
    medic_sweep(s);
    bool all = true;
    for (int c = 0; c < FV_NCOL; c++) all &= s->cured[c] != 0;
    if (all) {
      s->result = FV_WON;
      s->phase = FV_PH_OVER;
      break;
    }
    spend_action(s);
    break;
  }
  case FV_M_REST:
    s->ev[0] = FV_EV_REST, s->ev[1] = (int16_t)s->acts, s->ev[2] = s->ev[3] = 0;
    s->acts = 1;
    spend_action(s);
    break;
  case FV_M_DISCARD:
    discard_card(s, a);
    s->ev[0] = FV_EV_DISCARD, s->ev[1] = (int16_t)a, s->ev[2] = s->disc_role, s->ev[3] = 0;
    if (s->disc_back) { /* back to the turn the share interrupted */
      s->phase = (int16_t)(s->disc_back - 1);
      s->disc_back = 0;
      break;
    }
    after_draws(s);
    break;
  case FV_M_DRAW: {
    s->draws--;
    if (fv_is_calm(a)) {
      s->pcard[a] = FV_PC_OUT;
      s->calm++;
      s->ev[0] = FV_EV_CALM, s->ev[1] = -1, s->ev[2] = s->calm, s->ev[3] = 0;
      next_draw(s);
      break;
    }
    if (fv_is_surge(a)) {
      s->pcard[a] = FV_PC_OUT;
      if (s->rate_idx < FV_NRATES - 1) s->rate_idx++;
      s->phase = FV_PH_SURGE;
      s->ev[0] = FV_EV_SURGE, s->ev[1] = -1, s->ev[2] = s->rate_idx, s->ev[3] = 0;
      break;
    }
    s->pcard[a] = (uint8_t)r;
    s->ev[0] = FV_EV_DRAW, s->ev[1] = (int16_t)a, s->ev[2] = (int16_t)r, s->ev[3] = 0;
    next_draw(s);
    break;
  }
  case FV_M_FLIP: {
    int col = fv_colour(a), before = s->cubes[a][col];
    s->icard[a] = 0;
    if (s->phase == FV_PH_SURGE) {
      add_cubes(s, a, col, 3);
      s->ev[0] = FV_EV_SURGE, s->ev[1] = (int16_t)a, s->ev[2] = s->rate_idx, s->ev[3] = (int16_t)(s->cubes[a][col] - before);
      if (s->result != FV_PLAYING) break;
      /* the discards, shuffled, go back on top as a layer of their own */
      s->itop++;
      for (int c = 0; c < FV_N; c++)
        if (s->icard[c] == 0) s->icard[c] = (uint8_t)s->itop;
      inf_norm(s);
      next_draw(s);
      break;
    }
    bool quiet = s->cured[col] == 2 || fv_protected(s, a, col);
    add_cubes(s, a, col, 1);
    s->ev[0] = quiet ? FV_EV_QUIET : FV_EV_INFECT, s->ev[1] = (int16_t)a, s->ev[2] = (int16_t)col, s->ev[3] = (int16_t)(s->cubes[a][col] - before);
    if (s->result != FV_PLAYING) break;
    inf_norm(s);
    if (--s->infects <= 0) end_turn(s);
    break;
  }
  default: CW_FATAL("feverline: unknown move kind %d", kind);
  }
}

int fv_turn(const void *state, void *user) {
  (void)user;
  const fv_state *s = state;
  if (s->phase == FV_PH_ACT) return s->cur;
  if (s->phase == FV_PH_DISCARD) return s->disc_role;
  return BK_CHANCE;
}
static bool fv_terminal_turn(const void *state, void *user) {
  (void)user;
  const fv_state *s = state;
  return s->result != FV_PLAYING || s->phase == FV_PH_DRAW || s->phase == FV_PH_SURGE || s->phase == FV_PH_INFECT;
}
static int fv_turn_team(const void *state, void *user) {
  int t = fv_turn(state, user);
  return t == BK_CHANCE ? BK_CHANCE : 0;
}
bool fv_terminal(const void *state, void *user) {
  (void)user;
  return ((const fv_state *)state)->result != FV_PLAYING;
}

int fv_outcomes(const void *state, bk_move *out, uint32_t *weight, int cap, void *user) {
  (void)user;
  const fv_state *s = state;
  int n = 0;
  if (s->phase == FV_PH_DRAW) {
    int top = deck_top_pile(s);
    for (int c = 0; c < FV_NCARDS && n < cap; c++)
      if (s->pcard[c] == top) out[n] = mv(FV_M_DRAW, c, 0), weight[n++] = 1;
  } else if (s->phase == FV_PH_SURGE) {
    int lo = inf_bottom(s);
    for (int c = 0; c < FV_N && n < cap; c++)
      if (s->icard[c] == lo) out[n] = mv(FV_M_FLIP, c, 0), weight[n++] = 1;
  } else if (s->phase == FV_PH_INFECT) {
    for (int c = 0; c < FV_N && n < cap; c++)
      if (s->icard[c] == s->itop) out[n] = mv(FV_M_FLIP, c, 0), weight[n++] = 1;
  }
  return n;
}

void fv_chance(void *state, cw_rng *rng, void *user) {
  bk_move o[FV_NCARDS];
  uint32_t w[FV_NCARDS];
  int n = fv_outcomes(state, o, w, FV_NCARDS, user);
  if (n <= 0) CW_FATAL("feverline: a chance step with nothing to draw (phase %d)", ((fv_state *)state)->phase);
  fv_apply(state, o[cw_rng_int(rng, 0, n - 1)], user);
}

/* ── the team's value ───────────────────────────────────────────────── */

/* what the coming spreads look like, worked out once per evaluate */
typedef struct deck_view { int top, next, disc, rate_now, rate; float surge_p[3]; } deck_view;
static deck_view deck_view_of(const fv_state *s) {
  deck_view d = { 0 };
  for (int c = 0; c < FV_N; c++)
    d.top += s->icard[c] == s->itop, d.next += s->itop > 1 && s->icard[c] == s->itop - 1, d.disc += s->icard[c] == 0;
  d.rate = fv_rate(s);
  d.rate_now = s->phase == FV_PH_INFECT ? s->infects : d.rate;
  /* the chance of a surge in the coming draws (the top pile holds one surge until it is drawn) */
  int pile = 255, n = 0, surge = 0;
  for (int c = 0; c < FV_NCARDS; c++)
    if (s->pcard[c] >= FV_PC_DECK && s->pcard[c] < FV_PC_DISCARD && s->pcard[c] < pile) pile = s->pcard[c];
  for (int c = 0; c < FV_NCARDS; c++)
    if (s->pcard[c] == pile) n++, surge += fv_is_surge(c);
  for (int sp = 1; sp <= 2; sp++) {
    int draws = (s->phase == FV_PH_ACT ? 2 : s->phase == FV_PH_DRAW ? s->draws : 0) + 2 * (sp - 1);
    d.surge_p[sp] = n > 0 && surge ? fminf(1.0f, (float)draws / (float)n) : 0.05f * (float)sp;
  }
  return d;
}
/* the chance a city's fever card turns up within the next `spreads` spreads (this turn's and the ones after) */
static float flip_chance(const fv_state *s, const deck_view *d, int city, int spreads) {
  int k = d->rate_now + (spreads - 1) * d->rate;
  if (s->icard[city] == 0) /* in the discards: a surge lays them back on top */
    return 0.01f + d->surge_p[spreads] * fminf(1.0f, (float)(k > 2 ? k : 2) / (float)(d->disc + 1));
  if (s->icard[city] == s->itop) return d->top <= k ? 1.0f : (float)k / (float)d->top;
  if (s->itop > 1 && s->icard[city] == s->itop - 1 && d->top < k && d->next > 0) return fminf(1.0f, (float)(k - d->top) / (float)d->next);
  return 0.04f * (float)spreads;
}

float fv_value(const fv_state *s) {
  int cures = 0;
  for (int c = 0; c < FV_NCOL; c++) cures += s->cured[c] != 0;
  if (s->result == FV_WON) return 0.95f + 0.05f * (1.0f - (float)s->turns / 80.0f);
  if (s->result == FV_LOST) return 0.01f * (float)cures;
  float v = 14.0f * (float)cures;
  for (int c = 0; c < FV_NCOL; c++) v += s->cured[c] == 2 ? 0.8f : 0;
  /* road distance from each agent to the nearest clinic */
  int to_clinic[FV_MAX_R];
  for (int r = 0; r < s->roles; r++) {
    to_clinic[r] = 9;
    for (int x = 0; x < FV_N; x++)
      if (s->clinic[x]) {
        int d = s->pcard[x] == r ? 1 : fv_dist(s->loc[r], x); /* a clinic city's card flies there */
        to_clinic[r] = d < to_clinic[r] ? d : to_clinic[r];
      }
    if (s->pcard[s->loc[r]] == r && to_clinic[r] > 1) to_clinic[r] = 1; /* a charter goes anywhere */
  }
  /* road distance from each city to the nearest clinic: travel can hop clinic to clinic */
  int to_clinic_city[FV_NCITY];
  for (int x = 0; x < FV_N; x++) {
    to_clinic_city[x] = 9;
    for (int y = 0; y < FV_N; y++)
      if (s->clinic[y]) { int d = fv_dist(x, y); to_clinic_city[x] = d < to_clinic_city[x] ? d : to_clinic_city[x]; }
  }
#define TRAVEL(a, b) (fv_dist(a, b) < to_clinic_city[a] + 1 + to_clinic_city[b] ? fv_dist(a, b) : to_clinic_city[a] + 1 + to_clinic_city[b])
  /* cure plans: for each fever, the cheapest collector, counting the actions to gather the cards from teammates
   * (both walk to the card's city, or meet anywhere with the Courier), to reach a clinic, and to cure; a card the
   * team does not hold yet costs a stretch of draws */
  for (int c = 0; c < FV_NCOL; c++) {
    if (s->cured[c]) continue;
    /* a card nobody holds yet: the draws until one turns up, as actions */
    int in_deck = 0;
    for (int x = 0; x < FV_N; x++) in_deck += fv_colour(x) == c && s->pcard[x] >= FV_PC_DECK && s->pcard[x] < FV_PC_DISCARD;
    float draw_cost = fminf(30.0f, (float)s->roles * (float)fv_deck_left(s) / ((float)in_deck + 0.5f));
    int slack = fever_slack(s, c);
    if (slack < 0) v -= 40.0f; /* this fever can no longer be cured */
    else v -= 3.0f / (1.0f + (float)slack);
    float best = 99, ready = -99;
    for (int r = 0; r < s->roles; r++) {
      int need = fv_cure_need(s, r), have = fv_hand_colour(s, r, c);
      float cost = (float)to_clinic[r] + 1.0f;
      /* a hand that holds a cure: get it to a clinic, now (a strong pull, a step at a time) */
      if (have >= need) ready = fmaxf(ready, 9.0f - 1.3f * (float)(to_clinic[r] > 6 ? 6 : to_clinic[r]));
      if (have < need) {
        float tc[FV_NCITY];
        int nt = 0;
        for (int a = 0; a < s->roles; a++) {
          if (a == r) continue;
          bool courier = s->role[a] == FV_COURIER || s->role[r] == FV_COURIER;
          for (int x = 0; x < FV_N; x++) {
            if (s->pcard[x] != a || fv_colour(x) != c) continue;
            float t = courier ? 0.5f * (float)TRAVEL(s->loc[a], s->loc[r]) + 1.0f
                              : 0.5f * (float)(TRAVEL(s->loc[a], x) + TRAVEL(s->loc[r], x)) + 1.0f;
            tc[nt++] = t;
          }
        }
        for (int i = 1; i < nt; i++) /* cheapest first */
          for (int j = i; j > 0 && tc[j] < tc[j - 1]; j--) { float t = tc[j]; tc[j] = tc[j - 1], tc[j - 1] = t; }
        int miss = need - have;
        for (int i = 0; i < miss; i++) cost += i < nt ? tc[i] : draw_cost;
      }
      best = cost < best ? cost : best;
    }
    v += ready > -99 ? ready : 7.0f - 0.55f * fminf(best, 12.0f) - 0.25f * fmaxf(0.0f, fminf(best, 40.0f) - 12.0f);
  }
#undef TRAVEL
  /* outbreaks so far, and the ones the coming spreads may bring */
  v -= 2.5f * (float)s->outbreaks + 0.25f * (float)(s->outbreaks * s->outbreaks);
  float danger = 1.0f + 0.2f * (float)s->outbreaks;
  deck_view dv = deck_view_of(s);
  int cubes = 0;
  float risk[FV_NCITY];
  for (int city = 0; city < FV_N; city++) {
    risk[city] = 0;
    float p = -1;
    for (int c = 0; c < FV_NCOL; c++) {
      int k = s->cubes[city][c];
      if (!k || s->cured[c] == 2) continue;
      cubes += k;
      if (fv_protected(s, city, c)) continue;
      if (fv_colour(city) != c) { v -= k == 3 ? 0.6f : 0.05f * (float)k; continue; }
      if (p < 0) p = flip_chance(s, &dv, city, 2);
      float now = flip_chance(s, &dv, city, 1);
      if (k == 3) v -= 2.0f * (0.5f + 2.0f * p + 2.0f * now) * danger, risk[city] = 0.35f + 0.6f * p;
      else if (k == 2) v -= 2.0f * (0.15f + 0.8f * p), risk[city] = 0.08f + 0.3f * p;
      else v -= 0.2f * p;
    }
  }
  v -= 0.06f * (float)cubes;
  for (int c = 0; c < FV_NCOL; c++)
    if (s->cured[c] != 2 && s->supply[c] < 9) v -= 0.8f * (float)(9 - s->supply[c]);
  /* clinics spread over the regions */
  int regions = 0;
  for (int g = 0; g < FV_NCOL; g++) {
    bool any = false;
    for (int c = 0; c < FV_N && !any; c++) any = s->clinic[c] && fv_colour(c) == g;
    regions += any;
  }
  v += 1.0f * (float)regions + 0.2f * (float)(s->clinics > 5 ? 5 : s->clinics);
  /* the risky cities: how far the nearest agent is (the Medic counts nearer) */
  for (int city = 0; city < FV_N; city++) {
    if (risk[city] <= 0) continue;
    float d1 = 9, d2 = 9; /* the nearest agent counts, the next one a little: two cities pull two agents */
    for (int r = 0; r < s->roles; r++) {
      float dd = fminf((float)fv_dist(s->loc[r], city) * (s->role[r] == FV_MEDIC ? 0.7f : 1.0f), 5.0f);
      if (dd < d1) d2 = d1, d1 = dd;
      else if (dd < d2) d2 = dd;
    }
    v -= risk[city] * (d1 + 0.3f * (s->roles > 1 ? d2 : 0));
  }
  /* hands that will overflow with the next draws lose cards */
  for (int r = 0; r < s->roles; r++) {
    int over = fv_hand_count(s, r) + 2 - FV_HAND_MAX;
    if (over > 0) v -= 0.5f * (float)over;
  }
  /* time: the deck against the fevers still to cure */
  int left = fv_deck_left(s), uncured = FV_NCOL - cures;
  if (uncured > 0 && left < uncured * 5) v -= 0.3f * (float)(uncured * 5 - left);
  float x = 0.5f + v * 0.01f; /* linear: a step of the plan stays a step of the value */
  return x < 0.03f ? 0.03f : x > 0.93f ? 0.93f : x;
}

void fv_evaluate(const void *state, float score[BK_MAX_SEATS], void *user) {
  (void)user;
  const fv_state *s = state;
  float v;
  if (s->result == FV_WON) v = BK_WIN - (float)s->moves_played;
  else if (s->result == FV_LOST) v = BK_LOSS + (float)s->moves_played;
  else v = fv_value(s) * 1000.0f;
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = v;
}
static void fv_evaluate_team(const void *state, float score[BK_MAX_SEATS], void *user) {
  (void)user;
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  score[0] = fv_value(state);
}

/* ── the playout policy: a careful agent ────────────────────────────── */

static int nearest(const fv_state *s, int from, int want) { /* want: 0 a clinic, k a city at k+ cubes */
  int best = -1, bd = 99;
  for (int c = 0; c < FV_N; c++) {
    bool ok;
    if (want == 0) ok = s->clinic[c];
    else {
      int most = 0;
      for (int k = 0; k < FV_NCOL; k++)
        if (s->cured[k] != 2) most = s->cubes[c][k] > most ? s->cubes[c][k] : most;
      ok = most >= want;
    }
    if (!ok) continue;
    int d = fv_dist(from, c);
    if (d < bd) bd = d, best = c;
  }
  return best;
}
static bk_move step_toward(const fv_state *s, const bk_move *moves, int n, int goal) {
  int at = s->loc[s->cur], here = fv_dist(at, goal), best = -1, bd = here;
  for (int i = 0; i < n; i++) {
    int k = bk_move_field(moves[i], 0);
    if (k != FV_M_DRIVE && k != FV_M_SHUTTLE) continue;
    int d = fv_dist(bk_move_field(moves[i], 1), goal);
    if (d < bd) bd = d, best = i;
  }
  return best >= 0 ? moves[best] : BK_MOVE_NONE;
}

static bk_move fv_playout(const void *state, const bk_move *moves, int n, cw_rng *rng, void *user) {
  (void)user;
  const fv_state *s = state;
  if (n == 1) return moves[0];
  if (s->phase == FV_PH_DISCARD) { /* drop a card of the fever the hand holds least of (a cured one first) */
    int r = s->disc_role, best = 0, bs = 99;
    for (int i = 0; i < n; i++) {
      int c = bk_move_field(moves[i], 1), col = fv_colour(c);
      int sc = s->cured[col] ? -1 : fv_hand_colour(s, r, col);
      if (sc < bs) bs = sc, best = i;
    }
    return moves[best];
  }
  int r = s->cur, at = s->loc[r], need = fv_cure_need(s, r);
  for (int i = 0; i < n; i++)
    if (bk_move_field(moves[i], 0) == FV_M_CURE) return moves[i];
  /* treat what is piled up here */
  int tbest = -1, tcount = 0;
  for (int i = 0; i < n; i++)
    if (bk_move_field(moves[i], 0) == FV_M_TREAT) {
      int c = bk_move_field(moves[i], 1);
      if (s->cubes[at][c] > tcount) tcount = s->cubes[at][c], tbest = i;
    }
  if (tbest >= 0 && (tcount >= 2 || cw_rng_int(rng, 0, 2) == 0)) return moves[tbest];
  /* share a card that brings someone to a cure */
  for (int i = 0; i < n; i++) {
    int k = bk_move_field(moves[i], 0);
    if (k != FV_M_GIVE && k != FV_M_TAKE) continue;
    int card = bk_move_field(moves[i], 1), o = bk_move_field(moves[i], 2), to = k == FV_M_GIVE ? o : r, from = k == FV_M_GIVE ? r : o;
    int col = fv_colour(card);
    if (s->cured[col]) continue;
    if (fv_hand_colour(s, to, col) >= fv_hand_colour(s, from, col)) return moves[i];
  }
  /* holding a cure: head for a clinic */
  bool ready = false;
  for (int c = 0; c < FV_NCOL; c++) ready |= !s->cured[c] && fv_hand_colour(s, r, c) >= need;
  if (ready) {
    int g = nearest(s, at, 0);
    if (g >= 0) {
      bk_move m = step_toward(s, moves, n, g);
      if (m != BK_MOVE_NONE) return m;
    }
  }
  /* build a clinic far from the others sometimes */
  for (int i = 0; i < n; i++)
    if (bk_move_field(moves[i], 0) == FV_M_BUILD) {
      int g = nearest(s, at, 0);
      if ((g < 0 || fv_dist(at, g) >= 3) && cw_rng_int(rng, 0, 1) == 0) return moves[i];
    }
  /* go where it is worst */
  int g3 = nearest(s, at, 3);
  if (g3 < 0 || fv_dist(at, g3) > 4) g3 = nearest(s, at, 2);
  if (g3 >= 0 && cw_rng_int(rng, 0, 3) != 0) {
    bk_move m = step_toward(s, moves, n, g3);
    if (m != BK_MOVE_NONE) return m;
  }
  if (tbest >= 0) return moves[tbest];
  /* else a random move that is not a rest */
  for (int tries = 0; tries < 8; tries++) {
    bk_move m = moves[cw_rng_int(rng, 0, n - 1)];
    if (bk_move_field(m, 0) != FV_M_REST) return m;
  }
  return moves[cw_rng_int(rng, 0, n - 1)];
}

/* ── labels ─────────────────────────────────────────────────────────── */

void fv_label(const void *state, bk_move m, char *buf, size_t cap, void *user) {
  (void)user;
  const fv_state *s = state;
  int kind = bk_move_field(m, 0), a = bk_move_field(m, 1), b = bk_move_field(m, 2);
  static const char *const COL[FV_NCOL] = { "Ember", "Tide", "Moss", "Dusk" };
  switch (kind) {
  case FV_M_DRIVE: snprintf(buf, cap, "Drive to %s", FV_CITIES[a].name); break;
  case FV_M_FLY: snprintf(buf, cap, "Fly to %s (spend its card)", FV_CITIES[a].name); break;
  case FV_M_CHARTER: snprintf(buf, cap, "Charter to %s (spend the %s card)", FV_CITIES[a].name, FV_CITIES[s->loc[s->cur]].name); break;
  case FV_M_SHUTTLE: snprintf(buf, cap, "Shuttle to the clinic in %s", FV_CITIES[a].name); break;
  case FV_M_BUILD: snprintf(buf, cap, "Build a clinic in %s", FV_CITIES[s->loc[s->cur]].name); break;
  case FV_M_TREAT: snprintf(buf, cap, "Treat %s fever", COL[a]); break;
  case FV_M_GIVE: snprintf(buf, cap, "Give the %s card to the %s", FV_CITIES[a].name, fv_role_name(s->role[b])); break;
  case FV_M_TAKE: snprintf(buf, cap, "Take the %s card from the %s", FV_CITIES[a].name, fv_role_name(s->role[b])); break;
  case FV_M_CURE: snprintf(buf, cap, "Cure %s fever", COL[a]); break;
  case FV_M_REST: snprintf(buf, cap, "Rest (end actions)"); break;
  case FV_M_DISCARD: snprintf(buf, cap, "Discard %s", FV_CITIES[a].name); break;
  case FV_M_DRAW: snprintf(buf, cap, fv_is_surge(a) ? "Draw a surge" : fv_is_calm(a) ? "Draw a calm card" : "Draw %s", a < FV_NCITY ? FV_CITIES[a].name : ""); break;
  case FV_M_FLIP: snprintf(buf, cap, "Fever in %s", FV_CITIES[a].name); break;
  default: snprintf(buf, cap, "?");
  }
}

/* ── games ──────────────────────────────────────────────────────────── */

static uint64_t fv_hash_cb(const void *state, void *user) {
  (void)user;
  return fv_hash(state);
}

void fv_game(bk_game *g, int roles) {
  dist_init();
  memset(g, 0, sizeof *g);
  g->players = roles;
  g->state_size = sizeof(fv_state);
  g->moves = fv_moves;
  g->apply = fv_apply;
  g->turn = fv_turn;
  g->terminal = fv_terminal;
  g->evaluate = fv_evaluate;
  g->hash = fv_hash_cb;
  g->outcomes = fv_outcomes;
  g->chance = fv_chance;
}

void fv_search_game(bk_game *g) {
  fv_game(g, 1);
  g->players = 1;
  g->moves = fv_moves_sensible;
  g->turn = fv_turn_team;
  g->terminal = fv_terminal_turn; /* the plan covers this agent's actions (or a discard); the draws and the spread
                                   * that follow are scored as risks in evaluate */
  g->evaluate = fv_evaluate_team;
  g->playout = fv_playout;
}

void fv_cpu_options(int level, uint64_t seed, cw_search_options *o) {
  cw_search_level_options(level, CW_SEARCH_MCTS, o);
  o->kind = CW_SEARCH_MCTS;
  o->seed = seed;
  o->mistake = level <= 0 ? 0.2f : 0; /* Easy now and then takes a lesser plan */
  o->mistake_spread = 3;
  o->uct_c = 0.03f;
  o->playout_depth = 8;
  o->max_nodes = level <= 0 ? 6000 : level == 1 ? 20000 : 60000;
  o->tree_nodes = 16384;
}
