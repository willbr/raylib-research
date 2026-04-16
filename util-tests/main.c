// util-tests: smoke test for common/util/*.h
// Exercises every pure-logic function with assertions.
// Exits non-zero if any assertion fails.
//
// No InitWindow: the tests only touch pure-logic (math/pool/collide/camera/fx).
// raylib's RNG (GetRandomValue, SetRandomSeed) operates on private static
// state in rprand.h and does not touch CORE.Window, so it is safe to call
// before InitWindow. Headers for input/hud/debug are included for compile-
// check only; their functions require an active window and are validated
// at runtime during the fps migration (Task 10), not here.

#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "raylib.h"
#include "raymath.h"

#include "../common/util/math.h"
#include "../common/util/pool.h"
#include "../common/util/collide.h"
#include "../common/util/camera.h"
#include "../common/util/fx.h"
#include "../common/util/input.h"
#include "../common/util/hud.h"
#include "../common/util/debug.h"

static int g_fails = 0;

#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (label), __FILE__, __LINE__); g_fails++; } \
} while (0)

#define NEAR(a, b, eps) (fabsf((a) - (b)) < (eps))

static void test_math(void)   { /* filled in Task 1 */ }
static void test_pool(void)   { /* filled in Task 2 */ }
static void test_collide(void){ /* filled in Task 3 */ }
static void test_camera(void) { /* filled in Task 4 */ }
static void test_fx(void)     { /* filled in Task 5 */ }

int main(void) {
    test_math();
    test_pool();
    test_collide();
    test_camera();
    test_fx();
    // input/hud/debug: include-only, no standalone tests (window-dependent)
    printf("\n%s (%d failure%s)\n", g_fails ? "FAILED" : "ALL PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
