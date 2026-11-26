/* Crokinole's computer player: candidate shots (where on the arc x aim x power) aimed at the discs and spots that
 * matter, each simulated to rest with the real physics and judged by the score it leaves; then the chosen shot is
 * played with a seeded hand error (the level's aim and power spread). Pure and deterministic like crk.h. */
#ifndef CRK_CPU_H
#define CRK_CPU_H

#include "crk.h"

#define CRK_CPU_MAXCAND 512

typedef struct crk_cpu_level {
  int positions;      /* places along the arc tried */
  float aim_err;      /* radians (standard deviation) */
  float power_err;    /* fraction of the power (standard deviation) */
} crk_cpu_level;

/* 0 Easy, 1 Normal, 2 Hard */
crk_cpu_level crk_cpu_level_of(int level);

/* the candidate shots for the seat to move, in a fixed order; count (<= cap) */
int crk_cpu_candidates(const crk_state *s, const crk_cpu_level *L, uint64_t *out, int cap);
/* each side's value of a state from its own view (larger is better): its score and round count against the best
 * other side's; terminal states are +-1e6 */
void crk_cpu_evaluate(const crk_state *s, float v[4]);
/* the seeded hand error on a chosen shot (seed from the state's hash and the level) */
uint64_t crk_cpu_perturb(const crk_state *s, uint64_t move, const crk_cpu_level *L, uint64_t seed);
/* the whole choice: best candidate by simulated outcome, with the hand error; *evals counts simulated shots */
uint64_t crk_cpu_choose(const crk_state *s, int level, uint64_t seed, int *evals);

#endif
