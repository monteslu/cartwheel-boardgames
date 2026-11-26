/* Facet: the app. The shell (lobby, pause, settings), the table in 3D (velvet, brass, cards drawn once into
 * textures, faceted stones from the cuts in cuts.c), the HUD and the flow from the how-to-play card through a match
 * to the results. Rules and the selection live in rules.c and game.c; this file only reads them. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_shell.h"
#include "boardkit/bk_view.h"
#include "cartwheel/debug.h"
#include "cartwheel/pick3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "cuts.h"
#include "game.h"
#include "gemtrace.h"
#include "cardgems.h"

#define PI 3.14159265f

/* ── the stones' look ───────────────────────────────────────────────────────────────────────────── */

static const char *const STONE[GM_NTOK] = {"Ruby", "Sapphire", "Emerald", "Amethyst", "Diamond", "Topaz"};
static const int CUT_OF[GM_NTOK] = {0, 3, 2, 4, 5, 1}; /* jewels' cuts: princess, pear, emerald, marquise, round, trillion */
/* linear rgb body colours, and a lighter tone for card ink */
static const float BODY[GM_NTOK][3] = {
    {0.62f, 0.03f, 0.07f}, {0.05f, 0.16f, 0.62f}, {0.03f, 0.42f, 0.16f},
    {0.36f, 0.10f, 0.55f}, {0.80f, 0.84f, 0.90f}, {0.85f, 0.46f, 0.06f}};
static cw_color stone_srgb(int c, float k) {
  /* rough sRGB for 2D art */
  return cw_rgba(fminf(1, powf(BODY[c][0], 1 / 2.2f) * k), fminf(1, powf(BODY[c][1], 1 / 2.2f) * k),
                 fminf(1, powf(BODY[c][2], 1 / 2.2f) * k), 1);
}

/* ── state ─────────────────────────────────────────────────────────────────────────────────────── */

enum { FLOW_HOWTO, FLOW_PLAY, FLOW_RESULTS };
enum { FACE_PATRON = 100, FACE_BACK_TIER = BK_FACE_BACK }; /* fronts 0..89 commissions, 100.. patrons */

#define NTOKENS (GM_NCOLORS * 7 + 5)
typedef struct token { int colour, holder, slot; t3_vec3 pos; float vy; cw_node node; bool landed; } token;
/* holders: 0 the bank, 1 + seat */

static struct {
  cw_tree *tree;
  cw_node world, cam, cards[GM_NCARDS], patrons[GM_NPATRONS], decks[GM_TIERS], deck_top[GM_TIERS], plaque[2], ring;
  cw_node mats[BK_MAX_SEATS], mat_trim[BK_MAX_SEATS], ghost_root;
  t3_vec3 card_pos[GM_NCARDS];
  float card_scale[GM_NCARDS], card_lift[GM_NCARDS];
  cw_node halo[GM_NCARDS]; /* a warm glow under each card, lit while the person to move can fulfil it */
  float halo_k[GM_NCARDS];  /* its eased strength 0..1: it swells in and fades, never flashes */
  t3_quat card_rot[GM_NCARDS];
  bool card_shown[GM_NCARDS];
  t3_vec3 patron_pos[GM_NPATRONS];
  float patron_scale[GM_NPATRONS];
  token tok[NTOKENS];
  gm_game g;
  int flow;
  bool in_match;
  bk_seat seats[BK_MAX_SEATS];
  int players;
  cw_font serif, sans;
  bk_counter points[BK_MAX_SEATS];
  bk_counter bank[GM_NTOK];  /* the number beside each stack in the tray, counting to the new count */
  /* each mat's columns: the cards' bonus stones and the loose stones per colour, counting to the rules' values; a
   * bought card's bonus stone flies from the card to its column first, and the bonus count ticks when it lands */
  bk_counter bonus[BK_MAX_SEATS][GM_NCOLORS], toks[BK_MAX_SEATS][GM_NTOK];
  struct { bool live; int seat, colour; cw_vec2 from; float t; } fly[16];
  int bank_stack[GM_NTOK];   /* settings standing in each tray stack this frame */
  bk_deltas deltas;
  float t, ring_glow, plaque_k[2];
  t3_vec3 ring_pos;
  int ticks;
  uint64_t match_no;
  int results_pick, results_ticks;
  uint64_t match_shown; /* the match the mats' counters follow (a new match starts them over, no flights) */
  bool built;
  cw_vec2 canvas;    /* the HUD pass's canvas size, which the cursor's view and the labels use */
  gm_state last;     /* the state the view last showed (score deltas) */
  uint64_t start_seed; /* this match's seed and the flow it began in (a replay starts the same) */
  int start_flow;
  bool fresh;          /* the start tick: not recorded, play begins at the next */
} V;

/* the replay check (debug command replay) */
static struct {
  int32_t replaying, replay_done, replay_match, replay_hash_match;
  uint32_t want_hash, want_input;
  uint8_t *recording;
  size_t len;
} R;

static int a_up, a_down, a_left, a_right, a_pick, a_back;

/* debug fields: what gates and bots read (facet.*) */
static struct {
  int32_t ticks, flow, in_match, players, seat, turns, over, winners, points[BK_MAX_SEATS], cursor, ui_mode, npick, moves,
      match_no, cpu_seat, phase, nres[BK_MAX_SEATS], hand[BK_MAX_SEATS], cards[BK_MAX_SEATS], ok_on, alt_on, excess;
  uint32_t hash_lo, hash_hi;
  /* what the screen offers the seat to move: the targets the cursor may stand on, where they are on screen
   * (canvas px), and which cards it could pay for now (what the card panel says on each) */
  int32_t active_n, active[TG_COUNT], afford[TG_COUNT], finished;
  /* the tray's counts: the rules' piles, the number each stack's label is counting toward and showing, the
   * settings standing in each stack, and how many colours disagree (a settled label or a stack that is not the
   * pile; a label still counting is held to its target) */
  int32_t bank_rules[GM_NTOK], bank_target[GM_NTOK], bank_shown[GM_NTOK], bank_stack[GM_NTOK], bank_mismatch;
  /* the mats' columns: what each shows for bonuses and stones (seat * 6 + colour), and how many disagree with the
   * rules once settled (a count still counting, or a bonus stone still flying, is held to its target) */
  int32_t panel_bonus[BK_MAX_SEATS * GM_NTOK], panel_tokens[BK_MAX_SEATS * GM_NTOK], panel_flying, panel_mismatch;
  /* the glow under cards (per target, market and the mover's set-asides): lit, which the rules' legal moves can fulfil
   * now (0 on a CPU turn), how many targets disagree, and how many halos shine at half strength or more */
  int32_t glow[TG_COUNT], buyable[TG_COUNT], glow_mismatch, glow_lit;
  float mat_row_y[3];  /* where the first mat draws its rows (canvas px): the stones, the cards, the buys */
  int32_t prompt_back; /* the person's prompt line offers back (a stone picked, a card chosen, a hand-back begun) */
  int32_t tok[BK_MAX_SEATS * GM_NTOK]; /* the rules' stones per seat (seat * 6 + colour) */
  float pick_xy[6];                     /* where the picked stones float (canvas px; a tap there puts one back) */
  float active_xy[2 * TG_COUNT];
} D;

static void cmd_replay(int argc, const char **argv, cw_reply *r);
static void debug_register(void) {
  cw_debug_i32("facet.ticks", &D.ticks);
  cw_debug_i32("facet.flow", &D.flow); /* 0 how-to-play, 1 play, 2 results */
  cw_debug_i32("facet.in_match", &D.in_match);
  cw_debug_i32("facet.players", &D.players);
  cw_debug_i32("facet.seat", &D.seat);
  cw_debug_i32("facet.turns", &D.turns);
  cw_debug_i32("facet.over", &D.over);
  cw_debug_i32("facet.winners", &D.winners);
  cw_debug_field("facet.points", D.points, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_field("facet.nres", D.nres, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_field("facet.hand", D.hand, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_field("facet.cards", D.cards, CW_DBG_I32, BK_MAX_SEATS);
  cw_debug_i32("facet.cursor", &D.cursor);
  cw_debug_i32("facet.ui_mode", &D.ui_mode);
  cw_debug_i32("facet.npick", &D.npick);
  cw_debug_i32("facet.moves", &D.moves);
  cw_debug_i32("facet.match_no", &D.match_no);
  cw_debug_i32("facet.cpu_seat", &D.cpu_seat);
  cw_debug_i32("facet.phase", &D.phase);
  cw_debug_i32("facet.ok_on", &D.ok_on);
  cw_debug_i32("facet.alt_on", &D.alt_on);
  cw_debug_i32("facet.excess", &D.excess);
  cw_debug_u32("facet.hash_lo", &D.hash_lo);
  cw_debug_i32("facet.active_n", &D.active_n);
  cw_debug_field("facet.active", D.active, CW_DBG_I32, TG_COUNT);
  cw_debug_field("facet.active_xy", D.active_xy, CW_DBG_F32, 2 * TG_COUNT);
  cw_debug_field("facet.afford", D.afford, CW_DBG_I32, TG_COUNT);
  cw_debug_i32("facet.finished", &D.finished);
  cw_debug_field("facet.bank_rules", D.bank_rules, CW_DBG_I32, GM_NTOK);
  cw_debug_field("facet.bank_target", D.bank_target, CW_DBG_I32, GM_NTOK);
  cw_debug_field("facet.bank_shown", D.bank_shown, CW_DBG_I32, GM_NTOK);
  cw_debug_field("facet.bank_stack", D.bank_stack, CW_DBG_I32, GM_NTOK);
  cw_debug_i32("facet.bank_mismatch", &D.bank_mismatch);
  cw_debug_field("facet.panel_bonus", D.panel_bonus, CW_DBG_I32, BK_MAX_SEATS * GM_NTOK);
  cw_debug_field("facet.panel_tokens", D.panel_tokens, CW_DBG_I32, BK_MAX_SEATS * GM_NTOK);
  cw_debug_i32("facet.panel_flying", &D.panel_flying);
  cw_debug_i32("facet.panel_mismatch", &D.panel_mismatch);
  cw_debug_u32("facet.hash_hi", &D.hash_hi);
  cw_debug_field("facet.glow", D.glow, CW_DBG_I32, TG_COUNT);
  cw_debug_field("facet.buyable", D.buyable, CW_DBG_I32, TG_COUNT);
  cw_debug_i32("facet.glow_mismatch", &D.glow_mismatch);
  cw_debug_i32("facet.glow_lit", &D.glow_lit);
  cw_debug_f32_array("facet.mat_row_y", D.mat_row_y, 3);
  cw_debug_i32("facet.prompt_back", &D.prompt_back);
  cw_debug_field("facet.tok", D.tok, CW_DBG_I32, BK_MAX_SEATS * GM_NTOK);
  cw_debug_field("facet.pick_xy", D.pick_xy, CW_DBG_F32, 6);
  cw_debug_i32("facet.replaying", &R.replaying);
  cw_debug_i32("facet.replay_done", &R.replay_done);
  cw_debug_i32("facet.replay_match", &R.replay_match);
  cw_debug_i32("facet.replay_hash_match", &R.replay_hash_match);
  cw_debug_command("replay", "replay: replay the match from its start out of its own recording, then compare (facet.replay_match)", cmd_replay);
}

static gm_game *game_of(void);
/* the card under a market or set-aside target of the seat to move, -1 none */
static int card_at(const gm_state *st, int t) {
  int card = -1;
  if (t >= TG_CARD && t < TG_DECK) card = st->market[(t - TG_CARD) / GM_SLOTS][(t - TG_CARD) % GM_SLOTS];
  else if (t >= TG_RES && t < TG_HAND && t - TG_RES < st->s[st->seat].nres) card = st->s[st->seat].res[t - TG_RES];
  return card == GM_NONE ? -1 : card;
}
/* does the glow light under this target: a person to move, in the action phase, who can pay for its card now
 * (bonuses, stones and topaz). FACET_TEST_GLOW_ALL lights every card in a match, whoever moves and the how-to-play
 * card up or not: the play test's control, and the program survey's way to see a halo (facet_auto). */
static bool glow_wanted(const gm_state *st, int t) {
  gm_game *g = game_of();
  bool all = cw_test_env("FACET_TEST_GLOW_ALL") != NULL;
  if (all) return V.in_match && g->match && card_at(st, t) >= 0;
  if (!g->match || gm_game_over(g) || (V.flow != FLOW_PLAY && !(all && V.flow == FLOW_HOWTO)) || !V.in_match || g->cpu[st->seat] ||
      st->phase != GM_PH_ACT)
    return false;
  int card = card_at(st, t);
  if (card < 0) return false;
  uint8_t pay[GM_NTOK];
  return all || gm_price(&st->s[st->seat], card, pay);
}

static void debug_update(void) {
  D.flow = V.flow;
  D.in_match = V.in_match;
  if (!V.g.match) return;
  const gm_state *st = gm_game_state(&V.g);
  D.players = st->players;
  D.seat = st->seat;
  D.turns = st->turns;
  D.over = gm_game_over(&V.g);
  D.winners = st->winners;
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    D.points[s] = st->s[s].points;
    D.nres[s] = st->s[s].nres;
    D.hand[s] = gm_hand(&st->s[s]);
    D.cards[s] = st->s[s].ncards;
    for (int c = 0; c < GM_NTOK; c++) D.tok[s * GM_NTOK + c] = st->s[s].tok[c];
  }
  D.cursor = V.g.cursor_target;
  D.ui_mode = (int32_t)V.g.ui.mode;
  D.npick = V.g.ui.npick;
  for (int i = 0; i < 3; i++) {
    cw_vec2 p = i < V.g.ui.npick && V.g.have_view ? gm_pick_icon_at(&V.g, i) : cw_v2(0, 0);
    D.pick_xy[2 * i] = p.x, D.pick_xy[2 * i + 1] = p.y;
  }
  D.excess = V.g.ui.excess;
  D.moves = bk_match_moves_played(V.g.match);
  D.match_no = (int32_t)V.match_no;
  D.cpu_seat = V.g.cpu[st->seat] != NULL;
  D.phase = st->phase;
  D.ok_on = gm_target_active(&V.g, TG_OK);
  D.alt_on = gm_target_active(&V.g, TG_ALT);
  D.active_n = 0;
  for (int t = 0; t < TG_COUNT; t++) {
    D.afford[t] = 0;
    D.active[t] = -1;
    D.active_xy[2 * t] = D.active_xy[2 * t + 1] = 0;
  }
  if (!gm_game_over(&V.g) && V.flow == FLOW_PLAY) {
    cw_cursors *cur = bk_select_cursors(V.g.sel);
    for (int i = 0; i < V.g.nactive; i++) {
      D.active[i] = V.g.active[i];
      cw_vec2 p = cw_cursors_screen(cur, st->seat, i);
      D.active_xy[2 * i] = p.x, D.active_xy[2 * i + 1] = p.y;
    }
    D.active_n = V.g.nactive;
    uint8_t pay[GM_NTOK];
    for (int t = TG_CARD; t < TG_HAND; t++) {
      int card = -1;
      if (t < TG_DECK) card = st->market[(t - TG_CARD) / GM_SLOTS][(t - TG_CARD) % GM_SLOTS];
      else if (t >= TG_RES && t - TG_RES < st->s[st->seat].nres) card = st->s[st->seat].res[t - TG_RES];
      D.afford[t] = card >= 0 && card != GM_NONE && gm_price(&st->s[st->seat], card, pay);
    }
  }
  /* the glow against the rules' own list of legal moves */
  {
    bk_move mv[BK_MAX_MOVES];
    int n = gm_game_over(&V.g) ? 0 : bk_match_moves(V.g.match, mv, BK_MAX_MOVES);
    bool person = V.flow == FLOW_PLAY && !gm_game_over(&V.g) && !V.g.cpu[st->seat];
    D.glow_mismatch = 0;
    for (int t = 0; t < TG_COUNT; t++) D.glow[t] = D.buyable[t] = 0;
    for (int i = 0; i < n; i++) {
      int k = bk_move_field(mv[i], 0);
      if (k == GM_MV_BUY) D.buyable[TG_CARD + bk_move_field(mv[i], 1) * GM_SLOTS + bk_move_field(mv[i], 2)] = 1;
      if (k == GM_MV_BUY_RES) D.buyable[TG_RES + bk_move_field(mv[i], 1)] = 1;
    }
    for (int t = TG_CARD; t < TG_HAND; t++) {
      if (t >= TG_DECK && t < TG_RES) continue;
      D.glow[t] = glow_wanted(st, t);
      if (!person) D.buyable[t] = 0;
      D.glow_mismatch += D.glow[t] != D.buyable[t];
    }
    D.glow_lit = 0;
    for (int c = 0; c < GM_NCARDS; c++) D.glow_lit += V.halo_k[c] >= 0.5f;
  }
  uint64_t h = bk_match_hash(V.g.match);
  D.hash_lo = (uint32_t)h, D.hash_hi = (uint32_t)(h >> 32);
}

/* ── card faces (drawn once each into a texture: bk_card) ──────────────────────────────────────── */

static void gem_icon(cw_vec2 c, float r, int colour, float alpha) {
  /* a faceted stone seen from above, its outline the colour's own cut (shape tells colours apart too) */
  cw_vec2 pts[16];
  int n = 0;
  int cut = CUT_OF[colour];
  if (cut == 0) { /* princess: a square on its point */
    for (int i = 0; i < 4; i++) pts[n++] = cw_v2(c.x + r * cosf(PI * 0.5f * i), c.y + r * sinf(PI * 0.5f * i));
  } else if (cut == 3) { /* pear */
    for (int i = 0; i < 12; i++) {
      float t = 2 * PI * i / 12;
      pts[n++] = cw_v2(c.x + r * 0.82f * sinf(t) * powf(fabsf(sinf(t * 0.5f)), 0.85f), c.y - r * cosf(t));
    }
  } else if (cut == 2) { /* emerald cut: an octagon, longer than wide */
    float w = r * 0.72f, h = r, k = r * 0.28f;
    cw_vec2 q[8] = {{-w + k, -h}, {w - k, -h}, {w, -h + k}, {w, h - k}, {w - k, h}, {-w + k, h}, {-w, h - k}, {-w, -h + k}};
    for (int i = 0; i < 8; i++) pts[n++] = cw_v2(c.x + q[i].x, c.y + q[i].y);
  } else if (cut == 4) { /* marquise */
    for (int i = 0; i < 14; i++) {
      float t = 2 * PI * i / 14;
      float s = sinf(t);
      pts[n++] = cw_v2(c.x + r * 0.52f * s * fabsf(s) * 0 + r * 0.5f * sinf(t), c.y - r * cosf(t));
      (void)s;
    }
  } else if (cut == 5) { /* round */
    for (int i = 0; i < 16; i++) pts[n++] = cw_v2(c.x + r * cosf(2 * PI * i / 16), c.y + r * sinf(2 * PI * i / 16));
  } else { /* trillion */
    for (int i = 0; i < 3; i++) pts[n++] = cw_v2(c.x + r * cosf(-PI / 2 + 2 * PI * i / 3), c.y + r * sinf(-PI / 2 + 2 * PI * i / 3) + r * 0.12f);
  }
  cw_color body = stone_srgb(colour, 1.0f), dark = stone_srgb(colour, 0.55f), lite = stone_srgb(colour, 1.6f);
  body.a = dark.a = lite.a = alpha;
  cw_r2d_polygon(pts, n, dark);
  /* facets: a lighter crown toward the upper left */
  cw_vec2 inner[16];
  for (int i = 0; i < n; i++) inner[i] = cw_v2(c.x + (pts[i].x - c.x) * 0.55f, c.y + (pts[i].y - c.y) * 0.55f);
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    cw_vec2 tri[4] = {pts[i], pts[j], inner[j], inner[i]};
    float mx = (pts[i].x + pts[j].x) * 0.5f - c.x, my = (pts[i].y + pts[j].y) * 0.5f - c.y;
    float lit = 0.5f + 0.5f * (-(mx + my) / (r * 1.4142f));
    cw_color f = cw_rgba(dark.r + (lite.r - dark.r) * lit, dark.g + (lite.g - dark.g) * lit, dark.b + (lite.b - dark.b) * lit, alpha);
    cw_r2d_polygon(tri, 4, f);
  }
  cw_r2d_polygon(inner, n, body);
  cw_r2d_polygon_lines(pts, n, r * 0.06f, cw_rgba(0.1f, 0.07f, 0.05f, 0.55f * alpha));
  cw_r2d_circle(cw_v2(c.x - r * 0.22f, c.y - r * 0.25f), r * 0.12f, 10, cw_rgba(1, 1, 1, 0.55f * alpha));
}

static cw_text_metrics text_c(cw_font f, const char *s, float x, float y, float size, cw_color col, cw_text_align al) {
  cw_text_style st = {.size = size, .color = col, .align = al, .own_backdrop = true, .fixed_size = true};
  cw_text_metrics m = cw_font_measure(f, s, &st);
  float ox = al == CW_ALIGN_CENTER ? m.width * 0.5f : al == CW_ALIGN_RIGHT ? m.width : 0;
  st.align = CW_ALIGN_LEFT;
  return cw_r2d_text(f, s, cw_v2(x - ox, y), &st);
}

static void paper(cw_vec2 px, cw_color base, cw_color edge) {
  cw_r2d_rect(cw_rect_make(0, 0, px.x, px.y), edge);
  cw_r2d_round_rect(cw_rect_make(px.x * 0.035f, px.x * 0.035f, px.x * 0.93f, px.y - px.x * 0.07f), px.x * 0.05f, 6, base);
  /* soft fibres: fixed bands, the same every time */
  for (int i = 0; i < 9; i++) {
    float y = px.y * (0.08f + 0.1f * i);
    cw_r2d_rect(cw_rect_make(px.x * 0.06f, y, px.x * 0.88f, px.y * 0.012f), cw_rgba(1, 1, 1, 0.035f));
  }
}

/* the motif of a piece of work: rings of filigree around its stone, richer with the tier */
static void motif(cw_vec2 c, float r, int tier, int colour, int kind) {
  cw_color gold = cw_rgba(0.62f, 0.45f, 0.18f, 1), gold2 = cw_rgba(0.78f, 0.6f, 0.28f, 1);
  int petals = 6 + 2 * tier + kind % 3 * 2;
  for (int i = 0; i < petals; i++) {
    float a = 2 * PI * i / petals + kind * 0.37f;
    cw_vec2 p = cw_v2(c.x + cosf(a) * r * 0.78f, c.y + sinf(a) * r * 0.78f);
    cw_r2d_line(c, p, r * 0.03f, cw_rgba(0.62f, 0.45f, 0.18f, 0.5f));
    cw_r2d_circle(p, r * (0.08f + 0.02f * tier), 12, gold2);
    cw_r2d_circle(p, r * (0.05f + 0.015f * tier), 12, stone_srgb(colour, 0.8f + 0.1f * (i % 2)));
  }
  cw_r2d_circle_lines(c, r * 0.78f, r * 0.03f, 48, gold);
  if (tier >= 1) cw_r2d_circle_lines(c, r * 0.95f, r * 0.025f, 48, gold);
  if (tier >= 2)
    for (int i = 0; i < 24; i++) {
      float a = 2 * PI * i / 24;
      cw_r2d_circle(cw_v2(c.x + cosf(a) * r * 1.05f, c.y + sinf(a) * r * 1.05f), r * 0.035f, 8, gold2);
    }
  cw_r2d_circle(c, r * 0.5f, 32, gold);
  cw_r2d_circle(c, r * 0.44f, 32, cw_rgba(0.2f, 0.15f, 0.1f, 1));
  gem_icon(c, r * 0.4f, colour, 1);
}

/* a printed stone: the cut's own baked picture (cardgems.c) over a soft shadow, size = its width */
static void printed_gem(int colour, cw_vec2 c, float size) {
  cw_r2d_circle(cw_v2(c.x + size * 0.06f, c.y + size * 0.36f), size * 0.42f, 24, cw_rgba(0.18f, 0.12f, 0.06f, 0.16f));
  cw_r2d_circle(cw_v2(c.x + size * 0.04f, c.y + size * 0.3f), size * 0.33f, 24, cw_rgba(0.18f, 0.12f, 0.06f, 0.16f));
  cw_r2d_sprite(&(cw_sprite){.texture = gm_cardgem_texture(CUT_OF[colour]), .pos = c, .size = {size, size}, .pivot = {0.5f, 0.5f}});
}

/* the stones a card asks for: one row each, the stone and its count beside it, from the bottom up. tile: a patron's
 * needs are bonuses (cards), so each stone sits on a small card */
static void cost_rows(const uint8_t *need, cw_vec2 px, float x0, float y0, float step, float gem, bool tile) {
  float y = y0;
  for (int c = GM_NCOLORS - 1; c >= 0; c--) {
    if (!need[c]) continue;
    cw_vec2 at = cw_v2(x0, y);
    if (tile) {
      float w = gem * 0.78f, h = gem * 1.02f;
      cw_r2d_round_rect(cw_rect_make(at.x - w * 0.5f, at.y - h * 0.5f, w, h), w * 0.12f, 4, cw_rgba(0.97f, 0.94f, 0.86f, 1));
      cw_r2d_round_rect_lines(cw_rect_make(at.x - w * 0.5f, at.y - h * 0.5f, w, h), w * 0.12f, px.x * 0.008f, 4, stone_srgb(c, 0.9f));
    }
    printed_gem(c, at, gem * (tile ? 0.72f : 1.0f));
    char b[4];
    snprintf(b, sizeof b, "%d", need[c]);
    cw_text_style ts = {.size = step * 0.92f, .color = cw_rgba(0.12f, 0.08f, 0.04f, 1), .own_backdrop = true, .fixed_size = true,
                        .outline_width = step * 0.06f, .outline_color = cw_rgba(1, 0.97f, 0.9f, 1)};
    cw_text_metrics m = cw_font_measure(V.sans, b, &ts);
    cw_r2d_text(V.sans, b, cw_v2(at.x + gem * 0.56f, at.y - m.height * 0.52f), &ts);
    y -= step;
  }
}

static void paint(int face, cw_vec2 px, void *user) {
  (void)user;
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.07f, 1);
  if (face >= FACE_BACK_TIER) { /* a deck's back: dark leather, the guild's mark, the tier's pips */
    int tier = face - FACE_BACK_TIER;
    static const float TINT[3][3] = {{0.20f, 0.12f, 0.08f}, {0.10f, 0.14f, 0.20f}, {0.18f, 0.08f, 0.16f}};
    cw_r2d_rect(cw_rect_make(0, 0, px.x, px.y), cw_rgba(0.08f, 0.06f, 0.05f, 1));
    cw_r2d_round_rect(cw_rect_make(px.x * 0.05f, px.x * 0.05f, px.x * 0.9f, px.y - px.x * 0.1f), px.x * 0.05f, 6,
                      cw_rgba(TINT[tier][0], TINT[tier][1], TINT[tier][2], 1));
    cw_color gold = cw_rgba(0.75f, 0.58f, 0.26f, 1);
    cw_r2d_round_rect_lines(cw_rect_make(px.x * 0.1f, px.x * 0.1f, px.x * 0.8f, px.y - px.x * 0.2f), px.x * 0.04f, px.x * 0.012f, 6, gold);
    cw_vec2 c = cw_v2(px.x * 0.5f, px.y * 0.46f);
    float r = px.x * 0.3f;
    cw_vec2 d[4] = {{c.x, c.y - r}, {c.x + r * 0.8f, c.y}, {c.x, c.y + r}, {c.x - r * 0.8f, c.y}};
    cw_r2d_polygon_lines(d, 4, px.x * 0.02f, gold);
    cw_vec2 d2[4] = {{c.x, c.y - r * 0.55f}, {c.x + r * 0.44f, c.y}, {c.x, c.y + r * 0.55f}, {c.x - r * 0.44f, c.y}};
    cw_r2d_polygon(d2, 4, cw_rgba(0.75f, 0.58f, 0.26f, 0.35f));
    cw_r2d_line(d[0], d[2], px.x * 0.01f, gold);
    cw_r2d_line(d[1], d[3], px.x * 0.01f, gold);
    for (int i = 0; i <= tier; i++) cw_r2d_circle(cw_v2(px.x * (0.5f + 0.12f * (i - tier * 0.5f)), px.y * 0.83f), px.x * 0.035f, 12, gold);
    text_c(V.serif, "FACET", px.x * 0.5f, px.y * 0.12f, px.x * 0.13f, gold, CW_ALIGN_CENTER);
    return;
  }
  if (face >= FACE_PATRON) { /* a patron: a portrait medallion and the bonuses they ask for */
    const gm_patron *pt = &gm_patron_set[face - FACE_PATRON];
    paper(px, cw_rgba(0.86f, 0.8f, 0.68f, 1), cw_rgba(0.42f, 0.3f, 0.18f, 1));
    cw_vec2 c = cw_v2(px.x * 0.7f, px.y * 0.62f);
    float r = px.x * 0.21f;
    int seed = face - FACE_PATRON;
    cw_color sky = cw_rgba(0.25f + 0.05f * (seed % 3), 0.3f + 0.04f * (seed % 4), 0.38f, 1);
    cw_r2d_circle(c, r * 1.08f, 40, cw_rgba(0.62f, 0.45f, 0.18f, 1));
    cw_r2d_circle(c, r, 40, sky);
    /* a bust: shoulders and a head, a hat or hair by the patron */
    cw_color cloth = cw_rgba(0.18f + 0.06f * (seed % 4), 0.12f + 0.04f * (seed % 3), 0.1f + 0.05f * (seed % 5), 1);
    cw_r2d_push_clip(cw_rect_make(c.x - r, c.y - r, 2 * r, 2 * r));
    cw_r2d_circle(cw_v2(c.x, c.y + r * 1.05f), r * 0.78f, 32, cloth);
    cw_r2d_circle(cw_v2(c.x, c.y - r * 0.12f), r * 0.33f, 28, cw_rgba(0.82f, 0.66f, 0.52f, 1));
    if (seed % 2) cw_r2d_rect(cw_rect_make(c.x - r * 0.38f, c.y - r * 0.55f, r * 0.76f, r * 0.16f), cloth);
    else cw_r2d_arc(cw_v2(c.x, c.y - r * 0.12f), r * 0.3f, r * 0.4f, PI, 2 * PI, 16, cw_rgba(0.25f, 0.16f, 0.1f, 1));
    cw_r2d_pop_clip();
    cw_r2d_circle_lines(c, r, r * 0.05f, 40, cw_rgba(0.35f, 0.25f, 0.12f, 1));
    char b[8];
    snprintf(b, sizeof b, "%d", pt->points);
    text_c(V.serif, b, px.x * 0.72f, px.y * 0.04f, px.x * 0.3f, ink, CW_ALIGN_CENTER);
    cost_rows(pt->need, px, px.x * 0.16f, px.y * 0.83f, px.y * 0.22f, px.x * 0.23f, true);
    return;
  }
  /* a commission: its points in the band of its colour, its bonus stone large in a plain ring, what it costs down the
   * left; nothing else, so a card reads at a glance (its name is on the card panel) */
  const gm_card *cd = &gm_card_set[face];
  paper(px, cw_rgba(0.9f, 0.85f, 0.74f, 1), cw_rgba(0.42f, 0.3f, 0.18f, 1));
  cw_r2d_round_rect(cw_rect_make(px.x * 0.06f, px.x * 0.06f, px.x * 0.88f, px.y * 0.17f), px.x * 0.04f, 6, stone_srgb(cd->bonus, 0.95f));
  if (cd->points) {
    char b[4];
    snprintf(b, sizeof b, "%d", cd->points);
    cw_text_style ts = {.size = px.y * 0.17f, .color = cw_rgba(1, 0.97f, 0.9f, 1), .own_backdrop = true, .fixed_size = true,
                        .outline_width = px.y * 0.012f, .outline_color = cw_rgba(0.12f, 0.06f, 0.02f, 1)};
    cw_text_metrics m = cw_font_measure(V.serif, b, &ts);
    cw_r2d_text(V.serif, b, cw_v2(px.x * 0.2f - m.width * 0.5f, px.y * 0.105f - m.height * 0.5f), &ts);
  }
  /* the bonus stone in a plain gold setting: the stone owning the card adds for good, nothing else beside it */
  cw_vec2 gc = cw_v2(px.x * 0.7f, px.y * 0.4f);
  float gr = px.x * 0.235f;
  cw_r2d_circle_lines(gc, gr * 0.98f, gr * 0.05f, 48, cw_rgba(0.62f, 0.45f, 0.18f, 1));
  printed_gem(cd->bonus, gc, gr * 1.75f);
  cost_rows(cd->cost, px, px.x * 0.17f, px.y * 0.865f, px.y * 0.152f, px.x * 0.27f, false);
}

/* ── building the table ────────────────────────────────────────────────────────────────────────── */

static cw_node box(cw_node parent, const char *name, t3_vec3 pos, t3_vec3 size, cw_color col, float rough, float metal, float sheen) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  cw_node_set(n, "primitive", cw_val_enum("BOX"));
  cw_node_set(n, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(n, "color", cw_val_color(col.r, col.g, col.b, 1));
  cw_node_set(n, "roughness", cw_val_float(rough));
  cw_node_set(n, "metalness", cw_val_float(metal));
  if (sheen > 0) {
    cw_node_set(n, "sheen", cw_val_float(sheen));
    cw_node_set(n, "sheen_color", cw_val_color(fminf(1, col.r * 2.2f), fminf(1, col.g * 2.2f), fminf(1, col.b * 2.2f), 1));
    cw_node_set(n, "sheen_roughness", cw_val_float(0.45f));
  }
  cw_node_set(n, "cast_shadow", cw_val_bool(true));
  cw_node_set(n, "receive_shadow", cw_val_bool(true));
  cw_node3d_set_position(n, pos);
  return n;
}

/* a cut's polyhedron as a flat-shaded mesh: each facet plane's corners, fanned */
static t3_geometry *cut_geometry(int cut, float scale) {
  const jw_cut *c = &jw_cuts[cut];
  float *pos = malloc(sizeof(float) * 3 * 3 * 64 * JW_MAX_PLANES), *nor = malloc(sizeof(float) * 3 * 3 * 64 * JW_MAX_PLANES);
  int nv = 0;
  for (int p = 0; p < c->nplanes; p++) {
    const jw_plane *pl = &c->p[p];
    int idx[64], n = 0;
    float cx = 0, cy = 0, cz = 0;
    for (int v = 0; v < c->nverts && n < 64; v++) {
      const float *q = c->verts[v];
      if (fabsf(q[0] * pl->n[0] + q[1] * pl->n[1] + q[2] * pl->n[2] - pl->d) < 1e-3f) {
        idx[n++] = v;
        cx += q[0], cy += q[1], cz += q[2];
      }
    }
    if (n < 3) continue;
    cx /= n, cy /= n, cz /= n;
    /* order the corners round the facet */
    float ux, uy, uz;
    { const float *q = c->verts[idx[0]]; ux = q[0] - cx, uy = q[1] - cy, uz = q[2] - cz; }
    float vx = pl->n[1] * uz - pl->n[2] * uy, vy = pl->n[2] * ux - pl->n[0] * uz, vz = pl->n[0] * uy - pl->n[1] * ux;
    float ang[64];
    for (int i = 0; i < n; i++) {
      const float *q = c->verts[idx[i]];
      float dx = q[0] - cx, dy = q[1] - cy, dz = q[2] - cz;
      ang[i] = atan2f(dx * vx + dy * vy + dz * vz, dx * ux + dy * uy + dz * uz);
    }
    for (int i = 1; i < n; i++)
      for (int j = i; j > 0 && ang[j - 1] > ang[j]; j--) {
        float ta = ang[j]; ang[j] = ang[j - 1]; ang[j - 1] = ta;
        int ti = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = ti;
      }
    for (int i = 0; i < n; i++) {
      const float *a = c->verts[idx[i]], *b = c->verts[idx[(i + 1) % n]];
      float tri[3][3] = {{cx, cy, cz}, {a[0], a[1], a[2]}, {b[0], b[1], b[2]}};
      /* local: girdle in xy, crown +z. Table space: crown up (+y), girdle in xz */
      for (int k = 0; k < 3; k++) {
        pos[nv * 3] = tri[k][0] * scale, pos[nv * 3 + 1] = tri[k][2] * scale, pos[nv * 3 + 2] = -tri[k][1] * scale;
        nor[nv * 3] = pl->n[0], nor[nv * 3 + 1] = pl->n[2], nor[nv * 3 + 2] = -pl->n[1];
        nv++;
      }
    }
  }
  /* winding: make every triangle face along its normal */
  for (int t = 0; t < nv; t += 3) {
    float *p0 = &pos[t * 3], *p1 = &pos[t * 3 + 3], *p2 = &pos[t * 3 + 6];
    float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    float cr[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    float *nn = &nor[t * 3];
    if (cr[0] * nn[0] + cr[1] * nn[1] + cr[2] * nn[2] < 0)
      for (int k = 0; k < 3; k++) { float s = p1[k]; p1[k] = p2[k]; p2[k] = s; }
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nor, nv, 3));
  free(pos), free(nor);
  return g;
}

static t3_geometry *G_CUT[GM_NTOK];
static t3_material *M_GEM[GM_NTOK], *M_CHIP[GM_NTOK];

static cw_node make_token(int i, int colour) {
  char nm[24];
  snprintf(nm, sizeof nm, "Stone%d", i);
  cw_node n = cw_node_new("Node3D", nm);
  cw_node_add_child(V.world, n);
  /* the setting: a low brass-rimmed disc of velvet the stone sits in */
  cw_node chip = cw_node_new("MeshInstance3D", "Setting");
  cw_node_add_child(n, chip);
  t3_mesh *cm = t3_mesh_new(t3_cylinder_geometry_new(0.25f, 0.27f, 0.07f, 28, 1, false), M_CHIP[colour]);
  cm->base.cast_shadow = cm->base.receive_shadow = true;
  cw_mesh3d_set_procedural(chip, cm);
  t3_release(cm);
  cw_node gem = cw_node_new("MeshInstance3D", "Gem");
  cw_node_add_child(n, gem);
  t3_mesh *gm = t3_mesh_new(G_CUT[colour], M_GEM[colour]);
  gm->base.cast_shadow = true;
  cw_mesh3d_set_procedural(gem, gm);
  t3_release(gm);
  float zmin = -jw_cuts[CUT_OF[colour]].zmin;
  cw_node3d_set_position(gem, (t3_vec3){0, 0.035f + zmin * 0.235f * 0.8f, 0});
  cw_node3d_set_rotation(gem, (t3_vec3){0, 0.35f * (float)i, 0});
  return n;
}

static void build_table(void) {
  cw_node w = V.world;
  /* the table: walnut, a deep green baize on it, a velvet tray for the loose stones */
  box(w, "Table", (t3_vec3){0.6f, -0.32f, 0.3f}, (t3_vec3){19.5f, 0.6f, 11.6f}, cw_rgba(0.16f, 0.08f, 0.04f, 1), 0.45f, 0, 0);
  box(w, "Baize", (t3_vec3){0.6f, -0.012f, 0.3f}, (t3_vec3){18.6f, 0.02f, 10.8f}, cw_rgba(0.035f, 0.11f, 0.08f, 1), 0.95f, 0, 0.6f);
  box(w, "TrayRim", (t3_vec3){-6.55f, -0.0f, 0.0f}, (t3_vec3){1.5f, 0.06f, 7.6f}, cw_rgba(0.55f, 0.38f, 0.14f, 1), 0.35f, 0.9f, 0);
  box(w, "Tray", (t3_vec3){-6.55f, 0.012f, 0.0f}, (t3_vec3){1.36f, 0.06f, 7.45f}, cw_rgba(0.04f, 0.02f, 0.06f, 1), 0.9f, 0, 0.9f);
  /* the market's inlay: a thin brass line round it */
  box(w, "MarketRim", (t3_vec3){-1.4f, -0.006f, 0.45f}, (t3_vec3){7.4f, 0.01f, 6.4f}, cw_rgba(0.5f, 0.36f, 0.14f, 1), 0.4f, 0.85f, 0);
  box(w, "Market", (t3_vec3){-1.4f, -0.002f, 0.45f}, (t3_vec3){7.25f, 0.01f, 6.25f}, cw_rgba(0.03f, 0.085f, 0.065f, 1), 0.95f, 0, 0.6f);
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    char nm[16];
    snprintf(nm, sizeof nm, "MatTrim%d", s);
    V.mat_trim[s] = box(w, nm, (t3_vec3){0, -0.004f, 0}, (t3_vec3){GM_MAT_W + 0.12f, 0.02f, GM_MAT_H + 0.12f}, bk_seat_color(s), 0.5f, 0.2f, 0);
    snprintf(nm, sizeof nm, "Mat%d", s);
    V.mats[s] = box(w, nm, (t3_vec3){0, 0.004f, 0}, (t3_vec3){GM_MAT_W, 0.02f, GM_MAT_H}, cw_rgba(0.13f, 0.07f, 0.04f, 1), 0.55f, 0, 0);
  }
  for (int i = 0; i < 2; i++) {
    V.plaque[i] = box(w, i ? "PlaqueAlt" : "PlaqueOk", (t3_vec3){0, 0.04f, 0}, (t3_vec3){2.1f, 0.08f, 0.6f},
                      i ? cw_rgba(0.45f, 0.48f, 0.52f, 1) : cw_rgba(0.75f, 0.55f, 0.2f, 1), 0.3f, 0.95f, 0);
  }
  /* the cursor: a ring of soft light on the baize */
  V.ring = cw_node_new("MeshInstance3D", "Cursor");
  cw_node_add_child(w, V.ring);
  {
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    m->emissive = (t3_color){1, 0.85f, 0.5f};
    m->emissive_intensity = 2.2f;
    m->color = (t3_color){0, 0, 0};
    t3_mesh *rm = t3_mesh_new(t3_ring_geometry_new(0.92f, 1.0f, 48, 1), m);
    t3_release(m);
    cw_mesh3d_set_procedural(V.ring, rm);
    cw_node3d_set_rotation(V.ring, (t3_vec3){-PI / 2, 0, 0});
    t3_release(rm);
  }
  /* stones' materials and cuts */
  jw_cuts_build();
  for (int c = 0; c < GM_NTOK; c++) {
    G_CUT[c] = cut_geometry(CUT_OF[c], 0.235f);
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->color = (t3_color){BODY[c][0], BODY[c][1], BODY[c][2]};
    m->roughness = 0.04f;
    m->metalness = 0.0f;
    m->clearcoat = 1.0f;
    m->clearcoat_roughness = 0.02f;
    m->ior = c == 4 ? 2.4f : 1.77f;
    m->specular_intensity = 1.0f;
    m->flat_shading = true;
    m->emissive = (t3_color){BODY[c][0] * 0.12f, BODY[c][1] * 0.12f, BODY[c][2] * 0.12f};
    M_GEM[c] = m;
    t3_material *ch = t3_mesh_physical_material_new(0xffffff);
    ch->color = (t3_color){0.05f + BODY[c][0] * 0.12f, 0.04f + BODY[c][1] * 0.12f, 0.05f + BODY[c][2] * 0.12f};
    ch->roughness = 0.85f;
    ch->sheen = 1.0f;
    ch->sheen_color = (t3_color){0.3f + BODY[c][0] * 0.5f, 0.3f + BODY[c][1] * 0.5f, 0.3f + BODY[c][2] * 0.5f};
    ch->sheen_roughness = 0.5f;
    M_CHIP[c] = ch;
  }
  int i = 0;
  for (int c = 0; c < GM_NTOK; c++)
    for (int k = 0; k < (c == GM_WILD ? 5 : 7); k++, i++) {
      V.tok[i].colour = c;
      V.tok[i].node = make_token(i, c);
      V.tok[i].holder = -1;
    }
  /* cards: every commission its own card, every patron its own tile; deck piles */
  for (int k = 0; k < GM_NCARDS; k++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Card%d", k);
    V.cards[k] = bk_card_new(w, nm, &(bk_card_desc){.width = GM_CARD_W, .height = GM_CARD_H, .thickness = 0.02f, .corner = 0.08f,
                                                    .front = k, .back = FACE_BACK_TIER + gm_card_set[k].tier, .face_px = 320});
    V.card_rot[k] = (t3_quat){0, 0, 0, 1};
    V.card_scale[k] = 1;
  }
  /* the glow under a card the person to move can fulfil: one soft rounded halo (a texture made here), warm, added
   * to the baize under the card, shared by every card */
  {
    enum { HW = 96, HH = 128 };
    static uint8_t px[HW * HH * 4];
    for (int y = 0; y < HH; y++)
      for (int x = 0; x < HW; x++) {
        /* distance outside the card's rounded rectangle (the inner 74% x 80%), falling off softly: the halo stays
         * within its own card's gap, so two lit neighbours read as two */
        float dx = fmaxf(0, fabsf((float)x + 0.5f - HW * 0.5f) - HW * 0.37f), dy = fmaxf(0, fabsf((float)y + 0.5f - HH * 0.5f) - HH * 0.4f);
        float d = sqrtf(dx * dx + dy * dy) / (HW * 0.13f), a = d >= 1 ? 0 : (1 - d) * (1 - d) * (1 - d * 0.5f);
        uint8_t *o = &px[(y * HW + x) * 4];
        o[0] = o[1] = o[2] = 255, o[3] = (uint8_t)lroundf(255 * a);
      }
    t3_texture *tx = t3_data_texture_new(HW, HH, px);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = (t3_color){1.0f, 0.72f, 0.3f};
    t3_material_set_texture(m, T3_MAP, tx);
    m->transparent = true;
    m->depth_write = false;
    m->blending = T3_ADDITIVE_BLENDING;
    t3_release(tx);
    t3_geometry *hg = t3_plane_geometry_new(GM_CARD_W * 1.34f, GM_CARD_H * 1.25f, 1, 1);
    for (int k = 0; k < GM_NCARDS; k++) { /* a mesh each (a node owns its mesh), one geometry and material for all */
      t3_mesh *hm = t3_mesh_new(hg, m);
      hm->base.cast_shadow = false;
      hm->base.receive_shadow = false;
      char nm[16];
      snprintf(nm, sizeof nm, "Halo%d", k);
      V.halo[k] = cw_node_new("MeshInstance3D", nm);
      cw_node_add_child(w, V.halo[k]);
      if (!cw_mesh3d_set_procedural(V.halo[k], hm)) CW_FATAL("facet: the halo mesh was refused");
      cw_node3d_set_rotation(V.halo[k], (t3_vec3){-PI / 2, 0, 0});
      cw_node_set(V.halo[k], "visible", cw_val_bool(false));
      t3_release(hm);
    }
    t3_release(hg);
    t3_release(m);
  }
  for (int k = 0; k < GM_NPATRONS; k++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Patron%d", k);
    V.patrons[k] = bk_card_new(w, nm, &(bk_card_desc){.width = 1.15f, .height = 1.15f, .thickness = 0.03f, .corner = 0.1f,
                                                      .front = FACE_PATRON + k, .back = -1, .face_px = 256});
    V.patron_scale[k] = 1;
  }
  for (int t = 0; t < GM_TIERS; t++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Deck%d", t);
    t3_vec3 p = gm_card_pos(t, -1);
    V.decks[t] = box(w, nm, p, (t3_vec3){GM_CARD_W - 0.04f, 1, GM_CARD_H - 0.04f}, cw_rgba(0.8f, 0.76f, 0.68f, 1), 0.8f, 0, 0);
    snprintf(nm, sizeof nm, "DeckTop%d", t);
    V.deck_top[t] = bk_card_new(w, nm, &(bk_card_desc){.width = GM_CARD_W, .height = GM_CARD_H, .thickness = 0.02f, .corner = 0.08f,
                                                       .front = FACE_BACK_TIER + t, .back = -1, .face_px = 320});
  }
  V.built = true;
}

/* ── syncing the table to the rules ────────────────────────────────────────────────────────────── */

static void set_visible(cw_node n, bool on) { cw_node_set(n, "visible", cw_val_bool(on)); }

static t3_vec3 approach(t3_vec3 a, t3_vec3 b, float k) { return (t3_vec3){a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k}; }
static float dist3(t3_vec3 a, t3_vec3 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

/* where a colour's column counts its card bonuses (a bought card lands there and goes) */
static t3_vec3 bonus_well(int players, int seat, int colour) {
  t3_vec3 m = gm_mat_pos(players, seat);
  return (t3_vec3){gm_hand_pos(players, seat, colour).x, 0.0f, m.z + 0.05f};
}

/* which tokens sit where: keep each token's holder; move only as many as the counts changed */
static void assign_tokens(const gm_state *st) {
  for (int c = 0; c < GM_NTOK; c++) {
    int want[1 + BK_MAX_SEATS] = {0}, have[1 + BK_MAX_SEATS] = {0};
    want[0] = st->bank[c];
    for (int p = 0; p < st->players; p++) want[1 + p] = st->s[p].tok[c];
    int total = 0;
    for (int h = 0; h <= st->players; h++) total += want[h];
    int ids[8], n = 0;
    for (int i = 0; i < NTOKENS; i++)
      if (V.tok[i].colour == c) ids[n++] = i;
    /* tokens beyond the game's supply stay hidden */
    for (int k = 0; k < n; k++) {
      token *t = &V.tok[ids[k]];
      if (k >= total) { t->holder = -2; continue; }
      if (t->holder == -2) t->holder = -1;
      if (t->holder >= 0 && t->holder <= st->players) {
        if (have[t->holder] < want[t->holder]) have[t->holder]++;
        else t->holder = -1; /* released */
      }
    }
    for (int k = 0; k < n && k < total; k++) {
      token *t = &V.tok[ids[k]];
      if (t->holder >= 0) continue;
      for (int h = 0; h <= st->players; h++)
        if (have[h] < want[h]) { t->holder = h; have[h]++; t->landed = false; break; }
    }
    /* stack slots in id order per holder */
    int slot[1 + BK_MAX_SEATS] = {0};
    for (int k = 0; k < n; k++) {
      token *t = &V.tok[ids[k]];
      if (t->holder >= 0) t->slot = slot[t->holder]++;
    }
  }
}

static t3_vec3 token_home(const gm_state *st, const token *t) {
  float h = 0.075f * (float)t->slot;
  if (t->holder == 0) { /* in the tray: one stack a colour, one setting a stone, so its height is its count */
    t3_vec3 p = gm_bank_pos(t->colour);
    return (t3_vec3){p.x - 0.27f, 0.05f + h, p.z};
  }
  t3_vec3 p = gm_hand_pos(st->players, t->holder - 1, t->colour);
  return (t3_vec3){p.x, 0.05f + h, p.z};
}

static void sync(float dt) {
  const gm_state *st = gm_game_state(&V.g);
  float k = 1 - expf(-dt * 7.0f);
  /* mats */
  for (int s = 0; s < BK_MAX_SEATS; s++) {
    bool on = s < st->players;
    set_visible(V.mats[s], on);
    set_visible(V.mat_trim[s], on);
    if (!on) continue;
    t3_vec3 m = gm_mat_pos(st->players, s);
    cw_node3d_set_position(V.mats[s], (t3_vec3){m.x, 0.004f, m.z});
    cw_node3d_set_scale(V.mats[s], (t3_vec3){1, 1, gm_mat_h(st->players) / GM_MAT_H});
    cw_node3d_set_scale(V.mat_trim[s], (t3_vec3){1, 1, (gm_mat_h(st->players) + 0.12f) / (GM_MAT_H + 0.12f)});
    bool turn = s == st->seat && !gm_game_over(&V.g);
    cw_node3d_set_position(V.mat_trim[s], (t3_vec3){m.x, -0.004f, m.z});
    cw_color sc = bk_seat_color(s);
    float glow = turn ? 0.55f + 0.15f * sinf(V.t * 2.0f) : 0.0f; /* a slow breath, never a flash */
    cw_node_set(V.mat_trim[s], "emissive", cw_val_color(sc.r * glow, sc.g * glow, sc.b * glow, 1));
  }
  /* cards */
  int in_well[BK_MAX_SEATS][GM_NCOLORS] = {{0}};
  for (int c = 0; c < GM_NCARDS; c++) {
    int w = st->where[c];
    t3_vec3 to = V.card_pos[c];
    float sc = 1;
    bool show = true;
    if (w == GM_AT_DECK) {
      to = gm_card_pos(gm_card_set[c].tier, -1);
      to.y = 0.02f;
      show = false;
    } else if (w == GM_AT_MARKET) {
      for (int t = 0; t < GM_TIERS; t++)
        for (int s = 0; s < GM_SLOTS; s++)
          if (st->market[t][s] == c) to = gm_card_pos(t, s);
      to.y = 0.02f;
    } else if (w == GM_AT_RESERVED) {
      int o = st->owner[c], r = 0;
      for (int i = 0; i < st->s[o].nres; i++)
        if (st->s[o].res[i] == c) r = i;
      to = gm_res_pos(st->players, o, r);
      to.y = 0.03f + 0.01f * (float)r;
      sc = 0.42f;
    } else if (w == GM_AT_BOUGHT) {
      int o = st->owner[c], col = gm_card_set[c].bonus;
      to = bonus_well(st->players, o, col);
      to.y = 0.03f + 0.012f * (float)in_well[o][col]++;
      sc = 0.32f;
    }
    if (!V.card_shown[c] && show) { /* a card dealt: it rises from its deck */
      V.card_pos[c] = gm_card_pos(gm_card_set[c].tier, -1);
      V.card_pos[c].y = 0.3f;
      V.card_scale[c] = 1;
    }
    V.card_shown[c] = show;
    /* a fulfilled card lands in its column and goes: the column's +n is what stays */
    if (w == GM_AT_BOUGHT && show && dist3(V.card_pos[c], to) < 0.03f) show = false;
    set_visible(V.cards[c], show);
    if (!show) continue;
    {
      int tg = V.g.cursor_target, ui = V.g.ui.mode == UI_CARD ? V.g.ui.card : -1;
      bool under = false;
      for (int k = 0; k < 2; k++) {
        int t = k ? ui : tg;
        if (t >= TG_CARD && t < TG_DECK && st->market[(t - TG_CARD) / GM_SLOTS][(t - TG_CARD) % GM_SLOTS] == c) under = true;
        if (t >= TG_RES && t < TG_HAND && t - TG_RES < st->s[st->seat].nres && st->s[st->seat].res[t - TG_RES] == c) under = true;
      }
      if (under && V.flow == FLOW_PLAY) {
        to.y += 0.35f;
        sc *= w == GM_AT_RESERVED ? 1.5f : 1.08f;
        if (w == GM_AT_RESERVED) to.z -= 0.4f;
      }
    }
    float d = dist3(V.card_pos[c], to);
    t3_vec3 p = approach(V.card_pos[c], to, k);
    V.card_pos[c] = p;
    V.card_scale[c] += (sc - V.card_scale[c]) * k;
    /* drawn on a high arc while it travels (the path itself stays on the ground line): above the table and the other
     * mats, coming down on its own place */
    V.card_lift[c] += (fminf(d, 4.0f) * 0.3f - V.card_lift[c]) * (1 - expf(-dt * 12.0f)); /* eased: it rises, never pops */
    cw_node3d_set_position(V.cards[c], (t3_vec3){p.x, p.y + V.card_lift[c], p.z});
    cw_node3d_set_scale(V.cards[c], (t3_vec3){V.card_scale[c], 1, V.card_scale[c]});
  }
  /* the glow: under each card the person to move can fulfil now, easing in over about half a second */
  {
    bool want[GM_NCARDS] = {false};
    for (int t = TG_CARD; t < TG_HAND; t++) {
      int card = card_at(st, t);
      if (card >= 0 && glow_wanted(st, t)) want[card] = true;
    }
    float kg = 1 - expf(-dt * 5.0f);
    for (int c = 0; c < GM_NCARDS; c++) {
      if (!want[c] || !V.card_shown[c]) V.halo_k[c] += (0 - V.halo_k[c]) * kg;
      else V.halo_k[c] += (1 - V.halo_k[c]) * kg;
      if (V.halo_k[c] < 0.01f) V.halo_k[c] = 0;
      set_visible(V.halo[c], V.halo_k[c] > 0);
      if (V.halo_k[c] <= 0) continue;
      t3_vec3 p = V.card_pos[c];
      cw_node3d_set_position(V.halo[c], (t3_vec3){p.x, 0.006f, p.z});
      float sc = V.card_scale[c];
      cw_node3d_set_scale(V.halo[c], (t3_vec3){sc, sc, 1});
      cw_node_set(V.halo[c], "opacity", cw_val_float(V.halo_k[c]));
    }
  }
  for (int t = 0; t < GM_TIERS; t++) {
    int left = gm_deck_left(st, t);
    float h = 0.012f * (float)left + 0.001f;
    t3_vec3 p = gm_card_pos(t, -1);
    set_visible(V.decks[t], left > 0);
    set_visible(V.deck_top[t], left > 0);
    cw_node3d_set_scale(V.decks[t], (t3_vec3){1, h, 1});
    cw_node3d_set_position(V.decks[t], (t3_vec3){p.x, h * 0.5f, p.z});
    cw_node3d_set_position(V.deck_top[t], (t3_vec3){p.x, h + 0.01f, p.z});
  }
  /* patrons */
  for (int i = 0; i < GM_NPATRONS; i++) {
    bool show = st->patron_open[i] != 0;
    set_visible(V.patrons[i], show);
    if (!show) continue;
    t3_vec3 to;
    float sc = 1.3f; /* on the table: large enough that its asks read at the table camera */
    if (st->patron_open[i] == 1) to = gm_patron_pos(st, i);
    else {
      t3_vec3 m = gm_mat_pos(st->players, st->patron_owner[i]);
      int nth = 0;
      for (int j = 0; j < i; j++) nth += st->patron_open[j] == 2 && st->patron_owner[j] == st->patron_owner[i];
      to = (t3_vec3){m.x + 1.5f + 0.36f * (float)nth, 0, m.z - 0.6f};
      sc = 0.28f;
    }
    to.y = 0.025f;
    if (V.patron_pos[i].x == 0 && V.patron_pos[i].z == 0) V.patron_pos[i] = to;
    V.patron_pos[i] = approach(V.patron_pos[i], to, k);
    V.patron_scale[i] += (sc - V.patron_scale[i]) * k;
    cw_node3d_set_position(V.patrons[i], V.patron_pos[i]);
    cw_node3d_set_scale(V.patrons[i], (t3_vec3){V.patron_scale[i], 1, V.patron_scale[i]});
  }
  /* stones: fly home, then settle with a small bounce (the weight of a stone landing) */
  assign_tokens(st);
  memset(V.bank_stack, 0, sizeof V.bank_stack);
  for (int i = 0; i < NTOKENS; i++)
    if (V.tok[i].holder == 0) V.bank_stack[V.tok[i].colour]++;
  gm_trace_gem traced[GM_TRACE_MAX];
  int ntraced = 0;
  bool trace = gm_trace_ready();
  for (int i = 0; i < NTOKENS; i++) {
    token *t = &V.tok[i];
    set_visible(t->node, t->holder >= 0);
    if (t->holder < 0) continue;
    t3_vec3 home = token_home(st, t);
    if (t->pos.x == 0 && t->pos.z == 0) t->pos = home;
    float d = dist3(t->pos, home);
    t3_vec3 p = approach(t->pos, home, 1 - expf(-dt * 9.0f));
    p.y = home.y + fminf(d, 2.5f) * 0.35f;
    t->pos = p;
    cw_node3d_set_position(t->node, p);
    /* on a mat the stones stack: only the top one shows its stone, the rest show their settings' edges */
    int count = t->holder == 0 ? st->bank[t->colour] : st->s[t->holder - 1].tok[t->colour];
    bool top = t->slot == count - 1;
    bool gem_on = top || d > 0.05f;
    set_visible(cw_node_get(t->node, "Gem"), gem_on && !trace);
    if (gem_on && trace && ntraced < GM_TRACE_MAX) { /* the stone traced per pixel, sitting in its setting */
      const jw_cut *cut = &jw_cuts[CUT_OF[t->colour]];
      traced[ntraced++] = (gm_trace_gem){.pos = {p.x, p.y + 0.035f - cut->zmin * 0.205f * 0.8f, p.z}, .yaw = 0.35f * (float)i,
                                         .size = 0.205f, .cut = CUT_OF[t->colour], .opacity = 1};
    }
  }
  /* far stones first, so a nearer one draws over a farther one */
  for (int a = 1; a < ntraced; a++)
    for (int b = a; b > 0 && traced[b - 1].pos.z > traced[b].pos.z; b--) {
      gm_trace_gem tmp = traced[b];
      traced[b] = traced[b - 1];
      traced[b - 1] = tmp;
    }
  gm_trace_set(traced, ntraced);
  /* the cursor ring and the plaques */
  int tgt = V.g.cursor_target;
  bool human = !gm_game_over(&V.g) && !V.g.cpu[st->seat];
  bool cpu_entering = !gm_game_over(&V.g) && V.g.cpu[st->seat] && gm_game_cpu_intent(&V.g) != BK_MOVE_NONE;
  float want = (tgt >= 0 && (human || cpu_entering) && V.flow == FLOW_PLAY) ? 1 : 0;
  V.ring_glow += (want - V.ring_glow) * (1 - expf(-dt * 8));
  if (tgt >= 0) {
    t3_vec3 at = gm_target_pos(&V.g, tgt);
    at.y = 0.03f;
    V.ring_pos = V.ring_glow < 0.02f ? at : approach(V.ring_pos, at, 1 - expf(-dt * 16));
  }
  float rs = 0.38f;
  if (tgt >= TG_CARD && tgt < TG_RES) rs = 1.12f;
  else if (tgt >= TG_RES && tgt < TG_HAND) rs = 0.55f;
  else if (tgt >= TG_HAND && tgt < TG_PATRON) rs = 0.3f;
  else if (tgt >= TG_PATRON && tgt < TG_OK) rs = 0.85f;
  else if (tgt >= TG_OK) rs = 1.15f;
  rs *= 0.6f + 0.4f * V.ring_glow;
  bool card_target = (tgt >= TG_CARD && tgt < TG_DECK) || (tgt >= TG_RES && tgt < TG_HAND) || tgt >= TG_OK;
  const char *nohud = cw_test_env("FACET_NO_HUD"); /* the art's key frames: no cursor either */
  set_visible(V.ring, V.ring_glow > 0.02f && !card_target && !(nohud && *nohud));
  V.ring_pos.y = tgt >= TG_DECK && tgt < TG_RES ? 0.5f : 0.06f;
  cw_node3d_set_position(V.ring, V.ring_pos);
  cw_node3d_set_scale(V.ring, (t3_vec3){rs, rs, 1}); /* the ring lies in its local xy plane, turned flat */
  for (int i = 0; i < 2; i++) {
    bool on = V.flow == FLOW_PLAY && (human || cpu_entering) && gm_target_active(&V.g, i ? TG_ALT : TG_OK);
    if (on) V.plaque_k[i] += (1.0f - V.plaque_k[i]) * (1 - expf(-dt * 10.0f));
    else V.plaque_k[i] = 0; /* never left lying on the table while nobody can use it */
    t3_vec3 p = gm_target_pos(&V.g, i ? TG_ALT : TG_OK);
    set_visible(V.plaque[i], V.plaque_k[i] > 0.12f);
    cw_node3d_set_position(V.plaque[i], (t3_vec3){p.x, 0.04f * V.plaque_k[i], p.z});
    cw_node3d_set_scale(V.plaque[i], (t3_vec3){0.7f + 0.3f * V.plaque_k[i], V.plaque_k[i], 0.7f + 0.3f * V.plaque_k[i]});
    /* the plaque under the cursor glows (eased with the cursor's light, never a flash) */
    float lit = (tgt == (i ? TG_ALT : TG_OK)) ? V.ring_glow * 0.55f : 0.0f;
    cw_node_set(V.plaque[i], "emissive", cw_val_color(0.9f * lit, 0.7f * lit, 0.35f * lit, 1));
  }
}

/* ── the HUD ───────────────────────────────────────────────────────────────────────────────────── */

static cw_vec2 screen_of(t3_vec3 p) {
  cw_rect r = cw_rect_make(0, 0, V.canvas.x, V.canvas.y); /* the HUD's canvas (logical units), not the drawable */
  float depth;
  return cw_camera3d_world_to_screen(V.cam, r, p, &depth);
}

static const char *seat_name(int s) {
  static char b[BK_MAX_SEATS][32];
  if (V.seats[s].kind == BK_SEAT_CPU) snprintf(b[s], sizeof b[s], "Guild %d (%s)", s + 1, cw_shell_cpu_level_label((cw_shell_cpu_level)V.seats[s].level));
  else snprintf(b[s], sizeof b[s], "Player %d", s + 1);
  return b[s];
}

static void panel(cw_rect r, float a) {
  cw_r2d_round_rect(r, r.h * 0.12f, 8, cw_rgba(0.06f, 0.04f, 0.03f, 0.82f * a));
  cw_r2d_round_rect_lines(r, r.h * 0.12f, 2, 8, cw_rgba(0.75f, 0.58f, 0.26f, 0.8f * a));
}

static void hud_mats(const gm_state *st, float u) {
  for (int s = 0; s < st->players; s++) {
    t3_vec3 m = gm_mat_pos(st->players, s);
    float mh = gm_mat_h(st->players);
    cw_vec2 tl = screen_of((t3_vec3){m.x - GM_MAT_W * 0.5f, 0, m.z - mh * 0.5f});
    cw_color sc = bk_seat_color(s);
    bool turn = s == st->seat && !gm_game_over(&V.g);
    char b[64];
    float y = tl.y - 30 * u;
    y -= 4 * u;
    cw_r2d_round_rect(cw_rect_make(tl.x, y, 330 * u, 30 * u), 6 * u, 6, cw_rgba(0.05f, 0.03f, 0.02f, turn ? 0.9f : 0.6f));
    cw_r2d_rect(cw_rect_make(tl.x, y, 6 * u, 30 * u), sc);
    text_c(V.sans, seat_name(s), tl.x + 14 * u, y + 2 * u, 23 * u, turn ? cw_rgba(1, 0.92f, 0.7f, 1) : cw_rgba(0.85f, 0.8f, 0.72f, 1), CW_ALIGN_LEFT);
    snprintf(b, sizeof b, "%d", bk_counter_value(&V.points[s]));
    text_c(V.serif, b, tl.x + 286 * u, y - 1 * u, 28 * u, cw_rgba(1, 0.85f, 0.45f, 1), CW_ALIGN_RIGHT);
    text_c(V.sans, "pts", tl.x + 290 * u, y + 4 * u, 22 * u, cw_rgba(0.8f, 0.7f, 0.5f, 1), CW_ALIGN_LEFT);
    /* five colour columns and the topaz: the stack of loose stones on the mat (an empty colour shows its stone dimmed),
     * then the stones (right under their stack), the cards' bonus, and what the two buy together, one line each; the rows are named once, on
     * the mat's left edge */
    t3_vec3 m3 = gm_mat_pos(st->players, s);
    /* the three rows share the screen space between the stacks and the mat's bottom edge evenly, so a far mat (smaller
     * on screen) keeps its lines apart */
    float y_top = screen_of((t3_vec3){m3.x, 0, m3.z - 0.45f}).y + 24 * u, y_bot = screen_of((t3_vec3){m3.x, 0, m3.z + mh * 0.5f - 0.04f}).y;
    float yr[3];
    for (int r = 0; r < 3; r++) yr[r] = y_top + ((float)r + 0.5f) * (y_bot - y_top) / 3.0f;
    /* the rows, top to bottom: the stones (right under their stacks), the cards' bonus, the buys. What row k counts:
     * 0 the cards, 1 the stones, 2 the buys. FACET_TEST_ROWS_OLD (the play test's control) puts the cards first. */
    static int old_rows = -1;
    if (old_rows < 0) old_rows = cw_test_env("FACET_TEST_ROWS_OLD") != NULL;
    const int row_of[3] = {old_rows ? 0 : 1, old_rows ? 1 : 0, 2};
    static const char *const kind_name[3] = {"cards", "stones", "buys"};
    if (s == 0)
      for (int row = 0; row < 3; row++) D.mat_row_y[row_of[row] == 1 ? 0 : row_of[row] == 0 ? 1 : 2] = yr[row];
    float left_x = screen_of((t3_vec3){m3.x - GM_MAT_W * 0.5f, 0, m3.z + mh * 0.3f}).x;
    for (int r = 0; r < 3; r++) text_c(V.sans, kind_name[row_of[r]], left_x - 8 * u, yr[r] - 13 * u, 22 * u, cw_rgba(0.85f, 0.8f, 0.7f, 0.9f), CW_ALIGN_RIGHT);
    for (int c = 0; c < GM_NTOK; c++) {
      int bonus = c < GM_NCOLORS ? bk_counter_value(&V.bonus[s][c]) : 0, tok = bk_counter_value(&V.toks[s][c]);
      bool empty = bonus == 0 && tok == 0;
      float x = gm_hand_pos(st->players, s, c).x;
      if (tok == 0) { /* no stack: the colour's stone, dimmed */
        cw_vec2 ps = screen_of((t3_vec3){x, 0.05f, m3.z - 0.45f});
        cw_r2d_circle(ps, 15 * u, 20, cw_rgba(0.05f, 0.03f, 0.02f, 0.35f));
        printed_gem(c, ps, 24 * u);
        cw_r2d_circle(ps, 16 * u, 20, cw_rgba(0.13f, 0.07f, 0.04f, 0.55f));
      }
      cw_color dim = cw_rgba(0.62f, 0.58f, 0.52f, 0.75f);
      for (int row = 0; row < 3; row++) {
        int r = row_of[row];
        if (c == GM_WILD && r != 1) continue; /* topaz: no cards, and it buys any colour */
        cw_vec2 p = cw_v2(screen_of((t3_vec3){x, 0, m3.z + mh * 0.25f}).x, yr[row]);
        int v = r == 0 ? bonus : r == 1 ? tok : bonus + tok;
        snprintf(b, sizeof b, r == 0 ? "+%d" : r == 2 ? "= %d" : "%d", v);
        cw_color col = r == 0 ? cw_rgba(1, 0.84f, 0.4f, 1) : r == 1 ? cw_rgba(1, 1, 1, 1) : cw_rgba(0.72f, 1, 0.78f, 1);
        float gap = (y_bot - y_top) / 3.0f; /* never more than the row gap allows: lines stay apart on a far mat */
        float size = fmaxf(22 * u, fminf(r == 2 ? 28 * u : 25 * u, gap * (r == 2 ? 1.0f : 0.88f)));
        cw_text_style ts = {.size = size, .color = v || (r == 2 && !empty) ? col : dim, .own_backdrop = true,
                            .outline_width = 2 * u, .outline_color = cw_rgba(0.06f, 0.03f, 0.01f, 0.9f)};
        cw_text_metrics mt = cw_font_measure(r == 2 ? V.serif : V.sans, b, &ts);
        cw_r2d_text(r == 2 ? V.serif : V.sans, b, cw_v2(p.x - mt.width * 0.5f, p.y - mt.height * 0.5f), &ts);
      }
    }
    int hand = gm_hand(&st->s[s]);
    snprintf(b, sizeof b, "%d/10 stones", hand);
    text_c(V.sans, b, tl.x + 340 * u, y + 3 * u, 22 * u, hand >= 9 ? cw_rgba(1, 0.6f, 0.4f, 1) : cw_rgba(0.75f, 0.7f, 0.6f, 1), CW_ALIGN_LEFT);
  }
}

static const char *move_words(const gm_state *st, bk_move m, char *b, size_t cap) {
  gm_label(st, m, b, cap, NULL);
  if (b[0] >= 'a' && b[0] <= 'z') b[0] = (char)(b[0] - 'a' + 'A');
  return b;
}

static void hud_info(const gm_state *st, float u, cw_vec2 cs) {
  /* what the cursor stands on: a card's name, cost and pay */
  int tgt = V.g.cursor_target;
  int card = -1;
  if (tgt >= TG_CARD && tgt < TG_DECK) card = st->market[(tgt - TG_CARD) / GM_SLOTS][(tgt - TG_CARD) % GM_SLOTS];
  if (tgt >= TG_RES && tgt < TG_HAND) card = st->s[st->seat].res[tgt - TG_RES];
  if (V.g.ui.mode == UI_CARD) {
    int t = V.g.ui.card;
    if (t >= TG_CARD && t < TG_DECK) card = st->market[(t - TG_CARD) / GM_SLOTS][(t - TG_CARD) % GM_SLOTS];
    if (t >= TG_RES && t < TG_HAND) card = st->s[st->seat].res[t - TG_RES];
  }
  char b[160];
  if (card >= 0 && card != GM_NONE) {
    const gm_card *cd = &gm_card_set[card];
    uint8_t pay[GM_NTOK];
    bool can = gm_price(&st->s[st->seat], card, pay);
    cw_rect r = cw_rect_make(cs.x * 0.5f - 430 * u, 12 * u, 860 * u, can ? 68 * u : 88 * u);
    panel(r, 1);
    snprintf(b, sizeof b, "%s  -  %d point%s, and +1 %s for good", cd->name, cd->points, cd->points == 1 ? "" : "s", STONE[cd->bonus]);
    text_c(V.serif, b, cs.x * 0.5f, r.y + 6 * u, 22 * u, cw_rgba(1, 0.92f, 0.75f, 1), CW_ALIGN_CENTER);
    /* the cost, what the seat's cards cover, and the stones that would be spent (or are missing) */
    const gm_seat *me = &st->s[st->seat];
    uint8_t cover[GM_NCOLORS], spend[GM_NTOK];
    for (int c = 0; c < GM_NCOLORS; c++) cover[c] = (uint8_t)(cd->cost[c] < me->bonus[c] ? cd->cost[c] : me->bonus[c]);
    memcpy(spend, pay, sizeof spend);
    float x = r.x + 24 * u, y = r.y + 46 * u, g = 26 * u;
    cw_text_style lab = {.size = 22 * u, .color = cw_rgba(0.85f, 0.8f, 0.7f, 1), .own_backdrop = true};
    const char *heads[3] = {"Cost", "Your cards cover", can ? "You spend" : "Short by"};
    for (int row = 0; row < 3; row++) {
      cw_text_metrics hm = cw_r2d_text(V.sans, heads[row], cw_v2(x, y - 11 * u), &lab);
      x += hm.width + 10 * u;
      int any = 0;
      for (int c = 0; c < GM_NTOK; c++) {
        int n = row == 0 ? (c < GM_NCOLORS ? cd->cost[c] : 0) : row == 1 ? (c < GM_NCOLORS ? cover[c] : 0) : 0;
        if (row == 2) {
          if (can) n = spend[c];
          else if (c < GM_NCOLORS) { int miss = cd->cost[c] - me->bonus[c] - me->tok[c]; n = miss > 0 ? miss : 0; }
        }
        if (!n) continue;
        any++;
        printed_gem(c, cw_v2(x + g * 0.5f, y), g);
        snprintf(b, sizeof b, "%d", n);
        cw_text_style ns = {.size = 23 * u, .color = row == 1 ? cw_rgba(0.7f, 1, 0.75f, 1) : row == 2 && !can ? cw_rgba(1, 0.65f, 0.5f, 1) : cw_rgba(1, 0.95f, 0.85f, 1), .own_backdrop = true};
        cw_text_metrics nm = cw_r2d_text(V.sans, b, cw_v2(x + g + 2 * u, y - 12 * u), &ns);
        x += g + nm.width + 10 * u;
      }
      if (!any) {
        cw_text_metrics nm = cw_r2d_text(V.sans, row == 1 ? "none" : "nothing", cw_v2(x, y - 11 * u), &lab);
        x += nm.width;
      }
      x += 26 * u;
    }
    if (!can) text_c(V.sans, "set it aside to keep it for later", cs.x * 0.5f, r.y + r.h - 28 * u, 22 * u, cw_rgba(0.95f, 0.8f, 0.6f, 1), CW_ALIGN_CENTER);
  }
  if (tgt >= TG_PATRON && tgt < TG_OK) {
    const gm_patron *pt = &gm_patron_set[tgt - TG_PATRON];
    cw_rect r = cw_rect_make(cs.x * 0.5f - 300 * u, 12 * u, 600 * u, 36 * u);
    panel(r, 1);
    snprintf(b, sizeof b, "%s joins you: %d points", pt->name, pt->points);
    text_c(V.serif, b, cs.x * 0.5f, r.y + 6 * u, 22 * u, cw_rgba(1, 0.92f, 0.75f, 1), CW_ALIGN_CENTER);
  }
}

static void hud_prompt(const gm_state *st, float u, cw_vec2 cs) {
  /* the line that says what to do now, with the buttons drawn by the engine */
  const char *line = NULL;
  int seat = st->seat;
  char b[200];
  if (V.g.cpu[seat]) {
    bk_move m = gm_game_cpu_intent(&V.g);
    if (m != BK_MOVE_NONE) {
      char w[96];
      snprintf(b, sizeof b, "%s: %s", seat_name(seat), move_words(st, m, w, sizeof w));
    } else snprintf(b, sizeof b, "%s is thinking", seat_name(seat));
    line = b;
  } else {
    switch (V.g.ui.mode) {
    case UI_BROWSE:
      if (V.g.ui.npick == 0) line = "{ui_left|ui_right} choose  {ui_accept} pick a stone from the tray, or a card";
      else if (gm_target_active(&V.g, TG_OK)) line = "{ui_accept} on Take: take these stones  {ui_back} put one back";
      else if (V.g.ui.npick == 1) { /* two of a kind (a pile of four or more) or a different colour */
        int first = V.g.ui.pick[0];
        bool again = gm_target_active(&V.g, TG_BANK + first);
        if (again && V.g.cursor_target == TG_BANK + first) snprintf(b, sizeof b, "{ui_accept} take another %s: two of a kind  {ui_back} put it back", STONE[first]);
        else if (again) snprintf(b, sizeof b, "{ui_accept} pick a different colour, or another %s from its pile  {ui_back} put it back", STONE[first]);
        else snprintf(b, sizeof b, "{ui_accept} pick a different colour  {ui_back} put it back");
        line = b;
      } else line = "{ui_accept} pick a third colour  {ui_back} put one back";
      break;
    case UI_CARD: {
      bool ok = gm_target_active(&V.g, TG_OK), alt = gm_target_active(&V.g, TG_ALT);
      line = ok && alt ? "{ui_left|ui_right} Fulfil or Set aside  {ui_accept} do it  {ui_back} back"
             : ok      ? "{ui_accept} Fulfil it  {ui_back} back"
                       : "{ui_accept} Set it aside for later, with a topaz  {ui_back} back";
      break;
    }
    case UI_HANDBACK: {
      snprintf(b, sizeof b, "More than ten stones: choose %d to hand back from your mat  {ui_back} undo", V.g.ui.excess - V.g.ui.nret);
      line = V.g.ui.nret == V.g.ui.excess ? "{ui_accept} on Hand back to finish your turn  {ui_back} undo" : b;
      break;
    }
    case UI_PATRON: line = "Two patrons would join you: {ui_accept} choose one"; break;
    }
  }
  D.prompt_back = !V.g.cpu[seat] && strstr(line, "{ui_back}") != NULL;
  float y = cs.y - 44 * u;
  cw_vec2 sz = cw_prompt_text_size(V.sans, line, seat, 22 * u);
  panel(cw_rect_make(cs.x * 0.5f - sz.x * 0.5f - 18 * u, y - 8 * u, sz.x + 36 * u, sz.y + 16 * u), 1);
  cw_prompt_text(V.sans, line, V.g.cpu[seat] ? -1 : seat, cw_v2(cs.x * 0.5f, y), 22 * u, cw_rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
  if (V.g.ui.message == GM_MSG_NOTHING_TO_DO)
    text_c(V.sans, "You cannot fulfil that yet, and three cards are already set aside", cs.x * 0.5f, y - 40 * u, 22 * u, cw_rgba(1, 0.75f, 0.55f, 1), CW_ALIGN_CENTER);
  /* plaque words */
  for (int i = 0; i < 2; i++) {
    if (V.plaque_k[i] < 0.3f) continue;
    const char *w = i ? gm_alt_label(&V.g) : gm_ok_label(&V.g);
    if (!*w) continue;
    cw_vec2 p = screen_of(gm_target_pos(&V.g, i ? TG_ALT : TG_OK));
    cw_text_style ts = {.size = 30 * u, .color = cw_rgba(0.1f, 0.06f, 0.02f, V.plaque_k[i]), .own_backdrop = true, .fixed_size = true};
    cw_text_metrics mt = cw_font_measure(V.serif, w, &ts);
    cw_r2d_text(V.serif, w, cw_v2(p.x - mt.width * 0.5f, p.y - mt.height * 0.5f), &ts);
  }
  /* the ghost of the move: a line naming it, over the mat it changes */
  bk_move pv = V.g.cpu[seat] ? gm_game_cpu_intent(&V.g) : gm_game_preview(&V.g);
  if (pv != BK_MOVE_NONE && !V.g.cpu[seat]) {
    char w[96];
    t3_vec3 m = gm_mat_pos(st->players, seat);
    cw_vec2 p = screen_of((t3_vec3){m.x, 0, m.z + gm_mat_h(st->players) * 0.5f});
    snprintf(b, sizeof b, "%s", move_words(st, pv, w, sizeof w));
    cw_text_style ts = {.size = 22 * u, .own_backdrop = true};
    cw_text_metrics mt = cw_font_measure(V.sans, b, &ts);
    panel(cw_rect_make(p.x - mt.width * 0.5f - 10 * u, p.y + 4 * u, mt.width + 20 * u, 32 * u), 0.9f);
    text_c(V.sans, b, p.x, p.y + 7 * u, 22 * u, cw_rgba(0.75f, 0.95f, 1, 1), CW_ALIGN_CENTER);
  }
  /* picked stones float over the tray, see-through, until taken */
  for (int i = 0; i < V.g.ui.npick && V.g.ui.mode == UI_BROWSE; i++) {
    int c = V.g.ui.pick[i];
    gem_icon(gm_pick_icon_at(&V.g, i), 16 * u, c, 0.85f);
  }
  for (int i = 0; i < V.g.ui.nret && V.g.ui.mode == UI_HANDBACK; i++) {
    int c = V.g.ui.ret[i];
    cw_vec2 p = screen_of(gm_hand_pos(st->players, seat, c));
    cw_r2d_circle_lines(p, 20 * u, 3 * u, 24, cw_rgba(1, 0.5f, 0.4f, 0.9f));
  }
}

/* the how-to-play card: over play (page false: the table dimmed, a start prompt) and as the title page (page true:
 * the shell draws the way back) */
static void hud_howto(float u, cw_vec2 cs, bool page) {
  float w = 980 * u, h = 640 * u, x = (cs.x - w) * 0.5f, y = (cs.y - h) * 0.5f;
  if (!page) cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), cw_rgba(0.01f, 0.01f, 0.02f, 0.55f));
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 18 * u, 8, cw_rgba(0.93f, 0.88f, 0.78f, 1));
  cw_r2d_round_rect_lines(cw_rect_make(x + 10 * u, y + 10 * u, w - 20 * u, h - 20 * u), 12 * u, 2.5f * u, 8, cw_rgba(0.5f, 0.36f, 0.16f, 1));
  cw_color ink = cw_rgba(0.15f, 0.1f, 0.06f, 1);
  text_c(V.serif, "How to play Facet", cs.x * 0.5f, y + 28 * u, 44 * u, ink, CW_ALIGN_CENTER);
  static const char *lines[] = {
      "Your guild cuts stones for commissions. First to 15 points ends the game at the end of that round.",
      "On your turn, do one thing:",
      "  -  Take three stones of different colours from the tray, or two of one colour if its pile has four or more.",
      "  -  Fulfil a commission: pay its stones. Every card you own is a bonus stone that pays for later cards.",
      "  -  Set a card aside for later (it stays face up) and take a topaz: topaz stands in for any colour.",
      "Patrons on the far edge join you on their own once your bonuses meet theirs: 3 points each.",
      "You may hold ten stones at most: past ten, choose which to hand back.",
      NULL};
  float ly = y + 104 * u;
  for (int i = 0; lines[i]; i++) {
    cw_text_style st = {.size = 22 * u, .color = ink, .wrap_width = w - 120 * u, .own_backdrop = true};
    cw_text_metrics m = cw_r2d_text(V.sans, lines[i], cw_v2(x + 60 * u, ly), &st);
    ly += m.height + 12 * u;
  }
  cw_prompt_text(V.sans, "{ui_left|ui_right} move over the table   {ui_accept} pick   {ui_back} step back", -1,
                 cw_v2(cs.x * 0.5f, y + h - 110 * u), 22 * u, ink, CW_ALIGN_CENTER);
  if (!page) cw_prompt_text(V.sans, "{ui_accept} start", -1, cw_v2(cs.x * 0.5f, y + h - 60 * u), 26 * u, cw_rgba(0.45f, 0.28f, 0.1f, 1), CW_ALIGN_CENTER);
}

static void hud_results(const gm_state *st, float u, cw_vec2 cs) {
  float w = 760 * u, h = (230 + 52 * st->players) * u, x = (cs.x - w) * 0.5f, y = (cs.y - h) * 0.5f;
  cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), cw_rgba(0.01f, 0.01f, 0.02f, 0.45f));
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 18 * u, 8, cw_rgba(0.93f, 0.88f, 0.78f, 1));
  cw_r2d_round_rect_lines(cw_rect_make(x + 10 * u, y + 10 * u, w - 20 * u, h - 20 * u), 12 * u, 2.5f * u, 8, cw_rgba(0.5f, 0.36f, 0.16f, 1));
  cw_color ink = cw_rgba(0.15f, 0.1f, 0.06f, 1);
  char b[96];
  int nw = 0, wi = 0;
  for (int s = 0; s < st->players; s++)
    if (st->winners >> s & 1) nw++, wi = s;
  if (nw == 1) snprintf(b, sizeof b, "%s wins", seat_name(wi));
  else snprintf(b, sizeof b, "A shared win");
  text_c(V.serif, b, cs.x * 0.5f, y + 26 * u, 42 * u, ink, CW_ALIGN_CENTER);
  float ly = y + 100 * u;
  for (int s = 0; s < st->players; s++) {
    cw_r2d_rect(cw_rect_make(x + 70 * u, ly + 4 * u, 10 * u, 28 * u), bk_seat_color(s));
    snprintf(b, sizeof b, "%s", seat_name(s));
    text_c(V.sans, b, x + 96 * u, ly, 26 * u, ink, CW_ALIGN_LEFT);
    snprintf(b, sizeof b, "%d points, %d commissions, %d patrons", st->s[s].points, st->s[s].ncards, st->s[s].npatrons);
    text_c(V.sans, b, x + w - 70 * u, ly + 4 * u, 21 * u, ink, CW_ALIGN_RIGHT);
    ly += 52 * u;
  }
  cw_prompt_text(V.sans, "{ui_accept} play again   {ui_back} back to the title", -1, cw_v2(cs.x * 0.5f, y + h - 62 * u), 24 * u,
                 cw_rgba(0.45f, 0.28f, 0.1f, 1), CW_ALIGN_CENTER);
}

static const char *const CREDIT_LINES[] = {
    "Rules, code, card set and art: this repository (MIT). The card set is generated and balanced by tools/gen-cards.mjs.",
    "The stones' cuts come from the jewels game in even-more-cartwheel-examples (MIT, same author).",
    "Font: Cinzel, The Cinzel Project Authors, SIL Open Font License 1.1.",
    "Built with cartwheel-engine.",
    NULL};

/* beside each tray stack: its count, large, on a plate. A pile of four or more (take two allowed) has a lit gold
 * plate and a "take 2" tag; a smaller one a dark plate; topaz says it is only won by setting a card aside. */
static void hud_bank(const gm_state *st, float u) {
  for (int c = 0; c < GM_NTOK; c++) {
    t3_vec3 p = gm_bank_pos(c);
    cw_vec2 at = screen_of((t3_vec3){p.x + 0.33f, 0.05f, p.z});
    int n = bk_counter_value(&V.bank[c]);
    bool two = c != GM_WILD && st->bank[c] >= 4;
    float r = 30 * u;
    cw_color plate = two ? cw_rgba(0.78f, 0.6f, 0.24f, 0.95f) : cw_rgba(0.08f, 0.06f, 0.07f, 0.88f);
    cw_color ink = two ? cw_rgba(0.12f, 0.07f, 0.02f, 1) : n ? cw_rgba(0.95f, 0.9f, 0.82f, 1) : cw_rgba(0.55f, 0.5f, 0.48f, 1);
    cw_r2d_circle(at, r, 32, plate);
    cw_r2d_circle_lines(at, r, 2.5f * u, 32, stone_srgb(c, 1.3f));
    char b[8];
    snprintf(b, sizeof b, "%d", n);
    cw_text_style ts = {.size = 40 * u, .color = ink, .own_backdrop = true};
    cw_text_metrics m = cw_font_measure(V.sans, b, &ts);
    cw_r2d_text(V.sans, b, cw_v2(at.x - m.width * 0.5f, at.y - m.height * 0.5f), &ts);
    const char *tag = c == GM_WILD ? "wild" : two ? "take 2" : NULL;
    if (tag) {
      cw_text_style t2 = {.size = 22 * u, .color = c == GM_WILD ? cw_rgba(1, 0.8f, 0.4f, 1) : cw_rgba(1, 0.88f, 0.55f, 1), .own_backdrop = true,
                          .outline_width = 2 * u, .outline_color = cw_rgba(0, 0, 0, 0.8f)};
      cw_text_metrics m2 = cw_font_measure(V.sans, tag, &t2);
      cw_r2d_text(V.sans, tag, cw_v2(at.x - m2.width * 0.5f, at.y + r + 1 * u), &t2);
    }
  }
}

static void hud(float dt) {
  cw_r2d_begin(NULL);
  cw_vec2 cs = cw_r2d_canvas_size();
  V.canvas = cs;
  float u = cs.y / 1080.0f;
  const gm_state *st = gm_game_state(&V.g);
  hud_mats(st, u);
  hud_bank(st, u);
  bk_deltas_draw(&V.deltas, V.serif, 40 * u);
  for (int k = 0; k < 16; k++) { /* a bought card's bonus stone on its way to its column */
    if (!V.fly[k].live) continue;
    float t = V.fly[k].t, e = t * t * (3 - 2 * t);
    cw_vec2 to = screen_of(bonus_well(st->players, V.fly[k].seat, V.fly[k].colour));
    to.y -= 4 * u;
    cw_vec2 p = cw_v2(V.fly[k].from.x + (to.x - V.fly[k].from.x) * e, V.fly[k].from.y + (to.y - V.fly[k].from.y) * e - sinf(PI * t) * 80 * u);
    printed_gem(V.fly[k].colour, p, (64 - 34 * e) * u);
    text_c(V.sans, "+1", p.x + 30 * u, p.y - 28 * u, 26 * u, cw_rgba(1, 0.86f, 0.42f, 1 - 0.6f * e), CW_ALIGN_LEFT);
  }
  if (V.flow == FLOW_PLAY && !gm_game_over(&V.g)) {
    hud_info(st, u, cs);
    hud_prompt(st, u, cs);
  }
  if (V.flow == FLOW_HOWTO) hud_howto(u, cs, false);
  if (V.flow == FLOW_RESULTS) hud_results(st, u, cs);
  cw_r2d_end();
}

/* ── flow ──────────────────────────────────────────────────────────────────────────────────────── */

/* a match from its seed: the rules and the view set out afresh (a new match, or a replay of this one) */
static void begin_match(uint64_t seed) {
  int levels[BK_MAX_SEATS];
  for (int s = 0; s < V.players; s++) levels[s] = V.seats[s].kind == BK_SEAT_CPU ? V.seats[s].level : -1;
  gm_game_start(&V.g, V.players, levels, seed);
  V.g.view = (cw_view3d){V.cam, cw_rect_make(0, 0, V.canvas.x > 0 ? V.canvas.x : 1280, V.canvas.y > 0 ? V.canvas.y : 720)};
  V.g.have_view = true;
  V.g.active_key = 0;
  for (int s = 0; s < BK_MAX_SEATS; s++) bk_counter_init(&V.points[s], 0);
  for (int c = 0; c < GM_NTOK; c++) bk_counter_init(&V.bank[c], gm_game_state(&V.g)->bank[c]);
  for (int p = 0; p < BK_MAX_SEATS; p++)
    for (int c = 0; c < GM_NTOK; c++) {
      if (c < GM_NCOLORS) bk_counter_init(&V.bonus[p][c], 0);
      bk_counter_init(&V.toks[p][c], 0);
    }
  memset(V.fly, 0, sizeof V.fly);
  bk_deltas_init(&V.deltas);
  memset(V.card_shown, 0, sizeof V.card_shown);
  for (int i = 0; i < NTOKENS; i++) V.tok[i].holder = -1, V.tok[i].pos = (t3_vec3){0, 0, 0};
  memset(V.patron_pos, 0, sizeof V.patron_pos);
  memset(V.halo_k, 0, sizeof V.halo_k);
  V.last = *gm_game_state(&V.g);
  V.in_match = true;
}

static void start_match(void) {
  V.match_no++;
  uint64_t seed = bk_seed_mix(V.match_no, 0x6a3e7ull);
  const char *sv = cw_test_env("FACET_SEED");
  if (sv && *sv) seed = strtoull(sv, NULL, 10) + V.match_no - 1;
  begin_match(seed);
  V.start_seed = seed;
  V.start_flow = V.flow;
  V.fresh = true;
  R.replaying = R.replay_done = 0;
  cw_input_record_begin(); /* the whole match: pad presses, CPU presses and pointer picks */
}

/* debug command `replay`: the match so far again from its own recording (the rules state and the input hash must
 * come out the same: facet.replay_match, facet.replay_hash_match) */
static void cmd_replay(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (cw_shell_screen_now() != CW_SHELL_PLAY || !cw_input_recording_active() || !V.in_match) {
    cw_reply_error(r, "replay needs a match in play");
    return;
  }
  R.want_hash = (uint32_t)bk_match_hash(V.g.match);
  cw_input_record_end();
  R.want_input = cw_input_record_hash();
  size_t len = 0;
  const uint8_t *d = cw_input_recording(&len);
  free(R.recording);
  R.recording = malloc(len ? len : 1);
  if (!R.recording) CW_FATAL("facet: no memory for the recording");
  memcpy(R.recording, d, len);
  R.len = len;
  begin_match(V.start_seed);
  V.flow = V.start_flow;
  if (!cw_input_replay_begin(R.recording, R.len)) {
    cw_reply_error(r, "the recording did not load");
    return;
  }
  R.replaying = 1;
  R.replay_done = R.replay_match = R.replay_hash_match = 0;
  cw_reply_printf(r, "{\"bytes\":%zu,\"state\":%u,\"input\":%u}", len, R.want_hash, R.want_input);
}

static void on_start(int players, void *user) {
  (void)players, (void)user;
  int n = bk_lobby_seats(V.seats, BK_MAX_SEATS);
  if (n < 2) { /* playing alone: a Normal guild across the table */
    V.seats[1] = (bk_seat){BK_SEAT_CPU, 1};
    n = 2;
  }
  V.players = n;
  bool all_cpu = true;
  for (int s = 0; s < n; s++) all_cpu &= V.seats[s].kind == BK_SEAT_CPU;
  V.flow = all_cpu ? FLOW_PLAY : FLOW_HOWTO;
  start_match();
}

static void on_quit(void *user) {
  (void)user;
  gm_game_end(&V.g);
  V.in_match = false;
}

static gm_game *game_of(void) { return &V.g; }

static void on_pause_item(int i, void *user) {
  (void)user;
  if (i == 0) V.flow = FLOW_HOWTO;
}

static bk_credits CREDITS = {.title = "Facet", .inspired_by = "Inspired by Splendor", .lines = CREDIT_LINES}; /* font: init */

static void how_to_play(cw_vec2 canvas, void *user) {
  (void)user;
  hud_howto(canvas.y / 1080.0f, canvas, true);
}

/* the menus tick (bk_title, after the kit reads FACET_SEATS): the self-playing cart starts three guilds, every
 * level, when no switch did. Title tick 3: a host env request (sent at frame 2) is in by then. */
static void menu_update(cw_shell_screen screen, void *user) {
  (void)user;
#ifdef FACET_AUTO
  static bool auto_started;
  static int title_ticks;
  if (screen == CW_SHELL_TITLE) title_ticks++;
  if (!auto_started && !bk_lobby_started_by_test() && title_ticks > 2) {
    auto_started = true;
    cw_shell_start_seats(3, (int[]){CW_SHELL_CPU_HARD, CW_SHELL_CPU_NORMAL, CW_SHELL_CPU_EASY});
  }
#else
  (void)screen;
#endif
}

static const bk_title TITLE = {.test_env = "FACET_SEATS", .credits = &CREDITS, .how_to_play = how_to_play, .menu_update = menu_update};

static void init(void *user) {
  (void)user;
  cw_shell_desc d = {.title = "Facet", .subtitle = "a lapidary guild's market", .start = on_start, .quit = on_quit,
                     .pause_items = {"How to play"}, .pause_item = on_pause_item};
  bk_lobby_desc(&d, 4, NULL, NULL);
  bk_title_desc(&d, &TITLE);
  cw_shell_init(&d);
  a_up = cw_action_find("ui_up"), a_down = cw_action_find("ui_down"), a_left = cw_action_find("ui_left");
  a_right = cw_action_find("ui_right"), a_pick = cw_action_find("ui_accept"), a_back = cw_action_find("ui_back");
  if (a_pick < 0 || a_back < 0 || a_left < 0) CW_FATAL("facet: the shell's ui actions are missing");
  V.serif = cw_font_load("fonts/Cinzel.ttf", NULL);
  CREDITS.font = V.serif;
  V.sans = cw_ui_font_sans();
  cw_font_preload_range(V.serif, 32, 126);
  V.tree = cw_scene_app_tree();
  V.world = cw_node_get(cw_tree_root(V.tree), "World");
  V.cam = cw_node_get(V.world, "Camera");
  cw_node3d_set_position(V.cam, (t3_vec3){0.45f, 13.4f, 6.3f});
  cw_node_set(V.cam, "fov", cw_val_float(40));
  cw_node3d_look_at(V.cam, (t3_vec3){0.45f, 0, 0.25f}, (t3_vec3){0, 1, 0});
  bk_cards_init(V.tree, paint, NULL);
  gm_game_init(&V.g, &(gm_actions){a_left, a_right, a_up, a_down, a_pick, a_back});
  build_table();
  gm_trace_init(V.tree);
  debug_register();
  /* the table before a match: a game set out, nobody to move */
  V.players = 2;
  V.seats[0] = V.seats[1] = (bk_seat){BK_SEAT_CPU, 1};
  gm_game_start(&V.g, 2, (int[]){1, 1}, 1);
}


static void fixed_update_inner(void);
static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  D.ticks++;
  fixed_update_inner();
  debug_update();
}

static void fixed_update_inner(void) {
  if (!cw_shell_update()) return;
  V.ticks++;
  if (!V.in_match) return;
  if (V.fresh) { V.fresh = false; return; } /* the start tick is not recorded: play begins at the next */
  if (R.replaying && !cw_input_replay_active()) {
    R.replay_match = (uint32_t)bk_match_hash(V.g.match) == R.want_hash;
    R.replay_hash_match = cw_input_replay_hash() == R.want_input;
    R.replaying = 0;
    R.replay_done = 1;
    cw_info("facet replay: state %s, input hash %s", R.replay_match ? "same" : "DIFFERENT", R.replay_hash_match ? "same" : "DIFFERENT");
  }
  const gm_state *st = gm_game_state(&V.g);
  if (V.flow == FLOW_HOWTO) {
    for (int p = 0; p < V.players; p++)
      if (cw_action_just_pressed(p, a_pick)) V.flow = FLOW_PLAY;
    return;
  }
  if (V.flow == FLOW_RESULTS) {
    for (int p = 0; p < V.players; p++) {
      if (cw_action_just_pressed(p, a_pick)) { V.flow = FLOW_PLAY; start_match(); return; }
      if (cw_action_just_pressed(p, a_back)) { on_quit(NULL); cw_shell_to_title(); return; }
    }
    /* a self-playing cart plays match after match */
    bool self_playing = true; /* every seat a computer guild: match after match */
    for (int s = 0; s < V.players; s++) self_playing &= V.seats[s].kind == BK_SEAT_CPU;
#ifdef FACET_AUTO
    self_playing = true;
#endif
    const char *one = cw_test_env("FACET_ONE_MATCH"); /* a gate holds the results of its one match */
    if (one && *one) self_playing = false;
    if (self_playing && ++V.results_ticks >= 300) { V.results_ticks = 0; V.flow = FLOW_PLAY; start_match(); }
    return;
  }
  cw_shell_set_turn(gm_game_over(&V.g) ? -1 : st->seat);
  gm_game_tick(&V.g);
  if (gm_game_over(&V.g)) {
    V.flow = FLOW_RESULTS;
    D.finished++;
  }
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  float dt = 1.0f / 60.0f;
  V.t += dt;
  if (V.built && V.g.match) {
    if (V.in_match && V.flow == FLOW_PLAY) {
      int seat = gm_game_state(&V.g)->seat;
      bk_select_pointer(V.g.sel, V.g.cpu[seat] ? -1 : seat, V.tree);
    }
    bk_select_update(V.g.sel, dt);
    const gm_state *st = gm_game_state(&V.g);
    for (int s = 0; s < st->players; s++) {
      if (st->s[s].points != V.last.s[s].points) {
        t3_vec3 m = gm_mat_pos(st->players, s);
        bk_deltas_add(&V.deltas, screen_of(m), st->s[s].points - V.last.s[s].points, cw_rgba(1, 0.85f, 0.4f, 1));
        bk_counter_set(&V.points[s], st->s[s].points);
      }
      bk_counter_update(&V.points[s], dt);
    }
    {
      static int stale = -1; /* the gate's control: a view whose tray labels never follow the rules */
      if (stale < 0 && cw_test_env("FACET_TEST_STALE_COUNTS")) stale = 1;
      for (int c = 0; c < GM_NTOK; c++) {
        if (stale != 1 && V.bank[c].target != st->bank[c]) bk_counter_set(&V.bank[c], st->bank[c]);
        bk_counter_update(&V.bank[c], dt);
      }
      int mism = 0;
      for (int c = 0; c < GM_NTOK; c++) {
        D.bank_rules[c] = st->bank[c];
        D.bank_target[c] = V.bank[c].target;
        D.bank_shown[c] = bk_counter_value(&V.bank[c]);
        D.bank_stack[c] = V.bank_stack[c];
        bool settled = !bk_counter_moving(&V.bank[c]);
        if (V.bank[c].target != st->bank[c] || (settled && D.bank_shown[c] != st->bank[c]) || V.bank_stack[c] != st->bank[c]) mism++;
      }
      D.bank_mismatch = mism;
      /* the mats */
      static int stale_panel = -1; /* the gate's control: mats that never follow the rules */
      if (stale_panel < 0 && cw_test_env("FACET_TEST_STALE_PANEL")) stale_panel = 1;
      for (int p = 0; p < st->players && stale_panel != 1; p++) {
        for (int c = 0; c < GM_NCOLORS; c++) {
          int was = V.last.s[p].bonus[c], now = st->s[p].bonus[c];
          if (now > was && V.match_shown == V.match_no) /* a card bought: its bonus stone flies from the card */
            for (int card = 0; card < GM_NCARDS; card++)
              if (st->where[card] == GM_AT_BOUGHT && V.last.where[card] != GM_AT_BOUGHT && st->owner[card] == p && gm_card_set[card].bonus == c)
                for (int k = 0; k < 16; k++)
                  if (!V.fly[k].live) {
                    V.fly[k] = (typeof(V.fly[0])){true, p, c, screen_of(V.card_pos[card]), 0};
                    break;
                  }
          bool flying = false;
          for (int k = 0; k < 16; k++) flying |= V.fly[k].live && V.fly[k].seat == p && V.fly[k].colour == c;
          if (!flying && V.bonus[p][c].target != now) bk_counter_set(&V.bonus[p][c], now);
        }
        for (int c = 0; c < GM_NTOK; c++)
          if (V.toks[p][c].target != st->s[p].tok[c]) bk_counter_set(&V.toks[p][c], st->s[p].tok[c]);
      }
      V.match_shown = V.match_no;
      int flying_n = 0;
      for (int k = 0; k < 16; k++)
        if (V.fly[k].live) {
          V.fly[k].t += dt / 0.8f;
          if (V.fly[k].t >= 1) {
            V.fly[k].live = false;
            int p = V.fly[k].seat, c = V.fly[k].colour;
            bool more = false;
            for (int j = 0; j < 16; j++) more |= V.fly[j].live && V.fly[j].seat == p && V.fly[j].colour == c;
            if (!more) bk_counter_set(&V.bonus[p][c], st->s[p].bonus[c]);
          } else flying_n++;
        }
      int pm = 0;
      for (int p = 0; p < BK_MAX_SEATS; p++)
        for (int c = 0; c < GM_NTOK; c++) {
          int i = p * GM_NTOK + c;
          bool in = p < st->players;
          D.panel_bonus[i] = in && c < GM_NCOLORS ? bk_counter_value(&V.bonus[p][c]) : 0;
          D.panel_tokens[i] = in ? bk_counter_value(&V.toks[p][c]) : 0;
          if (!in) continue;
          bool fl = false;
          for (int k = 0; k < 16; k++) fl |= V.fly[k].live && V.fly[k].seat == p && V.fly[k].colour == c;
          if (c < GM_NCOLORS) {
            bk_counter *b = &V.bonus[p][c];
            if (!fl && (b->target != st->s[p].bonus[c] || (!bk_counter_moving(b) && D.panel_bonus[i] != st->s[p].bonus[c]))) pm++;
          }
          bk_counter *t = &V.toks[p][c];
          if (t->target != st->s[p].tok[c] || (!bk_counter_moving(t) && D.panel_tokens[i] != st->s[p].tok[c])) pm++;
        }
      for (int p = 0; p < BK_MAX_SEATS; p++)
        for (int c = 0; c < GM_NTOK; c++) {
          if (c < GM_NCOLORS) bk_counter_update(&V.bonus[p][c], dt);
          bk_counter_update(&V.toks[p][c], dt);
        }
      D.panel_flying = flying_n;
      D.panel_mismatch = pm;
    }
    bk_deltas_update(&V.deltas, dt);
    V.last = *st;
    sync(dt);
  }
  gm_trace_bake_step();
  const char *nohud = cw_test_env("FACET_NO_HUD"); /* the art script's key frames: the table alone */
  if (V.in_match && cw_shell_hud_shown() && !(nohud && *nohud)) hud(dt);
  if (V.g.match && V.canvas.x > 0 && (V.g.view.rect.w != V.canvas.x || V.g.view.rect.h != V.canvas.y)) {
    V.g.view = (cw_view3d){V.cam, cw_rect_make(0, 0, V.canvas.x, V.canvas.y)};
    V.g.have_view = true;
    V.g.active_key = 0; /* the cursor re-reads its view */
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  static cw_app_desc base = {.title = "Facet", .width = 1280, .height = 720, .init = init, .fixed_update = fixed_update,
                             .render = render, .debug = true};
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &base);
}
