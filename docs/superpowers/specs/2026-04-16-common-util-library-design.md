# Common Utility Library Design

**Date:** 2026-04-16
**Status:** Design approved (pending user review of written spec)
**Scope:** Lean header-only utility library for raylib game prototypes

## Summary

Extract the patterns that every prototype in this repo currently duplicates —
WASD input, mouse-look, entity pools, camera rigs, AABB/sphere collision,
particle systems, HUD widgets — into a set of small header-only modules under
`common/util/`. Keep the existing `objects3d.h` / `sprites2d.h` / `map3d.h`
largely untouched. Prototypes remain single-file `main.c` programs.

This is deliberately **not** a game framework. There is no scene stack, no
asset manager, no game loop takeover, no event bus. Each prototype still owns
its own architecture; the library just removes the parts everyone is forced
to re-type.

## Goals

- Cut the ~21× duplication of WASD input, pool iteration, health bars,
  gravity + floor snap, and mouse-look across prototypes.
- Make new prototypes faster to start: a fresh `genre/main.c` can reach
  "player moves, has a health bar, bullets fire" with a handful of helper
  calls instead of 150 lines of copy-paste.
- Match existing project style: header-only, `static inline`, macro-heavy
  concise definitions, raylib PascalCase, no build-system changes required.
- Preserve the option for any prototype to ignore the library entirely and
  write everything by hand.

## Non-goals

- No vehicle physics module (deferred; was option B in brainstorming).
- No file-format library. Each existing `common/*.h` owns its own I/O; the
  file-format versioning note in `ideas.md` is a separate later task.
- No save/load, no networking, no in-game terminal.
- No asset-path manager. Prototypes keep hardcoded relative paths for now.
- No reorganisation of `objects3d.h` / `sprites2d.h` / `map3d.h` beyond the
  one small move described in `fx.h` below.

## Directory layout

```
common/
  objects3d.h          existing; gains a back-compat re-include of util/fx.h
  sprites2d.h          existing, untouched
  map3d.h              existing, untouched
  util/
    math.h             angle wraps, signs, random floats, easing
    input.h            WASD/arrows/gamepad → Vec2/Vec3; mouse-look
    pool.h             POOL_SPAWN / FOR_ACTIVE macros
    camera.h           CamFPS, CamThirdPerson, CamChase, CamTopDown
    collide.h          AABB/Sphere/Capsule overlap, slide, ground-snap
    fx.h               2D + 3D particles, screen shake (relocated)
    hud.h              health bar, crosshair, damage vignette, debug text
    debug.h            F3 overlay: FPS, pos, free-fly cam swap
```

## Conventions

- **Angles:** radians everywhere. Matches `raymath`, `sinf`, and the majority
  of existing prototypes. `fps/main.c` currently stores yaw/pitch in degrees
  and converts with `DEG2RAD` before use; its migration will flip the stored
  fields to radians.
- **Types:** `PascalCase` (`Pool`, `CamFPS`, `AABB`, `Particle2D`).
- **Functions:** `PascalCase`, raylib-style. Module prefix only where a
  generic name would collide (`FxSpawnBurst2D`, `HudHealthBar`). Core types
  keep clean names (`AABBOverlap`, not `CollideAABBOverlap`).
- **Macros:** `UPPER_SNAKE` (`FOR_ACTIVE`, `RAND_F`). Matches existing
  `CUBE`, `SRECT`, `COL`.
- **Implementation:** `static inline` only. No `#define X_IMPL` gate — the
  existing headers claim to require it but no prototype actually uses it,
  because every game is a single `.c` file. Keeping it uniform avoids a
  trap.
- **Dependencies:** each header pulls only `raylib.h`, `raymath.h`, and the
  stdlib bits it needs. The only intra-util dep is: `camera.h` and
  `collide.h` may use `math.h`. Everything else stands alone.

## Module sketches

Sketches show the intended surface, not final signatures. Writing-plans will
expand these into task lists.

### math.h

Leaf utility header. No state.

```c
// Constants
#define TAU (6.283185307179586f)

// Angle helpers
static inline float WrapAnglePi(float a);     // → [-π, π]
static inline float WrapAngle2Pi(float a);    // → [0, 2π]
static inline float AngleLerp(float a, float b, float t);  // shortest-arc
static inline float AngleDiff(float a, float b);           // signed, shortest

// Scalars
static inline float SignF(float x);           // -1, 0, +1
static inline float ApproachF(float cur, float target, float step);
static inline float SmoothStep(float a, float b, float t);
static inline float EaseOutCubic(float t);
static inline float EaseInOutCubic(float t);

// Random
#define RAND_F(a, b)  ((a) + ((float)GetRandomValue(0, 10000) / 10000.0f) * ((b) - (a)))
static inline float    RandF(float a, float b);
static inline Vector2  RandOnUnitCircle(void);
static inline Vector3  RandOnUnitSphere(void);
static inline Vector3  RandInUnitSphere(void);
```

### input.h

Input normalization. Reads directly from raylib keyboard/mouse/gamepad 0.

```c
// Movement input, combined across WASD + arrows + gamepad left-stick.
// Deadzone ≈ 0.15. Output magnitude ≤ 1.
Vector2 InputMoveDir2(void);

// Same but projected onto the XZ plane relative to a camera yaw (radians).
// Forward = -Z when yaw=0. Used by FPS / third-person movement.
Vector3 InputMoveDir3Flat(float camYaw);

// Mouse-look accumulator. Adds mouse delta × sensitivity to *yaw/*pitch,
// clamps pitch to ±pitchClamp (radians).
void InputLookMouse(float *yaw, float *pitch, float sensitivity, float pitchClamp);

// Gamepad right-stick → yaw/pitch delta, deadzoned.
void InputLookStick(float *yaw, float *pitch, float sensitivity, float dt);

// Mouse capture toggle. Safe to spam; no-ops when already in desired state.
void InputSetCursorLocked(bool locked);
void InputToggleCursorLock(void);

// Shortcut: "is this action down", with one keyboard key and one gamepad button.
bool InputActionDown(int key, int gamepadButton);
bool InputActionPressed(int key, int gamepadButton);
```

### pool.h

Macro-based because C has no generics. Works on any array whose element type
has a `bool active` field.

```c
// Find first inactive slot, set active=true, return index (or -1 if full).
// Usage: int i = POOL_SPAWN(bullets, MAX_BULLETS);
#define POOL_SPAWN(arr, N)  /* ... */

// Iterator. p is the name you want for the loop variable (pointer).
// Usage: FOR_ACTIVE(bullets, MAX_BULLETS, b) { b->pos = ...; }
#define FOR_ACTIVE(arr, N, p)  /* ... */

// Ring-buffer variant for particles / decals. idx_var is an int you own.
// Usage: POOL_RING_PUSH(decals, MAX_DECALS, decalIdx);
#define POOL_RING_PUSH(arr, N, idx_var)  /* ... */

// Count helper. Linear scan; fine at MAX_N ≤ a few hundred.
int PoolCountActive(const void *arr, int stride, int N, int activeOffset);
// + a typed convenience macro POOL_COUNT_ACTIVE(arr, N)
```

Macro hygiene is the risk here. All macros will use block-local names
(`_i_`, `_pool_`) to avoid shadowing. `POOL_SPAWN` as a value-returning
macro relies on GNU statement expressions (`({ ... })`), which Zig cc
(Clang) supports. If portability to a non-Clang compiler is ever needed,
the fallback is a `POOL_SPAWN_INTO(idx_var, arr, N)` do-while variant that
assigns into a caller-supplied variable. Macros will be validated against
each existing prototype's loop patterns during migration.

### camera.h

Four rigs, each a small state struct with `Update` and a `ToCamera3D` to
produce a raylib `Camera3D`.

```c
typedef struct { Vector3 pos; float yaw, pitch; float fov; }     CamFPS;
typedef struct { Vector3 target; float yaw, pitch, dist;
                 Vector3 shoulder; float fov; }                    CamThirdPerson;
typedef struct { Vector3 target; float targetYaw; float dist,
                 height, lagSpeed; float fov; Vector3 _pos; }      CamChase;
typedef struct { Vector3 target; float height, angle, fov; }      CamTopDown;

// Each: fill a sane default with the Init fn.
CamFPS          CamFPSInit(Vector3 startPos);
CamThirdPerson  CamThirdPersonInit(Vector3 target);
CamChase        CamChaseInit(Vector3 target, float targetYaw);
CamTopDown      CamTopDownInit(Vector3 target);

// Update integrates accumulated yaw/pitch or follows the target.
// Caller drives look input itself (via input.h helpers).
void CamFPSUpdate(CamFPS *c, float dt);
void CamThirdPersonUpdate(CamThirdPerson *c, Vector3 target, float dt);
void CamChaseUpdate(CamChase *c, Vector3 target, float targetYaw, float dt);
void CamTopDownUpdate(CamTopDown *c, Vector3 target);

// Produce a raylib Camera3D suitable for BeginMode3D.
Camera3D CamFPSToCamera3D(const CamFPS *c);
Camera3D CamThirdPersonToCamera3D(const CamThirdPerson *c);
Camera3D CamChaseToCamera3D(const CamChase *c);
Camera3D CamTopDownToCamera3D(const CamTopDown *c);
```

Split of concerns: `input.h` produces yaw/pitch deltas, prototype adds them
to the cam struct, `camera.h` turns state into a raylib camera. No hidden
input reads inside camera code.

### collide.h

Thin geometry + the two patterns every 3D prototype reimplements: gravity +
floor snap, and wall slide with separate-axis resolution.

```c
typedef struct { Vector3 center, half; }            AABB;
typedef struct { Vector3 center; float radius; }    Sphere;
typedef struct { Vector3 a, b; float radius; }      Capsule;

bool AABBOverlap(AABB a, AABB b);
bool AABBvsSphere(AABB a, Sphere s);
bool SphereOverlap(Sphere a, Sphere b);
float  PointToAABBDist(Vector3 p, AABB a);   // 0 when inside

// Integrate gravity, clamp y to floorY, set *grounded. Matches the pattern
// in fps/3rd-person/platformer — consolidates ~15 copies.
void GroundSnap(Vector3 *pos, float *velY, float dt, float gravity,
                float floorY, bool *grounded);

// Attempt to move by `delta`, sliding along walls. Tries X axis and Z axis
// separately (the pattern fps uses). `radius` is the player's horizontal
// cylinder radius. Walls are treated as vertical AABBs.
void SlideXZ(Vector3 *pos, Vector3 delta, const AABB *walls, int wallCount,
             float radius);
```

### fx.h

Particle pools (2D and 3D) and screen shake. Screen shake + the 3D particle
helpers currently live in `objects3d.h` (lines ~150-240). Their definitions
are **deleted** from `objects3d.h` and moved here. `objects3d.h` then gains
one line at the top — `#include "util/fx.h"` — so the identifiers
(`ScreenShake`, `ShakeTrigger`, `SpawnParticleBurst`, etc.) remain visible
to existing callers and no prototype needs to change. The 3D particle
helpers keep their current names as aliases (`#define SpawnParticleBurst
FxSpawnBurst3D` or thin `static inline` wrappers — decided during
implementation).

```c
typedef struct { Vector2 pos, vel; float life, maxLife, size;
                 Color color; bool active; } Particle2D;
typedef struct { Vector3 pos, vel; float life, maxLife, size;
                 Color color; bool active; } Particle3D;

void FxSpawnBurst2D(Particle2D *pool, int N, Vector2 origin, Color col,
                    int count, float speed, float life);
void FxSpawnBurst3D(Particle3D *pool, int N, Vector3 origin, Color col,
                    int count, float speed, float life);
void FxUpdateParticles2D(Particle2D *pool, int N, float dt, Vector2 gravity);
void FxUpdateParticles3D(Particle3D *pool, int N, float dt, Vector3 gravity);
void FxDrawParticles2D(Particle2D *pool, int N);
void FxDrawParticles3D(Particle3D *pool, int N);  // draws as billboarded spheres

// Screen shake (relocated from objects3d.h)
typedef struct { float amp, decay; float t; } ScreenShake;
void    ShakeTrigger(ScreenShake *s, float amplitude, float decayPerSec);
void    ShakeUpdate(ScreenShake *s, float dt);
Vector2 ShakeOffset(const ScreenShake *s);  // add to camera target or HUD
```

### hud.h

Screen-space widgets. Everything takes explicit coordinates; no global
layout state except `HudDebugText`'s line cursor.

```c
// Health bar with background and outline. Colors default to green/red/black
// if you pass (Color){0} — will provide a HUD_BAR_DEFAULTS convenience macro.
void HudHealthBar(int x, int y, int w, int h, float pct,
                  Color fill, Color bg, Color outline);

// Simple crosshair at (x, y). Style: 0=cross, 1=dot, 2=circle.
void HudCrosshair(int x, int y, int size, int thick, int style, Color color);

// Full-screen red overlay at `alpha` (0..1). Safe to call every frame.
void HudDamageVignette(float alpha, Color color);

// Black bars top/bottom, each `pct` of screen height. Used for cinematic
// transitions (seen in biplane/starfox intro candidates).
void HudLetterbox(float pct, Color color);

// Auto-stacked top-left debug text. Reset per frame with HudDebugReset.
void HudDebugReset(void);
void HudDebugText(const char *text);  // or HudDebugTextf with format?
```

Considering adding a `HudDebugTextf(fmt, ...)` that wraps `TextFormat`; will
decide during implementation based on whether variadic macros in C99 cause
pain with warnings flags.

### debug.h

Small opt-in overlay. Default off; toggle with F3.

```c
typedef struct { bool visible; float accumDt; int frameCount; float shownFps; }
    DebugOverlay;

void DebugOverlayUpdate(DebugOverlay *d, float dt);            // call every frame
void DebugOverlayToggleOnKey(DebugOverlay *d, int key);        // default F3
void DebugOverlayDraw(const DebugOverlay *d,
                      const char *gameStateLine,
                      Vector3 playerPos);

// 3D debug shapes. Cheap lines, fine to spam.
void DebugDrawAABB(AABB a, Color col);
void DebugDrawSphere(Sphere s, Color col);
```

## Migration plan

Three phases. No big-bang rewrite; each phase leaves the repo green.

### Phase 1 — Add headers, touch zero games

Create all eight util headers with full implementations. Each header must
compile standalone (smoke-test: include it alone in a throwaway `.c` and
build with the project flags). The existing `objects3d.h` gains one line at
the top: `#include "util/fx.h"`. Every existing prototype continues to
build and run bit-identical.

**Exit criterion:** `zig build` succeeds for all 21 prototypes.

### Phase 2 — Reference migration: `fps/`

Port `fps/main.c` to use the new library. This is the blueprint for every
future migration and doubles as the first real validation that the API
actually fits real code.

Expected changes to `fps/main.c`:

- `yaw`/`pitch` fields change from degrees to radians. All `* DEG2RAD` call
  sites removed.
- `WallCollision()` + wall-slide loop replaced with `SlideXZ()`.
- Gravity + `pos.y <= 0` block replaced with `GroundSnap()`.
- Bullet / enemy / particle / pickup loops replaced with `FOR_ACTIVE`.
- Camera construction replaced with a `CamFPS` + `CamFPSToCamera3D`.
- Health bar / crosshair / damage vignette replaced with `hud.h` calls.
- Mouse-look block replaced with `InputLookMouse`.

**Exit criterion:** `zig build run-fps` launches and feels identical to a
casual playtest. Wave spawning, enemy behaviour, weapon feel, health and
damage all match. LOC for `fps/main.c` drops by ~100-150 lines.

### Phase 3 — Port others opportunistically

No schedule. Any prototype touched for unrelated work picks up the library
at the same time. Prototypes that are never touched again never migrate;
that is fine. The library earns its keep from future prototypes and from
whichever existing ones get revisited.

Preferred next targets, in priority order:
1. `3rd-person/` (near-copy of fps; highest ROI, reuses the fps migration)
2. `zelda/` (largest 2D game; validates 2D particles + hud on a non-FPS)
3. `platformer/` (tile-based; stresses `SlideXZ` on tile grids)
4. `rally/` / `boat/` / other vehicle games (deferred until a vehicle
   physics module is designed; the util library alone will not save much
   here because most of their duplication is driving code).

## Verification

There is no test suite and no linter in this repo. Verification is manual
and hinges on Phase 2:

- **Compiles:** `zig build` builds all 21 prototypes after each phase.
- **Behaves:** `zig build run-fps` plays indistinguishably from the
  pre-migration binary. A short scripted playthrough (move, shoot, take
  damage, clear a wave, reload, switch weapon) is the smoke test.
- **Headers standalone:** each `util/*.h` compiles when included in
  isolation. A trivial scratch `.c` file (kept out of the repo, or
  committed under `common/util/tests/` if we want it permanently) is the
  fastest way to confirm this during development.

## Open questions / deferred decisions

- `HudDebugText` vs `HudDebugTextf`: variadic macro wrapping `TextFormat`
  vs user formatting themselves. Decide during `hud.h` implementation.
- Pool macro hygiene: audit after writing against every prototype's
  existing pool loop to confirm no variable-name clashes.
- Whether to add `FxSpawnTracer` (line-segment particle for bullet trails):
  `fps` has it inline; probably worth including in `fx.h` after the fps
  migration tells us its final shape.
- Whether `SlideXZ` needs a variant that takes cylinder-vs-AABB or stays
  as AABB-only. Phase 2 will answer this.

## Future work (explicitly out of scope here)

- Vehicle physics module (the 5-game near-duplicate surface area).
- 2D hitbox/hurtbox generalised outside `sprites2d.h` so `zelda` / `resi`
  can use it cleanly.
- Turn-based battle scaffold (pokemon / rpgbattle / goldensun).
- Asset-path helper; file-format version numbers (ideas.md).
- Colour palette constants.
