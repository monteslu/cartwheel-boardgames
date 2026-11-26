/* Crokinole rules and disc physics: pure, headless, deterministic.
 *
 * The board is a plane (x right, y away from seat 0, metres, centre at the origin). Discs slide on polished wood
 * and stop by friction; eight rubber-sleeved pegs stand on the 15 line; a disc whose centre leaves the playing
 * surface drops into the ditch; a slow disc over the centre hole drops in and scores 20 at once.
 *
 * Everything here is + - * / and sqrt on floats (the module's own sin/cos/atan2 are polynomials), compiled with
 * -ffp-contract=off, so the same shot gives the same bits on every host. The state is flat and fixed size; a shot
 * runs as begin, ticks, end (the view animates the same ticks the CPU simulates), and crk_apply runs all three.
 *
 * Rules (our wording):
 *   - 2, 3 or 4 seats; 4 seats play as two teams of partners sitting opposite. 24 discs per round, split evenly.
 *   - Seats shoot in turn from their own quadrant: the disc's centre sits on the shooting line (the outer circle)
 *     inside the seat's quarter of the board.
 *   - If another side has a disc on the board, the shot must strike one of them (directly, or through a disc of
 *     the shooter's side it sets moving). A shot that does not is a foul: the shot disc and any disc of the
 *     shooter's side it touched go to the ditch. Moved opposing discs stay where they stop.
 *   - If no other side has a disc on the board, the shot disc must come to rest touching or inside the 15 line
 *     (or drop into the hole); otherwise it goes to the ditch.
 *   - A disc dropped into the centre hole is taken out and scores 20 for its side straight away.
 *   - After every shot, discs touching the shooting line or beyond it go to the ditch.
 *   - When all discs are shot the round is counted: 15, 10 or 5 per disc by region (a disc touching a line scores
 *     the lower region) plus the 20s. The side with the most scores the difference over the next best side.
 *     First side to the target (100 by default) wins. The first shooter moves one seat on each round. */
#ifndef CRK_H
#define CRK_H

#include <stdbool.h>
#include <stdint.h>

/* geometry (metres) */
#define CRK_R_SURF 0.330f   /* the playing surface: past it the ditch */
#define CRK_R_5 0.3048f     /* the shooting line, the outside of the 5 region */
#define CRK_R_10 0.2032f
#define CRK_R_15 0.1016f
#define CRK_R_HOLE 0.0175f
#define CRK_R_DISC 0.0159f
#define CRK_R_PEG 0.0056f   /* with its rubber sleeve */
#define CRK_NPEG 8
#define CRK_NDISC 24
#define CRK_VMAX 2.6f       /* the hardest flick, m/s */
#define CRK_SPIN_MAX 60.0f  /* rad/s */
/* a shot's settings are whole steps, so a pad, a finger and the CPU all give exactly the shot the CPU simulated */
#define CRK_POS_N 240       /* places along the seat's arc: 0 (its left end) .. CRK_POS_N */
#define CRK_AIM_N 560       /* aim steps either side of straight in */
#define CRK_AIM_STEP 0.0025f /* radians per aim step (so up to 1.4 rad) */
#define CRK_POW_N 200       /* power steps: CRK_POW_N is CRK_VMAX */
#define CRK_SPIN_N 8        /* spin steps either way */
#define CRK_TICK (1.0f / 60.0f)
#define CRK_MAX_TICKS 1200  /* a shot is stopped after 20 s whatever moves */

enum { CRK_HAND, CRK_BOARD, CRK_DITCH, CRK_TWENTY };
enum { CRK_AIM, CRK_MOVING, CRK_OVER, CRK_SETTLED };

typedef struct crk_disc {
  float x, y, vx, vy;
  float w, rot;              /* spin (rad/s, counter-clockwise) and the angle it has turned (view) */
  uint8_t owner, st, side, pad;
} crk_disc;

typedef struct crk_round_result {
  int16_t pts[4];            /* each side's count: board + 20s */
  int16_t twenties[4];
  int8_t winner;             /* side that scored, -1 a tie */
  uint8_t pad;
  int16_t scored;            /* what it scored */
} crk_round_result;

typedef struct crk_state {
  uint8_t players, sides, to_move, starter;
  uint8_t phase, shot_disc, foul, pad0;
  uint16_t round, shots;     /* round number (1 based); shots this round */
  int16_t target;            /* points to win */
  int16_t score[4];          /* per side */
  int16_t twenties[4];       /* 20s this round, per side */
  int8_t winner, last_foul;  /* winner side when over; the last shot's foul (0 none, 1 missed, 2 short) */
  uint16_t pad2;
  uint16_t ticks;            /* ticks of the shot in progress */
  uint32_t moved;            /* discs set moving by the shot */
  uint32_t touched_own;      /* own-side discs the shot disc touched */
  uint32_t hit_opp;          /* the shot struck an opposing disc */
  uint32_t open_board;       /* no opposing disc was on the board when the shot began */
  uint32_t contacts;         /* contacts this shot (sound, tests) */
  crk_round_result last;     /* the last finished round (UI) */
  uint16_t rounds_done, pad1;
  crk_disc d[CRK_NDISC];
} crk_state;

/* a shot in steps: where on the seat's arc (0..CRK_POS_N, left to right as the seat sees it), the aim (steps from
 * straight in, positive to the seat's left), the speed (0..CRK_POW_N) and the spin (-CRK_SPIN_N..CRK_SPIN_N,
 * positive counter-clockwise). A move packs it in 40 bits. */
typedef struct crk_shot { int16_t pos, aim, power, spin; } crk_shot;

/* a contact event the view can play (sound, sparks) */
typedef struct crk_event { uint8_t kind, a, b, pad; float speed, x, y; } crk_event;
enum { CRK_EV_DISC, CRK_EV_PEG, CRK_EV_DITCH, CRK_EV_HOLE };
#define CRK_MAXEV 64
typedef struct crk_events { int n; crk_event e[CRK_MAXEV]; } crk_events;

void crk_new(crk_state *s, int players, int target);
int crk_side_of(const crk_state *s, int seat);
/* the peg centres */
void crk_peg(int i, float *x, float *y);
/* the seat's quadrant axis: the unit vector from the centre toward the seat */
void crk_seat_axis(int players, int seat, float *ax, float *ay);
/* where a shot starts (the disc centre) and its direction */
void crk_shot_place(const crk_state *s, int seat, float pos, float *x, float *y);
void crk_shot_dir(const crk_state *s, int seat, float pos, float aim, float *dx, float *dy);
/* the shot position slides off any disc it would overlap: the nearest free pos, or -1 when none */
int crk_free_pos(const crk_state *s, int seat, int pos);
/* the next disc in hand for the seat, -1 none */
int crk_next_disc(const crk_state *s, int seat);
bool crk_opponent_on_board(const crk_state *s, int seat);
int crk_disc_points(const crk_disc *d); /* 15/10/5/0 by region (lines score the lower) */
/* the side counts if the round ended now (board + 20s) */
void crk_count(const crk_state *s, int16_t pts[4]);

uint64_t crk_move_pack(crk_shot sh);
crk_shot crk_shot_clamp(crk_shot sh);
crk_shot crk_move_unpack(uint64_t m);

/* one shot in three parts: the view ticks it, crk_apply runs it to the end */
void crk_shot_begin(crk_state *s, crk_shot sh);
bool crk_tick(crk_state *s, crk_events *ev); /* true once everything has stopped */
void crk_shot_end(crk_state *s);  /* settle, then next */
void crk_shot_settle(crk_state *s); /* the shot's rules: fouls, the line, 20s (the board as the shot left it) */
void crk_shot_next(crk_state *s);   /* the next seat, or the round counted and the next dealt, or the end */
void crk_apply(crk_state *s, uint64_t move);

uint64_t crk_hash(const crk_state *s);

/* deterministic trig (polynomials, the same bits on every host) */
void crk_sincos(float a, float *s, float *c);
float crk_atan2(float y, float x);

/* the speed (0..1 of CRK_VMAX) a disc needs to slide `dist` metres and stop, by the same friction, and back */
float crk_power_for(float dist);
float crk_slide_dist(float power);
/* the shot's settings as numbers */
static inline float crk_pos_f(int pos) { return (float)pos / (float)CRK_POS_N; }
static inline float crk_aim_f(int aim) { return (float)aim * CRK_AIM_STEP; }
static inline float crk_pow_f(int power) { return (float)power / (float)CRK_POW_N; }

#endif
