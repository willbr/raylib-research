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
static void test_pool(void) {
    typedef struct { int payload; bool active; } Item;

    enum { N = 4 };
    Item pool[N] = {0};

    // Fresh pool: spawn returns 0..N-1 then -1
    int i0 = POOL_SPAWN(pool, N); CHECK(i0 == 0, "POOL_SPAWN 0");
    int i1 = POOL_SPAWN(pool, N); CHECK(i1 == 1, "POOL_SPAWN 1");
    int i2 = POOL_SPAWN(pool, N); CHECK(i2 == 2, "POOL_SPAWN 2");
    int i3 = POOL_SPAWN(pool, N); CHECK(i3 == 3, "POOL_SPAWN 3");
    int i4 = POOL_SPAWN(pool, N); CHECK(i4 == -1, "POOL_SPAWN full");

    // Each slot is active, with distinct indices
    CHECK(pool[0].active && pool[3].active, "POOL_SPAWN sets active");

    // Free one, next spawn reuses it
    pool[1].active = false;
    int i5 = POOL_SPAWN(pool, N); CHECK(i5 == 1, "POOL_SPAWN reuses freed");

    // POOL_FOREACH — iterates all N; count with filter
    int total = 0, active = 0;
    POOL_FOREACH(pool, N, p) {
        total++;
        if (p->active) active++;
    }
    CHECK(total == N,      "POOL_FOREACH total");
    CHECK(active == N,     "POOL_FOREACH active");

    // POOL_COUNT_ACTIVE
    CHECK(POOL_COUNT_ACTIVE(pool, N) == N, "POOL_COUNT_ACTIVE full");
    pool[2].active = false;
    CHECK(POOL_COUNT_ACTIVE(pool, N) == N - 1, "POOL_COUNT_ACTIVE after free");

    // POOL_RING_PUSH — wraps and overwrites
    Item ring[3] = {0};
    int ri = 0;
    POOL_RING_PUSH(ring, 3, ri); CHECK(ri == 1 && ring[0].active, "RING push 0");
    POOL_RING_PUSH(ring, 3, ri); CHECK(ri == 2 && ring[1].active, "RING push 1");
    POOL_RING_PUSH(ring, 3, ri); CHECK(ri == 0 && ring[2].active, "RING push 2 wraps");
    ring[0].active = false; // simulate previous decal fading
    POOL_RING_PUSH(ring, 3, ri); CHECK(ri == 1 && ring[0].active, "RING push reactivate");

    // POOL_FOREACH + break: exit early at first inactive slot
    Item scan[4] = { {10, true}, {20, true}, {0, false}, {40, true} };
    int firstInactive = -1;
    POOL_FOREACH(scan, 4, s) {
        if (!s->active) { firstInactive = (int)(s - scan); break; }
    }
    CHECK(firstInactive == 2, "POOL_FOREACH break lands on first inactive");

    // POOL_FOREACH + continue: skip inactive while summing active payloads
    int sum = 0;
    POOL_FOREACH(scan, 4, s) {
        if (!s->active) continue;
        sum += s->payload;
    }
    CHECK(sum == 70, "POOL_FOREACH continue skips inactive");
}
// File-scope context for test_blocked (lifted from nested function — Clang/c99
// does not support GNU nested functions).
typedef struct { AABB wall; } CollideTestCtx;
static CollideTestCtx s_collide_ctx;
static bool test_blocked(Vector3 p, float r, void *u) {
    AABB w = ((CollideTestCtx *)u)->wall;
    return fabsf(p.x - w.center.x) < (w.half.x + r)
        && fabsf(p.z - w.center.z) < (w.half.z + r)
        && fabsf(p.y - w.center.y) < (w.half.y + r);
}

static void test_collide(void) {
    AABB a = { .center = {0, 0, 0}, .half = {1, 1, 1} };
    AABB b = { .center = {1.5f, 0, 0}, .half = {1, 1, 1} };   // overlaps on X
    AABB c = { .center = {3.0f, 0, 0}, .half = {1, 1, 1} };   // touches edge
    AABB d = { .center = {4.0f, 0, 0}, .half = {1, 1, 1} };   // clearly separate

    CHECK(AABBOverlap(a, b),  "AABB overlap true");
    CHECK(!AABBOverlap(a, c), "AABB edge exclusive");
    CHECK(!AABBOverlap(a, d), "AABB separated");

    Sphere s1 = { .center = {1.5f, 0, 0}, .radius = 1.0f };
    Sphere s2 = { .center = {5.0f, 0, 0}, .radius = 0.5f };
    CHECK(AABBvsSphere(a, s1),  "AABBvsSphere hit");
    CHECK(!AABBvsSphere(a, s2), "AABBvsSphere miss");

    Sphere q1 = { .center = {0, 0, 0}, .radius = 1.0f };
    Sphere q2 = { .center = {1.5f, 0, 0}, .radius = 1.0f };
    Sphere q3 = { .center = {3.0f, 0, 0}, .radius = 1.0f };
    CHECK(SphereOverlap(q1, q2), "Sphere overlap");
    CHECK(!SphereOverlap(q1, q3), "Sphere miss");

    // GroundSnap — falls, lands on floor, grounded flips true
    Vector3 pos = {0, 5.0f, 0};
    float velY = 0.0f;
    bool grounded = false;
    for (int step = 0; step < 120; step++) {
        GroundSnap(&pos, &velY, 1.0f/60.0f, 20.0f, 0.0f, &grounded);
    }
    CHECK(NEAR(pos.y, 0.0f, 1e-4f), "GroundSnap lands on floor");
    CHECK(velY == 0.0f,              "GroundSnap zeroes velY on floor");
    CHECK(grounded,                  "GroundSnap flips grounded true");

    // SlideXZ — wall at x=1.5 blocks X movement but allows Z movement.
    AABB wall = { .center = {2.0f, 0, 0}, .half = {0.5f, 1, 0.5f} };
    s_collide_ctx.wall = wall;
    Vector3 pp = {0, 0, 0};
    SlideXZ(&pp, (Vector3){5.0f, 0, 0}, 0.4f, test_blocked, &s_collide_ctx);
    CHECK(pp.x > 0.0f && pp.x < 1.5f,  "SlideXZ advances then blocks on X");
    pp = (Vector3){0, 0, 0};
    SlideXZ(&pp, (Vector3){0, 0, 5.0f}, 0.4f, test_blocked, &s_collide_ctx);
    CHECK(NEAR(pp.z, 5.0f, 1e-5f),     "SlideXZ free on Z");
}
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
