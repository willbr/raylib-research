#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "../common/objects3d.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Biplane flight sim: arcade flight with roll/pitch/yaw,
// rings to fly through, ground terrain, and clouds

#define NUM_RINGS     20
#define NUM_CLOUDS    30
#define NUM_TREES    200
#define NUM_BUSHES   120
#define NUM_BUILDINGS 25
#define NUM_HOUSES    15
#define WORLD_SIZE   500.0f

// Flight physics
#define THRUST_ACCEL   15.0f
#define MAX_SPEED      60.0f
#define MIN_SPEED       8.0f
#define DRAG            0.992f
#define PITCH_SPEED     1.8f
#define ROLL_SPEED      2.5f
#define YAW_SPEED       0.8f
#define ROLL_TO_YAW     0.6f   // banking turns
#define GRAVITY         5.0f
#define LIFT_FACTOR     0.4f

typedef struct {
    Vector3 pos;
    float pitch;    // radians, nose up/down
    float yaw;      // radians, heading
    float roll;     // radians, bank angle
    float speed;
    float throttle; // 0 to 1
    int ringsHit;
    bool crashed;
    float crashTimer;
    Vector3 crashPos;
} Plane;

// Debris from crash
#define MAX_DEBRIS 20
typedef struct {
    Vector3 pos;
    Vector3 vel;
    Vector3 rotVel;
    float pitch, yaw, roll;
    float size;
    Color color;
    PartType type;
    bool active;
} Debris;

// Explosion particles
#define MAX_EXPLOSION 60
typedef struct {
    Vector3 pos;
    Vector3 vel;
    float life, maxLife;
    float size;
    Color color;
    bool active;
} ExpParticle;

static Debris debris[MAX_DEBRIS];
static ExpParticle explosion[MAX_EXPLOSION];

// Forward declarations
Vector3 RotateVec(Vector3 v, float pitch, float yaw, float roll);

void SpawnCrash(Plane *p, Part *parts, int partCount) {
    p->crashPos = p->pos;

    // Turn each plane part into debris
    for (int i = 0; i < partCount && i < MAX_DEBRIS; i++) {
        Part *part = &parts[i];
        debris[i].pos = Vector3Add(p->pos, RotateVec(part->offset, p->pitch, p->yaw, p->roll));
        // Fly outward from crash center
        Vector3 dir = Vector3Subtract(debris[i].pos, p->pos);
        if (Vector3Length(dir) < 0.01f) dir = (Vector3){0, 1, 0};
        dir = Vector3Normalize(dir);
        float force = 5.0f + (float)GetRandomValue(0, 100) / 10.0f;
        debris[i].vel = Vector3Scale(dir, force);
        debris[i].vel.y += 3.0f + (float)GetRandomValue(0, 50) / 10.0f;
        debris[i].rotVel = (Vector3){
            (float)GetRandomValue(-50, 50) / 10.0f,
            (float)GetRandomValue(-50, 50) / 10.0f,
            (float)GetRandomValue(-50, 50) / 10.0f
        };
        debris[i].pitch = p->pitch;
        debris[i].yaw = p->yaw;
        debris[i].roll = p->roll;
        float maxSz = fmaxf(fmaxf(part->size.x, part->size.y), part->size.z);
        debris[i].size = (maxSz > 0.01f) ? maxSz : 0.2f;
        debris[i].color = part->color;
        debris[i].type = part->type;
        debris[i].active = true;
    }

    // Explosion particles
    for (int i = 0; i < MAX_EXPLOSION; i++) {
        float a1 = (float)GetRandomValue(0, 628) / 100.0f;
        float a2 = (float)GetRandomValue(-314, 314) / 200.0f;
        float spd = (float)GetRandomValue(3, 15);
        explosion[i].pos = p->pos;
        explosion[i].vel = (Vector3){
            cosf(a1) * cosf(a2) * spd,
            fabsf(sinf(a2)) * spd + 2.0f,
            sinf(a1) * cosf(a2) * spd
        };
        int roll2 = GetRandomValue(0, 2);
        if (roll2 == 0) explosion[i].color = (Color){255, 200, 50, 255};
        else if (roll2 == 1) explosion[i].color = (Color){255, 100, 0, 255};
        else explosion[i].color = (Color){80, 80, 80, 200};
        explosion[i].life = 0.5f + (float)GetRandomValue(0, 100) / 100.0f;
        explosion[i].maxLife = explosion[i].life;
        explosion[i].size = 0.1f + (float)GetRandomValue(0, 30) / 100.0f;
        explosion[i].active = true;
    }
}

typedef struct {
    Vector3 pos;
    float radius;
    float rotation;  // ring orientation yaw
    bool collected;
} Ring;

typedef struct {
    Vector3 pos;
    float size;
} Cloud;

typedef struct {
    Vector3 pos;
    float size;
} TreeObj;

typedef struct {
    Vector3 pos;
    float w, h, d;
    Color color;
} Building;

// Object models loaded from files (with hardcoded fallback)
#define MAX_OBJ_PARTS 32
static Part biplaneParts[MAX_OBJ_PARTS];
static int biplanePartCount = 0;
static Part treeParts[MAX_OBJ_PARTS];
static int treePartCount = 0;
static Part buildingParts[MAX_OBJ_PARTS];
static int buildingPartCount = 0;

// Hardcoded fallbacks (saved to files on first run)
static Part biplaneFallback[] = {
    CUBE(0, 0, 0,          0.3f, 0.3f, 1.8f, COL(180,50,40,255)),
    CUBE(0, 0.35f, 0.1f,   2.2f, 0.05f, 0.5f, COL(200,60,50,255)),
    CUBE(0, -0.2f, 0.1f,   2.2f, 0.05f, 0.5f, COL(200,60,50,255)),
    CYL(-0.6f, -0.2f, 0.1f,  0.02f, 0.55f, COL(100,80,40,255)),
    CYL( 0.6f, -0.2f, 0.1f,  0.02f, 0.55f, COL(100,80,40,255)),
    CYL(-0.3f, -0.2f, 0.1f,  0.02f, 0.55f, COL(100,80,40,255)),
    CYL( 0.3f, -0.2f, 0.1f,  0.02f, 0.55f, COL(100,80,40,255)),
    CUBE(0, 0.1f, -0.85f,  0.7f, 0.04f, 0.25f, COL(200,60,50,255)),
    CUBE(0, 0.25f, -0.85f, 0.04f, 0.25f, 0.25f, COL(200,60,50,255)),
    SPHERE(0, 0, 0.95f,    0.08f, COL(60,60,60,255)),
    CYL(0, 0, 0.7f,        0.18f, 0.25f, COL(80,80,90,255)),
    SPHERE(-0.25f, -0.4f, 0.3f,  0.08f, COL(30,30,30,255)),
    SPHERE( 0.25f, -0.4f, 0.3f,  0.08f, COL(30,30,30,255)),
    SPHERE(0, -0.15f, -0.8f,     0.05f, COL(30,30,30,255)),
    CUBE(0, 0.2f, 0.15f,   0.2f, 0.15f, 0.2f, COL(140,200,230,180)),
};
static Part treeFallback[] = {
    CYL(0, 0, 0,         0.12f, 1.5f, COL(90,65,35,255)),          // trunk
    CONE(0, 1.0f, 0,     1.0f, 1.0f, 0.05f, COL(30,80,30,255)),   // bottom canopy
    CONE(0, 1.6f, 0,     0.8f, 0.9f, 0.05f, COL(30,100,30,255)),  // middle canopy
    CONE(0, 2.2f, 0,     0.6f, 0.8f, 0.0f, COL(30,120,30,255)),   // top point
};
static Part buildingFallback[] = {
    CUBE(0, 0, 0,        5.0f, 10.0f, 5.0f, COL(140,130,120,255)),
    CUBE(0, 5.0f, 0,     5.2f, 0.3f, 5.2f, COL(100,90,80,255)),
};

// Load an object from file, falling back to hardcoded if missing
void LoadOrCreate(const char *path, Part *dest, int *destCount, Part *fallback, int fallbackCount) {
    *destCount = LoadObject3D(path, dest, MAX_OBJ_PARTS);
    if (*destCount == 0) {
        // Copy fallback and save file
        *destCount = fallbackCount;
        for (int i = 0; i < fallbackCount; i++) dest[i] = fallback[i];
        MakeDirectory("objects");
        SaveObject3D(path, dest, *destCount);
    }
}

static Plane plane;
static float clipNear = 0.1f;
static float clipFar = 2000.0f;
static bool showTweaks = false;
static Ring rings[NUM_RINGS];
static Cloud clouds[NUM_CLOUDS];
static TreeObj trees[NUM_TREES];
static TreeObj bushes[NUM_BUSHES];
static Building buildings[NUM_BUILDINGS];
static Building houses[NUM_HOUSES];

// Bush model
static Part bushParts[MAX_OBJ_PARTS];
static int bushPartCount = 0;
static Part bushFallback[] = {
    SPHERE(0, 0.4f, 0,        0.6f, COL(35,110,35,255)),
    SPHERE(0.3f, 0.35f, 0.2f, 0.45f, COL(40,125,40,255)),
    SPHERE(-0.2f, 0.35f,-0.2f,0.5f, COL(30,100,30,255)),
    SPHERE(0, 0.65f, 0,       0.35f, COL(45,130,45,255)),
};

// House model
static Part houseParts[MAX_OBJ_PARTS];
static int housePartCount = 0;
static Part houseFallback[] = {
    CUBE(0, 0, 0,              4.0f, 3.0f, 5.0f, COL(180,160,130,255)),     // walls
    CONE(0, 1.5f, 0,           3.0f, 2.5f, 0.0f, COL(160,60,40,255)),       // roof
    CUBE(0.5f, -0.5f, 2.5f,   0.8f, 1.5f, 0.1f, COL(100,70,40,255)),       // door
    CUBE(-0.8f, 0.3f, 2.5f,   0.6f, 0.5f, 0.1f, COL(140,200,230,200)),     // window L
    CUBE(1.5f, 0.3f, 2.5f,    0.6f, 0.5f, 0.1f, COL(140,200,230,200)),     // window R
    CYL(1.5f, 1.5f, -1.5f,    0.2f, 1.5f, COL(120,60,40,255)),             // chimney
};

// River: series of points defining the river center line
#define RIVER_SEGS 30
static Vector3 riverPts[RIVER_SEGS];

// Bridges where roads cross river
#define MAX_BRIDGES 6
typedef struct {
    Vector3 pos;
    float rotation;   // yaw angle of road at crossing
    float length;     // along road
    float width;      // road width
} Bridge;
static Bridge bridges[MAX_BRIDGES];
static int numBridges = 0;

// Roads
#define MAX_ROAD_PTS 80
#define MAX_ROADS     6
typedef struct {
    Vector3 pts[MAX_ROAD_PTS];
    int count;
    float width;
} Road;
static Road roads[MAX_ROADS];
static int numRoads = 0;

float TerrainHeight(float x, float z) {
    // Rolling hills
    float h = sinf(x * 0.02f) * 8.0f + cosf(z * 0.015f) * 6.0f;
    h += sinf(x * 0.05f + z * 0.03f) * 3.0f;
    if (h < 0) h = 0;
    return h;
}

void InitWorld(void) {
    // Load object models from files
    LoadOrCreate("objects/biplane.obj3d", biplaneParts, &biplanePartCount,
        biplaneFallback, sizeof(biplaneFallback)/sizeof(Part));
    LoadOrCreate("objects/pine_tree.obj3d", treeParts, &treePartCount,
        treeFallback, sizeof(treeFallback)/sizeof(Part));
    LoadOrCreate("objects/building.obj3d", buildingParts, &buildingPartCount,
        buildingFallback, sizeof(buildingFallback)/sizeof(Part));
    LoadOrCreate("objects/bush.obj3d", bushParts, &bushPartCount,
        bushFallback, sizeof(bushFallback)/sizeof(Part));
    LoadOrCreate("objects/house.obj3d", houseParts, &housePartCount,
        houseFallback, sizeof(houseFallback)/sizeof(Part));

    // Plane — start low, flying forward (+Z)
    plane = (Plane){
        .pos = {0, 15, 0},
        .yaw = 0,
        .speed = 20.0f,
        .throttle = 0.5f,
    };

    // Rings scattered in the sky
    for (int i = 0; i < NUM_RINGS; i++) {
        float angle = (float)i / NUM_RINGS * 2.0f * PI;
        float radius = 80.0f + (float)GetRandomValue(0, 150);
        rings[i].pos = (Vector3){
            cosf(angle) * radius,
            20.0f + (float)GetRandomValue(0, 40),
            sinf(angle) * radius
        };
        rings[i].radius = 6.0f + (float)GetRandomValue(0, 40) / 10.0f;
        rings[i].rotation = (float)GetRandomValue(0, 314) / 100.0f;
        rings[i].collected = false;
    }

    // Clouds
    for (int i = 0; i < NUM_CLOUDS; i++) {
        clouds[i].pos = (Vector3){
            (float)GetRandomValue(-400, 400),
            50.0f + (float)GetRandomValue(0, 40),
            (float)GetRandomValue(-400, 400)
        };
        clouds[i].size = 5.0f + (float)GetRandomValue(0, 80) / 10.0f;
    }

    // Trees
    for (int i = 0; i < NUM_TREES; i++) {
        float x = (float)GetRandomValue(-350, 350);
        float z = (float)GetRandomValue(-350, 350);
        trees[i].pos = (Vector3){ x, TerrainHeight(x, z), z };
        trees[i].size = 1.5f + (float)GetRandomValue(0, 20) / 10.0f;
    }

    // Bushes scattered everywhere
    for (int i = 0; i < NUM_BUSHES; i++) {
        float x = (float)GetRandomValue(-350, 350);
        float z = (float)GetRandomValue(-350, 350);
        bushes[i].pos = (Vector3){ x, TerrainHeight(x, z), z };
        bushes[i].size = 0.8f + (float)GetRandomValue(0, 15) / 10.0f;
    }

    // Buildings (city center)
    for (int i = 0; i < NUM_BUILDINGS; i++) {
        float x = 60.0f + (float)GetRandomValue(-50, 50);
        float z = 60.0f + (float)GetRandomValue(-50, 50);
        float h = 5.0f + (float)GetRandomValue(0, 20);
        buildings[i].pos = (Vector3){ x, TerrainHeight(x, z) + h/2, z };
        buildings[i].w = 3.0f + (float)GetRandomValue(0, 8);
        buildings[i].h = h;
        buildings[i].d = 3.0f + (float)GetRandomValue(0, 8);
        int shade = 140 + GetRandomValue(-30, 30);
        buildings[i].color = (Color){ shade, shade - 10, shade - 20, 255 };
    }

    // Houses (village, spread out)
    for (int i = 0; i < NUM_HOUSES; i++) {
        float x = -80.0f + (float)GetRandomValue(-60, 60);
        float z = -60.0f + (float)GetRandomValue(-60, 60);
        float h = 3.0f + (float)GetRandomValue(0, 3);
        houses[i].pos = (Vector3){ x, TerrainHeight(x, z) + h/2, z };
        houses[i].w = 4.0f + (float)GetRandomValue(0, 4);
        houses[i].h = h;
        houses[i].d = 5.0f + (float)GetRandomValue(0, 3);
        houses[i].color = WHITE; // not used, model has its own colors
    }

    // Roads: connect houses to buildings
    numRoads = 0;

    // Main road: from village center to city center
    {
        Road *rd = &roads[numRoads++];
        rd->width = 4.0f;
        float startX = -80, startZ = -60;  // village area center
        float endX = 60, endZ = 60;         // city area center
        rd->count = 30;
        for (int i = 0; i < rd->count; i++) {
            float t = (float)i / (rd->count - 1);
            float x = startX + t * (endX - startX);
            float z = startZ + t * (endZ - startZ);
            // Add slight curve
            x += sinf(t * PI) * 20.0f;
            rd->pts[i] = (Vector3){ x, TerrainHeight(x, z) + 0.08f, z };
        }
    }

    // Roads from each house to the nearest point on the main road
    for (int h = 0; h < NUM_HOUSES && numRoads < MAX_ROADS; h += 3) {
        Road *rd = &roads[numRoads++];
        rd->width = 2.5f;
        // Find nearest main road point
        float bestDist = 1e9f;
        int bestPt = 0;
        for (int p = 0; p < roads[0].count; p++) {
            float dx = houses[h].pos.x - roads[0].pts[p].x;
            float dz = houses[h].pos.z - roads[0].pts[p].z;
            float d = sqrtf(dx*dx + dz*dz);
            if (d < bestDist) { bestDist = d; bestPt = p; }
        }
        // Path from house to main road
        rd->count = 10;
        for (int i = 0; i < rd->count; i++) {
            float t = (float)i / (rd->count - 1);
            float x = houses[h].pos.x + t * (roads[0].pts[bestPt].x - houses[h].pos.x);
            float z = houses[h].pos.z + t * (roads[0].pts[bestPt].z - houses[h].pos.z);
            rd->pts[i] = (Vector3){ x, TerrainHeight(x, z) + 0.08f, z };
        }
    }

    // Smooth all roads
    for (int r = 0; r < numRoads; r++) {
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 1; i < roads[r].count - 1; i++) {
                roads[r].pts[i].x = (roads[r].pts[i-1].x + roads[r].pts[i].x * 2 + roads[r].pts[i+1].x) / 4.0f;
                roads[r].pts[i].z = (roads[r].pts[i-1].z + roads[r].pts[i].z * 2 + roads[r].pts[i+1].z) / 4.0f;
                roads[r].pts[i].y = TerrainHeight(roads[r].pts[i].x, roads[r].pts[i].z) + 0.08f;
            }
        }
    }

    // River: flows downhill, avoids hills and buildings
    // Start from one edge, step forward choosing the lowest nearby terrain
    {
        float rx = -200.0f, rz = -300.0f;
        for (int i = 0; i < RIVER_SEGS; i++) {
            // Sample terrain height in several directions and pick the lowest
            float bestX = rx, bestZ = rz + 20.0f;
            float bestH = TerrainHeight(bestX, bestZ);
            float stepZ = 600.0f / RIVER_SEGS;

            for (int s = -3; s <= 3; s++) {
                float testX = rx + s * 12.0f;
                float testZ = rz + stepZ;
                float h = TerrainHeight(testX, testZ);
                if (h < bestH) { bestH = h; bestX = testX; bestZ = testZ; }
            }

            // Smooth: don't jump too far sideways per step
            float maxDrift = 20.0f;
            if (bestX - rx > maxDrift) bestX = rx + maxDrift;
            if (bestX - rx < -maxDrift) bestX = rx - maxDrift;

            rx = bestX;
            rz = bestZ;

            // Push away from buildings and houses
            for (int pass = 0; pass < 3; pass++) {
                for (int b = 0; b < NUM_BUILDINGS; b++) {
                    float dx = rx - buildings[b].pos.x;
                    float dz2 = rz - buildings[b].pos.z;
                    float dist = sqrtf(dx * dx + dz2 * dz2);
                    float minDist = fmaxf(buildings[b].w, buildings[b].d) + 15.0f;
                    if (dist < minDist && dist > 0.1f) {
                        rx += dx / dist * (minDist - dist);
                    }
                }
                for (int h = 0; h < NUM_HOUSES; h++) {
                    float dx = rx - houses[h].pos.x;
                    float dz2 = rz - houses[h].pos.z;
                    float dist = sqrtf(dx * dx + dz2 * dz2);
                    float minDist = fmaxf(houses[h].w, houses[h].d) + 12.0f;
                    if (dist < minDist && dist > 0.1f) {
                        rx += dx / dist * (minDist - dist);
                    }
                }
            }

            riverPts[i] = (Vector3){ rx, TerrainHeight(rx, rz) + 0.1f, rz };
        }

        // Smooth the river path
        for (int pass = 0; pass < 3; pass++) {
            for (int i = 1; i < RIVER_SEGS - 1; i++) {
                riverPts[i].x = (riverPts[i-1].x + riverPts[i].x * 2 + riverPts[i+1].x) / 4.0f;
                riverPts[i].y = TerrainHeight(riverPts[i].x, riverPts[i].z) + 0.1f;
            }
        }
    }

    // Detect bridges: where roads cross the river
    numBridges = 0;
    for (int r = 0; r < numRoads && numBridges < MAX_BRIDGES; r++) {
        Road *rd = &roads[r];
        for (int i = 0; i < rd->count - 1 && numBridges < MAX_BRIDGES; i++) {
            for (int j = 0; j < RIVER_SEGS - 1; j++) {
                // Check if road segment and river segment are close
                Vector3 roadMid = Vector3Lerp(rd->pts[i], rd->pts[i+1], 0.5f);
                Vector3 riverMid = Vector3Lerp(riverPts[j], riverPts[j+1], 0.5f);
                float dx = roadMid.x - riverMid.x;
                float dz = roadMid.z - riverMid.z;
                float dist = sqrtf(dx*dx + dz*dz);
                if (dist < 8.0f) {
                    // Found a crossing
                    Vector3 roadDir = Vector3Subtract(rd->pts[i+1], rd->pts[i]);
                    bridges[numBridges].pos = roadMid;
                    bridges[numBridges].rotation = atan2f(roadDir.x, roadDir.z);
                    bridges[numBridges].length = 14.0f;
                    bridges[numBridges].width = rd->width + 1.0f;
                    numBridges++;

                    // Raise road points near the bridge
                    float bridgeHeight = 1.5f;
                    for (int k = 0; k < rd->count; k++) {
                        float bdx = rd->pts[k].x - roadMid.x;
                        float bdz = rd->pts[k].z - roadMid.z;
                        float bdist = sqrtf(bdx*bdx + bdz*bdz);
                        if (bdist < 12.0f) {
                            float lift = (1.0f - bdist / 12.0f) * bridgeHeight;
                            rd->pts[k].y += lift;
                        }
                    }
                    break; // one bridge per river crossing per road
                }
            }
        }
    }
}

// Rotate vector by pitch, yaw, roll (in that order)
Vector3 RotateVec(Vector3 v, float pitch, float yaw, float roll) {
    // Roll (around Z)
    float cr = cosf(roll), sr = sinf(roll);
    Vector3 r1 = { v.x * cr - v.y * sr, v.x * sr + v.y * cr, v.z };
    // Pitch (around X)
    float cp = cosf(pitch), sp = sinf(pitch);
    Vector3 r2 = { r1.x, r1.y * cp - r1.z * sp, r1.y * sp + r1.z * cp };
    // Yaw (around Y)
    float cy = cosf(yaw), sy = sinf(yaw);
    return (Vector3){ r2.x * cy + r2.z * sy, r2.y, -r2.x * sy + r2.z * cy };
}

// Draw a cube with full 3D rotation (pitch/yaw/roll)
void DrawCubeRotated(Vector3 center, float w, float h, float d, float pitch, float yaw, float roll, Color col) {
    float hw = w/2, hh = h/2, hd = d/2;
    Vector3 corners[8] = {
        {-hw,-hh,-hd}, { hw,-hh,-hd}, { hw, hh,-hd}, {-hw, hh,-hd},
        {-hw,-hh, hd}, { hw,-hh, hd}, { hw, hh, hd}, {-hw, hh, hd},
    };
    for (int c = 0; c < 8; c++) {
        corners[c] = Vector3Add(center, RotateVec(corners[c], pitch, yaw, roll));
    }
    Color dark = {(unsigned char)(col.r*0.7f),(unsigned char)(col.g*0.7f),(unsigned char)(col.b*0.7f),col.a};
    #define T2(a,b,c,color) DrawTriangle3D(a,b,c,color); DrawTriangle3D(a,c,b,color)
    T2(corners[0],corners[1],corners[2],col);  T2(corners[0],corners[2],corners[3],col);
    T2(corners[5],corners[4],corners[7],col);  T2(corners[5],corners[7],corners[6],col);
    T2(corners[3],corners[2],corners[6],dark);  T2(corners[3],corners[6],corners[7],dark);
    T2(corners[4],corners[5],corners[1],dark);  T2(corners[4],corners[1],corners[0],dark);
    T2(corners[4],corners[0],corners[3],dark);  T2(corners[4],corners[3],corners[7],dark);
    T2(corners[1],corners[5],corners[6],dark);  T2(corners[1],corners[6],corners[2],dark);
    #undef T2
}

void DrawBiplane(Plane *p) {
    for (int i = 0; i < biplanePartCount; i++) {
        Part *part = &biplaneParts[i];
        Vector3 rotOffset = RotateVec(part->offset, p->pitch, p->yaw, p->roll);
        Vector3 worldPos = Vector3Add(p->pos, rotOffset);

        switch (part->type) {
            case PART_CUBE: {
                DrawCubeRotated(worldPos, part->size.x, part->size.y, part->size.z,
                    p->pitch, p->yaw, p->roll, part->color);
                break;
            }
            case PART_SPHERE:
                DrawSphere(worldPos, part->size.x, part->color);
                break;
            case PART_CYLINDER: {
                Vector3 topOffset = { part->offset.x, part->offset.y + part->size.y, part->offset.z };
                Vector3 top = Vector3Add(p->pos, RotateVec(topOffset, p->pitch, p->yaw, p->roll));
                DrawCylinderEx(worldPos, top, part->size.x, part->size.x, 6, part->color);
                break;
            }
            case PART_CONE: {
                Vector3 topOffset = { part->offset.x, part->offset.y + part->size.y, part->offset.z };
                Vector3 top = Vector3Add(p->pos, RotateVec(topOffset, p->pitch, p->yaw, p->roll));
                DrawCylinderEx(worldPos, top, part->size.x, part->size.z, 8, part->color);
                break;
            }
        }
    }

    // Spinning propeller (visual only)
    float propAngle = (float)GetTime() * 30.0f;
    Vector3 propCenter = Vector3Add(p->pos, RotateVec((Vector3){0, 0, 1.0f}, p->pitch, p->yaw, p->roll));
    for (int b = 0; b < 2; b++) {
        float a = propAngle + b * PI;
        Vector3 bladeOffset = { cosf(a) * 0.5f, sinf(a) * 0.5f, 0 };
        Vector3 bladeWorld = Vector3Add(propCenter, RotateVec(bladeOffset, p->pitch, p->yaw, p->roll));
        DrawLine3D(propCenter, bladeWorld, (Color){200, 180, 140, 200});
    }

    // Shadow on terrain
    float groundY = TerrainHeight(p->pos.x, p->pos.z);
    if (p->pos.y < groundY + 80) {
        float shadowAlpha = Clamp(1.0f - (p->pos.y - groundY) / 80.0f, 0, 0.4f);
        DrawCircle3D((Vector3){p->pos.x, groundY + 0.1f, p->pos.z},
            1.5f, (Vector3){1,0,0}, 90, (Color){0, 0, 0, (unsigned char)(shadowAlpha * 255)});
    }
}

void DrawFlyRing(Ring *r) {
    if (r->collected) return;
    int segs = 16;
    float tubeR = 0.5f;
    float cy = cosf(r->rotation), sy = sinf(r->rotation);

    float pulse = 0.7f + sinf((float)GetTime() * 4.0f) * 0.3f;

    // Draw spheres along the ring to make a thick visible torus
    for (int s = 0; s < segs; s++) {
        float a = (float)s / segs * 2.0f * PI;
        float lx = cosf(a) * r->radius;
        float ly = sinf(a) * r->radius;

        // Rotate local point around Y by ring yaw
        Vector3 pt = {
            lx * cy + r->pos.x,
            ly + r->pos.y,
            -lx * sy + r->pos.z
        };

        Color col = (s % 2 == 0) ?
            (Color){255, (unsigned char)(200 * pulse), 0, 255} :
            (Color){255, 255, (unsigned char)(50 * pulse), 255};
        DrawSphere(pt, tubeR, col);
    }
}

void DrawCloud(Cloud *c) {
    DrawSphere(c->pos, c->size, (Color){240, 240, 250, 180});
    DrawSphere((Vector3){c->pos.x + c->size * 0.5f, c->pos.y - c->size * 0.1f, c->pos.z + c->size * 0.3f},
        c->size * 0.7f, (Color){235, 235, 245, 160});
    DrawSphere((Vector3){c->pos.x - c->size * 0.4f, c->pos.y + c->size * 0.1f, c->pos.z - c->size * 0.2f},
        c->size * 0.6f, (Color){230, 230, 240, 150});
}

void DrawTerrain(Vector3 camPos) {
    float step = 10.0f;
    float range = 500.0f;
    float x0 = floorf((camPos.x - range) / step) * step;
    float z0 = floorf((camPos.z - range) / step) * step;

    for (float z = z0; z < camPos.z + range; z += step) {
        for (float x = x0; x < camPos.x + range; x += step) {
            float h00 = TerrainHeight(x, z);
            float h10 = TerrainHeight(x + step, z);
            float h01 = TerrainHeight(x, z + step);
            float h11 = TerrainHeight(x + step, z + step);

            // Green with height variation
            int g = 100 + (int)(h00 * 3);
            if (g > 180) g = 180;
            Color col = { 40, g, 30, 255 };

            // Draw both windings so terrain visible from above and below
            Vector3 v00 = {x, h00, z}, v10 = {x+step, h10, z};
            Vector3 v01 = {x, h01, z+step}, v11 = {x+step, h11, z+step};
            // Slightly offset the second winding to avoid z-fighting
            Vector3 v00b = {x, h00 - 0.01f, z}, v10b = {x+step, h10 - 0.01f, z};
            Vector3 v01b = {x, h01 - 0.01f, z+step}, v11b = {x+step, h11 - 0.01f, z+step};
            DrawTriangle3D(v00, v10, v01, col);
            DrawTriangle3D(v10, v11, v01, col);
            DrawTriangle3D(v00b, v01b, v10b, col);
            DrawTriangle3D(v10b, v01b, v11b, col);
        }
    }
}

void DrawTree3D(TreeObj *t) {
    Vector3 scale = { t->size, t->size, t->size };
    DrawObject3DScaled(treeParts, treePartCount, t->pos, 0, scale);
}

int main(void) {
    InitWindow(800, 600, "Biplane Flight Sim");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();
    DisableCursor();

    InitWorld();

    Camera3D camera = { 0 };
    // Start camera behind the plane (plane faces +Z initially)
    Vector3 initFwd = RotateVec((Vector3){0, 0, 1}, plane.pitch, plane.yaw, plane.roll);
    camera.position = Vector3Subtract(plane.pos, Vector3Scale(initFwd, 6));
    camera.position.y = plane.pos.y + 2;
    camera.target = Vector3Add(plane.pos, Vector3Scale(initFwd, 5));
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 60;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // --- Plane controls ---
        if (!plane.crashed) {
            // Throttle (Shift/Ctrl)
            if (IsKeyDown(KEY_LEFT_SHIFT))   plane.throttle += 0.8f * dt;
            if (IsKeyDown(KEY_LEFT_CONTROL)) plane.throttle -= 0.8f * dt;
            plane.throttle = Clamp(plane.throttle, 0, 1);

            // Mouse look: pitch and roll (only when cursor locked)
            if (!showTweaks) {
                Vector2 mouseDelta = GetMouseDelta();
                plane.pitch -= mouseDelta.y * 0.002f;
                plane.roll += mouseDelta.x * 0.003f;
            }

            // WASD: pitch and roll
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    plane.pitch += PITCH_SPEED * dt;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  plane.pitch -= PITCH_SPEED * dt;
            if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT))  plane.roll -= ROLL_SPEED * dt;
            if (IsKeyDown(KEY_E) || IsKeyDown(KEY_RIGHT)) plane.roll += ROLL_SPEED * dt;

            // Yaw from rudder (A/D)
            if (IsKeyDown(KEY_A)) plane.yaw += YAW_SPEED * dt;
            if (IsKeyDown(KEY_D)) plane.yaw -= YAW_SPEED * dt;

            // Roll-induced yaw (banking turns)
            plane.yaw -= sinf(plane.roll) * ROLL_TO_YAW * dt;

            // Roll auto-level (gentle)
            plane.roll *= (1.0f - 0.3f * dt);

            // Pitch clamp
            plane.pitch = Clamp(plane.pitch, -PI/2.5f, PI/2.5f);

            // Speed from throttle
            float targetSpeed = MIN_SPEED + plane.throttle * (MAX_SPEED - MIN_SPEED);
            plane.speed += (targetSpeed - plane.speed) * 2.0f * dt;
            plane.speed *= DRAG;

            // Forward vector from orientation
            Vector3 forward = RotateVec((Vector3){0, 0, 1}, plane.pitch, plane.yaw, plane.roll);

            // Move
            plane.pos = Vector3Add(plane.pos, Vector3Scale(forward, plane.speed * dt));

            // Gravity vs lift
            float lift = plane.speed * LIFT_FACTOR * cosf(plane.roll) * cosf(plane.pitch);
            plane.pos.y += (lift - GRAVITY) * dt;

            // Ground collision
            float groundY = TerrainHeight(plane.pos.x, plane.pos.z);
            if (plane.pos.y < groundY + 0.5f) {
                if (plane.speed > 15 || fabsf(plane.pitch) > 0.3f || fabsf(plane.roll) > 0.5f) {
                    plane.crashed = true;
                    plane.crashTimer = 0;
                    SpawnCrash(&plane, biplaneParts, biplanePartCount);
                } else {
                    plane.pos.y = groundY + 0.5f;
                    plane.speed *= 0.98f;
                }
            }

            // Ceiling
            if (plane.pos.y > 120) plane.pos.y = 120;

            // Ring collection
            for (int i = 0; i < NUM_RINGS; i++) {
                if (rings[i].collected) continue;
                if (Vector3Distance(plane.pos, rings[i].pos) < rings[i].radius + 2.0f) {
                    rings[i].collected = true;
                    plane.ringsHit++;
                }
            }

            // Building collision
            for (int i = 0; i < NUM_BUILDINGS; i++) {
                Building *b = &buildings[i];
                if (fabsf(plane.pos.x - b->pos.x) < b->w/2 + 0.5f &&
                    fabsf(plane.pos.z - b->pos.z) < b->d/2 + 0.5f &&
                    plane.pos.y < b->pos.y + b->h/2) {
                    plane.crashed = true;
                    plane.crashTimer = 0;
                    SpawnCrash(&plane, biplaneParts, biplanePartCount);
                }
            }
        } else {
            plane.crashTimer += dt;

            // Update debris physics
            for (int i = 0; i < MAX_DEBRIS; i++) {
                if (!debris[i].active) continue;
                debris[i].vel.y -= 12.0f * dt;  // gravity
                debris[i].pos = Vector3Add(debris[i].pos, Vector3Scale(debris[i].vel, dt));
                debris[i].pitch += debris[i].rotVel.x * dt;
                debris[i].yaw += debris[i].rotVel.y * dt;
                debris[i].roll += debris[i].rotVel.z * dt;
                // Ground bounce
                float gY = TerrainHeight(debris[i].pos.x, debris[i].pos.z);
                if (debris[i].pos.y < gY + 0.1f) {
                    debris[i].pos.y = gY + 0.1f;
                    debris[i].vel.y = -debris[i].vel.y * 0.3f;
                    debris[i].vel.x *= 0.8f;
                    debris[i].vel.z *= 0.8f;
                    debris[i].rotVel = Vector3Scale(debris[i].rotVel, 0.7f);
                    if (fabsf(debris[i].vel.y) < 0.5f) {
                        debris[i].vel = (Vector3){0};
                        debris[i].rotVel = (Vector3){0};
                    }
                }
            }

            // Update explosion particles
            for (int i = 0; i < MAX_EXPLOSION; i++) {
                if (!explosion[i].active) continue;
                explosion[i].vel.y -= 5.0f * dt;
                explosion[i].pos = Vector3Add(explosion[i].pos, Vector3Scale(explosion[i].vel, dt));
                explosion[i].life -= dt;
                if (explosion[i].life <= 0) explosion[i].active = false;
            }

            // Restart
            if (IsKeyPressed(KEY_R)) {
                memset(debris, 0, sizeof(debris));
                memset(explosion, 0, sizeof(explosion));
                InitWorld();
            }
        }

        // --- Camera: chase behind and above ---
        {
            Vector3 forward = RotateVec((Vector3){0, 0, 1}, plane.pitch, plane.yaw, plane.roll);
            Vector3 up = RotateVec((Vector3){0, 1, 0}, plane.pitch, plane.yaw, plane.roll);

            float camDist = 6.0f;
            float camH = 2.0f;
            Vector3 camBehind = Vector3Subtract(plane.pos, Vector3Scale(forward, camDist));
            camBehind = Vector3Add(camBehind, Vector3Scale(up, camH));

            Vector3 camTarget = Vector3Add(plane.pos, Vector3Scale(forward, 5.0f));

            camera.position = Vector3Lerp(camera.position, camBehind, 5.0f * dt);
            camera.target = Vector3Lerp(camera.target, camTarget, 6.0f * dt);
            // Keep camera up as world up to avoid flipping
            camera.up = (Vector3){0, 1, 0};

            // Speed FOV
            float speedPct = Clamp(plane.speed / MAX_SPEED, 0, 1);
            float targetFov = 55.0f + speedPct * 20.0f;
            camera.fovy += (targetFov - camera.fovy) * 3.0f * dt;
        }

        // --- Draw ---
        BeginDrawing();
        // Sky gradient
        ClearBackground((Color){140, 180, 220, 255});
        DrawRectangleGradientV(0, 0, sw, sh, (Color){80, 140, 210, 255}, (Color){180, 215, 245, 255});

        // Sun position (far away, fixed in sky)
        Vector3 sunDir = Vector3Normalize((Vector3){0.6f, 0.25f, 0.4f});
        Vector3 sunPos = Vector3Add(camera.position, Vector3Scale(sunDir, 800.0f));

        // Set custom clip planes
        rlSetClipPlanes(clipNear, clipFar);
        BeginMode3D(camera);
            // Ground
            DrawPlane((Vector3){0, 0, 0}, (Vector2){500, 500}, (Color){40, 100, 30, 255});
            DrawGrid(50, 10.0f);

            // Sun (bright sphere in sky)
            DrawSphere(sunPos, 30.0f, (Color){255, 250, 220, 255});
            DrawSphere(sunPos, 35.0f, (Color){255, 240, 180, 80});

            DrawTerrain(camera.position);

            // Roads
            for (int r = 0; r < numRoads; r++) {
                Road *rd = &roads[r];
                for (int i = 0; i < rd->count - 1; i++) {
                    Vector3 p0 = rd->pts[i], p1 = rd->pts[i + 1];
                    Vector3 dir = Vector3Subtract(p1, p0);
                    Vector3 norm = { -dir.z, 0, dir.x };
                    float len = Vector3Length(norm);
                    if (len > 0.01f) norm = Vector3Scale(norm, rd->width / (2.0f * len));

                    Vector3 a = Vector3Add(p0, norm), b = Vector3Subtract(p0, norm);
                    Vector3 c = Vector3Add(p1, norm), d = Vector3Subtract(p1, norm);
                    a.y = p0.y; b.y = p0.y; c.y = p1.y; d.y = p1.y;

                    Color roadCol = (i % 2 == 0) ? (Color){70,70,75,255} : (Color){65,65,70,255};
                    DrawTriangle3D(a, b, c, roadCol);
                    DrawTriangle3D(b, d, c, roadCol);
                    DrawTriangle3D(a, c, b, roadCol);
                    DrawTriangle3D(b, c, d, roadCol);

                    // Center dashes on main road
                    if (r == 0 && i % 3 == 0) {
                        Vector3 cl0 = Vector3Lerp(a, b, 0.5f);
                        Vector3 cl1 = Vector3Lerp(c, d, 0.5f);
                        cl0.y += 0.02f; cl1.y += 0.02f;
                        DrawLine3D(cl0, cl1, YELLOW);
                    }
                    // Edge lines
                    if (r == 0) {
                        a.y += 0.02f; b.y += 0.02f; c.y += 0.02f; d.y += 0.02f;
                        DrawLine3D(a, c, WHITE);
                        DrawLine3D(b, d, WHITE);
                    }
                }
            }

            // Bridges
            for (int b = 0; b < numBridges; b++) {
                Bridge *br = &bridges[b];
                float cs = cosf(br->rotation), sn = sinf(br->rotation);
                float hl = br->length / 2.0f, hw = br->width / 2.0f;
                float bh = br->pos.y;

                // Bridge deck (raised platform)
                Vector3 fl = { br->pos.x - sn*hl - cs*hw, bh, br->pos.z - cs*hl + sn*hw };
                Vector3 fr = { br->pos.x - sn*hl + cs*hw, bh, br->pos.z - cs*hl - sn*hw };
                Vector3 bl = { br->pos.x + sn*hl - cs*hw, bh, br->pos.z + cs*hl + sn*hw };
                Vector3 bkr = { br->pos.x + sn*hl + cs*hw, bh, br->pos.z + cs*hl - sn*hw };

                Color deckCol = { 90, 80, 70, 255 };
                DrawTriangle3D(fl, fr, bl, deckCol);
                DrawTriangle3D(fr, bkr, bl, deckCol);
                DrawTriangle3D(fl, bl, fr, deckCol);
                DrawTriangle3D(fr, bl, bkr, deckCol);

                // Support pillars (underneath)
                Color pillarCol = { 100, 90, 75, 255 };
                float pillarH = 2.0f;
                Vector3 groundLevel;
                for (int p = -1; p <= 1; p += 2) {
                    float px = br->pos.x + sn * (hl * 0.6f * p);
                    float pz = br->pos.z + cs * (hl * 0.6f * p);
                    float gy = TerrainHeight(px, pz);
                    DrawCylinderEx(
                        (Vector3){px - cs*hw*0.8f, gy, pz + sn*hw*0.8f},
                        (Vector3){px - cs*hw*0.8f, bh, pz + sn*hw*0.8f},
                        0.2f, 0.2f, 6, pillarCol);
                    DrawCylinderEx(
                        (Vector3){px + cs*hw*0.8f, gy, pz - sn*hw*0.8f},
                        (Vector3){px + cs*hw*0.8f, bh, pz - sn*hw*0.8f},
                        0.2f, 0.2f, 6, pillarCol);
                }

                // Railings
                Color railCol = { 140, 130, 110, 255 };
                float railH = 1.0f;
                // Left railing
                DrawLine3D((Vector3){fl.x, fl.y + railH, fl.z}, (Vector3){bl.x, bl.y + railH, bl.z}, railCol);
                DrawLine3D(fl, (Vector3){fl.x, fl.y + railH, fl.z}, railCol);
                DrawLine3D(bl, (Vector3){bl.x, bl.y + railH, bl.z}, railCol);
                Vector3 midL = Vector3Lerp(fl, bl, 0.5f);
                DrawLine3D(midL, (Vector3){midL.x, midL.y + railH, midL.z}, railCol);
                // Right railing
                DrawLine3D((Vector3){fr.x, fr.y + railH, fr.z}, (Vector3){bkr.x, bkr.y + railH, bkr.z}, railCol);
                DrawLine3D(fr, (Vector3){fr.x, fr.y + railH, fr.z}, railCol);
                DrawLine3D(bkr, (Vector3){bkr.x, bkr.y + railH, bkr.z}, railCol);
                Vector3 midR = Vector3Lerp(fr, bkr, 0.5f);
                DrawLine3D(midR, (Vector3){midR.x, midR.y + railH, midR.z}, railCol);
            }

            // River
            {
                float riverWidth = 8.0f;
                for (int i = 0; i < RIVER_SEGS - 1; i++) {
                    Vector3 p0 = riverPts[i], p1 = riverPts[i + 1];
                    Vector3 dir = Vector3Subtract(p1, p0);
                    Vector3 norm = { -dir.z, 0, dir.x };
                    float len = Vector3Length(norm);
                    if (len > 0.01f) norm = Vector3Scale(norm, riverWidth / (2.0f * len));

                    Vector3 a = Vector3Add(p0, norm), b = Vector3Subtract(p0, norm);
                    Vector3 c = Vector3Add(p1, norm), d = Vector3Subtract(p1, norm);
                    a.y = p0.y; b.y = p0.y; c.y = p1.y; d.y = p1.y;

                    float wave = sinf((float)GetTime() * 1.5f + i * 0.5f) * 0.3f;
                    Color waterCol = { 40, (unsigned char)(100 + wave * 20), (unsigned char)(180 + wave * 15), 220 };
                    DrawTriangle3D(a, b, c, waterCol);
                    DrawTriangle3D(b, d, c, waterCol);
                    DrawTriangle3D(a, c, b, waterCol);
                    DrawTriangle3D(b, c, d, waterCol);
                }
            }

            // Trees
            for (int i = 0; i < NUM_TREES; i++) {
                if (Vector3Distance(trees[i].pos, camera.position) < 400)
                    DrawTree3D(&trees[i]);
            }

            // Bushes
            for (int i = 0; i < NUM_BUSHES; i++) {
                if (Vector3Distance(bushes[i].pos, camera.position) < 200) {
                    Vector3 bs = { bushes[i].size, bushes[i].size, bushes[i].size };
                    DrawObject3DScaled(bushParts, bushPartCount, bushes[i].pos, 0, bs);
                }
            }

            // Buildings
            for (int i = 0; i < NUM_BUILDINGS; i++) {
                Building *b = &buildings[i];
                Vector3 bScale = { b->w / 5.0f, b->h / 10.0f, b->d / 5.0f };
                DrawObject3DScaled(buildingParts, buildingPartCount, b->pos, 0, bScale);
            }

            // Houses
            for (int i = 0; i < NUM_HOUSES; i++) {
                Building *h = &houses[i];
                Vector3 hScale = { h->w / 4.0f, h->h / 3.0f, h->d / 5.0f };
                DrawObject3DScaled(houseParts, housePartCount, h->pos, 0, hScale);
            }

            // Rings
            for (int i = 0; i < NUM_RINGS; i++) DrawFlyRing(&rings[i]);

            // Clouds
            for (int i = 0; i < NUM_CLOUDS; i++) DrawCloud(&clouds[i]);

            // Biplane
            if (!plane.crashed) {
                DrawBiplane(&plane);
            } else {
                // Draw debris pieces
                for (int i = 0; i < MAX_DEBRIS; i++) {
                    if (!debris[i].active) continue;
                    float s = debris[i].size;
                    switch (debris[i].type) {
                        case PART_CUBE:
                            DrawCubeRotated(debris[i].pos, s, s * 0.5f, s,
                                debris[i].pitch, debris[i].yaw, debris[i].roll, debris[i].color);
                            break;
                        case PART_SPHERE:
                            DrawSphere(debris[i].pos, s * 0.5f, debris[i].color);
                            break;
                        case PART_CYLINDER:
                        case PART_CONE: {
                            Vector3 top = Vector3Add(debris[i].pos,
                                RotateVec((Vector3){0, s, 0}, debris[i].pitch, debris[i].yaw, debris[i].roll));
                            DrawCylinderEx(debris[i].pos, top, s * 0.3f, s * 0.1f, 4, debris[i].color);
                            break;
                        }
                    }
                }

                // Draw explosion particles
                for (int i = 0; i < MAX_EXPLOSION; i++) {
                    if (!explosion[i].active) continue;
                    float alpha = explosion[i].life / explosion[i].maxLife;
                    Color ec = explosion[i].color;
                    ec.a = (unsigned char)(alpha * ec.a);
                    float sz = explosion[i].size * (1.0f + (1.0f - alpha) * 2.0f);
                    DrawSphere(explosion[i].pos, sz, ec);
                }

                // Smoke rising from crash site
                if (plane.crashTimer < 5.0f) {
                    for (int s = 0; s < 3; s++) {
                        float sx = plane.crashPos.x + sinf(plane.crashTimer * 2 + s) * 0.5f;
                        float sz = plane.crashPos.z + cosf(plane.crashTimer * 3 + s) * 0.5f;
                        float sy = TerrainHeight(sx, sz) + plane.crashTimer * 2.0f + s * 0.5f;
                        float smokeAlpha = 1.0f - plane.crashTimer / 5.0f;
                        DrawSphere((Vector3){sx, sy, sz}, 0.3f + plane.crashTimer * 0.2f,
                            (Color){60, 60, 60, (unsigned char)(smokeAlpha * 150)});
                    }
                }
            }

        EndMode3D();

        // --- Lens flare (2D, from sun screen position) ---
        {
            Vector2 sunScreen = GetWorldToScreen(sunPos, camera);
            float cx = sw / 2.0f, cy = sh / 2.0f;

            // Check if sun is in front of camera
            Vector3 camToSun = Vector3Subtract(sunPos, camera.position);
            Vector3 camFwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            float sunDot = Vector3DotProduct(camFwd, Vector3Normalize(camToSun));

            // Only show if sun is in front of camera and roughly on screen
            if (sunDot > 0 &&
                sunScreen.x > -100 && sunScreen.x < sw + 100 &&
                sunScreen.y > -100 && sunScreen.y < sh + 100) {

                // Direction from center to sun
                float dx = sunScreen.x - cx, dy = sunScreen.y - cy;

                // Brightness falloff toward edges
                float edgeDist = sqrtf(dx*dx + dy*dy) / sqrtf(cx*cx + cy*cy);
                float brightness = Clamp(1.0f - edgeDist * 0.5f, 0.2f, 1.0f);

                // Big sun glow — multiple layers
                DrawCircle(sunScreen.x, sunScreen.y, 120 * brightness, (Color){255, 250, 220, (unsigned char)(25 * brightness)});
                DrawCircle(sunScreen.x, sunScreen.y, 80 * brightness, (Color){255, 250, 230, (unsigned char)(40 * brightness)});
                DrawCircle(sunScreen.x, sunScreen.y, 45 * brightness, (Color){255, 255, 240, (unsigned char)(70 * brightness)});
                DrawCircle(sunScreen.x, sunScreen.y, 20 * brightness, (Color){255, 255, 250, (unsigned char)(120 * brightness)});

                // Flare elements — bigger and bolder
                float flareDists[] = { 0.2f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f, 1.2f, 1.4f, 1.7f };
                float flareSizes[] = { 35, 15, 50, 12, 30, 45, 20, 60, 25 };
                Color flareColors[] = {
                    {255, 220, 150, 70}, {150, 220, 255, 55}, {255, 180, 80, 50},
                    {220, 255, 220, 45}, {255, 130, 70, 60}, {130, 180, 255, 55},
                    {255, 255, 180, 50}, {180, 220, 255, 45}, {255, 200, 130, 40}
                };

                for (int f = 0; f < 9; f++) {
                    float t = flareDists[f];
                    float fx = sunScreen.x - dx * t;
                    float fy = sunScreen.y - dy * t;
                    float sz = flareSizes[f] * brightness;
                    Color fc = flareColors[f];
                    fc.a = (unsigned char)(fc.a * brightness);
                    DrawCircle(fx, fy, sz, fc);
                    DrawCircle(fx, fy, sz * 0.6f, (Color){fc.r, fc.g, fc.b, (unsigned char)(fc.a * 0.4f)});
                    if (f % 2 == 0)
                        DrawPoly((Vector2){fx, fy}, 6, sz * 0.8f, 30, (Color){fc.r, fc.g, fc.b, (unsigned char)(fc.a * 0.3f)});
                }

                // Big screen-wide wash when looking at sun
                if (edgeDist < 0.8f) {
                    unsigned char washAlpha = (unsigned char)((0.8f - edgeDist) * 60 * brightness);
                    DrawRectangle(0, 0, sw, sh, (Color){255, 245, 220, washAlpha});
                }
            }
        }

        // --- ARCADE HUD ---

        // Speed — big bold bottom-left
        {
            DrawRectangle(15, sh - 90, 250, 80, (Color){0,0,0,180});
            DrawRectangleLinesEx((Rectangle){15, sh - 90, 250, 80}, 2, (Color){0,200,255,200});
            DrawText("SPEED", 25, sh - 85, 20, (Color){0,150,200,255});
            DrawText(TextFormat("%.0f", plane.speed * 3.6f), 25, sh - 62, 50, (Color){0,220,255,255});
            DrawText("KM/H", 180, sh - 42, 22, (Color){0,150,200,200});
        }

        // Altitude — bottom-left above speed
        {
            DrawRectangle(15, sh - 155, 250, 55, (Color){0,0,0,180});
            DrawRectangleLinesEx((Rectangle){15, sh - 155, 250, 55}, 2, (Color){100,200,100,200});
            DrawText("ALT", 25, sh - 150, 20, (Color){80,180,80,255});
            DrawText(TextFormat("%.0f m", plane.pos.y), 80, sh - 148, 40, (Color){100,255,100,255});
        }

        // Throttle — big vertical bar right side
        {
            int barX = sw - 70, barY = sh - 280, barW = 40, barH = 220;
            DrawRectangle(barX - 5, barY - 25, barW + 10, barH + 35, (Color){0,0,0,180});
            DrawRectangleLinesEx((Rectangle){barX - 5, barY - 25, barW + 10, barH + 35}, 2, (Color){255,150,0,200});
            DrawText("THR", barX, barY - 22, 22, (Color){255,150,0,255});
            DrawRectangle(barX, barY, barW, barH, (Color){30,30,30,255});
            int tH = (int)(plane.throttle * barH);
            Color thrCol = plane.throttle > 0.7f ? (Color){255,100,0,255} :
                           plane.throttle > 0.3f ? (Color){0,200,100,255} : (Color){100,100,100,255};
            DrawRectangle(barX, barY + barH - tH, barW, tH, thrCol);
            DrawText(TextFormat("%d%%", (int)(plane.throttle * 100)), barX + 2, barY + barH + 3, 18, thrCol);
        }

        // Rings — top center, big and gold
        {
            const char *ringText = TextFormat("%d / %d", plane.ringsHit, NUM_RINGS);
            int rw = MeasureText(ringText, 44);
            DrawRectangle(sw/2 - rw/2 - 30, 10, rw + 60, 60, (Color){0,0,0,180});
            DrawRectangleLinesEx((Rectangle){sw/2 - rw/2 - 30, 10, rw + 60, 60}, 2, GOLD);
            DrawText("RINGS", sw/2 - 30, 14, 18, (Color){200,180,80,255});
            DrawText(ringText, sw/2 - rw/2, 32, 44, GOLD);
        }

        // Compass — top right
        {
            float heading = plane.yaw * RAD2DEG;
            while (heading < 0) heading += 360;
            while (heading > 360) heading -= 360;
            DrawRectangle(sw - 130, 10, 120, 50, (Color){0,0,0,180});
            DrawRectangleLinesEx((Rectangle){sw - 130, 10, 120, 50}, 2, (Color){200,200,200,200});
            DrawText("HDG", sw - 125, 14, 18, (Color){180,180,180,255});
            DrawText(TextFormat("%.0f", heading), sw - 120, 32, 30, WHITE);
            DrawCircle(sw - 35, 35, 3, (Color){255,100,100,255});
        }

        // Attitude indicator — center of screen, larger
        {
            int cx = sw/2, cy = sh/2;
            float pitchOffset = plane.pitch * 120;
            float rollAngle = plane.roll;
            float lineLen = 90;
            float lx = cosf(rollAngle) * lineLen;
            float ly = sinf(rollAngle) * lineLen;
            DrawLineEx(
                (Vector2){cx - lx, cy + pitchOffset - ly},
                (Vector2){cx + lx, cy + pitchOffset + ly}, 2, (Color){220,220,220,100});
            // Wings
            DrawLineEx((Vector2){cx - 15, cy}, (Vector2){cx - 5, cy}, 2, (Color){220,220,220,120});
            DrawLineEx((Vector2){cx + 5, cy}, (Vector2){cx + 15, cy}, 2, (Color){220,220,220,120});
            DrawCircleLines(cx, cy, 3, (Color){220,220,220,100});
        }

        // Crashed — big dramatic
        if (plane.crashed) {
            // Red flash overlay
            float flash = Clamp(1.0f - plane.crashTimer / 0.5f, 0, 0.3f);
            DrawRectangle(0, 0, sw, sh, (Color){255, 0, 0, (unsigned char)(flash * 255)});

            DrawRectangle(sw/2 - 200, sh/2 - 50, 400, 110, (Color){0,0,0,220});
            DrawRectangleLinesEx((Rectangle){sw/2 - 200, sh/2 - 50, 400, 110}, 3, RED);
            int cw = MeasureText("CRASHED!", 60);
            DrawText("CRASHED!", sw/2 - cw/2, sh/2 - 40, 60, RED);
            if (plane.crashTimer > 1.0f) {
                int rw = MeasureText("PRESS R TO RETRY", 24);
                DrawText("PRESS R TO RETRY", sw/2 - rw/2, sh/2 + 30, 24, (Color){200,200,200,255});
            }
        }

        // All rings collected — big celebration
        if (plane.ringsHit >= NUM_RINGS) {
            DrawRectangle(sw/2 - 220, sh/2 - 45, 440, 100, (Color){0,0,0,220});
            DrawRectangleLinesEx((Rectangle){sw/2 - 220, sh/2 - 45, 440, 100}, 3, GOLD);
            int aw = MeasureText("ALL RINGS COLLECTED!", 40);
            DrawText("ALL RINGS COLLECTED!", sw/2 - aw/2, sh/2 - 35, 40, GOLD);
            DrawText("CONGRATULATIONS!", sw/2 - 100, sh/2 + 15, 24, WHITE);
        }

        // Controls — bottom
        DrawRectangle(0, sh - 25, sw, 25, (Color){0,0,0,120});
        DrawText("W/S: PITCH   Q/E: ROLL   A/D: RUDDER   SHIFT/CTRL: THROTTLE   TAB: SETTINGS",
            15, sh - 20, 16, (Color){150,150,180,220});

        // Toggle tweaks + unlock cursor
        if (IsKeyPressed(KEY_TAB)) {
            showTweaks = !showTweaks;
            if (showTweaks) EnableCursor();
            else DisableCursor();
        }

        // Clip plane sliders
        if (showTweaks) {
            int px = sw - 230, py = 50;
            int sliderW = 200, sliderH = 14;
            Vector2 mouse = GetMousePosition();
            bool mouseDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

            DrawRectangle(px - 10, py - 10, sliderW + 30, 110, (Color){0,0,0,200});
            DrawText("CLIP PLANES [TAB]", px, py - 5, 12, GOLD);
            py += 18;

            // Near clip
            DrawText(TextFormat("Near: %.3f", clipNear), px, py, 10, WHITE);
            py += 12;
            DrawRectangle(px, py, sliderW, sliderH, (Color){40,40,50,255});
            float nearPct = (clipNear - 0.01f) / (10.0f - 0.01f);
            DrawRectangle(px, py, (int)(nearPct * sliderW), sliderH, (Color){0,150,255,200});
            DrawRectangle(px + (int)(nearPct * sliderW) - 3, py - 2, 6, sliderH + 4, WHITE);
            if (mouseDown && mouse.y >= py - 2 && mouse.y <= py + sliderH + 4 &&
                mouse.x >= px && mouse.x <= px + sliderW) {
                clipNear = 0.01f + ((mouse.x - px) / (float)sliderW) * (10.0f - 0.01f);
            }
            py += sliderH + 10;

            // Far clip
            DrawText(TextFormat("Far: %.0f", clipFar), px, py, 10, WHITE);
            py += 12;
            DrawRectangle(px, py, sliderW, sliderH, (Color){40,40,50,255});
            float farPct = (clipFar - 10.0f) / (5000.0f - 10.0f);
            DrawRectangle(px, py, (int)(farPct * sliderW), sliderH, (Color){255,150,0,200});
            DrawRectangle(px + (int)(farPct * sliderW) - 3, py - 2, 6, sliderH + 4, WHITE);
            if (mouseDown && mouse.y >= py - 2 && mouse.y <= py + sliderH + 4 &&
                mouse.x >= px && mouse.x <= px + sliderW) {
                clipFar = 10.0f + ((mouse.x - px) / (float)sliderW) * (5000.0f - 10.0f);
            }
        }

        DrawFPS(sw - 80, sh - 20);
        EndDrawing();
    }

    EnableCursor();
    CloseWindow();
    return 0;
}
