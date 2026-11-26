/* kitdemo match (game.h). */
#include "game.h"

#include <string.h>

#include "cartwheel/shell.h"

const kd_state *kd_game_state(const kd_game *g) { return bk_match_state(g->match); }
bool kd_game_is_cpu(const kd_game *g, int seat) { return g->seat[seat].kind == BK_SEAT_CPU; }

static int target_of(bk_move m) { return m == KD_MV_BANK ? KD_T_BANK : m == KD_MV_ROLL ? KD_T_ROLL : (int)m; }

/* a CPU's next press toward its move: walk its cursor there through the same hops a pad makes, then confirm */
static bool enter_move(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  kd_game *g = user;
  return bk_cpu_press_toward(bk_select_cursors(g->sel), NULL, seat, target_of(move), g->left, g->right, g->up, g->down, g->ok, out);
}

void kd_game_init(kd_game *g) {
  memset(g, 0, sizeof *g);
  static const char *const NAMES[6] = { "left", "right", "up", "down", "ok", "back" };
  static const char *const BINDS[6] = {
    "pad:left, axis:lx-@0.5, key:left, key:a", "pad:right, axis:lx+@0.5, key:right, key:d",
    "pad:up, axis:ly-@0.5, key:up, key:w", "pad:down, axis:ly+@0.5, key:down, key:s",
    "pad:south, key:space, key:return", "pad:east, key:backspace",
  };
  cw_action *acts[6] = { &g->left, &g->right, &g->up, &g->down, &g->ok, &g->back };
  for (int i = 0; i < 6; i++) {
    *acts[i] = cw_action_add(NAMES[i], 0.5f);
    cw_action_bind_text(*acts[i], BINDS[i]);
  }
  g->sel = bk_select_new(&(bk_select_desc){ .left = g->left, .right = g->right, .up = g->up, .down = g->down,
                                            .confirm = g->ok, .back = g->back });
}

void kd_game_start(kd_game *g, uint64_t seed) {
  bk_match_free(g->match);
  for (int s = 0; s < 2; s++) bk_cpu_free(g->cpu[s]), g->cpu[s] = NULL;
  int n = bk_lobby_seats(g->seat, 2);
  if (n != 2) CW_FATAL("kitdemo: %d players (it is a two-player game)", n);
  g->seed = seed;
  kd_state s0;
  kd_setup(&s0);
  g->match = bk_match_new(&KD_RULES, &s0, seed);
  for (int s = 0; s < 2; s++) {
    if (g->seat[s].kind == BK_SEAT_CPU)
      g->cpu[s] = bk_cpu_new(&KD_RULES, &(bk_cpu_desc){ .level = g->seat[s].level, .next_press = enter_move, .user = g,
                                                        .seed = bk_seed_mix(seed, (uint64_t)s) });
    bk_select_place(g->sel, s, KD_T_SLOT0 + 1);
  }
  g->over_ticks = 0;
}

/* which targets the seat to move may choose now */
static void enable_targets(kd_game *g) {
  const kd_state *s = kd_game_state(g);
  for (int t = 0; t < KD_TARGETS; t++) {
    bool on = s->phase == KD_PICK ? (t < KD_SLOTS && s->market[t] != BK_NO_CARD) : s->phase == KD_CHOOSE ? t >= KD_T_BANK : false;
    bk_select_set_enabled(g->sel, t, on);
  }
}

void kd_game_tick(kd_game *g) {
  const kd_state *s = kd_game_state(g);
  if (bk_match_over(g->match)) {
    if (g->over_ticks++ == 0) g->wins[kd_winner(s)]++, g->matches++;
    return;
  }
  int turn = bk_match_turn(g->match);
  cw_shell_set_turn(turn); /* hot seats: the free pads drive the seat whose turn it is */
  enable_targets(g);
  /* CPUs press first (the whole seat each tick), then the rules read every seat alike */
  for (int p = 0; p < 2; p++)
    if (g->cpu[p]) {
      cw_action_value v[4];
      int n = bk_cpu_tick(g->cpu[p], g->match, p, v, 4);
      cw_input_set_actions(p, v, n);
    }
  if (!g->cpu[turn]) bk_select_feed(g->sel, turn); /* this tick's click or tap (recorded by the pointer channel) */
  bk_sel_event e = bk_select_tick(g->sel, turn);
  bool played = false;
  if (e.kind == BK_SEL_CONFIRM && e.target >= 0) {
    bk_move m = s->phase == KD_PICK ? KD_MV_SLOT(e.target) : e.target == KD_T_BANK ? KD_MV_BANK : KD_MV_ROLL;
    played = bk_match_play(g->match, m);
    if (played) {
      if (g->cpu[turn]) g->cpu_moves++;
      else if (e.pointer) g->pointer_picks++;
      else g->pad_moves++;
      if (kd_game_state(g)->phase == KD_CHOOSE) bk_select_place(g->sel, turn, KD_T_BANK);
    }
  } else if (e.kind == BK_SEL_BACK) {
    int slot = s->slot; /* the card taken this turn goes back to its slot: the cursor with it */
    if (bk_match_undo(g->match)) {
      g->undos++;
      bk_select_place(g->sel, turn, slot >= 0 ? slot : KD_T_SLOT0);
    }
  }
  s = kd_game_state(g);
  if (!bk_match_over(g->match) && s->phase == KD_PICK && bk_match_turn(g->match) != turn)
    for (int t = 0; t < KD_SLOTS; t++) /* the next seat's cursor starts on a card */
      if (s->market[t] != BK_NO_CARD) { bk_select_place(g->sel, bk_match_turn(g->match), t); break; }
}
