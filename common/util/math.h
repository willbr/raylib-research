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
    // Reduce modulo 2π first so this remains O(1) for any input; the while
    // loops then finish in at most one step.
    a = fmodf(a, TAU);
    if (a >   PI) a -= TAU;
    if (a <= -PI) a += TAU;
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
    // Cylindrical-projection sampling (Archimedes hat-box theorem): uniform
    // in z and θ is uniform on the sphere because areas on latitude bands
    // are proportional to Δz. Rejection-free, O(1).
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
