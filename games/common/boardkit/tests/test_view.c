/* bk_view's pure parts: counters, score deltas and the die's throw (no window, no GPU).
 *
 * Checks: a counter shows the old value at once, counts monotonically and lands exactly on the new one within
 * its time; a delta lives its life and goes; every throw (6 faces x 50 seeds) comes to rest on its face, starts
 * at the hand and ends at its spot, and never turns more than a quarter turn between two 60 Hz frames (a
 * tumble, not a jump cut).
 *
 * Card poses: a flip starts face down and ends face up (the front's normal turned from down to up), lies flat at both
 * ends and lifts between; a fan is symmetric about its centre, left to right, each card above the one before.
 *
 * CONTROLS that must fail: a throw made for the opposite face does not rest on the face asked for; the
 * ends-face-up check fails a flip toward face down. */
#include <math.h>

#include "boardkit/bk_card.h"
#include "boardkit/bk_view.h"
#include "check.h"

static float quat_angle(t3_quat a, t3_quat b) {
  float d = fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return 2 * acosf(d > 1 ? 1 : d);
}

int main(void) {
  bk_counter c;
  bk_counter_init(&c, 10);
  bk_counter_set(&c, 25);
  bool mono = bk_counter_value(&c) == 10;
  int last = 10, steps = 0;
  for (int i = 0; i < 60 && bk_counter_moving(&c); i++, steps++) {
    bk_counter_update(&c, 1.0f / 60);
    mono = mono && bk_counter_value(&c) >= last;
    last = bk_counter_value(&c);
  }
  CHECK(mono && !bk_counter_moving(&c) && bk_counter_value(&c) == 25 && steps <= 37, "a counter counts 10 to 25 upward in %d frames", steps);

  bk_deltas ds;
  bk_deltas_init(&ds);
  for (int i = 0; i < BK_DELTAS_MAX + 5; i++) bk_deltas_add(&ds, (cw_vec2){ 0, 0 }, i, (cw_color){ 1, 1, 1, 1 });
  CHECK(bk_deltas_live(&ds) == BK_DELTAS_MAX, "a full pool keeps %d deltas", BK_DELTAS_MAX);
  for (int i = 0; i < 90; i++) bk_deltas_update(&ds, 1.0f / 60);
  CHECK(bk_deltas_live(&ds) == 0, "deltas are gone after their life");

  int bad_face = 0, bad_end = 0, worst_frame = 0;
  float worst = 0;
  for (int f = 1; f <= 6; f++)
    for (uint64_t s = 0; s < 50; s++) {
      bk_die_throw th = bk_die_throw_make((t3_vec3){ -2, 1.2f, 1 }, (t3_vec3){ 1, 0.1f, -0.5f }, f, s);
      t3_vec3 p;
      t3_quat q, prev;
      bk_die_pose(&th, 0, &p, &prev);
      bool starts = fabsf(p.x + 2) < 1e-4f && fabsf(p.y - 1.2f) < 1e-3f;
      for (int k = 1; k <= 80; k++) {
        bk_die_pose(&th, (float)k / 60, &p, &q);
        float a = quat_angle(prev, q);
        if (a > worst) worst = a, worst_frame = k;
        prev = q;
      }
      bad_face += bk_die_face_up(q) != f;
      bad_end += !starts || fabsf(p.x - 1) > 1e-4f || fabsf(p.y - 0.1f) > 1e-4f || fabsf(p.z + 0.5f) > 1e-4f;
    }
  CHECK(bad_face == 0, "300 throws rest on the face rolled (%d wrong)", bad_face);
  CHECK(bad_end == 0, "each throw leaves the hand and ends on its spot (%d wrong)", bad_end);
  CHECK(worst < 1.5708f, "the largest turn between frames is %.2f rad (frame %d), under a quarter turn", worst, worst_frame);

  int caught = 0;
  for (int f = 1; f <= 6; f++) {
    bk_die_throw th = bk_die_throw_make((t3_vec3){ 0, 1, 0 }, (t3_vec3){ 0, 0.1f, 0 }, 7 - f, 3);
    t3_vec3 p;
    t3_quat q;
    bk_die_pose(&th, 2, &p, &q);
    caught += bk_die_face_up(q) != f;
  }
  CONTROL(caught == 6, "throws made for the opposite face rest on the wrong face (%d of 6)", caught);
  /* card poses */
  bk_pose rest = { { 0, 0.01f, 0 }, { 0, 0, 0, 1 } };
  t3_vec3 up = { 0, 1, 0 };
  bk_pose f0 = bk_card_flip_pose(rest, 0, 0.4f, true), f5 = bk_card_flip_pose(rest, 0.5f, 0.4f, true), f1 = bk_card_flip_pose(rest, 1, 0.4f, true);
  float n0 = t3_vec3_apply_quat(up, f0.rot).y, n1 = t3_vec3_apply_quat(up, f1.rot).y;
  CHECK(n0 < -0.99f && n1 > 0.99f && fabsf(f0.pos.y - 0.01f) < 1e-5f && fabsf(f1.pos.y - 0.01f) < 1e-5f && f5.pos.y > 0.3f,
        "a flip turns the front from down (%.2f) to up (%.2f), flat at both ends, lifted %.2f between", (double)n0, (double)n1, (double)f5.pos.y);
  float nb = t3_vec3_apply_quat(up, bk_card_flip_pose(rest, 1, 0.4f, false).rot).y;
  CONTROL(!(nb > 0.99f), "the ends-face-up check fails a flip toward face down (front at %.2f)", (double)nb);
  bk_pose fan[5];
  bk_card_fan(5, (t3_vec3){ 0, 0, 2 }, 3, 0.8f, 0.3f, fan);
  CHECK(fabsf(fan[0].pos.x + fan[4].pos.x) < 1e-4f && fabsf(fan[2].pos.x) < 1e-5f && fan[0].pos.x < fan[1].pos.x && fan[4].pos.y > fan[0].pos.y,
        "a fan of five is symmetric, left to right, each above the one before");
  return DONE("view");
}
