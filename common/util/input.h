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
