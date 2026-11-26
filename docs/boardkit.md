# The board-game kit

`games/common/boardkit` is the code every game in this repository shares. It sits on top of the engine's own
board-game modules (picking, cursors, hex grids, seat actions, CPU and hot seats in the shell, game-tree search)
and adds what the games would otherwise each write again. `games/kitdemo` uses every part and is the layout to
copy.

A game declares its apps with `bk_add_app` (`cw_add_app` plus the kit's sources) and includes
`"boardkit/<header>.h"`.

## The shape of a game

```
games/<codename>/src/rules.c   pure rules: a bk_game (the engine's cw_search_game), no engine calls but cw_rng / cw_hash
games/<codename>/src/game.c    the match: seats, bk_match, bk_cpu per CPU seat, actions turned into moves
games/<codename>/src/main.c    the shell, the 3D table, animation, the HUD
```

Rules read actions only, for every seat, plus the pointer through the engine's recorded pointer channel. A CPU
presses the same actions a pad does and a click or tap goes into the recording with them (`cw_input_pointer_channel`),
so the engine's input recording, a replay and the input hash carry every move whoever made it. kitdemo's `replay` debug command proves it for a match (`kd.replay_match`, `kd.replay_hash_match`).

## Headers

| Header | What it gives | Main calls |
|---|---|---|
| `bk_game.h` | the rules interface: `bk_game` = `cw_search_game`, `bk_move` = `cw_search_move`, `BK_CHANCE`, `BK_WIN` / `BK_LOSS`; the state rules | `bk_hash`, `bk_is_legal`, `bk_apply_copy`, `bk_settle`, `bk_seed_mix`, `bk_move_pack` / `bk_move_field` |
| `bk_match.h` | the turn and phase sequencer: plays moves, resolves chance with its own seeded rng, undo within a seat's own turn (never past a roll or into another turn), a log for the view | `bk_match_new`, `_play`, `_undo`, `_can_undo`, `_seal`, `_turn`, `_over`, `_hash`, `_log`, `_set_legal` |
| `bk_cpu.h` | CPU seats: the engine's search over ticks, then the move entered by pressing the pad's actions | `bk_cpu_new`, `bk_cpu_tick` (to `cw_input_set_actions`), `bk_cpu_press_toward`, `bk_cpu_intent` |
| `bk_pieces.h` | decks (a draw is a uniform pick when it happens), market rows, draw bags, dice, and each as weighted outcomes for search | `bk_deck_draw`, `bk_deck_outcomes`, `bk_market_refill`, `bk_bag_draw`, `bk_bag_outcomes`, `bk_die_roll`, `bk_die_outcomes`, `bk_shuffle` |
| `bk_select.h` | choosing among targets on the table with a pad, a mouse or a finger, on the engine's cursors: legal targets only, pointer taps resolved in the tick from the recorded pointer, tap to look and tap again to play, eased highlights | `bk_select_new`, `_set_enabled`, `_pointer` (per frame), `_feed` and `_tick` (per tick), `_glow` |
| `bk_view.h` | counters that count to their new value, "+3" score deltas that rise and fade, ghost previews (the mesh nodes' opacity), a die that tumbles onto the rolled face | `bk_counter_*`, `bk_deltas_*`, `bk_ghost_set`, `bk_die_throw_make`, `bk_die_pose`, `bk_die_node` |
| `bk_card.h` | cards: faces drawn once by the game's painter into 2D render targets worn in 3D (`cw_target_texture3d`), rounded slabs as procedural meshes, flip and fan poses | `bk_cards_init`, `bk_card_new`, `bk_card_set_front`, `bk_card_flip_pose`, `bk_card_fan`, `bk_pose_approach` |
| `bk_shell.h` | the lobby (seats as a pad, a hot seat, or a CPU at three levels, on the shell's join screen), the title's How to play and Credits pages, the test start in the menu tick | `bk_lobby_desc`, `bk_title_desc`, `bk_lobby_seats`, `bk_lobby_test_start`, `bk_seat_color`, `bk_credits_page` |

## Rules: a bk_game

Write the rules as the engine's `cw_search_game` (read `cartwheel/search.h` and the engine's
`docs/guides/game-search.md`). The kit's rules on top:

- **A flat state.** No pointers, padding zeroed (`memset` before filling), fixed size. The match copies it for
  undo and the search for its nodes; the hash is over its bytes unless the game gives `hash`.
- **Chance is a node, not a hidden order.** A roll or a draw is `turn() == BK_CHANCE` with `outcomes()` (and/or
  `chance()`). Decks are unordered piles drawn at random when drawn (`bk_deck_draw`, `bk_deck_outcomes`), so a
  CPU never knows the next card. The match resolves chance before a CPU thinks.
- **One decision per move.** A turn of several choices is several moves by the same seat; `turn()` says who.
  Undo works decision by decision inside a turn.
- **Terminal values** in the `BK_WIN` / `BK_LOSS` bands, a win less the moves played.
- **Never a clock or a global rng** in rules.

## The match

```c
kd_state s0; kd_setup(&s0);
bk_match *m = bk_match_new(&KD_RULES, &s0, seed);   /* opening chance resolved */
bk_match_play(m, move);                              /* false when illegal; chance after it resolved */
if (bk_match_can_undo(m)) bk_match_undo(m);          /* a person's own decisions this turn */
```

A continuous move (a flick's angle and power) cannot be listed by `moves()`: `bk_match_set_legal(m, legal, user)`
gives the match a kit-side check to accept it instead (the CPU still searches over a sample the game lists).

`bk_match_log` lists moves, chance outcomes (the outcome code when the game resolves through `outcomes()`) and
undos with the state hash after each; the view keeps the index it has shown and animates what is new (kitdemo
throws the die from a roll's log entry).

## CPU seats

```c
cpu = bk_cpu_new(&RULES, &(bk_cpu_desc){ .level = lv, .next_press = enter_move, .user = g, .seed = s });
/* fixed_update, before the rules read input, every tick for every CPU seat: */
int n = bk_cpu_tick(cpu, match, seat, v, 4);
cw_input_set_actions(seat, v, n);
```

`next_press` turns the chosen move into the next press: for a cursor game it is one call to
`bk_cpu_press_toward`, which tries the eight stick ways through the engine's hop and walks the cursor there,
then confirms. When the cursors refuse some targets (`cw_cursors_set_allow`, or `bk_select_set_enabled` through
`bk_select_enabled`), use `bk_cpu_press_toward_allow` with the same callback, so the CPU predicts the hops a pad
really makes and never plans a step onto a refused target. Levels come from `cw_search_level_options` (Easy: depth 2 and seeded mistakes, Normal: depth 4,
Hard: the full search); a game tunes with `bk_cpu_desc.options`. An MCTS game sets `reward_scale` in its options: the
default reward is the margin (a logistic of the seat's lead over the best other seat), and the scale is the lead, in
the game's evaluate units, worth 0.73; set it to about a fifth of a typical winning margin in that game. Each decision's search is seeded from the
match seed, the seat and the state hash. A CPU thinks at least `think_ticks` and waits `press_gap` between
presses so people can follow it. A move that never lands stops the game (`CW_FATAL`) after 400 presses.

## Selecting with a pad and a pointer

```c
/* init */  sel = bk_select_new(&(bk_select_desc){ .left = L, .right = R, .up = U, .down = D, .confirm = OK, .back = BACK });
            cw_cursors_set_nodes(bk_select_cursors(sel), targets, n);
/* tick */  bk_select_set_enabled(sel, t, legal);     /* only legal choices */
            bk_select_feed(sel, seat);                  /* this tick's click or tap (cw_input_pointer) on a target */
            bk_sel_event e = bk_select_tick(sel, seat); /* MOVED, CONFIRM, BACK or POINTED */
/* frame */ bk_select_pointer(sel, seat, tree);         /* the hover mark (drawing only) */
```

Reset every seat's cursor when a match starts (`bk_select_place(sel, seat, start)` for each seat, or `-1` for no
target), before the first tick. The cursor is not part of the rules' state, but the hops it makes are: a cursor left
where the last match ended makes the next match, or a replay of a recording, press differently from the first run,
and the replay diverges.

A first tap on a target moves the cursor there (show a ghost of the move); a tap on the target already chosen
confirms it. `bk_select_new` switches the engine's pointer channel on, so make it in init, before the match's
recording begins: the channel records each tick's pointer (where a press landed) with the actions, and
`bk_select_feed` resolves it to a target in the tick, so a replay picks exactly what the match picked. The engine's
gate control `CW_TEST_POINTER_UNRECORDED=1` leaves the pointer out of a recording (its replay must then differ).

## Animation and drawing

- Counters: `bk_counter_set` when the rules change a number; `bk_counter_value` while drawing.
- Score deltas: `bk_deltas_add` at the canvas point where the change happened (`cw_camera3d_world_to_screen`
  for a 3D spot), drawn inside a 2D pass. They fade in and out, never flash.
- Ghosts: build a copy of the piece, `bk_ghost_set(root, opacity)` it (any frame: it sets each MeshInstance3D's
  `opacity` and turns its shadow off), move it where the move would put it and show it while the player is
  choosing. A ghosted card keeps its see-through look when `bk_card_set_front` changes its face (kitdemo's ghost
  shows whichever market card the cursor is on). The opacity has no tint: a ghost is the piece, see-through.
- Dice: `bk_die_throw_make(from, to, face, seed)` and `bk_die_pose(throw, t, ...)` each frame; the die tumbles
  and lands on the face the rules rolled. Seed the throw from the log entry's hash so a replay throws alike.
- Cards: `bk_cards_init(tree, paint, user)` once; the painter draws a face id with `cw_r2d_*` in face pixels and
  runs once per face, in the frame's update (a kit node that runs on every screen), into a 2D render target that
  three.c wears through `cw_target_texture3d`; `bk_card_face_refresh` paints a face again. `bk_card_new` makes a slab with a front, a back and an edge. Poses ease with
  `bk_pose_approach`; `bk_card_flip_pose` and `bk_card_fan` give flips and hands.
- Program tables: one per game, shared by its apps (`PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/<game>.txt` on
  `<game>_auto`, see kitdemo's CMakeLists.txt). Ghosts and card faces are material states of their own. Run the programs survey with them on
  screen (kitdemo uses `--scenes KITDEMO_SEATS=p:c1` so a person's ghost shows) and then `--carts`.

## Lobby, title and credits

`bk_lobby_desc(&shell_desc, players, names, colours)` sets the shell up for one to four seats, each a person on a
pad, a hot seat or a CPU. After the shell's `start` hook, `bk_lobby_seats` says which.

`bk_title_desc(&shell_desc, &title)` (after `bk_lobby_desc`, with a static `bk_title`) puts "How to play" and
"Credits" on the title between Play and Settings. Each opens a page (the shell's `CW_SHELL_PAGE`): How to play is
drawn by the game's `how_to_play(canvas, user)` over its backdrop, Credits by the kit (`bk_credits_page`). Back,
accept or a tap returns to the title. It also wires the shell's menu tick (`cw_shell_desc.menu_update`): with
`test_env = "<GAME>_SEATS"` it starts the match from that test switch (`p`, `h`, `c0`..`c2`, comma or colon
separated; `bk_lobby_started_by_test` says it did), then calls the game's own `menu_update` (a self-playing cart's
start). The menu tick runs on every screen the menus own, also in a scene app, whose tree (and the game's
`fixed_update`) is paused outside play. The credits card is the one place a real game is named:
`inspired_by = "inspired by <game> (<designer>)"`. A game may still open it over play from a pause item
(`bk_credits_open`, `bk_credits_tick`, `bk_credits_draw`).

## Verifying a game through romdev

`tests/kitdemo-play.mjs` is the pattern: `tests/lib/romdev.mjs` opens the cart on the romdev server (GL or WebGPU),
sends test switches with the `env` request, drives the pad with one input timeline a span and the pointer with
`input({op:'pointer'})`, reads the game's debug fields, screenshots, and replays the match from its recording.
The game publishes where its targets sit on the canvas (`kd.target_px`) so a test can tap them.

## Tests

`node tests/boardkit.mjs` builds and runs `games/common/boardkit/tests/test_*.c` headless. Each prints a line
per check and at least one `CONTROL ... caught` for a control that must fail; the runner fails a test without
one. Add a test with each new part.
