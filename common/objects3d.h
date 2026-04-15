// objects3d.h — Simple 3D object format built from primitives
// Header-only: #define OBJECTS3D_IMPL in ONE .c file before including
//
// Usage:
//   Part parts[] = {
//       CUBE(0, 0.5, 0,   1, 1, 1,   RED),       // body
//       SPHERE(0, 1.3, 0,  0.3,        GOLD),      // head
//       CYL(0.4, 0.3, 0,  0.1, 0.8,   DARKGRAY),  // right arm
//   };
//   Object3D obj = OBJ(parts, pos, rotY);
//   DrawObject3D(&obj);

#ifndef OBJECTS3D_H
#define OBJECTS3D_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// --- Types ---

typedef enum {
    PART_CUBE,
    PART_SPHERE,
    PART_CYLINDER,
    PART_CONE,      // tapered cylinder: size = (bottomRadius, height, topRadius)
} PartType;

typedef struct {
    PartType type;
    Vector3 offset;   // position relative to object origin
    Vector3 size;     // cube: (w, h, d), sphere: (radius, -, -), cylinder: (radius, height, -), cone: (botR, height, topR)
    Color color;
    bool wireframe;   // draw wireframe overlay
} Part;

typedef struct {
    Part *parts;
    int count;
    Vector3 pos;      // world position
    float rotY;       // yaw rotation in radians
} Object3D;

// Helper: wraps a Color compound literal so commas don't break outer macros
// Use COL(r,g,b,a) instead of (Color){r,g,b,a} inside prefab/part macros
#define COL(r,g,b,a) ((Color){r,g,b,a})

// --- Shorthand macros for defining parts ---

// CUBE(offsetX, offsetY, offsetZ, width, height, depth, color)
#define CUBE(ox,oy,oz, w,h,d, col) \
    { PART_CUBE, {ox,oy,oz}, {w,h,d}, col, false }

#define CUBE_W(ox,oy,oz, w,h,d, col) \
    { PART_CUBE, {ox,oy,oz}, {w,h,d}, col, true }

// SPHERE(offsetX, offsetY, offsetZ, radius, color)
#define SPHERE(ox,oy,oz, r, col) \
    { PART_SPHERE, {ox,oy,oz}, {r,0,0}, col, false }

// CYL(offsetX, offsetY, offsetZ, radius, height, color)
// Cylinder grows upward from offset.y to offset.y + height
#define CYL(ox,oy,oz, r,h, col) \
    { PART_CYLINDER, {ox,oy,oz}, {r,h,0}, col, false }

// CONE(offsetX, offsetY, offsetZ, bottomRadius, height, topRadius, color)
// Tapered cylinder: grows upward, narrows from bottomRadius to topRadius
#define CONE(ox,oy,oz, br,h,tr, col) \
    { PART_CONE, {ox,oy,oz}, {br,h,tr}, col, false }

// Object from parts array, position, and Y rotation
#define OBJ(partsArr, position, rotation) \
    { partsArr, sizeof(partsArr)/sizeof(partsArr[0]), position, rotation }

// --- API ---

// Rotate a point around Y axis
static inline Vector3 RotateY(Vector3 v, float angle) {
    float cs = cosf(angle), sn = sinf(angle);
    return (Vector3){ v.x * cs - v.z * sn, v.y, v.x * sn + v.z * cs };
}

// Draw a rotated cube using 12 triangles (6 faces)
static inline void DrawCubeRotY(Vector3 center, float w, float h, float d, float rotY, Color col) {
    float hw = w/2, hh = h/2, hd = d/2;
    // 8 local corners, then rotate around Y and offset
    Vector3 corners[8] = {
        {-hw, -hh, -hd}, { hw, -hh, -hd}, { hw,  hh, -hd}, {-hw,  hh, -hd},
        {-hw, -hh,  hd}, { hw, -hh,  hd}, { hw,  hh,  hd}, {-hw,  hh,  hd},
    };
    for (int i = 0; i < 8; i++) {
        corners[i] = RotateY(corners[i], rotY);
        corners[i] = Vector3Add(corners[i], center);
    }
    Color dark = {(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),col.a};
    // Front (0,1,2,3) Back (5,4,7,6) Top (3,2,6,7) Bot (4,5,1,0) Left (4,0,3,7) Right (1,5,6,2)
    // Draw both windings so one always faces camera
    #define TRI2(a,b,c,color) DrawTriangle3D(a,b,c,color); DrawTriangle3D(a,c,b,color)
    TRI2(corners[0],corners[1],corners[2],col);  TRI2(corners[0],corners[2],corners[3],col);
    TRI2(corners[5],corners[4],corners[7],col);  TRI2(corners[5],corners[7],corners[6],col);
    TRI2(corners[3],corners[2],corners[6],dark);  TRI2(corners[3],corners[6],corners[7],dark);
    TRI2(corners[4],corners[5],corners[1],dark);  TRI2(corners[4],corners[1],corners[0],dark);
    TRI2(corners[4],corners[0],corners[3],dark);  TRI2(corners[4],corners[3],corners[7],dark);
    TRI2(corners[1],corners[5],corners[6],dark);  TRI2(corners[1],corners[6],corners[2],dark);
    #undef TRI2
}

// Draw a single part at a world position with rotation
static inline void DrawPart(Part *p, Vector3 origin, float rotY) {
    Vector3 local = RotateY(p->offset, rotY);
    Vector3 worldPos = Vector3Add(origin, local);

    switch (p->type) {
        case PART_CUBE:
            DrawCubeRotY(worldPos, p->size.x, p->size.y, p->size.z, rotY, p->color);
            if (p->wireframe) {
                // Wireframe as axis-aligned approximation (close enough)
                DrawCubeWires(worldPos, p->size.x, p->size.y, p->size.z,
                    (Color){ p->color.r/2, p->color.g/2, p->color.b/2, 255 });
            }
            break;
        case PART_SPHERE:
            DrawSphere(worldPos, p->size.x, p->color);
            if (p->wireframe) {
                Color wc = { p->color.r/2, p->color.g/2, p->color.b/2, 255 };
                DrawSphereWires(worldPos, p->size.x, 8, 8, wc);
            }
            break;
        case PART_CYLINDER: {
            Vector3 base = worldPos;
            Vector3 top = { worldPos.x, worldPos.y + p->size.y, worldPos.z };
            DrawCylinderEx(base, top, p->size.x, p->size.x, 8, p->color);
            if (p->wireframe)
                DrawCylinderWiresEx(base, top, p->size.x, p->size.x, 8,
                    (Color){ p->color.r/2, p->color.g/2, p->color.b/2, 255 });
            break;
        }
        case PART_CONE: {
            Vector3 base = worldPos;
            Vector3 top = { worldPos.x, worldPos.y + p->size.y, worldPos.z };
            DrawCylinderEx(base, top, p->size.x, p->size.z, 8, p->color);
            if (p->wireframe)
                DrawCylinderWiresEx(base, top, p->size.x, p->size.z, 8,
                    (Color){ p->color.r/2, p->color.g/2, p->color.b/2, 255 });
            break;
        }
    }
}

// Draw all parts of an object
static inline void DrawObject3D(Object3D *obj) {
    for (int i = 0; i < obj->count; i++) {
        DrawPart(&obj->parts[i], obj->pos, obj->rotY);
    }
}

// Draw with position/rotation override (useful for animation)
static inline void DrawObject3DAt(Object3D *obj, Vector3 pos, float rotY) {
    for (int i = 0; i < obj->count; i++) {
        DrawPart(&obj->parts[i], pos, rotY);
    }
}

// Draw a single part with per-axis scale applied
static inline void DrawPartScaled3(Part *p, Vector3 origin, float rotY, Vector3 scale) {
    Vector3 offset = { p->offset.x * scale.x, p->offset.y * scale.y, p->offset.z * scale.z };
    Vector3 local = RotateY(offset, rotY);
    Vector3 worldPos = Vector3Add(origin, local);

    switch (p->type) {
        case PART_CUBE: {
            float sx = p->size.x * scale.x, sy = p->size.y * scale.y, sz = p->size.z * scale.z;
            DrawCubeRotY(worldPos, sx, sy, sz, rotY, p->color);
            break;
        }
        case PART_SPHERE: {
            // Average scale for radius
            float avgS = (scale.x + scale.y + scale.z) / 3.0f;
            DrawSphere(worldPos, p->size.x * avgS, p->color);
            break;
        }
        case PART_CYLINDER: {
            float rx = p->size.x * (scale.x + scale.z) / 2.0f;
            float hy = p->size.y * scale.y;
            Vector3 top = { worldPos.x, worldPos.y + hy, worldPos.z };
            DrawCylinderEx(worldPos, top, rx, rx, 8, p->color);
            break;
        }
        case PART_CONE: {
            float rBot = p->size.x * (scale.x + scale.z) / 2.0f;
            float rTop = p->size.z * (scale.x + scale.z) / 2.0f;
            float hy = p->size.y * scale.y;
            Vector3 top = { worldPos.x, worldPos.y + hy, worldPos.z };
            DrawCylinderEx(worldPos, top, rBot, rTop, 8, p->color);
            break;
        }
    }
}

// Draw all parts with per-axis scale
static inline void DrawObject3DScaled(Part *parts, int count, Vector3 pos, float rotY, Vector3 scale) {
    for (int i = 0; i < count; i++) {
        DrawPartScaled3(&parts[i], pos, rotY, scale);
    }
}

// Draw object shadow (flattened silhouette on ground plane)
static inline void DrawObject3DShadow(Object3D *obj, float groundY) {
    for (int i = 0; i < obj->count; i++) {
        Part *p = &obj->parts[i];
        Vector3 local = RotateY(p->offset, obj->rotY);
        Vector3 worldPos = Vector3Add(obj->pos, local);
        Color shadowCol = { 0, 0, 0, 40 };

        switch (p->type) {
            case PART_CUBE:
                DrawCube((Vector3){worldPos.x, groundY + 0.01f, worldPos.z},
                         p->size.x, 0.01f, p->size.z, shadowCol);
                break;
            case PART_SPHERE:
                DrawCircle3D((Vector3){worldPos.x, groundY + 0.01f, worldPos.z},
                             p->size.x, (Vector3){1,0,0}, 90, shadowCol);
                break;
            case PART_CYLINDER:
            case PART_CONE:
                DrawCircle3D((Vector3){worldPos.x, groundY + 0.01f, worldPos.z},
                             p->size.x, (Vector3){1,0,0}, 90, shadowCol);
                break;
        }
    }
}

// --- Prefab library: common game objects ---
// Use these as starting points, copy and modify as needed.

// Human figure (~1.8 units tall)
//   Parts: legs, torso, head, arms
#define PREFAB_HUMAN(skinCol, shirtCol, pantsCol) { \
    CUBE(0, 0.35f, 0,  0.4f, 0.7f, 0.3f, pantsCol),  /* legs  0.0-0.7  */ \
    CUBE(0, 1.0f,  0,  0.5f, 0.6f, 0.3f, shirtCol),   /* torso 0.7-1.3  */ \
    SPHERE(0, 1.5f, 0,  0.2f, skinCol),                 /* head           */ \
    CYL(-0.35f, 0.7f, 0, 0.06f, 0.6f, shirtCol),       /* arm L          */ \
    CYL( 0.35f, 0.7f, 0, 0.06f, 0.6f, shirtCol),       /* arm R          */ \
}

// Simple car (~4 units long)
#define PREFAB_CAR(bodyCol) { \
    CUBE(0, 0.3f, 0,  1.2f, 0.5f, 2.0f, bodyCol),                  /* body    */ \
    CUBE(0, 0.65f, -0.2f, 0.9f, 0.4f, 1.0f, bodyCol),              /* cabin   */ \
    CUBE(0, 0.65f, -0.2f, 0.7f, 0.35f, 0.8f, COL(135,206,235,255)),/* window  */ \
    SPHERE(-0.5f, 0.15f, 0.8f,  0.15f, COL(80,80,80,255)),         /* wheel FL*/ \
    SPHERE( 0.5f, 0.15f, 0.8f,  0.15f, COL(80,80,80,255)),         /* wheel FR*/ \
    SPHERE(-0.5f, 0.15f,-0.8f,  0.15f, COL(80,80,80,255)),         /* wheel RL*/ \
    SPHERE( 0.5f, 0.15f,-0.8f,  0.15f, COL(80,80,80,255)),         /* wheel RR*/ \
}

// Tree (~3 units tall)
#define PREFAB_TREE { \
    CYL(0, 0, 0,  0.15f, 1.5f, COL(100,70,40,255)),     /* trunk */ \
    SPHERE(0, 2.2f, 0, 1.0f, COL(30,120,30,255)),        /* canopy */ \
    SPHERE(0, 2.8f, 0, 0.7f, COL(40,140,40,255)),        /* top    */ \
}

// Crate (~1 unit)
#define PREFAB_CRATE { \
    CUBE_W(0, 0.5f, 0,  1.0f, 1.0f, 1.0f, COL(160,120,60,255)), \
}

// Barrel
#define PREFAB_BARREL(col) { \
    CYL(0, 0, 0, 0.4f, 1.0f, col), \
}

// Bush (~1 unit tall, wide and round)
#define PREFAB_BUSH { \
    SPHERE(0, 0.5f, 0, 0.7f, COL(35,110,35,255)),              /* main   */ \
    SPHERE(0.4f, 0.4f, 0.3f, 0.5f, COL(40,125,40,255)),        /* clump R*/ \
    SPHERE(-0.3f, 0.4f, -0.3f, 0.55f, COL(30,100,30,255)),     /* clump L*/ \
    SPHERE(0, 0.8f, 0, 0.4f, COL(45,130,45,255)),               /* top    */ \
}

// Lamp post (~4 units tall)
#define PREFAB_LAMPPOST { \
    CYL(0, 0, 0, 0.08f, 3.5f, COL(80,80,80,255)),              /* pole  */ \
    CUBE(0, 3.6f, 0, 0.3f, 0.15f, 0.15f, COL(80,80,80,255)),   /* arm   */ \
    SPHERE(0.15f, 3.5f, 0, 0.12f, COL(255,255,0,255)),          /* light */ \
}

// --- File I/O ---
// Text format (one part per line):
//   # comment or object name
//   cube ox oy oz  w h d  r g b a
//   sphere ox oy oz  radius  r g b a
//   cyl ox oy oz  radius height  r g b a

// Save parts to a text file
static inline bool SaveObject3D(const char *filename, Part *parts, int count) {
    FILE *f = fopen(filename, "w");
    if (!f) return false;
    fprintf(f, "# object %d parts\n", count);
    for (int i = 0; i < count; i++) {
        Part *p = &parts[i];
        switch (p->type) {
            case PART_CUBE:
                fprintf(f, "cube %.3f %.3f %.3f  %.3f %.3f %.3f  %d %d %d %d\n",
                    p->offset.x, p->offset.y, p->offset.z,
                    p->size.x, p->size.y, p->size.z,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case PART_SPHERE:
                fprintf(f, "sphere %.3f %.3f %.3f  %.3f  %d %d %d %d\n",
                    p->offset.x, p->offset.y, p->offset.z,
                    p->size.x,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case PART_CYLINDER:
                fprintf(f, "cyl %.3f %.3f %.3f  %.3f %.3f  %d %d %d %d\n",
                    p->offset.x, p->offset.y, p->offset.z,
                    p->size.x, p->size.y,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
            case PART_CONE:
                fprintf(f, "cone %.3f %.3f %.3f  %.3f %.3f %.3f  %d %d %d %d\n",
                    p->offset.x, p->offset.y, p->offset.z,
                    p->size.x, p->size.y, p->size.z,
                    p->color.r, p->color.g, p->color.b, p->color.a);
                break;
        }
    }
    fclose(f);
    return true;
}

// Load parts from a text file. Returns number of parts loaded.
static inline int LoadObject3D(const char *filename, Part *parts, int maxParts) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && count < maxParts) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        Part *p = &parts[count];
        p->wireframe = false;
        int r, g, b, a;
        if (strncmp(line, "cube ", 5) == 0) {
            if (sscanf(line + 5, "%f %f %f %f %f %f %d %d %d %d",
                &p->offset.x, &p->offset.y, &p->offset.z,
                &p->size.x, &p->size.y, &p->size.z,
                &r, &g, &b, &a) == 10) {
                p->type = PART_CUBE;
                p->color = (Color){r, g, b, a};
                count++;
            }
        } else if (strncmp(line, "sphere ", 7) == 0) {
            if (sscanf(line + 7, "%f %f %f %f %d %d %d %d",
                &p->offset.x, &p->offset.y, &p->offset.z,
                &p->size.x,
                &r, &g, &b, &a) == 8) {
                p->type = PART_SPHERE;
                p->size.y = 0; p->size.z = 0;
                p->color = (Color){r, g, b, a};
                count++;
            }
        } else if (strncmp(line, "cyl ", 4) == 0) {
            if (sscanf(line + 4, "%f %f %f %f %f %d %d %d %d",
                &p->offset.x, &p->offset.y, &p->offset.z,
                &p->size.x, &p->size.y,
                &r, &g, &b, &a) == 9) {
                p->type = PART_CYLINDER;
                p->size.z = 0;
                p->color = (Color){r, g, b, a};
                count++;
            }
        } else if (strncmp(line, "cone ", 5) == 0) {
            if (sscanf(line + 5, "%f %f %f %f %f %f %d %d %d %d",
                &p->offset.x, &p->offset.y, &p->offset.z,
                &p->size.x, &p->size.y, &p->size.z,
                &r, &g, &b, &a) == 10) {
                p->type = PART_CONE;
                p->color = (Color){r, g, b, a};
                count++;
            }
        }
    }
    fclose(f);
    return count;
}

#endif // OBJECTS3D_H
