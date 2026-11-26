/* Smalti: the rules. See rules.h. */
#include "rules.h"

#include <stdio.h>
#include <string.h>

const int ms_floor_penalty[MS_FLOOR_SLOTS] = {-1, -1, -2, -2, -2, -3, -3};
const char *const ms_colour_name[MS_COLOURS] = {"cobalt", "amber", "coral", "jade", "pearl"};

static int popcount32(uint32_t x) {
  int n = 0;
  while (x) { x &= x - 1; n++; }
  return n;
}

void ms_init(ms_state *s, int players, uint64_t seed) {
  if (players < 2 || players > BK_MAX_SEATS) CW_FATAL("smalti: %d players (2..4)", players);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = (uint8_t)players;
  s->trays = (uint8_t)(2 * players + 1);
  s->phase = MS_PHASE_REFILL;
  s->seat = 0;
  s->first_next = 0;
  s->centre_marker = 1;
  for (int c = 0; c < MS_COLOURS; c++) s->bag[c] = MS_TILES_PER_COLOUR;
}

int ms_source_count(const ms_state *s, int source, int colour) {
  if (source == MS_CENTRE) return s->centre[colour];
  if (source < 0 || source >= s->trays) return 0;
  return s->tray[source][colour];
}

static bool line_accepts(const ms_player *pl, int line, int colour) {
  if (ms_wall_has(pl, line, ms_wall_col(line, colour))) return false;
  int cnt = pl->line_count[line];
  if (cnt == 0) return true;
  return pl->line_colour[line] == colour && cnt < line + 1;
}

int ms_moves(const ms_state *s, bk_move *out, int cap) {
  int n = 0;
  if (s->phase != MS_PHASE_DRAFT) return 0;
  const ms_player *pl = &s->p[s->seat];
  for (int src = 0; src <= MS_CENTRE; src++) {
    if (src >= s->trays && src != MS_CENTRE) continue;
    for (int c = 0; c < MS_COLOURS; c++) {
      if (!ms_source_count(s, src, c)) continue;
      for (int line = 0; line < MS_SIZE; line++)
        if (line_accepts(pl, line, c)) {
          if (n >= cap) CW_FATAL("smalti: move buffer of %d is too small", cap);
          out[n++] = ms_move(src, c, line);
        }
      if (n >= cap) CW_FATAL("smalti: move buffer of %d is too small", cap);
      out[n++] = ms_move(src, c, MS_FLOOR);
    }
  }
  return n;
}

/* drop n tiles of colour c on the breakage strip; what does not fit goes to the box */
static void floor_add(ms_state *s, ms_player *pl, int c, int n) {
  for (int i = 0; i < n; i++) {
    if (pl->floor_tiles + pl->floor_marker < MS_FLOOR_SLOTS) {
      pl->floor_tiles++;
      pl->floor_colour[c]++;
    } else {
      s->box[c]++;
    }
  }
}

static void floor_take_marker(ms_state *s, ms_player *pl) {
  if (pl->floor_tiles + 1 > MS_FLOOR_SLOTS) {
    /* a full strip: the marker bumps the last tile to the box */
    for (int c = MS_COLOURS - 1; c >= 0; c--)
      if (pl->floor_colour[c]) {
        pl->floor_colour[c]--;
        pl->floor_tiles--;
        s->box[c]++;
        break;
      }
  }
  pl->floor_marker = 1;
}

int ms_place_points(uint32_t wall, int row, int col) {
  int h = 1, v = 1;
  for (int c = col - 1; c >= 0 && (wall >> (row * MS_SIZE + c)) & 1u; c--) h++;
  for (int c = col + 1; c < MS_SIZE && (wall >> (row * MS_SIZE + c)) & 1u; c++) h++;
  for (int r = row - 1; r >= 0 && (wall >> (r * MS_SIZE + col)) & 1u; r--) v++;
  for (int r = row + 1; r < MS_SIZE && (wall >> (r * MS_SIZE + col)) & 1u; r++) v++;
  if (h > 1 && v > 1) return h + v;
  return h > v ? h : v;
}

int ms_floor_points(int slots) {
  if (slots > MS_FLOOR_SLOTS) slots = MS_FLOOR_SLOTS;
  int p = 0;
  for (int i = 0; i < slots; i++) p += ms_floor_penalty[i];
  return p;
}

int ms_end_bonus(uint32_t wall, int *rows, int *cols, int *sets) {
  int nr = 0, nc = 0, ns = 0;
  for (int r = 0; r < MS_SIZE; r++) {
    uint32_t m = 0x1fu << (r * MS_SIZE);
    if ((wall & m) == m) nr++;
  }
  for (int c = 0; c < MS_SIZE; c++) {
    bool full = true;
    for (int r = 0; r < MS_SIZE; r++) full = full && ((wall >> (r * MS_SIZE + c)) & 1u);
    if (full) nc++;
  }
  for (int k = 0; k < MS_COLOURS; k++) {
    bool full = true;
    for (int r = 0; r < MS_SIZE; r++) full = full && ((wall >> (r * MS_SIZE + ms_wall_col(r, k))) & 1u);
    if (full) ns++;
  }
  if (rows) *rows = nr;
  if (cols) *cols = nc;
  if (sets) *sets = ns;
  return nr * MS_BONUS_ROW + nc * MS_BONUS_COL + ns * MS_BONUS_COLOUR;
}

static bool wall_has_full_row(uint32_t wall) {
  for (int r = 0; r < MS_SIZE; r++) {
    uint32_t m = 0x1fu << (r * MS_SIZE);
    if ((wall & m) == m) return true;
  }
  return false;
}

int ms_round_preview(const ms_state *s, int seat) {
  const ms_player *pl = &s->p[seat];
  uint32_t wall = pl->wall;
  int pts = 0;
  for (int r = 0; r < MS_SIZE; r++)
    if (pl->line_count[r] == r + 1) {
      int col = ms_wall_col(r, pl->line_colour[r]);
      pts += ms_place_points(wall, r, col);
      wall |= 1u << (r * MS_SIZE + col);
    }
  pts += ms_floor_points(pl->floor_tiles + pl->floor_marker);
  return pts;
}

static void game_over(ms_state *s) {
  for (int i = 0; i < s->players; i++) s->p[i].score = (int16_t)(s->p[i].score + ms_end_bonus(s->p[i].wall, NULL, NULL, NULL));
  s->phase = MS_PHASE_OVER;
}

/* end of round, in the steps the view shows one at a time */
int ms_tile_row(ms_state *s, int seat, int row) {
  ms_player *pl = &s->p[seat];
  if (pl->line_count[row] != row + 1) return -1;
  int c = pl->line_colour[row];
  int col = ms_wall_col(row, c);
  int pts = ms_place_points(pl->wall, row, col);
  pl->wall |= 1u << (row * MS_SIZE + col);
  s->box[c] = (uint8_t)(s->box[c] + row); /* the rest of the row goes to the box */
  pl->line_count[row] = 0;
  pl->line_colour[row] = 0;
  pl->score = (int16_t)(pl->score + pts);
  return pts;
}

int ms_clear_floor(ms_state *s, int seat) {
  ms_player *pl = &s->p[seat];
  int before = pl->score;
  int score = before + ms_floor_points(pl->floor_tiles + pl->floor_marker);
  if (score < 0) score = 0;
  pl->score = (int16_t)score;
  for (int c = 0; c < MS_COLOURS; c++) {
    s->box[c] = (uint8_t)(s->box[c] + pl->floor_colour[c]);
    pl->floor_colour[c] = 0;
  }
  pl->floor_tiles = 0;
  pl->floor_marker = 0;
  return score - before;
}

void ms_round_finish(ms_state *s) {
  bool end = false;
  for (int i = 0; i < s->players; i++)
    if (wall_has_full_row(s->p[i].wall)) end = true;
  if (end) { game_over(s); return; }
  if (s->round >= MS_MAX_ROUNDS) { s->ended_by_cap = 1; game_over(s); return; }
  s->phase = MS_PHASE_REFILL;
  s->seat = s->first_next;
  s->centre_marker = 1;
}

static void round_end(ms_state *s) {
  for (int i = 0; i < s->players; i++) {
    for (int r = 0; r < MS_SIZE; r++) ms_tile_row(s, i, r);
    ms_clear_floor(s, i);
  }
  ms_round_finish(s);
}

bool ms_draft_empty(const ms_state *s) {
  for (int c = 0; c < MS_COLOURS; c++) {
    if (s->centre[c]) return false;
    for (int t = 0; t < s->trays; t++)
      if (s->tray[t][c]) return false;
  }
  return true;
}

void ms_take(ms_state *s, bk_move m) {
  if (s->phase != MS_PHASE_DRAFT) CW_FATAL("smalti: apply outside the draft (phase %d)", s->phase);
  int src = ms_move_source(m), c = ms_move_colour(m), line = ms_move_line(m);
  ms_player *pl = &s->p[s->seat];
  int n;
  if (src == MS_CENTRE) {
    n = s->centre[c];
    s->centre[c] = 0;
    if (s->centre_marker) {
      s->centre_marker = 0;
      s->first_next = s->seat;
      floor_take_marker(s, pl);
    }
  } else {
    n = s->tray[src][c];
    s->tray[src][c] = 0;
    for (int k = 0; k < MS_COLOURS; k++) {
      s->centre[k] = (uint8_t)(s->centre[k] + s->tray[src][k]);
      s->tray[src][k] = 0;
    }
  }
  if (n <= 0) CW_FATAL("smalti: move takes no tiles (source %d colour %d)", src, c);
  if (line == MS_FLOOR) {
    floor_add(s, pl, c, n);
  } else {
    int room = line + 1 - pl->line_count[line];
    int put = n < room ? n : room;
    pl->line_colour[line] = (uint8_t)c;
    pl->line_count[line] = (uint8_t)(pl->line_count[line] + put);
    floor_add(s, pl, c, n - put);
  }
  s->moves_played++;
  s->seat = (uint8_t)((s->seat + 1) % s->players);
}

void ms_apply(ms_state *s, bk_move m) {
  ms_take(s, m);
  if (ms_draft_empty(s)) round_end(s);
}

int ms_turn(const ms_state *s) {
  if (s->phase == MS_PHASE_REFILL) return BK_CHANCE;
  return s->seat;
}

bool ms_terminal(const ms_state *s) { return s->phase == MS_PHASE_OVER; }

static int draw_one(ms_state *s, cw_rng *rng) {
  int total = 0;
  for (int c = 0; c < MS_COLOURS; c++) total += s->bag[c];
  if (total == 0) {
    for (int c = 0; c < MS_COLOURS; c++) { s->bag[c] = s->box[c]; s->box[c] = 0; total += s->bag[c]; }
    if (total == 0) return -1;
  }
  int r = cw_rng_int(rng, 0, total - 1);
  for (int c = 0; c < MS_COLOURS; c++) {
    if (r < s->bag[c]) { s->bag[c]--; return c; }
    r -= s->bag[c];
  }
  CW_FATAL("smalti: bag draw out of range");
  return -1;
}

void ms_chance(ms_state *s, cw_rng *rng) {
  if (s->phase != MS_PHASE_REFILL) CW_FATAL("smalti: chance outside a refill (phase %d)", s->phase);
  int drawn = 0;
  for (int t = 0; t < s->trays; t++)
    for (int k = 0; k < MS_TRAY_TILES; k++) {
      int c = draw_one(s, rng);
      if (c < 0) goto done;
      s->tray[t][c]++;
      drawn++;
    }
done:
  s->round++;
  if (drawn == 0) { game_over(s); return; }
  s->phase = MS_PHASE_DRAFT;
}

/* ── evaluation ─────────────────────────────────────────────────────── */

/* a seat's expected final points from here, a heuristic: banked score, the round-end tiling it has locked in,
 * partial staging rows and progress toward the end bonuses */
static float seat_value(const ms_state *s, int seat) {
  const ms_player *pl = &s->p[seat];
  float v = (float)pl->score + (float)ms_round_preview(s, seat);
  /* partial rows: some of the adjacency they will earn, less the risk of not finishing */
  for (int r = 0; r < MS_SIZE; r++) {
    int k = pl->line_count[r];
    if (k == 0 || k == r + 1) continue;
    int col = ms_wall_col(r, pl->line_colour[r]);
    float fill = (float)k / (float)(r + 1);
    v += fill * 0.6f * (float)ms_place_points(pl->wall, r, col) - 0.25f * (float)(r + 1 - k);
  }
  /* end bonus progress (the wall after this round's tiling) */
  uint32_t wall = pl->wall;
  for (int r = 0; r < MS_SIZE; r++)
    if (pl->line_count[r] == r + 1) wall |= 1u << (r * MS_SIZE + ms_wall_col(r, pl->line_colour[r]));
  for (int r = 0; r < MS_SIZE; r++) {
    float k = (float)popcount32(wall & (0x1fu << (r * MS_SIZE)));
    v += MS_BONUS_ROW * (k / 5.0f) * (k / 5.0f);
  }
  for (int c = 0; c < MS_SIZE; c++) {
    int k = 0;
    for (int r = 0; r < MS_SIZE; r++) k += (wall >> (r * MS_SIZE + c)) & 1u;
    v += MS_BONUS_COL * ((float)k / 5.0f) * ((float)k / 5.0f);
  }
  for (int col = 0; col < MS_COLOURS; col++) {
    int k = 0;
    for (int r = 0; r < MS_SIZE; r++) k += (wall >> (r * MS_SIZE + ms_wall_col(r, col))) & 1u;
    v += MS_BONUS_COLOUR * ((float)k / 5.0f) * ((float)k / 5.0f);
  }
  return v;
}

unsigned ms_winners(const ms_state *s) {
  int best = -1, best_rows = -1;
  unsigned w = 0;
  for (int i = 0; i < s->players; i++) {
    int rows;
    ms_end_bonus(s->p[i].wall, &rows, NULL, NULL);
    int sc = s->p[i].score;
    if (sc > best || (sc == best && rows > best_rows)) { best = sc; best_rows = rows; w = 1u << i; }
    else if (sc == best && rows == best_rows) w |= 1u << i;
  }
  return w;
}

void ms_evaluate(const ms_state *s, float score[BK_MAX_SEATS]) {
  for (int i = 0; i < BK_MAX_SEATS; i++) score[i] = 0;
  if (s->phase == MS_PHASE_OVER) {
    unsigned w = ms_winners(s);
    for (int i = 0; i < s->players; i++) {
      int best_other = -1000;
      for (int j = 0; j < s->players; j++)
        if (j != i && s->p[j].score > best_other) best_other = s->p[j].score;
      float margin = (float)(s->p[i].score - best_other);
      score[i] = ((w >> i) & 1u) ? BK_WIN + margin : BK_LOSS + margin;
    }
    return;
  }
  for (int i = 0; i < s->players; i++) score[i] = seat_value(s, i);
}

uint64_t ms_hash(const ms_state *s) { return cw_hash_bytes(s, sizeof *s, 0); }

void ms_tile_census(const ms_state *s, int out[MS_COLOURS]) {
  for (int c = 0; c < MS_COLOURS; c++) {
    int n = s->bag[c] + s->box[c] + s->centre[c];
    for (int t = 0; t < s->trays; t++) n += s->tray[t][c];
    for (int i = 0; i < s->players; i++) {
      const ms_player *pl = &s->p[i];
      n += pl->floor_colour[c];
      for (int r = 0; r < MS_SIZE; r++) {
        if (pl->line_count[r] && pl->line_colour[r] == c) n += pl->line_count[r];
        if (ms_wall_has(pl, r, ms_wall_col(r, c))) n++;
      }
    }
    out[c] = n;
  }
}

void ms_move_label(const ms_state *s, bk_move m, char *buf, int cap) {
  int src = ms_move_source(m), c = ms_move_colour(m), line = ms_move_line(m);
  int n = ms_source_count(s, src, c);
  char from[24], to[24];
  if (src == MS_CENTRE) snprintf(from, sizeof from, "the centre");
  else snprintf(from, sizeof from, "tray %d", src + 1);
  if (line == MS_FLOOR) snprintf(to, sizeof to, "the breakage strip");
  else snprintf(to, sizeof to, "row %d", line + 1);
  snprintf(buf, (size_t)cap, "take %d %s from %s to %s", n, ms_colour_name[c], from, to);
}

/* ── bk_game glue ───────────────────────────────────────────────────── */

static int g_moves(const void *st, bk_move *out, int cap, void *u) { (void)u; return ms_moves(st, out, cap); }
static void g_apply(void *st, bk_move m, void *u) { (void)u; ms_apply(st, m); }
static int g_turn(const void *st, void *u) { (void)u; return ms_turn(st); }
static bool g_terminal(const void *st, void *u) { (void)u; return ms_terminal(st); }
static void g_evaluate(const void *st, float sc[BK_MAX_SEATS], void *u) { (void)u; ms_evaluate(st, sc); }
static uint64_t g_hash(const void *st, void *u) { (void)u; return ms_hash(st); }
static void g_chance(void *st, cw_rng *rng, void *u) { (void)u; ms_chance(st, rng); }

/* move ordering: rows that complete exactly first, the breakage strip last */
static float g_priority(const void *st, bk_move m, void *u) {
  (void)u;
  const ms_state *s = st;
  const ms_player *pl = &s->p[s->seat];
  int src = ms_move_source(m), c = ms_move_colour(m), line = ms_move_line(m);
  int n = ms_source_count(s, src, c);
  if (line == MS_FLOOR) return -10.0f - (float)n;
  int room = line + 1 - pl->line_count[line];
  int over = n - room;
  float p = 0;
  if (over == 0) p += 6.0f + (float)ms_place_points(pl->wall, line, ms_wall_col(line, c));
  else if (over > 0) p -= 2.0f * (float)over;
  else p += (float)n;
  if (src == MS_CENTRE && s->centre_marker) p -= 1.0f;
  return p;
}

void ms_game(bk_game *g, int players) {
  memset(g, 0, sizeof *g);
  g->players = players;
  g->state_size = sizeof(ms_state);
  g->moves = g_moves;
  g->apply = g_apply;
  g->turn = g_turn;
  g->terminal = g_terminal;
  g->evaluate = g_evaluate;
  g->hash = g_hash;
  g->chance = g_chance;
  g->priority = g_priority;
}
