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
// Steps in increments of `radius` so thick deltas never tunnel through thin walls.
static inline void SlideXZ(Vector3 *pos, Vector3 delta, float radius,
                           UtilBlockedFn blocked, void *user) {
    // X axis
    float remX = delta.x;
    float stepX = radius;
    while (fabsf(remX) > 1e-6f) {
        float s = (fabsf(remX) < stepX) ? remX : (remX > 0 ? stepX : -stepX);
        Vector3 tryPos = *pos;
        tryPos.x += s;
        if (blocked(tryPos, radius, user)) break;
        pos->x = tryPos.x;
        remX -= s;
    }
    // Z axis
    float remZ = delta.z;
    float stepZ = radius;
    while (fabsf(remZ) > 1e-6f) {
        float s = (fabsf(remZ) < stepZ) ? remZ : (remZ > 0 ? stepZ : -stepZ);
        Vector3 tryPos = *pos;
        tryPos.z += s;
        if (blocked(tryPos, radius, user)) break;
        pos->z = tryPos.z;
        remZ -= s;
    }
}

#endif // UTIL_COLLIDE_H
