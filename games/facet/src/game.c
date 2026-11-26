/* Facet: the match and the table's selection model (game.h). */
#include "game.h"

#include "cartwheel/debug.h"

#include <stdlib.h>
#include <string.h>

/* ── layout (world units; x right, z toward the viewer, the table top at y = 0) ─────────────────────── */

static const float ROW_Z[GM_TIERS] = {2.45f, 0.45f, -1.55f}; /* tier I nearest the players, III furthest */

t3_vec3 gm_bank_pos(int colour) { return (t3_vec3){-6.55f, 0.0f, -2.9f + 1.16f * (float)colour}; }
t3_vec3 gm_card_pos(int tier, int slot) {
  float x = slot < 0 ? -4.15f : -2.55f + 1.5f * (float)slot;
  return (t3_vec3){x, 0.0f, ROW_Z[tier]};
}
t3_vec3 gm_mat_pos(int players, int seat) {
  float span = players <= 2 ? 2.6f : players == 3 ? 2.35f : 2.05f;
  float z0 = -span * (float)(players - 1) * 0.5f;
  return (t3_vec3){5.85f, 0.0f, z0 + span * (float)seat};
}
/* a mat: five colour columns and the topaz on the left (each a stack of stones over its counts), the set-aside
 * cards fanned on the right, patrons won above them */
t3_vec3 gm_res_pos(int players, int seat, int r) {
  t3_vec3 m = gm_mat_pos(players, seat);
  return (t3_vec3){m.x + 1.5f + 0.3f * (float)r, 0.0f, m.z + 0.22f};
}
t3_vec3 gm_hand_pos(int players, int seat, int colour) {
  t3_vec3 m = gm_mat_pos(players, seat);
  return (t3_vec3){m.x - 2.1f + 0.6f * (float)colour, 0.0f, m.z - 0.45f};
}
t3_vec3 gm_patron_pos(const gm_state *st, int patron) {
  int slot = 0;
  for (int i = 0; i < patron; i++) slot += st->patron_open[i] != 0;
  int n = st->players + 1;
  return (t3_vec3){-0.3f + 1.62f * ((float)slot - (float)(n - 1) * 0.5f), 0.0f, -3.72f};
}

t3_vec3 gm_target_pos(const gm_game *g, int t) {
  const gm_state *st = gm_game_state(g);
  if (t < TG_CARD) return gm_bank_pos(t - TG_BANK);
  if (t < TG_DECK) return gm_card_pos((t - TG_CARD) / GM_SLOTS, (t - TG_CARD) % GM_SLOTS);
  if (t < TG_RES) return gm_card_pos(t - TG_DECK, -1);
  if (t < TG_HAND) return gm_res_pos(st->players, st->seat, t - TG_RES);
  if (t < TG_PATRON) return gm_hand_pos(st->players, st->seat, t - TG_HAND);
  if (t < TG_OK) return gm_patron_pos(st, t - TG_PATRON);
  if (t == TG_OK) return (t3_vec3){-1.05f, 0.0f, 4.05f};
  return (t3_vec3){1.35f, 0.0f, 4.05f};
}

/* ── rules helpers ──────────────────────────────────────────────────────────────────────────────── */

const gm_state *gm_game_state(const gm_game *g) { return bk_match_state(g->match); }
bool gm_game_over(const gm_game *g) { return bk_match_over(g->match); }
int gm_game_turn(const gm_game *g) { return bk_match_turn(g->match); }

static int legal(const gm_game *g, bk_move *out) { return bk_match_moves(g->match, out, BK_MAX_MOVES); }

/* the hand a take or set-aside leaves (fields 0..6), false when it is not possible from this state */
static bool final_hand(const gm_state *st, bk_move m, uint8_t hand[GM_NTOK]) {
  const gm_seat *p = &st->s[st->seat];
  memcpy(hand, p->tok, GM_NTOK);
  int k = bk_move_field(m, 0);
  if (k == GM_MV_TAKE3) {
    for (int i = 1; i <= 3; i++) {
      int c = bk_move_field(m, i);
      if (c != GM_NONE) hand[c]++;
    }
  } else if (k == GM_MV_TAKE2) hand[bk_move_field(m, 1)] += 2;
  else if (k == GM_MV_RESERVE || k == GM_MV_RESERVE_DECK) {
    if (st->bank[GM_WILD]) hand[GM_WILD]++;
  } else return false;
  for (int i = 4; i <= 6; i++) {
    int c = bk_move_field(m, i);
    if (c == GM_NONE) continue;
    if (c >= GM_NTOK || !hand[c]) return false;
    hand[c]--;
  }
  return true;
}

static int group_of(bk_move m) {
  int k = bk_move_field(m, 0);
  return k == GM_MV_TAKE2 ? GM_MV_TAKE3 : k;
}

bk_move gm_canonical(const gm_state *st, bk_move m) {
  int kind = bk_move_field(m, 0);
  if (kind == GM_MV_BUY || kind == GM_MV_BUY_RES || kind == GM_MV_PATRON || kind == GM_MV_PASS) m &= 0xffffffffull; /* no hand-backs */
  bk_move mv[BK_MAX_MOVES];
  int n = gm_moves(st, mv, BK_MAX_MOVES, NULL);
  for (int i = 0; i < n; i++)
    if (mv[i] == m) return m;
  uint8_t want[GM_NTOK], have[GM_NTOK];
  if (!final_hand(st, m, want)) return BK_MOVE_NONE;
  for (int i = 0; i < n; i++) {
    if (group_of(mv[i]) != group_of(m)) continue;
    if (group_of(m) != GM_MV_TAKE3 && (mv[i] & 0xffffffffull) != (m & 0xffffffffull)) continue; /* same card */
    if (final_hand(st, mv[i], have) && !memcmp(want, have, GM_NTOK)) return mv[i];
  }
  return BK_MOVE_NONE;
}

/* the colours a take move takes, as a sorted list */
static int take_colours(bk_move m, int out[3]) {
  int k = bk_move_field(m, 0), n = 0;
  if (k == GM_MV_TAKE2) {
    out[0] = out[1] = bk_move_field(m, 1);
    return 2;
  }
  if (k != GM_MV_TAKE3) return 0;
  for (int i = 1; i <= 3; i++)
    if (bk_move_field(m, i) != GM_NONE) out[n++] = bk_move_field(m, i);
  return n;
}

static void sort3(int *a, int n) {
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && a[j - 1] > a[j]; j--) { int t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
}

/* is `pick` (n stones) a part (or, with whole, all) of some legal take? */
static bool pick_fits(const gm_game *g, const int *pick, int n, bool whole) {
  bk_move mv[BK_MAX_MOVES];
  int nm = legal(g, mv);
  int want[3] = {0, 0, 0};
  memcpy(want, pick, sizeof(int) * (size_t)n);
  for (int i = 0; i < nm; i++) {
    int cs[3], k = take_colours(mv[i], cs);
    if (!k || k < n || (whole && k != n)) continue;
    /* sub-multiset */
    int used[3] = {0, 0, 0}, ok = 1;
    for (int a = 0; a < n && ok; a++) {
      int found = 0;
      for (int b = 0; b < k && !found; b++)
        if (!used[b] && cs[b] == want[a]) used[b] = found = 1;
      ok = found;
    }
    if (ok) return true;
  }
  return false;
}

static bool only_pass(const gm_game *g) {
  bk_move mv[BK_MAX_MOVES];
  int n = legal(g, mv);
  return n == 1 && bk_move_field(mv[0], 0) == GM_MV_PASS;
}

static bk_move take_move(const gm_ui *u) {
  int cs[3] = {u->pick[0], u->pick[1], u->pick[2]};
  if (u->npick == 2 && cs[0] == cs[1]) return bk_move_pack(GM_MV_TAKE2, cs[0], 0, 0, 0, 0, 0, 0);
  sort3(cs, u->npick);
  for (int i = u->npick; i < 3; i++) cs[i] = GM_NONE;
  return bk_move_pack(GM_MV_TAKE3, cs[0], cs[1], cs[2], 0, 0, 0, 0);
}

static bk_move with_rets(bk_move base, const int *ret, int n) {
  base &= 0xffffffffull;
  for (int i = 0; i < 3; i++) base |= (bk_move)(i < n ? ret[i] : GM_NONE) << (32 + 8 * i);
  return base;
}

/* the move picking OK (or ALT) on the card target would play: fields 0..3 */
static bk_move card_move(const gm_game *g, int target, bool alt) {
  (void)g;
  if (target >= TG_CARD && target < TG_DECK) {
    int t = (target - TG_CARD) / GM_SLOTS, s = (target - TG_CARD) % GM_SLOTS;
    return bk_move_pack(alt ? GM_MV_RESERVE : GM_MV_BUY, t, s, 0, 0, 0, 0, 0);
  }
  if (target >= TG_DECK && target < TG_RES) return alt ? bk_move_pack(GM_MV_RESERVE_DECK, target - TG_DECK, 0, 0, 0, 0, 0, 0) : BK_MOVE_NONE;
  if (target >= TG_RES && target < TG_HAND) return alt ? BK_MOVE_NONE : bk_move_pack(GM_MV_BUY_RES, target - TG_RES, 0, 0, 0, 0, 0, 0);
  return BK_MOVE_NONE;
}

/* is there a legal move with these fields 0..3 (any hand-backs)? */
static bool action_legal(const gm_game *g, bk_move base) {
  if (base == BK_MOVE_NONE) return false;
  bk_move mv[BK_MAX_MOVES];
  int n = legal(g, mv);
  int k = bk_move_field(base, 0);
  for (int i = 0; i < n; i++) {
    if (k == GM_MV_BUY || k == GM_MV_BUY_RES) {
      if (mv[i] == base) return true;
    } else if ((mv[i] & 0xffffffffull) == (base & 0xffffffffull))
      return true;
  }
  return false;
}

static int excess_after(const gm_state *st, bk_move base) {
  uint8_t h[GM_NTOK];
  bk_move none = with_rets(base, NULL, 0);
  if (!final_hand(st, none, h)) return 0;
  int n = 0;
  for (int i = 0; i < GM_NTOK; i++) n += h[i];
  return n - GM_HAND_CAP;
}

void gm_game_staged_hand(const gm_game *g, uint8_t hand[GM_NTOK]) {
  const gm_state *st = gm_game_state(g);
  memcpy(hand, st->s[st->seat].tok, GM_NTOK);
  if (g->ui.mode != UI_HANDBACK) return;
  final_hand(st, with_rets(g->ui.staged, g->ui.ret, g->ui.nret), hand);
}

/* ── the selection ──────────────────────────────────────────────────────────────────────────────── */

static void ui_reset(gm_game *g) {
  memset(&g->ui, 0, sizeof g->ui);
  g->ui.patron = -1;
  g->ui.card = -1;
  if (!gm_game_over(g) && gm_game_state(g)->phase == GM_PH_PATRON) g->ui.mode = UI_PATRON;
}

bool gm_target_active(const gm_game *g, int t) {
  const gm_state *st = gm_game_state(g);
  if (gm_game_over(g)) return false;
  const gm_seat *p = &st->s[st->seat];
  const gm_ui *u = &g->ui;
  switch (u->mode) {
  case UI_BROWSE:
    if (t >= TG_BANK && t < TG_BANK + GM_NCOLORS) {
      if (u->npick >= 3) return false;
      int pk[3] = {u->pick[0], u->pick[1], u->pick[2]};
      pk[u->npick] = t - TG_BANK;
      return pick_fits(g, pk, u->npick + 1, false);
    }
    if (t >= TG_CARD && t < TG_DECK) return st->market[(t - TG_CARD) / GM_SLOTS][(t - TG_CARD) % GM_SLOTS] != GM_NONE;
    if (t >= TG_DECK && t < TG_RES) return gm_deck_left(st, t - TG_DECK) > 0 && p->nres < GM_MAX_RES;
    if (t >= TG_RES && t < TG_HAND) return t - TG_RES < p->nres;
    if (t == TG_OK) return only_pass(g) || (u->npick > 0 && pick_fits(g, u->pick, u->npick, true));
    return false;
  case UI_CARD:
    if (t == u->card) return true;
    if (t == TG_OK) return action_legal(g, card_move(g, u->card, false));
    if (t == TG_ALT) return action_legal(g, card_move(g, u->card, true));
    return false;
  case UI_HANDBACK: {
    if (t == TG_OK) return u->nret == u->excess;
    if (t >= TG_HAND && t < TG_HAND + GM_NTOK && u->nret < u->excess) {
      uint8_t h[GM_NTOK];
      gm_game_staged_hand(g, h);
      return h[t - TG_HAND] > 0;
    }
    return false;
  }
  case UI_PATRON:
    if (t >= TG_PATRON && t < TG_PATRON + GM_NPATRONS) {
      int i = t - TG_PATRON;
      return st->patron_open[i] == 1 && gm_patron_met(p, i);
    }
    if (t == TG_OK) return u->patron >= 0;
    return false;
  }
  return false;
}

bk_move gm_game_preview(const gm_game *g) {
  const gm_ui *u = &g->ui;
  const gm_state *st = gm_game_state(g);
  if (gm_game_over(g)) return BK_MOVE_NONE;
  switch (u->mode) {
  case UI_BROWSE:
    if (u->npick > 0 && pick_fits(g, u->pick, u->npick, true)) return with_rets(take_move(u), NULL, 0);
    return BK_MOVE_NONE;
  case UI_CARD: {
    bk_move m = card_move(g, u->card, g->cursor_target == TG_ALT);
    return action_legal(g, m) ? with_rets(m, NULL, 0) : BK_MOVE_NONE;
  }
  case UI_HANDBACK: return with_rets(u->staged, u->ret, u->nret);
  case UI_PATRON: return u->patron >= 0 ? bk_move_pack(GM_MV_PATRON, u->patron, 0, 0, 0, 0, 0, 0) : BK_MOVE_NONE;
  }
  (void)st;
  return BK_MOVE_NONE;
}

const char *gm_ok_label(const gm_game *g) {
  if (!gm_target_active(g, TG_OK)) return "";
  switch (g->ui.mode) {
  case UI_BROWSE: return only_pass(g) ? "Pass" : "Take";
  case UI_CARD: return "Fulfil";
  case UI_HANDBACK: return "Hand back";
  case UI_PATRON: return "Welcome";
  }
  return "";
}
const char *gm_alt_label(const gm_game *g) { return gm_target_active(g, TG_ALT) ? "Set aside" : ""; }

static void play(gm_game *g, bk_move m) {
  const gm_state *st = gm_game_state(g);
  bk_move c = gm_canonical(st, m);
  if (c == BK_MOVE_NONE) CW_FATAL("facet: the selection built move 0x%llx, which the rules do not allow", (unsigned long long)m);
  if (!bk_match_play(g->match, c)) CW_FATAL("facet: the match refused move 0x%llx", (unsigned long long)c);
  ui_reset(g);
}

static void stage_or_play(gm_game *g, bk_move base) {
  int ex = excess_after(gm_game_state(g), base);
  if (ex <= 0) {
    play(g, with_rets(base, NULL, 0));
    return;
  }
  g->ui.mode = UI_HANDBACK;
  g->ui.staged = base & 0xffffffffull;
  g->ui.excess = ex;
  g->ui.nret = 0;
}

static int want_target; /* where the cursor goes after this press (-1: stays) */

static void press_pick(gm_game *g, int t) {
  gm_ui *u = &g->ui;
  if (t < 0 || !gm_target_active(g, t)) return;
  want_target = -1;
  switch (u->mode) {
  case UI_BROWSE:
    if (t < TG_BANK + GM_NCOLORS) {
      u->pick[u->npick++] = t - TG_BANK;
      if (pick_fits(g, u->pick, u->npick, true)) {
        /* whole: unless another stone could still join it, go to the plaque */
        bool more = false;
        for (int c = 0; c < GM_NCOLORS && !more && u->npick < 3; c++) more = gm_target_active(g, TG_BANK + c);
        if (!more) want_target = TG_OK;
      }
    } else if (t >= TG_CARD && t < TG_HAND) {
      u->npick = 0;
      u->mode = UI_CARD;
      u->card = t;
      want_target = gm_target_active(g, TG_OK) ? TG_OK : gm_target_active(g, TG_ALT) ? TG_ALT : -1;
      if (want_target < 0) { u->mode = UI_BROWSE; u->card = -1; u->message = GM_MSG_NOTHING_TO_DO; }
    } else if (t == TG_OK) {
      if (only_pass(g)) play(g, bk_move_pack(GM_MV_PASS, 0, 0, 0, 0, 0, 0, 0));
      else stage_or_play(g, take_move(u));
    }
    break;
  case UI_CARD:
    if (t == TG_OK || t == TG_ALT) {
      int card = u->card;
      stage_or_play(g, card_move(g, card, t == TG_ALT));
      if (g->ui.mode == UI_HANDBACK) want_target = -1;
    }
    break;
  case UI_HANDBACK:
    if (t == TG_OK) play(g, with_rets(u->staged, u->ret, u->nret));
    else {
      u->ret[u->nret++] = t - TG_HAND;
      if (u->nret == u->excess) want_target = TG_OK;
    }
    break;
  case UI_PATRON:
    if (t == TG_OK) play(g, bk_move_pack(GM_MV_PATRON, u->patron, 0, 0, 0, 0, 0, 0));
    else {
      u->patron = t - TG_PATRON;
      want_target = TG_OK;
    }
    break;
  }
}

static void press_back(gm_game *g) {
  gm_ui *u = &g->ui;
  want_target = -1;
  switch (u->mode) {
  case UI_BROWSE:
    if (u->npick > 0) want_target = TG_BANK + u->pick[--u->npick]; /* the stone goes back: the cursor to its stack */
    break;
  case UI_CARD:
    want_target = u->card;
    u->mode = UI_BROWSE;
    u->card = -1;
    break;
  case UI_HANDBACK:
    if (u->nret > 0) u->nret--;
    else {
      int k = bk_move_field(u->staged, 0);
      u->mode = UI_BROWSE;
      if (k == GM_MV_TAKE3 || k == GM_MV_TAKE2) want_target = TG_OK; /* the pick stays */
    }
    break;
  case UI_PATRON: u->patron = -1; break;
  }
}

/* ── the cursor ─────────────────────────────────────────────────────────────────────────────────── */

static void refresh_targets(gm_game *g) {
  int act[TG_COUNT], n = 0;
  uint64_t key = 1469598103934665603ull ^ (uint64_t)gm_game_state(g)->seat;
  for (int t = 0; t < TG_COUNT; t++)
    if (gm_target_active(g, t)) {
      act[n++] = t;
      key = (key ^ (uint64_t)(t + 1)) * 1099511628211ull;
    }
  int seat = gm_game_turn(g);
  if (key == g->active_key && seat == g->seat_shown) return;
  int stood = g->cursor_target;
  memcpy(g->active, act, sizeof(int) * (size_t)n);
  g->nactive = n;
  g->active_key = key;
  t3_vec3 pts[TG_COUNT];
  for (int i = 0; i < n; i++) pts[i] = gm_target_pos(g, act[i]);
  cw_cursors_set_points(bk_select_cursors(g->sel), pts, n);
  bk_select_enable_all(g->sel, true);
  if (g->have_view) cw_cursors_set_view(bk_select_cursors(g->sel), seat < 0 ? 0 : seat, &g->view);
  int place = -1;
  for (int i = 0; i < n; i++)
    if (act[i] == stood) place = i;
  /* a stack that can no longer be taken from (its stone just joined the pick): the nearest stack that still can,
   * the one below on a tie, rather than the first target. FACET_TEST_CURSOR_FIRST (a gate's control): the first target. */
  if (place < 0 && stood >= TG_BANK && stood < TG_BANK + GM_NCOLORS && !cw_test_env("FACET_TEST_CURSOR_FIRST")) {
    int best = 99;
    for (int i = 0; i < n; i++)
      if (act[i] >= TG_BANK && act[i] < TG_BANK + GM_NCOLORS) {
        int d = 2 * abs(act[i] - stood) - (act[i] > stood);
        if (d < best) best = d, place = i;
      }
  }
  if (seat >= 0) bk_select_place(g->sel, seat, place);
  g->seat_shown = seat;
}

static int index_of(const gm_game *g, int target) {
  for (int i = 0; i < g->nactive; i++)
    if (g->active[i] == target) return i;
  return -1;
}

static void go_to(gm_game *g, int seat, int target) {
  refresh_targets(g);
  int i = index_of(g, target);
  if (i >= 0) bk_select_place(g->sel, seat, i);
}

/* the picked stones float beside their stacks (drawn by the view at these canvas points, 16/1080 of the height
 * across): the one a tap lands on, -1 none */
cw_vec2 gm_pick_icon_at(const gm_game *g, int i) {
  float u = g->view.rect.h / 1080.0f, depth;
  cw_vec2 p = cw_camera3d_world_to_screen(g->view.camera, g->view.rect, gm_bank_pos(g->ui.pick[i]), &depth);
  return cw_v2(p.x + 70 * u + 22 * u * (float)i, p.y);
}
static int pick_icon_hit(const gm_game *g, cw_vec2 at) {
  if (!g->have_view || g->ui.mode != UI_BROWSE) return -1;
  float r = 20 * g->view.rect.h / 1080.0f;
  for (int i = g->ui.npick - 1; i >= 0; i--) {
    cw_vec2 p = gm_pick_icon_at(g, i);
    if ((p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y) <= r * r) return i;
  }
  return -1;
}

static void update_cursor_target(gm_game *g, int seat) {
  int i = seat >= 0 ? bk_select_target(g->sel, seat) : -1;
  g->cursor_target = i >= 0 && i < g->nactive ? g->active[i] : -1;
}

/* ── CPU seats: the presses that enter their move ───────────────────────────────────────────────── */

static bool press_toward(gm_game *g, int seat, int target, bk_press *out) {
  int i = index_of(g, target);
  if (i < 0) {
    /* not standable now: back out of whatever the selection holds */
    memset(out, 0, sizeof *out);
    out->v[0] = (cw_action_value){g->act.back, 1};
    out->count = 1;
    out->hold = 1;
    return true;
  }
  return bk_cpu_press_toward(bk_select_cursors(g->sel), NULL, seat, i, g->act.left, g->act.right, g->act.up, g->act.down, g->act.pick, out);
}

static bool press_back_out(gm_game *g, bk_press *out) {
  memset(out, 0, sizeof *out);
  out->v[0] = (cw_action_value){g->act.back, 1};
  out->count = 1;
  out->hold = 1;
  return true;
}

static bool next_press(const void *state, bk_move m, int seat, bk_press *out, void *user) {
  (void)state;
  gm_game *g = user;
  refresh_targets(g);
  gm_ui *u = &g->ui;
  int k = bk_move_field(m, 0);
  int want_rets[3], nr = 0;
  for (int i = 4; i <= 6; i++)
    if (bk_move_field(m, i) != GM_NONE && k != GM_MV_BUY && k != GM_MV_BUY_RES && k != GM_MV_PATRON && k != GM_MV_PASS)
      want_rets[nr++] = bk_move_field(m, i);
  if (k == GM_MV_PATRON) {
    if (u->mode != UI_PATRON) return press_back_out(g, out);
    if (u->patron != bk_move_field(m, 1)) return press_toward(g, seat, TG_PATRON + bk_move_field(m, 1), out);
    return press_toward(g, seat, TG_OK, out);
  }
  if (k == GM_MV_PASS) {
    if (u->mode != UI_BROWSE) return press_back_out(g, out);
    return press_toward(g, seat, TG_OK, out);
  }
  if (u->mode == UI_HANDBACK) {
    if ((u->staged & 0xffffffffull) != (m & 0xffffffffull)) return press_back_out(g, out);
    for (int i = 0; i < u->nret; i++)
      if (i >= nr || u->ret[i] != want_rets[i]) return press_back_out(g, out);
    if (u->nret < nr) return press_toward(g, seat, TG_HAND + want_rets[u->nret], out);
    return press_toward(g, seat, TG_OK, out);
  }
  if (k == GM_MV_TAKE3 || k == GM_MV_TAKE2) {
    if (u->mode != UI_BROWSE) return press_back_out(g, out);
    int cs[3], n = take_colours(m, cs);
    for (int i = 0; i < u->npick; i++)
      if (i >= n || u->pick[i] != cs[i]) return press_back_out(g, out);
    if (u->npick < n) return press_toward(g, seat, TG_BANK + cs[u->npick], out);
    return press_toward(g, seat, TG_OK, out);
  }
  /* a card: choose it, then the plaque */
  int target = -1;
  bool alt = k == GM_MV_RESERVE || k == GM_MV_RESERVE_DECK;
  if (k == GM_MV_BUY || k == GM_MV_RESERVE) target = TG_CARD + bk_move_field(m, 1) * GM_SLOTS + bk_move_field(m, 2);
  else if (k == GM_MV_RESERVE_DECK) target = TG_DECK + bk_move_field(m, 1);
  else if (k == GM_MV_BUY_RES) target = TG_RES + bk_move_field(m, 1);
  else CW_FATAL("facet cpu: no way to enter move kind %d", k);
  if (u->mode == UI_BROWSE) {
    if (u->npick) return press_back_out(g, out);
    return press_toward(g, seat, target, out);
  }
  if (u->mode == UI_CARD) {
    if (u->card != target) return press_back_out(g, out);
    return press_toward(g, seat, alt ? TG_ALT : TG_OK, out);
  }
  return press_back_out(g, out);
}

bk_move gm_game_cpu_intent(const gm_game *g) {
  int s = gm_game_over(g) ? -1 : gm_game_turn(g);
  return s >= 0 && g->cpu[s] ? bk_cpu_intent(g->cpu[s]) : BK_MOVE_NONE;
}
bool gm_game_cpu_thinking(const gm_game *g) {
  int s = gm_game_over(g) ? -1 : gm_game_turn(g);
  return s >= 0 && g->cpu[s] && bk_cpu_thinking(g->cpu[s]);
}

/* ── lifetime and tick ──────────────────────────────────────────────────────────────────────────── */

void gm_game_init(gm_game *g, const gm_actions *act) {
  memset(g, 0, sizeof *g);
  g->act = *act;
  g->sel = bk_select_new(&(bk_select_desc){.left = act->left, .right = act->right, .up = act->up, .down = act->down,
                                            .confirm = act->pick, .back = act->back});
  g->cursor_target = -1;
  g->seat_shown = -1;
}

void gm_game_end(gm_game *g) {
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    bk_cpu_free(g->cpu[s]);
    g->cpu[s] = NULL;
  }
  bk_match_free(g->match);
  g->match = NULL;
}

void gm_game_start(gm_game *g, int players, const int *cpu_level, uint64_t seed) {
  gm_game_end(g);
  g->players = players;
  g->seed = seed;
  gm_rules(&g->rules, players);
  gm_state st;
  gm_new(&st, players, seed);
  g->match = bk_match_new(&g->rules, &st, bk_seed_mix(seed, 0xfaceull));
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    g->cpu_level[s] = s < players ? cpu_level[s] : -1;
    if (g->cpu_level[s] < 0) continue;
    cw_search_options o;
    gm_cpu_options(g->cpu_level[s], players, &o);
    static const int UNITS[3] = {4000, 8000, 25000};
    g->cpu[s] = bk_cpu_new(&g->rules, &(bk_cpu_desc){.level = g->cpu_level[s], .options = &o,
                                                     .units_per_tick = UNITS[g->cpu_level[s] > 2 ? 2 : g->cpu_level[s]],
                                                     .seed = bk_seed_mix(seed, (uint64_t)s + 11), .next_press = next_press,
                                                     .user = g});
  }
  g->active_key = 0;
  g->cursor_target = -1;
  g->seat_shown = -1;
  g->secondary_was = false;
  g->log_seen = bk_match_log_next(g->match);
  ui_reset(g);
  refresh_targets(g);
}

bool gm_game_tick(gm_game *g) {
  if (!g->match) return false;
  /* every CPU seat presses (or releases) its actions before anything reads input */
  for (int s = 0; s < g->players; s++) {
    if (!g->cpu[s]) continue;
    cw_action_value v[4];
    int n = bk_cpu_tick(g->cpu[s], g->match, s, v, 4);
    cw_input_set_actions(s, v, n);
  }
  if (gm_game_over(g)) return false;
  int seat = gm_game_turn(g);
  int before = bk_match_log_next(g->match);
  refresh_targets(g);
  /* the pointer (this tick's, recorded): a right click steps back, a tap on a picked stone puts that stone back, any
   * other tap picks through bk_select */
  cw_pointer pt = cw_input_pointer();
  bool sec_press = pt.secondary && !g->secondary_was;
  g->secondary_was = pt.secondary;
  int put_back = -1;
  if (!g->cpu[seat]) {
    if (pt.pressed && !pt.secondary) put_back = pick_icon_hit(g, cw_r2d_window_to_canvas(pt.pos));
    if (put_back < 0 && !pt.secondary) bk_select_feed(g->sel, seat);
  }
  bk_sel_event e = bk_select_tick(g->sel, seat);
  if (!g->cpu[seat] && sec_press) e.kind = BK_SEL_BACK;
  update_cursor_target(g, seat);
  want_target = -1;
  if (e.kind == BK_SEL_CONFIRM) {
    g->ui.message = GM_MSG_NONE;
    press_pick(g, g->cursor_target);
  } else if (e.kind == BK_SEL_BACK) {
    g->ui.message = GM_MSG_NONE;
    press_back(g);
  }
  if (put_back >= 0) {
    gm_ui *u = &g->ui;
    int c = u->pick[put_back];
    for (int i = put_back; i + 1 < u->npick; i++) u->pick[i] = u->pick[i + 1];
    u->npick--;
    g->ui.message = GM_MSG_NONE;
    want_target = TG_BANK + c;
  }
  bool landed = bk_match_log_next(g->match) != before;
  if (landed) {
    want_target = -1;
    refresh_targets(g);
    int s2 = gm_game_over(g) ? -1 : gm_game_turn(g);
    if (s2 >= 0) update_cursor_target(g, s2);
  } else if (want_target >= 0) {
    go_to(g, seat, want_target);
    update_cursor_target(g, seat);
  } else {
    refresh_targets(g);
    update_cursor_target(g, seat);
  }
  return landed;
}
