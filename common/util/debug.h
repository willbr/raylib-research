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
