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
