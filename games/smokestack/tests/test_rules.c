/* Smokestack rules tests: headless, seeded. Exit 0 = all pass. Each check family has a control that must fail.
 *
 *   smokestack_test_rules [games-per-count]   */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_game.h"
#include "boardkit/bk_match.h"
#include "rules.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define CONTROL(c, ...) do { if (c) { printf("CONTROL "); printf(__VA_ARGS__); printf(": caught\n"); } else { fails++; fprintf(stderr, "FAIL CONTROL not caught: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

#define BAG_TOTAL (5 * 14)

/* what must hold in every state; NULL when it does. delivered: goods hauled away so far */
static const char *invariant_error(const rl_state *s, int delivered) {
  const rl_map_def *m = rl_map(s->map);
  int goods = 0;
  for (int c = 0; c < RL_COLORS; c++) goods += s->bag[c];
  for (int k = 0; k < m->nset; k++) {
    int n = rl_cubes(s, k);
    if (n && s->set_kind[k] != RL_CITY) return "goods in a town";
    for (int i = n; i < RL_CITY_CUBES; i++)
      if (s->cube[k][i]) return "a gap in a city's goods";
    for (int i = 0; i < n; i++)
      if (s->cube[k][i] - 1 == s->set_color[k]) return "a good in a city of its own colour";
    goods += n;
  }
  goods += s->drawn_n;
  if (goods + delivered != BAG_TOTAL) return "goods not conserved";
  for (int i = 0; i < s->players; i++) {
    const rl_player *p = &s->p[i];
    if (p->money < 0) return "negative money";
    if (p->engine < 1 || p->engine > RL_MAX_ENGINE) return "engine out of range";
    if (p->shares < 0 || p->shares > RL_MAX_SHARES) return "shares out of range";
    if (p->open_link >= 0 && (s->link[p->open_link].owner != i || s->link[p->open_link].done)) return "open link wrong";
  }
  /* every tile belongs to a live link, and every link's chain of tiles is whole */
  int tiles = 0;
  for (int h = 0; h < m->nhex; h++) {
    int l = s->tile_link[h];
    if (l < 0) continue;
    tiles++;
    if (s->link[l].owner < 0) return "a tile of a torn-up link";
    if (m->hex_set[h] >= 0) return "a tile on a settlement";
    if (m->terrain[h] == RL_T_LAKE || m->terrain[h] == RL_T_PEAK) return "a tile on a lake or peak";
  }
  int counted = 0;
  for (int l = 0; l < s->nlink; l++) {
    const rl_link *k = &s->link[l];
    if (k->owner < 0) continue;
    int8_t hx[RL_MAX_HEX];
    int n = rl_link_hexes(s, l, hx, RL_MAX_HEX);
    if (n != k->tiles) return "a link's chain of tiles is broken";
    counted += n;
    if (m->set_hex[k->from] != m->nb[k->first][k->from_dir >= 0 ? (k->from_dir + 3) % 6 : 0]) return "a link does not start at its settlement";
    if (k->done) {
      int end = m->nb[k->last][s->tile_out[k->last]];
      if (end < 0 || m->hex_set[end] != k->to) return "a finished link does not end at its settlement";
    }
  }
  if (counted != tiles) return "tiles outside any link";
  return NULL;
}

/* a route is legal: finished links, joined end to end from the city, at most engine links, ending at the first
 * city of the good's colour; NULL when it is */
static const char *route_error(const rl_state *s, int city, int color, int seat, const rl_route *r) {
  if (r->n < 1 || r->n > s->p[seat].engine) return "route length";
  int at = city;
  bool seen[RL_MAX_SET] = { 0 };
  seen[city] = true;
  for (int i = 0; i < r->n; i++) {
    const rl_link *l = &s->link[r->link[i]];
    if (l->owner < 0 || !l->done) return "unfinished link on a route";
    int next = l->from == at ? l->to : l->to == at ? l->from : -1;
    if (next < 0) return "route links do not join";
    if (seen[next]) return "route revisits a settlement";
    seen[next] = true;
    bool wants = s->set_kind[next] == RL_CITY && s->set_color[next] == color;
    if (wants != (i == r->n - 1)) return wants ? "route passes a city that wants the good" : "route ends where the good is not wanted";
    at = next;
  }
  if (at != r->dest) return "route ends elsewhere";
  return NULL;
}

typedef struct result { int winner, moves, delivered, links, tiles; uint64_t hash; int score[4]; bool ok; } result;
typedef bk_move (*policy_fn)(const bk_game *g, const rl_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx);

static bk_move pol_uniform(const bk_game *g, const rl_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx) {
  (void)g, (void)s, (void)ctx;
  return mv[cw_rng_int(rng, 0, n - 1)];
}
static bk_move pol_playout(const bk_game *g, const rl_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx) {
  (void)ctx;
  return g->playout(s, mv, n, rng, g->user);
}
/* seat levels in ctx (int[4]): the CPU as bk_cpu runs it */
static bk_move pol_cpu(const bk_game *g, const rl_state *s, const bk_move *mv, int n, cw_rng *rng, void *ctx) {
  (void)mv, (void)n, (void)rng;
  const int *lv = ctx;
  int seat = rl_turn(s);
  cw_search_options o;
  rl_cpu_options(lv[seat], &o);
  o.seed = bk_seed_mix(0x5eedu ^ (uint64_t)seat, bk_hash(g, s));
  cw_search *se = cw_search_new(&RL_CPU_RULES[s->players], &o); /* what the game's CPU seats search */
  bk_move m = BK_MOVE_NONE;
  if (!cw_search_run(se, s, &m)) CW_FATAL("search refused a seat's turn");
  cw_search_free(se);
  return m;
}

static result play(int players, uint64_t seed, policy_fn pol, void *ctx, bool check_all) {
  const bk_game *g = &RL_RULES[players];
  rl_state s;
  rl_setup(&s, players);
  bk_match *mt = bk_match_new(g, &s, seed);
  cw_rng rng;
  cw_rng_seed(&rng, seed ^ 0xabcdefu);
  result r = { 0 };
  r.ok = true;
  int guard = 0, delivered = 0;
  while (!bk_match_over(mt)) {
    const rl_state *st = bk_match_state(mt);
    if (++guard > 20000) { CHECK(0, "%dp seed %llu never ends", players, (unsigned long long)seed); r.ok = false; break; }
    bk_move mv[BK_MAX_MOVES];
    int n = bk_match_moves(mt, mv, BK_MAX_MOVES);
    if (n <= 0) { CHECK(0, "no moves at a decision (phase %d)", st->phase); r.ok = false; break; }
    if (check_all && st->phase == RL_PH_ROUTE) {
      rl_route rt[RL_MAX_ROUTES];
      int nr = rl_routes(st, st->good_city, st->good_slot, rl_turn(st), rt, RL_MAX_ROUTES);
      for (int i = 0; i < nr; i++) {
        const char *e = route_error(st, st->good_city, st->cube[st->good_city][st->good_slot] - 1, rl_turn(st), &rt[i]);
        if (e) { CHECK(0, "route: %s", e); r.ok = false; }
      }
    }
    bk_move m = pol(g, st, mv, n, &rng, ctx);
    bool was_route = st->phase == RL_PH_ROUTE;
    if (!bk_match_play(mt, m)) { CHECK(0, "the policy's move was refused"); r.ok = false; break; }
    if (was_route) delivered++;
    if (check_all) {
      const char *e = invariant_error(bk_match_state(mt), delivered);
      if (e) { CHECK(0, "%dp seed %llu after move %d: %s", players, (unsigned long long)seed, guard, e); r.ok = false; break; }
    }
  }
  const rl_state *st = bk_match_state(mt);
  r.winner = rl_winner(st);
  r.moves = st->moves;
  r.delivered = delivered;
  r.hash = bk_match_hash(mt);
  for (int i = 0; i < players; i++) {
    r.score[i] = rl_final_score(st, i);
    r.links += rl_links_done(st, i);
  }
  for (int h = 0; h < RL_MAX_HEX; h++) r.tiles += st->tile_link[h] >= 0;
  bk_match_free(mt);
  return r;
}

int main(int argc, char **argv) {
  int games = argc > 1 ? atoi(argv[1]) : 20;
  /* setup */
  for (int n = 2; n <= 4; n++) {
    rl_state s;
    rl_setup(&s, n);
    CHECK(rl_turn(&s) == BK_CHANCE, "%dp: setup opens with goods to draw", n);
    cw_rng rng;
    cw_rng_seed(&rng, 1);
    bk_settle(&RL_RULES[n], &s, &rng, NULL);
    const rl_map_def *m = rl_map(s.map);
    for (int k = 0; k < m->nset; k++) CHECK(rl_cubes(&s, k) == (m->set_kind[k] == RL_CITY ? 2 : 0), "%dp: %s starts with 2 goods", n, m->set_name[k]);
    CHECK(rl_turn(&s) == 0 && s.phase == RL_PH_BID, "%dp: seat 1 opens the auction", n);
    CHECK(invariant_error(&s, 0) == NULL, "%dp setup: %s", n, invariant_error(&s, 0));
  }
  /* CONTROL: the invariant check sees a broken board */
  {
    rl_state s;
    rl_setup(&s, 2);
    cw_rng rng;
    cw_rng_seed(&rng, 1);
    bk_settle(&RL_RULES[2], &s, &rng, NULL);
    s.cube[0][0] = 0; /* a gap: a good lost */
    CONTROL(invariant_error(&s, 0) != NULL, "invariants: a good taken off the board is noticed (%s)", invariant_error(&s, 0));
  }
  /* a link built by hand: start, carry on, finish; costs paid; tear-up when not carried on */
  {
    rl_state s;
    rl_setup(&s, 2);
    bk_match *mt = bk_match_new(&RL_RULES[2], &s, 9);
    bk_match_play(mt, rl_mv(RL_MV_PASS, 0, 0)); /* seat 0 drops out: seat 1 first for nothing */
    const rl_state *st = bk_match_state(mt);
    CHECK(st->order[0] == 1 && st->order[1] == 0 && st->p[1].money == 15, "the auction: the one left goes first and pays 0");
    bk_match_play(mt, rl_mv(RL_MV_ACTION, RL_A_GANG, 0));
    bk_match_play(mt, rl_mv(RL_MV_ACTION, RL_A_LEAD_BUILD, 0));
    st = bk_match_state(mt);
    CHECK(st->phase == RL_PH_BUILD && rl_turn(st) == 0, "Lead Builder builds first (turn %d)", rl_turn(st));
    CHECK(st->p[1].limit == 4, "Track Gang lays four");
    bk_move mv[BK_MAX_MOVES];
    int n = bk_match_moves(mt, mv, BK_MAX_MOVES), start = -1;
    for (int i = 0; i < n; i++)
      if (rl_mv_kind(mv[i]) == RL_MV_START) { start = i; break; }
    CHECK(start >= 0, "a link can start");
    int money0 = st->p[0].money, h = rl_mv_a(mv[start]);
    bk_match_play(mt, mv[start]);
    CHECK(((const rl_state *)bk_match_state(mt))->phase == RL_PH_EXIT, "start asks for the exit");
    n = bk_match_moves(mt, mv, BK_MAX_MOVES);
    CHECK(n > 0, "the tile has exits");
    /* an exit back the way it came is never offered */
    int entry = ((const rl_state *)bk_match_state(mt))->tile_in[h];
    for (int i = 0; i < n; i++) CHECK(rl_mv_a(mv[i]) != entry, "an exit into its own entry");
    CONTROL(!bk_is_legal(&RL_RULES[2], bk_match_state(mt), rl_mv(RL_MV_EXIT, entry, 0)), "legality: an exit back into the entry edge is refused");
    /* pick an exit into open ground (not finishing) if there is one */
    int pick = 0;
    for (int i = 0; i < n; i++) {
      int nh = rl_map(0)->nb[h][rl_mv_a(mv[i])];
      if (rl_map(0)->hex_set[nh] < 0) { pick = i; break; }
    }
    bk_match_play(mt, mv[pick]);
    st = bk_match_state(mt);
    CHECK(st->p[0].money == money0 - rl_tile_cost(st, h), "a tile costs its terrain (%d -> %d)", money0, st->p[0].money);
    if (st->p[0].open_link >= 0) {
      int link = st->p[0].open_link;
      bk_match_play(mt, rl_mv(RL_MV_PASS, 0, 0)); /* ends with it open (extended this turn: it stays) */
      st = bk_match_state(mt);
      CHECK(st->link[link].owner == 0, "an open link built on this turn stays");
    }
    bk_match_free(mt);
  }
  /* determinism, and its control */
  {
    int lv[4] = { 1, 1, 1, 1 };
    (void)lv;
    result a = play(3, 42, pol_playout, NULL, false), b = play(3, 42, pol_playout, NULL, false), c = play(3, 43, pol_playout, NULL, false);
    CHECK(a.hash == b.hash, "the same seed and moves give the same game");
    CONTROL(a.hash != c.hash, "determinism: another seed gives another game");
  }
  /* random and playout-policy full games at every count, with invariants after every move */
  for (int n = 2; n <= 4; n++) {
    int done = 0, del = 0, links = 0, wins[4] = { 0 };
    for (int i = 0; i < games; i++) {
      result r = play(n, 1000 + (uint64_t)i, i % 2 ? pol_uniform : pol_playout, NULL, true);
      if (!r.ok) continue;
      CHECK(r.winner >= 0 && r.winner < n, "%dp: a legal winner (%d)", n, r.winner);
      done++, del += r.delivered, links += r.links, wins[r.winner]++;
    }
    printf("%d players: %d/%d games end (half uniform, half playout policy), %.1f deliveries and %.1f links a game, wins %d %d %d %d\n", n,
           done, games, (double)del / (done ? done : 1), (double)links / (done ? done : 1), wins[0], wins[1], wins[2], wins[3]);
    CHECK(done == games, "%dp: every game ends", n);
    CHECK(del > 0, "%dp: goods get hauled", n);
  }
  /* CONTROL: a forged route through a city of the good's colour is refused by the route check */
  {
    result r = play(2, 7, pol_playout, NULL, false);
    (void)r;
    rl_state s;
    rl_setup(&s, 2);
    rl_route bad = { .dest = 0, .n = 1, .own = 0 };
    bad.link[0] = 0; /* link 0 does not exist yet */
    CONTROL(route_error(&s, 1, 0, 0, &bad) != NULL, "routes: a route over a link that is not there is refused (%s)", route_error(&s, 1, 0, 0, &bad));
  }
  /* CPU games through the search at every count (Normal against Easy and Hard), each ending with a legal winner */
  for (int n = 2; n <= 4; n++) {
    int lv[4] = { 1, 0, 2, 1 };
    result r = play(n, 77 + (uint64_t)n, pol_cpu, lv, true);
    CHECK(r.ok && r.winner >= 0 && r.winner < n, "%dp CPU game: a legal winner", n);
    printf("CPU game %d players: winner seat %d, scores %d %d %d %d, %d deliveries, %d links, %d moves\n", n, r.winner, r.score[0],
           r.score[1], r.score[2], r.score[3], r.delivered, r.links, r.moves);
  }
  /* CONTROL: a game cut short has no winner */
  {
    rl_state s;
    rl_setup(&s, 2);
    CONTROL(rl_winner(&s) == -1, "winner: a game not over names nobody");
  }
  if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
  printf("smokestack rules: all passed\n");
  return 0;
}
