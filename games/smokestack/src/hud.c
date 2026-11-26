/* Smokestack HUD and painted faces (view_internal.h): job cards, option plaques, the seat panels, prompts, the
 * how-to-play card and the results. Plain words for players; prompts name actions, never buttons. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "boardkit/bk_card.h"
#include "cartwheel/prompt.h"
#include "cartwheel/ui.h"
#include "view_internal.h"

static cw_color rgb(uint32_t c, float a) {
  cw_color k = cw_hex((c << 8) | 0xff);
  k.a = a;
  return k;
}

static float text(float x, float y, float size, cw_color col, const char *s, int align, float outline) {
  cw_font f = cw_ui_font_sans();
  cw_text_style st = { .size = size, .color = col, .outline_width = outline, .fixed_size = true, .own_backdrop = true };
  cw_text_metrics m = cw_font_measure(f, s, &st);
  float at = align == 0 ? x : align == 1 ? x - m.width * 0.5f : x - m.width;
  cw_r2d_text(f, s, cw_v2(at, y), &st);
  return m.width;
}
/* text no wider than maxw: the size shrinks to fit */
static float fit_text(float x, float y, float size, float maxw, cw_color col, const char *str, int align) {
  cw_font f = cw_ui_font_sans();
  cw_text_style st = { .size = size, .fixed_size = true };
  float w = cw_font_measure(f, str, &st).width;
  if (w > maxw) size *= maxw / w;
  return text(x, y, size, col, str, align, 0);
}
static void wrap(float x, float y, float w, float size, cw_color col, const char *s) {
  cw_font f = cw_ui_font_sans();
  cw_text_style st = { .size = size, .color = col, .wrap_width = w, .align = CW_ALIGN_CENTER, .fixed_size = true, .own_backdrop = true };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}

/* ── faces ── */
static const uint32_t JOB_RGB[RL_ACTIONS] = { 0x7a5a2e, 0x3d6b8a, 0x5c5c66, 0x8a5a2e, 0x4f7a3a, 0x8a3a3a };

static void icon(int a, float cx, float cy, float s, cw_color ink) {
  switch (a) {
  case RL_A_GANG: /* rails over ties */
    for (int i = -2; i <= 2; i++) cw_r2d_rect(cw_rect_make(cx + (float)i * s * 0.3f - s * 0.06f, cy - s * 0.42f, s * 0.12f, s * 0.84f), cw_hex(0x6b4a2eff));
    cw_r2d_rect(cw_rect_make(cx - s * 0.75f, cy - s * 0.25f, 1.5f * s, s * 0.09f), ink);
    cw_r2d_rect(cw_rect_make(cx - s * 0.75f, cy + s * 0.16f, 1.5f * s, s * 0.09f), ink);
    break;
  case RL_A_CHARTER: { /* a town hall with a flag */
    cw_r2d_rect(cw_rect_make(cx - s * 0.55f, cy - s * 0.05f, s * 1.1f, s * 0.55f), ink);
    cw_vec2 roof[3] = { { cx - s * 0.7f, cy - s * 0.05f }, { cx + s * 0.7f, cy - s * 0.05f }, { cx, cy - s * 0.5f } };
    cw_r2d_polygon(roof, 3, ink);
    cw_r2d_rect(cw_rect_make(cx - s * 0.02f, cy - s * 0.9f, s * 0.05f, s * 0.42f), ink);
    cw_vec2 flag[3] = { { cx + s * 0.03f, cy - s * 0.9f }, { cx + s * 0.4f, cy - s * 0.78f }, { cx + s * 0.03f, cy - s * 0.66f } };
    cw_r2d_polygon(flag, 3, cw_hex(0xc8372dff));
    break;
  }
  case RL_A_ENGINE:
  case RL_A_LEAD_SHIP: { /* a locomotive */
    float k = a == RL_A_ENGINE ? 1.0f : 0.8f;
    cw_r2d_rect(cw_rect_make(cx - s * 0.7f * k, cy - s * 0.2f * k, s * 1.0f * k, s * 0.4f * k), ink);
    cw_r2d_rect(cw_rect_make(cx + s * 0.3f * k, cy - s * 0.5f * k, s * 0.4f * k, s * 0.7f * k), ink);
    cw_r2d_rect(cw_rect_make(cx - s * 0.55f * k, cy - s * 0.5f * k, s * 0.16f * k, s * 0.32f * k), ink);
    for (int i = 0; i < 3; i++) cw_r2d_circle(cw_v2(cx - s * 0.5f * k + (float)i * s * 0.42f * k, cy + s * 0.3f * k), s * 0.16f * k, 20, ink);
    if (a == RL_A_ENGINE) {
      cw_r2d_rect(cw_rect_make(cx + s * 0.78f, cy - s * 0.04f, s * 0.36f, s * 0.08f), cw_hex(0x2e7a3aff));
      cw_r2d_rect(cw_rect_make(cx + s * 0.92f, cy - s * 0.18f, s * 0.08f, s * 0.36f), cw_hex(0x2e7a3aff));
    }
    break;
  }
  case RL_A_FREIGHT:
    cw_r2d_rect(cw_rect_make(cx - s * 0.6f, cy - s * 0.1f, s * 0.5f, s * 0.5f), cw_hex((RL_GOODS_RGB[0] << 8) | 0xff));
    cw_r2d_rect(cw_rect_make(cx + s * 0.05f, cy - s * 0.1f, s * 0.5f, s * 0.5f), cw_hex((RL_GOODS_RGB[2] << 8) | 0xff));
    cw_r2d_rect(cw_rect_make(cx - s * 0.28f, cy - s * 0.62f, s * 0.5f, s * 0.5f), cw_hex((RL_GOODS_RGB[1] << 8) | 0xff));
    break;
  case RL_A_LEAD_BUILD: {
    cw_vec2 hx[6];
    for (int i = 0; i < 6; i++) hx[i] = cw_v2(cx + s * 0.6f * cosf((float)M_PI / 6 + (float)i * (float)M_PI / 3), cy + s * 0.6f * sinf((float)M_PI / 6 + (float)i * (float)M_PI / 3));
    cw_r2d_polygon_lines(hx, 6, s * 0.1f, ink);
    text(cx, cy - s * 0.48f, s * 0.95f, ink, "1", 1, 0);
    break;
  }
  default: break;
  }
}

void rl_paint_face(int face, cw_vec2 px) {
  float w = px.x, h = px.y;
  if (face >= RL_FACE_JOB0 && face < RL_FACE_JOB0 + RL_ACTIONS) {
    int a = face - RL_FACE_JOB0;
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0xeee2c4ff));
    for (int i = 0; i < 40; i++) /* paper grain */
      cw_r2d_rect(cw_rect_make(0, h * (float)i / 40, w, h * 0.006f), cw_rgba(0.6f, 0.5f, 0.35f, 0.05f));
    /* icon first: the name across a coloured band, a big icon on the left and what it gives in two short lines,
     * sized to read at the table camera (the full sentence shows under the board when the cursor is on it) */
    static const char *const SHORT[RL_ACTIONS] = { "4 tiles", "new city", "+1 car", "+2 goods", "build 1st", "haul 1st" };
    cw_r2d_rect(cw_rect_make(0, 0, w, h * 0.3f), rgb(JOB_RGB[a], 1));
    cw_r2d_rect_lines(cw_rect_make(w * 0.025f, w * 0.025f, w * 0.95f, h - w * 0.05f), w * 0.014f, cw_hex(0x3b2a1aff));
    fit_text(w * 0.5f, h * 0.04f, h * 0.22f, w * 0.9f, cw_hex(0xfff6e0ff), RL_ACTION_NAMES[a], 1);
    icon(a, w * 0.2f, h * 0.66f, h * 0.18f, cw_hex(0x2b2622ff));
    fit_text(w * 0.67f, h * 0.48f, h * 0.3f, w * 0.56f, cw_hex(0x2b1d10ff), SHORT[a], 1);
    return;
  }
  if (face == BK_FACE_BACK) {
    cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0x3a2a1cff));
    return;
  }
  /* option plaques: brass lettering on dark wood */
  cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_hex(0x3b2617ff));
  for (int i = 0; i < 12; i++) cw_r2d_rect(cw_rect_make(0, h * (float)i / 12, w, h * 0.02f), cw_rgba(0.1f, 0.05f, 0.02f, 0.25f));
  cw_r2d_rect_lines(cw_rect_make(h * 0.06f, h * 0.06f, w - h * 0.12f, h * 0.88f), h * 0.035f, cw_hex(0xc9a45aff));
  static const char *const LABEL[] = { "Drop out", "Raise 1", "Raise 2", "Raise 5", "End building", "Issue share",
                                       "Charter a town", "Pass", "Grow engine", "As income", "As points" };
  if (face >= RL_FACE_COLOR0 && face < RL_FACE_COLOR0 + RL_COLORS) {
    int c = face - RL_FACE_COLOR0;
    cw_r2d_round_rect(cw_rect_make(w * 0.1f, h * 0.24f, h * 0.52f, h * 0.52f), h * 0.06f, 6, rgb(RL_GOODS_RGB[c], 1));
    char line[32];
    snprintf(line, sizeof line, "A %s city", RL_COLOR_NAMES[c]);
    text(w * 0.6f, h * 0.3f, h * 0.33f, cw_hex(0xf1dca0ff), line, 1, 0);
    return;
  }
  int i = face - RL_FACE_OPT0;
  if (i >= 0 && i < (int)(sizeof LABEL / sizeof LABEL[0])) text(w * 0.5f, h * 0.27f, h * 0.38f, cw_hex(0xf1dca0ff), LABEL[i], 1, 0);
}

int rl_option_face(const rl_state *s, bk_move m) {
  int k = rl_mv_kind(m), a = rl_mv_a(m);
  switch (k) {
  case RL_MV_PASS: return s->phase == RL_PH_BID ? RL_FACE_DROP : s->phase == RL_PH_BUILD ? RL_FACE_END : RL_FACE_PASS;
  case RL_MV_RAISE: return a == 1 ? RL_FACE_R1 : a == 2 ? RL_FACE_R2 : RL_FACE_R5;
  case RL_MV_SHARE: return RL_FACE_SHARE;
  case RL_MV_CHARTER: return RL_FACE_CHARTER;
  case RL_MV_ENGINE: return RL_FACE_ENGINE;
  case RL_MV_INCOME: return RL_FACE_INCOME;
  case RL_MV_POINTS: return RL_FACE_POINTS;
  case RL_MV_COLOR: return RL_FACE_COLOR0 + a;
  default: return RL_FACE_PASS;
  }
}

/* ── panels ── */
static cw_rect panel_rect(int seat) {
  cw_vec2 sz = cw_r2d_canvas_size();
  int n = RV.g->players;
  float w = sz.x * 0.19f, gap = sz.x * 0.008f, x0 = sz.x * 0.012f;
  (void)n;
  return cw_rect_make(x0 + (float)seat * (w + gap), sz.y * 0.015f, w, sz.y * 0.135f);
}
cw_vec2 rl_hud_panel_point(int seat, int which) {
  cw_rect r = panel_rect(seat);
  return cw_v2(r.x + r.w * (which ? 0.68f : 0.24f), r.y + r.h * 1.42f);
}

static const char *phase_name(const rl_state *s) {
  switch (s->phase) {
  case RL_PH_BID: return "Bidding for first place";
  case RL_PH_ACTION:
  case RL_PH_FREIGHT: return "Choosing jobs";
  case RL_PH_BUILD:
  case RL_PH_EXIT:
  case RL_PH_TOWN:
  case RL_PH_COLOR: return "Laying track";
  case RL_PH_HAUL:
  case RL_PH_ROUTE:
  case RL_PH_PAYOUT: return "Hauling goods";
  case RL_PH_PRODUCE: return "New goods";
  default: return "Final count";
  }
}

static void draw_panels(const char *const *names) {
  const rl_state *s = rl_game_state(RV.g);
  int turn = bk_match_turn(RV.g->match);
  for (int p = 0; p < s->players; p++) {
    cw_rect r = panel_rect(p);
    float u = r.h;
    bool now = p == turn && !bk_match_over(RV.g->match);
    cw_r2d_round_rect(r, u * 0.12f, 6, cw_rgba(0.09f, 0.07f, 0.05f, now ? 0.9f : 0.72f));
    if (now) cw_r2d_round_rect_lines(r, u * 0.12f, u * 0.035f, 6, rgb(RL_SEAT_RGB[p], 1));
    cw_r2d_rect(cw_rect_make(r.x + u * 0.08f, r.y + u * 0.14f, u * 0.06f, u * 0.72f), rgb(RL_SEAT_RGB[p], 1));
    char line[96];
    const bk_seat *st = &RV.g->seat[p];
    snprintf(line, sizeof line, "%s%s", names[p], st->kind == BK_SEAT_CPU ? (st->level == 0 ? "  Easy" : st->level == 1 ? "  Normal" : "  Hard") : "");
    text(r.x + u * 0.24f, r.y + u * 0.08f, u * 0.2f, rgb(RL_SEAT_RGB[p], 1), line, 0, 0);
    const rl_player *pl = &s->p[p];
    snprintf(line, sizeof line, "$%d", bk_counter_value(&RV.money[p]));
    text(r.x + u * 0.24f, r.y + u * 0.36f, u * 0.3f, cw_hex(0xf2d27aff), line, 0, 0);
    snprintf(line, sizeof line, "%d pts", bk_counter_value(&RV.points[p]));
    text(r.x + r.w * 0.55f, r.y + u * 0.36f, u * 0.3f, cw_hex(0xf4efe4ff), line, 0, 0);
    int net = rl_net_income(pl);
    snprintf(line, sizeof line, "%s$%d a round", net < 0 ? "-" : "+", net < 0 ? -net : net);
    text(r.x + u * 0.24f, r.y + u * 0.7f, u * 0.15f, net < 0 ? cw_hex(0xf08a7aff) : cw_hex(0xbcd9a4ff), line, 0, 0);
    /* the engine: a locomotive and its size in words (a good goes that many links), under the points */
    snprintf(line, sizeof line, "engine %d", pl->engine);
    float ew = text(r.x + r.w - u * 0.1f, r.y + u * 0.69f, u * 0.16f, cw_hex(0xe8dcc0ff), line, 2, 0);
    icon(RL_A_ENGINE, r.x + r.w - u * 0.1f - ew - u * 0.2f, r.y + u * 0.79f, u * 0.13f, rgb(RL_SEAT_RGB[p], 1));
    if (pl->shares) {
      snprintf(line, sizeof line, "%d share%s", pl->shares, pl->shares > 1 ? "s" : "");
      text(r.x + r.w - u * 0.1f, r.y + u * 0.1f, u * 0.15f, cw_hex(0xe0c8a0ff), line, 2, 0);
    }
  }
  /* round and phase */
  cw_vec2 sz = cw_r2d_canvas_size();
  char line[64];
  snprintf(line, sizeof line, "Round %d of %d", s->round > s->rounds ? s->rounds : s->round, s->rounds);
  text(sz.x * 0.985f, sz.y * 0.02f, sz.y * 0.032f, cw_hex(0xf4efe4ff), line, 2, 2);
  text(sz.x * 0.985f, sz.y * 0.062f, sz.y * 0.024f, cw_hex(0xe0d2b4ff), phase_name(s), 2, 2);
  int8_t ord[RL_MAX_PLAYERS];
  (void)ord;
  /* turn order */
  float x = sz.x * 0.985f;
  for (int i = s->players - 1; i >= 0; i--) {
    int seat = s->order[i];
    x -= sz.y * 0.032f;
    cw_r2d_circle(cw_v2(x, sz.y * 0.115f), sz.y * 0.012f, 16, rgb(RL_SEAT_RGB[seat], 1));
  }
  text(x - sz.y * 0.02f, sz.y * 0.1f, sz.y * 0.024f, cw_hex(0xe0d2b4ff), "order", 2, 2);
}

/* what the cursor's choice does, in words */
static void choice_words(const rl_state *s, int turn, bk_move m, char *buf, size_t cap) {
  const rl_map_def *mp = rl_map(s->map);
  int k = rl_mv_kind(m), a = rl_mv_a(m), b = rl_mv_b(m);
  buf[0] = 0;
  switch (k) {
  case RL_MV_RAISE: snprintf(buf, cap, "Bid $%d for first place", s->bid + a); break;
  case RL_MV_PASS:
    if (s->phase == RL_PH_BID) snprintf(buf, cap, "Drop out: take the last place still open");
    else if (s->phase == RL_PH_BUILD) snprintf(buf, cap, s->p[turn].open_link >= 0 && !s->p[turn].extended ? "End building: your unfinished link is torn up" : "End building for this round");
    else snprintf(buf, cap, "Pass this haul");
    break;
  case RL_MV_ACTION: snprintf(buf, cap, "%s: %s", RL_ACTION_NAMES[a], RL_ACTION_TEXT[a]); break;
  case RL_MV_PLACE: snprintf(buf, cap, "Put the %s good in %s", RL_COLOR_NAMES[s->drawn[0] - 1], mp->set_name[a]); break;
  case RL_MV_START: snprintf(buf, cap, "Start a link out of %s: a tile here costs $%d", mp->set_name[mp->hex_set[mp->nb[a][b]]], rl_tile_cost(s, a)); break;
  case RL_MV_EXIT: {
    int h = rl_next_tile(s, turn, NULL, NULL), n = mp->nb[h][a], set = mp->hex_set[n];
    int cost = rl_tile_cost(s, h) + (set >= 0 && s->set_kind[set] == RL_TOWN);
    if (set >= 0) snprintf(buf, cap, "Lay this tile for $%d and finish the link at %s", cost, mp->set_name[set]);
    else snprintf(buf, cap, "Lay this tile for $%d", cost);
    break;
  }
  case RL_MV_SHARE: snprintf(buf, cap, "Issue a share: $5 now, $1 a round and 1 point at the end (%d more allowed)", rl_share_room(s, turn) - 1); break;
  case RL_MV_CHARTER: snprintf(buf, cap, "Charter a town into a new city"); break;
  case RL_MV_TOWN: snprintf(buf, cap, "Charter %s", mp->set_name[a]); break;
  case RL_MV_COLOR: snprintf(buf, cap, "%s becomes a %s city", mp->set_name[s->charter_town], RL_COLOR_NAMES[a]); break;
  case RL_MV_GOOD: snprintf(buf, cap, "Haul the %s good from %s", RL_COLOR_NAMES[s->cube[a][b] - 1], mp->set_name[a]); break;
  case RL_MV_ENGINE: snprintf(buf, cap, "Grow your engine to %d cars: goods go %d links", s->p[turn].engine + 1, s->p[turn].engine + 1); break;
  case RL_MV_DEST: {
    rl_route r[RL_MAX_ROUTES];
    int nr = rl_routes(s, s->good_city, s->good_slot, turn, r, RL_MAX_ROUTES);
    for (int i = 0; i < nr; i++)
      if (r[i].dest == a) snprintf(buf, cap, "To %s: %d link%s, %d of them yours", mp->set_name[a], r[i].n, r[i].n > 1 ? "s" : "", r[i].own);
    break;
  }
  case RL_MV_INCOME: snprintf(buf, cap, "Your %d link%s raise your income by %d", s->last_gain, s->last_gain > 1 ? "s" : "", s->last_gain); break;
  case RL_MV_POINTS: snprintf(buf, cap, "Your %d link%s score %d point%s now", s->last_gain, s->last_gain > 1 ? "s" : "", s->last_gain, s->last_gain > 1 ? "s" : ""); break;
  default: break;
  }
}

static const char *prompt_for(const rl_state *s) {
  switch (s->phase) {
  case RL_PH_BID: return "{left|right} choose    {ok} raise the bid or drop out";
  case RL_PH_ACTION: return "{up|down} choose a job    {ok} take it";
  case RL_PH_FREIGHT: return "{left|right} choose a city    {ok} put the good there";
  case RL_PH_BUILD: return s->p[rl_current(s)].open_link >= 0 ? "{left|right} choose a way    {ok} lay the next tile" : "{left|right} choose a lit edge    {ok} start a link there";
  case RL_PH_EXIT: return "{left|right} turn the tile    {ok} lay it    {back} take it back";
  case RL_PH_TOWN: return "{left|right} choose a town    {ok} charter it    {back} take it back";
  case RL_PH_COLOR: return "{left|right} choose a colour    {ok} found the city";
  case RL_PH_HAUL: return "{left|right} choose a good    {ok} haul it, or grow your engine, or pass";
  case RL_PH_ROUTE: return "{left|right} choose where it goes    {ok} haul it    {back} pick another";
  case RL_PH_PAYOUT: return "{left|right} choose    {ok} take your share";
  default: return "";
  }
}

/* the how-to-play card: over play (with its close prompt) and as the title page (the shell draws the way back) */
static void draw_help(cw_vec2 sz, float a, bool prompt) {
  cw_rect r = cw_rect_make(sz.x * 0.2f, sz.y * 0.16f, sz.x * 0.6f, sz.y * 0.7f);
  if (prompt) cw_r2d_rect(cw_rect_make(0, 0, sz.x, sz.y), cw_rgba(0, 0, 0, 0.35f * a));
  cw_r2d_round_rect(r, sz.y * 0.02f, 8, cw_rgba(0.94f, 0.89f, 0.77f, a));
  cw_r2d_round_rect_lines(r, sz.y * 0.02f, sz.y * 0.005f, 8, cw_rgba(0.35f, 0.22f, 0.12f, a));
  cw_color ink = cw_rgba(0.16f, 0.11f, 0.07f, a);
  text(sz.x * 0.5f, r.y + sz.y * 0.03f, sz.y * 0.05f, ink, "How to play Smokestack", 1, 0);
  static const char *const LINES[] = {
    "Lay track between towns and cities, then run trains to haul goods to the city of their colour.",
    "Each round: bid for first place, take a job card, lay up to three tiles, haul twice, then collect income ($2 plus your income, less $1 a share).",
    "A link is a run of tiles from one settlement to another. Tiles cost more in forest, across rivers and in hills.",
    "A good travels as many links as your engine has cars. Every link it uses pays its owner a point.",
    "Your own links pay you as points or as income. Short of money? Issue a share (up to your finished links plus two).",
    "At the end each finished link adds 2 points, every $4 left adds 1 and each share takes 1 away. Most points wins.",
  };
  for (int i = 0; i < 6; i++) wrap(r.x + sz.x * 0.04f, r.y + sz.y * (0.12f + 0.085f * (float)i), r.w - sz.x * 0.08f, sz.y * 0.027f, ink, LINES[i]);
  if (prompt) cw_prompt_text(cw_ui_font_sans(), "{ok} start playing", -1, cw_v2(sz.x * 0.5f, r.y + r.h - sz.y * 0.07f), sz.y * 0.032f, ink, CW_ALIGN_CENTER);
}

static void draw_results(const char *const *names) {
  const rl_state *s = rl_game_state(RV.g);
  cw_vec2 sz = cw_r2d_canvas_size();
  cw_rect r = cw_rect_make(sz.x * 0.27f, sz.y * 0.22f, sz.x * 0.46f, sz.y * (0.2f + 0.07f * (float)s->players));
  cw_r2d_round_rect(r, sz.y * 0.02f, 8, cw_rgba(0.09f, 0.07f, 0.05f, 0.9f));
  cw_r2d_round_rect_lines(r, sz.y * 0.02f, sz.y * 0.004f, 8, cw_hex(0xc9a45aff));
  int8_t o[RL_MAX_PLAYERS];
  rl_standing(s, o);
  char line[96];
  snprintf(line, sizeof line, "%s wins", names[o[0]]);
  text(sz.x * 0.5f, r.y + sz.y * 0.025f, sz.y * 0.055f, rgb(RL_SEAT_RGB[o[0]], 1), line, 1, 0);
  text(r.x + r.w * 0.36f, r.y + sz.y * 0.11f, sz.y * 0.022f, cw_hex(0xd8c8a8ff), "points", 1, 0);
  text(r.x + r.w * 0.5f, r.y + sz.y * 0.11f, sz.y * 0.022f, cw_hex(0xd8c8a8ff), "links", 1, 0);
  text(r.x + r.w * 0.63f, r.y + sz.y * 0.11f, sz.y * 0.022f, cw_hex(0xd8c8a8ff), "money", 1, 0);
  text(r.x + r.w * 0.75f, r.y + sz.y * 0.11f, sz.y * 0.022f, cw_hex(0xd8c8a8ff), "shares", 1, 0);
  text(r.x + r.w * 0.88f, r.y + sz.y * 0.11f, sz.y * 0.022f, cw_hex(0xd8c8a8ff), "total", 1, 0);
  for (int k = 0; k < s->players; k++) {
    int p = o[k];
    float y = r.y + sz.y * (0.15f + 0.07f * (float)k);
    text(r.x + r.w * 0.05f, y, sz.y * 0.034f, rgb(RL_SEAT_RGB[p], 1), names[p], 0, 0);
    snprintf(line, sizeof line, "%d", s->p[p].points);
    text(r.x + r.w * 0.36f, y, sz.y * 0.034f, CW_WHITE, line, 1, 0);
    snprintf(line, sizeof line, "+%d", RL_LINK_POINTS * rl_links_done(s, p));
    text(r.x + r.w * 0.5f, y, sz.y * 0.034f, CW_WHITE, line, 1, 0);
    snprintf(line, sizeof line, "+%d", s->p[p].money / RL_MONEY_PER_POINT);
    text(r.x + r.w * 0.63f, y, sz.y * 0.034f, CW_WHITE, line, 1, 0);
    snprintf(line, sizeof line, "-%d", s->p[p].shares);
    text(r.x + r.w * 0.75f, y, sz.y * 0.034f, CW_WHITE, line, 1, 0);
    snprintf(line, sizeof line, "%d", rl_final_score(s, p));
    text(r.x + r.w * 0.88f, y, sz.y * 0.034f, cw_hex(0xf2d27aff), line, 1, 0);
  }
}

void rl_view_hud(float dt, bool help, const char *const *names) {
  (void)dt;
  if (!RV.g->match) return;
  const rl_state *s = rl_game_state(RV.g);
  cw_vec2 sz = cw_r2d_canvas_size();
  cw_font f = cw_ui_font_sans();
  draw_panels(names);
  bk_deltas_draw(&RV.deltas, f, sz.y * 0.04f);
  int turn = bk_match_turn(RV.g->match);
  cw_color ink = cw_rgba(1, 1, 1, 0.95f);
  cw_vec2 at = cw_v2(sz.x * 0.5f, sz.y * 0.952f);
  /* the band behind the prompt */
  cw_r2d_rect(cw_rect_make(0, sz.y * 0.86f, sz.x, sz.y * 0.14f), cw_rgba(0.06f, 0.045f, 0.03f, 0.6f));
  char line[160];
  if (bk_match_over(RV.g->match)) {
    draw_results(names);
    if (RV.g->over_ticks > 240) cw_prompt_text(f, "{ok} play again", -1, at, sz.y * 0.032f, ink, CW_ALIGN_CENTER);
  } else if (turn >= 0) {
    bool cpu = rl_game_is_cpu(RV.g, turn);
    int tg = bk_select_target(RV.g->sel, turn);
    bk_move m = tg >= 0 ? RV.g->tmove[tg] : BK_MOVE_NONE;
    if (s->phase == RL_PH_BID) {
      if (s->bid_leader >= 0) snprintf(line, sizeof line, "High bid $%d by %s", s->bid, names[s->bid_leader]);
      else snprintf(line, sizeof line, "No bid yet: the last one bidding goes first and pays");
      text(sz.x * 0.5f, sz.y * 0.868f, sz.y * 0.026f, cw_hex(0xf2d27aff), line, 1, 2);
    }
    if (s->phase == RL_PH_FREIGHT && s->drawn_n) {
      snprintf(line, sizeof line, "Freight Office drew a %s good", RL_COLOR_NAMES[s->drawn[0] - 1]);
      text(sz.x * 0.5f, sz.y * 0.868f, sz.y * 0.026f, cw_hex(0xf2d27aff), line, 1, 2);
    }
    if (m != BK_MOVE_NONE) {
      choice_words(s, turn, m, line, sizeof line);
      text(sz.x * 0.5f, sz.y * 0.903f, sz.y * 0.028f, cw_hex(0xf4e7c8ff), line, 1, 0);
    }
    if (cpu) {
      snprintf(line, sizeof line, "%s is thinking", names[turn]);
      text(sz.x * 0.5f, sz.y * 0.942f, sz.y * 0.032f, rgb(RL_SEAT_RGB[turn], 1), line, 1, 0);
    } else {
      cw_prompt_text(f, prompt_for(s), turn, at, sz.y * 0.032f, ink, CW_ALIGN_CENTER);
    }
  }
  if (RV.train_on && RV.train_t < RV.train_time + 0.5f) {
    const rl_map_def *mp = rl_map(s->map);
    snprintf(line, sizeof line, "%s hauls a good to %s", names[RV.train_seat], mp->set_name[RV.train_dest]);
    text(sz.x * 0.5f, sz.y * 0.868f, sz.y * 0.026f, rgb(RL_SEAT_RGB[RV.train_seat], 1), line, 1, 2.5f);
  }
  if (help) draw_help(sz, 1, true);
}

void rl_view_help_page(cw_vec2 canvas) { draw_help(canvas, 1, false); }
