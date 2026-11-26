/* boardkit: lobby and credits (bk_shell.h). */
#include "bk_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/ui.h"

static const char *const DEFAULT_NAMES[4] = { "Player 1", "Player 2", "Player 3", "Player 4" };

cw_color bk_seat_color(int seat) {
  static const uint32_t C[4] = { 0xd9573fff, 0x3f7fe0ff, 0x4dbb6aff, 0xe6bc3cff };
  return cw_hex(C[((seat % 4) + 4) % 4]);
}

void bk_lobby_desc(cw_shell_desc *d, int max_players, const char *const *names, const cw_color *colors) {
  if (max_players < 1 || max_players > 4) CW_FATAL("bk_lobby_desc: %d players (1..4: the series' cap)", max_players);
  d->max_players = max_players;
  d->shared_screen = true;
  d->cpu_seats = max_players > 1;
  d->hot_seats = max_players > 1;
  for (int s = 0; s < 4; s++) {
    d->seat_names[s] = names && names[s] ? names[s] : DEFAULT_NAMES[s];
    d->seat_colors[s] = colors ? colors[s] : bk_seat_color(s);
  }
}

int bk_lobby_seats(bk_seat *out, int cap) {
  int n = cw_shell_players();
  if (n > 4) CW_FATAL("bk_lobby_seats: %d players (the series' cap is 4)", n);
  for (int s = 0; s < n && s < cap; s++) {
    cw_shell_cpu_level lv;
    if (!cw_shell_seat_taken(s)) out[s] = (bk_seat){ BK_SEAT_EMPTY, 0 };
    else if (cw_shell_seat_cpu(s, &lv)) out[s] = (bk_seat){ BK_SEAT_CPU, (int)lv };
    else if (cw_shell_seat_hot(s)) out[s] = (bk_seat){ BK_SEAT_HOT, 0 };
    else out[s] = (bk_seat){ BK_SEAT_PAD, 0 };
  }
  return n;
}

static bool test_started;
bool bk_lobby_started_by_test(void) { return test_started; }

bool bk_lobby_test_start(const char *env) {
  if (test_started || cw_shell_screen_now() != CW_SHELL_TITLE) return false;
  const char *v = cw_test_env(env);
  if (!v || !*v) return false;
  int seats[4], n = 0;
  for (const char *c = v; *c && n < 4;) {
    if (*c == 'p') seats[n++] = CW_SHELL_SEAT_PAD;
    else if (*c == 'h') seats[n++] = CW_SHELL_SEAT_HOT;
    else if (*c == 'c' && c[1] >= '0' && c[1] <= '2') seats[n++] = c[1] - '0', c++;
    else CW_FATAL("bk_lobby_test_start: %s=%s: seats are p (pad), h (hot seat), c0..c2 (CPU), separated by , or :", env, v);
    c++;
    if (*c == ',' || *c == ':') c++;
  }
  test_started = true;
  cw_info("boardkit: %s=%s: starting with %d seats", env, v, n);
  cw_shell_start_seats(n, seats);
  return true;
}

void bk_credits_open(bk_credits_view *v) { v->open = true; v->t = 0; }

bool bk_credits_tick(bk_credits_view *v, cw_action confirm, cw_action back, int players) {
  if (!v->open) return false;
  for (int p = 0; p < players && p < 4; p++)
    if (cw_action_just_pressed(p, confirm) || (back >= 0 && cw_action_just_pressed(p, back))) v->open = false;
  return v->open;
}

static void credits_card(const bk_credits *c, cw_vec2 sz, float a, bool prompt) {
  cw_font f = c->font.id ? c->font : cw_ui_font_sans();
  cw_r2d_rect(cw_rect_make(0, 0, sz.x, sz.y), cw_rgba(0.02f, 0.02f, 0.04f, 0.72f * a));
  float w = sz.x * 0.62f, h = sz.y * 0.78f, x = (sz.x - w) * 0.5f, y = (sz.y - h) * 0.5f + (1 - a) * sz.y * 0.03f;
  cw_r2d_rect(cw_rect_make(x, y, w, h), cw_rgba(0.95f, 0.92f, 0.85f, a));
  cw_r2d_rect_lines(cw_rect_make(x + 8, y + 8, w - 16, h - 16), 2, cw_rgba(0.45f, 0.33f, 0.2f, a));
  float ts = sz.y * 0.055f, ls = sz.y * 0.028f;
  cw_color ink = cw_rgba(0.13f, 0.1f, 0.08f, a);
  cw_text_style st = { .size = ts, .color = ink, .wrap_width = w * 0.86f, .align = CW_ALIGN_CENTER, .own_backdrop = true };
  float cy = y + h * 0.07f;
  cw_text_metrics m = cw_r2d_text(f, c->title, cw_v2(x + w * 0.07f, cy), &st);
  cy += m.height + ls;
  if (c->inspired_by && *c->inspired_by) {
    st.size = ls * 1.1f;
    st.color = cw_rgba(0.42f, 0.27f, 0.14f, a);
    m = cw_r2d_text(f, c->inspired_by, cw_v2(x + w * 0.07f, cy), &st);
    cy += m.height + ls;
  }
  st.size = ls;
  st.color = ink;
  for (int i = 0; c->lines && c->lines[i]; i++) {
    m = cw_r2d_text(f, c->lines[i], cw_v2(x + w * 0.07f, cy), &st);
    cy += m.height + ls * 0.45f;
  }
  if (prompt) cw_prompt_text(f, "{ui_accept} close", -1, cw_v2(sz.x * 0.5f, y + h - ls * 2.2f), ls, ink, CW_ALIGN_CENTER);
}

void bk_credits_draw(bk_credits_view *v, const bk_credits *c, float dt) {
  if (!v->open) return;
  v->t += dt * 3;
  if (v->t > 1) v->t = 1;
  credits_card(c, cw_r2d_canvas_size(), v->t * v->t * (3 - 2 * v->t), true);
}

void bk_credits_page(const bk_credits *c, cw_vec2 canvas) { credits_card(c, canvas, 1, false); }

/* ── the title ── */
static const bk_title *TITLE;

static void title_page(int index, cw_vec2 canvas, void *user) {
  (void)user;
  if (index == 0 && TITLE->how_to_play) TITLE->how_to_play(canvas, TITLE->user);
  else if (index == 1 && TITLE->credits) bk_credits_page(TITLE->credits, canvas);
  else CW_FATAL("bk_title: a page %d with nothing to draw it", index);
}

static void title_menu_update(cw_shell_screen screen, void *user) {
  (void)user;
  if (TITLE->test_env && screen == CW_SHELL_TITLE) bk_lobby_test_start(TITLE->test_env);
  if (TITLE->menu_update) TITLE->menu_update(screen, TITLE->user);
}

void bk_title_desc(cw_shell_desc *d, const bk_title *t) {
  if (!t) CW_FATAL("bk_title_desc: no bk_title");
  TITLE = t;
  d->title_items[0] = t->how_to_play ? "How to play" : NULL;
  d->title_items[1] = t->credits ? "Credits" : NULL;
  d->draw_page = title_page;
  d->menu_update = title_menu_update;
}

bool bk_hud_hidden(void) {
  const char *v = cw_test_env("BK_NO_HUD");
  return v && *v && strcmp(v, "0") != 0;
}
