// map3d.h — Simple 3D tile-based map format for raylib prototypes
// Header-only: just #include this file
//
// Usage:
//   // Define tile types
//   TileDef defs[] = {
//       TILEDEF_FLOOR(GREEN),
//       TILEDEF_WALL(1.0, BROWN),
//       TILEDEF_WALL(2.0, GRAY),
//       TILEDEF_RAMP_N(1.0, BEIGE),
//   };
//
//   // Build map from string (each char maps to a tile index)
//   Map3D map = {0};
//   const char *layout =
//       "1111111111"
//       "1000000001"
//       "1002200001"
//       "1000000001"
//       "1111111111";
//   Map3DLoad(&map, layout, 10, 5, 2.0, defs, 4);
//   Map3DDrawAll(&map);

#ifndef MAP3D_H
#define MAP3D_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// --- Types ---

#define MAP3D_MAX_W    64
#define MAP3D_MAX_H    64
#define MAP3D_MAX_DEFS 36   // '0'-'9' + 'a'-'z'

typedef enum {
    TILE_EMPTY,     // nothing, void
    TILE_FLOOR,     // flat ground
    TILE_WALL,      // solid block, floor-to-height
    TILE_RAMP_N,    // ramp: high on north (z-), low on south (z+)
    TILE_RAMP_S,
    TILE_RAMP_E,    // high on east (x+)
    TILE_RAMP_W,
    TILE_WATER,     // floor with water effect
    TILE_PIT,       // hole/gap in the floor
    TILE_PLATFORM,  // raised flat platform
} TileType;

typedef struct {
    TileType type;
    float height;   // wall height, ramp height, platform height
    Color topColor;
    Color sideColor;
} TileDef;

typedef struct {
    int tiles[MAP3D_MAX_H][MAP3D_MAX_W];   // index into defs array
    int width, height;
    float tileSize;          // world units per tile
    TileDef defs[MAP3D_MAX_DEFS];
    int numDefs;
} Map3D;

// --- Shorthand macros for tile definitions ---

#define TILEDEF_EMPTY \
    { TILE_EMPTY, 0, BLACK, BLACK }

#define TILEDEF_FLOOR(col) \
    { TILE_FLOOR, 0, col, col }

#define TILEDEF_WALL(h, col) \
    { TILE_WALL, h, \
      col, \
      (Color){(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),255} }

#define TILEDEF_RAMP_N(h, col) { TILE_RAMP_N, h, col, (Color){(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),255} }
#define TILEDEF_RAMP_S(h, col) { TILE_RAMP_S, h, col, (Color){(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),255} }
#define TILEDEF_RAMP_E(h, col) { TILE_RAMP_E, h, col, (Color){(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),255} }
#define TILEDEF_RAMP_W(h, col) { TILE_RAMP_W, h, col, (Color){(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),255} }

#define TILEDEF_WATER(col) \
    { TILE_WATER, 0, col, col }

#define TILEDEF_PIT \
    { TILE_PIT, 0, (Color){20,20,25,255}, (Color){15,15,20,255} }

#define TILEDEF_PLATFORM(h, col) \
    { TILE_PLATFORM, h, col, (Color){(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),255} }

// --- API ---

// Load map from string: chars '0'-'9' map to defs[0]-defs[9], 'a'-'z' to defs[10]-defs[35]
static inline void Map3DLoad(Map3D *map, const char *layout, int w, int h,
                             float tileSize, TileDef *defs, int numDefs) {
    map->width = w;
    map->height = h;
    map->tileSize = tileSize;
    map->numDefs = numDefs;
    for (int i = 0; i < numDefs && i < MAP3D_MAX_DEFS; i++) map->defs[i] = defs[i];

    for (int y = 0; y < h && y < MAP3D_MAX_H; y++) {
        for (int x = 0; x < w && x < MAP3D_MAX_W; x++) {
            char c = layout[y * w + x];
            int idx = 0;
            if (c >= '0' && c <= '9') idx = c - '0';
            else if (c >= 'a' && c <= 'z') idx = 10 + (c - 'a');
            else if (c == ' ' || c == '.') idx = 0;
            map->tiles[y][x] = idx;
        }
    }
}

// Get tile def at grid position (bounds-checked)
static inline TileDef *Map3DGet(Map3D *map, int tx, int tz) {
    if (tx < 0 || tx >= map->width || tz < 0 || tz >= map->height)
        return &map->defs[0];
    return &map->defs[map->tiles[tz][tx]];
}

// Convert grid coords to world position (tile center, ground level)
static inline Vector3 Map3DToWorld(Map3D *map, int tx, int tz) {
    float s = map->tileSize;
    return (Vector3){ tx * s + s/2, 0, tz * s + s/2 };
}

// Convert world position to grid coords
static inline void Map3DFromWorld(Map3D *map, Vector3 pos, int *tx, int *tz) {
    *tx = (int)(pos.x / map->tileSize);
    *tz = (int)(pos.z / map->tileSize);
}

// Get the floor height at a world position (handles ramps)
static inline float Map3DHeightAt(Map3D *map, Vector3 pos) {
    int tx, tz;
    Map3DFromWorld(map, pos, &tx, &tz);
    TileDef *td = Map3DGet(map, tx, tz);
    float s = map->tileSize;

    // Fractional position within tile (0-1)
    float fx = (pos.x - tx * s) / s;
    float fz = (pos.z - tz * s) / s;
    fx = Clamp(fx, 0, 1);
    fz = Clamp(fz, 0, 1);

    switch (td->type) {
        case TILE_WALL:     return td->height;
        case TILE_PLATFORM: return td->height;
        case TILE_RAMP_N:   return td->height * fz;         // high at z=0, low at z=1
        case TILE_RAMP_S:   return td->height * (1.0f - fz); // high at z=1
        case TILE_RAMP_E:   return td->height * (1.0f - fx); // high at x=1
        case TILE_RAMP_W:   return td->height * fx;          // high at x=0
        case TILE_PIT:      return -1.0f;
        default:            return 0;
    }
}

// Check if a world position collides with a solid tile (wall)
static inline bool Map3DSolid(Map3D *map, Vector3 pos, float radius) {
    float s = map->tileSize;
    // Check the 4 grid cells the radius could touch
    int x0 = (int)((pos.x - radius) / s), x1 = (int)((pos.x + radius) / s);
    int z0 = (int)((pos.z - radius) / s), z1 = (int)((pos.z + radius) / s);
    for (int tz = z0; tz <= z1; tz++) {
        for (int tx = x0; tx <= x1; tx++) {
            TileDef *td = Map3DGet(map, tx, tz);
            if (td->type == TILE_WALL) return true;
        }
    }
    return false;
}

// Check if world position is out of map bounds
static inline bool Map3DOutOfBounds(Map3D *map, Vector3 pos) {
    return pos.x < 0 || pos.z < 0 ||
           pos.x >= map->width * map->tileSize ||
           pos.z >= map->height * map->tileSize;
}

// --- Rendering ---

// Helper: draw a quad from 4 world-space points (auto-fix winding)
static inline void Map3DQuad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    // Try both windings, raylib culls back-faces
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
    DrawTriangle3D(a, c, b, col);
    DrawTriangle3D(a, d, c, col);
}

// Draw a single tile
static inline void Map3DDrawTile(Map3D *map, int tx, int tz) {
    TileDef *td = Map3DGet(map, tx, tz);
    if (td->type == TILE_EMPTY) return;

    float s = map->tileSize;
    float x0 = tx * s, z0 = tz * s;
    float x1 = x0 + s, z1 = z0 + s;

    switch (td->type) {
        case TILE_FLOOR: {
            DrawPlane((Vector3){(x0+x1)/2, 0, (z0+z1)/2}, (Vector2){s, s}, td->topColor);
            break;
        }
        case TILE_WALL: {
            float h = td->height;
            Vector3 center = { (x0+x1)/2, h/2, (z0+z1)/2 };
            // Top face
            DrawPlane((Vector3){center.x, h, center.z}, (Vector2){s, s}, td->topColor);
            // Side faces (draw all 4, camera culling handles visibility)
            Map3DQuad((Vector3){x0,0,z0}, (Vector3){x1,0,z0}, (Vector3){x1,h,z0}, (Vector3){x0,h,z0}, td->sideColor);
            Map3DQuad((Vector3){x0,0,z1}, (Vector3){x0,h,z1}, (Vector3){x1,h,z1}, (Vector3){x1,0,z1}, td->sideColor);
            Map3DQuad((Vector3){x0,0,z0}, (Vector3){x0,h,z0}, (Vector3){x0,h,z1}, (Vector3){x0,0,z1}, td->sideColor);
            Map3DQuad((Vector3){x1,0,z0}, (Vector3){x1,0,z1}, (Vector3){x1,h,z1}, (Vector3){x1,h,z0}, td->sideColor);
            break;
        }
        case TILE_RAMP_N: case TILE_RAMP_S: case TILE_RAMP_E: case TILE_RAMP_W: {
            float h = td->height;
            // Corner heights
            float hNW, hNE, hSW, hSE;
            switch (td->type) {
                case TILE_RAMP_N: hNW = h; hNE = h; hSW = 0; hSE = 0; break;
                case TILE_RAMP_S: hNW = 0; hNE = 0; hSW = h; hSE = h; break;
                case TILE_RAMP_E: hNW = 0; hNE = h; hSW = 0; hSE = h; break;
                case TILE_RAMP_W: hNW = h; hNE = 0; hSW = h; hSE = 0; break;
                default: hNW = hNE = hSW = hSE = 0; break;
            }
            Vector3 nw = {x0, hNW, z0}, ne = {x1, hNE, z0};
            Vector3 sw = {x0, hSW, z1}, se = {x1, hSE, z1};
            // Slope surface
            Map3DQuad(nw, ne, se, sw, td->topColor);
            // Side walls where height > 0
            if (hNW > 0 || hNE > 0)
                Map3DQuad((Vector3){x0,0,z0}, (Vector3){x1,0,z0}, ne, nw, td->sideColor);
            if (hSW > 0 || hSE > 0)
                Map3DQuad((Vector3){x0,0,z1}, sw, se, (Vector3){x1,0,z1}, td->sideColor);
            if (hNW > 0 || hSW > 0)
                Map3DQuad((Vector3){x0,0,z0}, nw, sw, (Vector3){x0,0,z1}, td->sideColor);
            if (hNE > 0 || hSE > 0)
                Map3DQuad((Vector3){x1,0,z0}, (Vector3){x1,0,z1}, se, ne, td->sideColor);
            break;
        }
        case TILE_WATER: {
            float wave = sinf((float)GetTime() * 2.0f + (float)(tx + tz)) * 0.05f;
            DrawPlane((Vector3){(x0+x1)/2, wave, (z0+z1)/2}, (Vector2){s, s}, td->topColor);
            break;
        }
        case TILE_PIT: {
            float depth = -1.0f;
            DrawPlane((Vector3){(x0+x1)/2, depth, (z0+z1)/2}, (Vector2){s, s}, td->topColor);
            // Pit walls
            Map3DQuad((Vector3){x0,depth,z0}, (Vector3){x1,depth,z0}, (Vector3){x1,0,z0}, (Vector3){x0,0,z0}, td->sideColor);
            Map3DQuad((Vector3){x0,depth,z1}, (Vector3){x0,0,z1}, (Vector3){x1,0,z1}, (Vector3){x1,depth,z1}, td->sideColor);
            Map3DQuad((Vector3){x0,depth,z0}, (Vector3){x0,0,z0}, (Vector3){x0,0,z1}, (Vector3){x0,depth,z1}, td->sideColor);
            Map3DQuad((Vector3){x1,depth,z0}, (Vector3){x1,depth,z1}, (Vector3){x1,0,z1}, (Vector3){x1,0,z0}, td->sideColor);
            break;
        }
        case TILE_PLATFORM: {
            float h = td->height;
            DrawPlane((Vector3){(x0+x1)/2, h, (z0+z1)/2}, (Vector2){s, s}, td->topColor);
            // Sides
            Map3DQuad((Vector3){x0,0,z0}, (Vector3){x1,0,z0}, (Vector3){x1,h,z0}, (Vector3){x0,h,z0}, td->sideColor);
            Map3DQuad((Vector3){x0,0,z1}, (Vector3){x0,h,z1}, (Vector3){x1,h,z1}, (Vector3){x1,0,z1}, td->sideColor);
            Map3DQuad((Vector3){x0,0,z0}, (Vector3){x0,h,z0}, (Vector3){x0,h,z1}, (Vector3){x0,0,z1}, td->sideColor);
            Map3DQuad((Vector3){x1,0,z0}, (Vector3){x1,0,z1}, (Vector3){x1,h,z1}, (Vector3){x1,h,z0}, td->sideColor);
            break;
        }
        default: break;
    }
}

// Draw the entire map
static inline void Map3DDrawAll(Map3D *map) {
    for (int z = 0; z < map->height; z++)
        for (int x = 0; x < map->width; x++)
            Map3DDrawTile(map, x, z);
}

// Draw only tiles visible from a camera (frustum-approximated by range)
static inline void Map3DDrawNear(Map3D *map, Vector3 camPos, float range) {
    int cx, cz;
    Map3DFromWorld(map, camPos, &cx, &cz);
    int r = (int)(range / map->tileSize) + 1;
    for (int z = cz - r; z <= cz + r; z++)
        for (int x = cx - r; x <= cx + r; x++)
            if (x >= 0 && x < map->width && z >= 0 && z < map->height)
                Map3DDrawTile(map, x, z);
}

// Draw grid lines on the map floor (useful for debugging)
static inline void Map3DDrawGrid(Map3D *map, Color col) {
    float s = map->tileSize;
    for (int x = 0; x <= map->width; x++)
        DrawLine3D((Vector3){x*s, 0.02f, 0}, (Vector3){x*s, 0.02f, map->height*s}, col);
    for (int z = 0; z <= map->height; z++)
        DrawLine3D((Vector3){0, 0.02f, z*s}, (Vector3){map->width*s, 0.02f, z*s}, col);
}

// Draw a 2D minimap overlay
static inline void Map3DDraw2D(Map3D *map, int screenX, int screenY, int pixPerTile) {
    for (int z = 0; z < map->height; z++) {
        for (int x = 0; x < map->width; x++) {
            TileDef *td = Map3DGet(map, x, z);
            Color c;
            switch (td->type) {
                case TILE_EMPTY:    c = (Color){10,10,10,200}; break;
                case TILE_FLOOR:    c = td->topColor; c.a = 200; break;
                case TILE_WALL:     c = td->sideColor; c.a = 255; break;
                case TILE_WATER:    c = td->topColor; c.a = 200; break;
                case TILE_PIT:      c = (Color){20,20,20,200}; break;
                case TILE_PLATFORM: c = td->topColor; c.a = 230; break;
                default:            c = td->topColor; c.a = 200; break;
            }
            DrawRectangle(screenX + x * pixPerTile, screenY + z * pixPerTile,
                         pixPerTile, pixPerTile, c);
        }
    }
    DrawRectangleLines(screenX, screenY,
                       map->width * pixPerTile, map->height * pixPerTile,
                       (Color){80,80,80,255});
}

// --- File I/O ---
// Text format:
//   width height tileSize
//   row0chars
//   row1chars
//   ...

static inline bool SaveMap3D(const char *filename, Map3D *map) {
    FILE *f = fopen(filename, "w");
    if (!f) return false;
    fprintf(f, "%d %d %.1f\n", map->width, map->height, map->tileSize);
    for (int z = 0; z < map->height; z++) {
        for (int x = 0; x < map->width; x++) {
            int t = map->tiles[z][x];
            fprintf(f, "%c", t < 10 ? '0' + t : 'a' + (t - 10));
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

static inline bool LoadMap3DFile(const char *filename, Map3D *map, TileDef *defs, int numDefs) {
    FILE *f = fopen(filename, "r");
    if (!f) return false;
    int w, h;
    float ts;
    if (fscanf(f, "%d %d %f\n", &w, &h, &ts) != 3) { fclose(f); return false; }
    map->width = (w < MAP3D_MAX_W) ? w : MAP3D_MAX_W;
    map->height = (h < MAP3D_MAX_H) ? h : MAP3D_MAX_H;
    map->tileSize = ts;
    map->numDefs = numDefs;
    for (int i = 0; i < numDefs && i < MAP3D_MAX_DEFS; i++) map->defs[i] = defs[i];
    char line[MAP3D_MAX_W + 4];
    for (int z = 0; z < map->height; z++) {
        if (!fgets(line, sizeof(line), f)) break;
        for (int x = 0; x < map->width; x++) {
            char c = line[x];
            int idx = 0;
            if (c >= '0' && c <= '9') idx = c - '0';
            else if (c >= 'a' && c <= 'z') idx = 10 + (c - 'a');
            map->tiles[z][x] = idx;
        }
    }
    fclose(f);
    return true;
}

#endif // MAP3D_H
