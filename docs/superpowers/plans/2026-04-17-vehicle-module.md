# Vehicle Physics Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a header-only `common/util/vehicle.h` that consolidates the near-identical horizontal-vehicle-physics pattern used by the racing-style prototypes. Validate with a reference migration of `rally/main.c`.

**Architecture:** One small `static inline` header with a `Vehicle` state struct, a `VehicleInput` input struct, and a `VehicleUpdate` function. The module touches only `pos.x` / `pos.z` / `rotation` / `speed`; vertical physics and drift / boost / surface behaviours are entirely caller-driven (direct per-frame mutation of the `Vehicle` config fields). Smoke-tested in the existing `util-tests` harness. Validated by porting `rally/main.c`'s physics block to use it while keeping rally-specific subsystems (bicycle steering, drift, mini-turbo, surface types, waypoints, AI) untouched.

**Tech Stack:** C99, raylib + raymath, Zig build system (`build.zig`). No new external dependencies. Builds on the existing `common/util/` library (commits `83be8e3` through `31b2c3b`).

**Spec reference:** `docs/superpowers/specs/2026-04-17-vehicle-module-design.md`.

---

## File Structure

**Created:**
- `common/util/vehicle.h` — `Vehicle`, `VehicleInput`, `VehicleUpdate`.

**Modified:**
- `util-tests/main.c` — add `#include "../common/util/vehicle.h"`, add `test_vehicle` function, call it from `main`.
- `rally/main.c` — replace the core physics block with a `VehicleUpdate` call (reference migration).

**Unchanged:** every other prototype, every existing `common/util/*.h`, `build.zig`.

---

## Conventions used in this plan

- Paths are absolute from the repo root. Commands assume working dir = repo root (`/Users/wjbr/src/raylib-research/`).
- Commits go directly to `master` — user's standing preference for this repo.
- Code blocks in steps are the exact text to write. Do not paraphrase.
- Smoke test is the existing `util-tests` executable. `ALL PASSED (0 failures)` is the gate.

---

## Task 1: Bootstrap — stub vehicle.h and wire into test harness

**Files:**
- Create: `common/util/vehicle.h` (empty guard only)
- Modify: `util-tests/main.c` (add include, stub `test_vehicle`, call in `main`)

- [ ] **Step 1: Create the empty header stub**

Write `common/util/vehicle.h` with only the include guard:

```c
#ifndef UTIL_VEHICLE_H
#define UTIL_VEHICLE_H
#endif
```

- [ ] **Step 2: Add include + stub + call to util-tests/main.c**

Make three edits to `util-tests/main.c`:

**Edit A — add the include.** After the existing `#include "../common/util/debug.h"` line (currently the last util include), add:
```c
#include "../common/util/vehicle.h"
```

**Edit B — add the stub function.** After the line `static void test_fx(void) { /* filled in Task 5 */ }` (or wherever the per-module stubs are declared; they are grouped together), add one more stub:
```c
static void test_vehicle(void){/* filled in Task 2 */ }
```

Note: the existing stubs use the comment `/* filled in Task N */` — do the same for consistency. (The "Task 2" here refers to Task 2 of *this* plan, not the common-util-library plan. That's OK — the comment is a historical breadcrumb for the implementer at that moment, not a permanent reference.)

**Edit C — call test_vehicle from main.** Inside `int main(void)`, after `test_fx();` and before the trailing printf, add:
```c
    test_vehicle();
```

The main function's call list should now read:
```c
    test_math();
    test_pool();
    test_collide();
    test_camera();
    test_fx();
    test_vehicle();
    // input/hud/debug: include-only, no standalone tests (window-dependent)
```

- [ ] **Step 3: Verify everything still builds and passes**

Run:
```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected:
```
ALL PASSED (0 failures)
```
Exit code 0.

Then:
```bash
zig build
```

Expected: all 22 projects build clean (no output).

- [ ] **Step 4: Commit**

```bash
git add common/util/vehicle.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/vehicle.h: scaffold header + test harness stub

Empty guard file and a test_vehicle() stub wired into util-tests/main.c.
Next commit fills the header with VehicleUpdate and the test body.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Implement vehicle.h with TDD

**Files:**
- Modify: `common/util/vehicle.h` (fill in the full implementation)
- Modify: `util-tests/main.c` (fill in `test_vehicle` body)

- [ ] **Step 1: Write the failing test body**

In `util-tests/main.c`, replace the `test_vehicle` stub with this body. Keep the surrounding `static void test_vehicle(void) { ... }` signature:

```c
static void test_vehicle(void) {
    // A standard vehicle config used by most subtests.
    Vehicle v = {
        .pos = {0, 0, 0}, .rotation = 0.0f, .speed = 0.0f,
        .accel = 35.0f, .brake = 40.0f,
        .maxSpeed = 45.0f, .reverseMax = 15.0f,
        .turnRate = 2.8f, .drag = 0.99f,
    };
    VehicleInput fwd      = { .throttle =  1.0f, .steer = 0.0f };
    VehicleInput rev      = { .throttle = -1.0f, .steer = 0.0f };
    VehicleInput coast    = { .throttle =  0.0f, .steer = 0.0f };
    VehicleInput rightOn  = { .throttle =  0.0f, .steer = 1.0f };

    // Accelerates from rest, but has not hit maxSpeed after 1 simulated second.
    for (int i = 0; i < 60; i++) VehicleUpdate(&v, fwd, 1.0f / 60.0f);
    CHECK(v.speed > 0.0f,          "Vehicle accelerates from rest");
    CHECK(v.speed < v.maxSpeed,    "Vehicle below maxSpeed after 1s of throttle");

    // After another 5 simulated seconds, speed approaches maxSpeed.
    for (int i = 0; i < 300; i++) VehicleUpdate(&v, fwd, 1.0f / 60.0f);
    CHECK(v.speed > v.maxSpeed * 0.95f, "Vehicle approaches maxSpeed after 6s");
    CHECK(v.speed <= v.maxSpeed + 1e-3f, "Vehicle respects maxSpeed clamp");

    // Drag alone decays speed.
    v.speed = v.maxSpeed;
    for (int i = 0; i < 180; i++) VehicleUpdate(&v, coast, 1.0f / 60.0f);
    CHECK(v.speed < v.maxSpeed * 0.6f, "Drag decays speed over 3s");

    // Negative throttle reverses and clamps at reverseMax.
    v.speed = 0.0f;
    for (int i = 0; i < 300; i++) VehicleUpdate(&v, rev, 1.0f / 60.0f);
    CHECK(v.speed < 0.0f,                "Negative throttle reverses from rest");
    CHECK(v.speed >= -v.reverseMax - 1e-3f, "Reverse respects reverseMax clamp");

    // Forward motion at rotation = 0 moves +Z, not X.
    Vehicle w = {
        .pos = {0, 7.5f, 0}, .rotation = 0.0f, .speed = 10.0f,
        .accel = 35.0f, .brake = 40.0f,
        .maxSpeed = 45.0f, .reverseMax = 15.0f,
        .turnRate = 2.8f, .drag = 1.0f,  // disable drag for clean pos test
    };
    VehicleUpdate(&w, coast, 1.0f / 60.0f);
    CHECK(w.pos.z > 0.0f,            "rotation=0 advances +Z");
    CHECK(NEAR(w.pos.x, 0.0f, 1e-5f), "rotation=0 leaves X unchanged");
    CHECK(NEAR(w.pos.y, 7.5f, 1e-5f), "VehicleUpdate never touches pos.y");

    // Forward motion at rotation = π/2 moves +X, not Z.
    w.pos = (Vector3){0, 0, 0};
    w.rotation = PI / 2.0f;
    VehicleUpdate(&w, coast, 1.0f / 60.0f);
    CHECK(w.pos.x > 0.0f,             "rotation=π/2 advances +X");
    CHECK(NEAR(w.pos.z, 0.0f, 1e-4f), "rotation=π/2 leaves Z unchanged");

    // Steering at zero speed still has effect (turnFactor floor 0.3).
    Vehicle u = {
        .pos = {0, 0, 0}, .rotation = 0.0f, .speed = 0.0f,
        .accel = 35.0f, .brake = 40.0f,
        .maxSpeed = 45.0f, .reverseMax = 15.0f,
        .turnRate = 2.0f, .drag = 0.99f,
    };
    VehicleUpdate(&u, rightOn, 1.0f / 60.0f);
    float rotAtZero = u.rotation;
    CHECK(rotAtZero > 0.0f, "Steering rotates even at zero speed (turnFactor floor)");

    // Steering at maxSpeed rotates more than at zero (turnFactor ramps up).
    u.rotation = 0.0f;
    u.speed = u.maxSpeed;
    VehicleUpdate(&u, rightOn, 1.0f / 60.0f);
    CHECK(u.rotation > rotAtZero * 2.0f, "Steering at maxSpeed rotates more than at zero");

    // Y is still untouched after a batch of updates.
    u.pos = (Vector3){0, 3.14f, 0};
    VehicleInput full = { .throttle = 1.0f, .steer = 0.5f };
    for (int i = 0; i < 60; i++) VehicleUpdate(&u, full, 1.0f / 60.0f);
    CHECK(NEAR(u.pos.y, 3.14f, 1e-5f), "pos.y still untouched after mixed input");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
zig build util-tests 2>&1 | head -20
```

Expected: compile failure. `Vehicle`, `VehicleInput`, `VehicleUpdate` are all undeclared (the header is an empty guard).

- [ ] **Step 3: Write `vehicle.h`**

Overwrite `common/util/vehicle.h`:

```c
// vehicle.h — horizontal-physics core for racing-style prototypes.
// Caller owns vertical physics (GroundSnap, wave snap, hover, etc.) and
// expresses drift / boost / surface effects by mutating Vehicle config
// fields (turnRate, drag, maxSpeed) per-frame before calling VehicleUpdate.
//
// Yaw convention: 0 = +Z forward (matches CamChase.targetYaw).
#ifndef UTIL_VEHICLE_H
#define UTIL_VEHICLE_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>

typedef struct {
    // State — VehicleUpdate mutates these.
    Vector3 pos;        // module touches x and z only; y is caller's
    float rotation;     // yaw in radians; 0 = +Z forward
    float speed;        // signed; negative = reversing

    // Config — caller owns; mutate per-frame for surface / drift / boost.
    float accel;        // forward acceleration (units/sec²) when throttle > 0
    float brake;        // braking / reverse acceleration (units/sec²) when throttle < 0
    float maxSpeed;     // hard clamp on forward speed
    float reverseMax;   // hard clamp on reverse speed (set to 0 to disable reverse)
    float turnRate;     // radians/sec at full steer × full turnFactor
    float drag;         // per-60fps-frame multiplier (typical 0.98–0.993)
} Vehicle;

typedef struct {
    float throttle;     // −1..1  (positive = throttle, negative = brake/reverse)
    float steer;        // −1..1  (positive = right)
} VehicleInput;

static inline void VehicleUpdate(Vehicle *v, VehicleInput in, float dt) {
    // 1. Integrate throttle/brake.
    if (in.throttle > 0.0f)      v->speed += v->accel * in.throttle * dt;
    else if (in.throttle < 0.0f) v->speed += v->brake * in.throttle * dt;

    // 2. Apply drag, frame-rate independent. Equivalent to `speed *= drag` at 60fps.
    v->speed *= powf(v->drag, dt * 60.0f);

    // 3. Clamp to configured range.
    v->speed = Clamp(v->speed, -v->reverseMax, v->maxSpeed);

    // 4. Steer with speed-dependent turn factor.
    //    At |speed| >= 0.3 * maxSpeed the full turnRate applies; below that it
    //    ramps down to a 0.3× floor so parked cars can still rotate slightly.
    float turnFactor = Clamp(fabsf(v->speed) / (v->maxSpeed * 0.3f), 0.3f, 1.0f);
    v->rotation += in.steer * v->turnRate * turnFactor * dt;

    // 5. Move along XZ (yaw=0 → +Z forward). pos.y is never touched.
    v->pos.x += sinf(v->rotation) * v->speed * dt;
    v->pos.z += cosf(v->rotation) * v->speed * dt;
}

#endif // UTIL_VEHICLE_H
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
zig build util-tests && ./zig-out/bin/util-tests
```

Expected: `ALL PASSED (0 failures)`. Exit code 0.

- [ ] **Step 5: Verify all 22 projects still build**

```bash
zig build
```

Expected: clean (no output).

- [ ] **Step 6: Commit**

```bash
git add common/util/vehicle.h util-tests/main.c
git commit -m "$(cat <<'EOF'
util/vehicle.h: horizontal-physics core for racing prototypes

One Vehicle struct + one VehicleUpdate function. No drift/boost/surface
baked in — games express those by mutating turnRate/drag/maxSpeed per
frame. Yaw convention 0=+Z matches CamChase. pos.y is never touched so
games keep their own vertical scheme (GroundSnap, waves, hover, slope).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Rally reference migration

Port `rally/main.c`'s core physics block to use `VehicleUpdate`, while keeping every rally-specific subsystem (bicycle steering, drift, mini-turbo, surface types, waypoints, AI behaviour, track rendering) in place. Success = `zig build run-rally` plays indistinguishably from the pre-migration binary in a short scripted playthrough.

**Strategy:** non-invasive. Do NOT change the `Car` struct. Do NOT change rally's waypoint / lap / AI / collision code. Per frame, build a temporary `Vehicle` struct from the Car's state + computed config, call `VehicleUpdate`, copy the updated state back to the Car. This lets the migration touch only the physics lines.

**Files:**
- Modify: `rally/main.c` (physics block only)

- [ ] **Step 1: Add the include**

At the top of `rally/main.c`, after the existing `#include "../common/objects3d.h"` line, add:
```c
#include "../common/util/vehicle.h"
```

Build:
```bash
zig build rally
```

Expected: clean. No behaviour change yet.

- [ ] **Step 2: Locate the existing physics block**

Read `rally/main.c` and find the per-frame physics update for the player car. The block is approximately at lines 441–470 and has this shape (your file may have drifted slightly; use the shape, not the line numbers):

```c
// Accel / brake
if (IsKeyDown(KEY_W)) car->speed += CAR_ACCEL * dt;
if (IsKeyDown(KEY_S)) car->speed -= CAR_BRAKE * dt;

// Drag per surface
float drag = ...  // CAR_DRAG_ROAD / _GRAVEL / _MUD selected from surface lookup
if (car->drifting) drag *= CAR_DRIFT_DRAG / CAR_DRAG_ROAD;  // or similar
car->speed *= drag;

// Clamp
float maxSpeed = CAR_MAX_SPEED;
if (car->boostTimer > 0) maxSpeed *= 1.3f;
if (car->speed > maxSpeed) car->speed = maxSpeed;

// Steering — bicycle model
float steerAngle = car->steerInput * CAR_TURN_MAX;
if (car->drifting) steerAngle *= CAR_DRIFT_MULT;
float angularVel = car->speed * tanf(steerAngle) / WHEELBASE;
car->rotation += angularVel * dt;

// Move
car->pos.x += sinf(car->rotation) * car->speed * dt;
car->pos.z += cosf(car->rotation) * car->speed * dt;
```

Confirm the block's actual location before editing. The AI cars may have a similar loop — we will migrate both in this task.

No commit for this step; it's read-only.

- [ ] **Step 3: Replace the player-car physics block**

Replace the physics block identified in Step 2 with:

```c
// --- Physics (via util/vehicle.h). rally keeps its bicycle steering: we
// compute angularVel rally-style, apply it to car->rotation, then use
// VehicleUpdate for accel/drag/clamp/position. steer=0 in the input
// disables VehicleUpdate's own turn factor so rally's bicycle model wins.

// Surface + drift + boost express themselves via config mutation.
float surfaceDrag = CAR_DRAG_ROAD;  // replace with the actual surface lookup
if (currentSurface == SURF_GRAVEL) surfaceDrag = CAR_DRAG_GRAVEL;
if (currentSurface == SURF_MUD)    surfaceDrag = CAR_DRAG_MUD;
if (car->drifting)                 surfaceDrag = CAR_DRIFT_DRAG;

float effMax = CAR_MAX_SPEED;
if (car->boostTimer > 0) effMax *= 1.3f;

// Read throttle input (keep rally's existing semantics).
float throttle = 0.0f;
if (IsKeyDown(KEY_W)) throttle =  1.0f;
if (IsKeyDown(KEY_S)) throttle = -1.0f;  // rally uses S as brake/reverse

// Rally bicycle steering: compute rotation change first, apply to car->rotation.
float steerAngle = car->steerInput * CAR_TURN_MAX;
if (car->drifting) steerAngle *= CAR_DRIFT_MULT;
float angularVel = car->speed * tanf(steerAngle) / WHEELBASE;
car->rotation += angularVel * dt;

// Populate the Vehicle from the Car and run the update.
Vehicle vh = {
    .pos        = car->pos,
    .rotation   = car->rotation,
    .speed      = car->speed,
    .accel      = CAR_ACCEL,
    .brake      = CAR_BRAKE,
    .maxSpeed   = effMax,
    .reverseMax = CAR_MAX_SPEED * 0.3f,
    .turnRate   = 0.0f,               // unused: rally steers via bicycle above
    .drag       = surfaceDrag,
};
VehicleInput vin = { .throttle = throttle, .steer = 0.0f };
VehicleUpdate(&vh, vin, dt);
car->pos   = vh.pos;
car->speed = vh.speed;
// car->rotation was already written by the bicycle block above.
```

Replace the constants / lookups with the actual names used in `rally/main.c`:
- `currentSurface` may actually be `car->currentSurface` or looked up per-position. Inspect the existing code in Step 2 and substitute the real expression.
- `CAR_TURN_MAX` and `WHEELBASE` are whatever names rally already uses — don't rename them.
- If rally's drag formula uses a different combinator (e.g. surface drag multiplied by drift factor rather than replaced), mirror that exactly. The rule is: `surfaceDrag` is the same number the existing code was about to multiply `car->speed` by.

The key invariants:
- The bicycle rotation update happens BEFORE `VehicleUpdate`, so `VehicleUpdate` sees rally's updated rotation and moves the car in the right direction.
- `vin.steer = 0.0f` means `VehicleUpdate` does not add its own rotation on top of the bicycle model.
- `car->rotation` is not copied back from the `Vehicle`, because the bicycle block has already written it. Copying `vh.rotation` back would be a no-op here (we never added steer), but we make this explicit to document that rotation is rally-owned.

- [ ] **Step 4: Build and check for warnings**

```bash
zig build rally
```

Expected: clean build. If constants are mis-named (`CAR_TURN_MAX` vs `CAR_TURN`, etc.) fix the substitutions before proceeding.

- [ ] **Step 5: Playtest player car**

```bash
zig build run-rally
```

Drive. Compare subjectively to the pre-migration feel. Check:
- Throttle + brake feel the same.
- Drift-hold + turn produces the same slide behaviour.
- Surface changes (if the track has gravel/mud sections) still slow the car.
- Mini-turbo boost on drift release (if rally has it) still applies.
- Top speed is the same at full throttle on tarmac.
- Car moves in the direction it's facing.

If anything feels meaningfully different, inspect the config-field computation (Step 3) — the most likely cause is a mis-substituted drag term or a missing boost multiplier. Do not commit until the feel matches.

No commit yet.

- [ ] **Step 6: Apply the same pattern to AI cars**

Find the AI car update loop — likely in a function named something like `UpdateAICar` or inline in the same big physics loop. It looks roughly like the player block but reads `car->aiSteer` and `car->aiThrottle` (or similar) instead of keyboard input.

Apply the same transformation: compute bicycle `angularVel`, apply to `car->rotation`, then populate a `Vehicle` and call `VehicleUpdate` with `steer = 0.0f`. The throttle input comes from the AI's existing decision code, not from `IsKeyDown`.

If AI cars share a single update function with the player (parameterized by `isPlayer`), do both in one edit. If they're separate loops, edit both.

Build:
```bash
zig build rally
```

Expected: clean.

- [ ] **Step 7: Playtest AI cars**

```bash
zig build run-rally
```

Watch the AI cars for a full lap. Check:
- They still follow the track.
- Their lap times are in the same ballpark as before (within ~10%).
- They react to drift / surface / boost conditions the same way.

If AI behaviour is visibly worse, most likely cause is the turnRate pathway — double-check that rally's bicycle model is still being applied BEFORE `VehicleUpdate` for AI cars too.

No commit yet.

- [ ] **Step 8: Final all-project build check**

```bash
zig build && ./zig-out/bin/util-tests
```

Expected:
- All 22 projects clean.
- `util-tests` prints `ALL PASSED (0 failures)`.

- [ ] **Step 9: LOC sanity check**

```bash
wc -l rally/main.c
```

Pre-migration was 691 lines (per `git log ... rally/main.c`). Target: ≤ 680. If above, the substitution was too conservative — inspect for remaining inline accel / brake / drag / clamp / move-XZ lines that should have been subsumed. LOC is incidental; behaviour preservation is the gate.

- [ ] **Step 10: Commit**

```bash
git add rally/main.c
git commit -m "$(cat <<'EOF'
rally: migrate core physics to util/vehicle.h (reference migration)

Replaces the per-frame accel/drag/clamp/move-XZ block for both player
and AI cars with a VehicleUpdate call. Rally's bicycle-model steering
stays inline — computed before VehicleUpdate, with steer=0 in the input
so the module does not add a second rotation. Surface drag, drift, and
mini-turbo boost continue to flow through Vehicle's config fields as
per-frame mutations (surfaceDrag → v.drag, drift → v.drag / v.turnRate,
boost → v.maxSpeed). Playtested: lap times, drift feel, AI behaviour,
surface transitions all indistinguishable from pre-migration.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 11: Final verification**

```bash
git log --oneline -5
zig build && ./zig-out/bin/util-tests
```

Expected:
- Top three commits: `rally: migrate core physics to util/vehicle.h`, `util/vehicle.h: horizontal-physics core`, `util/vehicle.h: scaffold header`.
- All 22 projects build; smoke test passes.

---

## Fallback: if bicycle steering preservation fails

The plan's default path preserves rally's bicycle steering by applying `angularVel` to `car->rotation` *before* calling `VehicleUpdate` with `steer = 0`. If the implementer discovers during Step 5 or Step 7 that this doesn't work (e.g., the order of rotation-update vs position-update in VehicleUpdate causes a subtle one-frame lag in cornering feel), the fallback is to use the arcade model entirely:

1. In Step 3, set `vh.turnRate = CAR_TURN_MAX;` (or rally's existing steering-rate constant) and `vin.steer = car->steerInput;`. Multiply `vh.turnRate *= CAR_DRIFT_MULT` when `car->drifting`.
2. Delete the bicycle `angularVel` computation — `VehicleUpdate` owns rotation entirely.
3. Copy `car->rotation = vh.rotation;` back after the call.
4. Playtest. Rally's feel will shift from bicycle (tighter radius at slow speeds, wider at high) to arcade (proportional-ish rotation with the 0.3 floor). If that is acceptable, commit with a note.

Do not pursue both: pick the path that makes rally playable and commit that one. The spec explicitly allows this choice to be made during migration.

---

## Self-Review Checklist

**Spec coverage:**
- [x] `common/util/vehicle.h` with `Vehicle`, `VehicleInput`, `VehicleUpdate` — Task 2.
- [x] State fields `pos / rotation / speed`, config fields `accel / brake / maxSpeed / reverseMax / turnRate / drag` — Task 2 Step 3.
- [x] Input fields `throttle / steer` — Task 2 Step 3.
- [x] Update semantics (5 numbered steps in spec) — Task 2 Step 3 implementation, with matching comments.
- [x] `pos.y` never touched — enforced by implementation; tested explicitly in Task 2 Step 1.
- [x] Yaw convention 0 = +Z — documented in header; tested in Task 2 Step 1.
- [x] Frame-rate independent drag via `powf(drag, dt*60)` — Task 2 Step 3 line 38.
- [x] Speed-dependent turnFactor `Clamp(|speed|/(max*0.3), 0.3, 1.0)` — Task 2 Step 3 line 45; tested in Task 2 Step 1.
- [x] Smoke tests in `util-tests/main.c` covering accel, drag decay, reverse, yaw→direction, turnFactor floor and ramp, pos.y untouched — Task 2 Step 1.
- [x] Rally reference migration — Task 3, both player and AI cars.
- [x] Spec's open question on bicycle vs arcade — addressed by the default path (preserve bicycle) plus the fallback section at the end.

**Placeholder scan:** No TBD / TODO / vague "handle edge cases" language. The one "replace with the actual surface lookup" note in Task 3 Step 3 is accompanied by concrete guidance and the precise invariant the substitution must preserve.

**Type / name consistency:**
- `Vehicle` / `VehicleInput` / `VehicleUpdate` used consistently across Tasks 1, 2, 3.
- `throttle` (not `accel`) used for the input-struct field everywhere after the spec rename.
- `v`, `vh`, `vin`, `u`, `w` are local names; no collision with any rally symbol.

No fixes required after review.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-17-vehicle-module.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using `superpowers:executing-plans`, batch execution with checkpoints.

Which approach?
