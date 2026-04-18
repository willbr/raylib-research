# Vehicle Physics Module Design

**Date:** 2026-04-17
**Status:** Design approved (pending user review of written spec)
**Scope:** Lean horizontal-physics module for the racing-style prototypes

## Summary

Extract the near-identical horizontal-vehicle-physics pattern that the five
racing prototypes (`rally`, `boat`, `kart`, `micromachines`, `wipeout`) all
re-implement into a single tiny header — `common/util/vehicle.h` — matching the
existing `common/util/` style. One struct plus one update function. No drift
system, no boost system, no waypoints, no collision response, no vertical
physics: those are expressed by the caller either as direct mutation of the
Vehicle's config fields each frame, or by using other utilities already
present (`GroundSnap`, `SlideXZ`).

This is the "option B" module deferred from the common-util-library spec
(2026-04-16). It is deliberately the *lean* slice of that option — not a
racing framework, just the shared physics core.

## Goals

- Remove the ~30× duplicated speed-integration / steering / XZ-move block that
  appears nearly identical across 5 vehicle prototypes.
- Keep each prototype's idiosyncrasies (boat waves, micromachines spin-out,
  wipeout hover, kart items, rally surface types) entirely in its own `main.c`.
- Match the existing `util/` philosophy: header-only, `static inline`,
  unopinionated, one clear responsibility per header.
- Validate by porting `rally/` as the reference migration.

## Non-goals

- Not a physics engine. No friction circle, lateral-slip model, or tire forces.
- Not a racing framework. No track definitions, AI, lap timing, or waypoint
  progression.
- Not a universal vehicle. Does not cover flight (`biplane`), trick sports
  (`snowboard`, `skate`), or weapons (`kart` / `wipeout`). Those systems stay
  in their prototypes and run alongside `VehicleUpdate`.
- No drift / boost / surface subsystems. Games mutate Vehicle's config fields
  (`turnRate`, `drag`, `maxSpeed`) directly per-frame to express those effects.

## Directory layout

Adds one file:

```
common/util/vehicle.h
```

No changes to existing `util/` files. No changes to other prototypes.

## API

```c
// common/util/vehicle.h
#ifndef UTIL_VEHICLE_H
#define UTIL_VEHICLE_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>

typedef struct {
    // State — VehicleUpdate mutates these.
    Vector3 pos;        // module touches x and z only; y is caller's responsibility
    float rotation;     // yaw in radians. 0 = +Z forward (matches CamChase convention)
    float speed;        // signed scalar along the forward axis; negative = reversing

    // Config — caller-owned. Mutate per-frame to express surface / drift /
    // boost effects (e.g. car.drag = offRoad ? DRAG_MUD : DRAG_ROAD;)
    float accel;        // forward acceleration, units/sec² when in.throttle > 0
    float brake;        // braking / reverse acceleration, units/sec² when in.throttle < 0
    float maxSpeed;     // hard clamp on forward speed
    float reverseMax;   // hard clamp on reverse speed (pass 0 to disable reverse)
    float turnRate;     // radians/sec at full steer × full turnFactor
    float drag;         // multiplier per 60fps frame (typical 0.98–0.993)
} Vehicle;

typedef struct {
    float throttle;     // −1..1  (positive = throttle, negative = brake/reverse)
    float steer;        // −1..1  (positive = right)
} VehicleInput;

void VehicleUpdate(Vehicle *v, VehicleInput in, float dt);

#endif // UTIL_VEHICLE_H
```

Caller pattern:

```c
// Init (named fields; no VehicleInit helper needed)
Vehicle car = {
    .pos = {0, 0, 0}, .rotation = 0, .speed = 0,
    .accel = 35.0f, .brake = 40.0f,
    .maxSpeed = 45.0f, .reverseMax = 15.0f,
    .turnRate = 2.8f, .drag = 0.992f,
};

// Per frame — optionally mutate config, then update
car.drag     = offRoad ? CAR_DRAG_MUD : CAR_DRAG_ROAD;
car.turnRate = drifting ? CAR_TURN * 1.6f : CAR_TURN;
car.maxSpeed = boostTimer > 0 ? CAR_MAX * 1.3f : CAR_MAX;

VehicleInput in = {
    .throttle = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f),
    .steer    = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f),
};
VehicleUpdate(&car, in, dt);
// car.pos.y is not touched — caller sets it (GroundSnap, wave, hover, etc.)
```

## Update semantics

`VehicleUpdate` does exactly this, in order:

1. **Integrate throttle/brake**
   - If `in.throttle > 0`: `v->speed += v->accel * in.throttle * dt`
   - If `in.throttle < 0`: `v->speed += v->brake * in.throttle * dt` (so negative input decelerates or reverses)

2. **Apply drag** (frame-rate independent):
   `v->speed *= powf(v->drag, dt * 60.0f)`
   This gives identical behaviour to `v->speed *= v->drag` at 60fps, and
   degrades gracefully at other frame rates. Existing games pin 60fps via
   `SetTargetFPS(60)` so in practice this is equivalent.

3. **Clamp speed**:
   - `v->speed = Clamp(v->speed, -v->reverseMax, v->maxSpeed)`

4. **Steer with speed-dependent turnFactor**:
   - `turnFactor = Clamp(|v->speed| / (v->maxSpeed * 0.3f), 0.3f, 1.0f)`
   - `v->rotation += in.steer * v->turnRate * turnFactor * dt`
   - The 0.3 floor means steering still has visible effect at low speeds;
     the 0.3× ramp-to-full reflects the typical arcade-racer feel across
     rally/kart/micromachines/wipeout.

5. **Move along XZ plane**:
   - `v->pos.x += sinf(v->rotation) * v->speed * dt`
   - `v->pos.z += cosf(v->rotation) * v->speed * dt`

`v->pos.y` is never read or written. The caller applies whatever vertical
treatment the game requires — `GroundSnap` (kart, rally), wave-height
function (boat), slope-normal gravity (snowboard — not a direct user of
this module but the pattern applies), or hover-lerp (wipeout).

## Conventions and integration with existing util

- **Angles in radians.** Matches `camera.h` / `collide.h` / existing vehicle
  prototypes that store `rotation` in radians.
- **Yaw convention: 0 = +Z forward.** Matches `CamChase.targetYaw` (the
  vehicle-family convention already documented in `camera.h`). This is the
  inverse of `CamFPS.yaw` — a `Vehicle.rotation` can be passed directly to
  `CamChaseUpdate(&chase, car.pos, car.rotation, dt)` without transformation.
- **No cross-module dependencies.** `vehicle.h` includes only `raylib.h`,
  `raymath.h`, and `<math.h>`. It does not `#include "math.h"` from the
  util directory (would add a dead include) and has no runtime dependency
  on any other util header.

## Testing

Smoke-tested in `util-tests/main.c` alongside the other pure-logic modules.
A new `test_vehicle()` stub will be added to the existing harness. Planned
assertions:

- **Accelerates toward maxSpeed.** After N frames with `in.throttle = 1`,
  `speed` asymptotes below `maxSpeed`. E.g. after 1 simulated second,
  `speed > 0` and `speed < maxSpeed`. After 5 seconds, `speed >
  maxSpeed * 0.95f`.
- **Brakes / reverses.** After N frames with `in.throttle = -1` from rest,
  `speed < 0` (went negative). Clamped to `-reverseMax`.
- **Drag alone decays.** Starting from `speed = maxSpeed`, no input, N
  frames: `speed < maxSpeed * 0.5f` after a few seconds.
- **Steering rotates only while moving.** At `speed = 0`, full steer, one
  frame of dt — rotation changes by only `0.3 * turnRate * dt` (the floor);
  at `speed = maxSpeed`, same input → `turnRate * dt`.
- **Forward motion follows yaw.** At `rotation = 0`, forward speed → `pos.z`
  increases, `pos.x` unchanged. At `rotation = π/2`, `pos.x` increases,
  `pos.z` unchanged.
- **Y is never touched.** After any number of updates, `pos.y` equals whatever
  the caller left it at.

No integration test beyond the reference migration (rally playtests the same
way the fps migration playtested).

## Reference migration

One target for this plan: **`rally/main.c`**.

Rationale: rally has the richest combination of features a vehicle module
must survive — three surface types, drift with mini-turbo boost, bicycle
steering model, waypoint progression. If the API supports rally, the other
racing games (kart, micromachines, wipeout) will be trivially portable and
will follow opportunistically outside this plan.

Expected changes in `rally/main.c`:

- The `Car` struct's physics fields (`pos`, `rotation`, `speed`) are replaced
  by or co-exist with a `Vehicle` struct. (Probably: embed `Vehicle` inside
  `Car`, since `Car` has other state like `lap`, `nextWP`, `drifting`,
  `driftTime`, `boostTimer`, `isPlayer`, etc. that are rally-specific.)
- The ~30-line physics block around `rally/main.c:441-470` is replaced by:
  - Mutation of `car.vehicle.drag`/`turnRate`/`maxSpeed` based on surface
    and drift/boost state.
  - One call to `VehicleUpdate(&car.vehicle, input, dt)`.
- The bicycle-model steering currently in rally (`angularVel = speed *
  tan(steerAngle) / wheelbase`) becomes the simpler speed-dependent
  turn-factor model the module uses. If this changes rally's handling
  feel meaningfully during playtest, we revisit — either expose turnFactor
  customisation or leave rally's bicycle model inline and migrate only the
  accel/drag/speed parts.
- Surface types (`CAR_DRAG_ROAD`, `_GRAVEL`, `_MUD`) stay as rally-local
  `#define`s; they are assigned into `car.vehicle.drag` each frame based
  on `SurfaceType` lookup.
- Drift becomes: `if (drifting) { car.vehicle.turnRate *= CAR_DRIFT_MULT;
  car.vehicle.drag *= (CAR_DRIFT_DRAG / CAR_DRAG_ROAD); }`. The mini-turbo
  on release is rally-local — raises `car.vehicle.maxSpeed` for the boost
  duration.
- AI cars use the same `Vehicle` struct; their input is synthesized from
  waypoint targeting logic (rally-local, unchanged).

**Playtest success criterion:** `zig build run-rally` behaves
indistinguishably from the pre-migration binary in a short scripted
playthrough — lap timing, drift feel, surface audio/visual transitions,
AI difficulty. LOC target: ≤ 20 lines saved in `rally/main.c`. LOC is
incidental; behaviour preservation is the real gate.

## Migration plan beyond rally

Explicitly out of scope for the implementation plan that follows this
spec. Future migrations (`kart`, `micromachines`, `wipeout`, `boat`) happen
when those games are next touched for any reason, same as the strategy used
for the util library.

## Open questions

- **Bicycle-vs-arcade steering model for rally.** The plan's default
  steering is the arcade speed-dependent turnFactor that every other racing
  prototype uses. Rally currently uses a bicycle model. The plan leaves
  this unresolved: if the arcade model feels wrong during playtest, the
  fallback is to keep rally's bicycle math inline and have it write into
  `car.vehicle.rotation` directly after `VehicleUpdate` (or simply not
  migrate rally's steering at all, leaving it as a rally-specific detail).
  Decided during reference migration, not during spec approval.

- **2D vs 3D type.** `micromachines` stores position as `Vector2` (top-down).
  `skate` uses a separate `z` field. The plan's `Vehicle.pos` is `Vector3`.
  Micromachines' migration (not in this plan) would pack its Vector2 into
  `(x, 0, y)` before calling and unpack after. Acceptable; a `Vehicle2D`
  variant is not worth the duplicated code.

## Future work (out of scope here)

- Vehicle module variants for flight (`biplane`) or trick-sports
  (`snowboard`, `skate`). Flight especially has enough unique state
  (pitch/yaw/roll, lift) that it would be its own module (`flight.h`) not
  a variant of this one.
- Track / waypoint helper used by all racing games (currently each rolls
  its own).
- Generic AI opponent helper — waypoint-following, rubber-band.
