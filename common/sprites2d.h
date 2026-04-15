// sprites2d.h — Simple 2D sprite format built from primitives
// Header-only: just #include this file
//
// Usage:
//   Sprite2DPart parts[] = {
//       SRECT(0, 0,   20, 30,  RED),        // body
//       SCIRCLE(0, -20, 8,     GOLD),       // head
//       SLINE(-12, 5, -20, 20, 2, BLUE),    // arm
//   };
//   DrawSprite2D(parts, count, screenX, screenY, scale);

#ifndef SPRITES2D_H
#define SPRITES2D_H

#include "raylib.h"
#include "rlgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// --- Types ---

typedef enum {
    SP_RECT,
    SP_CIRCLE,
    SP_TRIANGLE,
    SP_LINE,
    SP_ELLIPSE,
    SP_POLYGON,    // regular polygon: x,y = center, w = radius, h = sides, extra1 = rotation
} Sprite2DType;

typedef struct {
    Sprite2DType type;
    float x, y;        // offset from sprite origin
    float w, h;        // rect: width/height, circle: radius (w only), line: x2/y2 offset, triangle: x2/y2
    float extra1, extra2; // triangle: x3/y3, line: thickness, ellipse: ry
    Color color;
} Sprite2DPart;

// --- Shorthand macros ---

// SRECT(offsetX, offsetY, width, height, color)
#define SRECT(ox,oy, w,h, col) \
    { SP_RECT, ox, oy, w, h, 0, 0, col }

// SCIRCLE(offsetX, offsetY, radius, color)
#define SCIRCLE(ox,oy, r, col) \
    { SP_CIRCLE, ox, oy, r, 0, 0, 0, col }

// SELLIPSE(offsetX, offsetY, radiusX, radiusY, color)
#define SELLIPSE(ox,oy, rx,ry, col) \
    { SP_ELLIPSE, ox, oy, rx, 0, 0, ry, col }

// STRIANGLE(x1,y1, x2,y2, x3,y3, color) — 3 vertex offsets from origin
#define STRIANGLE(x1,y1, x2,y2, x3,y3, col) \
    { SP_TRIANGLE, x1, y1, x2, y2, x3, y3, col }

// SLINE(x1,y1, x2,y2, thickness, color)
#define SLINE(x1,y1, x2,y2, thick, col) \
    { SP_LINE, x1, y1, x2, y2, thick, 0, col }

// SPOLYGON(centerX, centerY, radius, numSides, rotation, color)
#define SPOLYGON(ox,oy, r, sides, rot, col) \
    { SP_POLYGON, ox, oy, r, sides, rot, 0, col }

// --- Drawing ---

static inline void DrawSprite2DPart(Sprite2DPart *p, float ox, float oy, float scale) {
    float px = ox + p->x * scale;
    float py = oy + p->y * scale;

    switch (p->type) {
        case SP_RECT: {
            float w = p->w * scale, h = p->h * scale;
            DrawRectangle((int)(px - w/2), (int)(py - h/2), (int)w, (int)h, p->color);
            break;
        }
        case SP_CIRCLE:
            DrawCircle((int)px, (int)py, p->w * scale, p->color);
            break;
        case SP_ELLIPSE:
            DrawEllipse((int)px, (int)py, p->w * scale, p->extra2 * scale, p->color);
            break;
        case SP_TRIANGLE: {
            Vector2 v1 = { ox + p->x * scale, oy + p->y * scale };
            Vector2 v2 = { ox + p->w * scale, oy + p->h * scale };
            Vector2 v3 = { ox + p->extra1 * scale, oy + p->extra2 * scale };
            DrawTriangle(v1, v2, v3, p->color);
            DrawTriangle(v1, v3, v2, p->color);  // both windings
            break;
        }
        case SP_LINE: {
            float x2 = ox + p->w * scale;
            float y2 = oy + p->h * scale;
            DrawLineEx((Vector2){px, py}, (Vector2){x2, y2}, p->extra1 * scale, p->color);
            break;
        }
        case SP_POLYGON: {
            int sides = (int)p->h;
            if (sides < 3) sides = 3;
            float r = p->w * scale;
            float rot = p->extra1;
            // Draw as filled triangles from center to each edge
            for (int s = 0; s < sides; s++) {
                float a0 = rot + (float)s / sides * 2.0f * PI;
                float a1 = rot + (float)(s + 1) / sides * 2.0f * PI;
                Vector2 v0 = { px + cosf(a0) * r, py + sinf(a0) * r };
                Vector2 v1 = { px + cosf(a1) * r, py + sinf(a1) * r };
                DrawTriangle((Vector2){px, py}, v0, v1, p->color);
                DrawTriangle((Vector2){px, py}, v1, v0, p->color);
            }
            break;
        }
    }
}

static inline void DrawSprite2D(Sprite2DPart *parts, int count, float x, float y, float scale) {
    for (int i = 0; i < count; i++) {
        DrawSprite2DPart(&parts[i], x, y, scale);
    }
}

// Draw with outline (for editor selection)
static inline void DrawSprite2DPartOutline(Sprite2DPart *p, float ox, float oy, float scale, Color outlineCol) {
    float px = ox + p->x * scale;
    float py = oy + p->y * scale;

    switch (p->type) {
        case SP_RECT: {
            float w = p->w * scale, h = p->h * scale;
            DrawRectangleLines((int)(px - w/2), (int)(py - h/2), (int)w, (int)h, outlineCol);
            break;
        }
        case SP_CIRCLE:
            DrawCircleLines((int)px, (int)py, p->w * scale, outlineCol);
            break;
        case SP_ELLIPSE:
            DrawEllipseLines((int)px, (int)py, p->w * scale, p->extra2 * scale, outlineCol);
            break;
        case SP_TRIANGLE: {
            Vector2 v1 = { ox + p->x * scale, oy + p->y * scale };
            Vector2 v2 = { ox + p->w * scale, oy + p->h * scale };
            Vector2 v3 = { ox + p->extra1 * scale, oy + p->extra2 * scale };
            DrawLineV(v1, v2, outlineCol);
            DrawLineV(v2, v3, outlineCol);
            DrawLineV(v3, v1, outlineCol);
            break;
        }
        case SP_LINE: {
            float x2 = ox + p->w * scale;
            float y2 = oy + p->h * scale;
            DrawLineEx((Vector2){px, py}, (Vector2){x2, y2}, p->extra1 * scale + 2, outlineCol);
            break;
        }
        case SP_POLYGON: {
            int sides = (int)p->h;
            if (sides < 3) sides = 3;
            float r = p->w * scale;
            float rot = p->extra1;
            for (int s = 0; s < sides; s++) {
                float a0 = rot + (float)s / sides * 2.0f * PI;
                float a1 = rot + (float)(s + 1) / sides * 2.0f * PI;
                DrawLineV((Vector2){px + cosf(a0)*r, py + sinf(a0)*r},
                          (Vector2){px + cosf(a1)*r, py + sinf(a1)*r}, outlineCol);
            }
            break;
        }
    }
}

// --- File I/O ---
// Text format:
//   # sprite
//   rect ox oy w h  r g b a
//   circle ox oy radius  r g b a
//   ellipse ox oy rx ry  r g b a
//   tri x1 y1 x2 y2 x3 y3  r g b a
//   line x1 y1 x2 y2 thickness  r g b a

static inline bool SaveSprite2D(const char *filename, Sprite2DPart *parts, int count) {
    FILE *f = fopen(filename, "w");
    if (!f) return false;
    fprintf(f, "# sprite2d %d parts\n", count);
    for (int i = 0; i < count; i++) {
        Sprite2DPart *p = &parts[i];
        switch (p->type) {
            case SP_RECT:
                fprintf(f, "rect %.1f %.1f %.1f %.1f  %d %d %d %d\n",
                    p->x, p->y, p->w, p->h, p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case SP_CIRCLE:
                fprintf(f, "circle %.1f %.1f %.1f  %d %d %d %d\n",
                    p->x, p->y, p->w, p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case SP_ELLIPSE:
                fprintf(f, "ellipse %.1f %.1f %.1f %.1f  %d %d %d %d\n",
                    p->x, p->y, p->w, p->extra2, p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case SP_TRIANGLE:
                fprintf(f, "tri %.1f %.1f %.1f %.1f %.1f %.1f  %d %d %d %d\n",
                    p->x, p->y, p->w, p->h, p->extra1, p->extra2,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case SP_LINE:
                fprintf(f, "line %.1f %.1f %.1f %.1f %.1f  %d %d %d %d\n",
                    p->x, p->y, p->w, p->h, p->extra1,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case SP_POLYGON:
                fprintf(f, "poly %.1f %.1f %.1f %.0f %.2f  %d %d %d %d\n",
                    p->x, p->y, p->w, p->h, p->extra1,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
        }
    }
    fclose(f);
    return true;
}

static inline int LoadSprite2D(const char *filename, Sprite2DPart *parts, int maxParts) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && count < maxParts) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        Sprite2DPart *p = &parts[count];
        int r, g, b, a;
        if (strncmp(line, "rect ", 5) == 0) {
            if (sscanf(line + 5, "%f %f %f %f %d %d %d %d",
                &p->x, &p->y, &p->w, &p->h, &r, &g, &b, &a) == 8) {
                p->type = SP_RECT; p->color = (Color){r,g,b,a}; count++;
            }
        } else if (strncmp(line, "circle ", 7) == 0) {
            if (sscanf(line + 7, "%f %f %f %d %d %d %d",
                &p->x, &p->y, &p->w, &r, &g, &b, &a) == 7) {
                p->type = SP_CIRCLE; p->color = (Color){r,g,b,a}; count++;
            }
        } else if (strncmp(line, "ellipse ", 8) == 0) {
            if (sscanf(line + 8, "%f %f %f %f %d %d %d %d",
                &p->x, &p->y, &p->w, &p->extra2, &r, &g, &b, &a) == 8) {
                p->type = SP_ELLIPSE; p->color = (Color){r,g,b,a}; count++;
            }
        } else if (strncmp(line, "tri ", 4) == 0) {
            if (sscanf(line + 4, "%f %f %f %f %f %f %d %d %d %d",
                &p->x, &p->y, &p->w, &p->h, &p->extra1, &p->extra2,
                &r, &g, &b, &a) == 10) {
                p->type = SP_TRIANGLE; p->color = (Color){r,g,b,a}; count++;
            }
        } else if (strncmp(line, "line ", 5) == 0) {
            if (sscanf(line + 5, "%f %f %f %f %f %d %d %d %d",
                &p->x, &p->y, &p->w, &p->h, &p->extra1,
                &r, &g, &b, &a) == 9) {
                p->type = SP_LINE; p->color = (Color){r,g,b,a}; count++;
            }
        } else if (strncmp(line, "poly ", 5) == 0) {
            if (sscanf(line + 5, "%f %f %f %f %f %d %d %d %d",
                &p->x, &p->y, &p->w, &p->h, &p->extra1,
                &r, &g, &b, &a) == 9) {
                p->type = SP_POLYGON; p->color = (Color){r,g,b,a}; count++;
            }
        }
    }
    fclose(f);
    return count;
}

// --- Prefab sprites ---

// Simple character (~60px tall at scale 1)
#define PREFAB_SPRITE_HUMAN(bodyCol, skinCol, hairCol) { \
    SRECT(0, 10, 12, 16, bodyCol),        /* legs   */ \
    SRECT(0, -4, 16, 20, bodyCol),        /* body   */ \
    SCIRCLE(0, -18, 7, skinCol),          /* head   */ \
    SCIRCLE(0, -22, 5, hairCol),          /* hair   */ \
    SRECT(-12, -2, 4, 16, bodyCol),       /* arm L  */ \
    SRECT(12, -2, 4, 16, bodyCol),        /* arm R  */ \
}

#define PREFAB_SPRITE_MONSTER(col) { \
    SRECT(0, 0, 30, 36, col),             /* body   */ \
    SCIRCLE(-6, -10, 3, RED),             /* eye L  */ \
    SCIRCLE(6, -10, 3, RED),              /* eye R  */ \
    SRECT(0, 12, 10, 8, col),             /* leg L  */ \
    SRECT(0, 12, 10, 8, col),             /* leg R  */ \
}

// --- 3D Billboard rendering ---
// Draw a 2D sprite as a billboard in 3D space (requires active BeginMode3D)
// Uses GetWorldToScreen to project, then draws in screen space after EndMode3D
// Alternatively, use this with a render texture for true 3D billboarding

// Store placed sprites for 3D scenes
typedef enum { SPRITE_BILLBOARD, SPRITE_PLANE } SpriteDisplayMode;

typedef struct {
    Vector3 pos;
    float scale;
    float rotY;
    Sprite2DPart parts[32];
    int partCount;
    char filename[32];
    bool active;
    SpriteDisplayMode displayMode;  // billboard or flat plane
} PlacedSprite2D;

// Draw a 2D sprite at a 3D position (call between BeginMode3D/EndMode3D)
// Projects to screen space and draws there — simple billboard effect
static inline void DrawSprite2DAt3D(Sprite2DPart *parts, int count, Vector3 worldPos,
                                     float scale, Camera3D cam) {
    Vector2 screen = GetWorldToScreen(worldPos, cam);
    // Distance-based scale
    float dist = Vector3Distance(worldPos, cam.position);
    float distScale = 20.0f / (dist + 1.0f);  // closer = bigger
    float finalScale = scale * distScale;
    if (finalScale < 0.1f) return;  // too far, skip

    // Find bottom extent of sprite so origin is at the feet
    float maxY = 0;
    for (int i = 0; i < count; i++) {
        float bottom = 0;
        switch (parts[i].type) {
            case SP_RECT:    bottom = parts[i].y + parts[i].h / 2; break;
            case SP_CIRCLE:  bottom = parts[i].y + parts[i].w; break;
            case SP_ELLIPSE: bottom = parts[i].y + parts[i].extra2; break;
            case SP_TRIANGLE: {
                bottom = parts[i].y;
                if (parts[i].h > bottom) bottom = parts[i].h;
                if (parts[i].extra2 > bottom) bottom = parts[i].extra2;
                break;
            }
            case SP_LINE:    bottom = parts[i].y > parts[i].h ? parts[i].y : parts[i].h; break;
            case SP_POLYGON: bottom = parts[i].y + parts[i].w; break;
        }
        if (bottom > maxY) maxY = bottom;
    }

    DrawSprite2D(parts, count, screen.x, screen.y - maxY * finalScale, finalScale);
}

// Draw a 2D sprite as a flat plane on the ground (call inside BeginMode3D)
// Maps sprite X to world X, sprite Y to world -Z (so "up" in sprite = "forward")
static inline void DrawSprite2DAsPlane(Sprite2DPart *parts, int count, Vector3 worldPos,
                                        float scale, float rotY, Camera3D cam) {
    // Match billboard size: compute screen pixels per world unit at this position,
    // then derive world units per sprite pixel
    float dist = Vector3Distance(worldPos, cam.position);
    float billboardScale = scale * 20.0f / (dist + 1.0f);  // screen px per sprite px
    Vector2 s0 = GetWorldToScreen(worldPos, cam);
    Vector2 s1 = GetWorldToScreen((Vector3){worldPos.x, worldPos.y + 1.0f, worldPos.z}, cam);
    float screenPerWorld = Vector2Distance(s0, s1);  // screen px per world unit
    if (screenPerWorld < 1.0f) screenPerWorld = 1.0f;
    float pixToWorld = billboardScale / screenPerWorld;  // world units per sprite px
    float cosR = cosf(rotY), sinR = sinf(rotY);

    // Find bottom extent so origin is at feet (same as billboard)
    float maxY = 0;
    for (int i = 0; i < count; i++) {
        float bottom = 0;
        switch (parts[i].type) {
            case SP_RECT:    bottom = parts[i].y + parts[i].h / 2; break;
            case SP_CIRCLE:  bottom = parts[i].y + parts[i].w; break;
            case SP_ELLIPSE: bottom = parts[i].y + parts[i].extra2; break;
            case SP_TRIANGLE: {
                bottom = parts[i].y;
                if (parts[i].h > bottom) bottom = parts[i].h;
                if (parts[i].extra2 > bottom) bottom = parts[i].extra2;
                break;
            }
            case SP_LINE:    bottom = parts[i].y > parts[i].h ? parts[i].y : parts[i].h; break;
            case SP_POLYGON: bottom = parts[i].y + parts[i].w; break;
        }
        if (bottom > maxY) maxY = bottom;
    }
    float yOff = maxY * pixToWorld;  // shift up so feet are at worldPos

    // Vertical plane: sprite X -> world XZ (rotated by rotY), sprite Y -> world Y
    #define S2W_X(sx, sy) (worldPos.x + (sx) * pixToWorld * cosR)
    #define S2W_Y(sx, sy) (worldPos.y + yOff - (sy) * pixToWorld)
    #define S2W_Z(sx, sy) (worldPos.z + (sx) * pixToWorld * sinR)

    for (int i = 0; i < count; i++) {
        Sprite2DPart *p = &parts[i];

        switch (p->type) {
            case SP_RECT: {
                float l = p->x - p->w*0.5f, r = p->x + p->w*0.5f;
                float t = p->y - p->h*0.5f, b = p->y + p->h*0.5f;
                Vector3 v0 = { S2W_X(l,t), S2W_Y(l,t), S2W_Z(l,t) };
                Vector3 v1 = { S2W_X(r,t), S2W_Y(r,t), S2W_Z(r,t) };
                Vector3 v2 = { S2W_X(r,b), S2W_Y(r,b), S2W_Z(r,b) };
                Vector3 v3 = { S2W_X(l,b), S2W_Y(l,b), S2W_Z(l,b) };
                DrawTriangle3D(v0, v1, v2, p->color);
                DrawTriangle3D(v0, v2, v3, p->color);
                DrawTriangle3D(v2, v1, v0, p->color);
                DrawTriangle3D(v3, v2, v0, p->color);
                break;
            }
            case SP_CIRCLE: {
                float rad = p->w * pixToWorld;
                float wcx = S2W_X(p->x, p->y), wcy = S2W_Y(p->x, p->y), wcz = S2W_Z(p->x, p->y);
                int segs = 16;
                for (int s = 0; s < segs; s++) {
                    float a0 = (float)s / segs * 2.0f * PI;
                    float a1 = (float)(s+1) / segs * 2.0f * PI;
                    Vector3 vc = {wcx, wcy, wcz};
                    Vector3 va = {wcx + cosf(a0)*rad*cosR, wcy + sinf(a0)*rad, wcz + cosf(a0)*rad*sinR};
                    Vector3 vb = {wcx + cosf(a1)*rad*cosR, wcy + sinf(a1)*rad, wcz + cosf(a1)*rad*sinR};
                    DrawTriangle3D(vc, va, vb, p->color);
                    DrawTriangle3D(vc, vb, va, p->color);
                }
                break;
            }
            case SP_ELLIPSE: {
                float rx = p->w * pixToWorld, ry = p->extra2 * pixToWorld;
                float wcx = S2W_X(p->x, p->y), wcy = S2W_Y(p->x, p->y), wcz = S2W_Z(p->x, p->y);
                int segs = 16;
                for (int s = 0; s < segs; s++) {
                    float a0 = (float)s / segs * 2.0f * PI;
                    float a1 = (float)(s+1) / segs * 2.0f * PI;
                    Vector3 vc = {wcx, wcy, wcz};
                    Vector3 va = {wcx + cosf(a0)*rx*cosR, wcy + sinf(a0)*ry, wcz + cosf(a0)*rx*sinR};
                    Vector3 vb = {wcx + cosf(a1)*rx*cosR, wcy + sinf(a1)*ry, wcz + cosf(a1)*rx*sinR};
                    DrawTriangle3D(vc, va, vb, p->color);
                    DrawTriangle3D(vc, vb, va, p->color);
                }
                break;
            }
            case SP_TRIANGLE: {
                Vector3 va = { S2W_X(p->x, p->y), S2W_Y(p->x, p->y), S2W_Z(p->x, p->y) };
                Vector3 vb = { S2W_X(p->w, p->h), S2W_Y(p->w, p->h), S2W_Z(p->w, p->h) };
                Vector3 vc = { S2W_X(p->extra1, p->extra2), S2W_Y(p->extra1, p->extra2), S2W_Z(p->extra1, p->extra2) };
                DrawTriangle3D(va, vb, vc, p->color);
                DrawTriangle3D(va, vc, vb, p->color);
                break;
            }
            case SP_LINE: {
                float thick = p->extra1 * pixToWorld * 0.5f;
                if (thick < 0.005f) thick = 0.005f;
                // Build a quad along the line with thickness in the vertical plane
                float dx = p->w - p->x, dy = p->h - p->y;
                float len = sqrtf(dx*dx + dy*dy);
                if (len < 0.001f) break;
                // Perpendicular in sprite space (rotated into vertical plane)
                float nx = -dy / len, ny = dx / len;
                float x0 = p->x + nx*p->extra1*0.5f, y0 = p->y + ny*p->extra1*0.5f;
                float x1 = p->x - nx*p->extra1*0.5f, y1 = p->y - ny*p->extra1*0.5f;
                float x2 = p->w - nx*p->extra1*0.5f, y2 = p->h - ny*p->extra1*0.5f;
                float x3 = p->w + nx*p->extra1*0.5f, y3 = p->h + ny*p->extra1*0.5f;
                Vector3 v0 = { S2W_X(x0,y0), S2W_Y(x0,y0), S2W_Z(x0,y0) };
                Vector3 v1 = { S2W_X(x1,y1), S2W_Y(x1,y1), S2W_Z(x1,y1) };
                Vector3 v2 = { S2W_X(x2,y2), S2W_Y(x2,y2), S2W_Z(x2,y2) };
                Vector3 v3 = { S2W_X(x3,y3), S2W_Y(x3,y3), S2W_Z(x3,y3) };
                DrawTriangle3D(v0, v1, v2, p->color);
                DrawTriangle3D(v0, v2, v3, p->color);
                DrawTriangle3D(v2, v1, v0, p->color);
                DrawTriangle3D(v3, v2, v0, p->color);
                break;
            }
            default: break;
        }
    }
    #undef S2W_X
    #undef S2W_Y
    #undef S2W_Z
}

// Get screen-space bounding box of a billboard sprite at a 3D position
static inline Rectangle GetSprite2DScreenRect(Sprite2DPart *parts, int count, Vector3 worldPos,
                                               float scale, Camera3D cam) {
    Vector2 screen = GetWorldToScreen(worldPos, cam);
    float dist = Vector3Distance(worldPos, cam.position);
    float distScale = 20.0f / (dist + 1.0f);
    float finalScale = scale * distScale;

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f, bottomY = 0;
    for (int i = 0; i < count; i++) {
        float l, r, t, b;
        switch (parts[i].type) {
            case SP_RECT:
                l = parts[i].x - parts[i].w/2; r = parts[i].x + parts[i].w/2;
                t = parts[i].y - parts[i].h/2; b = parts[i].y + parts[i].h/2;
                break;
            case SP_CIRCLE:
                l = parts[i].x - parts[i].w; r = parts[i].x + parts[i].w;
                t = parts[i].y - parts[i].w; b = parts[i].y + parts[i].w;
                break;
            case SP_ELLIPSE:
                l = parts[i].x - parts[i].w; r = parts[i].x + parts[i].w;
                t = parts[i].y - parts[i].extra2; b = parts[i].y + parts[i].extra2;
                break;
            case SP_TRIANGLE:
                l = fminf(fminf(parts[i].x, parts[i].w), parts[i].extra1);
                r = fmaxf(fmaxf(parts[i].x, parts[i].w), parts[i].extra1);
                t = fminf(fminf(parts[i].y, parts[i].h), parts[i].extra2);
                b = fmaxf(fmaxf(parts[i].y, parts[i].h), parts[i].extra2);
                break;
            case SP_LINE:
                l = fminf(parts[i].x, parts[i].w); r = fmaxf(parts[i].x, parts[i].w);
                t = fminf(parts[i].y, parts[i].h); b = fmaxf(parts[i].y, parts[i].h);
                break;
            case SP_POLYGON:
                l = parts[i].x - parts[i].w; r = parts[i].x + parts[i].w;
                t = parts[i].y - parts[i].w; b = parts[i].y + parts[i].w;
                break;
            default: l = r = t = b = 0; break;
        }
        if (l < minX) minX = l; if (r > maxX) maxX = r;
        if (t < minY) minY = t; if (b > maxY) maxY = b;
        if (b > bottomY) bottomY = b;
    }

    float offY = -bottomY * finalScale;
    return (Rectangle){
        screen.x + minX * finalScale,
        screen.y + offY + minY * finalScale,
        (maxX - minX) * finalScale,
        (maxY - minY) * finalScale
    };
}

#endif // SPRITES2D_H
