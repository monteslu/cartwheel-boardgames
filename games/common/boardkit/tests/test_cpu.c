/* bk_cpu: CPU seats entering moves through actions, on three in a row with a cursor.
 *
 * The test stands in for the engine's input: each tick the CPU's actions go into a per-seat "pad" whose
 * just-pressed edges drive the game's input layer (a cursor of the engine's cursor module over the nine
 * cells: a direction hops it, confirm places), and only that layer plays moves. So the CPU must walk the
 * cursor there as a person would (bk_cpu_press_toward). Checks: Hard against Hard draws, every match;
 * Hard never loses to Easy; bk_cpu_press_toward_allow walks a cursor that refuses some targets home in 300 random
 * layouts (CONTROL: without the callback some walks get lost); a CPU presses nothing off its turn and nothing before its think time; the same
 * seed gives the same action stream.
 *
 * CONTROL that must fail: a next_press that walks the wrong way never lands its move, and the driver stops
 * the game (CW_FATAL) instead of pressing forever: run in a child process, which must die. */
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "boardkit/bk_cpu.h"
#include "check.h"
#include "tiny_games.h"

enum { A_LEFT = 0, A_RIGHT, A_UP, A_DOWN, A_OK, A_N };

typedef struct sim {
  bk_match *m;
  cw_cursors *cur;
  bool down[2][A_N];
  int off_turn_presses, first_press_tick[2], tick;
  uint64_t stream; /* hash of every action set */
  bool wrong_way;
} sim;

static bool enter(const void *state, bk_move move, int seat, bk_press *out, void *user) {
  (void)state;
  sim *s = user;
  bool ok = bk_cpu_press_toward(s->cur, NULL, seat, (int)move, A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, out);
  if (s->wrong_way && out->v[0].action != A_OK) /* the control: mirror the way */
    for (int i = 0; i < out->count; i++) out->v[i].action ^= 1;
  if (s->wrong_way && out->v[0].action == A_OK) out->v[0].action = A_LEFT; /* and never confirm */
  return ok;
}

/* one match, seat 0 and 1 CPUs at levels a and b; returns the winner (0, 1, 2 draw) */
static int run(sim *s, int la, int lb, uint64_t seed) {
  ttt t = ttt_from(".........", 0);
  s->m = bk_match_new(&TTT, &t, seed);
  bk_cpu *cpu[2];
  int lv[2] = { la, lb };
  for (int p = 0; p < 2; p++) {
    cpu[p] = bk_cpu_new(&TTT, &(bk_cpu_desc){ .level = lv[p], .next_press = enter, .user = s, .seed = seed + (uint64_t)p });
    cw_cursors_place(s->cur, p, 4);
    s->first_press_tick[p] = -1;
  }
  memset(s->down, 0, sizeof s->down);
  for (s->tick = 0; !bk_match_over(s->m) && s->tick < 20000; s->tick++) {
    for (int p = 0; p < 2; p++) {
      int turn = bk_match_turn(s->m);
      cw_action_value v[4];
      int n = bk_cpu_tick(cpu[p], s->m, p, v, 4);
      if (n && turn != p) s->off_turn_presses++;
      if (n && s->first_press_tick[p] < 0) s->first_press_tick[p] = s->tick;
      s->stream = cw_hash_bytes(v, sizeof v[0] * (size_t)n, s->stream ^ (uint64_t)p);
      bool now[A_N] = { 0 };
      for (int i = 0; i < n; i++) now[v[i].action] = v[i].strength > 0.5f;
      /* the game's input layer: just-pressed edges */
      for (int a = 0; a < A_N; a++) {
        bool edge = now[a] && !s->down[p][a];
        s->down[p][a] = now[a];
        if (!edge || turn != p) continue;
        static const cw_vec2 DIR[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        if (a < A_OK) cw_cursors_hop(s->cur, p, DIR[a]);
        else bk_match_play(s->m, (bk_move)cw_cursors_target(s->cur, p)); /* an occupied cell is refused */
      }
    }
  }
  int w = ((const ttt *)bk_match_state(s->m))->winner;
  bk_cpu_free(cpu[0]);
  bk_cpu_free(cpu[1]);
  bk_match_free(s->m);
  return w;
}

/* bk_cpu_press_toward_allow: random layouts where some targets are refused (cw_cursors_set_allow, as a game sets
 * for a pad). The CPU walks a pad cursor (refusing the same targets) from a start to a target; a walk ends when it
 * stands there, when press_toward gives up, or after 40 presses. Returns how many of n walks arrived. */
static bool refused_by(int player, int target, void *user) {
  (void)player;
  const uint8_t *ref = user;
  return !ref[target];
}
static int walks(int n, bool use_allow, int *stood_refused) {
  uint64_t r = 12345;
  int arrived = 0;
  for (int w = 0; w < n; w++) {
    enum { N = 14 };
    t3_vec3 pts[N];
    uint8_t ref[N];
    for (int i = 0; i < N; i++) {
      r = r * 6364136223846793005ull + 1442695040888963407ull;
      pts[i] = (t3_vec3){ (float)((r >> 33) % 600), (float)((r >> 13) % 400), 0 };
      ref[i] = (r >> 50) % 3 == 0;
    }
    ref[0] = ref[1] = 0; /* start 0, target 1 */
    cw_cursors *cur = cw_cursors_new(NULL);
    cw_cursors_set_points(cur, pts, N);
    cw_cursors_set_allow(cur, refused_by, ref);
    cw_cursors_place(cur, 0, 0);
    for (int k = 0; k < 40; k++) {
      bk_press p;
      bool ok = use_allow ? bk_cpu_press_toward_allow(cur, NULL, refused_by, ref, 0, 1, A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, &p)
                          : bk_cpu_press_toward(cur, NULL, 0, 1, A_LEFT, A_RIGHT, A_UP, A_DOWN, A_OK, &p);
      if (!ok) break;
      if (p.v[0].action == A_OK) {
        arrived++;
        break;
      }
      cw_vec2 dir = { 0, 0 };
      for (int i = 0; i < p.count; i++) {
        cw_action a = p.v[i].action;
        dir.x += a == A_RIGHT ? 1.f : a == A_LEFT ? -1.f : 0.f;
        dir.y += a == A_DOWN ? 1.f : a == A_UP ? -1.f : 0.f;
      }
      cw_cursors_hop(cur, 0, dir);
      int at = cw_cursors_target(cur, 0);
      if (at >= 0 && ref[at]) (*stood_refused)++;
    }
    cw_cursors_free(cur);
  }
  return arrived;
}

int main(void) {
  {
    int bad = 0, bad0 = 0;
    int with = walks(300, true, &bad), without = walks(300, false, &bad0);
    CHECK(with == 300 && bad == 0, "press_toward_allow walks a cursor that refuses targets home: %d of 300 arrived", with);
    /* CONTROL: the same walks without telling the CPU which targets are refused lose their way in some layouts */
    CONTROL(without < 300, "press_toward without the allow callback gets lost (%d of 300 arrived)", without);
  }

  sim s;
  memset(&s, 0, sizeof s);
  s.cur = cw_cursors_new(NULL);
  t3_vec3 pts[9];
  for (int i = 0; i < 9; i++) pts[i] = (t3_vec3){ (float)(i % 3) * 100, (float)(i / 3) * 100, 0 };
  cw_cursors_set_points(s.cur, pts, 9);

  int draws = 0;
  for (uint64_t seed = 1; seed <= 3; seed++) draws += run(&s, 2, 2, seed) == 2;
  CHECK(draws == 3, "Hard against Hard through the pad's actions: %d of 3 drawn", draws);
  CHECK(s.off_turn_presses == 0, "no press off a CPU's turn");
  CHECK(s.first_press_tick[0] + 1 >= 24, "the first press waits for the think time (Hard: 24 ticks; pressed on tick %d)", s.first_press_tick[0] + 1);

  int hard_lost = 0, hard_won = 0;
  for (uint64_t seed = 1; seed <= 8; seed++) {
    int w = run(&s, 0, 2, seed);
    hard_lost += w == 0, hard_won += w == 1;
  }
  CHECK(hard_lost == 0, "Hard never loses to Easy (won %d of 8, lost %d)", hard_won, hard_lost);

  s.stream = 0;
  run(&s, 1, 0, 5);
  uint64_t a = s.stream;
  s.stream = 0;
  run(&s, 1, 0, 5);
  CHECK(a == s.stream, "the same seeds press the same actions (%016llx)", (unsigned long long)a);

  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) {
    s.wrong_way = true;
    freopen("/dev/null", "w", stderr);
    run(&s, 2, 2, 1);
    _exit(0); /* reaching here means the driver pressed on without stopping */
  }
  int status = 0;
  waitpid(pid, &status, 0);
  bool died = !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CONTROL(died, "a CPU walking the wrong way is stopped (child %s %d)", WIFSIGNALED(status) ? "signal" : "exit",
          WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
  cw_cursors_free(s.cur);
  return DONE("cpu");
}
