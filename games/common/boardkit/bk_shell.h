/* boardkit: the standard lobby and the credits screen, on the engine's game shell (cartwheel/shell.h).
 *
 * Lobby: every game seats one to four players, each a person on a pad of their own, a person passing a pad
 * (a hot seat), or a CPU at Easy, Normal or Hard. The shell's join screen already offers all three
 * (cpu_seats, hot_seats); bk_lobby_desc fills its seat names and colours, and bk_lobby_seats reads the
 * match's seats back after the shell's start hook. A test switch (cw_test_env) skips the menus:
 *   <ENV>=c2,c2      two Hard CPUs        <ENV>=p,c1   a person on a pad and a Normal CPU
 *   <ENV>=h,h,c0     two hot seats and an Easy CPU
 * (":" separates as well as ",": programs.mjs --scenes splits its values at commas, so a survey passes p:c1)
 * which is how a self-playing <game>_auto cart starts. The switch is read in the shell's menu tick
 * (cw_shell_desc.menu_update): bk_title_desc wires it.
 *
 * The title (bk_title_desc): two items of the game's between Play and Settings, "How to play" and "Credits", each a
 * page the shell opens (CW_SHELL_PAGE): How to play is the game's own drawing, Credits the kit's card.
 *
 * Credits: a full-screen card: the game's title, its lines (code, art, fonts, sounds and their licences), and the
 * one place a real game may be named: an "inspired by <game>" line. On the title it is a page (bk_credits_page);
 * a game may also open it over play (bk_credits_open, from a pause item), where back or confirm closes it. */
#ifndef BK_SHELL_H
#define BK_SHELL_H

#include <stdbool.h>

#include "cartwheel/input.h"
#include "cartwheel/render2d.h"
#include "cartwheel/shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BK_SEAT_EMPTY, BK_SEAT_PAD, BK_SEAT_HOT, BK_SEAT_CPU } bk_seat_kind;
typedef struct bk_seat { bk_seat_kind kind; int level; } bk_seat; /* level: 0..2 for a CPU */

/* Fill a shell desc for a 1..max_players (<= 4) seat board game: CPU and hot seats on, the seat names and
 * colours given (NULL: "Player 1".. and the kit's four seat colours). */
void bk_lobby_desc(cw_shell_desc *d, int max_players, const char *const *names, const cw_color *colors);
/* The kit's four seat colours (warm red, blue, green, gold: told apart by people with the commonest colour
 * blindness by lightness as well as hue). */
cw_color bk_seat_color(int seat);
/* After the shell's start hook: each seat 0..players-1; returns players. */
int bk_lobby_seats(bk_seat *out, int cap);
/* While the title shows: when the environment switch `env` is set, start the match with those seats (once). True
 * when it started one. Call it from the shell's menu tick (cw_shell_desc.menu_update, which bk_title_desc wires):
 * in a scene app (cw_scene_app) the tree, and with it the game's fixed_update, is paused outside play. */
bool bk_lobby_test_start(const char *env);
/* True once bk_lobby_test_start started the match from its switch. */
bool bk_lobby_started_by_test(void);

/* Art frames: true while the test switch BK_NO_HUD is set (not "0"). A game skips its HUD, prompts, score
 * popups and cursor marks then, so a launcher art script (tools/make-art.sh) can capture covers, backgrounds and
 * title images of the table alone: draw the HUD only when cw_shell_hud_shown() && !bk_hud_hidden(). Labelled
 * screenshots keep their HUD (capture them without the switch). */
bool bk_hud_hidden(void);

/* ── credits ── */
typedef struct bk_credits bk_credits;
struct bk_credits {
  const char *title;          /* the game's own title */
  const char *inspired_by;    /* "inspired by <game> (<designer>)": the only real game name in the game */
  const char *const *lines;   /* NULL-terminated: who made what, and every licence */
  cw_font font;               /* (cw_ui_font_sans) */
};
typedef struct bk_credits_view { bool open; float t; } bk_credits_view;
void bk_credits_open(bk_credits_view *v);
/* In fixed_update while open: confirm or back (any player) closes it; true while it stays open. */
bool bk_credits_tick(bk_credits_view *v, cw_action confirm, cw_action back, int players);
/* In render, in a cw_r2d pass, while open: the card over the table, eased in. */
void bk_credits_draw(bk_credits_view *v, const bk_credits *c, float dt);
/* The card as a title page (cw_shell_desc.draw_page): drawn whole, no close prompt (the shell draws the way back). */
void bk_credits_page(const bk_credits *c, cw_vec2 canvas);

/* ── the title ── */
typedef struct bk_title {
  const char *test_env;      /* "<GAME>_SEATS": the menu tick starts the match from it (bk_lobby_test_start); NULL none */
  const bk_credits *credits; /* the Credits page (NULL: no Credits item) */
  /* draws the How to play page in canvas units, in the shell's 2D pass over draw_background (NULL: no How to play
   * item). A page that turns reads ui_left / ui_right in menu_update. */
  void (*how_to_play)(cw_vec2 canvas, void *user);
  /* the game's own menu tick, after the kit's (cw_shell_desc.menu_update; an autopilot's start goes here) */
  void (*menu_update)(cw_shell_screen screen, void *user);
  void *user;
} bk_title;
/* Fill a shell desc's title items ("How to play", "Credits"), draw_page and menu_update from t (kept by pointer:
 * static). Call after bk_lobby_desc, before cw_shell_init. */
void bk_title_desc(cw_shell_desc *d, const bk_title *t);

#ifdef __cplusplus
}
#endif
#endif
