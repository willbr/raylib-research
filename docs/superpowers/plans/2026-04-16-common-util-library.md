# Common Utility Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a header-only utility library under `common/util/` that consolidates input, math, entity pools, camera rigs, AABB/slide collision, particles, HUD widgets, and debug overlays. Validate with a reference migration of `fps/main.c`.

**Architecture:** Eight `static inline` headers under `common/util/`, each with a single responsibility and minimal inter-deps. Particle + screen-shake code relocates from `common/objects3d.h` into `util/fx.h`; `objects3d.h` gains one `#include "util/fx.h"` line so existing callers stay untouched. A `util-tests/main.c` smoke-test executable is added to `build.zig` — it exercises every pure-logic function with `assert`, and serves as the TDD harness for this plan. Verification is the smoke test plus a manual playtest of the migrated `fps` binary.

**Tech Stack:** C99, raylib + raymath (already in the project), Zig build system (`build.zig`), Clang via Zig cc. No new external dependencies.

**Spec reference:** `docs/superpowers/specs/2026-04-16-common-util-library-design.md`.

---

## File Structure

**Created:**
- `common/util/math.h` — angle wraps, signs, random floats, easing
- `common/util/pool.h` — `POOL_SPAWN`, `POOL_FOREACH`, `POOL_RING_PUSH`, `POOL_COUNT_ACTIVE`
- `common/util/collide.h` — `AABB`, `Sphere`, `Capsule`, overlap, `GroundSnap`, `SlideXZ`
- `common/util/camera.h` — `CamFPS`, `CamThirdPerson`, `CamChase`, `CamTopDown`
- `common/util/fx.h` — 2D + 3D particles, `ScreenShake` (moved from objects3d.h)
- `common/util/input.h` — movement/look helpers, cursor lock, action buttons
- `common/util/hud.h` — health bar, crosshair, damage vignette, letterbox, debug text
- `common/util/debug.h` — F3 overlay, `DebugDrawAABB`, `DebugDrawSphere`
- `util-tests/main.c` — smoke-test executable; asserts + compile-includes

**Modified:**
- `common/objects3d.h` — delete `Particle3D`, particle fns, and `ScreenShake` block (lines 149-238); add `#include "util/fx.h"` immediately after the existing includes
- `build.zig` — add `"util-tests"` to the projects tuple
- `fps/main.c` — reference migration (Task 10)

**Unchanged:** `common/sprites2d.h`, `common/map3d.h`, all other prototypes.

---

## Conventions used in this plan

- Paths are absolute from the repo root. All commands assume working dir = repo root.
- Every `.h` uses the same guard style: `#ifndef UTIL_<NAME>_H / #define / #endif`.
- Angles are radians.
- `__extension__ ({ ... })` (GNU statement expression) is used where a macro must return a value. Zig cc (Clang) supports this; it's also supported by GCC.
- `__typeof__` (GNU) is used for generic iterator macros. Same compiler support.
- The smoke test prints `FAIL: <label>` for each failed assertion and `ALL PASSED` / `FAILED` at the end. It exits non-zero on any failure.
- Commits are made after each task is fully green. Commit message style matches recent history (lowercase imperative, subject line under ~72 chars, one short body paragraph).

---

## Task 0: Bootstrap — directory, empty headers, smoke-test harness, build step

**Files:**
- Create: `common/util/math.h`
- Create: `common/util/pool.h`
- Create: `common/util/collide.h`
- Create: `common/util/camera.h`
- Create: `common/util/fx.h`
- Create: `common/util/input.h`
- Create: `common/util/hud.h`
- Create: `common/util/debug.h`
- Create: `util-tests/main.c`
- Modify: `build.zig`

- [ ] **Step 1: Create directory structure**

Run:
```bash
mkdir -p common/util util-tests
```

- [ ] **Step 2: Create the eight empty header stubs**

Each stub is three lines. Use Write eight times. Example for `common/util/math.h`:

```c
#ifndef UTIL_MATH_H
#define UTIL_MATH_H
#endif
```

Repeat with guard names `UTIL_POOL_H`, `UTIL_COLLIDE_H`, `UTIL_CAMERA_H`, `UTIL_FX_H`, `UTIL_INPUT_H`, `UTIL_HUD_H`, `UTIL_DEBUG_H` for the seven other files.

- [ ] **Step 3: Create the smoke-test harness**

Write `util-tests/main.c`:

```c
// util-tests: smoke test for common/util/*.h
// Exercises every pure-logic function with assertions.
// Include-only for modules that require a raylib window (input/hud/debug).
// Exits non-zero if any assertion fails.

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
    printf("\n%s (%d failure%s)\n", g_fails ? "FAILED" : "ALL PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
```

- [ ] **Step 4: Add `util-tests` to `build.zig`**

Open `build.zig` and edit the `projects` tuple on line 3 so it includes `"util-tests"` at the end:

Old:
```zig
const projects = .{ "fps", "rally", "3rd-person", "rts", "soccer", "kart", "platformer", "zelda", "micromachines", "skate", "resi", "wipeout", "editor", "snowboard", "rpgbattle", "biplane", "goldensun", "starfox", "boat", "pokemon", "fighter" };
```

New:
```zig
const projects = .{ "fps", "rally", "3rd-person", "rts", "soccer", "kart", "platformer", "zelda", "micromachines", "skate", "resi", "wipeout", "editor", "snowboard", "rpgbattle", "biplane", "goldensun", "starfox", "boat", "pokemon", "fighter", "util-tests" };
```

No other build.zig changes needed — the existing inline loop already builds `<name>/main.c` as an executable with raylib linked.

- [ ] **Step 5: Verify smoke test builds and runs**

Run:
```bash
zig build util-tests
./zig-out/bin/util-tests
```

Expected output:
```
ALL PASSED (0 failures)
```

Exit code 0.

- [ ] **Step 6: Verify nothing else broke**

Run:
```bash
zig build
```

Expected: all 22 projects build (21 games + util-tests). Non-zero exit is a failure of this task.

- [ ] **Step 7: Commit**

```bash
git add common/util/ util-tests/ build.zig
git commit -m "$(cat <<'EOF'
bootstrap common/util/ layout and util-tests harness

Adds empty util headers under common/util/ and a util-tests executable
wired into build.zig. Each header is just a guard for now; subsequent
tasks fill them in with TDD assertions in util-tests/main.c.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 1: math.h — angle wraps, sign, approach, ease, random

**Files:**
- Modify: `common/util/math.h`
- Modify: `util-tests/main.c` (the `test_math` stub)

- [ ] **Step 1: Write the failing test body**

Replace the empty `test_math` stub in `util-tests/main.c` with:

```c
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
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
zig build util-tests 2>&1 | head -40
```

Expected: compile failure — `SignF`, `WrapAnglePi`, etc. are undeclared.

- [ ] **Step 3: Write `math.h`**

Overwrite `common/util/math.h` with:

```c
// math.h — angle wrap, sign, approach, easing, random helpers
#ifndef UTIL_MATH_H
#define UTIL_MATH_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>

#ifndef TAU
#define TAU (6.283185307179586f)
#endif

static inline float WrapAnglePi(float a) {
    // Fast-path for typical values; fall back to fmod for large inputs.
    while (a >   PI) a -= TAU;
    while (a <= -PI) a += TAU;
    return a;
}

static inline float WrapAngle2Pi(float a) {
    a = fmodf(a, TAU);
    if (a < 0.0f) a += TAU;
    return a;
}

static inline float AngleDiff(float target, float source) {
    return WrapAnglePi(target - source);
}

static inline float AngleLerp(float a, float b, float t) {
    return a + AngleDiff(b, a) * t;
}

static inline float SignF(float x) {
    if (x > 0.0f) return  1.0f;
    if (x < 0.0f) return -1.0f;
    return 0.0f;
}

static inline float ApproachF(float cur, float target, float step) {
    if (cur < target) { cur += step; if (cur > target) cur = target; }
    else if (cur > target) { cur -= step; if (cur < target) cur = target; }
    return cur;
}

static inline float SmoothStep(float a, float b, float t) {
    if (b == a) return a;
    float x = (t - a) / (b - a);
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static inline float EaseOutCubic(float t) {
    float x = 1.0f - t;
    return 1.0f - x * x * x;
}

static inline float EaseInOutCubic(float t) {
    return (t < 0.5f) ? (4.0f * t * t * t)
                      : (1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f);
}

static inline float RandF(float a, float b) {
    return a + ((float)GetRandomValue(0, 10000) / 10000.0f) * (b - a);
}

#define RAND_F(a, b)  RandF((a), (b))

static inline Vector2 RandOnUnitCircle(void) {
    float a = RandF(0.0f, TAU);
    return (Vector2){ cosf(a), sinf(a) };
}

static inline Vector3 RandOnUnitSphere(void) {
    // Marsaglia 1972
    float z = RandF(-1.0f, 1.0f);
    float t = RandF(0.0f, TAU);
    float r = sqrtf(1.0f - z * z);
    return (Vector3){ r * cosf(t), z, r * sinf(t) };
}

static inline Vector3 RandInUnitSphere(void) {
    Vector3 p = RandOnUnitSphere();
    float r = cbrtf(RandF(0.0f, 1.0f));
    return (Vector3){ p.x * r, p.y * r, p.z * r };
}

#endif // UTIL_MATH_H
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED (0 failures)`. Exit code 0.

- [ ] **Step 5: Commit**

```bash
git add common/util/math.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/math.h: angle wrap, sign, approach, easing, random helpers

Covers what every prototype currently open-codes: WrapAnglePi,
SignF, ApproachF, SmoothStep, EaseOutCubic, RandF, RandOnUnitSphere.
Smoke-tested in util-tests.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: pool.h — spawn, iterate, ring push, count

**Files:**
- Modify: `common/util/pool.h`
- Modify: `util-tests/main.c` (the `test_pool` stub)

- [ ] **Step 1: Write the failing test body**

Replace `test_pool` in `util-tests/main.c`:

```c
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
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
zig build util-tests 2>&1 | head -20
```

Expected: compile failure — `POOL_SPAWN`, `POOL_FOREACH`, etc. undeclared.

- [ ] **Step 3: Write `pool.h`**

Overwrite `common/util/pool.h`:

```c
// pool.h — entity-pool helpers built on a `.active` bool convention.
// Caller owns the storage; macros iterate / spawn / count.
#ifndef UTIL_POOL_H
#define UTIL_POOL_H

#include <stdbool.h>

// Find first inactive slot in (arr, N), mark active=true, return its index.
// Returns -1 if pool is full. GNU statement expression (supported by Zig cc / Clang / GCC).
//
// Usage:  int i = POOL_SPAWN(bullets, MAX_BULLETS);
//         if (i >= 0) { bullets[i].pos = ...; }
#define POOL_SPAWN(arr, N) __extension__ ({ \
    int _found_ = -1; \
    for (int _i_ = 0; _i_ < (int)(N); _i_++) { \
        if (!(arr)[_i_].active) { \
            (arr)[_i_].active = true; \
            _found_ = _i_; \
            break; \
        } \
    } \
    _found_; \
})

// Iterate a typed pointer over every slot (active or not). The caller filters
// on `p->active` as needed. Single `for`, so `break` and `continue` behave
// exactly as written.
//
// Usage:  POOL_FOREACH(bullets, MAX_BULLETS, b) { if (!b->active) continue; ... }
#define POOL_FOREACH(arr, N, p) \
    for (__typeof__(&(arr)[0]) p = &(arr)[0]; p < &(arr)[(N)]; p++)

// Ring-buffer push: marks (arr)[idx_var] active, advances idx_var modulo N.
// Use for decals / tracers / oldest-overwrite particle sources.
//
// Usage:  int decalIdx = 0; ... POOL_RING_PUSH(decals, MAX_DECALS, decalIdx);
#define POOL_RING_PUSH(arr, N, idx_var) do { \
    (arr)[(idx_var)].active = true; \
    (idx_var) = ((idx_var) + 1) % (int)(N); \
} while (0)

// Count active slots.
#define POOL_COUNT_ACTIVE(arr, N) __extension__ ({ \
    int _c_ = 0; \
    for (int _i_ = 0; _i_ < (int)(N); _i_++) if ((arr)[_i_].active) _c_++; \
    _c_; \
})

#endif // UTIL_POOL_H
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`. Exit 0.

- [ ] **Step 5: Commit**

```bash
git add common/util/pool.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/pool.h: POOL_SPAWN / POOL_FOREACH / POOL_RING_PUSH

Replaces the 21× copy-paste of "for (int i=0; i<N; i++) if (!arr[i].active)
{ arr[i] = ...; break; }" across prototypes. Uses GNU statement expressions
and __typeof__ (Clang/GCC). Break/continue in POOL_FOREACH behave naturally
because the macro is a single for-loop.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: collide.h — AABB, Sphere, Capsule, GroundSnap, SlideXZ

`SlideXZ` takes a user-supplied `blocked` callback rather than an AABB array, so callers with multiple obstacle sources (e.g. fps's walls + props) can express their check in one function.

**Files:**
- Modify: `common/util/collide.h`
- Modify: `util-tests/main.c` (the `test_collide` stub)

- [ ] **Step 1: Write the failing test body**

Replace `test_collide`:

```c
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
    // Callback closes over a single AABB via user-data.
    AABB wall = { .center = {2.0f, 0, 0}, .half = {0.5f, 1, 0.5f} };
    struct { AABB wall; } ctx = { wall };
    bool test_blocked(Vector3 p, float r, void *u) {
        AABB w = ((__typeof__(&ctx))u)->wall;
        return fabsf(p.x - w.center.x) < (w.half.x + r)
            && fabsf(p.z - w.center.z) < (w.half.z + r)
            && fabsf(p.y - w.center.y) < (w.half.y + r);
    }
    Vector3 pp = {0, 0, 0};
    SlideXZ(&pp, (Vector3){5.0f, 0, 0}, 0.4f, test_blocked, &ctx);
    CHECK(pp.x < 1.5f,                 "SlideXZ blocked on X");
    pp = (Vector3){0, 0, 0};
    SlideXZ(&pp, (Vector3){0, 0, 5.0f}, 0.4f, test_blocked, &ctx);
    CHECK(NEAR(pp.z, 5.0f, 1e-5f),     "SlideXZ free on Z");
}
```

Note: nested function `test_blocked` is a GNU extension and is supported by Zig cc; if it causes trouble on a particular build, lift it to file scope with the `ctx` widened into a file-static variable.

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
zig build util-tests 2>&1 | head -30
```

Expected: compile failure — `AABB`, `GroundSnap`, `SlideXZ` etc. undeclared.

- [ ] **Step 3: Write `collide.h`**

Overwrite `common/util/collide.h`:

```c
// collide.h — AABB/Sphere/Capsule geometry, ground snap, wall slide.
#ifndef UTIL_COLLIDE_H
#define UTIL_COLLIDE_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>
#include <math.h>

typedef struct { Vector3 center, half;   } AABB;
typedef struct { Vector3 center; float radius; } Sphere;
typedef struct { Vector3 a, b;  float radius; } Capsule;

static inline bool AABBOverlap(AABB a, AABB b) {
    return fabsf(a.center.x - b.center.x) < (a.half.x + b.half.x)
        && fabsf(a.center.y - b.center.y) < (a.half.y + b.half.y)
        && fabsf(a.center.z - b.center.z) < (a.half.z + b.half.z);
}

static inline bool AABBvsSphere(AABB a, Sphere s) {
    float qx = Clamp(s.center.x, a.center.x - a.half.x, a.center.x + a.half.x);
    float qy = Clamp(s.center.y, a.center.y - a.half.y, a.center.y + a.half.y);
    float qz = Clamp(s.center.z, a.center.z - a.half.z, a.center.z + a.half.z);
    float dx = s.center.x - qx, dy = s.center.y - qy, dz = s.center.z - qz;
    return (dx*dx + dy*dy + dz*dz) < (s.radius * s.radius);
}

static inline bool SphereOverlap(Sphere a, Sphere b) {
    float d2 = Vector3DistanceSqr(a.center, b.center);
    float r = a.radius + b.radius;
    return d2 < r * r;
}

static inline float PointToAABBDist(Vector3 p, AABB a) {
    float qx = Clamp(p.x, a.center.x - a.half.x, a.center.x + a.half.x);
    float qy = Clamp(p.y, a.center.y - a.half.y, a.center.y + a.half.y);
    float qz = Clamp(p.z, a.center.z - a.half.z, a.center.z + a.half.z);
    float dx = p.x - qx, dy = p.y - qy, dz = p.z - qz;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Integrate gravity, clamp to floorY, set grounded. Pass grounded=NULL if unused.
static inline void GroundSnap(Vector3 *pos, float *velY, float dt, float gravity,
                              float floorY, bool *grounded) {
    *velY -= gravity * dt;
    pos->y += (*velY) * dt;
    if (pos->y <= floorY) {
        pos->y = floorY;
        *velY = 0.0f;
        if (grounded) *grounded = true;
    } else {
        if (grounded) *grounded = false;
    }
}

// Callback: return true if (pos, radius) collides with a wall.
typedef bool (*UtilBlockedFn)(Vector3 pos, float radius, void *user);

// Try to move by `delta` (only X and Z consumed), sliding along walls.
// Per-axis separate resolve, matching fps/3rd-person. `radius` is the player's
// horizontal cylinder radius. Y is not touched — use GroundSnap for that.
static inline void SlideXZ(Vector3 *pos, Vector3 delta, float radius,
                           UtilBlockedFn blocked, void *user) {
    Vector3 tryPos = *pos;
    tryPos.x += delta.x;
    if (!blocked(tryPos, radius, user)) pos->x = tryPos.x;
    tryPos = *pos;
    tryPos.z += delta.z;
    if (!blocked(tryPos, radius, user)) pos->z = tryPos.z;
}

#endif // UTIL_COLLIDE_H
```

- [ ] **Step 4: Run test to verify it passes**

```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`. Exit 0.

- [ ] **Step 5: Commit**

```bash
git add common/util/collide.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/collide.h: AABB/Sphere types, GroundSnap, SlideXZ

Consolidates the ~15× duplicated gravity+floor snap and per-axis slide
pattern. SlideXZ takes a user blocked-callback so callers with
heterogeneous obstacle sources (walls + props + tiles) express the
check in one function.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: camera.h — FPS, ThirdPerson, Chase, TopDown rigs

**Files:**
- Modify: `common/util/camera.h`
- Modify: `util-tests/main.c` (the `test_camera` stub)

- [ ] **Step 1: Write the failing test body**

Replace `test_camera`:

```c
static void test_camera(void) {
    // CamFPS: yaw=0 → forward is -Z. yaw=PI/2 → forward is +X.
    CamFPS fps = CamFPSInit((Vector3){0, 1, 0});
    fps.yaw = 0.0f; fps.pitch = 0.0f;
    Camera3D c = CamFPSToCamera3D(&fps);
    CHECK(NEAR(c.target.x - c.position.x, 0.0f, 1e-4f), "CamFPS yaw=0 target x");
    CHECK(c.target.z - c.position.z < -0.9f,           "CamFPS yaw=0 target -Z");

    fps.yaw = PI / 2.0f;
    c = CamFPSToCamera3D(&fps);
    CHECK(c.target.x - c.position.x > 0.9f,            "CamFPS yaw=π/2 target +X");

    // CamChase: after many updates, _pos converges to behind target at height
    CamChase chase = CamChaseInit((Vector3){0, 0, 0}, 0.0f);
    chase.lagSpeed = 20.0f;  // fast convergence for the test
    for (int step = 0; step < 200; step++) {
        CamChaseUpdate(&chase, (Vector3){0, 0, 0}, 0.0f, 1.0f / 60.0f);
    }
    CHECK(chase._pos.z < -chase.dist * 0.9f,           "CamChase behind target");
    CHECK(NEAR(chase._pos.y, chase.height, 0.01f),     "CamChase at height");

    // CamTopDown: camera above target, target equal
    CamTopDown td = CamTopDownInit((Vector3){3, 0, 4});
    CamTopDownUpdate(&td, (Vector3){3, 0, 4});
    c = CamTopDownToCamera3D(&td);
    CHECK(c.position.y > 1.0f,              "CamTopDown camera above");
    CHECK(NEAR(c.target.x, 3.0f, 1e-4f),    "CamTopDown target x");
    CHECK(NEAR(c.target.z, 4.0f, 1e-4f),    "CamTopDown target z");

    // CamThirdPerson: target equals what we passed; position at dist behind
    CamThirdPerson tp = CamThirdPersonInit((Vector3){0, 1, 0});
    tp.yaw = 0.0f; tp.pitch = 0.0f; tp.shoulder = (Vector3){0, 0.5f, 0};
    CamThirdPersonUpdate(&tp, (Vector3){0, 1, 0}, 1.0f / 60.0f);
    c = CamThirdPersonToCamera3D(&tp);
    CHECK(c.position.z > tp.dist * 0.9f,     "CamThirdPerson behind target +Z");
}
```

The `CamFPS` forward-sign convention here is: yaw=0 → looking down -Z, yaw=+π/2 → looking down +X. This matches standard FPS mouse-look where mouse-right adds to yaw and rotates the view clockwise (looking down from above).

- [ ] **Step 2: Run test to verify it fails**

```bash
zig build util-tests 2>&1 | head -30
```

Expected: compile failure — `CamFPS`, `CamChaseInit`, etc. undeclared.

- [ ] **Step 3: Write `camera.h`**

Overwrite `common/util/camera.h`:

```c
// camera.h — four raylib-camera rigs: FPS, ThirdPerson, Chase, TopDown.
#ifndef UTIL_CAMERA_H
#define UTIL_CAMERA_H

#include "raylib.h"
#include "raymath.h"
#include "math.h"
#include <math.h>

typedef struct { Vector3 pos; float yaw, pitch; float fov; } CamFPS;
typedef struct {
    Vector3 target;
    float yaw, pitch, dist;
    Vector3 shoulder;   // x = lateral, y = vertical eye-height offset
    float fov;
} CamThirdPerson;
typedef struct {
    Vector3 target;
    float targetYaw;
    float dist, height, lagSpeed;
    float fov;
    Vector3 _pos;       // internal: lagged world-space position
} CamChase;
typedef struct { Vector3 target; float height, angle; float fov; } CamTopDown;

static inline CamFPS CamFPSInit(Vector3 pos) {
    return (CamFPS){ .pos = pos, .yaw = 0.0f, .pitch = 0.0f, .fov = 75.0f };
}

static inline CamThirdPerson CamThirdPersonInit(Vector3 target) {
    return (CamThirdPerson){
        .target = target, .yaw = 0.0f, .pitch = -0.15f, .dist = 4.0f,
        .shoulder = (Vector3){ 0.7f, 1.5f, 0.0f }, .fov = 70.0f,
    };
}

static inline CamChase CamChaseInit(Vector3 target, float targetYaw) {
    Vector3 pos = target;
    pos.y += 2.5f;
    pos.x -= 6.0f * sinf(targetYaw);
    pos.z -= 6.0f * cosf(targetYaw);
    return (CamChase){
        .target = target, .targetYaw = targetYaw,
        .dist = 6.0f, .height = 2.5f, .lagSpeed = 5.0f, .fov = 70.0f,
        ._pos = pos,
    };
}

static inline CamTopDown CamTopDownInit(Vector3 target) {
    return (CamTopDown){ .target = target, .height = 15.0f, .angle = 0.0f, .fov = 60.0f };
}

static inline void CamFPSUpdate(CamFPS *c, float dt) {
    (void)c; (void)dt;  // state is caller-driven (via input.h); Update exists for API symmetry.
}

static inline void CamThirdPersonUpdate(CamThirdPerson *c, Vector3 target, float dt) {
    (void)dt;
    c->target = target;
}

static inline void CamChaseUpdate(CamChase *c, Vector3 target, float targetYaw, float dt) {
    c->target = target;
    c->targetYaw = targetYaw;
    Vector3 desired = target;
    desired.x -= c->dist * sinf(targetYaw);
    desired.z -= c->dist * cosf(targetYaw);
    desired.y += c->height;
    float t = 1.0f - expf(-c->lagSpeed * dt);
    c->_pos.x += (desired.x - c->_pos.x) * t;
    c->_pos.y += (desired.y - c->_pos.y) * t;
    c->_pos.z += (desired.z - c->_pos.z) * t;
}

static inline void CamTopDownUpdate(CamTopDown *c, Vector3 target) {
    c->target = target;
}

// Forward convention: yaw=0 → -Z; yaw=+π/2 → +X. Pitch is up-positive.
static inline Camera3D CamFPSToCamera3D(const CamFPS *c) {
    float cy = cosf(c->yaw), sy = sinf(c->yaw);
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    Vector3 fwd = (Vector3){ sy * cp, sp, -cy * cp };
    return (Camera3D){
        .position   = c->pos,
        .target     = Vector3Add(c->pos, fwd),
        .up         = (Vector3){0, 1, 0},
        .fovy       = c->fov,
        .projection = CAMERA_PERSPECTIVE,
    };
}

static inline Camera3D CamThirdPersonToCamera3D(const CamThirdPerson *c) {
    float cy = cosf(c->yaw), sy = sinf(c->yaw);
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    // Camera sits behind target on the forward axis, plus shoulder offset on right
    Vector3 behind = (Vector3){ -sy * cp,  -sp, cy * cp };
    Vector3 right  = (Vector3){ cy,         0,  sy };
    Vector3 pos = c->target;
    pos.x += behind.x * c->dist + right.x * c->shoulder.x;
    pos.y += behind.y * c->dist + c->shoulder.y;
    pos.z += behind.z * c->dist + right.z * c->shoulder.x;
    Vector3 aim = c->target;
    aim.y += c->shoulder.y;
    return (Camera3D){
        .position = pos, .target = aim, .up = (Vector3){0, 1, 0},
        .fovy = c->fov, .projection = CAMERA_PERSPECTIVE,
    };
}

static inline Camera3D CamChaseToCamera3D(const CamChase *c) {
    return (Camera3D){
        .position = c->_pos, .target = c->target, .up = (Vector3){0, 1, 0},
        .fovy = c->fov, .projection = CAMERA_PERSPECTIVE,
    };
}

static inline Camera3D CamTopDownToCamera3D(const CamTopDown *c) {
    return (Camera3D){
        .position = (Vector3){ c->target.x, c->target.y + c->height, c->target.z + 0.001f },
        .target   = c->target,
        .up       = (Vector3){0, 0, -1},
        .fovy     = c->fov,
        .projection = CAMERA_PERSPECTIVE,
    };
}

#endif // UTIL_CAMERA_H
```

- [ ] **Step 4: Run test to verify it passes**

```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`. Exit 0.

- [ ] **Step 5: Commit**

```bash
git add common/util/camera.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/camera.h: CamFPS / CamThirdPerson / CamChase / CamTopDown

Four camera rigs with state structs + Init / Update / ToCamera3D. Input
(mouse / gamepad) is caller-driven via util/input.h; camera.h only turns
state into a raylib Camera3D. Forward convention pinned: yaw=0 → -Z.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: fx.h — 2D + 3D particles, screen shake

Before writing fx.h, move the existing definitions out of `common/objects3d.h` — but delay that edit until Task 6. In Task 5 we just *copy* the existing 3D code into fx.h (adapted) and add 2D alongside. `objects3d.h` keeps its copy for now; this yields a temporary duplicate-symbol situation that Task 6 resolves. That keeps each task green in isolation.

Not quite: duplicate `static inline` symbols in different headers that are transitively included by the same TU will cause a redefinition error as soon as any game includes both `objects3d.h` and `fx.h`. **No game includes both yet** (fx.h is only included by util-tests, not by objects3d.h or any prototype). So Task 5 is safe; Task 6 does the swap atomically.

**Files:**
- Modify: `common/util/fx.h`
- Modify: `util-tests/main.c` (the `test_fx` stub)

- [ ] **Step 1: Write the failing test body**

Replace `test_fx`:

```c
static void test_fx(void) {
    // 3D particle pool spawn / update / decay
    Particle3D p3[16] = {0};
    SetRandomSeed(42);
    SpawnParticleBurst(p3, 16, (Vector3){0, 0, 0}, 8, 1.0f, 2.0f, 0.5f, 0.5f, 0.1f, 0.1f);
    CHECK(POOL_COUNT_ACTIVE(p3, 16) == 8, "SpawnParticleBurst spawns count");

    for (int s = 0; s < 60; s++) UpdateParticles3D(p3, 16, 1.0f / 30.0f, 10.0f);
    // After 2s of gravity, all particles with life ≤ 0.5s are dead
    CHECK(POOL_COUNT_ACTIVE(p3, 16) == 0, "UpdateParticles3D decays to zero");

    // 2D particle pool
    Particle2D p2[16] = {0};
    SpawnParticleBurst2D(p2, 16, (Vector2){0, 0}, RED, 6,
                         10.0f, 20.0f, 0.5f, 0.5f, 2.0f, 4.0f);
    CHECK(POOL_COUNT_ACTIVE(p2, 16) == 6, "SpawnParticleBurst2D spawns");
    for (int s = 0; s < 60; s++) UpdateParticles2D(p2, 16, 1.0f / 30.0f, (Vector2){0, 200.0f});
    CHECK(POOL_COUNT_ACTIVE(p2, 16) == 0, "UpdateParticles2D decays");

    // ScreenShake
    ScreenShake sh = {0};
    ShakeTrigger(&sh, 0.5f);
    CHECK(sh.timer > 0.0f, "ShakeTrigger sets timer");
    for (int s = 0; s < 60; s++) ShakeUpdate(&sh, 0.02f);
    CHECK(sh.timer == 0.0f, "ShakeUpdate decays to zero");
    // Offset with no time left returns zero
    Vector2 off = ShakeOffset(&sh);
    CHECK(off.x == 0.0f && off.y == 0.0f, "ShakeOffset zero when done");
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
zig build util-tests 2>&1 | head -20
```

Expected: compile failure — `Particle3D` is only defined via the transitive include chain from `objects3d.h`, and we haven't touched that yet, so `fx.h` is empty and its symbols are undeclared. Note the smoke test directly includes `fx.h` but not `objects3d.h`; therefore `Particle3D` / `SpawnParticleBurst` / `ScreenShake` are genuinely undefined here.

- [ ] **Step 3: Write `fx.h`**

Overwrite `common/util/fx.h`:

```c
// fx.h — 2D + 3D particle pools and screen shake.
// The 3D particle API (SpawnParticleBurst / UpdateParticles3D / DrawParticles3D)
// and ScreenShake originated in common/objects3d.h; this header is now their
// canonical home. objects3d.h re-includes this file so legacy callers are
// unaffected.
#ifndef UTIL_FX_H
#define UTIL_FX_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>
#include <math.h>

// ---- 3D particles ---------------------------------------------------------

typedef struct {
    Vector3 pos, vel;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} Particle3D;

// Spawn a burst of particles at pos. Colors cycle through fire orange, fire
// yellow, gray smoke (same behaviour as the original objects3d.h version).
static inline void SpawnParticleBurst(Particle3D *particles, int maxParticles,
                                      Vector3 pos, int count,
                                      float speedMin, float speedMax,
                                      float lifeMin,  float lifeMax,
                                      float sizeMin,  float sizeMax) {
    for (int i = 0; i < maxParticles && count > 0; i++) {
        if (particles[i].active) continue;
        float a1 = (float)GetRandomValue(0, 628) / 100.0f;
        float a2 = (float)GetRandomValue(-314, 314) / 200.0f;
        float spd = speedMin + (float)GetRandomValue(0, 100) / 100.0f * (speedMax - speedMin);
        particles[i].pos = pos;
        particles[i].vel = (Vector3){
            cosf(a1) * cosf(a2) * spd,
            fabsf(sinf(a2)) * spd + 2.0f,
            sinf(a1) * cosf(a2) * spd,
        };
        int roll = GetRandomValue(0, 2);
        if (roll == 0)      particles[i].color = (Color){255, 200,  50, 255};
        else if (roll == 1) particles[i].color = (Color){255, 100,   0, 255};
        else                particles[i].color = (Color){ 80,  80,  80, 200};
        particles[i].life    = lifeMin + (float)GetRandomValue(0, 100) / 100.0f * (lifeMax - lifeMin);
        particles[i].maxLife = particles[i].life;
        particles[i].size    = sizeMin + (float)GetRandomValue(0, 100) / 100.0f * (sizeMax - sizeMin);
        particles[i].active  = true;
        count--;
    }
}

static inline void UpdateParticles3D(Particle3D *particles, int maxParticles,
                                     float dt, float gravity) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        particles[i].pos = Vector3Add(particles[i].pos, Vector3Scale(particles[i].vel, dt));
        particles[i].vel.y -= gravity * dt;
        particles[i].life  -= dt;
        if (particles[i].life <= 0.0f) particles[i].active = false;
    }
}

static inline void DrawParticles3D(Particle3D *particles, int maxParticles) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        float a = particles[i].life / particles[i].maxLife;
        Color c = particles[i].color;
        c.a = (unsigned char)(a * c.a);
        DrawSphere(particles[i].pos, particles[i].size * a, c);
    }
}

// ---- 2D particles ---------------------------------------------------------

typedef struct {
    Vector2 pos, vel;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} Particle2D;

// Spawn a burst of 2D particles with user-supplied color.
static inline void SpawnParticleBurst2D(Particle2D *particles, int maxParticles,
                                        Vector2 pos, Color color, int count,
                                        float speedMin, float speedMax,
                                        float lifeMin,  float lifeMax,
                                        float sizeMin,  float sizeMax) {
    for (int i = 0; i < maxParticles && count > 0; i++) {
        if (particles[i].active) continue;
        float a   = (float)GetRandomValue(0, 628) / 100.0f;
        float spd = speedMin + (float)GetRandomValue(0, 100) / 100.0f * (speedMax - speedMin);
        particles[i].pos = pos;
        particles[i].vel = (Vector2){ cosf(a) * spd, sinf(a) * spd };
        particles[i].color   = color;
        particles[i].life    = lifeMin + (float)GetRandomValue(0, 100) / 100.0f * (lifeMax - lifeMin);
        particles[i].maxLife = particles[i].life;
        particles[i].size    = sizeMin + (float)GetRandomValue(0, 100) / 100.0f * (sizeMax - sizeMin);
        particles[i].active  = true;
        count--;
    }
}

// Gravity here is a 2D vector (pixels/sec²). (0, 200) is typical downward.
static inline void UpdateParticles2D(Particle2D *particles, int maxParticles,
                                     float dt, Vector2 gravity) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        particles[i].vel.x += gravity.x * dt;
        particles[i].vel.y += gravity.y * dt;
        particles[i].pos.x += particles[i].vel.x * dt;
        particles[i].pos.y += particles[i].vel.y * dt;
        particles[i].life  -= dt;
        if (particles[i].life <= 0.0f) particles[i].active = false;
    }
}

static inline void DrawParticles2D(Particle2D *particles, int maxParticles) {
    for (int i = 0; i < maxParticles; i++) {
        if (!particles[i].active) continue;
        float a = particles[i].life / particles[i].maxLife;
        Color c = particles[i].color;
        c.a = (unsigned char)(a * c.a);
        DrawCircleV(particles[i].pos, particles[i].size, c);
    }
}

// ---- Screen shake ---------------------------------------------------------

typedef struct {
    float amount;
    float timer;
    float decay;
} ScreenShake;

static inline void ShakeTrigger(ScreenShake *s, float amount) {
    s->amount = amount;
    s->timer  = amount;
}

static inline void ShakeUpdate(ScreenShake *s, float dt) {
    if (s->timer > 0.0f) {
        s->timer -= dt;
        if (s->timer < 0.0f) s->timer = 0.0f;
    }
}

static inline Vector2 ShakeOffset(ScreenShake *s) {
    if (s->timer <= 0.0f) return (Vector2){0, 0};
    float intensity = s->timer / s->amount;
    return (Vector2){
        (float)GetRandomValue(-100, 100) / 100.0f * s->amount * intensity,
        (float)GetRandomValue(-100, 100) / 100.0f * s->amount * intensity,
    };
}

#endif // UTIL_FX_H
```

- [ ] **Step 4: Run test to verify it passes**

```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`. Exit 0.

- [ ] **Step 5: Check that the duplicate-symbol situation is benign**

Run:
```bash
zig build
```

Expected: all 22 projects still build. `objects3d.h` still contains its copy of these definitions, but no translation unit includes both `objects3d.h` and `util/fx.h`:
- `util-tests/main.c` includes `util/fx.h` only.
- Every other game includes `objects3d.h` (some also include `sprites2d.h` / `map3d.h`) but none include `util/fx.h`.

If this step fails with a redefinition error, it means a header we don't own transitively includes one of these paths. Investigate before proceeding.

- [ ] **Step 6: Commit**

```bash
git add common/util/fx.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/fx.h: 2D + 3D particles and screen shake

3D particle API (SpawnParticleBurst / UpdateParticles3D / DrawParticles3D)
and ScreenShake mirror the existing objects3d.h implementations verbatim
— they will migrate out of objects3d.h in the next commit. Adds a new 2D
particle API alongside.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Retire the relocated code from `objects3d.h`

Delete the now-duplicate 3D particle + screen-shake blocks from `objects3d.h`, then add a single `#include "util/fx.h"` so the identifiers stay visible to every existing caller.

**Files:**
- Modify: `common/objects3d.h`

- [ ] **Step 1: Delete lines 149-238 of `common/objects3d.h`**

Remove this entire block, which starts with `// --- Particle system ---` and ends at `// Draw a rotated cube using 12 triangles (6 faces)` (exclusive — that comment marks the next section, which stays). Concretely, the deleted block contains:

- The `// --- Particle system ---` header comment
- `typedef struct ... Particle3D;`
- `static inline void SpawnParticleBurst(...)` definition
- `static inline void UpdateParticles3D(...)` definition
- `static inline void DrawParticles3D(...)` definition
- The `// --- Screen shake ---` header comment
- `typedef struct ... ScreenShake;`
- `static inline void ShakeTrigger(...)` definition
- `static inline void ShakeUpdate(...)` definition
- `static inline Vector2 ShakeOffset(...)` definition

Double-check the exact bounds with:
```bash
sed -n '149,238p' common/objects3d.h
```
before deleting.

Use the Edit tool with the full particle+shake block as `old_string` and an empty string as `new_string`, being careful to preserve the surrounding blank lines so the file doesn't gain stray whitespace.

- [ ] **Step 2: Add `#include "util/fx.h"` near the top of `common/objects3d.h`**

Find the existing include block (around lines 16-20, currently `#include "raylib.h"` / `#include "raymath.h"` / `<math.h>` / `<stdio.h>` / `<string.h>`). Add `#include "util/fx.h"` as the last include in that block.

Path convention: `objects3d.h` is at `common/objects3d.h`, `fx.h` is at `common/util/fx.h`. Relative include is `"util/fx.h"`.

- [ ] **Step 3: Build every project**

Run:
```bash
zig build
```

Expected: all 22 projects build. `fps`, `3rd-person`, `rts` (which call `SpawnParticleBurst`, `UpdateParticles3D`, `DrawParticles3D`, `ShakeTrigger`, `ShakeUpdate`, `ShakeOffset`) resolve via `fx.h` and compile unchanged. `util-tests` still builds.

- [ ] **Step 4: Run the smoke test and a quick playtest**

Run:
```bash
./zig-out/bin/util-tests
```

Expected: `ALL PASSED`.

Then:
```bash
zig build run-fps
```

Launch the game, fire a weapon (particles should render as before), take damage (screen shake should work). Close the window when satisfied. This is a subjective check — the only test we have for rendering code.

- [ ] **Step 5: Commit**

```bash
git add common/objects3d.h
git commit -m "$(cat <<'EOF'
move particles + ScreenShake from objects3d.h into util/fx.h

objects3d.h now re-includes util/fx.h; the relocated definitions keep
their original names and signatures, so fps / 3rd-person / rts compile
and play identically without edits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: input.h — movement, mouse-look, gamepad, cursor, actions

Input can't be unit-tested without a raylib window, so this task is compile-only. Validation proper happens in Task 10 (fps migration).

**Files:**
- Modify: `common/util/input.h`

- [ ] **Step 1: Write `input.h`**

Overwrite `common/util/input.h`:

```c
// input.h — keyboard / mouse / gamepad normalizers.
// Requires an active raylib window; do not call at file scope.
#ifndef UTIL_INPUT_H
#define UTIL_INPUT_H

#include "raylib.h"
#include "raymath.h"
#include "math.h"
#include <math.h>
#include <stdbool.h>

#define INPUT_GAMEPAD   0
#define INPUT_DEADZONE  0.15f

// Movement input combined across WASD + arrows + left-stick.
// x = right(+) / left(-);  y = down(+) / up(-).
// Magnitude is clamped to 1 on diagonals and gamepad.
static inline Vector2 InputMoveDir2(void) {
    Vector2 v = {0, 0};
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) v.x += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  v.x -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  v.y += 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    v.y -= 1.0f;
    if (IsGamepadAvailable(INPUT_GAMEPAD)) {
        float gx = GetGamepadAxisMovement(INPUT_GAMEPAD, GAMEPAD_AXIS_LEFT_X);
        float gy = GetGamepadAxisMovement(INPUT_GAMEPAD, GAMEPAD_AXIS_LEFT_Y);
        if (fabsf(gx) > INPUT_DEADZONE) v.x += gx;
        if (fabsf(gy) > INPUT_DEADZONE) v.y += gy;
    }
    float m2 = v.x * v.x + v.y * v.y;
    if (m2 > 1.0f) {
        float m = sqrtf(m2);
        v.x /= m; v.y /= m;
    }
    return v;
}

// XZ-plane movement in world space, camera-relative. Forward (W/up) maps to
// the camera's forward vector; right (D/right) to the camera's right vector.
// Matches camera.h's CamFPS convention: yaw=0 → forward is -Z.
static inline Vector3 InputMoveDir3Flat(float camYaw) {
    Vector2 m = InputMoveDir2();
    float cy = cosf(camYaw), sy = sinf(camYaw);
    Vector3 fwd   = { sy,  0, -cy };   // yaw=0 → (0, 0, -1)
    Vector3 right = { cy,  0,  sy };   // yaw=0 → (1, 0,  0)
    float f = -m.y;   // W (up) = forward
    float r =  m.x;   // D (right) = right
    return (Vector3){
        fwd.x * f + right.x * r,
        0.0f,
        fwd.z * f + right.z * r,
    };
}

// Mouse-look integrator. `sensitivity` is radians-per-pixel (typical 0.002f).
// Pitch is clamped to ±pitchClamp radians (typical 1.55f ≈ 89°).
static inline void InputLookMouse(float *yaw, float *pitch, float sensitivity, float pitchClamp) {
    Vector2 d = GetMouseDelta();
    *yaw   += d.x * sensitivity;
    *pitch -= d.y * sensitivity;
    if (*pitch >  pitchClamp) *pitch =  pitchClamp;
    if (*pitch < -pitchClamp) *pitch = -pitchClamp;
}

// Gamepad right-stick look. `sensitivity` is radians-per-second.
static inline void InputLookStick(float *yaw, float *pitch, float sensitivity, float dt) {
    if (!IsGamepadAvailable(INPUT_GAMEPAD)) return;
    float rx = GetGamepadAxisMovement(INPUT_GAMEPAD, GAMEPAD_AXIS_RIGHT_X);
    float ry = GetGamepadAxisMovement(INPUT_GAMEPAD, GAMEPAD_AXIS_RIGHT_Y);
    if (fabsf(rx) > INPUT_DEADZONE) *yaw   += rx * sensitivity * dt;
    if (fabsf(ry) > INPUT_DEADZONE) *pitch -= ry * sensitivity * dt;
}

static inline void InputSetCursorLocked(bool locked) {
    if (locked) DisableCursor();
    else        EnableCursor();
}

static inline void InputToggleCursorLock(void) {
    if (IsCursorHidden()) EnableCursor();
    else                  DisableCursor();
}

// Either-or: keyboard key OR gamepad button.
static inline bool InputActionDown(int key, int gamepadButton) {
    if (IsKeyDown(key)) return true;
    if (IsGamepadAvailable(INPUT_GAMEPAD)
        && IsGamepadButtonDown(INPUT_GAMEPAD, gamepadButton)) return true;
    return false;
}

static inline bool InputActionPressed(int key, int gamepadButton) {
    if (IsKeyPressed(key)) return true;
    if (IsGamepadAvailable(INPUT_GAMEPAD)
        && IsGamepadButtonPressed(INPUT_GAMEPAD, gamepadButton)) return true;
    return false;
}

#endif // UTIL_INPUT_H
```

- [ ] **Step 2: Build and run smoke test (compile check)**

Run:
```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`. The smoke test already includes `input.h`, so any compile error in this header shows up here.

- [ ] **Step 3: Build all games**

Run:
```bash
zig build
```

Expected: all 22 projects still build. `input.h` isn't used by any game yet — we're only checking it didn't break anything through the shared headers.

- [ ] **Step 4: Commit**

```bash
git add common/util/input.h
git commit -m "$(cat <<'EOF'
util/input.h: WASD+arrows+pad movement, mouse-look, cursor lock

Normalizes the identical 21× WASD handler into InputMoveDir2 /
InputMoveDir3Flat. Adds InputLookMouse with pitch-clamp, gamepad
right-stick look, and InputAction{Down,Pressed} helpers. Runtime
validation happens in the fps migration (Task 10) — this header is
compile-tested only.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: hud.h — health bar, crosshair, damage vignette, letterbox, debug text

Draw-only functions; no unit tests. Compile-tested via the smoke test include.

**Files:**
- Modify: `common/util/hud.h`

- [ ] **Step 1: Write `hud.h`**

Overwrite `common/util/hud.h`:

```c
// hud.h — screen-space widgets. All coordinates are caller-supplied pixels.
// Requires an active raylib window; call only inside BeginDrawing/EndDrawing.
#ifndef UTIL_HUD_H
#define UTIL_HUD_H

#include "raylib.h"

static inline void HudHealthBar(int x, int y, int w, int h, float pct,
                                Color fill, Color bg, Color outline) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    DrawRectangle(x, y, w, h, bg);
    DrawRectangle(x, y, (int)((float)w * pct), h, fill);
    DrawRectangleLines(x, y, w, h, outline);
}

// style: 0 = cross (4 short lines around center), 1 = dot, 2 = ring.
static inline void HudCrosshair(int x, int y, int size, int thick, int style, Color color) {
    float ft = (float)thick;
    if (style == 0) {
        DrawLineEx((Vector2){(float)(x - size), (float)y},
                   (Vector2){(float)(x - thick), (float)y}, ft, color);
        DrawLineEx((Vector2){(float)(x + thick), (float)y},
                   (Vector2){(float)(x + size), (float)y}, ft, color);
        DrawLineEx((Vector2){(float)x, (float)(y - size)},
                   (Vector2){(float)x, (float)(y - thick)}, ft, color);
        DrawLineEx((Vector2){(float)x, (float)(y + thick)},
                   (Vector2){(float)x, (float)(y + size)}, ft, color);
    } else if (style == 1) {
        DrawCircle(x, y, ft, color);
    } else {
        DrawCircleLines(x, y, (float)size, color);
    }
}

static inline void HudDamageVignette(float alpha, Color color) {
    if (alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    Color c = color;
    c.a = (unsigned char)(alpha * (float)color.a);
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), c);
}

static inline void HudLetterbox(float pct, Color color) {
    if (pct <= 0.0f) return;
    int sh  = GetScreenHeight();
    int sw  = GetScreenWidth();
    int bar = (int)((float)sh * pct);
    DrawRectangle(0, 0,        sw, bar, color);
    DrawRectangle(0, sh - bar, sw, bar, color);
}

// --- Debug-text stack ---
// Simple top-left auto-stacking text. Reset the cursor each frame with
// HudDebugReset(); then HudDebugText(...) appends lines downward.
// State is per-TU (each game is one .c, so each game has its own cursor).
static int _hud_dbg_y = 10;

static inline void HudDebugReset(void) { _hud_dbg_y = 10; }

static inline void HudDebugText(const char *text) {
    DrawText(text, 10, _hud_dbg_y, 20, WHITE);
    _hud_dbg_y += 22;
}

#endif // UTIL_HUD_H
```

- [ ] **Step 2: Build and run smoke test**

```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`.

- [ ] **Step 3: Build all games**

```bash
zig build
```

Expected: all 22 projects build.

- [ ] **Step 4: Commit**

```bash
git add common/util/hud.h
git commit -m "$(cat <<'EOF'
util/hud.h: HealthBar, Crosshair, DamageVignette, Letterbox, DebugText

Widget API for the HUD elements that every prototype draws manually.
Draw-only; no unit tests. Validated at compile time via util-tests.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: debug.h — F3 overlay + wireframe AABB/Sphere

**Files:**
- Modify: `common/util/debug.h`

- [ ] **Step 1: Write `debug.h`**

Overwrite `common/util/debug.h`:

```c
// debug.h — opt-in runtime debug overlay and wireframe collision shapes.
#ifndef UTIL_DEBUG_H
#define UTIL_DEBUG_H

#include "raylib.h"
#include "collide.h"

typedef struct {
    bool   visible;
    float  accumDt;
    int    frameCount;
    float  shownFps;
} DebugOverlay;

static inline void DebugOverlayUpdate(DebugOverlay *d, float dt) {
    d->accumDt    += dt;
    d->frameCount += 1;
    if (d->accumDt >= 0.5f) {
        d->shownFps   = (float)d->frameCount / d->accumDt;
        d->accumDt    = 0.0f;
        d->frameCount = 0;
    }
}

// Toggle visibility on a key press. Call once per frame.
static inline void DebugOverlayToggleOnKey(DebugOverlay *d, int key) {
    if (IsKeyPressed(key)) d->visible = !d->visible;
}

// Draw a small bottom-left panel: FPS, player position, and an optional extra line.
static inline void DebugOverlayDraw(const DebugOverlay *d,
                                    const char *gameStateLine,
                                    Vector3 playerPos) {
    if (!d->visible) return;
    int y = GetScreenHeight() - 80;
    DrawRectangle(5, y - 5, 300, 75, (Color){0, 0, 0, 180});
    DrawText(TextFormat("FPS: %.0f", (double)d->shownFps), 10, y,       18, GREEN);
    DrawText(TextFormat("Pos: %.2f, %.2f, %.2f",
                        (double)playerPos.x, (double)playerPos.y, (double)playerPos.z),
             10, y + 20, 18, WHITE);
    if (gameStateLine) DrawText(gameStateLine, 10, y + 40, 18, YELLOW);
}

static inline void DebugDrawAABB(AABB a, Color col) {
    DrawCubeWires(a.center, a.half.x * 2.0f, a.half.y * 2.0f, a.half.z * 2.0f, col);
}

static inline void DebugDrawSphere(Sphere s, Color col) {
    DrawSphereWires(s.center, s.radius, 8, 8, col);
}

#endif // UTIL_DEBUG_H
```

- [ ] **Step 2: Build and run smoke test**

```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED`.

- [ ] **Step 3: Build all games**

```bash
zig build
```

Expected: all 22 projects build.

- [ ] **Step 4: Commit**

```bash
git add common/util/debug.h
git commit -m "$(cat <<'EOF'
util/debug.h: F3 overlay, DebugDrawAABB/Sphere

Small opt-in overlay showing FPS, player position, and a user-supplied
status line. Wireframe AABB / Sphere helpers for collision debugging.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: fps migration — port `fps/main.c` to use the new library

Reference migration. End result: `fps` plays identically to the pre-migration binary, but with fewer lines of duplicated code and the new library exercised end-to-end.

This task is bigger than the others. The smaller sub-steps break it into behaviour-preserving chunks, each of which leaves the binary runnable.

**Files:**
- Modify: `fps/main.c`

- [ ] **Step 1: Add the new includes**

At the top of `fps/main.c`, after the existing `#include "../common/objects3d.h"` line, add:

```c
#include "../common/util/math.h"
#include "../common/util/input.h"
#include "../common/util/pool.h"
#include "../common/util/camera.h"
#include "../common/util/collide.h"
#include "../common/util/hud.h"
```

(We skip `fx.h` explicitly — it's already pulled in via `objects3d.h`. Skip `debug.h` — not used in this migration.)

Build:
```bash
zig build fps
```

Expected: builds with no warnings. No behaviour change yet.

- [ ] **Step 2: Convert `Player.yaw` / `Player.pitch` from degrees to radians**

The existing code stores yaw/pitch in degrees and multiplies by `DEG2RAD` at every use. Flipping to radians removes the multiplications.

Mechanical changes in `fps/main.c`:

1. Update the mouse-look block (currently lines ~414-419) from:
   ```c
   Vector2 md = GetMouseDelta();
   player.yaw -= md.x * 0.1f;
   player.pitch += md.y * 0.1f;
   if (player.pitch > 89) player.pitch = 89;
   if (player.pitch < -89) player.pitch = -89;
   ```
   to a single call:
   ```c
   InputLookMouse(&player.yaw, &player.pitch, 0.002f, 1.55f);
   ```
   The sensitivity `0.002f` radians-per-pixel roughly matches the prior feel (old `0.1f` degrees-per-pixel × DEG2RAD ≈ 0.00175; `0.002f` is close and easy to tune).

   Note the sign flip on yaw: the new helper does `*yaw += md.x * sens`, whereas the old code did `-=`. To preserve look direction, either:
   - negate the sensitivity (`-0.002f`), or
   - leave it positive and let the `CamFPS` math in Task 4 handle it — our `CamFPSToCamera3D` uses `fwd = (sy*cp, sp, -cy*cp)`, i.e. yaw-right = +X turn, same visual effect.

   Start with positive `0.002f` and playtest. If mouse-X feels inverted, pass `-0.002f`.

2. Delete the manual yaw/pitch-to-vector conversion (currently lines ~421-424):
   ```c
   float yr = player.yaw * DEG2RAD, pr = player.pitch * DEG2RAD;
   Vector3 forward = Vector3Normalize((Vector3){sinf(yr), -tanf(pr), cosf(yr)});
   Vector3 flatFwd = Vector3Normalize((Vector3){sinf(yr), 0, cosf(yr)});
   Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3){0,1,0}));
   ```

   It gets replaced in the next sub-step.

3. In `FireWeapon` (line ~364), the call site does `FireWeapon(&player, forward, right)`. Look at every place `player.yaw`/`player.pitch` is still used in degrees (notably line ~606 inside the HUD / minimap block) and change:
   ```c
   float yr = player.yaw * DEG2RAD, pr = player.pitch * DEG2RAD;
   ```
   to:
   ```c
   float yr = player.yaw, pr = player.pitch;
   ```
   or remove the temporary and inline `player.yaw` / `player.pitch` directly.

Build and briefly run:
```bash
zig build run-fps
```

Expected: the game runs. Mouse-look works (flip the sensitivity sign if X feels inverted). Pitch clamp still holds (no flipping upside-down when looking up).

- [ ] **Step 3: Replace movement and camera construction with `InputMoveDir3Flat` and `CamFPS`**

Replace the whole "Movement" block (currently lines ~426-432):

```c
float speed = 10.0f * dt;
Vector3 move = {0};
if (IsKeyDown(KEY_W)) move = Vector3Add(move, Vector3Scale(flatFwd, speed));
if (IsKeyDown(KEY_S)) move = Vector3Add(move, Vector3Scale(flatFwd, -speed));
if (IsKeyDown(KEY_A)) move = Vector3Add(move, Vector3Scale(right, -speed));
if (IsKeyDown(KEY_D)) move = Vector3Add(move, Vector3Scale(right, speed));
```

with:

```c
float speed = 10.0f * dt;
Vector3 move = Vector3Scale(InputMoveDir3Flat(player.yaw), speed);
```

Replace the camera struct setup block in `main` (currently lines ~400-404 plus wherever the frame's camera position/target is written):

```c
Camera3D camera = {0};
camera.fovy = 75.0f;
camera.projection = CAMERA_PERSPECTIVE;
camera.up = (Vector3){0, 1, 0};
```

with the `CamFPS` rig. Pick one of:
- Keep `camera` as a raylib `Camera3D` written each frame from a `CamFPS` struct. That's the least invasive:
  ```c
  CamFPS camRig = CamFPSInit((Vector3){0, 1.5f, 0});
  ```
  Each frame, before `BeginMode3D`:
  ```c
  camRig.pos = (Vector3){ player.pos.x, player.pos.y + 1.5f, player.pos.z };
  camRig.yaw = player.yaw;
  camRig.pitch = player.pitch;
  Camera3D camera = CamFPSToCamera3D(&camRig);
  ```
  The screen-shake offset (currently folded into `camera.position` and `camera.target` via `shakeOff`) still applies: take the `CamFPSToCamera3D` result, then add `shakeOff.x`/`shakeOff.y` to both fields exactly as the old code did.

For `forward` / `flatFwd` / `right` — which were used by `FireWeapon(&player, forward, right)` — compute them from the rig for just that call site:
```c
Camera3D cam = CamFPSToCamera3D(&camRig);
Vector3 forward  = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
Vector3 flatFwd  = Vector3Normalize((Vector3){ forward.x, 0.0f, forward.z });
Vector3 right    = Vector3Normalize(Vector3CrossProduct(forward, (Vector3){0,1,0}));
```

Or extract once per frame into local variables.

Build and run:
```bash
zig build run-fps
```

Expected: movement works — W goes forward (in the direction you're facing), A strafes left, etc. Firing still hits where you're aiming. Damage and kills still register.

- [ ] **Step 4: Replace `WallCollision` + per-axis slide with `SlideXZ`**

Keep the existing `WallCollision` and `PropCollision` functions (they are project-specific and stay in `fps/main.c`). Wrap them in a callback compatible with `SlideXZ`:

Add above `main`:
```c
static bool FpsBlocked(Vector3 pos, float radius, void *user) {
    (void)user;
    return WallCollision(pos, radius) || PropCollision(pos, radius);
}
```

Replace the per-axis slide block (currently lines ~435-438):
```c
Vector3 newPos = Vector3Add(player.pos, (Vector3){move.x, 0, 0});
if (!WallCollision(newPos, 0.4f) && !PropCollision(newPos, 0.4f)) player.pos.x = newPos.x;
newPos = Vector3Add(player.pos, (Vector3){0, 0, move.z});
if (!WallCollision(newPos, 0.4f) && !PropCollision(newPos, 0.4f)) player.pos.z = newPos.z;
```

with:
```c
SlideXZ(&player.pos, move, 0.4f, FpsBlocked, NULL);
```

Build and run:
```bash
zig build run-fps
```

Expected: player slides along walls / props exactly as before.

- [ ] **Step 5: Replace gravity + floor with `GroundSnap`**

Find the block that integrates `player.velY`, applies it to `player.pos.y`, and clamps at `y <= 0` setting `grounded = true`. (In the current file this is scattered around lines ~445-465 alongside the jump handling.) Replace the gravity + floor-clamp part with a single call:

```c
GroundSnap(&player.pos, &player.velY, dt, 25.0f, 0.0f, &player.grounded);
```

(Use whichever gravity constant the file currently uses — inspect the existing integration to find it. Commonly 20–30 f/s². Keep the existing jump-impulse code unchanged: jump still sets `player.velY = 8.0f; player.grounded = false;`.)

Build and run:
```bash
zig build run-fps
```

Expected: jump height and fall time match the prior behaviour.

- [ ] **Step 6: Replace pool loops with `POOL_FOREACH` / `POOL_SPAWN`**

Across `fps/main.c`, find every:
```c
for (int i = 0; i < MAX_X; i++) {
    if (!X[i].active) continue;
    ...
}
```
and replace with:
```c
POOL_FOREACH(X, MAX_X, x) {
    if (!x->active) continue;
    ...
    // use x->field instead of X[i].field
}
```

Pools in this file are: `bullets`, `enemies`, `particles`, `pickups`, `props` (and `walls` — though walls are never deactivated). Convert the first three, leave `walls` alone, leave `pickups` / `props` at your discretion (converting them is pure cleanup). Keep `for (int i ...)` where the body also references `i` as an index (e.g. for decrementing `enemiesAlive` or swapping with another array).

Spawn call-sites: replace:
```c
for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) continue;
    bullets[i] = (Bullet){ ... };
    break;
}
```
with:
```c
int i = POOL_SPAWN(bullets, MAX_BULLETS);
if (i >= 0) bullets[i] = (Bullet){ ..., .active = true };
```

Note the `.active = true` in the initializer: `POOL_SPAWN` sets `.active` to true on the returned slot, but if you then assign a whole-struct literal to that slot, the literal's `.active` field overrides. Include `.active = true` in the literal to make the intent explicit.

Build and run:
```bash
zig build run-fps
```

Expected: same enemy / bullet behaviour.

- [ ] **Step 7: Replace HUD widgets**

Find the HUD draw block (inside `BeginDrawing` / `EndDrawing`, after `EndMode3D`). Replace:

- The health bar (something like `DrawRectangle(20, sh-50, 200, 20, ...)` + fill + outline) with:
  ```c
  HudHealthBar(20, sh - 50, 200, 20, player.hp / player.maxHp,
               (Color){220, 50, 50, 255}, (Color){40, 40, 40, 255}, BLACK);
  ```
  Adjust colors to match the prior palette.

- The crosshair (four `DrawLineEx` calls at screen center) with:
  ```c
  HudCrosshair(sw / 2, sh / 2, 12, 2, 0, WHITE);
  ```

- The damage-flash fullscreen overlay with:
  ```c
  HudDamageVignette(player.damageFlash, (Color){200, 30, 30, 255});
  ```
  (The prior code multiplies `damageFlash * 400` to construct the alpha; tune the `player.damageFlash` decay rate if needed so its usable range becomes 0..1 for `HudDamageVignette`. Simplest: don't retune, just keep the old line and call `HudDamageVignette` with the matching product.)

Leave `DrawText` score/wave/ammo lines alone for now — those belong to `hud.h`'s future debug-text stack, but reworking them isn't required for the migration.

Build and run:
```bash
zig build run-fps
```

Expected: health bar looks the same, crosshair present, damage flash still visible on hit.

- [ ] **Step 8: Full playtest**

```bash
zig build run-fps
```

Run through a wave cycle:
- Move around (W/A/S/D + mouse)
- Fire each weapon (1/2/3 to switch if the game supports it — check `fps/main.c`)
- Take damage (let an enemy close in)
- Reload (press R if supported)
- Kill several enemies, clear at least wave 1
- Die, restart with Enter

Compare subjectively to git history if needed (`git stash && zig build run-fps` on the pre-migration state). Any behaviour regression is a task failure — fix before committing.

- [ ] **Step 9: LOC sanity check**

```bash
wc -l fps/main.c
```

Before migration: ~884 lines. Target: ≤780 lines (≥100-line reduction per the spec's success criterion). If you're above 800, there's still a pattern that could be collapsed — but the binary behaving correctly is the real success criterion. LOC is a sanity check, not a hard gate.

- [ ] **Step 10: Commit**

```bash
git add fps/main.c
git commit -m "$(cat <<'EOF'
fps: migrate to common/util/ (reference migration)

Ports fps/main.c to the new utility headers: input.h for mouse-look and
WASD, camera.h for the FPS rig, collide.h for SlideXZ + GroundSnap, pool.h
for entity iteration, hud.h for HealthBar / Crosshair / DamageVignette.
Yaw/pitch storage flips from degrees to radians to match camera.h's
convention. Playtested: wave progression, weapon feel, damage, respawn.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 11: Final verification**

```bash
zig build
./zig-out/bin/util-tests
```

Expected: all 22 projects build clean; `util-tests` passes.

---

## Self-Review Checklist

Run through this once after the plan is written, before offering execution choices.

**Spec coverage:**
- [x] `math.h` with `WrapAngle*`, `SignF`, `RandF`, `RandOnUnitSphere`, `ApproachF`, `SmoothStep`, `EaseOutCubic` — Task 1.
- [x] `input.h` with `InputMoveDir2`, `InputMoveDir3Flat`, `InputLookMouse`, `InputLookStick`, cursor lock, action helpers — Task 7.
- [x] `pool.h` with `POOL_SPAWN`, `POOL_FOREACH` (renamed from spec's `FOR_ACTIVE` for semantic reasons), `POOL_RING_PUSH`, `POOL_COUNT_ACTIVE` — Task 2. Deviation from spec: `POOL_FOREACH` iterates all slots and requires caller to filter on `.active`; explained in Task 2 comments.
- [x] `camera.h` with `CamFPS`, `CamThirdPerson`, `CamChase`, `CamTopDown` plus Init/Update/ToCamera3D — Task 4.
- [x] `collide.h` with `AABB`, `Sphere`, `Capsule`, overlap helpers, `GroundSnap`, `SlideXZ` — Task 3. Deviation from spec: `SlideXZ` takes a callback, not an AABB array, because fps has two obstacle types (walls + props) that can't share an array without extra copying. Rationale is in Task 3.
- [x] `fx.h` with 2D + 3D particle pools and `ScreenShake` — Task 5.
- [x] `objects3d.h` relocation: delete particles/shake, add `#include "util/fx.h"` — Task 6.
- [x] `hud.h` with `HudHealthBar`, `HudCrosshair`, `HudDamageVignette`, `HudLetterbox`, `HudDebugText`/`HudDebugReset` — Task 8.
- [x] `debug.h` with `DebugOverlay`, `DebugDrawAABB`, `DebugDrawSphere` — Task 9.
- [x] `fps` reference migration — Task 10.
- [x] Phase 1 exit criterion ("`zig build` succeeds for all 21 prototypes") — enforced in Tasks 0, 6, 7, 8, 9, 10 Step 11.
- [x] Phase 2 exit criterion ("`zig build run-fps` launches and feels identical") — Task 10 Step 8.

**Placeholder scan:** No TBD / TODO / implement-later markers. Every code step includes full code. Every command step has a run command and an expected result.

**Type / name consistency:**
- `CamFPS.yaw` / `.pitch` in radians, consistent across camera.h, input.h, and the fps migration.
- `ScreenShake` struct fields `amount` / `timer` / `decay` match both fx.h (Task 5) and the existing objects3d.h signature that's being preserved (Task 6).
- `SpawnParticleBurst` 3D signature `(Particle3D*, int, Vector3, int, float, float, float, float, float, float)` in fx.h matches every existing call site in fps / 3rd-person / rts (verified against `common/objects3d.h:163` during planning).
- `Particle2D` does have a `Color` parameter in `SpawnParticleBurst2D`; `Particle3D` / `SpawnParticleBurst` does not. Deliberate asymmetry, documented in Task 5.
- `POOL_FOREACH` vs `FOR_ACTIVE` (spec used the latter name): renamed across Task 2 test, Task 2 implementation, Task 10 migration. No stragglers.

**Open design-time decisions resolved in the plan:**
- Radians vs degrees → radians (spec + Task 10 Step 2).
- Single-target Phase 2 → fps only.
- `SlideXZ` callback vs array → callback.
- `POOL_FOREACH` semantics → iterate all, caller filters (rationale: simpler macro, natural `break`/`continue`).
- `HudDebugTextf` variadic wrapper → skipped; only `HudDebugText(const char *)` provided. Callers can use `TextFormat(...)` inline.

Fixed during self-review: none — the plan is complete as written.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-16-common-util-library.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration with per-task checkpoints. Best for a plan this size because each task is independently reviewable and a mistake in one doesn't contaminate the next.

**2. Inline Execution** — Execute tasks in this session using `superpowers:executing-plans`, batch execution with checkpoints for review.

Which approach?
