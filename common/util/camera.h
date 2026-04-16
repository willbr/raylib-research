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
