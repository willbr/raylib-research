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

static void test_math(void) {
    // SignF
    CHECK(SignF( 3.0f) ==  1.0f, "SignF positive");
    CHECK(SignF(-3.0f) == -1.0f, "SignF negative");
    CHECK(SignF( 0.0f) ==  0.0f, "SignF zero");

    // WrapAnglePi — result must land in (-PI, PI]
    for (float a = -10.0f; a < 10.0f; a += 0.37f) {
        float w = WrapAnglePi(a);
        CHECK(w > -PI - 1e-5f && w <=  PI + 1e-5f, "WrapAnglePi range");
    }
    CHECK(NEAR(WrapAnglePi(0.0f), 0.0f, 1e-5f), "WrapAnglePi 0");
    CHECK(NEAR(WrapAnglePi(TAU), 0.0f, 1e-4f),  "WrapAnglePi 2π");

    // WrapAngle2Pi — result must land in [0, TAU)
    for (float a = -10.0f; a < 10.0f; a += 0.37f) {
        float w = WrapAngle2Pi(a);
        CHECK(w >= 0.0f && w < TAU, "WrapAngle2Pi range");
    }

    // AngleDiff — shortest signed arc
    CHECK(NEAR(AngleDiff(0.1f, -0.1f), 0.2f, 1e-5f),   "AngleDiff near 0");
    CHECK(NEAR(AngleDiff(PI + 0.1f, -PI + 0.1f), 0.0f, 1e-4f), "AngleDiff wrap");

    // ApproachF — moves toward target, does not overshoot
    CHECK(NEAR(ApproachF(0.0f, 10.0f, 3.0f), 3.0f,  1e-5f), "ApproachF step");
    CHECK(NEAR(ApproachF(0.0f, 2.0f, 5.0f),  2.0f,  1e-5f), "ApproachF clamp up");
    CHECK(NEAR(ApproachF(10.0f, 0.0f, 3.0f), 7.0f,  1e-5f), "ApproachF down");
    CHECK(NEAR(ApproachF(0.0f, -2.0f, 5.0f), -2.0f, 1e-5f), "ApproachF clamp down");

    // SmoothStep endpoints + midpoint
    CHECK(NEAR(SmoothStep(0.0f, 1.0f, 0.0f), 0.0f, 1e-5f), "SmoothStep 0");
    CHECK(NEAR(SmoothStep(0.0f, 1.0f, 1.0f), 1.0f, 1e-5f), "SmoothStep 1");
    CHECK(NEAR(SmoothStep(0.0f, 1.0f, 0.5f), 0.5f, 1e-5f), "SmoothStep mid");

    // EaseOutCubic endpoints
    CHECK(NEAR(EaseOutCubic(0.0f), 0.0f, 1e-5f), "EaseOutCubic 0");
    CHECK(NEAR(EaseOutCubic(1.0f), 1.0f, 1e-5f), "EaseOutCubic 1");

    // Non-endpoint cases that discriminate correct formulas from accidents
    CHECK(NEAR(EaseOutCubic(0.5f),   0.875f,  1e-5f), "EaseOutCubic 0.5 = 0.875");
    CHECK(NEAR(EaseInOutCubic(0.5f), 0.5f,    1e-5f), "EaseInOutCubic 0.5 = 0.5");
    CHECK(NEAR(EaseInOutCubic(0.25f),0.0625f, 1e-5f), "EaseInOutCubic 0.25 = 0.0625");
    CHECK(NEAR(SmoothStep(0.0f, 1.0f, 0.25f), 0.15625f, 1e-5f), "SmoothStep 0.25 = 0.15625");

    // AngleLerp — shortest-arc interpolation
    CHECK(NEAR(AngleLerp(0.0f, PI / 2.0f, 0.5f), PI / 4.0f, 1e-5f),
          "AngleLerp 0 → π/2 midpoint");
    // Cross the antimeridian: from near +π to near -π, midpoint lands on ±π
    {
        float al = AngleLerp(PI - 0.1f, -PI + 0.1f, 0.5f);
        CHECK(NEAR(al, PI, 0.01f) || NEAR(al, -PI, 0.01f),
              "AngleLerp wraps through antimeridian");
    }

    // WrapAnglePi boundary: -π falls outside (-π, π], wraps to +π
    CHECK(NEAR(WrapAnglePi(-PI), PI, 1e-4f), "WrapAnglePi -π → +π");

    // RandF stays in range
    SetRandomSeed(12345);
    for (int i = 0; i < 200; i++) {
        float r = RandF(5.0f, 10.0f);
        CHECK(r >= 5.0f && r <= 10.0f, "RandF range");
    }

    // RandOnUnitSphere magnitude ≈ 1
    for (int i = 0; i < 20; i++) {
        Vector3 v = RandOnUnitSphere();
        float m = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
        CHECK(NEAR(m, 1.0f, 1e-3f), "RandOnUnitSphere magnitude");
    }

    // RandOnUnitCircle magnitude ≈ 1
    for (int i = 0; i < 20; i++) {
        Vector2 v2 = RandOnUnitCircle();
        float m = sqrtf(v2.x * v2.x + v2.y * v2.y);
        CHECK(NEAR(m, 1.0f, 1e-4f), "RandOnUnitCircle magnitude");
    }

    // RandInUnitSphere: every sample lies within or on the unit sphere
    for (int i = 0; i < 40; i++) {
        Vector3 v = RandInUnitSphere();
        float m = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        CHECK(m <= 1.0f + 1e-4f, "RandInUnitSphere within unit sphere");
    }
}
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
