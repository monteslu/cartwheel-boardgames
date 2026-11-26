# Working in cartwheel-boardgames

Original board games built with [cartwheel-engine](https://github.com/monteslu/cartwheel-engine), one directory
each under `games/`, sharing one board-game kit (`games/common/boardkit`, guide: `docs/boardkit.md`). They build
against the prebuilt engine SDKs pinned in `scripts/deps.lock` and ship as dual GL + WebGPU carts (`.wasc`).

Every game: one shared screen (up to four players, never a second screen or hidden hands), gamepad first, touch
second, the keyboard only a fallback; playable solo against CPU seats; little or no hidden information.

## Rules for every game

- Our own names, art, text and layouts. No commercial game names anywhere (code, docs, commits, file names, art,
  in-game text) except one line on the game's credits screen and in its README credits section:
  "inspired by <game>". Never in a title.
- Art is made by scripts committed next to their output (JavaScript, no Python).
- No strobe effects (flashes swell and fade) and no screen shake (rumble the pad instead).
- At most four seats.
- Rules read actions only, for every seat: a CPU presses the same actions a pad does (`cw_input_set_actions`, via
  the kit's `bk_cpu`), so recordings, replays and the input hash carry CPU moves.
- Fail fast: never swallow an error. Every check has a control that must fail.

## Adding a game

1. `games/<codename>/CMakeLists.txt` declaring its apps with `bk_add_app` (`cw_add_app` plus the kit), as
   `games/kitdemo/CMakeLists.txt` does: the game, and `<codename>_auto` (the self-playing cart that cart-smoke runs:
   CPU seats on every seat). In a scene app the tree, and with it the game's fixed_update, is paused outside play:
   start test seats from the shell's menu tick (`bk_title_desc` with `test_env`, which also puts How to play and
   Credits on the title), as kitdemo does.
2. `games/<codename>/src/` split as kitdemo is (read `docs/boardkit.md` first): `rules.c` (pure, a `bk_game`), `game.c` (the match: turns, CPU
   seats, events), `main.c` (shell, input, view).
3. `games/<codename>/assets/` (scenes, fonts) and `programs/<codename>.txt` (three.c program table: run
   `node deps/cartwheel-sdk/linux-x64/tools/programs.mjs <app> --build build/native` under the GPU lock with
   `CW_PROJECT_ROOT` set to this repo, re-run it with `--carts`, rebuild and commit the table and its generated C).
   Each app takes the table named after it by default, so give `<codename>_auto` (and any other app of the game)
   the game's one table with `PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/<codename>.txt` in its `bk_add_app`, as
   kitdemo does; survey every app together (`programs.mjs <codename> <codename>_auto ...`) so the one table holds
   what each draws, with a person's seat in the survey (`--scenes <GAME>_SEATS=p:c1`) when people see things CPUs
   never show (ghost previews). Then survey the carts too: `programs.mjs --carts <codename> <codename>_auto` (it finds
   the project and its apps itself).
4. Append the codename to `games.txt` (the one game list: CI, cart-smoke, the prompt check and the release read
   it).
5. Tests in `tests/<codename>-*.mjs` / `tests/<codename>-gates.json`, each with a control that must fail.
6. A README section and a `games/<codename>/CREDITS` with the "inspired by" line and every licence.

## Build and test

| Command | Does |
|---|---|
| `scripts/fetch-deps.sh` | fetches the pinned SDKs and the engine source at the same commit (its test tools) into `deps/` |
| `scripts/build.sh [app ...]` | builds `build/native` and `build/cart-wgpu` (the cart that ships); `TARGETS=native` for one tree |
| `scripts/games.sh [targets]` | the game list (`games.txt`), or the ninja targets CI builds |
| `node tests/boardkit.mjs` | the kit's headless tests, each with a control that must fail |
| `node tests/gate.mjs --gates-file <game>-gates.json [gate ...]` | a game's gates (the engine's gate runner) |
| `node tests/cart-smoke.mjs [game ...]` | a short headless run of each game's cart (what CI runs) |
| `node tests/kitdemo-play.mjs [--gpu gl,webgpu]` | kitdemo through romdev on GL and WebGPU: pad, CPU, pointer, undo, the die, a replay of the match (and its control), pictures in out/kitdemo |
| `node tests/cart-version.mjs [game ...]` | every cart's manifest carries this build's version, with a control |
| `node deps/cartwheel-engine/tools/check-prompts.mjs games/<game>` | the prompt check below (CI runs it for every game) |

Anything that uses the GPU goes through the engine's GPU lock, headless:
`node deps/cartwheel-engine/tools/with-gpu-suite.mjs <name> <command>`. Split long suites into short lock holds.
Verify a game through the romdev MCP server on 127.0.0.1:7331 (loadMedia the cart, step frames with an input
schedule, screenshot) on GL and on WebGPU, and look at the pictures.

In a worktree run `scripts/fetch-deps.sh` (deps are per worktree). If a download fails (a release whose files are
not up yet), copy `deps/` from the main checkout with `cp -a` (never a symlink: a linked deps/ follows the other
checkout's pin), then run fetch-deps again so it checks every sha256.

The repository pins one engine release. When you move the pin, change the engine commit and every SDK line (with
its sha256) in `scripts/deps.lock`, run `scripts/fetch-deps.sh`, and re-run every game's gates.

Delete extra builds and frames as you go: the disk is shared.

## Launcher art and the README screenshot

Every release game (games.txt without `dev`) ships launcher art, checked by `node tests/cart-art.mjs` (CI runs it;
a manifest without its cover is the control that must fail):

- `games/<game>/art/`: `icon.png`, `cover.jpg` (box art with the name across the top: `scripts/cover-name.sh`),
  `background.jpg`, `logo.png`, `title.jpg` and 3 to 6 screenshots, named by `art/art.json` with a label each.
  `bk_add_app` takes `ART ${CMAKE_CURRENT_SOURCE_DIR}/art`, a DESCRIPTION of 300 characters or less and an AUTHOR.
- All of it is made by a committed script, `games/<game>/tools/make-art.sh`: frames captured through romdev
  (`scripts/romdev-shot.mjs`, under the GPU lock) and composed with ImageMagick. A logo font lives in
  `games/<game>/tools/fonts/` with its licence (it is not packed into the cart). Capture the cover, background,
  title and icon frames with the test switch `BK_NO_HUD=1` (the game draws its HUD only when
  `cw_shell_hud_shown() && !bk_hud_hidden()`), framed on the pieces rather than a board edge; labelled screenshots
  keep their HUD.
- `docs/screenshots/<game>.jpg`: a real mid-game frame (16:9), shown in the game's README section with alt text
  describing the scene.
- Look at every image: the cover's name must read at a launcher grid's size, and the screenshots must show the
  game at its best.

## CI and releases

`.github/workflows/ci.yml` (push to main, pull requests) builds every game in `games.txt` as a cart, runs the
prompt check, the kit tests, cart-version and cart-smoke. `.github/workflows/release.yml` (a `v*` tag) waits for
the commit's green run on main, builds the release carts and attaches them to the tag's GitHub release, with the
version's `CHANGELOG.md` section as notes. CHANGELOG.md holds releases only (no Unreleased section).

## Showing controls

Prompts use the engine's button prompts (`cartwheel/prompt.h`): text names the
action, and the engine draws the glyph for the device each player holds. A pad
gets a diamond of four dots with the button to press solid, a keyboard a keycap,
a touch screen the on-screen control.

- Never write a button's name or colour in text or art: `{jump}: hop a gap`, not
  a letter or "the green button". Different pads put different letters and
  colours on the same button, and a keyboard or phone has none. The word "Start"
  is the one exception.
- Text says what the button does; the glyph says which one. Draw text that holds
  prompts with `cw_prompt_text` (markup: `{action}`, `{left|right}` for a pair,
  `{pad:west}` for one physical input), and single glyphs with
  `cw_prompt_action` or `cw_prompt_binding`.
- Colours belong to the game. A green lane is fine; telling the player to press
  green is not.
- When a design is about position (lanes, answer slots), lay it out where the
  buttons sit: west, south, east left to right, or a diamond.
- Confirm is south and back is east on every pad.
- Do not track the last device yourself: ask `cw_prompt_device_of`.
- Pass the seat to a prompt that one player reads (a split-screen HUD, a ready
  slot) and `-1` to one every seat reads (a title, an instructions card).
- Every pad has four face buttons, a d-pad, two shoulders, Start and Back.
  Sticks and analog triggers are not always there: anything a game needs must
  also have a binding on a button or the d-pad.

`check-prompts.mjs` fails on the usual slips in string literals ("A: jump",
"(B)", "press X", "LB / RB", "RT: gas"). It cannot see letters drawn in art or in
code, so review those by hand, and move keyboard hints such as "W / S: gas" to
the markup too. A line that really means it carries `prompt-ok: <reason>` on it
or on the line above.
