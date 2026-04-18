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
