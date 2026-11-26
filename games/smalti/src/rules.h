/* Smalti: the rules, pure and headless.
 *
 * Players draft coloured glass tiles from round trays (one tray per player pair plus one, see below) and from
 * the shared centre, fill five staging rows of growing length, and at the end of each round move one tile of
 * every full staging row onto their 5x5 mosaic, scoring it by the run of tiles it joins. Tiles that do not fit
 * drop to the breakage strip and cost points. The game ends after the round in which someone completes a
 * horizontal mosaic row; finished rows, columns and colour sets then score bonuses.
 *
 * The state is flat and fixed size (bk_game rules): counts, not tile lists. The only randomness is the bag
 * draw that fills the trays at the start of each round, a chance node (turn() == BK_CHANCE).
 *
 * Moves: bk_move_pack(source, colour, line). source 0..trays-1 is a tray, MS_CENTRE the centre; colour
 * 0..4; line 0..4 a staging row, MS_FLOOR straight to the breakage strip. */
#ifndef SMALTI_RULES_H
#define SMALTI_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "boardkit/bk_game.h"

#define MS_COLOURS 5
#define MS_SIZE 5             /* mosaic is MS_SIZE x MS_SIZE */
#define MS_MAX_TRAYS 9
#define MS_TRAY_TILES 4
#define MS_TILES_PER_COLOUR 20
#define MS_FLOOR_SLOTS 7
#define MS_CENTRE 9           /* source index of the centre */
#define MS_FLOOR 5            /* line index of the breakage strip */
#define MS_MAX_ROUNDS 40      /* safety cap: a game that has not ended by then ends (never seen in real play) */

/* end bonuses */
#define MS_BONUS_ROW 2
#define MS_BONUS_COL 7
#define MS_BONUS_COLOUR 10

enum { MS_PHASE_REFILL = 0, MS_PHASE_DRAFT = 1, MS_PHASE_OVER = 2 };

typedef struct ms_player {
  int16_t score;
  uint8_t line_colour[MS_SIZE]; /* colour of staging row r, meaningful when line_count[r] > 0 */
  uint8_t line_count[MS_SIZE];  /* 0..r+1 */
  uint8_t floor_tiles;          /* tiles on the breakage strip (the marker not included) */
  uint8_t floor_marker;         /* holds the first-player marker */
  uint8_t floor_colour[MS_COLOURS]; /* colours of the floor tiles (they go back to the box) */
  uint32_t wall;                /* bit r*5+c: mosaic cell (row r, column c) is set */
} ms_player;

typedef struct ms_state {
  cw_rng rng;                   /* the match's draws */
  uint8_t players;              /* 2..4 */
  uint8_t trays;                /* 5, 7, 9 */
  uint8_t phase;
  uint8_t seat;                 /* seat to move in the draft */
  uint8_t first_next;           /* who starts the next round (holder of the marker) */
  uint8_t centre_marker;        /* the first-player marker is still in the centre */
  uint8_t round;                /* rounds started, 1-based once the first refill happened */
  uint8_t ended_by_cap;
  uint8_t tray[MS_MAX_TRAYS][MS_COLOURS];
  uint8_t centre[MS_COLOURS];
  uint8_t bag[MS_COLOURS];
  uint8_t box[MS_COLOURS];      /* discarded tiles, poured back into the bag when it runs dry */
  uint16_t moves_played;
  uint16_t pad0;
  ms_player p[BK_MAX_SEATS];
} ms_state;

/* the mosaic column colour `colour` takes in row `row` (our own diagonal layout) */
static inline int ms_wall_col(int row, int colour) { return (colour + 2 * row) % MS_SIZE; }
/* the colour of mosaic cell (row, col) */
static inline int ms_wall_colour(int row, int col) { return ((col - 2 * row) % MS_SIZE + 2 * MS_SIZE) % MS_SIZE; }
static inline bool ms_wall_has(const ms_player *pl, int row, int col) { return (pl->wall >> (row * MS_SIZE + col)) & 1u; }

static inline bk_move ms_move(int source, int colour, int line) { return bk_move_pack(source, colour, line, 0, 0, 0, 0, 0); }
static inline int ms_move_source(bk_move m) { return bk_move_field(m, 0); }
static inline int ms_move_colour(bk_move m) { return bk_move_field(m, 1); }
static inline int ms_move_line(bk_move m) { return bk_move_field(m, 2); }

extern const int ms_floor_penalty[MS_FLOOR_SLOTS]; /* -1 -1 -2 -2 -2 -3 -3 */
extern const char *const ms_colour_name[MS_COLOURS];

/* a fresh game, players 2..4, seed for the bag draws; phase REFILL (a chance node) */
void ms_init(ms_state *s, int players, uint64_t seed);
/* the bk_game over ms_state for this many players */
void ms_game(bk_game *g, int players);

int ms_moves(const ms_state *s, bk_move *out, int cap);
/* the draft part of a move (ms_apply = ms_take, then the round end when the trays and centre are empty) */
void ms_take(ms_state *s, bk_move m);
void ms_apply(ms_state *s, bk_move m);
bool ms_draft_empty(const ms_state *s);
/* the round end in steps (ms_apply runs them all): tile one full staging row (its points, -1 when the row is not
 * full), settle a breakage strip (its penalty, after the floor at 0), then finish the round (game over or refill) */
int ms_tile_row(ms_state *s, int seat, int row);
int ms_clear_floor(ms_state *s, int seat);
void ms_round_finish(ms_state *s);
int ms_turn(const ms_state *s);
bool ms_terminal(const ms_state *s);
void ms_chance(ms_state *s, cw_rng *rng);
void ms_evaluate(const ms_state *s, float score[BK_MAX_SEATS]);
uint64_t ms_hash(const ms_state *s);

/* points a tile placed at (row, col) scores on this wall (the tile itself not yet on it) */
int ms_place_points(uint32_t wall, int row, int col);
/* floor penalty for n occupied slots (n clamped to MS_FLOOR_SLOTS), a value <= 0 */
int ms_floor_points(int slots);
/* end bonus of a wall: rows, columns, colour sets */
int ms_end_bonus(uint32_t wall, int *rows, int *cols, int *sets);
/* how many tiles of `colour` the source holds */
int ms_source_count(const ms_state *s, int source, int colour);
/* the round-end scoring a player would get now (lines that are full, adjacency and floor), without changing s */
int ms_round_preview(const ms_state *s, int seat);
/* winners: bit i set for each seat that wins (ties on score break on finished rows, then share) */
unsigned ms_winners(const ms_state *s);
/* total tiles of each colour accounted for (bag, box, trays, centre, lines, floors, walls): 20 each */
void ms_tile_census(const ms_state *s, int out[MS_COLOURS]);
void ms_move_label(const ms_state *s, bk_move m, char *buf, int cap);

#endif
