/* The kit tests' checks: CHECK prints ok / FAIL, CONTROL_CAUGHT a control that must fail and did (tests/boardkit.mjs
 * counts "CONTROL ... caught" lines and fails a test that runs none), CONTROL_MISSED one that passed. */
#ifndef BK_CHECK_H
#define BK_CHECK_H
#include <stdio.h>
static int failed;
#define CHECK(c, ...) do { if (!(c)) { failed++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
/* the control's check: `bad` is true when the control was caught (the broken thing was noticed) */
#define CONTROL(bad, ...) do { if (bad) { printf("CONTROL "); printf(__VA_ARGS__); printf(": caught\n"); } else { failed++; printf("FAIL CONTROL not caught: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define DONE(name) (printf(failed ? "%d failed\n" : name ": all passed\n", failed), failed ? 1 : 0)
#endif
