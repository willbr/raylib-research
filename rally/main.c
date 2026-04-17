#include "raylib.h"
#include "raymath.h"
#include "../common/objects3d.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Sega Rally style: winding track with surface types, AI, dust effects

#define TRACK_SEGS    64
#define TRACK_WIDTH   14.0f
#define BANK_WIDTH     5.0f  // horizontal reach of the grassy bank beyond each edge
#define BANK_HEIGHT    0.4f  // vertical rise of the bank at its outer lip (just a shoulder)
#define NUM_CARS       4
#define TOTAL_LAPS     3
#define MAX_DUST      80

// Car physics
#define CAR_ACCEL      35.0f
#define CAR_BRAKE      40.0f   // S/Down full brake — scrubs quickly and can cross 0 into reverse
#define CAR_HANDBRAKE  18.0f   // Space handbrake — gentler, clamps at 0 (no reverse)
#define CAR_MAX_SPEED  45.0f
#define CAR_TURN        2.8f
#define CAR_DRAG_ROAD   0.992f
#define CAR_DRAG_GRAVEL 0.990f
#define CAR_DRAG_MUD    0.985f
#define CAR_DRIFT_MULT  1.6f
#define CAR_DRIFT_DRAG  0.988f

// Surface types per track segment
typedef enum { SURF_TARMAC, SURF_GRAVEL, SURF_MUD } SurfaceType;

typedef struct {
    Vector3 pos;
    float rotation;
    float speed;
    float velY;      // vertical speed (only non-zero when airborne)
    bool airborne;   // off the ground — gravity applies, no road-follow
    float rampCooldown; // seconds until another ramp launch is allowed
    float visRoll;   // visual lean into corners (radians, cosmetic)
    float visPitch;  // visual nose-up/down tilt (radians, cosmetic)
    Vector3 prevRearL, prevRearR; // previous rear-wheel world positions (for skid marks)
    bool hasPrevWheels;
    int lap;
    int nextWP;
    int currentSeg;  // cached nearest segment
    int prevSeg;     // last frame's currentSeg, for transition detection
    bool isPlayer;
    bool finished;
    bool drifting;
    float driftTime;
    Color color;
    float aiNoise;
    float steerInput;
} Car;

// --- Skid marks (ring buffer) ---
#define MAX_SKIDS 512
typedef struct { Vector3 a, b; bool active; } Skid;
static Skid skids[MAX_SKIDS];
static int skidIdx = 0;

#define RALLY_GRAVITY 35.0f

// Explicit launch-ramp segments — hitting one at speed triggers a jump.
// Must match the rampSegs used in GenerateTrack().
#define RAMP_COUNT 3
static const int RAMP_SEGS[RAMP_COUNT] = { 12, 30, 48 };

static bool IsRampSeg(int seg) {
    for (int i = 0; i < RAMP_COUNT; i++) if (RAMP_SEGS[i] == seg) return true;
    return false;
}

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float life;
    Color color;
    bool active;
} Dust;

// Car models using objects3d.h
static Part carBody[] = {
    CUBE(0, 0.25f, 0,   1.0f, 0.4f, 1.8f, COL(200,40,40,255)),     // body
    CUBE(0, 0.5f, -0.15f, 0.75f, 0.35f, 0.9f, COL(180,35,35,255)), // cabin
    CUBE(0, 0.5f, -0.15f, 0.55f, 0.3f, 0.7f, COL(140,200,230,255)),// windows
    SPHERE(-0.45f, 0.12f, 0.7f,  0.14f, COL(30,30,30,255)),         // wheel FL
    SPHERE( 0.45f, 0.12f, 0.7f,  0.14f, COL(30,30,30,255)),         // wheel FR
    SPHERE(-0.45f, 0.12f,-0.7f,  0.14f, COL(30,30,30,255)),         // wheel RL
    SPHERE( 0.45f, 0.12f,-0.7f,  0.14f, COL(30,30,30,255)),         // wheel RR
    CUBE(0, 0.05f, -0.95f, 0.4f, 0.08f, 0.1f, COL(60,60,60,255)),  // rear bumper
    SPHERE(-0.35f, 0.3f, 0.9f, 0.06f, COL(255,255,200,255)),        // headlight L
    SPHERE( 0.35f, 0.3f, 0.9f, 0.06f, COL(255,255,200,255)),        // headlight R
    SPHERE(-0.35f, 0.3f,-0.9f, 0.06f, COL(255,30,30,255)),          // taillight L
    SPHERE( 0.35f, 0.3f,-0.9f, 0.06f, COL(255,30,30,255)),          // taillight R
};
static int carBodyCount = sizeof(carBody)/sizeof(Part);

// Track data
static Vector3 trackPts[TRACK_SEGS];
static Vector3 trackDirs[TRACK_SEGS];
static Vector3 trackNormals[TRACK_SEGS];
static SurfaceType trackSurface[TRACK_SEGS];

// Dust particles
static Dust dust[MAX_DUST];
static int dustIdx = 0;

// Trees along track
#define MAX_TREES 80
static Vector3 treePosns[MAX_TREES];
static float treeSizes[MAX_TREES];
static int numTrees = 0;

#define MAX_BUILDINGS 28
typedef struct { Vector3 pos; Vector3 size; float rotY; Color wall, roof; } Building;
static Building buildings[MAX_BUILDINGS];
static int numBuildings = 0;

#define MAX_PROPS 64
typedef enum { PROP_CRATE, PROP_FENCE } PropType;
typedef struct {
    Vector3 pos;
    Vector3 size;   // width, height, depth (in local orientation before rotY)
    float rotY;
    PropType type;
    bool alive;     // solid collider (false once hit)
    // Tumbling-debris state, active while flyTimer > 0 after a hit.
    Vector3 vel;
    float angVel;
    float flyTimer;
} Prop;
static Prop props[MAX_PROPS];
static int numProps = 0;

#define MAX_PUDDLES 40
typedef struct { Vector3 pos; float radius; } Puddle;
static Puddle puddles[MAX_PUDDLES];
static int numPuddles = 0;

#define MAX_FANS 80
typedef struct { Vector3 pos; Color shirt; float phase; } Fan;
static Fan fans[MAX_FANS];
static int numFans = 0;

// Checkpoint gates — drawn as arches across the road at fixed segments.
// Crossing one banks a split time for the current lap.
#define NUM_CHECKPOINTS 3
static const int CHECKPOINT_SEGS[NUM_CHECKPOINTS] = { 16, 32, 48 };
static float cpSplitTimes[NUM_CHECKPOINTS] = {0};
static int cpNextIdx = 0;
static float cpFlashTimer = 0.0f;

void GenerateTrack(void) {
    // Winding rally course with varied curvature and gentle height.
    for (int i = 0; i < TRACK_SEGS; i++) {
        float t = (float)i / TRACK_SEGS * 2.0f * PI;
        // Irregular shape: deformed ellipse with tight corners
        float rx = 100.0f + sinf(t * 2.0f) * 35.0f + cosf(t * 3.0f) * 20.0f;
        float rz = 70.0f + cosf(t * 2.0f) * 25.0f + sinf(t * 5.0f) * 15.0f;
        // Two-octave height — gentle hills, no sharp drops.
        float hy = sinf(t * 1.7f + 0.3f) * 1.8f + cosf(t * 3.1f + 0.7f) * 1.0f;
        trackPts[i] = (Vector3){ cosf(t) * rx, hy, sinf(t) * rz };

        // Surface assignment: tarmac default, gravel/mud in sections
        if (i % 16 < 6) trackSurface[i] = SURF_GRAVEL;
        else if (i % 16 < 8) trackSurface[i] = SURF_MUD;
        else trackSurface[i] = SURF_TARMAC;
    }

    // Smooth track (all three axes).
    for (int pass = 0; pass < 4; pass++) {
        Vector3 temp[TRACK_SEGS];
        for (int i = 0; i < TRACK_SEGS; i++) {
            int prev = (i - 1 + TRACK_SEGS) % TRACK_SEGS;
            int next = (i + 1) % TRACK_SEGS;
            temp[i] = (Vector3){
                (trackPts[prev].x + trackPts[i].x * 2 + trackPts[next].x) / 4.0f,
                (trackPts[prev].y + trackPts[i].y * 2 + trackPts[next].y) / 4.0f,
                (trackPts[prev].z + trackPts[i].z * 2 + trackPts[next].z) / 4.0f
            };
        }
        for (int i = 0; i < TRACK_SEGS; i++) trackPts[i] = temp[i];
    }

    // Explicit launch ramps — sharp bumps the smoothing won't flatten too
    // much. Placed on straightish sections so the car hits them square.
    // These segments must match RAMP_SEGS (used by the launch logic).
    for (int r = 0; r < RAMP_COUNT; r++) {
        int s = RAMP_SEGS[r];
        trackPts[s].y += 4.0f;
        trackPts[(s + 1) % TRACK_SEGS].y += 1.5f;
        trackPts[(s + TRACK_SEGS - 1) % TRACK_SEGS].y += 1.5f;
    }

    // Final light smoothing so ramp approach/landing isn't a cliff, then
    // shift so the lowest point sits on the ground plane (trees stay at y=0).
    for (int pass = 0; pass < 1; pass++) {
        Vector3 temp[TRACK_SEGS];
        for (int i = 0; i < TRACK_SEGS; i++) {
            int prev = (i - 1 + TRACK_SEGS) % TRACK_SEGS;
            int next = (i + 1) % TRACK_SEGS;
            temp[i] = (Vector3){
                trackPts[i].x,
                (trackPts[prev].y + trackPts[i].y * 2 + trackPts[next].y) / 4.0f,
                trackPts[i].z
            };
        }
        for (int i = 0; i < TRACK_SEGS; i++) trackPts[i] = temp[i];
    }

    float minY = 1e9f;
    for (int i = 0; i < TRACK_SEGS; i++) if (trackPts[i].y < minY) minY = trackPts[i].y;
    if (minY < 0.0f) for (int i = 0; i < TRACK_SEGS; i++) trackPts[i].y -= minY;

    // Compute directions and normals
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        trackDirs[i] = Vector3Normalize(Vector3Subtract(trackPts[next], trackPts[i]));
        trackNormals[i] = (Vector3){ -trackDirs[i].z, 0, trackDirs[i].x };
    }

    // Place trees along track edges
    numTrees = 0;
    for (int i = 0; i < TRACK_SEGS && numTrees < MAX_TREES; i += 1) {
        for (int side = -1; side <= 1; side += 2) {
            if (numTrees >= MAX_TREES) break;
            if (GetRandomValue(0, 3) != 0) continue; // sparse
            // Keep trees clear of the bank geometry (road edge + BANK_WIDTH)
            // — they'd otherwise be swallowed by the sloping bank quads.
            float offset = (TRACK_WIDTH / 2.0f) + BANK_WIDTH + 1.5f
                         + (float)GetRandomValue(0, 10);
            treePosns[numTrees] = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], side * offset));
            // Plant the tree at its adjacent track segment's height so it
            // rides the same terrain as the road — the flat ground plane
            // is way below once the track gains height variation.
            treePosns[numTrees].y = trackPts[i].y;
            treeSizes[numTrees] = 1.0f + (float)GetRandomValue(0, 10) / 10.0f;
            numTrees++;
        }
    }

    // Place buildings sparsely further out than the trees. Each building
    // is a coloured box with a pitched roof; rotated to face the track.
    numBuildings = 0;
    Color wallPalette[] = {
        {210,190,150,255}, {180,160,120,255}, {205,205,200,255},
        {150,115, 80,255}, {200,120,100,255}, {160,175,180,255},
    };
    Color roofPalette[] = {
        {120, 50, 40,255}, { 90, 70, 60,255}, { 60, 70, 95,255},
        { 70, 55, 40,255}, {150,110, 90,255},
    };
    int paletteWalls = sizeof(wallPalette) / sizeof(wallPalette[0]);
    int paletteRoofs = sizeof(roofPalette) / sizeof(roofPalette[0]);

    for (int i = 0; i < TRACK_SEGS && numBuildings < MAX_BUILDINGS; i += 2) {
        for (int side = -1; side <= 1; side += 2) {
            if (numBuildings >= MAX_BUILDINGS) break;
            if (GetRandomValue(0, 5) != 0) continue;  // very sparse

            float offset = (TRACK_WIDTH / 2.0f) + BANK_WIDTH + 18.0f
                         + (float)GetRandomValue(0, 20);
            Vector3 p = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], side * offset));
            p.y = trackPts[i].y;

            Building *b = &buildings[numBuildings++];
            b->pos  = p;
            b->size = (Vector3){
                4.0f + (float)GetRandomValue(0, 40) / 10.0f,  // width 4–8
                3.0f + (float)GetRandomValue(0, 60) / 10.0f,  // height 3–9
                4.0f + (float)GetRandomValue(0, 40) / 10.0f,  // depth 4–8
            };
            // Face roughly toward the track (flip if on the far side).
            float toTrack = atan2f(-trackNormals[i].x * side, -trackNormals[i].z * side);
            b->rotY = toTrack + ((float)GetRandomValue(-20, 20)) * 0.01f;
            b->wall = wallPalette[GetRandomValue(0, paletteWalls - 1)];
            b->roof = roofPalette[GetRandomValue(0, paletteRoofs - 1)];
        }
    }

    // Place crashable props: fences lining the shoulder and crates on the
    // road surface. Sparse so the track stays mostly clear.
    numProps = 0;
    for (int i = 3; i < TRACK_SEGS && numProps < MAX_PROPS; i++) {
        // Fence rows on the shoulder every few segments, alternating sides.
        if ((i % 4) == 0 && numProps + 1 < MAX_PROPS) {
            int side = ((i / 4) % 2) ? 1 : -1;
            float off = TRACK_WIDTH / 2.0f + 0.6f;
            Vector3 p = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], side * off));
            p.y = trackPts[i].y + 0.4f;  // sit on top of the shoulder
            Prop *pr = &props[numProps++];
            pr->pos = p;
            pr->size = (Vector3){2.4f, 0.9f, 0.25f};
            pr->rotY = atan2f(trackDirs[i].x, trackDirs[i].z);
            pr->type = PROP_FENCE;
            pr->alive = true;
        }
        // Crates scattered in the road (rare — don't clog every lap).
        if (GetRandomValue(0, 7) == 0 && numProps < MAX_PROPS) {
            float off = ((float)GetRandomValue(-30, 30)) / 10.0f;  // -3..3 from centerline
            Vector3 p = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], off));
            p.y = trackPts[i].y + 0.4f;  // half-height above road
            Prop *pr = &props[numProps++];
            pr->pos = p;
            pr->size = (Vector3){0.8f, 0.8f, 0.8f};
            pr->rotY = (float)GetRandomValue(0, 314) * 0.01f;
            pr->type = PROP_CRATE;
            pr->alive = true;
        }
    }

    // Cheering fans lining the shoulder — clusters of 2–4 every handful
    // of segments, on either side, just outside the bank lip.
    numFans = 0;
    Color shirtPalette[] = {
        {220, 80, 70,255}, {70,120,220,255}, {230,200, 80,255},
        {90,190,110,255}, {200,100,210,255}, {240,240,240,255},
    };
    int shirtCount = sizeof(shirtPalette) / sizeof(shirtPalette[0]);
    for (int i = 1; i < TRACK_SEGS && numFans < MAX_FANS; i += 2) {
        for (int side = -1; side <= 1; side += 2) {
            if (numFans >= MAX_FANS) break;
            if (GetRandomValue(0, 2) != 0) continue;   // not every segment
            int cluster = GetRandomValue(2, 4);
            for (int c = 0; c < cluster && numFans < MAX_FANS; c++) {
                float radial = (TRACK_WIDTH / 2.0f) + BANK_WIDTH + 1.0f
                             + (float)GetRandomValue(0, 15) / 10.0f;
                float lateral = ((float)GetRandomValue(-20, 20)) / 10.0f;
                Vector3 p = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], side * radial));
                p = Vector3Add(p, Vector3Scale(trackDirs[i], lateral));
                p.y = trackPts[i].y;
                fans[numFans].pos = p;
                fans[numFans].shirt = shirtPalette[GetRandomValue(0, shirtCount - 1)];
                fans[numFans].phase = (float)GetRandomValue(0, 628) / 100.0f;
                numFans++;
            }
        }
    }

    // Puddles on mud segments — splash when driven through.
    numPuddles = 0;
    for (int i = 0; i < TRACK_SEGS && numPuddles < MAX_PUDDLES; i++) {
        if (trackSurface[i] != SURF_MUD) continue;
        if (GetRandomValue(0, 2) != 0) continue;
        float off = ((float)GetRandomValue(-35, 35)) / 10.0f;
        Vector3 p = Vector3Add(trackPts[i], Vector3Scale(trackNormals[i], off));
        p.y = trackPts[i].y + 0.015f;  // flush on the road
        puddles[numPuddles].pos = p;
        puddles[numPuddles].radius = 0.9f + (float)GetRandomValue(0, 16) / 10.0f;
        numPuddles++;
    }
}

// XZ-only distance — used for segment proximity so elevated (ramp) track
// points don't register as "farther" than flat ones and cause currentSeg
// to oscillate at the foot of a ramp.
static float XZDistance(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

int NearestSeg(Vector3 pos) {
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < TRACK_SEGS; i++) {
        float d = XZDistance(pos, trackPts[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// Search only nearby segments to avoid jitter at corners
int NearestSegLocal(Vector3 pos, int hint) {
    int best = hint;
    float bestD = XZDistance(pos, trackPts[hint]);
    int range = 5;
    for (int offset = -range; offset <= range; offset++) {
        int idx = (hint + offset + TRACK_SEGS) % TRACK_SEGS;
        float d = XZDistance(pos, trackPts[idx]);
        if (d < bestD) { bestD = d; best = idx; }
    }
    return best;
}

// Interpolated track-surface height at the XZ footprint of `pos`, plus
// a small ride height so wheels sit on the road.
float TrackHeightAt(Vector3 pos, int seg) {
    int next = (seg + 1) % TRACK_SEGS;
    Vector3 segDir = Vector3Subtract(trackPts[next], trackPts[seg]);
    float segLen2 = segDir.x * segDir.x + segDir.z * segDir.z;
    if (segLen2 < 1e-6f) return trackPts[seg].y + 0.2f;
    float dx = pos.x - trackPts[seg].x, dz = pos.z - trackPts[seg].z;
    float t = Clamp((dx * segDir.x + dz * segDir.z) / segLen2, 0.0f, 1.0f);
    return trackPts[seg].y + (trackPts[next].y - trackPts[seg].y) * t + 0.2f;
}

// Get closest point on track segment for smooth distance
Vector3 ClosestPointOnSeg(Vector3 pos, int seg) {
    int next = (seg + 1) % TRACK_SEGS;
    Vector3 segDir = Vector3Subtract(trackPts[next], trackPts[seg]);
    float segLen = Vector3Length(segDir);
    if (segLen < 0.001f) return trackPts[seg];
    segDir = Vector3Scale(segDir, 1.0f / segLen);
    Vector3 toPos = Vector3Subtract(pos, trackPts[seg]);
    float dot = Clamp(Vector3DotProduct(toPos, segDir), 0, segLen);
    return Vector3Add(trackPts[seg], Vector3Scale(segDir, dot));
}

float DistFromTrack(Vector3 pos) {
    int seg = NearestSeg(pos);
    int next = (seg + 1) % TRACK_SEGS;
    Vector3 segDir = Vector3Subtract(trackPts[next], trackPts[seg]);
    float segLen = Vector3Length(segDir);
    if (segLen < 0.001f) return Vector3Distance(pos, trackPts[seg]);
    segDir = Vector3Scale(segDir, 1.0f / segLen);
    Vector3 toPos = Vector3Subtract(pos, trackPts[seg]);
    float dot = Vector3DotProduct(toPos, segDir);
    Vector3 closest = Vector3Add(trackPts[seg], Vector3Scale(segDir, Clamp(dot, 0, segLen)));
    return Vector3Distance(pos, closest);
}

SurfaceType GetSurface(Vector3 pos) {
    return trackSurface[NearestSeg(pos)];
}

void SpawnDust(Vector3 pos, Color col) {
    Dust *d = &dust[dustIdx];
    d->pos = pos;  // caller is responsible for Y; with track height variation,
                   // spawning at a fixed y buried the dust under the road.
    d->vel = (Vector3){
        (float)GetRandomValue(-20, 20) / 10.0f,
        (float)GetRandomValue(5, 20) / 10.0f,
        (float)GetRandomValue(-20, 20) / 10.0f
    };
    d->life = 0.5f + (float)GetRandomValue(0, 30) / 100.0f;
    d->color = col;
    d->active = true;
    dustIdx = (dustIdx + 1) % MAX_DUST;
}

// Continuous progress: completed segments + fraction along the current one.
// Using integer waypoints alone caused rank to flicker between cars on the
// same segment — whoever hit the next waypoint first popped ahead by a
// whole unit until the other caught up.
float CarProgress(Car *c) {
    int next = c->nextWP;
    int prev = (next + TRACK_SEGS - 1) % TRACK_SEGS;
    Vector3 segVec = Vector3Subtract(trackPts[next], trackPts[prev]);
    float segLen2 = Vector3DotProduct(segVec, segVec);
    float frac = 0.0f;
    if (segLen2 > 1e-6f) {
        Vector3 toCar = Vector3Subtract(c->pos, trackPts[prev]);
        frac = Clamp(Vector3DotProduct(toCar, segVec) / segLen2, 0.0f, 1.0f);
    }
    // When nextWP wraps to 0 the lap counter has already incremented —
    // back it out so progress stays monotonic across the start line.
    int lap = (next == 0) ? (c->lap - 1) : c->lap;
    return (float)lap * TRACK_SEGS + (float)prev + frac;
}

void DrawTrack(void) {
    for (int i = 0; i < TRACK_SEGS; i++) {
        int next = (i + 1) % TRACK_SEGS;
        float hw = TRACK_WIDTH / 2.0f;
        Vector3 p0 = trackPts[i], p1 = trackPts[next];
        Vector3 n0 = trackNormals[i], n1 = trackNormals[next];

        Vector3 inner0 = Vector3Add(p0, Vector3Scale(n0, -hw));
        Vector3 outer0 = Vector3Add(p0, Vector3Scale(n0, hw));
        Vector3 inner1 = Vector3Add(p1, Vector3Scale(n1, -hw));
        Vector3 outer1 = Vector3Add(p1, Vector3Scale(n1, hw));

        // Surface color
        Color roadCol;
        switch (trackSurface[i]) {
            case SURF_TARMAC: roadCol = (i % 4 < 2) ? (Color){75,75,80,255} : (Color){70,70,75,255}; break;
            case SURF_GRAVEL: roadCol = (i % 4 < 2) ? (Color){140,120,90,255} : (Color){130,110,85,255}; break;
            case SURF_MUD:    roadCol = (i % 4 < 2) ? (Color){90,70,45,255} : (Color){85,65,40,255}; break;
        }

        // Road surface (both windings for visibility)
        DrawTriangle3D(inner0, outer0, inner1, roadCol);
        DrawTriangle3D(outer0, outer1, inner1, roadCol);
        DrawTriangle3D(inner0, inner1, outer0, roadCol);
        DrawTriangle3D(outer0, inner1, outer1, roadCol);

        // Grassy banks either side — slant up from the road edge outward.
        Color bankCol = (Color){95, 135, 70, 255};
        Vector3 bankL0 = Vector3Add(inner0, Vector3Scale(n0, -BANK_WIDTH));
        Vector3 bankL1 = Vector3Add(inner1, Vector3Scale(n1, -BANK_WIDTH));
        Vector3 bankR0 = Vector3Add(outer0, Vector3Scale(n0,  BANK_WIDTH));
        Vector3 bankR1 = Vector3Add(outer1, Vector3Scale(n1,  BANK_WIDTH));
        bankL0.y += BANK_HEIGHT; bankL1.y += BANK_HEIGHT;
        bankR0.y += BANK_HEIGHT; bankR1.y += BANK_HEIGHT;
        // Left bank (both windings)
        DrawTriangle3D(inner0, bankL0, inner1, bankCol);
        DrawTriangle3D(bankL0, bankL1, inner1, bankCol);
        DrawTriangle3D(inner0, inner1, bankL0, bankCol);
        DrawTriangle3D(bankL0, inner1, bankL1, bankCol);
        // Right bank (both windings)
        DrawTriangle3D(outer0, outer1, bankR0, bankCol);
        DrawTriangle3D(bankR0, outer1, bankR1, bankCol);
        DrawTriangle3D(outer0, bankR0, outer1, bankCol);
        DrawTriangle3D(bankR0, bankR1, outer1, bankCol);

        // Edge lines
        if (i % 2 == 0) {
            Color edgeCol = (Color){220, 220, 220, 255};
            inner0.y += 0.02f; inner1.y += 0.02f;
            outer0.y += 0.02f; outer1.y += 0.02f;
            DrawLine3D(inner0, inner1, edgeCol);
            DrawLine3D(outer0, outer1, edgeCol);
        }

        // Center dashes (tarmac only)
        if (trackSurface[i] == SURF_TARMAC && i % 4 == 0) {
            Vector3 cl0 = Vector3Lerp(inner0, outer0, 0.5f);
            Vector3 cl1 = Vector3Lerp(inner1, outer1, 0.5f);
            cl0.y += 0.02f; cl1.y += 0.02f;
            DrawLine3D(cl0, cl1, YELLOW);
        }
    }

    // Start/finish line
    Vector3 p = trackPts[0], n = trackNormals[0];
    float hw = TRACK_WIDTH / 2.0f;
    for (int c = 0; c < 10; c++) {
        float t0 = (float)c / 10, t1 = (float)(c+1) / 10;
        Vector3 left = Vector3Add(p, Vector3Scale(n, -hw));
        Vector3 right = Vector3Add(p, Vector3Scale(n, hw));
        Vector3 a = Vector3Lerp(left, right, t0);
        Vector3 b = Vector3Lerp(left, right, t1);
        Vector3 fwd = Vector3Scale(trackDirs[0], 1.5f);
        Vector3 a2 = Vector3Add(a, fwd), b2 = Vector3Add(b, fwd);
        // Sit on the road surface at seg 0 (+ small offset so z-fighting
        // doesn't strobe it), not at absolute y=0.03 — the track no longer
        // lives at ground level.
        float y = trackPts[0].y + 0.03f;
        a.y = b.y = a2.y = b2.y = y;
        Color chk = (c % 2 == 0) ? WHITE : BLACK;
        DrawTriangle3D(a, b, b2, chk);
        DrawTriangle3D(a, b2, a2, chk);
        DrawTriangle3D(a, b2, b, chk);
        DrawTriangle3D(a, a2, b2, chk);
    }
}

void DrawCheckpoint(int seg, int idx) {
    Vector3 p = trackPts[seg];
    Vector3 n = trackNormals[seg];
    float hw = TRACK_WIDTH / 2.0f + 0.3f;
    Vector3 L = Vector3Add(p, Vector3Scale(n, -hw));
    Vector3 R = Vector3Add(p, Vector3Scale(n,  hw));
    float postH = 5.0f;
    // Two orange/white striped posts.
    Color post = (idx == cpNextIdx) ? (Color){255, 160, 50, 255} : (Color){200, 200, 200, 255};
    DrawCylinderEx(L, (Vector3){L.x, L.y + postH, L.z}, 0.18f, 0.14f, 8, post);
    DrawCylinderEx(R, (Vector3){R.x, R.y + postH, R.z}, 0.18f, 0.14f, 8, post);
    // Banner spanning the top. DrawCubeRotY uses RotateY (CCW from above),
    // so the angle that maps the cube's +X onto L→R is atan2(Δz, Δx) —
    // NOT rally's physics-yaw atan2(Δx, Δz) which would mis-orient the
    // banner by up to 90°.
    float spanW = Vector3Distance(L, R);
    float banRot = atan2f(R.z - L.z, R.x - L.x);
    Vector3 banCenter = { (L.x + R.x) * 0.5f, L.y + postH - 0.45f, (L.z + R.z) * 0.5f };
    Color ban = (idx == cpNextIdx) ? (Color){255, 120, 40, 255} : (Color){80, 80, 110, 255};
    DrawCubeRotY(banCenter, spanW, 0.85f, 0.2f, banRot, ban);
}

void DrawFan(const Fan *f, float t) {
    // Simple stick figure with a bobbing arms-in-the-air animation.
    float bob  = sinf(t * 6.0f + f->phase) * 0.12f;     // cheer bob
    float wave = sinf(t * 9.0f + f->phase * 1.3f) * 0.3f;  // arm sway
    Vector3 feet = { f->pos.x, f->pos.y,        f->pos.z };
    Vector3 hip  = { f->pos.x, f->pos.y + 0.55f, f->pos.z };
    Vector3 chest= { f->pos.x, f->pos.y + 0.95f + bob, f->pos.z };
    Vector3 head = { f->pos.x, f->pos.y + 1.10f + bob, f->pos.z };
    // Legs
    DrawCylinderEx(feet, hip, 0.07f, 0.07f, 6, (Color){40, 40, 60, 255});
    // Torso
    DrawCylinderEx(hip, chest, 0.13f, 0.12f, 6, f->shirt);
    // Head
    DrawSphere(head, 0.13f, (Color){235, 200, 170, 255});
    // Arms raised
    Vector3 lHand = { f->pos.x - 0.25f, f->pos.y + 1.35f + bob + wave, f->pos.z };
    Vector3 rHand = { f->pos.x + 0.25f, f->pos.y + 1.35f + bob - wave, f->pos.z };
    DrawCylinderEx(chest, lHand, 0.05f, 0.04f, 5, f->shirt);
    DrawCylinderEx(chest, rHand, 0.05f, 0.04f, 5, f->shirt);
}

void DrawProp(const Prop *p) {
    if (!p->alive && p->flyTimer <= 0.0f) return;
    Color main, trim;
    if (p->type == PROP_CRATE) {
        main = (Color){150, 100,  60, 255};
        trim = (Color){110,  75,  40, 255};
    } else {
        main = (Color){220, 220, 220, 255};  // painted fence
        trim = (Color){200,  50,  50, 255};
    }
    Vector3 center = { p->pos.x, p->pos.y, p->pos.z };
    DrawCubeRotY(center, p->size.x, p->size.y, p->size.z, p->rotY, main);
    // Trim strip along the top edge for readability.
    Vector3 top = { p->pos.x, p->pos.y + p->size.y * 0.42f, p->pos.z };
    DrawCubeRotY(top, p->size.x * 0.95f, p->size.y * 0.1f, p->size.z * 1.02f,
                 p->rotY, trim);
}

void DrawBuilding(const Building *b) {
    // Walls: a box centred on b->pos (base at b->pos.y).
    Vector3 wallCenter = { b->pos.x, b->pos.y + b->size.y * 0.5f, b->pos.z };
    DrawCubeRotY(wallCenter, b->size.x, b->size.y, b->size.z, b->rotY, b->wall);

    // Pitched roof:
    //   ridge runs along the local X axis between R0 (x=-hx) and R1 (+hx);
    //   two sloping quads cover the -Z and +Z halves of the footprint;
    //   two triangular gables close the ends at x=±hx.
    float hx = b->size.x * 0.5f;
    float hz = b->size.z * 0.5f;
    float h  = b->size.y * 0.45f;  // peak height above the wall top
    float base = b->pos.y + b->size.y;
    Vector3 c0 = RotateY((Vector3){-hx, 0, -hz}, b->rotY);  // front-left
    Vector3 c1 = RotateY((Vector3){ hx, 0, -hz}, b->rotY);  // front-right
    Vector3 c2 = RotateY((Vector3){ hx, 0,  hz}, b->rotY);  // back-right
    Vector3 c3 = RotateY((Vector3){-hx, 0,  hz}, b->rotY);  // back-left
    Vector3 r0 = RotateY((Vector3){-hx, 0,  0},  b->rotY);  // ridge left end
    Vector3 r1 = RotateY((Vector3){ hx, 0,  0},  b->rotY);  // ridge right end
    Vector3 P0 = { b->pos.x + c0.x, base,     b->pos.z + c0.z };
    Vector3 P1 = { b->pos.x + c1.x, base,     b->pos.z + c1.z };
    Vector3 P2 = { b->pos.x + c2.x, base,     b->pos.z + c2.z };
    Vector3 P3 = { b->pos.x + c3.x, base,     b->pos.z + c3.z };
    Vector3 R0 = { b->pos.x + r0.x, base + h, b->pos.z + r0.z };
    Vector3 R1 = { b->pos.x + r1.x, base + h, b->pos.z + r1.z };

    // Gables (wall colour) at x = ±hx — both windings so back-face culling
    // can't swallow either end.
    DrawTriangle3D(P0, P3, R0, b->wall);
    DrawTriangle3D(P0, R0, P3, b->wall);
    DrawTriangle3D(P1, R1, P2, b->wall);
    DrawTriangle3D(P1, P2, R1, b->wall);

    // Front slope: quad P0–P1 up to R0–R1 (roof colour, both windings).
    DrawTriangle3D(P0, R0, P1, b->roof);
    DrawTriangle3D(P1, R0, R1, b->roof);
    DrawTriangle3D(P0, P1, R0, b->roof);
    DrawTriangle3D(P1, R1, R0, b->roof);

    // Back slope: quad P3–P2 up to R0–R1.
    DrawTriangle3D(P3, P2, R1, b->roof);
    DrawTriangle3D(P3, R1, R0, b->roof);
    DrawTriangle3D(P3, R1, P2, b->roof);
    DrawTriangle3D(P3, R0, R1, b->roof);
}

void DrawTree(Vector3 pos, float size) {
    // Trunk
    DrawCylinderEx(pos, (Vector3){pos.x, pos.y + size * 2.5f, pos.z},
        size * 0.15f, size * 0.1f, 6, (Color){90, 65, 35, 255});
    // Canopy layers
    for (int i = 0; i < 3; i++) {
        float y = pos.y + size * (1.2f + i * 0.7f);
        float r = size * (1.0f - i * 0.25f);
        DrawCylinderEx((Vector3){pos.x, y, pos.z},
            (Vector3){pos.x, y + r * 0.7f, pos.z},
            r, 0.05f, 8, (Color){30, 80 + i * 20, 30, 255});
    }
}

// --- Arcade HUD text helpers ---
static void ArcText(const char *t, int x, int y, int size, Color col) {
    // Chunky drop-shadow for arcade readability.
    int off = size >= 40 ? 4 : (size >= 24 ? 3 : 2);
    DrawText(t, x + off, y + off, size, (Color){0, 0, 0, 200});
    DrawText(t, x, y, size, col);
}

static void ArcTextOutlined(const char *t, int x, int y, int size, Color col, Color outline) {
    // Heavy outline for hero text (position, countdown).
    int o = size >= 80 ? 4 : 3;
    for (int dx = -o; dx <= o; dx += o)
        for (int dy = -o; dy <= o; dy += o)
            if (dx || dy) DrawText(t, x + dx, y + dy, size, outline);
    DrawText(t, x, y, size, col);
}

static void ArcPanel(int x, int y, int w, int h, Color fill, Color border) {
    DrawRectangle(x, y, w, h, fill);
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)w, (float)h}, 3.0f, border);
}

// Monospaced rendering for changing numeric fields (race timer, speed) so
// they don't shimmy as individual glyphs in the default font have different
// widths. Each glyph is centered in a cell as wide as '0'.
static int ArcMonoCell(int size) { return MeasureText("0", size); }

static int ArcMonoWidth(const char *t, int size) {
    int len = 0;
    while (t[len]) len++;
    return len * ArcMonoCell(size);
}

static void ArcTextMono(const char *t, int x, int y, int size, Color col) {
    int off  = size >= 40 ? 4 : (size >= 24 ? 3 : 2);
    int cell = ArcMonoCell(size);
    for (int i = 0; t[i]; i++) {
        char s[2] = { t[i], 0 };
        int cw = MeasureText(s, size);
        int cx = x + i * cell + (cell - cw) / 2;
        DrawText(s, cx + off, y + off, size, (Color){0, 0, 0, 200});
        DrawText(s, cx, y, size, col);
    }
}

void DrawCar(Car *car, int colorIdx) {
    // Copy parts and tint body color
    Part tinted[sizeof(carBody)/sizeof(Part)];
    for (int i = 0; i < carBodyCount; i++) tinted[i] = carBody[i];
    tinted[0].color = car->color;                    // body
    Color darker = { car->color.r*3/4, car->color.g*3/4, car->color.b*3/4, 255 };
    tinted[1].color = darker;                         // cabin

    // Yaw passes in physics convention (CW from above) — RotateVec3D's yaw
    // matches that, unlike the Y-only RotateY used by DrawPart.
    DrawObject3DRotated(tinted, carBodyCount, car->pos,
                        car->visPitch, car->rotation, car->visRoll);

    // Shadow: a rotated rectangle pinned to the road surface, not the car.
    // Separates from the car body when airborne so jumps read clearly.
    {
        float shadowY = TrackHeightAt(car->pos, car->currentSeg) - 0.18f;
        // Fade + shrink a touch with altitude above the ground for perspective.
        float alt = car->pos.y - shadowY - 0.02f;
        if (alt < 0) alt = 0;
        float shrink = 1.0f / (1.0f + alt * 0.1f);
        unsigned char a = (unsigned char)(140 * shrink);
        Color sc = (Color){0, 0, 0, a};
        float hx = 0.6f * shrink, hz = 1.0f * shrink;
        Vector3 c0 = RotateY((Vector3){-hx, 0, -hz}, -car->rotation);
        Vector3 c1 = RotateY((Vector3){ hx, 0, -hz}, -car->rotation);
        Vector3 c2 = RotateY((Vector3){ hx, 0,  hz}, -car->rotation);
        Vector3 c3 = RotateY((Vector3){-hx, 0,  hz}, -car->rotation);
        Vector3 p0 = { car->pos.x + c0.x, shadowY, car->pos.z + c0.z };
        Vector3 p1 = { car->pos.x + c1.x, shadowY, car->pos.z + c1.z };
        Vector3 p2 = { car->pos.x + c2.x, shadowY, car->pos.z + c2.z };
        Vector3 p3 = { car->pos.x + c3.x, shadowY, car->pos.z + c3.z };
        DrawTriangle3D(p0, p1, p2, sc);
        DrawTriangle3D(p0, p2, p3, sc);
        DrawTriangle3D(p0, p2, p1, sc);
        DrawTriangle3D(p0, p3, p2, sc);
    }

    // Drift smoke
    if (car->drifting && car->speed > 10) {
        float cs = cosf(car->rotation), sn = sinf(car->rotation);
        Vector3 rearPos = { car->pos.x - sn * 0.8f, car->pos.y - 0.1f, car->pos.z + cs * 0.8f };
        if (GetRandomValue(0, 2) == 0) SpawnDust(rearPos, (Color){200,200,200,150});
    }

    // Surface dust
    SurfaceType surf = GetSurface(car->pos);
    if (car->speed > 15 && surf != SURF_TARMAC) {
        float cs = cosf(car->rotation), sn = sinf(car->rotation);
        Vector3 rearPos = { car->pos.x - sn * 0.9f, car->pos.y - 0.1f, car->pos.z + cs * 0.9f };
        Color dustCol = (surf == SURF_GRAVEL) ? (Color){180,160,120,120} : (Color){100,80,50,120};
        if (GetRandomValue(0, 1) == 0) SpawnDust(rearPos, dustCol);
    }

}

int main(void) {
    InitWindow(800, 600, "Rally Racing");
    SetTargetFPS(144);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    MaximizeWindow();

    GenerateTrack();

    // Init cars
    Car cars[NUM_CARS] = {0};
    Color carColors[] = { {200,40,40,255}, {40,40,200,255}, {40,180,40,255}, {220,200,40,255} };
    for (int i = 0; i < NUM_CARS; i++) {
        float offset = ((float)i - (NUM_CARS - 1) * 0.5f) * 3.0f;
        cars[i].pos = Vector3Add(trackPts[0], Vector3Scale(trackNormals[0], offset));
        cars[i].pos.y = trackPts[0].y + 0.2f;
        cars[i].rotation = atan2f(trackDirs[0].x, trackDirs[0].z);
        cars[i].speed = 0;
        cars[i].lap = 0;
        cars[i].nextWP = 1;
        cars[i].currentSeg = 0;
        cars[i].velY = 0;
        cars[i].airborne = false;
        cars[i].prevSeg = 0;
        cars[i].rampCooldown = 0;
        cars[i].visRoll = 0;
        cars[i].visPitch = 0;
        cars[i].hasPrevWheels = false;
        cars[i].isPlayer = (i == 0);
        cars[i].finished = false;
        cars[i].drifting = false;
        cars[i].driftTime = 0;
        cars[i].color = carColors[i];
        cars[i].aiNoise = (float)GetRandomValue(-20, 20) / 100.0f;
        cars[i].steerInput = 0;
    }

    Camera3D camera = { 0 };
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 55;
    camera.projection = CAMERA_PERSPECTIVE;

    float countdown = 3.99f;
    bool raceStarted = false;
    int playerRank = 1;
    float bestLapTime = 999.0f;
    float lapTimer = 0;
    float raceTimer = 0;
    float splitTimes[TOTAL_LAPS] = {0};
    float finishCinTime = 0.0f;  // seconds since player crossed finish line

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        countdown -= dt;
        if (!raceStarted && countdown <= 0) raceStarted = true;
        if (raceStarted && !cars[0].finished) { raceTimer += dt; lapTimer += dt; }

        // --- Update cars ---
        for (int ci = 0; ci < NUM_CARS; ci++) {
            Car *car = &cars[ci];
            if (car->finished) continue;

            float accel = 0, steer = 0;
            SurfaceType surf = GetSurface(car->pos);
            float maxSpd = CAR_MAX_SPEED;
            float drag;
            float surfTurnMult = 1.0f;  // extra oversteer on loose surfaces
            switch (surf) {
                case SURF_TARMAC: drag = CAR_DRAG_ROAD; surfTurnMult = 1.0f; break;
                case SURF_GRAVEL: drag = CAR_DRAG_GRAVEL; maxSpd *= 0.95f; surfTurnMult = 1.4f; break;
                case SURF_MUD:    drag = CAR_DRAG_MUD; maxSpd *= 0.9f; surfTurnMult = 1.8f; break;
            }

            if (!raceStarted) goto moveCar;

            bool handbraking = false;
            if (car->isPlayer) {
                if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    accel = CAR_ACCEL;
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  accel = -CAR_BRAKE;
                if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  steer = CAR_TURN;
                if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) steer = -CAR_TURN;

                // Space held: either drift (when turning at speed) or
                // straight-line handbrake (when not).
                bool spaceHeld = IsKeyDown(KEY_SPACE);
                if (spaceHeld && fabsf(steer) > 0.1f && car->speed > 15.0f) {
                    // DRIFT: skid through the corner. W/S accel stays active
                    // so the player can throttle through the slide; drift
                    // drag + boosted turn do the work without bleeding speed.
                    car->drifting = true;
                    car->driftTime += dt;
                    steer *= CAR_DRIFT_MULT;
                    drag = CAR_DRIFT_DRAG;
                } else {
                    car->drifting = false;
                    car->driftTime = 0;
                    if (spaceHeld) handbraking = true;  // straight-line handbrake
                }
            } else {
                // AI
                Vector3 target = trackPts[car->nextWP];
                target = Vector3Add(target, Vector3Scale(trackNormals[car->nextWP], car->aiNoise * TRACK_WIDTH * 0.3f));
                Vector3 toTarget = Vector3Subtract(target, car->pos);
                toTarget.y = 0;
                float targetAngle = atan2f(toTarget.x, toTarget.z);
                float angleDiff = targetAngle - car->rotation;
                while (angleDiff > PI)  angleDiff -= 2*PI;
                while (angleDiff < -PI) angleDiff += 2*PI;
                steer = Clamp(angleDiff * 3.0f, -CAR_TURN, CAR_TURN);
                accel = CAR_ACCEL * 0.88f;

                // Rubber banding
                float playerProg = CarProgress(&cars[0]);
                float myProg = CarProgress(car);
                if (myProg > playerProg + 5) accel *= 0.65f;
                else if (myProg < playerProg - 5) accel *= 1.15f;

                // Brake for sharp corners
                if (fabsf(angleDiff) > 0.6f) accel *= 0.5f;
            }

            // No throttle / steering / drag while airborne — the car
            // coasts along its launch velocity until it lands.
            if (car->airborne) { accel = 0; steer = 0; drag = 1.0f; }

            // Physics
            if (handbraking) {
                // Handbrake decelerates toward 0 from either direction and
                // holds there — reverse requires S / Down.
                float dv = CAR_HANDBRAKE * dt;
                if      (car->speed >  dv) car->speed -= dv;
                else if (car->speed < -dv) car->speed += dv;
                else                       car->speed  = 0.0f;
            } else {
                car->speed += accel * dt;
            }
            car->speed *= drag;
            car->speed = Clamp(car->speed, -10.0f, maxSpd);
            car->steerInput = steer;

moveCar:;
            // Bicycle model: rear-wheel drive, front-wheel steering
            // No turning without speed — wheels must be rolling
            // Bicycle geometry: turn radius = wheelbase / tan(steerAngle).
            // With wheelbase=3.0 and maxSteerAngle=0.13 on tarmac, radius is
            // ~23 units — a proper rally car's turning circle. Drift (1.6×)
            // and loose-surface mult (1.4×–1.8×) tighten it meaningfully.
            float wheelbase = 3.0f;
            float maxSteerAngle = 0.13f * surfTurnMult;
            float steerAngle = steer / CAR_TURN * maxSteerAngle;  // normalize steer input
            if (car->speed < 0) steerAngle = -steerAngle;

            float cs = cosf(car->rotation), sn = sinf(car->rotation);

            if (fabsf(car->speed) > 0.5f) {
                // Rotation only updates while the wheels are on the road —
                // in the air the car holds its heading.
                if (!car->airborne) {
                    float angularVel = car->speed * tanf(steerAngle) / wheelbase;
                    car->rotation += angularVel * dt;
                }

                // Move the whole car forward in its (now updated) facing direction
                float newCs = cosf(car->rotation), newSn = sinf(car->rotation);
                car->pos.x += newSn * car->speed * dt;
                car->pos.z += newCs * car->speed * dt;
            }
            // Refresh currentSeg before reading TrackHeightAt so a
            // segment-boundary crossing doesn't alias targetY against the
            // old segment.
            car->prevSeg = car->currentSeg;
            car->currentSeg = NearestSegLocal(car->pos, car->currentSeg);

            // --- Vertical / airborne physics ---
            // Natural hills just follow the road — only the explicit ramps
            // launch the car. This keeps flat / rolling terrain stable.
            {
                float targetY = TrackHeightAt(car->pos, car->currentSeg);
                if (car->airborne) {
                    car->velY -= RALLY_GRAVITY * dt;
                    car->pos.y += car->velY * dt;
                    if (car->pos.y <= targetY) {
                        car->pos.y = targetY;
                        car->velY = 0.0f;
                        car->airborne = false;
                    }
                } else {
                    if (car->rampCooldown > 0.0f) car->rampCooldown -= dt;
                    bool justEnteredRamp =
                        (car->currentSeg != car->prevSeg) && IsRampSeg(car->currentSeg);
                    if (justEnteredRamp && fabsf(car->speed) > 15.0f
                        && car->rampCooldown <= 0.0f) {
                        car->airborne = true;
                        // Modest launch — speed-scaled with a firm cap so
                        // even top-speed hits stay in rally territory.
                        float launch = 4.0f + fabsf(car->speed) * 0.2f;
                        if (launch > 13.0f) launch = 13.0f;
                        car->velY = launch;
                        // 0.6s lock-out: stops re-launch if currentSeg
                        // re-crosses the ramp due to corner jitter.
                        car->rampCooldown = 0.6f;
                        car->pos.y = targetY;
                    } else {
                        // Glued to the road.
                        car->pos.y = targetY;
                        car->velY = 0.0f;
                    }
                }
            }

            // --- Visual lean / tilt (cosmetic, does not affect physics) ---
            {
                float steerMag  = car->steerInput / CAR_TURN;
                float speedFrac = fabsf(car->speed) / CAR_MAX_SPEED;
                // Subtle body lean — a real rally car rolls maybe 3–5° in a
                // hard corner. 0.22 rad at 1.8× drift mult was ~22°, enough
                // that the inside wheels lifted off the road visually.
                float targetRoll = -steerMag * speedFrac * 0.08f;
                if (car->drifting)  targetRoll *= 1.4f;
                if (car->airborne)  targetRoll *= 0.3f;

                float targetPitch = 0.0f;
                if (car->airborne) {
                    float v = car->velY;
                    if (v >  15.0f) v =  15.0f;
                    if (v < -15.0f) v = -15.0f;
                    targetPitch = -v * 0.025f;  // nose up while rising
                } else {
                    int next = (car->currentSeg + 1) % TRACK_SEGS;
                    float dx = XZDistance(trackPts[next], trackPts[car->currentSeg]);
                    float dy = trackPts[next].y - trackPts[car->currentSeg].y;
                    if (dx > 0.001f) targetPitch = -atan2f(dy, dx);
                }

                // Exponential-style smoothing so motion reads dynamic but stays framerate-safe.
                float rRate = 1.0f - expf(-10.0f * dt);
                float pRate = 1.0f - expf(-6.0f  * dt);
                car->visRoll  += (targetRoll  - car->visRoll)  * rRate;
                car->visPitch += (targetPitch - car->visPitch) * pRate;
            }

            // --- Skid marks: spawn rear-wheel streaks while drifting on ground. ---
            if (car->drifting && !car->airborne && fabsf(car->speed) > 5.0f) {
                // Rear-wheel body offsets (matches carBody RL / RR parts).
                // Y = car->pos.y - 0.18 puts the streak flush on the road
                // (car->pos.y = track+0.2, so track+0.02 = car->pos.y-0.18).
                Vector3 lBody = { -0.45f, 0, -0.7f };
                Vector3 rBody = {  0.45f, 0, -0.7f };
                Vector3 lWorld = Vector3Add(car->pos, RotateY(lBody, -car->rotation));
                Vector3 rWorld = Vector3Add(car->pos, RotateY(rBody, -car->rotation));
                lWorld.y = car->pos.y - 0.18f;
                rWorld.y = car->pos.y - 0.18f;
                if (car->hasPrevWheels) {
                    skids[skidIdx] = (Skid){ car->prevRearL, lWorld, true };
                    skidIdx = (skidIdx + 1) % MAX_SKIDS;
                    skids[skidIdx] = (Skid){ car->prevRearR, rWorld, true };
                    skidIdx = (skidIdx + 1) % MAX_SKIDS;
                }
                car->prevRearL = lWorld;
                car->prevRearR = rWorld;
                car->hasPrevWheels = true;
            } else {
                car->hasPrevWheels = false;
            }

            // Checkpoint crossing (player only): arm the next checkpoint
            // and record a split when the expected segment is entered.
            if (car->isPlayer && car->currentSeg != car->prevSeg
                && car->currentSeg == CHECKPOINT_SEGS[cpNextIdx]) {
                cpSplitTimes[cpNextIdx] = lapTimer;
                cpNextIdx = (cpNextIdx + 1) % NUM_CHECKPOINTS;
                cpFlashTimer = 1.2f;
            }

            // Track boundary: only push when the car is actually past the edge.
            // The previous implementation triggered at (edgeDist - 3), i.e. the
            // inner 40% of the track, so it was constantly nudging the car
            // toward centerline during normal driving — felt like teleporting.
            // Inside the track: total freedom. Past the edge: linear capped push.
            Vector3 closest = ClosestPointOnSeg(car->pos, car->currentSeg);
            Vector3 toCenter = Vector3Subtract(closest, car->pos);
            toCenter.y = 0;
            float trackDist = Vector3Length(toCenter);
            float edgeDist = TRACK_WIDTH / 2.0f;
            if (trackDist > edgeDist && trackDist > 0.01f) {
                Vector3 pushDir = Vector3Scale(toCenter, 1.0f / trackDist);
                float overEdge = trackDist - edgeDist;
                float pushStrength = 3.0f + overEdge * 4.0f;
                if (pushStrength > 10.0f) pushStrength = 10.0f;
                car->pos = Vector3Add(car->pos, Vector3Scale(pushDir, pushStrength * dt));
                car->speed *= (1.0f - 0.3f * dt);
            }

            // Tree collision
            for (int t = 0; t < numTrees; t++) {
                Vector3 diff = Vector3Subtract(car->pos, treePosns[t]);
                diff.y = 0;
                if (Vector3Length(diff) < treeSizes[t] * 0.5f + 0.8f) {
                    car->speed *= 0.3f;
                    Vector3 push = Vector3Normalize(diff);
                    car->pos = Vector3Add(car->pos, Vector3Scale(push, 0.5f));
                }
            }

            // Puddle splash: spawn blue dust while the car is passing through.
            for (int pu = 0; pu < numPuddles; pu++) {
                float dx = car->pos.x - puddles[pu].pos.x;
                float dz = car->pos.z - puddles[pu].pos.z;
                float r  = puddles[pu].radius + 0.4f;
                if (dx*dx + dz*dz < r*r && fabsf(car->speed) > 3.0f) {
                    if (GetRandomValue(0, 1) == 0) {
                        Vector3 sp = {
                            car->pos.x + ((float)GetRandomValue(-40,40))/100.0f,
                            puddles[pu].pos.y + 0.1f,
                            car->pos.z + ((float)GetRandomValue(-40,40))/100.0f,
                        };
                        SpawnDust(sp, (Color){130, 180, 220, 180});
                    }
                }
            }

            // Crashable prop collision — crates spin and tumble away
            // for a moment after the hit; fences just disintegrate.
            for (int pi = 0; pi < numProps; pi++) {
                Prop *pr = &props[pi];
                if (!pr->alive) continue;
                float dx = car->pos.x - pr->pos.x;
                float dz = car->pos.z - pr->pos.z;
                float hitR = (pr->type == PROP_FENCE) ? 1.1f : 0.55f;
                if (dx*dx + dz*dz < (hitR + 0.4f) * (hitR + 0.4f)) {
                    pr->alive = false;
                    car->speed *= (pr->type == PROP_FENCE) ? 0.90f : 0.82f;
                    if (pr->type == PROP_CRATE) {
                        // Fling the crate away from the car's direction of
                        // travel with some upward kick and a random spin.
                        float hx = -dx, hz = -dz;  // from car → prop
                        float mag = sqrtf(hx*hx + hz*hz);
                        if (mag > 1e-4f) { hx /= mag; hz /= mag; }
                        float shove = 6.0f + fabsf(car->speed) * 0.4f;
                        pr->vel = (Vector3){
                            hx * shove + ((float)GetRandomValue(-20,20))/20.0f,
                            5.0f + (float)GetRandomValue(0, 40) / 10.0f,
                            hz * shove + ((float)GetRandomValue(-20,20))/20.0f,
                        };
                        pr->angVel = ((float)GetRandomValue(-80, 80)) / 10.0f;
                        pr->flyTimer = 1.8f;
                    }
                    // Debris puff: brown for crates, lighter for fences.
                    Color debris = (pr->type == PROP_FENCE)
                        ? (Color){200, 200, 200, 180}
                        : (Color){150, 100,  60, 180};
                    for (int k = 0; k < 6; k++) {
                        SpawnDust((Vector3){pr->pos.x, pr->pos.y + 0.3f, pr->pos.z}, debris);
                    }
                }
            }

            // Integrate flying props (crates only).
            for (int pi = 0; pi < numProps; pi++) {
                Prop *pr = &props[pi];
                if (pr->flyTimer <= 0.0f) continue;
                pr->flyTimer -= dt;
                pr->vel.y -= 18.0f * dt;           // gravity
                pr->pos = Vector3Add(pr->pos, Vector3Scale(pr->vel, dt));
                pr->rotY += pr->angVel * dt;
                // Bounce off the ground once, then settle.
                float floorY = TrackHeightAt(pr->pos, car->currentSeg) - 0.2f;
                if (pr->pos.y < floorY + 0.1f && pr->vel.y < 0.0f) {
                    pr->pos.y = floorY + 0.1f;
                    pr->vel.y = -pr->vel.y * 0.35f;
                    pr->vel.x *= 0.6f; pr->vel.z *= 0.6f;
                    pr->angVel *= 0.5f;
                    if (fabsf(pr->vel.y) < 0.8f) pr->flyTimer = fminf(pr->flyTimer, 0.3f);
                }
            }

            // Waypoint progression
            float wpDist = Vector3Distance(car->pos, trackPts[car->nextWP]);
            if (wpDist < 10.0f) {
                int prev = car->nextWP;
                car->nextWP = (car->nextWP + 1) % TRACK_SEGS;
                if (car->nextWP == 0 && prev == TRACK_SEGS - 1) {
                    car->lap++;
                    if (car->isPlayer) {
                        if (lapTimer < bestLapTime) bestLapTime = lapTimer;
                        if (car->lap <= TOTAL_LAPS) splitTimes[car->lap - 1] = lapTimer;
                        lapTimer = 0;
                    }
                    if (car->lap >= TOTAL_LAPS) car->finished = true;
                }
            }
        }

        // Car-car collision
        for (int i = 0; i < NUM_CARS; i++) {
            for (int j = i + 1; j < NUM_CARS; j++) {
                Vector3 diff = Vector3Subtract(cars[i].pos, cars[j].pos);
                diff.y = 0;
                float dist = Vector3Length(diff);
                if (dist < 2.0f && dist > 0.01f) {
                    Vector3 push = Vector3Scale(Vector3Normalize(diff), (2.0f - dist) * 0.5f);
                    cars[i].pos = Vector3Add(cars[i].pos, push);
                    cars[j].pos = Vector3Subtract(cars[j].pos, push);
                }
            }
        }

        // Update dust
        for (int i = 0; i < MAX_DUST; i++) {
            if (!dust[i].active) continue;
            dust[i].pos = Vector3Add(dust[i].pos, Vector3Scale(dust[i].vel, dt));
            dust[i].vel.y -= 3.0f * dt;
            dust[i].life -= dt;
            if (dust[i].life <= 0) dust[i].active = false;
        }

        // Rankings
        playerRank = 1;
        for (int i = 1; i < NUM_CARS; i++)
            if (CarProgress(&cars[i]) > CarProgress(&cars[0])) playerRank++;

        // --- Camera ---
        if (cars[0].finished) {
            finishCinTime += dt;
            Car *p = &cars[0];
            float t = finishCinTime;

            // Stage 1 (0 – 3s): ease a 180° orbit around the car.
            // Stage 2 (3 – 6s): rise up and pull back into the sky.
            float orbitDur = 3.0f;
            float riseDur  = 3.0f;
            float dist   = 6.0f;
            float height = 2.5f;

            float u;
            if (t < orbitDur)        u = t / orbitDur;
            else                     u = 1.0f;
            u = u * u * (3.0f - 2.0f * u);      // smoothstep ease
            float orbitAngle = u * PI;           // 0 (behind) → π (in front)

            if (t > orbitDur) {
                float r = (t - orbitDur) / riseDur;
                if (r > 1.0f) r = 1.0f;
                r = r * r * (3.0f - 2.0f * r);
                height += r * 55.0f;
                dist   += r * 20.0f;
            }

            float theta = p->rotation + PI + orbitAngle;
            Vector3 camPos = {
                p->pos.x + sinf(theta) * dist,
                p->pos.y + height,
                p->pos.z + cosf(theta) * dist
            };
            Vector3 camTarget = { p->pos.x, p->pos.y + 0.8f, p->pos.z };
            // Ease into the cinematic from whatever the follow-cam had.
            camera.position = Vector3Lerp(camera.position, camPos,    6.0f * dt);
            camera.target   = Vector3Lerp(camera.target,   camTarget, 6.0f * dt);
            camera.fovy    += (52.0f - camera.fovy) * 3.0f * dt;
        } else {
            Car *p = &cars[0];
            float cs = cosf(p->rotation), sn = sinf(p->rotation);
            float speedPct = Clamp(fabsf(p->speed) / CAR_MAX_SPEED, 0, 1.2f);
            float camDist = 2.8f - speedPct * 0.6f;   // pulled right in behind the car
            float camH    = 1.4f - speedPct * 0.2f;
            Vector3 camPos = {
                p->pos.x - sn * camDist,
                p->pos.y + camH,
                p->pos.z - cs * camDist
            };
            Vector3 camTarget = {
                p->pos.x + sn * 2.0f,   // look less far past the car so it fills more of the frame
                p->pos.y + 0.5f,
                p->pos.z + cs * 2.0f
            };
            camera.position = Vector3Lerp(camera.position, camPos,    20.0f * dt);
            camera.target   = Vector3Lerp(camera.target,   camTarget, 20.0f * dt);

            float targetFov = 48.0f + speedPct * 12.0f;  // narrower base FOV = bigger car
            camera.fovy += (targetFov - camera.fovy) * 4.0f * dt;
        }

        // --- Draw ---
        BeginDrawing();
        // Sky gradient — deeper blue high, warmer band near horizon.
        ClearBackground((Color){75, 120, 180, 255});
        DrawRectangleGradientV(0, 0, sw, sh,
            (Color){ 60, 110, 185, 255},   // top
            (Color){200, 205, 195, 255});  // horizon

        BeginMode3D(camera);
            // Ground beneath track
            DrawPlane((Vector3){0, -0.02f, 0}, (Vector2){500, 500}, (Color){60, 100, 40, 255});

            DrawTrack();

            // Skid marks (on the road, below cars/particles so they read as decals)
            for (int i = 0; i < MAX_SKIDS; i++) {
                if (!skids[i].active) continue;
                Vector3 a = skids[i].a, b = skids[i].b;
                Vector3 dir = Vector3Subtract(b, a);
                dir.y = 0;
                float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                if (len < 0.001f) continue;
                Vector3 perp = { -dir.z / len * 0.12f, 0, dir.x / len * 0.12f };
                Vector3 p1 = Vector3Subtract(a, perp);
                Vector3 p2 = Vector3Add(a, perp);
                Vector3 p3 = Vector3Add(b, perp);
                Vector3 p4 = Vector3Subtract(b, perp);
                Color sc = (Color){ 20, 20, 20, 200 };
                DrawTriangle3D(p1, p2, p3, sc);
                DrawTriangle3D(p1, p3, p4, sc);
                DrawTriangle3D(p1, p3, p2, sc);
                DrawTriangle3D(p1, p4, p3, sc);
            }

            // Buildings
            for (int i = 0; i < numBuildings; i++) DrawBuilding(&buildings[i]);

            // Crashable props (fences + crates)
            for (int i = 0; i < numProps; i++) DrawProp(&props[i]);

            // Cheering fans — bob in time with raceTimer
            for (int i = 0; i < numFans; i++) DrawFan(&fans[i], raceTimer);

            // Checkpoint gates
            for (int i = 0; i < NUM_CHECKPOINTS; i++) DrawCheckpoint(CHECKPOINT_SEGS[i], i);

            // Puddles (flat blue discs flush with the road)
            for (int i = 0; i < numPuddles; i++) {
                Vector3 a = puddles[i].pos;
                Vector3 b = (Vector3){ a.x, a.y + 0.005f, a.z };
                DrawCylinderEx(a, b, puddles[i].radius, puddles[i].radius, 14,
                               (Color){40, 90, 140, 200});
                DrawCylinderWiresEx(a, b, puddles[i].radius, puddles[i].radius, 14,
                                    (Color){80, 140, 190, 200});
            }

            // Trees
            for (int i = 0; i < numTrees; i++) DrawTree(treePosns[i], treeSizes[i]);

            // Dust particles
            for (int i = 0; i < MAX_DUST; i++) {
                if (!dust[i].active) continue;
                float alpha = dust[i].life / 0.6f;
                Color dc = dust[i].color;
                dc.a = (unsigned char)(alpha * dc.a);
                DrawSphere(dust[i].pos, 0.1f + (1.0f - alpha) * 0.2f, dc);
            }

            // Cars (sort by distance from camera for rough depth)
            for (int ci = 0; ci < NUM_CARS; ci++) DrawCar(&cars[ci], ci);

        EndMode3D();

        // --- HUD ---
        // Speedometer (bottom-left, chunky panel)
        {
            int panelW = 280, panelH = 90;
            int px = 20, py = sh - panelH - 20;
            ArcPanel(px, py, panelW, panelH, (Color){0, 0, 0, 200}, (Color){0, 200, 255, 255});
            int kph = (int)(fabsf(cars[0].speed) * 3.6f);
            const char *speedTxt = TextFormat("%d", kph);
            // Right-align the mono digits inside a 3-digit slot.
            int speedSlot = 3 * ArcMonoCell(64);
            int speedW = ArcMonoWidth(speedTxt, 64);
            ArcTextMono(speedTxt, px + panelW - speedSlot - 70 + (speedSlot - speedW),
                        py + 12, 64, (Color){0, 220, 255, 255});
            ArcText("KM/H", px + panelW - 60, py + 40, 24, (Color){0, 180, 220, 255});

            // Surface label above speedometer
            SurfaceType surf = GetSurface(cars[0].pos);
            const char *surfName = (surf == SURF_TARMAC) ? "TARMAC" : (surf == SURF_GRAVEL) ? "GRAVEL" : "MUD";
            Color surfCol = (surf == SURF_TARMAC) ? (Color){180, 200, 220, 255} :
                            (surf == SURF_GRAVEL) ? (Color){230, 200, 140, 255} : (Color){180, 130, 70, 255};
            ArcText(surfName, px + 12, py - 32, 28, surfCol);
        }

        // Position (top-right, huge)
        const char *ordinals[] = {"1st", "2nd", "3rd", "4th"};
        Color posCol = (playerRank == 1) ? GOLD : (playerRank == 2) ? (Color){200,200,220,255} :
                       (playerRank == 3) ? (Color){200,130,60,255} : WHITE;
        int posSize = 88;
        int posW = MeasureText(ordinals[playerRank - 1], posSize);
        ArcTextOutlined(ordinals[playerRank - 1], sw - posW - 30, 15, posSize, posCol, BLACK);

        // Lap / timers (top-right under position)
        int dispLap = cars[0].lap + 1;
        if (dispLap > TOTAL_LAPS) dispLap = TOTAL_LAPS;
        const char *lapTxt = TextFormat("LAP %d/%d", dispLap, TOTAL_LAPS);
        int lapW = MeasureText(lapTxt, 40);
        ArcText(lapTxt, sw - lapW - 30, 115, 40, WHITE);

        int mins = (int)raceTimer / 60, secs = (int)raceTimer % 60;
        int ms = (int)(fmodf(raceTimer, 1.0f) * 100);
        const char *timeTxt = TextFormat("%d:%02d.%02d", mins, secs, ms);
        int timeW = ArcMonoWidth(timeTxt, 34);
        ArcTextMono(timeTxt, sw - timeW - 30, 160, 34, (Color){0, 220, 255, 255});

        // Split "LAP " (static) from the number (mono) so the fixed text
        // keeps its natural kerning while the digits don't shimmy.
        const char *lapNumTxt  = TextFormat("%.2f", lapTimer);
        int lapNumW  = ArcMonoWidth(lapNumTxt, 24);
        int lapLabelW = MeasureText("LAP ", 24);
        int lapTotalW = lapLabelW + lapNumW;
        ArcText    ("LAP ",     sw - lapTotalW - 30,             200, 24, (Color){220, 220, 230, 255});
        ArcTextMono(lapNumTxt,  sw - lapNumW - 30,               200, 24, (Color){220, 220, 230, 255});

        if (bestLapTime < 999.0f) {
            const char *bestNumTxt = TextFormat("%.2f", bestLapTime);
            int bestNumW  = ArcMonoWidth(bestNumTxt, 24);
            int bestLabelW = MeasureText("BEST ", 24);
            int bestTotalW = bestLabelW + bestNumW;
            ArcText    ("BEST ",    sw - bestTotalW - 30,            228, 24, YELLOW);
            ArcTextMono(bestNumTxt, sw - bestNumW - 30,              228, 24, YELLOW);
        }

        // Drift indicator (top-center)
        if (cars[0].drifting) {
            Color driftCol = (cars[0].driftTime > 1.5f) ? ORANGE : YELLOW;
            int dw = MeasureText("DRIFT!", 56);
            ArcTextOutlined("DRIFT!", sw / 2 - dw / 2, 20, 56, driftCol, BLACK);
        }

        // Checkpoint flash (shows the split just banked)
        if (cpFlashTimer > 0.0f) {
            cpFlashTimer -= dt;
            if (cpFlashTimer < 0.0f) cpFlashTimer = 0.0f;
            int prevIdx = (cpNextIdx + NUM_CHECKPOINTS - 1) % NUM_CHECKPOINTS;
            float alpha = cpFlashTimer / 1.2f;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
            Color c = (Color){255, 200, 80, (unsigned char)(alpha * 255)};
            const char *cp = TextFormat("CP %d   %.2f", prevIdx + 1, cpSplitTimes[prevIdx]);
            int w = MeasureText(cp, 40);
            ArcTextOutlined(cp, sw / 2 - w / 2, 100, 40, c, (Color){0,0,0,(unsigned char)(alpha*200)});
        }

        // Mini-map (bottom-right, auto-fit to track bounds)
        {
            int mmSize = 220, mmPad = 10;
            int mmx = sw - mmSize - 20, mmy = sh - mmSize - 20;
            ArcPanel(mmx, mmy, mmSize, mmSize, (Color){0, 0, 0, 180}, (Color){80, 180, 220, 255});

            // Compute track bounds so the map auto-fits regardless of track shape.
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (int i = 0; i < TRACK_SEGS; i++) {
                if (trackPts[i].x < minX) minX = trackPts[i].x;
                if (trackPts[i].x > maxX) maxX = trackPts[i].x;
                if (trackPts[i].z < minZ) minZ = trackPts[i].z;
                if (trackPts[i].z > maxZ) maxZ = trackPts[i].z;
            }
            float centerX = (minX + maxX) * 0.5f;
            float centerZ = (minZ + maxZ) * 0.5f;
            float halfExt = fmaxf((maxX - minX) * 0.5f, (maxZ - minZ) * 0.5f) + 8.0f;
            float mmScale = (mmSize - mmPad * 2) * 0.5f / halfExt;
            int cx = mmx + mmSize / 2, cy = mmy + mmSize / 2;

            for (int i = 0; i < TRACK_SEGS; i++) {
                int next = (i + 1) % TRACK_SEGS;
                Vector2 a = {cx + (trackPts[i].x    - centerX) * mmScale,
                             cy + (trackPts[i].z    - centerZ) * mmScale};
                Vector2 b = {cx + (trackPts[next].x - centerX) * mmScale,
                             cy + (trackPts[next].z - centerZ) * mmScale};
                Color lineCol;
                switch (trackSurface[i]) {
                    case SURF_TARMAC: lineCol = (Color){120, 120, 140, 240}; break;
                    case SURF_GRAVEL: lineCol = (Color){190, 170, 120, 240}; break;
                    case SURF_MUD:    lineCol = (Color){140, 100, 60, 240};  break;
                }
                DrawLineEx(a, b, 3.0f, lineCol);
            }
            for (int ci = 0; ci < NUM_CARS; ci++) {
                float mx = cx + (cars[ci].pos.x - centerX) * mmScale;
                float mz = cy + (cars[ci].pos.z - centerZ) * mmScale;
                DrawCircle(mx, mz, cars[ci].isPlayer ? 6 : 4, cars[ci].color);
                if (cars[ci].isPlayer)
                    DrawCircleLines(mx, mz, 9, WHITE);
            }
        }

        // Countdown (center, huge)
        if (countdown > 0 && !raceStarted) {
            int cd = (int)ceilf(countdown);
            if (cd > 0) {
                const char *cdText = TextFormat("%d", cd);
                int cdSize = 220;
                // Scale pulses with fraction — big on each whole-second tick
                float frac = countdown - floorf(countdown);
                float pulse = 1.0f + (1.0f - frac) * 0.25f;
                int size = (int)(cdSize * pulse);
                int cdw = MeasureText(cdText, size);
                Color col = (cd == 1) ? YELLOW : WHITE;
                ArcTextOutlined(cdText, sw / 2 - cdw / 2, sh / 2 - size / 2, size, col, BLACK);
            }
        }
        if (raceStarted && countdown > -0.8f) {
            int gow = MeasureText("GO!", 220);
            ArcTextOutlined("GO!", sw / 2 - gow / 2, sh / 2 - 110, 220, GREEN, BLACK);
        }

        // Finish screen
        if (cars[0].finished) {
            DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 120});
            ArcPanel(sw / 2 - 320, sh / 2 - 140, 640, 280,
                     (Color){10, 15, 30, 230}, (Color){255, 215, 0, 255});

            const char *finTxt = TextFormat("FINISH  %s", ordinals[playerRank - 1]);
            int fw = MeasureText(finTxt, 72);
            ArcTextOutlined(finTxt, sw / 2 - fw / 2, sh / 2 - 110, 72, GOLD, BLACK);

            const char *tTxt = TextFormat("TIME  %d:%02d.%02d", mins, secs, ms);
            int tw = MeasureText(tTxt, 40);
            ArcText(tTxt, sw / 2 - tw / 2, sh / 2 - 10, 40, WHITE);

            if (bestLapTime < 999.0f) {
                const char *bTxt = TextFormat("BEST LAP  %.2f", bestLapTime);
                int bw = MeasureText(bTxt, 36);
                ArcText(bTxt, sw / 2 - bw / 2, sh / 2 + 40, 36, YELLOW);
            }
        }

        // Help text and FPS (more visible)
        ArcText("WASD: DRIVE   SPACE+TURN: BRAKE / DRIFT",
                20, sh - 22, 16, (Color){140, 140, 170, 230});
        // FPS also mono so the counter doesn't shimmy.
        {
            const char *fpsTxt = TextFormat("%d", GetFPS());
            int fpsNumW = ArcMonoWidth(fpsTxt, 22);
            ArcTextMono(fpsTxt, 20, 15, 22, GREEN);
            ArcText(" FPS", 20 + fpsNumW, 15, 22, GREEN);
        }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
