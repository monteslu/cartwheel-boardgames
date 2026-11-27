# Changelog

## 0.3.1

- **Kilowatt Country:** every city name now stands nearest its own city's dot, or has a leader line to it, so the
  names tell the real geography on all seven maps (a name drawn beside a neighbouring city read as that city).
  Leaders keep clear of other cities and their houses. On France, Berck-Plage on the north coast takes the place of
  Auxerre, crowded in beside Paris; on Central Europe, Halle takes Magdeburg's place; so every name finds a clear
  spot. The play test checks this on every map and every set of regions, with a control that must fail.

## 0.3.0

Every game on engine v0.84.0, with How to play and Credits on the title and each move type checked on the pad and
the pointer.

- **Facet:** take two stones of one kind from the pad or the pointer; a warm glow under the cards you can pay for on
  your turn; back steps out of a pick, and a click or tap puts a picked stone back; the cursor stays on the pile you
  took from; the player mats read stones, cards, buys. How to play and Credits on the title.
- **Rose Window:** the favour count is labelled; a used tool can be taken back with the pointer as well as the pad;
  player plates sized to their text at four seats; a stronger Hard that looks ahead by tree search and plays for the
  winning margin. How to play and Credits on the title.
- **Lantern Harbor:** each building's lantern value on a lantern, apart from its coin cost; goods as barrels with
  counts; score popups rise clear of the player names; computer players tuned for the winning margin. How to play
  and Credits on the title.
- **Kilowatt Country:** seven maps of real places in place of the made-up country: United States, Eastern States,
  Great Lakes, France, Iberia, The Balkans and Central Europe, each with thirty real cities at their real spots on a
  night map with real coasts, borders, lakes and rivers (GeoNames and Natural Earth data), links priced by the real
  distance between cities, five regions per map. People choose the map on a setup card before the first round, with
  the pad or the pointer. Smaller city plinths, and a thin leader line from a name that has to stand off from its
  city; no name hides another city on any map. Also: city names placed clear of each other on dark pills, the
  set-aside plants large and face up, larger fuel icons on the plant cards, money changes on the player panels, a
  weaker Easy so the levels stay apart.
- **Smalti:** colour pictures loaded as colour, clicks and taps in the recorded input.
- **Mandible:** a larger, clearer last-move line in words, clicks and taps in the recorded input.

## 0.2.0

Four new games and a readability pass over every game, on engine v0.84.0.

- **Rose Window (new):** draft dice and set coloured glass into a cathedral window, 1 to 4 players.
- **Lantern Harbor (new):** take roles, build up a harbour town and ship goods for lanterns, 2 to 4 players.
- **Fever Line (new):** a cooperative race to cure four fevers on nine maps of real cities, 1 to 4 players.
- **Kilowatt Country (new):** bid on power plants, buy fuel and light a night map, 2 to 4 players.
- **Every game:** How to play and Credits on the title screen, cards and counts readable at the table camera, launcher
  art and a README screenshot.
- **Crokinole:** region scores lettered on the board, a larger score panel, mouse and touch shots in the recorded
  input.
- **First Hearth:** each good shown with its own icon, count and name; cards show what they give as big icons and
  numbers.
- **Smalti:** glassier tiles, breakage penalties written as numbers, a stronger Hard at three and four seats.
- **Facet:** calmer commission cards, larger patron cards.
- **Smokestack:** larger job cards, score changes shown one at a time, a livelier economy.
- **Mandible:** threefold repetition draws, a stronger Hard, a walnut table with a felt mat.
- **Computer players:** the tree-search levels play for the winning margin, with Easy, Normal and Hard measured again.

## 0.1.0

The first release: six board games for one shared screen, each with computer opponents at Easy, Normal and Hard,
played with a gamepad, touch or a mouse.

- **Crokinole:** flick wooden discs toward the centre of a round board, 2 to 4 players (4 as two teams). Inspired by
  the traditional game.
- **First Hearth:** send your clan out to gather, build and grow a stone-age village, 2 to 4 players. Inspired by
  Stone Age.
- **Smalti:** draft coloured glass and set it into a Venetian mosaic, 2 to 4 players. Inspired by Azul.
- **Facet:** collect gems, buy cuts that make the next ones cheaper, and win patrons, 2 to 4 players. Inspired by
  Splendor.
- **Smokestack:** lay track across a hex valley and run goods between cities, 2 to 4 players. Inspired by Steam.
- **Mandible:** surround the rival Empress with carved insect pieces, 2 players or 1 against the computer. Inspired
  by Hive.
- The board-game kit (`games/common/boardkit`): turns with undo, computer players through the engine's game-tree
  search, decks, bags, dice, cards, cursors, the lobby and credits.
